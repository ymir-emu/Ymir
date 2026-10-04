#include <ymir/hw/vdp/renderer/vdp_renderer_hw_d3d12.hpp>

#include <ymir/hw/vdp/renderer/common/vdp1_steppers.hpp>

#include "common_hw/common_hw_defs.hpp"
#include "common_hw/memory_usage_tracker.hpp"

#include "d3d12/d3d12_barrier_tracker.hpp"
#include "d3d12/d3d12_dev_log.hpp"
#include "d3d12/d3d12_upload_buffer.hpp"

#include <ymir/gpu/d3d12/d3d12_commands.hpp>
#include <ymir/gpu/d3d12/d3d12_descriptor_heap.hpp>
#include <ymir/gpu/d3d12/d3d12_descriptor_heap_allocator.hpp>
#include <ymir/gpu/d3d12/d3d12_device.hpp>
#include <ymir/gpu/d3d12/d3d12_fence.hpp>
#include <ymir/gpu/d3d12/d3d12_pipeline_state.hpp>
#include <ymir/gpu/d3d12/d3d12_resource.hpp>
#include <ymir/gpu/d3d12/d3d12_root_signature.hpp>
#include <ymir/gpu/d3d12/d3d12_utils.hpp>

#include <ymir/gpu/shaders/gpu_shaders.hpp>

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/dev_assert.hpp>
#include <ymir/util/dev_log.hpp>
#include <ymir/util/dirty_bitmap.hpp>
#include <ymir/util/scope_guard.hpp>
#include <ymir/util/string.hpp>

#include <d3d12.h>

#include <fmt/format.h>

#include <array>
#include <cassert>
#include <chrono>
#include <concepts>
#include <vector>

using namespace ymir::gpu::d3d12;

namespace ymir::vdp {

namespace static_config {

    /// @brief Selects the method for copying the composite output texture to the frontend.
    /// `false` copies only the display region (HRes by VRes).
    /// `true` copies the entire resource.
    static constexpr bool copyFullCompositeResource = false;

} // namespace static_config

// ---------------------------------------------------------------------------------------------------------------------
// TODO: these could be useful in multiple backends. Make them generic and reusable, and move to a shared header.

/// @brief Packs up boolean values into the least significant bits of an unsigned integer.
/// @tparam T the unsigned integral type
/// @param[in] bools the bools to pack
/// @return the packed value
template <std::unsigned_integral T>
static uint32 PackBools(std::span<const bool> bools) {
    T value = 0;
    size_t count = std::min(bools.size(), sizeof(T) * 8);
    for (size_t i = 0; i < count; ++i) {
        if (bools[i]) {
            value |= static_cast<T>(1u) << static_cast<T>(i);
        }
    }
    return value;
}

/// @brief Maximum number of frames in flight.
static constexpr size_t kNumFrames = 3;

/// @brief Size of the upload buffers, in bytes.
/// Should be large enough to fit multiple worst case single transfers, but not waste space needlessly.
static constexpr UINT64 kUploadBufferSize = 16 * 1024 * 1024;

// ---------------------------------------------------------------------------------------------------------------------

/// @brief Converts the given shader into a `D3D12_SHADER_BYTECODE` structure.
/// @tparam stage the shader stage
/// @param[in] shader the compiler shader
/// @return a `D3D12_SHADER_BYTECODE` with a reference to the shader's bytecode
template <gpu::ShaderStage stage>
D3D12_SHADER_BYTECODE ToShaderBytecode(const gpu::CompiledShader<stage> &shader) {
    return D3D12_SHADER_BYTECODE{
        .pShaderBytecode = shader.bytecode.data(),
        .BytecodeLength = shader.bytecode.size(),
    };
}

/// @brief Manages an online copy of offline descriptors.
class DescriptorTable {
public:
    /// @brief Maximum number of handles to manage.
    /// Adjust as needed to accomodate the largest set of descriptor in use by any one descriptor range.
    static constexpr std::size_t kMaxSources = 9;

    DescriptorTable() = default;
    DescriptorTable(const DescriptorTable &) = delete;

    /// @brief Binds to the specified source descriptors.
    /// @tparam ...TSources the source descriptor types
    /// @param[in] ...srcs the source descriptors to bind
    template <std::convertible_to<const DescriptorRange *>... TSources>
    void Bind(TSources... srcs) {
        static_assert(sizeof...(TSources) <= kMaxSources, "Too many descriptor sources");
        m_srcs = {static_cast<const DescriptorRange *>(srcs)...};
        m_count = sizeof...(TSources);
    }

    /// @brief Retrieves the online descriptor range.
    /// @return the online descriptor range
    const DescriptorRange &Descriptors() const {
        return m_descs;
    }

    /// @brief Creates a descriptor range with a copy of the bound source descriptors in the specified heap.
    /// @param[in] device the device that owns the descriptors
    /// @param[in] heapAlloc the heap allocator
    /// @param[in] heapType the heap type
    /// @return `true` if successful, `false` if allocation failed
    bool Rebuild(D3D12Device &device, DescriptorHeapAllocator &heapAlloc, D3D12_DESCRIPTOR_HEAP_TYPE heapType) {
        if (m_count == 0) {
            return true;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE srcHandles[kMaxSources];
        UINT srcSizes[kMaxSources];
        UINT total = 0;
        for (size_t i = 0; i < m_count; ++i) {
            srcHandles[i] = m_srcs[i]->cpuHandle;
            srcSizes[i] = m_srcs[i]->count;
            total += srcSizes[i];
        }

        DescriptorRange dstDescs{};
        if (!heapAlloc.Allocate(dstDescs, total)) {
            return false;
        }

        device->CopyDescriptors(1, &dstDescs.cpuHandle, &total, m_count, srcHandles, srcSizes, heapType);
        m_descs = dstDescs;
        return true;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(UINT index = 0) const {
        return m_descs.GetCPUHandle(index);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle(UINT index = 0) const {
        return m_descs.GetGPUHandle(index);
    }

private:
    std::array<const DescriptorRange *, kMaxSources> m_srcs{};
    std::size_t m_count = 0;
    DescriptorRange m_descs{};
};

// ---------------------------------------------------------------------------------------------------------------------

struct Direct3D12VDPRenderer::Impl {
    Impl(const Direct3D12RendererCallbacks &hwCallbacks, VDPState &state,
         const config::VDP2AccessPatternsConfig &vdp2AccessPatternsConfig,
         const config::VDP2DebugRender &vdp2DebugRenderOptions, const config::Enhancements &enhancements)
        : vdpState(state)
        , enhancements(enhancements)
        , hwCallbacks(hwCallbacks)
        , vdp2(vdp2AccessPatternsConfig, vdp2DebugRenderOptions) {}

    VDPState &vdpState;
    const config::Enhancements &enhancements;

    struct ResolutionScaling {
        /// @brief Time point when resolution scaling parameters can be updated.
        /// Used to rate limit certain changes that could be flooded from the frontend.
        std::chrono::steady_clock::time_point nextUpdate{};

        /// @brief Latest applied resolution scaling parameters.
        /// Updated by `UpdateResolutionScaling()` when `nextResScaleUpdate` is reached.
        struct Parameters {
            bool enabled = false;
            bool scaleToTargetRes = false;
            uint32 factor = 2u;
            uint32 width = kDefaultResH;
            uint32 height = kDefaultResV;
        } params;

        /// @brief Resolution scaling dimensions currently applied to resources.
        /// Clamped to 704x512..8192x4096
        Dimensions current{kMaxResH, kMaxResV};

        /// @brief Desired resolution scaling dimensions to be applied to resources.
        /// Clamped to 704x512..8192x4096
        Dimensions desired{kMaxResH, kMaxResV};

        /// @brief Current scaled display resolution, passed to shaders.
        /// Clamped to 320x224..8192x4096
        Dimensions display{kDefaultResH, kDefaultResV};
    } resScale;

    const Direct3D12RendererCallbacks &hwCallbacks;

    D3D12Device device;

    struct Features {
        bool enhancedBarriers = false;
    } features;

    D3D12CommandQueue cmdQueue;
    D3D12Fence computeFence;

    D3D12DescriptorHeap offlineHeap;
    DescriptorHeapAllocator offlineHeapAlloc;

    D3D12DescriptorHeap resourceHeap;
    DescriptorHeapAllocator resourceHeapAlloc;

    // =================================================================================================================
    // VDP1 rendering

    struct VDP1Resources {
        VDP1Resources() {
            memset(&cpuCommonRenderParams, 0, sizeof(cpuCommonRenderParams));
            memset(&cpuEraseParams, 0, sizeof(cpuEraseParams));
            memset(&cpuPolyDrawParams, 0, sizeof(cpuPolyDrawParams));
        }

        // VDP1 VRAM is exposed as a ByteAddressBuffer to shaders as they often need to access raw bytes in 8-bit and
        // 16-bit units.

        /// @brief VRAM buffer.
        D3D12Resource vramBuffer;
        /// @brief VRAM buffer SRV (offline).
        DescriptorRange vramSRV;

        /// @brief Bit shift for the granularity for VRAM dirty bitmap chunks.
        static constexpr size_t kVRAMDirtyBitmapChunkSizeShift = 8;

        /// @brief Granularity for VRAM dirty bitmap chunks, in bytes.
        static constexpr size_t kVRAMDirtyBitmapChunkSize = static_cast<size_t>(1) << kVRAMDirtyBitmapChunkSizeShift;

        /// @brief Number of bits in the VRAM dirty bitmap.
        static constexpr size_t kVRAMDirtyBitmapSize = kVDP1VRAMSize / kVRAMDirtyBitmapChunkSize;

        // D3D12 buffer transfers must be done in multiples of 4 bytes.
        // The chunk must not be larger than VDP1 VRAM itself. In fact, it shouldn't be too large as it wastes memory
        // and time with unnecessary copies of VRAM data.
        static_assert(kVRAMDirtyBitmapChunkSize >= sizeof(uint32) && kVRAMDirtyBitmapChunkSize <= kVDP1VRAMSize,
                      "VDP1 VRAM upload chunk size is out of range");

        /// @brief VRAM dirty bitmap.
        util::DirtyBitmap<kVRAMDirtyBitmapSize> vramDirty;
        /// @brief Tracks VRAM usage by textures in a batch of spans.
        MemoryUsageTracker<kVDP1VRAMSize> vramTexUsageTracker;

        // The VDP1 FBRAM buffer contains four FBRAM-sized buffers to accomodate the outputs of the enhancements.
        // The buffers are indexed as follows:
        //   [0] Main field
        //   [1] Alternate field (deinterlace)
        //   [2] Main mesh field
        //   [3] Alternate mesh field

        /// @brief FBRAM buffer.
        D3D12Resource fbramBuffer;
        /// @brief FBRAM buffer SRV (offline).
        DescriptorRange fbramSRV;
        /// @brief FBRAM buffer UAV (offline).
        DescriptorRange fbramUAV;

        /// @brief Scaled FBRAM buffer.
        D3D12Resource fbramScaledBuffer;
        /// @brief Scaled FBRAM buffer SRV (offline).
        DescriptorRange fbramScaledSRV;
        /// @brief Scaled FBRAM buffer UAV (offline).
        DescriptorRange fbramScaledUAV;

        /// @brief FBRAM dirty bitmap.
        util::DirtyBitmap<kVDP1FBRAMSize> fbramDirty;
        /// @brief FBRAM writes buffer.
        D3D12Resource fbramWritesBuffer;
        /// @brief FBRAM writes buffer SRV (offline).
        DescriptorRange fbramWritesSRV;

        /// @brief FBRAM download buffer.
        D3D12Resource fbramDownloadBuffer;
        /// @brief Permanently mapped view of the FBRAM download buffer.
        void *fbramDownloadBufferPtr = nullptr;
        /// @brief Set by `VDP1DebugSyncFB` to lazily sync FBRAM when convenient.
        std::atomic_bool fbramDebugSyncRequest{false};
        /// @brief Set to `true` when submitting spans to indicate that FBRAM contents may have been modified by the
        /// GPU, requiring a download to synchronize the readback buffer.
        bool fbramReadbackDirty = false;
        /// @brief Whether the FBRAM readback buffer has new content to copy. Set when the FBRAM download command is
        /// submitted.
        uint32 fbramReadbackCopyPending = false;

        // ---------------------------------------------------------------------

        /// @brief Common rendering parameters, uploaded as 32-bit root constants.
        VDP1CommonRenderParams cpuCommonRenderParams{};

        /// @brief Erase parameters, uploaded as 32-bit root constants.
        VDP1EraseParams cpuEraseParams{};

        /// @brief Polygon drawing parameters, uploaded as 32-bit root constants.
        VDP1PolyDrawParams cpuPolyDrawParams{};

        /// @brief Compute shader for writing to the framebuffer.
        gpu::ComputeShader fbramWriteShader;
        /// @brief Root signature for writing to the framebuffer.
        D3D12RootSignature fbramWriteRootSig;
        /// @brief Descriptor range for writing to the framebuffer.
        DescriptorTable fbramWriteDescs;
        /// @brief Pipeline state object for writing to the framebuffer.
        D3D12PipelineState fbramWritePSO;

        /// @brief Compute shader for erasing the framebuffer.
        gpu::ComputeShader eraseShader;
        /// @brief Root signature for erasing the framebuffer.
        D3D12RootSignature eraseRootSig;

        /// @brief Internal sprite data output buffer.
        D3D12Resource internalSpriteOutBuffer;
        /// @brief Internal sprite data output buffer UAV (offline).
        DescriptorRange internalSpriteOutUAV;

        // The polygon drawing shader operates on consecutive polygons span batches that share the same properties:
        // - System and user clipping areas
        // - CMDPMOD, CMDCOLR, CMDSRCA and CMDSIZE values
        // - Shader specializations:
        //   - Checkerboard vs. transparent meshes
        //   - Color blending mode:
        //     - Copy: Replace and Half-Luminance modes
        //     - Right shift: Shadow mode
        //     - OIT: Half-Transparency mode
        //     - MSB: MSB mode

        /// @brief Compute shaders for drawing polygons.
        std::array<gpu::ComputeShader, 2 * 4> polyDrawShaders;
        /// @brief Root signature for drawing polygons.
        /// Applies to all non-OIT variants of the polygon drawing shader.
        D3D12RootSignature polyDrawRootSig;
        /// @brief Root signature for drawing polygons.
        /// Applies to the OIT variant of the polygon drawing shader.
        D3D12RootSignature polyDrawOITRootSig;

        // The polygon output merger shader applies the output of the polygon drawing shader to FBRAM, operating on
        // 32-bit values at a time.
        // Shader specializations:
        // - Checkerboard vs. transparent meshes
        // - Merging mode:
        //   - Copy: Replace and Half-Luminance modes
        //   - Right shift: Shadow mode
        //   - OIT: Half-Transparency mode

        /// @brief Compute shaders for merging polygon outputs.
        std::array<gpu::ComputeShader, 2 * 3> outputMergerShaders;
        /// @brief Root signature for merging polygon outputs (non-OIT variants).
        D3D12RootSignature outputMergerRootSig;
        /// @brief Root signature for merging polygon outputs (OIT variant only).
        D3D12RootSignature outputMergerOITRootSig;

        // ---------------------------------------------------------------------
        // Rendering state

        // Currently active polygon drawing shader
        size_t currPolyDrawShaderIndex = -1;
        // Currently active output merger shader
        size_t currOutputMergerShaderIndex = -1;
        // Set to true if any transparent mesh polygon was drawn
        bool transparentMeshDrawn = false;
        // Whether to double vertical coordinates when drawing VDP1 sprites
        bool doubleV = false;
    } vdp1;

    /// @brief Constructs a polygon drawing shader index from its variant options.
    /// @param[in] mode polygon drawing mode
    /// @return the shader index
    size_t MakeVDP1PolyDrawShaderIndex(VDP1Command::DrawMode mode) const {
        size_t value = 0;
        bit::deposit_into<0>(value, enhancements.transparentMeshes);
        if (mode.msbOn) {
            // MSB -> mode 3 (MSB)
            bit::deposit_into<1, 2>(value, 3);
        } else if (vdpState.regs1.pixel8Bits) {
            // 8-bit sprite data -> mode 0 (Copy) -- shading modes not supported
            bit::deposit_into<1, 2>(value, 0);
        } else if (mode.colorCalcBits == 3) {
            // Half-Transparency -> mode 2 (OIT)
            bit::deposit_into<1, 2>(value, 2);
        } else if (mode.colorCalcBits == 1) {
            // Shadow -> mode 1 (Shift)
            bit::deposit_into<1, 2>(value, 1);
        }
        // Other modes -> mode 0 (Copy)
        return value;
    }

    struct PolyDrawShaderIndex {
        size_t meshMode;
        size_t shadingMode;
    };

    /// @brief Expands a bit-packed VDP1 polygon drawing shader index into its components.
    /// @param[in] index the shader index
    /// @return the index's components
    PolyDrawShaderIndex ExpandVDP1PolyDrawShaderIndex(size_t index) {
        return {
            .meshMode = bit::extract<0>(index),
            .shadingMode = bit::extract<1, 2>(index),
        };
    }

    /// @brief Determines if the given polygon drawing shader variant uses OIT shading mode.
    /// @param[in] index the shader index
    /// @return `true` if the shader uses OIT shading mode, `false` otherwise
    bool IsVDP1PolyDrawShaderOIT(size_t index) {
        return bit::extract<1, 2>(index) == 2;
    }

    /// @brief Determines if the given polygon drawing shader variant uses MSB shading mode.
    /// @param[in] index the shader index
    /// @return `true` if the shader uses MSB shading mode, `false` otherwise
    bool IsVDP1PolyDrawShaderMSB(size_t index) {
        return bit::extract<1, 2>(index) == 3;
    }

    /// @brief Retrieves the name of a polygon drawing shader variant.
    /// @param[in] index the shader index
    /// @return the shader variant name of the form "Ms#-Sh#"
    std::string GetVDP1PolyDrawShaderVariantName(size_t index) {
        const PolyDrawShaderIndex components = ExpandVDP1PolyDrawShaderIndex(index);
        return fmt::format("Ms{}-Sh{}", components.meshMode, components.shadingMode);
    }

    /// @brief Constructs an output merger shader index from its variant options.
    /// @param[in] mode polygon drawing mode
    /// @return the shader index
    size_t MakeVDP1OutputMergerShaderIndex(VDP1Command::DrawMode mode) const {
        size_t value = 0;
        bit::deposit_into<0>(value, enhancements.transparentMeshes);
        if (vdpState.regs1.pixel8Bits) {
            // 8-bit sprite data -> mode 0 (Copy) -- shading modes not supported
            bit::deposit_into<1, 2>(value, 0);
        } else if (mode.colorCalcBits == 1) {
            // Shadow -> mode 1
            bit::deposit_into<1, 2>(value, 1);
        } else if (mode.colorCalcBits == 3) {
            // Half-Luminance -> mode 2
            bit::deposit_into<1, 2>(value, 2);
        }
        // Other modes -> mode 0
        // MSB doesn't invoke this shader.
        return value;
    }

    struct OutputMergerShaderIndex {
        size_t meshMode;
        size_t mergeMode;
    };

    /// @brief Expands a bit-packed VDP1 output merger shader index into its components.
    /// @param[in] index the shader index
    /// @return the index's components
    OutputMergerShaderIndex ExpandVDP1OutputMergerShaderIndex(size_t index) {
        return {
            .meshMode = bit::extract<0>(index),
            .mergeMode = bit::extract<1, 2>(index),
        };
    }

    /// @brief Determines if the given output merger shader variant uses OIT merging mode.
    /// @param[in] index the shader index
    /// @return `true` if the shader uses OIT merging mode, `false` otherwise
    bool IsVDP1OutputMergerShaderOIT(size_t index) {
        return bit::extract<1, 2>(index) == 2;
    }

    /// @brief Retrieves the name of an output merger shader variant.
    /// @param[in] index the shader index
    /// @return the shader variant name of the form "Ms#-Mg#"
    std::string GetVDP1OutputMergerShaderVariantName(size_t index) {
        const OutputMergerShaderIndex components = ExpandVDP1OutputMergerShaderIndex(index);
        return fmt::format("Ms{}-Mg{}", components.meshMode, components.mergeMode);
    }

    // =================================================================================================================
    // VDP2 rendering

    struct VDP2Resources {
        VDP2Resources(const config::VDP2AccessPatternsConfig &accessPatternsConfig,
                      const config::VDP2DebugRender &debugRenderOptions)
            : accessPatternsConfig(accessPatternsConfig)
            , debugRenderOptions(debugRenderOptions) {}

        // VDP2 VRAM is exposed as a ByteAddressBuffer to shaders as they often need to access raw bytes in 8-bit,
        // 16-bit and 32-bit units.

        /// @brief VRAM data buffer.
        D3D12Resource vramBuffer;
        /// @brief VRAM data buffer SRV (offline).
        DescriptorRange vramSRV;

        /// @brief Bit shift for the granularity for VRAM dirty bitmap chunks.
        static constexpr size_t kVRAMDirtyBitmapChunkSizeShift = 8;

        /// @brief Granularity for VRAM dirty bitmap chunks, in bytes.
        static constexpr size_t kVRAMDirtyBitmapChunkSize = static_cast<size_t>(1) << kVRAMDirtyBitmapChunkSizeShift;

        /// @brief Number of bits in the VRAM dirty bitmap.
        static constexpr size_t kVRAMDirtyBitmapSize = kVDP2VRAMSize / kVRAMDirtyBitmapChunkSize;

        // D3D12 buffer transfers must be done in multiples of 4 bytes.
        // The chunk must not be larger than VDP2 VRAM itself. In fact, it shouldn't be too large as it wastes memory
        // and time with unnecessary copies of VRAM data.
        static_assert(kVRAMDirtyBitmapChunkSize >= sizeof(uint32) && kVRAMDirtyBitmapChunkSize <= kVDP2VRAMSize,
                      "VDP2 VRAM upload chunk size is out of range");

        /// @brief VRAM dirty bitmap.
        util::DirtyBitmap<kVRAMDirtyBitmapSize> vramDirty;

        // VDP2 CRAM is not directly exposed. Instead, shaders get two convenient views:
        // - CRAM converted to R8G8B8A8 colors based on the current color RAM mode
        // - Top half of raw CRAM bytes, for rotation coefficients

        /// @brief CPU-side CRAM color buffer.
        CRAMColorCache cpuCRAMColorCache{};

        /// @brief Current CRAM generation (dirty tracking).
        uint32 cramGeneration = 0;

        /// @brief CPU-side LNCL/BACK screen buffer (0=LNCL; 1=BACK).
        std::array<std::array<ColorR8G8B8A8, kMaxResV>, 2> cpuLnclBack{};

        /// @brief CPU-side VDP2 rotation parameter base values.
        std::array<VDP2RotParamBase, kMaxNormalResV * 2> cpuRotParamBases{};

        /// @brief 2D texture for the composited VDP2 output.
        /// This cannot be instantiated per frame because interlaced graphics are weaved into the same output frame.
        D3D12Resource compositeOutTexture;
        /// @brief Composited VDP2 output UAV (offline).
        DescriptorRange compositeOutUAV;

        // ---------------------------------------------------------------------

        /// @brief Common rendering parameters, uploaded as 32-bit root constants.
        VDP2CommonRenderParams cpuCommonRenderParams{};

        /// @brief CPU-side layer rendering parameters.
        VDP2LayerRenderParams cpuLayerRenderParams{};

        /// @brief CPU-side layer composition parameters.
        VDP2ComposeParams cpuComposeParams{};

        /// @brief Compute shader for drawing the sprite layer.
        gpu::ComputeShader drawSpriteShader;
        /// @brief Root signature for drawing the sprite layer.
        D3D12RootSignature drawSpriteRootSig;

        /// @brief Compute shader for drawing background layers.
        gpu::ComputeShader drawBGsShader;
        /// @brief Root signature for drawing background layers.
        D3D12RootSignature drawBGsRootSig;

        /// @brief Compute shader for compositing layers.
        gpu::ComputeShader composeShader;
        /// @brief Root signature for compositing layers.
        D3D12RootSignature composeRootSig;

        // ---------------------------------------------------------------------
        // Rendering state

        uint32 nextLayerRenderLine = 0;
        uint32 nextComposeLine = 0;

        uint32 layerRenderParamsGeneration = 0;
        uint32 composeParamsGeneration = 0;

        const config::VDP2AccessPatternsConfig &accessPatternsConfig;
        const config::VDP2DebugRender &debugRenderOptions;
    } vdp2;

    // =================================================================================================================
    // Per-frame and shared resources

    BarrierTracker barrierTracker;

    struct DescToDelete {
        UINT baseIndex;
        UINT count;

        explicit DescToDelete(const DescriptorRange &desc)
            : baseIndex(desc.baseIndex)
            , count(desc.count) {}
    };

    struct DeleteQueues {
        std::vector<DescToDelete> offlineDescs;
        std::vector<DescToDelete> onlineDescs;
        std::vector<wil::com_ptr_nothrow<ID3D12Resource>> resources;
    } deleteQueues;

    /// @brief Resources for a single frame.
    struct FrameContext {
        D3D12CommandAllocator cmdAlloc;
        UINT64 signaledValue = 0; // fence value associated with this frame. 0 means never used

        // -------------------------------------------------------------------------------------------------------------
        // VDP1

        /// @brief Span parameters buffer.
        D3D12Resource spanParamsBuffer;
        /// @brief Span parameters buffer SRV (offline).
        DescriptorRange spanParamsSRV;

        /// @brief Span prefix sum buffer.
        D3D12Resource spanPrefixSumsBuffer;
        /// @brief Span prefix sum buffer SRV (offline).
        DescriptorRange spanPrefixSumsSRV;

        /// @brief CPU-side span parameters buffer.
        std::array<VDP1SpanParams, kMaxVDP1Spans> cpuSpanParams{};
        /// @brief CPU-side span prefix sums buffer.
        /// The first entry is always 0 to simplify implementation.
        std::array<HLSLuint, kMaxVDP1Spans + 1> cpuSpanPrefixSums{};
        /// @brief Number of spans allocated so far.
        size_t cpuSpanCount = 0;

        /// @brief Command parameters buffer.
        D3D12Resource cmdParamsBuffer;
        /// @brief Command parameters buffer SRV (offline).
        DescriptorRange cmdParamsSRV;
        /// @brief CPU-side command parameters buffer.
        std::array<VDP1CommandParams, kMaxVDP1Commands> cpuCmdParams{};
        /// @brief Number of commands allocated so far.
        size_t cpuCmdCount = 0;

        /// @brief Internal OIT fragments list heads buffer.
        D3D12Resource oitListHeadsBuffer;
        /// @brief Internal OIT fragments list heads UAV (offline).
        DescriptorRange oitListHeadsUAV;

        /// @brief Internal OIT fragment nodes buffer.
        D3D12Resource oitFragmentsBuffer;
        /// @brief Internal OIT fragment nodes SRV (offline).
        DescriptorRange oitFragmentsSRV;
        /// @brief Internal OIT fragment nodes UAV (offline).
        DescriptorRange oitFragmentsUAV;

        /// @brief Internal OIT counter buffer.
        D3D12Resource oitCounterBuffer;
        /// @brief Internal OIT counter UAV (offline).
        DescriptorRange oitCounterUAV;

        /// @brief Descriptor range for erasing the framebuffer.
        DescriptorTable eraseDescs;
        /// @brief Pipeline state object for erasing the framebuffer.
        D3D12PipelineState erasePSO;

        /// @brief Descriptor range for drawing polygons (Copy and Shift variants).
        DescriptorTable polyDrawDescs;
        /// @brief Descriptor range for drawing polygons (OIT variants).
        DescriptorTable polyDrawOITDescs;
        /// @brief Descriptor range for drawing polygons (MSB variants).
        DescriptorTable polyDrawMSBDescs;
        /// @brief Pipeline state objects for drawing polygons.
        std::array<D3D12PipelineState, 2 * 4> polyDrawPSOs;

        /// @brief Descriptor range for the output merger (non-OIT variants).
        DescriptorTable outputMergerDescs;
        /// @brief Descriptor range for the output merger (OIT variants).
        DescriptorTable outputMergerOITDescs;
        /// @brief Pipeline state objects for the output merger.
        std::array<D3D12PipelineState, 2 * 3> outputMergerPSOs;

        // -------------------------------------------------------------------------------------------------------------
        // VDP2

        /// @brief CRAM color buffer.
        D3D12Resource cramColorBuffer;
        /// @brief CRAM color buffer SRV (offline).
        DescriptorRange cramColorSRV;

        /// @brief Raw CRAM rotation coefficients buffer.
        D3D12Resource cramRotCoeffBuffer;
        /// @brief Raw CRAM rotation coefficients buffer SRV (offline).
        DescriptorRange cramRotCoeffSRV;

        /// @brief Current CRAM generation of this frame (dirty tracking).
        uint32 cramGeneration = 0xFFFFFFFF;

        /// @brief 2D texture array for the outputs of NBG0-3, RBG0-1, sprite and mesh layers (in that order).
        /// Contains the intermediate per-layer outputs of the VDP2 rendering process.
        D3D12Resource layerOutTexture;
        /// @brief Layer outputs SRV (offline).
        DescriptorRange layerOutSRV;
        /// @brief Layer outputs UAV (offline).
        DescriptorRange layerOutUAV;

        /// @brief 2D texture array for RBG0-1 line color outputs (in that order).
        D3D12Resource rbgLineColorOutTexture;
        /// @brief RBG0-1 line color outputs SRV (offline).
        DescriptorRange rbgLineColorOutSRV;
        /// @brief RBG0-1 line color outputs UAV (offline).
        DescriptorRange rbgLineColorOutUAV;

        /// @brief Color calculation window 2D texture.
        D3D12Resource colorCalcWindowTexture;
        /// @brief Color calculation window SRV (offline).
        DescriptorRange colorCalcWindowSRV;
        /// @brief Color calculation window UAV (offline).
        DescriptorRange colorCalcWindowUAV;

        /// @brief LNCL/BACK screen buffer.
        D3D12Resource lnclBackBuffer;
        /// @brief LNCL/BACK screen buffer SRV (offline).
        DescriptorRange lnclBackSRV;

        /// @brief VDP2 rotation parameter base values buffer.
        D3D12Resource rotParamBasesBuffer;
        /// @brief VDP2 rotation parameter base values buffer SRV (offline).
        DescriptorRange rotParamBasesSRV;

        /// @brief VDP2 sprite attributes 2D texture array (sprite then mesh).
        D3D12Resource spriteAttrsTexture;
        /// @brief VDP2 sprite attributes SRV (offline).
        DescriptorRange spriteAttrsSRV;
        /// @brief VDP2 sprite attributes UAV (offline).
        DescriptorRange spriteAttrsUAV;

        // ---------------------------------------------------------------------

        /// @brief Layer rendering parameters buffer.
        D3D12Resource layerRenderParamsBuffer;
        /// @brief Layer rendering parameters buffer SRV (offline).
        DescriptorRange layerRenderParamsSRV;
        /// @brief Current layer rendering parameters generation (dirty tracking).
        uint32 layerRenderParamsGeneration = 0xFFFFFFFF;

        /// @brief Layer composition parameters buffer.
        D3D12Resource composeParamsBuffer;
        /// @brief Layer composition parameters buffer SRV (offline).
        DescriptorRange composeParamsSRV;
        /// @brief Current composition parameters generation (dirty tracking).
        uint32 composeParamsGeneration = 0xFFFFFFFF;

        /// @brief Pipeline state object for drawing the sprite layer.
        D3D12PipelineState drawSpritePSO;
        /// @brief Descriptor range for drawing the sprite layer.
        DescriptorTable drawSpriteDescs;

        /// @brief Pipeline state object for drawing background layers.
        D3D12PipelineState drawBGsPSO;
        /// @brief Descriptor range for drawing background layers.
        DescriptorTable drawBGsDescs;

        /// @brief Pipeline state object for compositing layers.
        D3D12PipelineState composePSO;
        /// @brief Descriptor range for compositing layers.
        DescriptorTable composeDescs;

        // ---------------------------------------------------------------------

        DeleteQueues deleteQueues;

        void Reset() {
            cmdAlloc->Reset();
        }
    };

    /// @brief Ring buffer of frame resources.
    /// @tparam count number of frames
    template <size_t count>
    struct FrameSet {
        std::array<FrameContext, count> frames;
        size_t frameIndex = 0;
        UINT64 currFenceValue = 0;

        DescriptorHeapAllocator &offlineHeapAlloc;
        DescriptorHeapAllocator &onlineHeapAlloc;

        FrameSet(DescriptorHeapAllocator &offlineHeapAlloc, DescriptorHeapAllocator &onlineHeapAlloc)
            : offlineHeapAlloc(offlineHeapAlloc)
            , onlineHeapAlloc(onlineHeapAlloc) {}

        FrameContext &GetCurrentFrame() {
            return frames[frameIndex];
        }
        const FrameContext &GetCurrentFrame() const {
            return frames[frameIndex];
        }

        UINT64 GetNextFenceValue() const {
            return currFenceValue + 1;
        }

        util::ValueResult<UINT64> IncrementFence(D3D12Fence &fence, D3D12CommandQueue &cmdQueue) {
            // Schedule a signal command in the queue
            FrameContext &currFrame = GetCurrentFrame();
            const UINT64 signalValue = currFenceValue + 1;
            if (FAILED(fence.Signal(cmdQueue, signalValue))) {
                return util::ErrorMessage{"Failed to signal fence"};
            }
            currFrame.signaledValue = signalValue;

            // Set the fence value for the current frame
            currFenceValue = signalValue;

            return signalValue;
        }

        util::VoidResult<> MoveToNextFrame(D3D12Fence &fence, D3D12CommandQueue &cmdQueue) {
            IncrementFence(fence, cmdQueue);

            // Update the frame index
            ++frameIndex;
            if (frameIndex >= count) {
                frameIndex = 0;
            }

            // Wait for next frame
            FrameContext &nextFrame = GetCurrentFrame();
            if (fence->GetCompletedValue() < nextFrame.signaledValue) {
                fence.Wait(INFINITE, nextFrame.signaledValue);
            }

            // Reset frame
            nextFrame.Reset();

            // Free all resources pending for deletion from the frame
            for (DescToDelete &range : nextFrame.deleteQueues.offlineDescs) {
                offlineHeapAlloc.Free(range.baseIndex, range.count);
            }
            for (DescToDelete &range : nextFrame.deleteQueues.onlineDescs) {
                onlineHeapAlloc.Free(range.baseIndex, range.count);
            }
            nextFrame.deleteQueues.offlineDescs.clear();
            nextFrame.deleteQueues.onlineDescs.clear();
            nextFrame.deleteQueues.resources.clear(); // automatically invokes Release() on all resources

            return {};
        }

        util::VoidResult<> WaitForGPU(D3D12Fence &fence, D3D12CommandQueue &cmdQueue) {
            FrameContext &currFrame = GetCurrentFrame();

            // Schedule a signal command in the queue
            const UINT64 signalValue = currFenceValue + 1;
            if (FAILED(fence.Signal(cmdQueue, signalValue))) {
                return util::ErrorMessage{"Failed to signal fence"};
            }

            // Wait until the fence has been processed
            fence.Wait(INFINITE, signalValue);

            // Increment the fence value for the current frame
            currFenceValue = signalValue;

            return {};
        }

        void WaitForLatestFrame(D3D12Fence &fence, D3D12CommandQueue &cmdQueue) {
            if (fence.GetCompletedValue() < currFenceValue) {
                fence.Wait(INFINITE, currFenceValue);
            }
        }

        FrameContext &operator[](size_t index) {
            return frames[index];
        }
        const FrameContext &operator[](size_t index) const {
            return frames[index];
        }

        constexpr size_t Count() const {
            return count;
        }
    };

    /// @brief Per-frame resources.
    FrameSet<kNumFrames> frames{offlineHeapAlloc, resourceHeapAlloc};

    /// @brief Command list.
    D3D12GraphicsCommandList cmdList;

    /// @brief Upload ring buffer.
    UploadRingBuffer uploadBuffer;

    // =================================================================================================================
    // Resource management

    struct RootSignatureSpec {
        UINT64 constantsSize = 0;
        UINT numSRVs = 0;
        UINT numUAVs = 0;
        std::string name;
    };

    [[nodiscard]] util::VoidResult<> CreateRootSignature(D3D12RootSignature &rootSig, const RootSignatureSpec &spec) {
        // NOTE: SRV/UAV descriptors start from 1 because SPIRV-Cross assumes buffers in t0/u0 are constant
        auto builder = rootSig.Builder();
        builder.Add32BitConstants(0, spec.constantsSize / sizeof(uint32));
        builder.AddDescriptorTable().AddSRVs(spec.numSRVs, 1).AddUAVs(spec.numUAVs, 1);
        if (HRESULT hr = builder.Build(device); FAILED(hr)) {
            return util::ErrorMessage{
                fmt::format("Could not build root signature \"{}\", error code {:X}", spec.name, (uint32)hr)};
        }
        vdp1.fbramWriteRootSig->SetName(util::StringToWString(spec.name).c_str());
        return {};
    }

    [[nodiscard]] util::VoidResult<> CreateShader(gpu::ComputeShader &shader, std::string_view path) {
        auto shaderBlobResult = LoadShader(path.data());
        if (!shaderBlobResult) {
            return util::ErrorMessage{
                fmt::format("Could not load compute shader from \"{}\": {}", path, shaderBlobResult.Error().message)};
        }
        shader.format = gpu::ShaderBytecodeFormat::DXIL;
        shader.bytecode = shaderBlobResult.Value();
        shader.entrypoint = kCSEntrypoint;
        auto result = gpu::ValidateShader(shader);
        if (!result) {
            return util::ErrorMessage{
                fmt::format("Compute shader \"{}\" validation failed: {}", path, result.Error().message)};
        }
        return {};
    }

    [[nodiscard]] util::VoidResult<> CreatePSO(D3D12PipelineState &pso, D3D12RootSignature &rootSig,
                                               gpu::ComputeShader &shader, std::string_view name) {
        const D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{
            .pRootSignature = rootSig.GetPointer(),
            .CS = ToShaderBytecode(shader),
        };
        if (HRESULT hr = pso.CreateCompute(device, psoDesc); FAILED(hr)) {
            return util::ErrorMessage{
                fmt::format("Could not build pipeline state object \"{}\", error code {:X}", name, (uint32)hr)};
        }
        vdp1.fbramWritePSO->SetName(util::StringToWString(name).c_str());
        return {};
    }

    struct BufferSpec {
        DescriptorRange *srv = nullptr;
        DescriptorRange *uav = nullptr;
        std::string name;
    };

    enum class BufferType { Raw, Primitive, Structured };

    struct FullBufferSpec {
        BufferSpec bufferSpec;
        BufferType type;
        union {
            struct {
                UINT64 size;
            } raw;
            struct {
                DXGI_FORMAT format;
                UINT numElements;
            } primitive;
            struct {
                UINT elementSize;
                UINT numElements;
            } structured;
        };
    };

    /// @brief Creates a buffer.
    /// @param[out] buffer the resource object to create
    /// @param[in] spec buffer specifications
    /// @return nothing, or an error message
    [[nodiscard]] util::VoidResult<> CreateBuffer(D3D12Resource &buffer, const FullBufferSpec &spec) {
        const bool raw = spec.type == BufferType::Raw;
        const bool primitive = spec.type == BufferType::Primitive;
        const bool structured = spec.type == BufferType::Structured;
        assert(raw || primitive || structured);

        const char *bufferTypeName = raw ? "raw" : primitive ? "primitive" : "structured";

        const UINT64 elementSize = raw         ? 1u
                                   : primitive ? GetElementSize(spec.primitive.format)
                                               : spec.structured.elementSize;
        const UINT64 numElements = raw         ? spec.raw.size
                                   : primitive ? spec.primitive.numElements
                                               : spec.structured.numElements;
        const UINT64 size = elementSize * numElements;
        const UINT64 viewSize = size / sizeof(HLSLuint);
        if (viewSize != static_cast<UINT>(viewSize)) {
            return util::ErrorMessage{fmt::format("Could not create {} buffer \"{}\": buffer is too large",
                                                  bufferTypeName, spec.bufferSpec.name)};
        }

        D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
        if (spec.bufferSpec.uav != nullptr) {
            flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        }

        FrameContext &currFrame = frames.GetCurrentFrame();

        wil::com_ptr_nothrow<ID3D12Resource> oldResource = buffer.GetPointer();
        if (HRESULT hr = buffer.BufferBuilder(size).Flags(flags).BuildCommitted(device); FAILED(hr)) {
            return util::ErrorMessage{fmt::format("Could not create {} buffer \"{}\", error code {:X}", bufferTypeName,
                                                  spec.bufferSpec.name, (uint32)hr)};
        }
        buffer->SetName(util::StringToWString(spec.bufferSpec.name).c_str());
        if (oldResource != nullptr) {
            currFrame.deleteQueues.resources.push_back(oldResource);
            barrierTracker.DeleteBuffer(oldResource.get());
        }

        const DXGI_FORMAT format = raw         ? DXGI_FORMAT_R32_TYPELESS
                                   : primitive ? spec.primitive.format
                                               : DXGI_FORMAT_UNKNOWN;

        if (spec.bufferSpec.srv != nullptr) {
            DescToDelete descToDelete{*spec.bufferSpec.srv};
            if (!offlineHeapAlloc.Allocate(*spec.bufferSpec.srv)) {
                return util::ErrorMessage{
                    fmt::format("Could not allocate {} buffer \"{}\" SRV", bufferTypeName, spec.bufferSpec.name)};
            }
            if (descToDelete.count > 0) {
                currFrame.deleteQueues.offlineDescs.push_back(descToDelete);
            }
            const D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{
                .Format = format,
                .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                .Buffer =
                    {
                        .FirstElement = 0,
                        .NumElements = static_cast<UINT>(structured ? numElements : viewSize),
                        .StructureByteStride = structured ? static_cast<UINT>(elementSize) : 0u,
                        .Flags = raw ? D3D12_BUFFER_SRV_FLAG_RAW : D3D12_BUFFER_SRV_FLAG_NONE,
                    },
            };
            device->CreateShaderResourceView(buffer.GetPointer(), &srvDesc, spec.bufferSpec.srv->cpuHandle);
        }
        if (spec.bufferSpec.uav != nullptr) {
            DescToDelete descToDelete{*spec.bufferSpec.uav};
            if (!offlineHeapAlloc.Allocate(*spec.bufferSpec.uav)) {
                return util::ErrorMessage{
                    fmt::format("Could not allocate {} buffer \"{}\" UAV", bufferTypeName, spec.bufferSpec.name)};
            }
            if (descToDelete.count > 0) {
                currFrame.deleteQueues.offlineDescs.push_back(descToDelete);
            }
            const D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{
                .Format = format,
                .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
                .Buffer =
                    {
                        .FirstElement = 0,
                        .NumElements = static_cast<UINT>(structured ? numElements : viewSize),
                        .StructureByteStride = structured ? static_cast<UINT>(elementSize) : 0u,
                        .CounterOffsetInBytes = 0,
                        .Flags = raw ? D3D12_BUFFER_UAV_FLAG_RAW : D3D12_BUFFER_UAV_FLAG_NONE,
                    },
            };
            device->CreateUnorderedAccessView(buffer.GetPointer(), nullptr, &uavDesc, spec.bufferSpec.uav->cpuHandle);
        }

        return {};
    }

    /// @brief Creates a `ByteAddressBuffer`.
    /// @param[out] buffer the resource object to create
    /// @param[in] size buffer size in bytes
    /// @param[in] spec buffer specifications
    /// @return nothing, or an error message
    [[nodiscard]] util::VoidResult<> CreateRawBuffer(D3D12Resource &buffer, UINT64 size, const BufferSpec &spec) {
        const FullBufferSpec fullSpec{
            .bufferSpec = spec,
            .type = BufferType::Raw,
            .raw = {.size = size},
        };
        return CreateBuffer(buffer, fullSpec);
    }

    /// @brief Creates a `Buffer`.
    /// @param[out] buffer the resource object to create
    /// @param[in] format format of each element
    /// @param[in] numElements number of elements in the buffer
    /// @param[in] spec buffer specifications
    /// @return nothing, or an error message
    [[nodiscard]] util::VoidResult<> CreatePrimitiveBuffer(D3D12Resource &buffer, DXGI_FORMAT format, UINT numElements,
                                                           const BufferSpec &spec) {
        const FullBufferSpec fullSpec{
            .bufferSpec = spec,
            .type = BufferType::Primitive,
            .primitive =
                {
                    .format = format,
                    .numElements = numElements,
                },
        };
        return CreateBuffer(buffer, fullSpec);
    }

    /// @brief Creates a `StructuredBuffer`.
    /// @param[out] buffer the resource object to create
    /// @param[in] elementSize element size in bytes, must be a multiple of 4
    /// @param[in] numElements number of elements in the buffer
    /// @param[in] spec buffer specifications
    /// @return nothing, or an error message
    [[nodiscard]] util::VoidResult<> CreateStructuredBuffer(D3D12Resource &buffer, UINT elementSize, UINT numElements,
                                                            const BufferSpec &spec) {
        assert((elementSize & 3ull) == 0);
        const FullBufferSpec fullSpec{
            .bufferSpec = spec,
            .type = BufferType::Structured,
            .structured =
                {
                    .elementSize = elementSize,
                    .numElements = numElements,
                },
        };
        return CreateBuffer(buffer, fullSpec);
    }

    /// @brief Creates a `StructuredBuffer<T>`.
    /// @tparam T structured buffer element type from which to derive the element size
    /// @param[out] buffer the resource object to create
    /// @param[in] numElements number of elements in the buffer
    /// @return nothing, or an error message
    template <typename T>
    [[nodiscard]] util::VoidResult<> CreateStructuredBuffer(D3D12Resource &buffer, UINT64 numElements,
                                                            const BufferSpec &spec) {
        return CreateStructuredBuffer(buffer, sizeof(T), numElements, spec);
    }

    struct TextureSpec {
        DescriptorRange *srv = nullptr;
        DescriptorRange *uav = nullptr;
        std::string name;
    };

    /// @brief Creates a `Texture2D`.
    /// @param[out] texture the resource object to create
    /// @param[in] format the texture format
    /// @param[in] width the texture width
    /// @param[in] height the texture height
    /// @param[in] spec texture specifications
    /// @return nothing, or an error message
    [[nodiscard]] util::VoidResult<> Create2DTexture(D3D12Resource &texture, DXGI_FORMAT format, UINT width,
                                                     UINT height, const TextureSpec &spec) {

        D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
        if (spec.uav != nullptr) {
            flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        }

        FrameContext &currFrame = frames.GetCurrentFrame();

        wil::com_ptr_nothrow<ID3D12Resource> oldResource = texture.GetPointer();
        if (HRESULT hr = texture.Texture2DBuilder(width, height).Format(format).Flags(flags).BuildCommitted(device);
            FAILED(hr)) {
            return util::ErrorMessage{
                fmt::format("Could not create 2D texture \"{}\", error code {:X}", spec.name, (uint32)hr)};
        }
        texture->SetName(util::StringToWString(spec.name).c_str());
        if (oldResource != nullptr) {
            currFrame.deleteQueues.resources.push_back(oldResource);
            barrierTracker.DeleteTexture(oldResource.get());
        }

        if (spec.srv != nullptr) {
            DescToDelete descToDelete{*spec.srv};
            if (!offlineHeapAlloc.Allocate(*spec.srv)) {
                return util::ErrorMessage{fmt::format("Could not allocate 2D texture \"{}\" SRV", spec.name)};
            }
            if (descToDelete.count > 0) {
                currFrame.deleteQueues.offlineDescs.push_back(descToDelete);
            }
            const D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{
                .Format = format,
                .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                .Texture2D =
                    {
                        .MostDetailedMip = 0,
                        .MipLevels = 1,
                        .PlaneSlice = 0,
                        .ResourceMinLODClamp = 0.0f,
                    },
            };
            device->CreateShaderResourceView(texture.GetPointer(), &srvDesc, spec.srv->cpuHandle);
        }

        if (spec.uav != nullptr) {
            DescToDelete descToDelete{*spec.uav};
            if (!offlineHeapAlloc.Allocate(*spec.uav)) {
                return util::ErrorMessage{fmt::format("Could not allocate 2D texture \"{}\" UAV", spec.name)};
            }
            if (descToDelete.count > 0) {
                currFrame.deleteQueues.offlineDescs.push_back(descToDelete);
            }
            const D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{
                .Format = format,
                .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D,
                .Texture2D =
                    {
                        .MipSlice = 0,
                        .PlaneSlice = 0,
                    },
            };
            device->CreateUnorderedAccessView(texture.GetPointer(), nullptr, &uavDesc, spec.uav->cpuHandle);
        }

        return {};
    }

    /// @brief Creates a `Texture2DArray`.
    /// @param[out] texture the resource object to create
    /// @param[in] format the texture format
    /// @param[in] width the texture width
    /// @param[in] height the texture height
    /// @param[in] arraySize the number of elements in the texture array
    /// @param[in] spec texture specifications
    /// @return nothing, or an error message
    [[nodiscard]] util::VoidResult<> Create2DTextureArray(D3D12Resource &texture, DXGI_FORMAT format, UINT width,
                                                          UINT height, UINT arraySize, const TextureSpec &spec) {

        D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
        if (spec.uav != nullptr) {
            flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        }

        FrameContext &currFrame = frames.GetCurrentFrame();

        wil::com_ptr_nothrow<ID3D12Resource> oldResource = texture.GetPointer();
        if (HRESULT hr =
                texture.Texture2DBuilder(width, height, arraySize).Format(format).Flags(flags).BuildCommitted(device);
            FAILED(hr)) {
            return util::ErrorMessage{
                fmt::format("Could not create 2D texture array \"{}\", error code {:X}", spec.name, (uint32)hr)};
        }
        texture->SetName(util::StringToWString(spec.name).c_str());
        if (oldResource != nullptr) {
            currFrame.deleteQueues.resources.push_back(oldResource);
            barrierTracker.DeleteTexture(oldResource.get());
        }

        if (spec.srv != nullptr) {
            DescToDelete descToDelete{*spec.srv};
            if (!offlineHeapAlloc.Allocate(*spec.srv)) {
                return util::ErrorMessage{fmt::format("Could not allocate 2D texture array \"{}\" SRV", spec.name)};
            }
            if (descToDelete.count > 0) {
                currFrame.deleteQueues.offlineDescs.push_back(descToDelete);
            }
            const D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{
                .Format = format,
                .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                .Texture2DArray =
                    {
                        .MostDetailedMip = 0,
                        .MipLevels = 1,
                        .FirstArraySlice = 0,
                        .ArraySize = arraySize,
                        .PlaneSlice = 0,
                        .ResourceMinLODClamp = 0.0f,
                    },
            };
            device->CreateShaderResourceView(texture.GetPointer(), &srvDesc, spec.srv->cpuHandle);
        }

        if (spec.uav != nullptr) {
            DescToDelete descToDelete{*spec.uav};
            if (!offlineHeapAlloc.Allocate(*spec.uav)) {
                return util::ErrorMessage{fmt::format("Could not allocate 2D texture array \"{}\" UAV", spec.name)};
            }
            if (descToDelete.count > 0) {
                currFrame.deleteQueues.offlineDescs.push_back(descToDelete);
            }
            const D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{
                .Format = format,
                .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY,
                .Texture2DArray =
                    {
                        .MipSlice = 0,
                        .FirstArraySlice = 0,
                        .ArraySize = arraySize,
                        .PlaneSlice = 0,
                    },
            };
            device->CreateUnorderedAccessView(texture.GetPointer(), nullptr, &uavDesc, spec.uav->cpuHandle);
        }

        return {};
    }

    // =================================================================================================================
    // Operations

    util::VoidResult<> Initialize(ID3D12Device *pDevice) {
        device.Assign(pDevice);

        // Check features
        D3D12_FEATURE_DATA_D3D12_OPTIONS12 options12{};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &options12, sizeof(options12)))) {
            features.enhancedBarriers = options12.EnhancedBarriersSupported;
        } else {
            features.enhancedBarriers = false;
        }
        barrierTracker.UseEnhancedBarriers(features.enhancedBarriers);

        // Main command queue
        if (HRESULT hr = cmdQueue.Create(device, D3D12_COMMAND_LIST_TYPE_COMPUTE); FAILED(hr)) {
            return util::ErrorMessage{
                fmt::format("Could not create VDP renderer command queue, error code {:X}", (uint32)hr)};
        }
        cmdQueue->SetName(L"[Ymir-VDP] Command queue");

        // Main compute fence
        if (HRESULT hr = computeFence.Create(device, 0, D3D12_FENCE_FLAG_NONE); FAILED(hr)) {
            return util::ErrorMessage{
                fmt::format("Could not create VDP renderer compute fence, error code {:X}", (uint32)hr)};
        }
        computeFence->SetName(L"[Ymir-VDP] Compute fence");

        // Resource heaps
        {
            D3D12_DESCRIPTOR_HEAP_DESC desc{
                .Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                .NumDescriptors = 1024 + 1024 * kNumFrames,
                .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
            };
            if (HRESULT hr = resourceHeap.Create(device, desc); FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Could not create VDP renderer CBV/SRV/UAV heap, error code {:X}", (uint32)hr)};
            }
            resourceHeap->SetName(L"[Ymir-VDP] CBV/SRV/UAV heap");
            resourceHeapAlloc.Bind(resourceHeap);

            desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            desc.NumDescriptors = 512 + 512 * kNumFrames;
            if (HRESULT hr = offlineHeap.Create(device, desc); FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Could not create VDP renderer offline CBV/SRV/UAV heap, error code {:X}", (uint32)hr)};
            }
            offlineHeap->SetName(L"[Ymir-VDP] CBV/SRV/UAV offline heap");
            offlineHeapAlloc.Bind(offlineHeap);
        }

        // Per-frame command allocators and command list
        for (int i = 0; i < frames.Count(); ++i) {
            FrameContext &frame = frames[i];
            if (HRESULT hr = frame.cmdAlloc.Create(device, D3D12_COMMAND_LIST_TYPE_COMPUTE); FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Could not create VDP renderer command allocator #{}, error code {:X}", i, (uint32)hr)};
            }
            frame.cmdAlloc->SetName(fmt::format(L"[Ymir-VDP] Command allocator #{}", i).c_str());
        }
        if (HRESULT hr = cmdList.Create(device, frames.GetCurrentFrame().cmdAlloc, D3D12_COMMAND_LIST_TYPE_COMPUTE);
            FAILED(hr)) {
            return util::ErrorMessage{
                fmt::format("Could not create VDP renderer command list, error code {:X}", (uint32)hr)};
        }
        cmdList->SetName(L"[Ymir-VDP] Command list");

        // Generic upload buffer
        {
            if (auto result = uploadBuffer.Create(device, kUploadBufferSize); !result) {
                return util::ErrorMessage{
                    fmt::format("Could not create VDP upload buffer: {}", result.Error().message)};
            }
            uploadBuffer.SetDebugName("VDP");
            uploadBuffer.GetBufferResource()->SetName(L"[Ymir-VDP] Upload buffer");
        }

        // =============================================================================================================
        // VDP1

        // -------------------------------------------------------------------------------------------------------------
        // Common resources

        // VDP1 VRAM buffer
        if (auto result = CreateRawBuffer(vdp1.vramBuffer, kVDP1VRAMSize,
                                          {
                                              .srv = &vdp1.vramSRV,
                                              .name = "[Ymir-VDP1] VRAM buffer",
                                          });
            !result) {
            return result;
        }
        barrierTracker.InitializeBuffer(vdp1.vramBuffer.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                        D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);

        // VDP1 FBRAM buffer
        if (auto result = CreateRawBuffer(vdp1.fbramBuffer,
                                          // kVDP1FBRAMSize is the size of a single framebuffer.
                                          // *2 for the the two framebuffers in VDP1 FBRAM.
                                          // *2 for deinterlace alternate field buffers.
                                          // *2 for transparent mesh buffers.
                                          kVDP1FBRAMSize * 2 * 2 * 2,
                                          {

                                              .srv = &vdp1.fbramSRV,
                                              .uav = &vdp1.fbramUAV,
                                              .name = "[Ymir-VDP1] FBRAM buffer",
                                          });
            !result) {
            return result;
        }
        barrierTracker.InitializeBuffer(vdp1.fbramBuffer.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                        D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);

        // VDP1 FBRAM writes buffer
        if (auto result = CreateStructuredBuffer<VDP1FBRAMWrite>(vdp1.fbramWritesBuffer, kVDP1VRAMSize,
                                                                 {
                                                                     .srv = &vdp1.fbramWritesSRV,
                                                                     .name = "[Ymir-VDP1] FBRAM writes buffer",
                                                                 });
            !result) {
            return result;
        }
        barrierTracker.InitializeBuffer(vdp1.fbramWritesBuffer.GetPointer(),
                                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                        D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);

        // VDP1 FBRAM download buffer
        {
            // Allocate enough room to download the standard VDP1 FBRAM.
            // TODO: add deinterlace and transparent mesh buffers to save state
            static constexpr UINT64 kSize = kVDP1FBRAMSize * 2;

            auto builder = vdp1.fbramDownloadBuffer.BufferBuilder(kSize);
            builder.InitialState(D3D12_RESOURCE_STATE_COPY_DEST);
            builder.HeapType(D3D12_HEAP_TYPE_READBACK);
            if (HRESULT hr = builder.BuildCommitted(device); FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Could not create VDP1 FBRAM download buffer, error code {:X}", (uint32)hr)};
            }
            vdp1.fbramDownloadBuffer->SetName(L"[Ymir-VDP1] FBRAM download buffer");

            if (HRESULT hr = vdp1.fbramDownloadBuffer->Map(0, nullptr, &vdp1.fbramDownloadBufferPtr); FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Could not map VDP1 FBRAM download buffer, error code {:X}", (uint32)hr)};
            }
        }

        // Internal sprite data output buffer
        if (auto result = CreateStructuredBuffer<HLSLuint>( //
                vdp1.internalSpriteOutBuffer,
                // Each entry in this buffer represents a logical output pixel.
                // Entries are 32-bit, holding the sprite data in the 8 or 16 LSBs and the span index in the 16
                // MSBs to enable parallel rendering with guaranteed pixel ordering.
                // *2 for deinterlace alternate field
                // *2 for transparent mesh buffer
                kVDP1FBRAMSize * 2 * 2,
                {
                    .uav = &vdp1.internalSpriteOutUAV,
                    .name = "[Ymir-VDP1] Internal sprite data output buffer",
                });
            !result) {
            return result;
        }
        barrierTracker.InitializeBuffer(vdp1.internalSpriteOutBuffer.GetPointer(),
                                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_BARRIER_SYNC_COMPUTE_SHADING,
                                        D3D12_BARRIER_ACCESS_UNORDERED_ACCESS);

        // -------------------------------------------------------------------------------------------------------------
        // Shaders and root signatures

        // Framebuffer write
        {
            if (auto result = CreateShader(vdp1.fbramWriteShader, "src/vdp/cs_vdp1_fbram_write.cso"); !result) {
                return util::ErrorMessage{
                    fmt::format("Could not create VDP1 framebuffer write compute shader: {}", result.Error().message)};
            }

            const RootSignatureSpec rootSigSpec{
                .constantsSize = sizeof(VDP1CommonRenderParams) + sizeof(HLSLuint),
                .numSRVs = 1,
                .numUAVs = 1,
                .name = "[Ymir-VDP1] Framebuffer write root signature",
            };
            if (auto result = CreateRootSignature(vdp1.fbramWriteRootSig, rootSigSpec); !result) {
                return result;
            }
            if (auto result = CreatePSO(vdp1.fbramWritePSO, vdp1.fbramWriteRootSig, vdp1.fbramWriteShader,
                                        "[Ymir-VDP1] Framebuffer write pipeline state object");
                !result) {
                return result;
            }

            vdp1.fbramWriteDescs.Bind(&vdp1.fbramWritesSRV, &vdp1.fbramUAV);
            if (!vdp1.fbramWriteDescs.Rebuild(device, resourceHeapAlloc, resourceHeap.GetHeapType())) {
                return util::ErrorMessage{"Could not allocate VDP1 framebuffer write descriptors"};
            }
        }

        // Framebuffer erase
        {
            if (auto result = CreateShader(vdp1.eraseShader, "src/vdp/cs_vdp1_erase.cso"); !result) {
                return util::ErrorMessage{
                    fmt::format("Could not create VDP1 framebuffer erase compute shader: {}", result.Error().message)};
            }

            const RootSignatureSpec rootSigSpec{
                .constantsSize = sizeof(VDP1CommonRenderParams) + sizeof(VDP1EraseParams),
                .numUAVs = 1,
                .name = "[Ymir-VDP1] Framebuffer erase root signature",
            };
            if (auto result = CreateRootSignature(vdp1.eraseRootSig, rootSigSpec); !result) {
                return result;
            }
        }

        // Polygon drawing
        for (size_t shaderIndex = 0; shaderIndex < vdp1.polyDrawShaders.size(); ++shaderIndex) {
            const auto [meshMode, shadingMode] = ExpandVDP1PolyDrawShaderIndex(shaderIndex);
            const std::string variantName = GetVDP1PolyDrawShaderVariantName(shaderIndex);
            const std::string filename = fmt::format("src/vdp/cs_vdp1_polydraw_{}_{}.cso", meshMode, shadingMode);
            if (auto result = CreateShader(vdp1.polyDrawShaders[shaderIndex], filename); !result) {
                return util::ErrorMessage{
                    fmt::format("Could not create VDP1 polygon drawing compute shader variant {}: {}", variantName,
                                result.Error().message)};
            }
        }

        // Polygon drawing root signatures
        {
            const RootSignatureSpec rootSigSpec{
                .constantsSize = sizeof(VDP1CommonRenderParams) + sizeof(VDP1PolyDrawParams),
                .numSRVs = 4,
                .numUAVs = 1,
                .name = "[Ymir-VDP1] Polygon drawing root signature",
            };
            if (auto result = CreateRootSignature(vdp1.polyDrawRootSig, rootSigSpec); !result) {
                return result;
            }

            const RootSignatureSpec rootSigOITSpec{
                .constantsSize = sizeof(VDP1CommonRenderParams) + sizeof(VDP1PolyDrawParams),
                .numSRVs = 4,
                .numUAVs = 3,
                .name = "[Ymir-VDP1] Polygon drawing root OITsignature",
            };
            if (auto result = CreateRootSignature(vdp1.polyDrawOITRootSig, rootSigOITSpec); !result) {
                return result;
            }
        }

        // Polygon output merger shaders
        for (size_t shaderIndex = 0; shaderIndex < vdp1.outputMergerShaders.size(); ++shaderIndex) {
            const auto [meshMode, mergeMode] = ExpandVDP1OutputMergerShaderIndex(shaderIndex);
            const std::string variantName = GetVDP1OutputMergerShaderVariantName(shaderIndex);
            const std::string filename = fmt::format("src/vdp/cs_vdp1_output_merger_{}_{}.cso", meshMode, mergeMode);
            if (auto result = CreateShader(vdp1.outputMergerShaders[shaderIndex], filename); !result) {
                return util::ErrorMessage{
                    fmt::format("Could not create VDP1 output merger compute shader variant {}: {}", variantName,
                                result.Error().message)};
            }
        }

        // Polygon output merger root signatures
        {
            const RootSignatureSpec rootSigSpec{
                .constantsSize = sizeof(VDP1CommonRenderParams),
                .numUAVs = 2,
                .name = "[Ymir-VDP1] Output merger root signature",
            };
            if (auto result = CreateRootSignature(vdp1.outputMergerRootSig, rootSigSpec); !result) {
                return result;
            }

            const RootSignatureSpec rootSigOITSpec{
                .constantsSize = sizeof(VDP1CommonRenderParams),
                .numSRVs = 1,
                .numUAVs = 2,
                .name = "[Ymir-VDP1] Output merger OIT root signature",
            };
            if (auto result = CreateRootSignature(vdp1.outputMergerOITRootSig, rootSigOITSpec); !result) {
                return result;
            }
        }

        // -------------------------------------------------------------------------------------------------------------
        // Per-frame VDP1 resources

        for (int i = 0; i < frames.Count(); ++i) {
            FrameContext &frameCtx = frames[i];

            // Span parameters buffer
            if (auto result = CreateStructuredBuffer<VDP1SpanParams>(
                    frameCtx.spanParamsBuffer, frameCtx.cpuSpanParams.size(),
                    {
                        .srv = &frameCtx.spanParamsSRV,
                        .name = fmt::format("[Ymir-VDP1] Span parameters buffer #{}", i),
                    });
                !result) {
                return result;
            }
            barrierTracker.InitializeBuffer(frameCtx.spanParamsBuffer.GetPointer(),
                                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                            D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);

            // Span prefix sums buffer
            if (auto result = CreateStructuredBuffer<HLSLuint>(
                    frameCtx.spanPrefixSumsBuffer, frameCtx.cpuSpanPrefixSums.size(),
                    {
                        .srv = &frameCtx.spanPrefixSumsSRV,
                        .name = fmt::format("[Ymir-VDP1] Span prefix sums buffer #{}", i),
                    });
                !result) {
                return result;
            }
            barrierTracker.InitializeBuffer(frameCtx.spanPrefixSumsBuffer.GetPointer(),
                                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                            D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);

            // Command parameters buffer
            if (auto result = CreateStructuredBuffer<VDP1CommandParams>(
                    frameCtx.cmdParamsBuffer, frameCtx.cpuCmdParams.size(),
                    {
                        .srv = &frameCtx.cmdParamsSRV,
                        .name = fmt::format("[Ymir-VDP1] Command parameters buffer #{}", i),
                    });
                !result) {
                return result;
            }
            barrierTracker.InitializeBuffer(frameCtx.cmdParamsBuffer.GetPointer(),
                                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                            D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);

            // OIT fragments list heads buffer
            if (auto result = CreateStructuredBuffer<HLSLuint>(
                    frameCtx.oitListHeadsBuffer,
                    // Each entry in this buffer represents a logical output pixel.
                    // Entries are 32-bit, holding the index of the head of the list.
                    // *2 for deinterlace alternate field
                    // *2 for transparent mesh buffer
                    kVDP1FBRAMSize * 2 * 2,
                    {
                        .uav = &frameCtx.oitListHeadsUAV,
                        .name = fmt::format("[Ymir-VDP1] OIT fragments list heads buffer #{}", i),
                    });
                !result) {
                return result;
            }
            barrierTracker.InitializeBuffer(frameCtx.oitListHeadsBuffer.GetPointer(),
                                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_BARRIER_SYNC_COMPUTE_SHADING,
                                            D3D12_BARRIER_ACCESS_UNORDERED_ACCESS);

            // OIT fragments buffer
            if (auto result = CreateStructuredBuffer<VDP1OITFragment>(
                    frameCtx.oitFragmentsBuffer, kMaxVDP1OITFragmentsPerDispatch,
                    {
                        .srv = &frameCtx.oitFragmentsSRV,
                        .uav = &frameCtx.oitFragmentsUAV,
                        .name = fmt::format("[Ymir-VDP1] OIT fragments buffer #{}", i),
                    });
                !result) {
                return result;
            }
            barrierTracker.InitializeBuffer(frameCtx.oitFragmentsBuffer.GetPointer(),
                                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_BARRIER_SYNC_COMPUTE_SHADING,
                                            D3D12_BARRIER_ACCESS_UNORDERED_ACCESS);

            // OIT counter buffer
            // This buffer contains a single `uint` used as an atomic counter.
            if (auto result = CreateRawBuffer(frameCtx.oitCounterBuffer, sizeof(HLSLuint),
                                              {
                                                  .uav = &frameCtx.oitCounterUAV,
                                                  .name = fmt::format("[Ymir-VDP1] OIT counter buffer #{}", i),
                                              });
                !result) {
                return result;
            }
            barrierTracker.InitializeBuffer(frameCtx.oitCounterBuffer.GetPointer(),
                                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_BARRIER_SYNC_COMPUTE_SHADING,
                                            D3D12_BARRIER_ACCESS_UNORDERED_ACCESS);

            // Framebuffer erase
            if (auto result = CreatePSO(frameCtx.erasePSO, vdp1.eraseRootSig, vdp1.eraseShader,
                                        fmt::format("[Ymir-VDP1] Framebuffer erase pipeline state object #{}", i));
                !result) {
                return result;
            }
            frameCtx.eraseDescs.Bind(&vdp1.fbramUAV);
            if (!frameCtx.eraseDescs.Rebuild(device, resourceHeapAlloc, resourceHeap.GetHeapType())) {
                return util::ErrorMessage{fmt::format("Could not allocate VDP1 framebuffer erase descriptors #{}", i)};
            }

            // Polygon drawing pipeline state objects
            for (size_t shaderIndex = 0; shaderIndex < vdp1.polyDrawShaders.size(); ++shaderIndex) {
                const std::string variantName = GetVDP1PolyDrawShaderVariantName(shaderIndex);
                const bool isOIT = IsVDP1PolyDrawShaderOIT(shaderIndex);
                D3D12RootSignature &rootSig = isOIT ? vdp1.polyDrawOITRootSig : vdp1.polyDrawRootSig;
                gpu::ComputeShader &shader = vdp1.polyDrawShaders[shaderIndex];
                std::string name =
                    fmt::format("[Ymir-VDP1] Polygon drawing variant {} pipeline state object #{}", variantName, i);
                if (auto result = CreatePSO(frameCtx.polyDrawPSOs[shaderIndex], rootSig, shader, name); !result) {
                    return result;
                }
            }

            // Polygon drawing descriptors (Copy and Shift variants)
            frameCtx.polyDrawDescs.Bind(&frameCtx.spanParamsSRV, &frameCtx.spanPrefixSumsSRV, &frameCtx.cmdParamsSRV,
                                        &vdp1.vramSRV, &vdp1.internalSpriteOutUAV);
            if (!frameCtx.polyDrawDescs.Rebuild(device, resourceHeapAlloc, resourceHeap.GetHeapType())) {
                return util::ErrorMessage{fmt::format("Could not allocate VDP1 polygon drawing descriptors #{}", i)};
            }

            // Polygon drawing descriptors (OIT variant only)
            frameCtx.polyDrawOITDescs.Bind(&frameCtx.spanParamsSRV, &frameCtx.spanPrefixSumsSRV, &frameCtx.cmdParamsSRV,
                                           &vdp1.vramSRV, &frameCtx.oitListHeadsUAV, &frameCtx.oitFragmentsUAV,
                                           &frameCtx.oitCounterUAV);
            if (!frameCtx.polyDrawOITDescs.Rebuild(device, resourceHeapAlloc, resourceHeap.GetHeapType())) {
                return util::ErrorMessage{
                    fmt::format("Could not allocate VDP1 polygon drawing OIT descriptors #{}", i)};
            }

            // Polygon drawing descriptors (MSB variant only)
            frameCtx.polyDrawMSBDescs.Bind(&frameCtx.spanParamsSRV, &frameCtx.spanPrefixSumsSRV, &frameCtx.cmdParamsSRV,
                                           &vdp1.vramSRV, &vdp1.fbramUAV);
            if (!frameCtx.polyDrawMSBDescs.Rebuild(device, resourceHeapAlloc, resourceHeap.GetHeapType())) {
                return util::ErrorMessage{
                    fmt::format("Could not allocate VDP1 polygon drawing MSB descriptors #{}", i)};
            }

            // Output merger
            for (size_t shaderIndex = 0; shaderIndex < vdp1.outputMergerShaders.size(); ++shaderIndex) {
                const std::string variantName = GetVDP1OutputMergerShaderVariantName(shaderIndex);
                const bool isOIT = IsVDP1OutputMergerShaderOIT(shaderIndex);
                D3D12RootSignature &rootSig = isOIT ? vdp1.outputMergerOITRootSig : vdp1.outputMergerRootSig;
                gpu::ComputeShader &shader = vdp1.outputMergerShaders[shaderIndex];
                std::string name =
                    fmt::format("[Ymir-VDP1] Output merger variant {} pipeline state object #{}", variantName, i);
                if (auto result = CreatePSO(frameCtx.outputMergerPSOs[shaderIndex], rootSig, shader, name); !result) {
                    return result;
                }
            }
            frameCtx.outputMergerDescs.Bind(&vdp1.fbramUAV, &vdp1.internalSpriteOutUAV);
            if (!frameCtx.outputMergerDescs.Rebuild(device, resourceHeapAlloc, resourceHeap.GetHeapType())) {
                return util::ErrorMessage{fmt::format("Could not allocate VDP1 output merger descriptors #{}", i)};
            }
            frameCtx.outputMergerOITDescs.Bind(&frameCtx.oitFragmentsSRV, &vdp1.fbramUAV, &frameCtx.oitListHeadsUAV);
            if (!frameCtx.outputMergerOITDescs.Rebuild(device, resourceHeapAlloc, resourceHeap.GetHeapType())) {
                return util::ErrorMessage{fmt::format("Could not allocate VDP1 output merger OIT descriptors #{}", i)};
            }
        }

        // =============================================================================================================
        // VDP2

        // -------------------------------------------------------------------------------------------------------------
        // Common resources

        // VDP2 VRAM buffer
        if (auto result = CreateRawBuffer(vdp2.vramBuffer, kVDP2VRAMSize,
                                          {
                                              .srv = &vdp2.vramSRV,
                                              .name = "[Ymir-VDP2] VRAM buffer",
                                          });
            !result) {
            return result;
        }
        barrierTracker.InitializeBuffer(vdp2.vramBuffer.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                        D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);

        // Composited VDP2 output texture
        if (auto result = Create2DTexture(vdp2.compositeOutTexture, DXGI_FORMAT_R8G8B8A8_UNORM, kMaxResH, kMaxResV,
                                          {
                                              .uav = &vdp2.compositeOutUAV,
                                              .name = "[Ymir-VDP2] Composited output texture",
                                          });
            !result) {
            return result;
        }
        barrierTracker.InitializeTexture(vdp2.compositeOutTexture.GetPointer(), D3D12_RESOURCE_STATE_COMMON,
                                         D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
                                         D3D12_BARRIER_LAYOUT_COMMON);

        // -------------------------------------------------------------------------------------------------------------
        // Shaders and root signatures

        // Sprite layer rendering
        {
            if (auto result = CreateShader(vdp2.drawSpriteShader, "src/vdp/cs_vdp2_render_sprite.cso"); !result) {
                return util::ErrorMessage{fmt::format("Could not create VDP2 sprite layer rendering compute shader: {}",
                                                      result.Error().message)};
            }

            const RootSignatureSpec rootSigSpec{
                .constantsSize = sizeof(VDP2CommonRenderParams),
                .numSRVs = 5,
                .numUAVs = 2,
                .name = "[Ymir-VDP2] Sprite layer rendering root signature",
            };
            if (auto result = CreateRootSignature(vdp2.drawSpriteRootSig, rootSigSpec); !result) {
                return result;
            }
        }

        // Layer rendering
        {
            if (auto result = CreateShader(vdp2.drawBGsShader, "src/vdp/cs_vdp2_render_bgs.cso"); !result) {
                return util::ErrorMessage{fmt::format(
                    "Could not create VDP2 background layer rendering compute shader: {}", result.Error().message)};
            }

            const RootSignatureSpec rootSigSpec{
                .constantsSize = sizeof(VDP2CommonRenderParams),
                .numSRVs = 6,
                .numUAVs = 3,
                .name = "[Ymir-VDP2] Background layer rendering root signature",
            };
            if (auto result = CreateRootSignature(vdp2.drawBGsRootSig, rootSigSpec); !result) {
                return result;
            }
        }

        // Layer compositing
        {
            if (auto result = CreateShader(vdp2.composeShader, "src/vdp/cs_vdp2_compose.cso"); !result) {
                return util::ErrorMessage{fmt::format(
                    "Could not create VDP2 layer compositing rendering compute shader: {}", result.Error().message)};
            }

            const RootSignatureSpec rootSigSpec{
                .constantsSize = sizeof(VDP2CommonRenderParams),
                .numSRVs = 6,
                .numUAVs = 1,
                .name = "[Ymir-VDP2] Layer compositing root signature",
            };
            if (auto result = CreateRootSignature(vdp2.composeRootSig, rootSigSpec); !result) {
                return result;
            }
        }

        // -------------------------------------------------------------------------------------------------------------
        // Per-frame VDP2 resources

        for (int i = 0; i < frames.Count(); ++i) {
            FrameContext &frameCtx = frames[i];

            // VDP2 CRAM color buffer
            if (auto result =
                    CreatePrimitiveBuffer(frameCtx.cramColorBuffer, DXGI_FORMAT_R8G8B8A8_UINT, kVDP2CRAMColorBufferSize,
                                          {
                                              .srv = &frameCtx.cramColorSRV,
                                              .name = fmt::format("[Ymir-VDP2] CRAM color buffer #{}", i),
                                          });
                !result) {
                return result;
            }
            barrierTracker.InitializeBuffer(frameCtx.cramColorBuffer.GetPointer(),
                                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                            D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);

            // VDP2 CRAM rotation coefficients buffer
            if (auto result =
                    CreateRawBuffer(frameCtx.cramRotCoeffBuffer, kVDP2CRAMRotCoeffBufferSize,
                                    {
                                        .srv = &frameCtx.cramRotCoeffSRV,
                                        .name = fmt::format("[Ymir-VDP2] CRAM rotation coefficients buffer #{}", i),
                                    });
                !result) {
                return result;
            }
            barrierTracker.InitializeBuffer(frameCtx.cramRotCoeffBuffer.GetPointer(),
                                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                            D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);

            // Layer outputs 2D texture array
            if (auto result =
                    Create2DTextureArray(frameCtx.layerOutTexture, DXGI_FORMAT_R8G8B8A8_UINT, kMaxResH, kMaxResV,
                                         // The array contains:
                                         //   [0..3] NBG0-3
                                         //   [4..5] RBG0-1
                                         //      [6] Sprite
                                         //      [7] Transparent meshes
                                         // The alpha channel is used for pixel attributes:
                                         //   [0..2] Priority
                                         //      [6] Color format (0=RGB, 1=Palette)
                                         //      [7] Special color calculation flag
                                         4 + 2 + 1 + 1,
                                         {
                                             .srv = &frameCtx.layerOutSRV,
                                             .uav = &frameCtx.layerOutUAV,
                                             .name = fmt::format("[Ymir-VDP2] Layer outputs texture array #{}", i),
                                         });
                !result) {
                return result;
            }
            barrierTracker.InitializeTexture(
                frameCtx.layerOutTexture.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_BARRIER_LAYOUT_COMMON);

            // RBG0-1 line color outputs 2D texture array
            if (auto result = Create2DTextureArray(
                    frameCtx.rbgLineColorOutTexture, DXGI_FORMAT_R8G8B8A8_UINT, kMaxNormalResH, kMaxNormalResV, 2,
                    {
                        .srv = &frameCtx.rbgLineColorOutSRV,
                        .uav = &frameCtx.rbgLineColorOutUAV,
                        .name = fmt::format("[Ymir-VDP2] RBG line color outputs texture array #{}", i),
                    });
                !result) {
                return result;
            }
            barrierTracker.InitializeTexture(
                frameCtx.rbgLineColorOutTexture.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_BARRIER_LAYOUT_COMMON);

            // Color calculation window texture
            if (auto result =
                    Create2DTexture(frameCtx.colorCalcWindowTexture, DXGI_FORMAT_R8_UINT, kMaxResH, kMaxResV,
                                    {
                                        .srv = &frameCtx.colorCalcWindowSRV,
                                        .uav = &frameCtx.colorCalcWindowUAV,
                                        .name = fmt::format("[Ymir-VDP2] Color calculation window texture #{}", i),
                                    });
                !result) {
                return result;
            }
            barrierTracker.InitializeTexture(
                frameCtx.colorCalcWindowTexture.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_BARRIER_LAYOUT_COMMON);

            // LNCL/BACK screen buffer
            if (auto result =
                    CreatePrimitiveBuffer(frameCtx.lnclBackBuffer, DXGI_FORMAT_R8G8B8A8_UINT, kMaxResV * 2u,
                                          {
                                              .srv = &frameCtx.lnclBackSRV,
                                              .name = fmt::format("[Ymir-VDP2] LNCL/BACK screen buffer #{}", i),
                                          });
                !result) {
                return result;
            }
            barrierTracker.InitializeBuffer(frameCtx.lnclBackBuffer.GetPointer(),
                                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                            D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);

            // VDP2 rotation parameter base values buffer
            if (auto result = CreateStructuredBuffer<VDP2RotParamBase>(
                    frameCtx.rotParamBasesBuffer, kMaxNormalResV * 2u,
                    {
                        .srv = &frameCtx.rotParamBasesSRV,
                        .name = fmt::format("[Ymir-VDP2] Rotation parameter base values buffer #{}", i),
                    });
                !result) {
                return result;
            }
            barrierTracker.InitializeBuffer(frameCtx.rotParamBasesBuffer.GetPointer(),
                                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                            D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);

            // VDP2 sprite attributes 2D texture array
            if (auto result =
                    Create2DTextureArray(frameCtx.spriteAttrsTexture, DXGI_FORMAT_R8_UINT, kMaxResH, kMaxResV, 2,
                                         {
                                             .srv = &frameCtx.spriteAttrsSRV,
                                             .uav = &frameCtx.spriteAttrsUAV,
                                             .name = fmt::format("[Ymir-VDP2] Sprite attributes texture array #{}", i),
                                         });
                !result) {
                return result;
            }
            barrierTracker.InitializeTexture(
                frameCtx.spriteAttrsTexture.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_BARRIER_LAYOUT_COMMON);

            // VDP2 layer rendering parameters buffer
            if (auto result = CreateStructuredBuffer<VDP2LayerRenderParams>(
                    frameCtx.layerRenderParamsBuffer, 1,
                    {
                        .srv = &frameCtx.layerRenderParamsSRV,
                        .name = fmt::format("[Ymir-VDP2] Layer rendering parameters buffer #{}", i),
                    });
                !result) {
                return result;
            }
            barrierTracker.InitializeBuffer(frameCtx.layerRenderParamsBuffer.GetPointer(),
                                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                            D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);

            // VDP2 layer compositing parameters buffer
            if (auto result = CreateStructuredBuffer<VDP2ComposeParams>(
                    frameCtx.composeParamsBuffer, 1,
                    {
                        .srv = &frameCtx.composeParamsSRV,
                        .name = fmt::format("[Ymir-VDP2] Layer compositing parameters buffer #{}", i),
                    });
                !result) {
                return result;
            }
            barrierTracker.InitializeBuffer(frameCtx.composeParamsBuffer.GetPointer(),
                                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                            D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);

            // Sprite layer rendering
            if (auto result = CreatePSO(frameCtx.drawSpritePSO, vdp2.drawSpriteRootSig, vdp2.drawSpriteShader,
                                        fmt::format("[Ymir-VDP2] Sprite layer rendering pipeline state object #{}", i));
                !result) {
                return result;
            }
            frameCtx.drawSpriteDescs.Bind(&frameCtx.layerRenderParamsSRV, &vdp2.vramSRV, &frameCtx.cramColorSRV,
                                          &frameCtx.rotParamBasesSRV, &vdp1.fbramSRV, &frameCtx.layerOutUAV,
                                          &frameCtx.spriteAttrsUAV);
            if (!frameCtx.drawSpriteDescs.Rebuild(device, resourceHeapAlloc, resourceHeap.GetHeapType())) {
                return util::ErrorMessage{
                    fmt::format("Could not allocate VDP2 sprite layer rendering descriptors #{}", i)};
            }

            // Layer rendering
            if (auto result =
                    CreatePSO(frameCtx.drawBGsPSO, vdp2.drawBGsRootSig, vdp2.drawBGsShader,
                              fmt::format("[Ymir-VDP2] Background layer rendering pipeline state object #{}", i));
                !result) {
                return result;
            }
            frameCtx.drawBGsDescs.Bind(&frameCtx.layerRenderParamsSRV, &vdp2.vramSRV, &frameCtx.cramColorSRV,
                                       &frameCtx.cramRotCoeffSRV, &frameCtx.rotParamBasesSRV, &frameCtx.spriteAttrsSRV,
                                       &frameCtx.layerOutUAV, &frameCtx.rbgLineColorOutUAV,
                                       &frameCtx.colorCalcWindowUAV);
            if (!frameCtx.drawBGsDescs.Rebuild(device, resourceHeapAlloc, resourceHeap.GetHeapType())) {
                return util::ErrorMessage{fmt::format("Could not allocate VDP2 layer rendering descriptors #{}", i)};
            }

            // Layer compositing
            if (auto result = CreatePSO(frameCtx.composePSO, vdp2.composeRootSig, vdp2.composeShader,
                                        fmt::format("[Ymir-VDP2] Layer compositing pipeline state object #{}", i));
                !result) {
                return result;
            }
            frameCtx.composeDescs.Bind(&frameCtx.composeParamsSRV, &frameCtx.layerOutSRV, &frameCtx.lnclBackSRV,
                                       &frameCtx.rbgLineColorOutSRV, &frameCtx.spriteAttrsSRV,
                                       &frameCtx.colorCalcWindowSRV, &vdp2.compositeOutUAV);
            if (!frameCtx.composeDescs.Rebuild(device, resourceHeapAlloc, resourceHeap.GetHeapType())) {
                return util::ErrorMessage{fmt::format("Could not allocate VDP2 layer compositing descriptors #{}", i)};
            }
        }

        Reset();

        // Initialize command list
        {
            ID3D12DescriptorHeap *heaps[] = {resourceHeap.GetPointer()};
            cmdList->SetDescriptorHeaps(std::size(heaps), heaps);
        }

        // Initialize VDP1 OIT fragment list heads buffer
        for (int i = 0; i < frames.Count(); ++i) {
            FrameContext &frameCtx = frames[i];

            ID3D12Resource *dstResource = frameCtx.oitListHeadsBuffer.GetPointer();
            ID3D12Resource *uploadBufferPtr = uploadBuffer.GetBufferResource().GetPointer();

            // Emit barrier transition
            barrierTracker.TransitionBuffer(dstResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_BARRIER_SYNC_COPY,
                                            D3D12_BARRIER_ACCESS_COPY_DEST);
            barrierTracker.Flush(cmdList);

            // Upload default values
            UploadAllocation alloc{};
            const size_t size = kVDP1FBRAMSize * 2 * sizeof(HLSLuint);
            if (auto result = AllocateUploadBuffer(uploadBuffer, size, 4, alloc); !result) {
                return util::ErrorMessage{fmt::format(
                    "Failed to allocate upload buffer for VDP1 OIT fragment list heads: {}", result.Error().message)};
            }
            std::fill_n(static_cast<HLSLuint *>(alloc.data), kVDP1FBRAMSize * 2, 0xFFFFFFFF);

            cmdList->CopyBufferRegion(dstResource, 0, uploadBufferPtr, alloc.offset, size);
        }

        return {};
    }

    void Shutdown() {
        frames.WaitForGPU(computeFence, cmdQueue);
    }

    util::ValueResult<std::vector<char>> LoadShader(const char *path) {
        if (!g_fsShaders.is_file(path)) {
            return util::ErrorMessage{fmt::format("Embedded file not found: {}", path)};
        }
        auto file = g_fsShaders.open(path);
        return std::vector<char>{file.begin(), file.end()};
    }

    // -----------------------------------------------------------------------------------------------------------------
    // State

    void Reset() {
        // VDP1
        vdp1.vramDirty.SetAll();
        if (auto result = VDP1UploadFBRAM(); !result) {
            devlog::warn<grp::dx12_base>("Failed to upload VDP1 FBRAM: {}", result.Error().message);
        }

        // VDP2
        vdp2.vramDirty.SetAll();
        ++vdp2.cramGeneration;
        ++vdp2.layerRenderParamsGeneration;
        ++vdp2.composeParamsGeneration;

        vdp2.nextLayerRenderLine = 0;
        vdp2.nextComposeLine = 0;

        VDP2CacheAllCRAMColors();
        VDP2UpdateEnabledLayers();
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Common

    /// @brief Attempts to allocate a chunk of data in the specified upload buffer, waiting to free up space if needed.
    /// @param[in] uploadBuffer the upload buffer
    /// @param[in] size the requested size
    /// @param[in] alignment the requested alignment
    /// @param[out] outAlloc receives the allocation information
    /// @return nothing on success, an error message on failure
    util::VoidResult<> AllocateUploadBuffer(UploadRingBuffer &uploadBuffer, size_t size, size_t alignment,
                                            UploadAllocation &outAlloc) {
        // Sanity check: the upload buffer can hold transfers of this size
        YMIR_DEV_ASSERT(size < uploadBuffer.GetSize());

        if (!uploadBuffer.Allocate(size, alignment, computeFence.GetCompletedValue(), outAlloc)) {
            // Block until next fence completes and retry
            const UINT64 waitValue = uploadBuffer.FindFenceValueForAllocation(size, alignment);
            computeFence.Wait(INFINITE, waitValue);

            // At this point, we really should be able to allocate the buffer
            if (!uploadBuffer.Allocate(size, alignment, computeFence->GetCompletedValue(), outAlloc)) {
                // TODO: consider increasing the upload buffer size or allocating overflow buffers.
                // For now we'll just log the error and fail
                YMIR_DEV_CHECK();
                std::string message = fmt::format("Failed to allocate {} bytes (align {}) in {} upload buffer", size,
                                                  alignment, uploadBuffer.GetDebugName());
                devlog::warn<grp::dx12_vdp2>("{}", message);
                return util::ErrorMessage{std::move(message)};
            }
        }
        return {};
    }

    /// @brief Updates the current scaled resolution, updating enhancement settings if necessary.
    void UpdateResolutionScaling() {
        resScale.params.enabled = enhancements.scaleResolution;
        if (!resScale.params.enabled) {
            // Internal resolution scaling is disabled.
            // Use current/maximum display resolution.
            resScale.desired = {kMaxResH, kMaxResV};
            resScale.display = {HRes, VRes};
            return;
        }

        // At this point, internal resolution scaling is enabled

        // Rate-limit resolution updates
        using namespace std::chrono_literals;
        const auto now = std::chrono::steady_clock::now();
        const bool update = now >= resScale.nextUpdate;
        if (update) {
            static constexpr auto kUpdateInterval = 100ms;
            resScale.nextUpdate = now + kUpdateInterval;
        }

        resScale.params.scaleToTargetRes = enhancements.scaleToTargetResolution;
        if (resScale.params.scaleToTargetRes) {
            // Scale internal resolution to target resolution
            if (update) {
                resScale.params.width = enhancements.scaleResTargetWidth;
                resScale.params.height = enhancements.scaleResTargetHeight;
            }
            resScale.desired = {
                std::clamp(resScale.params.width, kMaxResH, kMaxScaledResH),
                std::clamp(resScale.params.height, kMaxResV, kMaxScaledResV),
            };
            resScale.display = {
                std::clamp(resScale.params.width, HRes, kMaxScaledResH),
                std::clamp(resScale.params.height, VRes, kMaxScaledResV),
            };
        } else {
            // Scale internal resolution by a static factor
            if (update) {
                resScale.params.factor =
                    std::clamp(enhancements.scaleResFactor, kMinResScaleFactor, kMaxResScaleFactor);
            }

            // Normalize hi-res dimensions back to normal res before applying scaling
            const uint32 factor = resScale.params.factor;
            const uint32 hres = HRes > kMaxNormalResH ? (HRes >> 1u) : HRes;
            const uint32 vres = VRes > kMaxNormalResV ? (VRes >> 1u) : VRes;
            resScale.desired = {
                std::clamp(kMaxNormalResH * factor, kMaxResH, kMaxScaledResH),
                std::clamp(kMaxNormalResV * factor, kMaxResV, kMaxScaledResV),
            };
            resScale.display = {
                std::min(hres * factor, kMaxScaledResH),
                std::min(vres * factor, kMaxScaledResV),
            };
        }
    }

    [[nodiscard]] util::VoidResult<> RecreateScaledObjects() {
        if (resScale.current == resScale.desired) {
            // No need to recreate objects, already at the target resolution
            return {};
        }
        resScale.current = resScale.desired;

        // Recreate all objects whose size depend on resolution scaling
        FrameContext &currFrame = frames.GetCurrentFrame();

        const auto [width, height] = resScale.current;

        uint64 fbSize = kVDP1FBRAMSize;
        if (resScale.params.enabled) {
            if (resScale.params.scaleToTargetRes) {
                fbSize = fbSize * resScale.display.width * resScale.display.height;
                fbSize = (fbSize + HRes - 1) / HRes;
                fbSize = (fbSize + VRes - 1) / VRes;
            } else {
                // This overshoots the target size at higher scaling factors, but saves the trouble of properly
                // computing the scaling factor based on the current resolution
                fbSize = fbSize * resScale.params.factor * resScale.params.factor;
            }
        }

        // VDP1 FBRAM buffer
        if (resScale.params.enabled) {
            if (auto result = CreateRawBuffer(vdp1.fbramScaledBuffer,
                                              // kVDP1FBRAMSize is the size of a single framebuffer.
                                              // *2 for the the two framebuffers in VDP1 FBRAM.
                                              // *2 for deinterlace alternate field buffers.
                                              // *2 for transparent mesh buffers.
                                              fbSize * 2 * 2 * 2,
                                              {

                                                  .srv = &vdp1.fbramScaledSRV,
                                                  .uav = &vdp1.fbramScaledUAV,
                                                  .name = "[Ymir-VDP1] Scaled FBRAM buffer",
                                              });
                !result) {
                return result;
            }
            barrierTracker.InitializeBuffer(vdp1.fbramScaledBuffer.GetPointer(),
                                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                            D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
        } else {
            currFrame.deleteQueues.resources.push_back(vdp1.fbramScaledBuffer.GetPointer());
            barrierTracker.DeleteBuffer(vdp1.fbramScaledBuffer.GetPointer());
            offlineHeapAlloc.Free(vdp1.fbramScaledSRV.baseIndex, vdp1.fbramScaledSRV.count);
            offlineHeapAlloc.Free(vdp1.fbramScaledUAV.baseIndex, vdp1.fbramScaledUAV.count);
            vdp1.fbramScaledBuffer.Destroy();
            vdp1.fbramScaledSRV.Reset();
            vdp1.fbramScaledUAV.Reset();
        }

        // Internal sprite data output buffer
        if (auto result = CreateStructuredBuffer<HLSLuint>( //
                vdp1.internalSpriteOutBuffer,
                // Each entry in this buffer represents a logical output pixel.
                // Entries are 32-bit, holding the sprite data in the 8 or 16 LSBs and the span index in the 16
                // MSBs to enable parallel rendering with guaranteed pixel ordering.
                // *2 for deinterlace alternate field
                // *2 for transparent mesh buffer
                fbSize * 2 * 2,
                {
                    .uav = &vdp1.internalSpriteOutUAV,
                    .name = "[Ymir-VDP1] Internal sprite data output buffer",
                });
            !result) {
            return result;
        }
        barrierTracker.InitializeBuffer(vdp1.internalSpriteOutBuffer.GetPointer(),
                                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_BARRIER_SYNC_COMPUTE_SHADING,
                                        D3D12_BARRIER_ACCESS_UNORDERED_ACCESS);

        // Composited VDP2 output texture
        if (auto result = Create2DTexture(vdp2.compositeOutTexture, DXGI_FORMAT_R8G8B8A8_UNORM, width, height,
                                          {
                                              .uav = &vdp2.compositeOutUAV,
                                              .name = "[Ymir-VDP2] Composited output texture",
                                          });
            !result) {
            return result;
        }

        barrierTracker.InitializeTexture(vdp2.compositeOutTexture.GetPointer(), D3D12_RESOURCE_STATE_COMMON,
                                         D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
                                         D3D12_BARRIER_LAYOUT_COMMON);

        for (size_t i = 0; i < frames.Count(); ++i) {
            FrameContext &frameCtx = frames.frames[i];

            // VDP2 sprite attributes 2D texture array
            if (auto result =
                    Create2DTextureArray(frameCtx.spriteAttrsTexture, DXGI_FORMAT_R8_UINT, width, height, 2,
                                         {
                                             .srv = &frameCtx.spriteAttrsSRV,
                                             .uav = &frameCtx.spriteAttrsUAV,
                                             .name = fmt::format("[Ymir-VDP2] Sprite attributes texture array #{}", i),
                                         });
                !result) {
                return result;
            }
            barrierTracker.InitializeTexture(
                frameCtx.spriteAttrsTexture.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_BARRIER_LAYOUT_COMMON);

            // Layer outputs 2D texture array
            if (auto result =
                    Create2DTextureArray(frameCtx.layerOutTexture, DXGI_FORMAT_R8G8B8A8_UINT, width, height,
                                         // The array contains:
                                         //   [0..3] NBG0-3
                                         //   [4..5] RBG0-1
                                         //      [6] Sprite
                                         //      [7] Transparent meshes
                                         // The alpha channel is used for pixel attributes:
                                         //   [0..2] Priority
                                         //      [6] Color format (0=RGB, 1=Palette)
                                         //      [7] Special color calculation flag
                                         4 + 2 + 1 + 1,
                                         {
                                             .srv = &frameCtx.layerOutSRV,
                                             .uav = &frameCtx.layerOutUAV,
                                             .name = fmt::format("[Ymir-VDP2] Layer outputs texture array #{}", i),
                                         });
                !result) {
                return result;
            }
            barrierTracker.InitializeTexture(
                frameCtx.layerOutTexture.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_BARRIER_LAYOUT_COMMON);

            // RBG0-1 line color outputs 2D texture array
            uint32 rbgLnclWidth, rbgLnclHeight;
            if (resScale.params.enabled) {
                rbgLnclWidth = width;
                rbgLnclHeight = height;
            } else {
                rbgLnclWidth = kMaxNormalResH;
                rbgLnclHeight = kMaxNormalResV;
            }
            if (auto result = Create2DTextureArray(
                    frameCtx.rbgLineColorOutTexture, DXGI_FORMAT_R8G8B8A8_UINT, width, height, 2,
                    {
                        .srv = &frameCtx.rbgLineColorOutSRV,
                        .uav = &frameCtx.rbgLineColorOutUAV,
                        .name = fmt::format("[Ymir-VDP2] RBG line color outputs texture array #{}", i),
                    });
                !result) {
                return result;
            }
            barrierTracker.InitializeTexture(
                frameCtx.rbgLineColorOutTexture.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_BARRIER_LAYOUT_COMMON);

            // Recreate all affected online descriptor tables
            currFrame.deleteQueues.onlineDescs.push_back(DescToDelete{frameCtx.eraseDescs.Descriptors()});
            currFrame.deleteQueues.onlineDescs.push_back(DescToDelete{frameCtx.polyDrawDescs.Descriptors()});
            currFrame.deleteQueues.onlineDescs.push_back(DescToDelete{frameCtx.polyDrawOITDescs.Descriptors()});
            currFrame.deleteQueues.onlineDescs.push_back(DescToDelete{frameCtx.polyDrawMSBDescs.Descriptors()});
            currFrame.deleteQueues.onlineDescs.push_back(DescToDelete{frameCtx.outputMergerDescs.Descriptors()});
            currFrame.deleteQueues.onlineDescs.push_back(DescToDelete{frameCtx.outputMergerOITDescs.Descriptors()});
            currFrame.deleteQueues.onlineDescs.push_back(DescToDelete{frameCtx.drawSpriteDescs.Descriptors()});
            currFrame.deleteQueues.onlineDescs.push_back(DescToDelete{frameCtx.drawBGsDescs.Descriptors()});
            currFrame.deleteQueues.onlineDescs.push_back(DescToDelete{frameCtx.composeDescs.Descriptors()});
            if (!frameCtx.eraseDescs.Rebuild(device, resourceHeapAlloc, resourceHeap.GetHeapType())) {
                YMIR_DEV_CHECK();
            }
            if (!frameCtx.polyDrawDescs.Rebuild(device, resourceHeapAlloc, resourceHeap.GetHeapType())) {
                YMIR_DEV_CHECK();
            }
            if (!frameCtx.polyDrawOITDescs.Rebuild(device, resourceHeapAlloc, resourceHeap.GetHeapType())) {
                YMIR_DEV_CHECK();
            }
            if (!frameCtx.polyDrawMSBDescs.Rebuild(device, resourceHeapAlloc, resourceHeap.GetHeapType())) {
                YMIR_DEV_CHECK();
            }
            if (!frameCtx.outputMergerDescs.Rebuild(device, resourceHeapAlloc, resourceHeap.GetHeapType())) {
                YMIR_DEV_CHECK();
            }
            if (!frameCtx.outputMergerOITDescs.Rebuild(device, resourceHeapAlloc, resourceHeap.GetHeapType())) {
                YMIR_DEV_CHECK();
            }
            if (!frameCtx.drawSpriteDescs.Rebuild(device, resourceHeapAlloc, resourceHeap.GetHeapType())) {
                YMIR_DEV_CHECK();
            }
            if (!frameCtx.drawBGsDescs.Rebuild(device, resourceHeapAlloc, resourceHeap.GetHeapType())) {
                YMIR_DEV_CHECK();
            }
            if (!frameCtx.composeDescs.Rebuild(device, resourceHeapAlloc, resourceHeap.GetHeapType())) {
                YMIR_DEV_CHECK();
            }
        }

        return {};
    }

    // -----------------------------------------------------------------------------------------------------------------
    // VDP1 rendering

    void VDP1WriteVRAM(uint32 address) {
        // Submit spans if the target address is in use by a textured polygon in the current batch
        if (vdp1.vramTexUsageTracker.IsInUse(address)) [[unlikely]] {
            devlog::debug<grp::dx12_vdp1>("VDP1 VRAM write to {:05X} which is in use by pending spans; submitting now",
                                          address);
            VDP1SubmitSpans();
        }
        vdp1.vramDirty.Set(address >> VDP1Resources::kVRAMDirtyBitmapChunkSizeShift);
    }

    void VDP1DownloadFBRAM() {
        // Bail out if there have been no changes
        if (!vdp1.fbramReadbackCopyPending) {
            return;
        }
        vdp1.fbramReadbackCopyPending = false;

        // Submit partial command list
        auto result = SubmitCommandList();
        if (!result) {
            devlog::warn<grp::dx12_base>("Failed to submit command list: {}", result.Error().message);
            return;
        }

        // Wait for it to complete
        computeFence.Wait(INFINITE, result.Value());

        // Copy downloaded FBRAM
        memcpy(vdpState.mem1.FBRAM.data(), vdp1.fbramDownloadBufferPtr, kVDP1FBRAMSize * 2);
    }

    void VDP1SyncFB() {
        // Submit pending spans to ensure we're synced as far as possible
        VDP1SubmitSpans();

        // Copy FBRAM to readback buffer if modified
        VDP1CopyFBRAMToReadback();

        // Download FBRAM from readback buffer
        VDP1DownloadFBRAM();
    }

    void VDP1DebugSyncFB() {
        vdp1.fbramDebugSyncRequest.store(true, std::memory_order_release);
    }

    void VDP1WriteFB(uint32 address, uint32 size) {
        for (uint32 i = 0; i < size; ++i) {
            vdp1.fbramDirty.Set(address + i);
        }
    }

    [[nodiscard]] util::VoidResult<> VDP1FlushVRAM() {
        if (!vdp1.vramDirty) {
            return {};
        }

        ID3D12Resource *dstResource = vdp1.vramBuffer.GetPointer();
        ID3D12Resource *uploadBufferPtr = uploadBuffer.GetBufferResource().GetPointer();

        // Emit barrier transition
        barrierTracker.TransitionBuffer(dstResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_BARRIER_SYNC_COPY,
                                        D3D12_BARRIER_ACCESS_COPY_DEST);
        barrierTracker.Flush(cmdList);

        // Upload all modified VRAM chunks
        size_t pos, count = 0;
        UploadAllocation alloc{};
        for (pos = vdp1.vramDirty.FindNext(count); pos < vdp1.vramDirty.Size();
             pos = vdp1.vramDirty.FindNext(count, pos + count)) {
            const uint32 vramOffset = pos << VDP2Resources::kVRAMDirtyBitmapChunkSizeShift;
            const uint32 size = count << VDP2Resources::kVRAMDirtyBitmapChunkSizeShift;

            // Get upload buffer chunk for this transfer
            if (auto result = AllocateUploadBuffer(uploadBuffer, size, 4, alloc); !result) {
                return util::ErrorMessage{
                    fmt::format("Failed to allocate upload buffer for VDP1 VRAM chunk: {}", result.Error().message)};
            }

            // Upload VRAM chunk
            memcpy(alloc.data, &vdpState.mem1.VRAM[vramOffset], size);
            cmdList->CopyBufferRegion(dstResource, vramOffset, uploadBufferPtr, alloc.offset, size);
        }
        vdp1.vramDirty.ClearAll();

        return {};
    }

    [[nodiscard]] util::VoidResult<> VDP1UploadFBRAM() {
        static constexpr size_t kFrameSize = kVDP1FBRAMSize * 2;

        ID3D12Resource *dstResource = vdp1.fbramBuffer.GetPointer();
        ID3D12Resource *uploadBufferPtr = uploadBuffer.GetBufferResource().GetPointer();

        // Transition to copy
        barrierTracker.TransitionBuffer(dstResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_BARRIER_SYNC_COPY,
                                        D3D12_BARRIER_ACCESS_COPY_DEST);
        barrierTracker.Flush(cmdList);

        // Get upload buffer chunk
        UploadAllocation alloc{};
        if (auto result = AllocateUploadBuffer(uploadBuffer, kFrameSize, 4, alloc); !result) {
            return util::ErrorMessage{
                fmt::format("Failed to allocate upload buffer for VDP1 FBRAM: {}", result.Error().message)};
        }

        // Upload buffer
        memcpy(alloc.data, vdpState.mem1.FBRAM.data(), kFrameSize);
        cmdList->CopyBufferRegion(dstResource, kFrameSize * 0, uploadBufferPtr, alloc.offset, kFrameSize);
        if (enhancements.deinterlace) {
            cmdList->CopyBufferRegion(dstResource, kFrameSize * 1, uploadBufferPtr, alloc.offset, kFrameSize);
        }

        if (enhancements.transparentMeshes) {
            // Transition to UAV usage for clearing
            barrierTracker.TransitionBuffer(dstResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                            D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS);
            barrierTracker.Flush(cmdList);

            // Clear mesh buffers
            // Buffers 2 and 3 are the main and alternate mesh buffers respectively
            static constexpr UINT kClearValue[4] = {0, 0, 0, 0};
            const D3D12_RECT rect{
                .left = kFrameSize * 2,
                .top = 0,
                .right = static_cast<LONG>(kFrameSize * (enhancements.deinterlace ? 4 : 3)),
                .bottom = 1,
            };
            cmdList->ClearUnorderedAccessViewUint(vdp1.fbramWriteDescs.GetGPUHandle(1), vdp1.fbramUAV.cpuHandle,
                                                  dstResource, kClearValue, 1, &rect);
        }

        // No longer dirty
        vdp1.fbramDirty.ClearAll();

        return {};
    }

    [[nodiscard]] util::VoidResult<> VDP1FlushFBRAM() {
        if (!vdp1.fbramDirty) {
            return {};
        }

        ID3D12Resource *dstResource = vdp1.fbramWritesBuffer.GetPointer();
        ID3D12Resource *uploadBufferPtr = uploadBuffer.GetBufferResource().GetPointer();

        // Transition to copy
        barrierTracker.TransitionBuffer(dstResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_BARRIER_SYNC_COPY,
                                        D3D12_BARRIER_ACCESS_COPY_DEST);
        barrierTracker.Flush(cmdList);

        auto &fb = vdpState.mem1.FBRAM[vdpState.fbIndex.draw];

        // Group modified FBRAM writes into 32-bit chunks
        std::vector<VDP1FBRAMWrite> writes{};
        size_t pos, count = 0;
        const uint64 *bitmapData = vdp1.fbramDirty.GetData();
        for (pos = vdp1.fbramDirty.FindNextGroup<4>(count); pos < vdp1.fbramDirty.Size();
             pos = vdp1.fbramDirty.FindNextGroup<4>(count, pos + count)) {
            const uint32 baseAddress = pos;

            for (size_t i = 0; i < count; i += 4) {
                VDP1FBRAMWrite &write = writes.emplace_back();
                write.address = baseAddress + i;

                const uint8 bits = bitmapData[write.address >> 6u] >> (write.address & 63u);
                write.andMask = 0;
                for (uint32 j = 0; j < 4; ++j) {
                    const uint32 shift = j * 8u;
                    if (((bits >> j) & 1u) == 0u) {
                        write.andMask |= (0xFFu << shift);
                    }
                }
                write.orMask = util::ReadLE<uint32>(&fb[write.address]);
                write.orMask &= ~write.andMask;
            }
        }
        vdp1.fbramDirty.ClearAll();

        assert(!writes.empty());

        // Get upload buffer chunk for this transfer
        UploadAllocation alloc{};
        const size_t size = writes.size() * sizeof(VDP1FBRAMWrite);
        if (auto result = AllocateUploadBuffer(uploadBuffer, size, 4, alloc); !result) {
            return util::ErrorMessage{
                fmt::format("Failed to allocate upload buffer for VDP1 FBRAM writes list: {}", result.Error().message)};
        }

        // Upload list
        memcpy(alloc.data, writes.data(), size);
        cmdList->CopyBufferRegion(dstResource, 0, uploadBufferPtr, alloc.offset, size);

        // Transition to SRV usage
        barrierTracker.TransitionBuffer(dstResource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                        D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
        barrierTracker.Flush(cmdList);

        // Dispatch FBRAM write compute shader
        const uint32 writeCount = writes.size();
        cmdList->SetPipelineState(vdp1.fbramWritePSO.GetPointer());
        cmdList->SetComputeRootSignature(vdp1.fbramWriteRootSig.GetPointer());
        cmdList->SetComputeRoot32BitConstants(0, sizeof(vdp1.cpuCommonRenderParams) / sizeof(uint32),
                                              &vdp1.cpuCommonRenderParams, 0);
        cmdList->SetComputeRoot32BitConstants(0, 1, &writeCount, sizeof(vdp1.cpuCommonRenderParams) / sizeof(uint32));
        cmdList->SetComputeRootDescriptorTable(1, vdp1.fbramWriteDescs.GetGPUHandle());
        cmdList->Dispatch((writeCount + 63) / 64, 1, 1);

        // Insert UAV barrier to ensure the following shaders see these changes
        barrierTracker.UAVBuffer(vdp1.fbramBuffer.GetPointer());

        // Mark GPU-side FBRAM as dirty
        vdp1.fbramReadbackDirty = true;

        return {};
    }

    void VDP1EraseFramebuffer(uint64 cycles) {
        // Vertical scale is doubled in double-interlace mode
        const VDP1Regs &regs1 = vdpState.regs1;
        const VDP2Regs &regs2 = vdpState.regs2;
        const bool doubleDensity = regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity;
        const uint32 scaleV = doubleDensity ? 1u : 0u;

        // Constrain erase area to certain limits based on current resolution
        const uint32 maxH = (regs2.TVMD.HRESOn & 1) ? 428 : 400;
        const uint32 maxV = VRes >> scaleV;

        vdp1.cpuEraseParams.coords.x1 = std::min<uint32>(regs1.eraseX1Latch, maxH) >> 3u;
        vdp1.cpuEraseParams.coords.y1 = std::min<uint32>(regs1.eraseY1Latch, maxV);
        vdp1.cpuEraseParams.coords.x3 = std::min<uint32>(regs1.eraseX3Latch, maxH) >> 3u;
        vdp1.cpuEraseParams.coords.y3 = std::min<uint32>(regs1.eraseY3Latch, maxV);
        vdp1.cpuEraseParams.coords.scaleV = scaleV;

        vdp1.cpuEraseParams.erase.writeValue = bit::byte_swap<uint16>(regs1.eraseWriteValueLatch);
        vdp1.cpuEraseParams.erase.addressShift = regs1.eraseOffsetShift - 8;

        vdp1.cpuEraseParams.vblank.enable = cycles != 0;
        if (vdp1.cpuEraseParams.vblank.enable) {
            // Compute last line and pixel that can be drawn with the given cycle budget
            const uint32 lineWidth = (vdp1.cpuEraseParams.coords.x3 << 3u) - (vdp1.cpuEraseParams.coords.x1 << 3u);
            if (lineWidth > 0) {
                vdp1.cpuEraseParams.vblank.maxY = cycles / lineWidth;
                vdp1.cpuEraseParams.vblank.maxX = cycles % lineWidth;
            } else {
                vdp1.cpuEraseParams.vblank.maxY = 0;
                vdp1.cpuEraseParams.vblank.maxX = 0;
            }
        }

        // Do framebuffer erase
        const auto &erase = vdp1.cpuEraseParams;
        const uint32 width = (erase.coords.x3 << 3) - (erase.coords.x1 << 3) + 1;
        const uint32 height = erase.coords.y3 - erase.coords.y1 + 1;

        FrameContext &frameCtx = frames.GetCurrentFrame();

        VDP1UpdateCommonRenderParams();
        vdp1.cpuCommonRenderParams.displayParams.drawFB = vdpState.fbIndex.display;

        // Transition FBRAM to UAV usage
        barrierTracker.TransitionBuffer(vdp1.fbramBuffer.GetPointer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                        D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS);
        barrierTracker.Flush(cmdList);

        // Dispatch erase shader
        cmdList->SetPipelineState(frameCtx.erasePSO.GetPointer());
        cmdList->SetComputeRootSignature(vdp1.eraseRootSig.GetPointer());
        cmdList->SetComputeRoot32BitConstants(0, sizeof(vdp1.cpuCommonRenderParams) / sizeof(uint32),
                                              &vdp1.cpuCommonRenderParams, 0);
        cmdList->SetComputeRoot32BitConstants(0, sizeof(vdp1.cpuEraseParams) / sizeof(uint32), &vdp1.cpuEraseParams,
                                              sizeof(vdp1.cpuCommonRenderParams) / sizeof(uint32));
        cmdList->SetComputeRootDescriptorTable(1, frameCtx.eraseDescs.GetGPUHandle());
        cmdList->Dispatch((width + 63) / 64, (height + 31) / 32, 1);
        // NOTE: works on 32-bit units, so two writes per thread, hence why (width+63)/64 instead of +31/32

        // Insert UAV barrier to ensure the following shaders see these changes
        barrierTracker.UAVBuffer(vdp1.fbramBuffer.GetPointer());

        // Mark GPU-side FBRAM as dirty
        vdp1.fbramReadbackDirty = true;
    }

    void VDP1SwapFramebuffer() {
        // Submit any pending spans
        auto spanResult = VDP1SubmitSpans();
        if (spanResult) {
            if (!spanResult.Value()) {
                // Still have to update the rendering parameters
                VDP1UpdateCommonRenderParams();
            }
        } else {
            devlog::warn<grp::dx12_vdp1>("VDP1 span submission failed: {}", spanResult.Error().message);
        }

        VDP1CopyFBRAMToReadback();
    }

    /// @brief Copies GPU-modified FBRAM to the readback buffer.
    void VDP1CopyFBRAMToReadback() {
        if (!vdp1.fbramReadbackDirty) {
            return;
        }
        vdp1.fbramReadbackDirty = false;
        vdp1.fbramReadbackCopyPending = true;

        // Download FBRAM
        barrierTracker.TransitionBuffer(vdp1.fbramBuffer.GetPointer(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                                        D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_COPY_SOURCE);
        barrierTracker.Flush(cmdList);
        cmdList->CopyBufferRegion(vdp1.fbramDownloadBuffer.GetPointer(), 0, vdp1.fbramBuffer.GetPointer(), 0,
                                  kVDP1FBRAMSize * 2);
    }

    void VDP1BeginFrame() {
        const VDP1Regs &regs1 = vdpState.regs1;
        const VDP2Regs &regs2 = vdpState.regs2;
        vdp1.doubleV =
            enhancements.deinterlace && regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity && !regs1.dblInterlaceEnable;
    }

    void VDP1ExecuteCommand(uint32 cmdAddress, VDP1Command::Control control) {
        switch (control.command) {
        case VDP1Command::CommandType::DrawNormalSprite: VDP1Cmd_DrawNormalSprite(cmdAddress, control); break;
        case VDP1Command::CommandType::DrawScaledSprite: VDP1Cmd_DrawScaledSprite(cmdAddress, control); break;
        case VDP1Command::CommandType::DrawDistortedSprite: [[fallthrough]];
        case VDP1Command::CommandType::DrawDistortedSpriteAlt: VDP1Cmd_DrawDistortedSprite(cmdAddress, control); break;

        case VDP1Command::CommandType::DrawPolygon: VDP1Cmd_DrawPolygon(cmdAddress); break;
        case VDP1Command::CommandType::DrawPolylines: [[fallthrough]];
        case VDP1Command::CommandType::DrawPolylinesAlt: VDP1Cmd_DrawPolylines(cmdAddress); break;
        case VDP1Command::CommandType::DrawLine: VDP1Cmd_DrawLine(cmdAddress); break;

        case VDP1Command::CommandType::UserClipping: [[fallthrough]];
        case VDP1Command::CommandType::UserClippingAlt: VDP1Cmd_SetUserClipping(cmdAddress); break;
        case VDP1Command::CommandType::SystemClipping: VDP1Cmd_SetSystemClipping(cmdAddress); break;
        case VDP1Command::CommandType::SetLocalCoordinates: VDP1Cmd_SetLocalCoordinates(cmdAddress); break;
        }
    }

    struct VDP1CommandData {
        VDP1Command::DrawMode mode;
        uint16 color;

        uint32 charAddr;
        VDP1Command::Size size;
    };

    struct VDP1SpanData {
        uint16 cmdIndex;
        VDP1Command::DrawMode mode;
        Color555 gouraud0;
        Color555 gouraud1;

        uint32 charAddr;
        VDP1Command::Size size;
        uint32 texV;
        bool flipH;
        uint32 endCodeIndex;
    };

    util::ValueResult<bool> VDP1SubmitSpans() {
        // TODO: scaled VDP1 command and span lists need to be tracked separately on the CPU side, but can reuse the
        // same span and command buffers. VDP1 will always have to render at normal resolution in addition to the
        // enhanced/scaled version regardless of resolution scaling because of visible FBRAM effects

        FrameContext &frameCtx = frames.GetCurrentFrame();
        if (frameCtx.cpuSpanCount == 0) {
            // No spans to dispatch
            frameCtx.cpuCmdCount = 0;
            return false;
        }

        // Clear counters even if we fail to submit them to avoid crashes on extreme cases.
        // Errors should never happen, however.
        util::ScopeGuard sgFinish{[&] {
            frameCtx.cpuSpanCount = 0;
            frameCtx.cpuCmdCount = 0;
            vdp1.vramTexUsageTracker.Clear();
        }};

        // We should have a shader selected by now
        assert(vdp1.currPolyDrawShaderIndex != -1);

        VDP1UpdateCommonRenderParams();
        vdp1.cpuPolyDrawParams.numSpans = frameCtx.cpuSpanCount;

        if (auto result = VDP1FlushVRAM(); !result) {
            devlog::warn<grp::dx12_vdp1>("VDP1 VRAM flush failed: {}", result.Error().message);
        }
        if (auto result = VDP1FlushFBRAM(); !result) {
            devlog::warn<grp::dx12_vdp1>("VDP1 FBRAM flush failed: {}", result.Error().message);
        }

        ID3D12Resource *uploadBufferPtr = uploadBuffer.GetBufferResource().GetPointer();
        UploadAllocation alloc{};

        barrierTracker.TransitionBuffer(frameCtx.spanParamsBuffer.GetPointer(), D3D12_RESOURCE_STATE_COPY_DEST,
                                        D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_COPY_DEST);
        barrierTracker.TransitionBuffer(frameCtx.spanPrefixSumsBuffer.GetPointer(), D3D12_RESOURCE_STATE_COPY_DEST,
                                        D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_COPY_DEST);
        barrierTracker.TransitionBuffer(frameCtx.cmdParamsBuffer.GetPointer(), D3D12_RESOURCE_STATE_COPY_DEST,
                                        D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_COPY_DEST);
        barrierTracker.Flush(cmdList);

        // Upload spans
        {
            const size_t size = sizeof(VDP1SpanParams) * frameCtx.cpuSpanCount;
            if (auto result = AllocateUploadBuffer(uploadBuffer, size, 4, alloc); !result) {
                return util::ErrorMessage{fmt::format("Failed to allocate upload buffer for VDP1 span parameters: {}",
                                                      result.Error().message)};
            }
            memcpy(alloc.data, &frameCtx.cpuSpanParams, size);

            cmdList->CopyBufferRegion(frameCtx.spanParamsBuffer.GetPointer(), 0, uploadBufferPtr, alloc.offset, size);
        }

        // Upload prefix sums
        {
            const size_t size = sizeof(HLSLuint) * (frameCtx.cpuSpanCount + 1);
            if (auto result = AllocateUploadBuffer(uploadBuffer, size, 4, alloc); !result) {
                return util::ErrorMessage{fmt::format("Failed to allocate upload buffer for VDP1 span prefix sums: {}",
                                                      result.Error().message)};
            }
            memcpy(alloc.data, &frameCtx.cpuSpanPrefixSums, size);

            cmdList->CopyBufferRegion(frameCtx.spanPrefixSumsBuffer.GetPointer(), 0, uploadBufferPtr, alloc.offset,
                                      size);
        }

        // Upload commands
        {
            const size_t size = sizeof(VDP1CommandParams) * frameCtx.cpuCmdCount;
            if (auto result = AllocateUploadBuffer(uploadBuffer, size, 4, alloc); !result) {
                return util::ErrorMessage{fmt::format(
                    "Failed to allocate upload buffer for VDP1 commands parameters: {}", result.Error().message)};
            }
            memcpy(alloc.data, &frameCtx.cpuCmdParams, size);

            cmdList->CopyBufferRegion(frameCtx.cmdParamsBuffer.GetPointer(), 0, uploadBufferPtr, alloc.offset, size);
        }

        barrierTracker.TransitionBuffer(frameCtx.spanParamsBuffer.GetPointer(),
                                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                        D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
        barrierTracker.TransitionBuffer(frameCtx.spanPrefixSumsBuffer.GetPointer(),
                                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                        D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
        barrierTracker.TransitionBuffer(frameCtx.cmdParamsBuffer.GetPointer(),
                                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                        D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
        barrierTracker.TransitionBuffer(vdp1.vramBuffer.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                        D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
        barrierTracker.TransitionBuffer(vdp1.internalSpriteOutBuffer.GetPointer(),
                                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_BARRIER_SYNC_COMPUTE_SHADING,
                                        D3D12_BARRIER_ACCESS_UNORDERED_ACCESS);

        const bool isOIT = IsVDP1PolyDrawShaderOIT(vdp1.currPolyDrawShaderIndex);
        const bool isMSB = IsVDP1PolyDrawShaderMSB(vdp1.currPolyDrawShaderIndex);

        if (isMSB) {
            barrierTracker.TransitionBuffer(vdp1.fbramBuffer.GetPointer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                            D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS);
        } else if (isOIT) {
            barrierTracker.TransitionBuffer(frameCtx.oitListHeadsBuffer.GetPointer(),
                                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_BARRIER_SYNC_COMPUTE_SHADING,
                                            D3D12_BARRIER_ACCESS_UNORDERED_ACCESS);
            barrierTracker.TransitionBuffer(frameCtx.oitFragmentsBuffer.GetPointer(),
                                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_BARRIER_SYNC_COMPUTE_SHADING,
                                            D3D12_BARRIER_ACCESS_UNORDERED_ACCESS);
            barrierTracker.TransitionBuffer(frameCtx.oitCounterBuffer.GetPointer(),
                                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_BARRIER_SYNC_COMPUTE_SHADING,
                                            D3D12_BARRIER_ACCESS_UNORDERED_ACCESS);
            barrierTracker.Flush(cmdList);

            // Reset atomic counter
            static constexpr UINT kClearValue[4] = {0, 0, 0, 0};
            cmdList->ClearUnorderedAccessViewUint(frameCtx.polyDrawOITDescs.GetGPUHandle(6),
                                                  frameCtx.oitCounterUAV.cpuHandle,
                                                  frameCtx.oitCounterBuffer.GetPointer(), kClearValue, 0, nullptr);

            barrierTracker.UAVBuffer(frameCtx.oitCounterBuffer.GetPointer());
        }
        barrierTracker.Flush(cmdList);

        // Dispatch polygon drawing shader
        const D3D12RootSignature &rootSig = isOIT ? vdp1.polyDrawOITRootSig : vdp1.polyDrawRootSig;
        const DescriptorTable &descs = isOIT   ? frameCtx.polyDrawOITDescs
                                       : isMSB ? frameCtx.polyDrawMSBDescs
                                               : frameCtx.polyDrawDescs;
        cmdList->SetPipelineState(frameCtx.polyDrawPSOs[vdp1.currPolyDrawShaderIndex].GetPointer());
        cmdList->SetComputeRootSignature(rootSig.GetPointer());
        cmdList->SetComputeRoot32BitConstants(0, sizeof(vdp1.cpuCommonRenderParams) / sizeof(uint32),
                                              &vdp1.cpuCommonRenderParams, 0);
        cmdList->SetComputeRoot32BitConstants(0, sizeof(vdp1.cpuPolyDrawParams) / sizeof(uint32),
                                              &vdp1.cpuPolyDrawParams,
                                              sizeof(vdp1.cpuCommonRenderParams) / sizeof(uint32));
        cmdList->SetComputeRootDescriptorTable(1, descs.GetGPUHandle());
        cmdList->Dispatch((frameCtx.cpuSpanPrefixSums[frameCtx.cpuSpanCount] + 63) / 64, 1, 1);

        // Merge output into FBRAM if needed.
        // MSB shader writes directly to FBRAM, no merging needed.
        if (!isMSB) {
            const bool isMergerOIT = IsVDP1OutputMergerShaderOIT(vdp1.currOutputMergerShaderIndex);
            if (isMergerOIT) {
                barrierTracker.TransitionBuffer(
                    frameCtx.oitListHeadsBuffer.GetPointer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS);
                barrierTracker.TransitionBuffer(
                    frameCtx.oitFragmentsBuffer.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            }
            barrierTracker.TransitionBuffer(vdp1.fbramBuffer.GetPointer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                            D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS);
            barrierTracker.UAVBuffer(vdp1.internalSpriteOutBuffer.GetPointer());
            barrierTracker.Flush(cmdList);

            // Set up parameters
            const D3D12RootSignature &rootSig = isMergerOIT ? vdp1.outputMergerOITRootSig : vdp1.outputMergerRootSig;
            const DescriptorTable &tbl = isMergerOIT ? frameCtx.outputMergerOITDescs : frameCtx.outputMergerDescs;
            const VDP1Regs &regs1 = vdpState.regs1;
            const VDP2Regs &regs2 = vdpState.regs2;
            const uint32 pixelsPerEntry = regs1.pixel8Bits ? 4u : 2u; // each entry is 32 bits
            const uint32 mergeW = regs1.fbSizeH / pixelsPerEntry;
            const uint32 mergeH = regs1.fbSizeV;
            const uint32 mergeZ = regs2.TVMD.IsInterlaced() && enhancements.deinterlace ? 2 : 1;

            // Dispatch output merger shader
            cmdList->SetPipelineState(frameCtx.outputMergerPSOs[vdp1.currOutputMergerShaderIndex].GetPointer());
            cmdList->SetComputeRootSignature(rootSig.GetPointer());
            cmdList->SetComputeRoot32BitConstants(0, sizeof(vdp1.cpuCommonRenderParams) / sizeof(uint32),
                                                  &vdp1.cpuCommonRenderParams, 0);
            cmdList->SetComputeRootDescriptorTable(1, tbl.GetGPUHandle());
            cmdList->Dispatch((mergeW + 7) / 8, (mergeH + 7) / 8, mergeZ);
        }

        vdp1.fbramReadbackDirty = true;

        return true;
    }

    void VDP1UpdateCommonRenderParams() {
        VDP1CommonRenderParams &params = vdp1.cpuCommonRenderParams;
        const VDP1Regs &regs1 = vdpState.regs1;
        const VDP2Regs &regs2 = vdpState.regs2;
        const bool doubleDensity = regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity;

        auto &displayParams = params.displayParams;
        displayParams.fbSizeH = std::countr_zero(regs1.fbSizeH) - 9u;
        displayParams.fbSizeV = std::countr_zero(regs1.fbSizeV) - 8u;
        displayParams.pixel8Bits = regs1.pixel8Bits;
        displayParams.doubleDensity = doubleDensity;
        displayParams.dblInterlaceEnable = regs1.dblInterlaceEnable;
        displayParams.dblInterlaceDrawLine = regs1.dblInterlaceDrawLine;
        displayParams.evenOddCoordSelect = regs1.evenOddCoordSelect;
        displayParams.drawFB = vdpState.fbIndex.draw;
        displayParams.hresMode = regs2.TVMD.HRESOn;
        displayParams.vresMode = regs2.TVMD.VRESOn;
    }

    void VDP1SelectPolyDrawShader(VDP1Command::DrawMode mode) {
        // Submit existing spans before switching shaders
        const size_t polyDrawIndex = MakeVDP1PolyDrawShaderIndex(mode);
        const size_t outputMergerIndex = MakeVDP1OutputMergerShaderIndex(mode);
        if (vdp1.currPolyDrawShaderIndex != polyDrawIndex || vdp1.currOutputMergerShaderIndex != outputMergerIndex) {
            VDP1SubmitSpans();
            vdp1.currPolyDrawShaderIndex = polyDrawIndex;
            vdp1.currOutputMergerShaderIndex = outputMergerIndex;
        }
    }

    uint16 VDP1AddCommand(const VDP1CommandData &data, bool textured) {
        FrameContext &frameCtx = frames.GetCurrentFrame();

        // If list is full, flush it
        if (frameCtx.cpuCmdCount + 1 >= kMaxVDP1Commands) {
            VDP1SubmitSpans();
        }

        // Switch polygon drawing shader based on the current settings
        VDP1SelectPolyDrawShader(data.mode);

        const uint16 cmdIndex = frameCtx.cpuCmdCount++;
        VDP1CommandParams &cmdParams = frameCtx.cpuCmdParams[cmdIndex];

        const VDP1State &state = vdpState.state1;
        const sint32 doubleV = vdp1.doubleV ? 1 : 0;
        cmdParams.userClip0.x = state.userClipX0;
        cmdParams.userClip0.y = (state.userClipY0 << doubleV) | doubleV;
        cmdParams.userClip1.x = state.userClipX1;
        cmdParams.userClip1.y = (state.userClipY1 << doubleV) | doubleV;
        cmdParams.sysClip.h = state.sysClipH;
        cmdParams.sysClip.v = (state.sysClipV << doubleV) | doubleV;

        cmdParams.cmdcolr = data.color;
        cmdParams.cmdpmod = data.mode.u16;
        if (textured) {
            cmdParams.cmdsize = data.size.u16;
            cmdParams.cmdsrca = data.charAddr >> 3u;
        }

        return cmdIndex;
    }

    bool VDP1AddSpan(CoordS32 coord0, CoordS32 coord1, VDP1SpanData &data, bool textured, bool antialias) {
        // Discard if completely out of bounds
        if (coord0.x() < 0 && coord1.x() < 0) {
            return false;
        }
        if (coord0.y() < 0 && coord1.y() < 0) {
            return false;
        }
        const sint32 sysClipH = vdpState.state1.sysClipH;
        if (coord0.x() > sysClipH && coord1.x() > sysClipH) {
            return false;
        }
        const sint32 doubleV = vdp1.doubleV ? 1 : 0;
        const sint32 sysClipV = (vdpState.state1.sysClipV << doubleV) | doubleV;
        if (coord0.y() > sysClipV && coord1.y() > sysClipV) {
            return false;
        }

        // Mark transparent mesh drawn if that's the case
        if (data.mode.meshEnable && enhancements.transparentMeshes) {
            vdp1.transparentMeshDrawn = true;
        }

        // Determine span length
        LineStepper line{coord0, coord1};
        const uint32 skip = line.SystemClip(sysClipH, sysClipV);
        const uint32 length = line.Length();

        if (length == 0) {
            // Entire line was clipped
            return false;
        }

        const bool isOIT = IsVDP1PolyDrawShaderOIT(vdp1.currPolyDrawShaderIndex);
        const uint32 fragLimit = isOIT ? kMaxVDP1OITFragmentsPerDispatch : kMaxVDP1FragmentsPerDispatch;

        // Submit spans now if the total fragment count would exceed the limit
        FrameContext &frameCtx = frames.GetCurrentFrame();
        if (frameCtx.cpuSpanPrefixSums[frameCtx.cpuSpanCount] + length >= fragLimit) {
            VDP1SubmitSpans();

            // Readd command
            frameCtx.cpuCmdParams[0] = frameCtx.cpuCmdParams[data.cmdIndex];
            frameCtx.cpuCmdCount = 1;
            data.cmdIndex = 0;
        }

        // Append span to list
        VDP1SpanParams &spanParams = frameCtx.cpuSpanParams[frameCtx.cpuSpanCount];

        const auto [x0, y0] = coord0;
        const auto [x1, y1] = coord1;

        spanParams.coord0 = {x0, y0};
        spanParams.coord1 = {x1, y1};
        spanParams.cmdIndex = data.cmdIndex;
        spanParams.skip = skip;
        spanParams.attrs.antialias = antialias;

        if (data.mode.gouraudEnable) {
            spanParams.gouraud0.r = data.gouraud0.r;
            spanParams.gouraud0.g = data.gouraud0.g;
            spanParams.gouraud0.b = data.gouraud0.b;
            spanParams.gouraud1.r = data.gouraud1.r;
            spanParams.gouraud1.g = data.gouraud1.g;
            spanParams.gouraud1.b = data.gouraud1.b;
        }

        spanParams.attrs.textured = textured;
        if (textured) {
            spanParams.attrs.texV = data.texV;
            spanParams.attrs.flipH = data.flipH;
            spanParams.attrs.endCodeIndex = data.endCodeIndex;
        }

        // Update prefix sum
        HLSLuint &nextSum = frameCtx.cpuSpanPrefixSums[frameCtx.cpuSpanCount + 1];
        const HLSLuint currSum = frameCtx.cpuSpanPrefixSums[frameCtx.cpuSpanCount];
        nextSum = currSum + length;

        // If list is full, flush it
        ++frameCtx.cpuSpanCount;
        if (frameCtx.cpuSpanCount >= frameCtx.cpuSpanParams.size()) {
            VDP1SubmitSpans();

            // Readd command
            frameCtx.cpuCmdParams[0] = frameCtx.cpuCmdParams[data.cmdIndex];
            frameCtx.cpuCmdCount = 1;
            data.cmdIndex = 0;
        }

        // Indicate that the span was drawn
        return true;
    }

    void VDP1PlotTexturedQuad(VDP1SpanData &data, uint32 cmdAddress, VDP1Command::Control control, CoordS32 coordA,
                              CoordS32 coordB, CoordS32 coordC, CoordS32 coordD) {
        QuadStepper quad{coordA, coordB, coordC, coordD};

        if (data.mode.gouraudEnable) {
            const uint32 gouraudTable = static_cast<uint32>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x1C)) << 3u;
            Color555 gouraudA;
            Color555 gouraudB;
            Color555 gouraudC;
            Color555 gouraudD;
            gouraudA.u16 = vdpState.mem1.ReadVRAM<uint16>(gouraudTable + 0u);
            gouraudB.u16 = vdpState.mem1.ReadVRAM<uint16>(gouraudTable + 2u);
            gouraudC.u16 = vdpState.mem1.ReadVRAM<uint16>(gouraudTable + 4u);
            gouraudD.u16 = vdpState.mem1.ReadVRAM<uint16>(gouraudTable + 6u);
            quad.SetupGouraud(gouraudA, gouraudB, gouraudC, gouraudD);
        }

        // A width of zero results in the first fetched texel being used for the entire texture.
        // We simulate this by reducing the texture height to one.
        const uint32 charSizeH = data.size.H * 8;
        const uint32 charSizeV = data.size.V;
        const bool flipV = control.flipV;
        TextureStepper texVStepper{};
        quad.SetupTexture(texVStepper, charSizeH == 0 ? 1 : charSizeV, flipV);

        data.texV = charSizeV; // out of range value to ensure first iteration fetches end codes

        // Optimization for the case where the quad goes outside the system clipping area.
        // Skip rendering the rest of the quad when a line is clipped after plotting at least one line.
        // The first few lines of the quad could also be clipped; that is accounted for by requiring at least one
        // plotted line. The point is to skip the calculations once the quad iterator reaches a point where no more
        // lines can be plotted because they all sit outside the system clip area.
        //
        // This also handles a degenerate case with a bowtie quad sitting outside the corner of the screen with two
        // points poking into the screen area in a configuration similar to this:
        //
        //                       D
        //                        B
        //   +-----------------+
        //   |            A    |
        //   |               C |
        //   |                 |
        //   |                 |
        //   |                 |
        //   +-----------------+
        //
        // In this case, the line gets fully clipped partway through the quad, but comes back into view at the end, so
        // we need to check for two sequences of plotted lines rather than one.
        bool linePlotted = false;
        int plottedSegmentsCount = 0;
        const int plottedSegmentsMax = quad.IsDegenerate() ? 2 : 1;

        // Mark VRAM as in use for this texture
        uint32 texSize = charSizeH * charSizeV;
        switch (data.mode.colorMode) {
        case 0: [[fallthrough]];       // 4 bpp, 16 colors, bank mode
        case 1: texSize >>= 1u; break; // 4 bpp, 16 colors, lookup table mode
        case 2: [[fallthrough]];       // 8 bpp, 64 colors, bank mode
        case 3: [[fallthrough]];       // 8 bpp, 128 colors, bank mode
        case 4: break;                 // 8 bpp, 256 colors, bank mode
        case 5: texSize <<= 1u; break; // 16 bpp, 32768 colors, RGB mode
        }
        devlog::trace<grp::dx12_vdp1>("Tracking VRAM usage for texture: {:05X}..{:05X}", data.charAddr,
                                      data.charAddr + texSize - 1);
        vdp1.vramTexUsageTracker.MarkRange(data.charAddr, texSize);

        // TODO: cache this
        auto findEndCodeIndex = [&](uint32 v) -> uint32 {
            if (data.mode.endCodeDisable) {
                return charSizeH;
            }

            int endCodeCount = 0;

            for (uint32 i = 0; i < charSizeH; ++i) {
                const uint32 u = control.flipH ? charSizeH - 1 - i : i;

                const uint32 charIndex = u + v * charSizeH;

                auto processEndCode = [&](bool endCode) -> bool {
                    if (endCode && !data.mode.endCodeDisable) {
                        ++endCodeCount;
                    }
                    return endCodeCount >= 2;
                };

                // Read next texel
                uint32 color;
                switch (data.mode.colorMode) {
                case 0: // 4 bpp, 16 colors, bank mode
                    color = vdpState.mem1.ReadVRAM<uint8>(data.charAddr + (charIndex >> 1));
                    color = (color >> ((~u & 1) * 4)) & 0xF;
                    if (processEndCode(color == 0xF)) {
                        return u;
                    }
                    break;
                case 1: // 4 bpp, 16 colors, lookup table mode
                    color = vdpState.mem1.ReadVRAM<uint8>(data.charAddr + (charIndex >> 1));
                    color = (color >> ((~u & 1) * 4)) & 0xF;
                    if (processEndCode(color == 0xF)) {
                        return u;
                    }
                    break;
                case 2: // 8 bpp, 64 colors, bank mode
                    color = vdpState.mem1.ReadVRAM<uint8>(data.charAddr + charIndex);
                    if (processEndCode(color == 0xFF)) {
                        return u;
                    }
                    break;
                case 3: // 8 bpp, 128 colors, bank mode
                    color = vdpState.mem1.ReadVRAM<uint8>(data.charAddr + charIndex);
                    if (processEndCode(color == 0xFF)) {
                        return u;
                    }
                    break;
                case 4: // 8 bpp, 256 colors, bank mode
                    color = vdpState.mem1.ReadVRAM<uint8>(data.charAddr + charIndex);
                    if (processEndCode(color == 0xFF)) {
                        return u;
                    }
                    break;
                case 5: // 16 bpp, 32768 colors, RGB mode
                    color = vdpState.mem1.ReadVRAM<uint16>(data.charAddr + charIndex * sizeof(uint16));
                    if (processEndCode(color == 0x7FFF)) {
                        return u;
                    }
                    break;
                }
            };
            return charSizeH;
        };

        // Interpolate linearly over edges A-D and B-C
        for (; quad.CanStep(); quad.Step()) {
            // Plot lines between the interpolated points
            const CoordS32 coordL = quad.LeftEdge().Coord();
            const CoordS32 coordR = quad.RightEdge().Coord();

            while (texVStepper.ShouldStepTexel()) {
                texVStepper.StepTexel();
            }
            texVStepper.StepPixel();

            const uint32 newTexV = texVStepper.Value();
            if (newTexV != data.texV) {
                data.texV = newTexV;
                data.endCodeIndex = findEndCodeIndex(newTexV);
            }

            if (data.mode.gouraudEnable) {
                data.gouraud0 = quad.LeftEdge().GouraudValue();
                data.gouraud1 = quad.RightEdge().GouraudValue();
            }

            if (VDP1AddSpan(coordL, coordR, data, true, true)) {
                if (!linePlotted) {
                    linePlotted = true;
                    ++plottedSegmentsCount;
                }
            } else if (plottedSegmentsCount >= plottedSegmentsMax) {
                // No more lines can be drawn past this point
                break;
            } else {
                linePlotted = false;
            }
        }
    }

    void VDP1Cmd_DrawNormalSprite(uint32 cmdAddress, VDP1Command::Control control) {
        if (!vdpState.state2.layerEnabled[0]) {
            return;
        }
        const VDP1State &state = vdpState.state1;

        const VDP1Command::DrawMode mode{.u16 = vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x04)};
        const uint16 color = vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x06);
        const uint32 charAddr = vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x08) << 3u;
        const VDP1Command::Size size{.u16 = vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x0A)};
        const sint32 xa = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x0C)) + state.localCoordX;
        const sint32 ya = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x0E)) + state.localCoordY;
        const uint32 charSizeH = size.H * 8;
        const uint32 charSizeV = size.V;

        const sint32 xb = xa + std::max(charSizeH, 1u) - 1u; // right X
        const sint32 yb = ya + std::max(charSizeV, 1u) - 1u; // bottom Y

        const sint32 doubleV = vdp1.doubleV ? 1 : 0;
        const sint32 yAdd = enhancements.deinterlace ? doubleV : 0;

        const CoordS32 coordA{xa, ya << doubleV};
        const CoordS32 coordB{xb, ya << doubleV};
        const CoordS32 coordC{xb, (yb << doubleV) + yAdd};
        const CoordS32 coordD{xa, (yb << doubleV) + yAdd};

        const VDP1CommandData cmdData{
            .mode = mode,
            .color = color,
            .charAddr = charAddr,
            .size = size,
        };
        const uint16 cmdIndex = VDP1AddCommand(cmdData, true);

        VDP1SpanData spanData{
            .cmdIndex = cmdIndex,
            .mode = mode,
            .charAddr = charAddr,
            .size = size,
            .flipH = control.flipH != 0,
        };

        VDP1PlotTexturedQuad(spanData, cmdAddress, control, coordA, coordB, coordC, coordD);
    }

    void VDP1Cmd_DrawScaledSprite(uint32 cmdAddress, VDP1Command::Control control) {
        if (!vdpState.state2.layerEnabled[0]) {
            return;
        }

        const VDP1State &state = vdpState.state1;

        const VDP1Command::DrawMode mode{.u16 = vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x04)};
        const uint16 color = vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x06);
        const uint32 charAddr = vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x08) << 3u;
        const VDP1Command::Size size{.u16 = vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x0A)};
        const sint32 xa = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x0C));
        const sint32 ya = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x0E));

        // Calculated quad coordinates
        sint32 qxa = xa;
        sint32 qya = ya;
        sint32 qxb = xa;
        sint32 qyb = ya;
        sint32 qxc = xa;
        sint32 qyc = ya;
        sint32 qxd = xa;
        sint32 qyd = ya;

        const uint8 zoomPointH = bit::extract<0, 1>(control.zoomPoint);
        const uint8 zoomPointV = bit::extract<2, 3>(control.zoomPoint);

        if (zoomPointH == 0) {
            const sint32 xc = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x14));

            qxb = xc;
            qxc = xc;
        } else {
            const sint32 xb = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x10));

            switch (zoomPointH) {
            case 1:
                qxb += xb;
                qxc += xb;
                break;
            case 2:
                qxa -= xb >> 1;
                qxb += (xb + 1) >> 1;
                qxc += (xb + 1) >> 1;
                qxd -= xb >> 1;
                break;
            case 3:
                qxa -= xb;
                qxd -= xb;
                break;
            }
        }

        if (zoomPointV == 0) {
            const sint32 yc = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x16));

            qyc = yc;
            qyd = yc;
        } else {
            const sint32 yb = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x12));

            switch (zoomPointV) {
            case 1:
                qyc += yb;
                qyd += yb;
                break;
            case 2:
                qya -= yb >> 1;
                qyb -= yb >> 1;
                qyc += (yb + 1) >> 1;
                qyd += (yb + 1) >> 1;
                break;
            case 3:
                qya -= yb;
                qyb -= yb;
                break;
            }
        }

        qxa += state.localCoordX;
        qya += state.localCoordY;
        qxb += state.localCoordX;
        qyb += state.localCoordY;
        qxc += state.localCoordX;
        qyc += state.localCoordY;
        qxd += state.localCoordX;
        qyd += state.localCoordY;

        const sint32 doubleV = vdp1.doubleV ? 1 : 0;
        const sint32 yAdd = enhancements.deinterlace ? doubleV : 0;

        const CoordS32 coordA{qxa, qya << doubleV};
        const CoordS32 coordB{qxb, qyb << doubleV};
        const CoordS32 coordC{qxc, (qyc << doubleV) + yAdd};
        const CoordS32 coordD{qxd, (qyd << doubleV) + yAdd};

        const VDP1CommandData cmdData{
            .mode = mode,
            .color = color,
            .charAddr = charAddr,
            .size = size,
        };
        const uint16 cmdIndex = VDP1AddCommand(cmdData, true);

        VDP1SpanData spanData{
            .cmdIndex = cmdIndex,
            .mode = mode,
            .charAddr = charAddr,
            .size = size,
            .flipH = control.flipH != 0,
        };

        VDP1PlotTexturedQuad(spanData, cmdAddress, control, coordA, coordB, coordC, coordD);
    }

    void VDP1Cmd_DrawDistortedSprite(uint32 cmdAddress, VDP1Command::Control control) {
        if (!vdpState.state2.layerEnabled[0]) {
            return;
        }

        const VDP1State &state = vdpState.state1;

        const VDP1Command::DrawMode mode{.u16 = vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x04)};
        const uint16 color = vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x06);
        const uint32 charAddr = vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x08) << 3u;
        const VDP1Command::Size size{.u16 = vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x0A)};
        const sint32 xa = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x0C)) + state.localCoordX;
        const sint32 ya = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x0E)) + state.localCoordY;
        const sint32 xb = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x10)) + state.localCoordX;
        const sint32 yb = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x12)) + state.localCoordY;
        const sint32 xc = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x14)) + state.localCoordX;
        const sint32 yc = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x16)) + state.localCoordY;
        const sint32 xd = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x18)) + state.localCoordX;
        const sint32 yd = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x1A)) + state.localCoordY;

        const bool isRegularRect = (xa == xd) && (xb == xc) && (ya == yb) && (yc == yd);

        const sint32 doubleV = vdp1.doubleV ? 1 : 0;
        const sint32 yAddAB = enhancements.deinterlace && isRegularRect && (ya >= yc) ? doubleV : 0;
        const sint32 yAddCD = enhancements.deinterlace && isRegularRect && (ya < yc) ? doubleV : 0;

        const CoordS32 coordA{xa, (ya << doubleV) + yAddAB};
        const CoordS32 coordB{xb, (yb << doubleV) + yAddAB};
        const CoordS32 coordC{xc, (yc << doubleV) + yAddCD};
        const CoordS32 coordD{xd, (yd << doubleV) + yAddCD};

        const VDP1CommandData cmdData{
            .mode = mode,
            .color = color,
            .charAddr = charAddr,
            .size = size,
        };
        const uint16 cmdIndex = VDP1AddCommand(cmdData, true);

        VDP1SpanData spanData{
            .cmdIndex = cmdIndex,
            .mode = mode,
            .charAddr = charAddr,
            .size = size,
            .flipH = control.flipH != 0,
        };

        VDP1PlotTexturedQuad(spanData, cmdAddress, control, coordA, coordB, coordC, coordD);
    }

    void VDP1Cmd_DrawPolygon(uint32 cmdAddress) {
        if (!vdpState.state2.layerEnabled[0]) {
            return;
        }

        const VDP1State &state = vdpState.state1;
        const VDP1Command::DrawMode mode{.u16 = vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x04)};

        const uint16 color = vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x06);
        const sint32 xa = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x0C)) + state.localCoordX;
        const sint32 ya = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x0E)) + state.localCoordY;
        const sint32 xb = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x10)) + state.localCoordX;
        const sint32 yb = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x12)) + state.localCoordY;
        const sint32 xc = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x14)) + state.localCoordX;
        const sint32 yc = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x16)) + state.localCoordY;
        const sint32 xd = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x18)) + state.localCoordX;
        const sint32 yd = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x1A)) + state.localCoordY;
        const uint32 gouraudTable = static_cast<uint32>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x1C)) << 3u;

        const bool isRegularRect = (xa == xd) && (xb == xc) && (ya == yb) && (yc == yd);

        const sint32 doubleV = vdp1.doubleV ? 1 : 0;
        const sint32 yAddAB = enhancements.deinterlace && isRegularRect && (ya >= yc) ? doubleV : 0;
        const sint32 yAddCD = enhancements.deinterlace && isRegularRect && (ya < yc) ? doubleV : 0;

        const CoordS32 coordA{xa, (ya << doubleV) + yAddAB};
        const CoordS32 coordB{xb, (yb << doubleV) + yAddAB};
        const CoordS32 coordC{xc, (yc << doubleV) + yAddCD};
        const CoordS32 coordD{xd, (yd << doubleV) + yAddCD};

        Color555 gouraudA;
        Color555 gouraudB;
        Color555 gouraudC;
        Color555 gouraudD;
        if (mode.gouraudEnable) {
            gouraudA.u16 = vdpState.mem1.ReadVRAM<uint16>(gouraudTable + 0u);
            gouraudB.u16 = vdpState.mem1.ReadVRAM<uint16>(gouraudTable + 2u);
            gouraudC.u16 = vdpState.mem1.ReadVRAM<uint16>(gouraudTable + 4u);
            gouraudD.u16 = vdpState.mem1.ReadVRAM<uint16>(gouraudTable + 6u);
        }

        const VDP1CommandData cmdData{
            .mode = mode,
            .color = color,
        };
        const uint16 cmdIndex = VDP1AddCommand(cmdData, false);

        VDP1SpanData spanData{
            .cmdIndex = cmdIndex,
            .mode = mode,
        };

        QuadStepper quad{coordA, coordB, coordC, coordD};

        if (mode.gouraudEnable) {
            quad.SetupGouraud(gouraudA, gouraudB, gouraudC, gouraudD);
        }

        // Optimization for the case where the quad goes outside the system clipping area.
        // Skip rendering the rest of the quad when a line is clipped after plotting at least one line.
        // The first few lines of the quad could also be clipped; that is accounted for by requiring at least one
        // plotted line. The point is to skip the calculations once the quad iterator reaches a point where no more
        // lines can be plotted because they all sit outside the system clip area.
        //
        // This also handles a degenerate case with a bowtie quad sitting outside the corner of the screen with two
        // points poking into the screen area in a configuration similar to this:
        //
        //                       D
        //                        B
        //   +-----------------+
        //   |            A    |
        //   |               C |
        //   |                 |
        //   |                 |
        //   |                 |
        //   +-----------------+
        //
        // In this case, the line gets fully clipped partway through the quad, but comes back into view at the end, so
        // we need to check for two sequences of plotted lines rather than one.
        bool linePlotted = false;
        int plottedSegmentsCount = 0;
        const int plottedSegmentsMax = quad.IsDegenerate() ? 2 : 1;

        // Interpolate linearly over edges A-D and B-C
        for (; quad.CanStep(); quad.Step()) {
            // Plot lines between the interpolated points
            const CoordS32 coordL = quad.LeftEdge().Coord();
            const CoordS32 coordR = quad.RightEdge().Coord();

            if (mode.gouraudEnable) {
                spanData.gouraud0 = quad.LeftEdge().GouraudValue();
                spanData.gouraud1 = quad.RightEdge().GouraudValue();
            }

            if (VDP1AddSpan(coordL, coordR, spanData, false, true)) {
                if (!linePlotted) {
                    linePlotted = true;
                    ++plottedSegmentsCount;
                }
            } else if (plottedSegmentsCount >= plottedSegmentsMax) {
                // No more lines can be drawn past this point
                break;
            } else {
                linePlotted = false;
            }
        }
    }

    void VDP1Cmd_DrawPolylines(uint32 cmdAddress) {
        if (!vdpState.state2.layerEnabled[0]) {
            return;
        }

        const VDP1State &state = vdpState.state1;
        const VDP1Command::DrawMode mode{.u16 = vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x04)};

        const uint16 color = vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x06);
        const sint32 xa = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x0C)) + state.localCoordX;
        const sint32 ya = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x0E)) + state.localCoordY;
        const sint32 xb = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x10)) + state.localCoordX;
        const sint32 yb = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x12)) + state.localCoordY;
        const sint32 xc = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x14)) + state.localCoordX;
        const sint32 yc = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x16)) + state.localCoordY;
        const sint32 xd = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x18)) + state.localCoordX;
        const sint32 yd = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x1A)) + state.localCoordY;
        const uint32 gouraudTable = static_cast<uint32>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x1C)) << 3u;

        const bool isRegularRect = (xa == xd) && (xb == xc) && (ya == yb) && (yc == yd);

        const sint32 doubleV = vdp1.doubleV ? 1 : 0;
        const sint32 yAddAB = enhancements.deinterlace && isRegularRect && (ya >= yc) ? doubleV : 0;
        const sint32 yAddCD = enhancements.deinterlace && isRegularRect && (ya < yc) ? doubleV : 0;

        const CoordS32 coordA{xa, (ya << doubleV) + yAddAB};
        const CoordS32 coordB{xb, (yb << doubleV) + yAddAB};
        const CoordS32 coordC{xc, (yc << doubleV) + yAddCD};
        const CoordS32 coordD{xd, (yd << doubleV) + yAddCD};

        Color555 gouraudA;
        Color555 gouraudB;
        Color555 gouraudC;
        Color555 gouraudD;
        if (mode.gouraudEnable) {
            gouraudA.u16 = vdpState.mem1.ReadVRAM<uint16>(gouraudTable + 0u);
            gouraudB.u16 = vdpState.mem1.ReadVRAM<uint16>(gouraudTable + 2u);
            gouraudC.u16 = vdpState.mem1.ReadVRAM<uint16>(gouraudTable + 4u);
            gouraudD.u16 = vdpState.mem1.ReadVRAM<uint16>(gouraudTable + 6u);
        }

        const VDP1CommandData cmdData{
            .mode = mode,
            .color = color,
        };
        const uint16 cmdIndex = VDP1AddCommand(cmdData, false);

        VDP1SpanData spanData{
            .cmdIndex = cmdIndex,
            .mode = mode,
        };

        if (mode.gouraudEnable) {
            spanData.gouraud0 = gouraudA;
            spanData.gouraud1 = gouraudB;
        }
        VDP1AddSpan(coordA, coordB, spanData, false, false);
        if (mode.gouraudEnable) {
            spanData.gouraud0 = gouraudB;
            spanData.gouraud1 = gouraudC;
        }
        VDP1AddSpan(coordB, coordC, spanData, false, false);
        if (mode.gouraudEnable) {
            spanData.gouraud0 = gouraudC;
            spanData.gouraud1 = gouraudD;
        }
        VDP1AddSpan(coordC, coordD, spanData, false, false);
        if (mode.gouraudEnable) {
            spanData.gouraud0 = gouraudD;
            spanData.gouraud1 = gouraudA;
        }
        VDP1AddSpan(coordD, coordA, spanData, false, false);
    }

    void VDP1Cmd_DrawLine(uint32 cmdAddress) {
        if (!vdpState.state2.layerEnabled[0]) {
            return;
        }

        const VDP1State &state = vdpState.state1;
        const VDP1Command::DrawMode mode{.u16 = vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x04)};

        const uint16 color = vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x06);
        const sint32 xa = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x0C)) + state.localCoordX;
        const sint32 ya = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x0E)) + state.localCoordY;
        const sint32 xb = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x10)) + state.localCoordX;
        const sint32 yb = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x12)) + state.localCoordY;

        const CoordS32 coordA{xa, ya};
        const CoordS32 coordB{xb, yb};

        const VDP1CommandData cmdData{
            .mode = mode,
            .color = color,
        };
        const uint16 cmdIndex = VDP1AddCommand(cmdData, false);

        VDP1SpanData spanData{
            .cmdIndex = cmdIndex,
            .mode = mode,
        };

        if (mode.gouraudEnable) {
            const uint32 gouraudTable = static_cast<uint32>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x1C)) << 3u;
            spanData.gouraud0.u16 = vdpState.mem1.ReadVRAM<uint16>(gouraudTable + 0u);
            spanData.gouraud1.u16 = vdpState.mem1.ReadVRAM<uint16>(gouraudTable + 2u);
        }

        VDP1AddSpan(coordA, coordB, spanData, false, false);
    }

    void VDP1Cmd_SetUserClipping(uint32 cmdAddress) {
        VDP1State &state = vdpState.state1;
        const sint32 doubleV = vdp1.doubleV ? 1 : 0;
        state.userClipX0 = bit::extract<0, 9>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x0C));
        state.userClipX1 = bit::extract<0, 9>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x14));
        state.userClipY0 = bit::extract<0, 8>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x0E));
        state.userClipY1 = bit::extract<0, 8>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x16));
    }

    void VDP1Cmd_SetSystemClipping(uint32 cmdAddress) {
        VDP1State &state = vdpState.state1;
        const sint32 doubleV = vdp1.doubleV ? 1 : 0;
        state.sysClipH = bit::extract<0, 9>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x14));
        state.sysClipV = bit::extract<0, 8>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x16));
    }

    void VDP1Cmd_SetLocalCoordinates(uint32 cmdAddress) {
        VDP1State &state = vdpState.state1;
        state.localCoordX = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x0C));
        state.localCoordY = bit::sign_extend<13>(vdpState.mem1.ReadVRAM<uint16>(cmdAddress + 0x0E));
    }

    // -----------------------------------------------------------------------------------------------------------------
    // VDP2 rendering

    uint32 HRes = kDefaultResH;
    uint32 VRes = kDefaultResV;
    bool exclusiveMonitor = false;

    void VDP2CacheCRAMColor(uint32 address) {
        CRAMColorCache &colorCache = vdp2.cpuCRAMColorCache;
        switch (vdpState.regs2.vramControl.colorRAMMode) {
        case 0: { // RGB 5:5:5, half CRAM
            const auto value = vdpState.mem2.ReadCRAM<uint16>(address & ~1u);
            const Color555 color5{.u16 = value};
            const Color888 color8 = ConvertRGB555to888(color5);
            ColorR8G8B8A8 &color = colorCache[address >> 1u];
            color.r = color8.r;
            color.g = color8.g;
            color.b = color8.b;
            color.a = color8.msb;
            break;
        }
        case 1: { // RGB 5:5:5, full CRAM
            const auto value = vdpState.mem2.ReadCRAM<uint16>(address & ~1u);
            const Color555 color5{.u16 = value};
            const Color888 color8 = ConvertRGB555to888(color5);
            ColorR8G8B8A8 &color = colorCache[address >> 1u];
            color.r = color8.r;
            color.g = color8.g;
            color.b = color8.b;
            color.a = color8.msb;
            break;
        }
        case 2: [[fallthrough]]; // RGB 8:8:8, full CRAM
        case 3: [[fallthrough]]; // RGB 8:8:8, full CRAM
        default: {
            address = vdpState.mem2.UnmapCRAMAddress<uint8>(address);
            const auto value = vdpState.mem2.ReadCRAM<uint32>(address & ~3u);
            const Color888 color8{.u32 = value};
            ColorR8G8B8A8 &color = colorCache[address >> 2u];
            color.r = color8.r;
            color.g = color8.g;
            color.b = color8.b;
            color.a = color8.msb;
            break;
        }
        }
    }

    void VDP2CacheAllCRAMColors() {
        CRAMColorCache &colorCache = vdp2.cpuCRAMColorCache;
        switch (vdpState.regs2.vramControl.colorRAMMode) {
        case 0: // RGB 5:5:5, half CRAM
            for (uint32 i = 0; i < 1024; ++i) {
                const auto value = vdpState.mem2.ReadCRAM<uint16>(i * sizeof(uint16));
                const Color555 color5{.u16 = value};
                const Color888 color8 = ConvertRGB555to888(color5);
                colorCache[i].r = color8.r;
                colorCache[i].g = color8.g;
                colorCache[i].b = color8.b;
                colorCache[i].a = color8.msb;
            }
            break;
        case 1: // RGB 5:5:5, full CRAM
            for (uint32 i = 0; i < 2048; ++i) {
                const auto value = vdpState.mem2.ReadCRAM<uint16>(i * sizeof(uint16));
                const Color555 color5{.u16 = value};
                const Color888 color8 = ConvertRGB555to888(color5);
                colorCache[i].r = color8.r;
                colorCache[i].g = color8.g;
                colorCache[i].b = color8.b;
                colorCache[i].a = color8.msb;
            }
            break;
        case 2: [[fallthrough]]; // RGB 8:8:8, full CRAM
        case 3: [[fallthrough]]; // RGB 8:8:8, full CRAM
        default:
            for (uint32 i = 0; i < 1024; ++i) {
                const auto value = vdpState.mem2.ReadCRAM<uint32>(i * sizeof(uint32));
                const Color888 color8{.u32 = value};
                colorCache[i].r = color8.r;
                colorCache[i].g = color8.g;
                colorCache[i].b = color8.b;
                colorCache[i].a = color8.msb;
            }
            break;
        }
    }

    void VDP2WriteVRAM(uint32 address) {
        vdp2.vramDirty.Set(address >> VDP2Resources::kVRAMDirtyBitmapChunkSizeShift);
    }

    void VDP2WriteCRAM(uint32 address) {
        ++vdp2.cramGeneration;
        VDP2CacheCRAMColor(address);
    }

    void VDP2WriteReg(uint32 address, uint16 value) {
        struct DirtyFlags {
            bool render = false;
            bool compose = false;
            bool enabledLayers = false;
            bool cram = false;
        };
        static constexpr auto kDirtyFlags = [] {
            std::array<DirtyFlags, 0x11E / sizeof(uint16) + 1> arr{};

            for (uint32 addr : {
                     0x000 /*TVMD*/,   0x002 /*EXTEN*/,  0x006 /*VRSIZE*/, 0x00E /*RAMCTL*/, 0x010 /*CYCA0L*/,
                     0x012 /*CYCA0U*/, 0x014 /*CYCA1L*/, 0x016 /*CYCA1U*/, 0x018 /*CYCB0L*/, 0x01A /*CYCB0U*/,
                     0x01C /*CYCB1L*/, 0x01E /*CYCB1U*/, 0x020 /*BGON*/,   0x022 /*MZCTL*/,  0x024 /*SFSEL*/,
                     0x026 /*SFCODE*/, 0x028 /*CHCTLA*/, 0x02A /*CHCTLB*/, 0x02C /*BMPNA*/,  0x02E /*BMPNB*/,
                     0x030 /*PNCNA*/,  0x032 /*PNCNB*/,  0x034 /*PNCNC*/,  0x036 /*PNCND*/,  0x038 /*PNCR*/,
                     0x03A /*PLSZ*/,   0x03C /*MPOFN*/,  0x03E /*MPOFR*/,  0x040 /*MPABN0*/, 0x042 /*MPCDN0*/,
                     0x044 /*MPABN1*/, 0x046 /*MPCDN1*/, 0x048 /*MPABN2*/, 0x04A /*MPCDN2*/, 0x04C /*MPABN3*/,
                     0x04E /*MPCDN3*/, 0x050 /*MPABRA*/, 0x052 /*MPCDRA*/, 0x054 /*MPEFRA*/, 0x056 /*MPGHRA*/,
                     0x058 /*MPIJRA*/, 0x05A /*MPKLRA*/, 0x05C /*MPMNRA*/, 0x05E /*MPOPRA*/, 0x060 /*MPABRB*/,
                     0x062 /*MPCDRB*/, 0x064 /*MPEFRB*/, 0x066 /*MPGHRB*/, 0x068 /*MPIJRB*/, 0x06A /*MPKLRB*/,
                     0x06C /*MPMNRB*/, 0x06E /*MPOPRB*/, 0x070 /*SCXIN0*/, 0x072 /*SCXDN0*/, 0x074 /*SCYIN0*/,
                     0x076 /*SCYDN0*/, 0x078 /*ZMXIN0*/, 0x07A /*ZMXDN0*/, 0x07C /*ZMYIN0*/, 0x07E /*ZMYDN0*/,
                     0x080 /*SCXIN1*/, 0x082 /*SCXDN1*/, 0x084 /*SCYIN1*/, 0x086 /*SCYDN1*/, 0x088 /*ZMXIN1*/,
                     0x08A /*ZMXDN1*/, 0x08C /*ZMYIN1*/, 0x08E /*ZMYDN1*/, 0x090 /*SCXN2*/,  0x092 /*SCYN2*/,
                     0x094 /*SCXN3*/,  0x096 /*SCYN3*/,  0x098 /*ZMCTL*/,  0x09A /*SCRCTL*/, 0x09C /*VCSTAU*/,
                     0x09E /*VCSTAL*/, 0x0A0 /*LSTA0U*/, 0x0A2 /*LSTA0L*/, 0x0A4 /*LSTA1U*/, 0x0A6 /*LSTA1L*/,
                     0x0A8 /*LCTAU*/,  0x0AA /*LCTAL*/,  0x0AC /*BKTAU*/,  0x0AE /*BKTAL*/,  0x0B0 /*RPMD*/,
                     0x0B2 /*RPRCTL*/, 0x0B4 /*KTCTL*/,  0x0B6 /*KTAOF*/,  0x0B8 /*OVPNRA*/, 0x0BA /*OVPNRB*/,
                     0x0BC /*RPTAU*/,  0x0BE /*RPTAL*/,  0x0C0 /*WPSX0*/,  0x0C2 /*WPSY0*/,  0x0C4 /*WPEX0*/,
                     0x0C6 /*WPEY0*/,  0x0C8 /*WPSX1*/,  0x0CA /*WPSY1*/,  0x0CC /*WPEX1*/,  0x0CE /*WPEY1*/,
                     0x0D0 /*WCTLA*/,  0x0D2 /*WCTLB*/,  0x0D4 /*WCTLC*/,  0x0D6 /*WCTLD*/,  0x0D8 /*LWTA0U*/,
                     0x0DA /*LWTA0L*/, 0x0DC /*LWTA1U*/, 0x0DE /*LWTA1L*/, 0x0E0 /*SPCTL*/,  0x0E2 /*SDCTL*/,
                     0x0E4 /*CRAOFA*/, 0x0E6 /*CRAOFB*/, 0x0E8 /*LNCLEN*/, 0x0EA /*SFPRMD*/, 0x0EC /*CCCTL*/,
                     0x0EE /*SFCCMD*/, 0x0F0 /*PRISA*/,  0x0F2 /*PRISB*/,  0x0F4 /*PRISC*/,  0x0F6 /*PRISD*/,
                     0x0F8 /*PRINA*/,  0x0FA /*PRINB*/,  0x0FC /*PRIR*/,
                 }) {
                arr[addr / sizeof(uint16)].render = true;
            }

            for (uint32 addr : {
                     0x000 /*TVMD*/,   0x006 /*VRSIZE*/, 0x020 /*BGON*/,  0x0E0 /*SPCTL*/, 0x0E2 /*SDCTL*/,
                     0x0E8 /*LNCLEN*/, 0x0EC /*CCCTL*/,  0x100 /*CCRSA*/, 0x102 /*CCRSB*/, 0x104 /*CCRSC*/,
                     0x106 /*CCRSD*/,  0x108 /*CCRNA*/,  0x10A /*CCRNB*/, 0x10C /*CCRR*/,  0x10E /*CCRLB*/,
                     0x110 /*CLOFEN*/, 0x112 /*CLOFSL*/, 0x114 /*COAR*/,  0x116 /*COAG*/,  0x118 /*COAB*/,
                     0x11A /*COBR*/,   0x11C /*COBG*/,   0x11E /*COBB*/,
                 }) {
                arr[addr / sizeof(uint16)].compose = true;
            }

            for (uint32 addr : {0x020 /*BGON*/, 0x028 /*CHCTLA*/, 0x02A /*CHCTLB*/}) {
                arr[addr / sizeof(uint16)].enabledLayers = true;
            }

            arr[0x00E / sizeof(uint16) /*RAMCTL*/].cram = true;

            return arr;
        }();

        if (address <= 0x11E) {
            const auto &dirtyFlags = kDirtyFlags[address / sizeof(uint16)];
            if (dirtyFlags.render) {
                ++vdp2.layerRenderParamsGeneration;
            }
            if (dirtyFlags.compose) {
                ++vdp2.composeParamsGeneration;
            }

            if (dirtyFlags.enabledLayers) {
                VDP2UpdateEnabledLayers();
            }

            if (dirtyFlags.cram) {
                ++vdp2.cramGeneration;
                VDP2CacheAllCRAMColors();
            }
        }
    }

    [[nodiscard]] util::VoidResult<> VDP2FlushVRAM() {
        if (!vdp2.vramDirty) {
            return {};
        }

        ID3D12Resource *dstResource = vdp2.vramBuffer.GetPointer();
        ID3D12Resource *uploadBufferPtr = uploadBuffer.GetBufferResource().GetPointer();

        // Emit barrier transition
        barrierTracker.TransitionBuffer(dstResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_BARRIER_SYNC_COPY,
                                        D3D12_BARRIER_ACCESS_COPY_DEST);
        barrierTracker.Flush(cmdList);

        // Upload all modified VRAM chunks
        size_t pos, count = 0;
        UploadAllocation alloc{};
        for (pos = vdp2.vramDirty.FindNext(count); pos < vdp2.vramDirty.Size();
             pos = vdp2.vramDirty.FindNext(count, pos + count)) {
            const uint32 vramOffset = pos << VDP2Resources::kVRAMDirtyBitmapChunkSizeShift;
            const uint32 size = count << VDP2Resources::kVRAMDirtyBitmapChunkSizeShift;

            // Get upload buffer chunk for this transfer
            if (auto result = AllocateUploadBuffer(uploadBuffer, size, 4, alloc); !result) {
                return util::ErrorMessage{
                    fmt::format("Failed to allocate upload buffer for VDP2 VRAM chunk: {}", result.Error().message)};
            }

            // Upload VRAM chunk
            memcpy(alloc.data, &vdpState.mem2.VRAM[vramOffset], size);
            cmdList->CopyBufferRegion(dstResource, vramOffset, uploadBufferPtr, alloc.offset, size);
        }
        vdp2.vramDirty.ClearAll();

        return {};
    }

    [[nodiscard]] util::VoidResult<> VDP2FlushCRAM() {
        FrameContext &frameCtx = frames.GetCurrentFrame();
        if (frameCtx.cramGeneration == vdp2.cramGeneration) {
            return {};
        }
        frameCtx.cramGeneration = vdp2.cramGeneration;

        ID3D12Resource *uploadBufferPtr = uploadBuffer.GetBufferResource().GetPointer();

        UploadAllocation alloc{};

        // Update color cache
        {
            const size_t size = sizeof(CRAMColorCache);
            if (auto result = AllocateUploadBuffer(uploadBuffer, size, 4, alloc); !result) {
                return util::ErrorMessage{fmt::format("Failed to allocate upload buffer for VDP2 CRAM color cache: {}",
                                                      result.Error().message)};
            }
            memcpy(alloc.data, vdp2.cpuCRAMColorCache.data(), size);

            ID3D12Resource *dstResource = frameCtx.cramColorBuffer.GetPointer();

            // Emit barrier transition
            barrierTracker.TransitionBuffer(dstResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_BARRIER_SYNC_COPY,
                                            D3D12_BARRIER_ACCESS_COPY_DEST);
            barrierTracker.Flush(cmdList);

            cmdList->CopyBufferRegion(dstResource, 0, uploadBufferPtr, alloc.offset, size);
        }

        // Update rotation coefficients view
        const VDP2Regs &regs2 = vdpState.regs2;
        if ((regs2.bgEnabled[4] || regs2.bgEnabled[5]) && regs2.vramControl.colorRAMCoeffTableEnable) {
            const size_t size = kVDP2CRAMRotCoeffBufferSize;
            if (auto result = AllocateUploadBuffer(uploadBuffer, size, 4, alloc); !result) {
                return util::ErrorMessage{
                    fmt::format("Failed to allocate upload buffer for VDP2 CRAM rotation coefficients: {}",
                                result.Error().message)};
            }
            memcpy(alloc.data, &vdpState.mem2.CRAM[kVDP2CRAMSize / 2], size);

            ID3D12Resource *dstResource = frameCtx.cramRotCoeffBuffer.GetPointer();

            // Emit barrier transition
            barrierTracker.TransitionBuffer(dstResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_BARRIER_SYNC_COPY,
                                            D3D12_BARRIER_ACCESS_COPY_DEST);
            barrierTracker.Flush(cmdList);

            cmdList->CopyBufferRegion(dstResource, 0, uploadBufferPtr, alloc.offset, size);
        }

        return {};
    }

    void VDP2UpdateCommonRenderParams() {
        // vdp2.cpuCommonRenderParams.startY is updated by the line rendering functions.

        const VDP1Regs &regs1 = vdpState.regs1;
        const VDP2Regs &regs2 = vdpState.regs2;
        VDP2CommonRenderParams &params = vdp2.cpuCommonRenderParams;

        params.displayParams.displayEnable = regs2.TVMD.DISP;
        params.displayParams.borderColorMode = regs2.TVMD.BDCLMD;
        params.displayParams.interlaceMode = static_cast<HLSLuint>(regs2.TVMD.LSMDn);
        params.displayParams.oddField = regs2.TVSTAT.ODD;
        params.displayParams.exclusiveMonitor = exclusiveMonitor;
        params.displayParams.colorRAMMode = regs2.vramControl.colorRAMMode;
        params.displayParams.hiResH = bit::test<1>(regs2.TVMD.HRESOn);
        params.displayParams.palMode = regs2.TVSTAT.PAL;
        params.displayParams.hresMode = regs2.TVMD.HRESOn;
        params.displayParams.vresMode = regs2.TVMD.VRESOn;
        params.displayParams.dblInterlaceEnable = regs1.dblInterlaceEnable;
        params.displayParams.dblInterlaceDrawLine = regs1.dblInterlaceDrawLine;

        params.layerParams.layerEnabled = PackBools<HLSLuint>(vdpState.state2.layerEnabled);
        params.layerParams.bgEnabled = PackBools<HLSLuint>(regs2.bgEnabled);
        params.layerParams.lineColorEnableRBG0 = regs2.bgParams[0].lineColorScreenEnable;
        params.layerParams.lineColorEnableRBG1 = regs2.bgParams[1].lineColorScreenEnable;
        params.layerParams.mosaicH = regs2.mosaicH - 1;
        params.layerParams.mosaicV = regs2.mosaicV - 1;
        params.layerParams.rotParamMode = static_cast<HLSLuint>(regs2.commonRotParams.rotParamMode);
        params.layerParams.restrictedColorCalc = regs2.restrictedColorCalc;
        params.layerParams.extendedColorCalc = regs2.colorCalcParams.extendedColorCalcEnable && regs2.TVMD.HRESOn < 2;
        params.layerParams.useAdditiveBlend = regs2.colorCalcParams.useAdditiveBlend;
        params.layerParams.useSecondScreenRatio = regs2.colorCalcParams.useSecondScreenRatio;
        params.layerParams.colorGradEnable = regs2.colorCalcParams.colorGradEnable;
        params.layerParams.colorGradScreen = static_cast<HLSLuint>(regs2.colorCalcParams.colorGradScreen);

        auto isCoeff = [](RotDataBankSel sel) { return sel == RotDataBankSel::Coefficients; };

        const VRAMControl &vramCtl = regs2.vramControl;
        params.rotParams.coeffTableCRAM = vramCtl.colorRAMCoeffTableEnable;
        params.rotParams.coeffDataAccess = 0;
        if (isCoeff(vramCtl.rotDataBankSelA0)) {
            params.rotParams.coeffDataAccess |= 1u << 0u;
        }
        if (isCoeff(vramCtl.partitionVRAMA ? vramCtl.rotDataBankSelA1 : vramCtl.rotDataBankSelA0)) {
            params.rotParams.coeffDataAccess |= 1u << 1u;
        }
        if (isCoeff(vramCtl.rotDataBankSelB0)) {
            params.rotParams.coeffDataAccess |= 1u << 2u;
        }
        if (isCoeff(vramCtl.partitionVRAMB ? vramCtl.rotDataBankSelB1 : vramCtl.rotDataBankSelB0)) {
            params.rotParams.coeffDataAccess |= 1u << 3u;
        }
        params.rotParams.coeffDataPerDot = vramCtl.perDotRotationCoeffs;

        const RotationParams &rotParamsA = regs2.rotParams[0];
        params.rotParams.coeffATableEnable = rotParamsA.coeffTableEnable;
        params.rotParams.coeffAUseLineColorData = rotParamsA.coeffUseLineColorData;
        params.rotParams.coeffADataSize = rotParamsA.coeffDataSize;
        params.rotParams.coeffADataMode = static_cast<HLSLuint>(rotParamsA.coeffDataMode);

        const RotationParams &rotParamsB = regs2.rotParams[1];
        params.rotParams.coeffBTableEnable = rotParamsB.coeffTableEnable;
        params.rotParams.coeffBUseLineColorData = rotParamsB.coeffUseLineColorData;
        params.rotParams.coeffBDataSize = rotParamsB.coeffDataSize;
        params.rotParams.coeffBDataMode = static_cast<HLSLuint>(rotParamsB.coeffDataMode);

        params.spriteParams.rotate = regs1.fbRotEnable;
        params.spriteParams.pixel8Bits = regs1.pixel8Bits;
        params.spriteParams.type = regs2.spriteParams.type;
        params.spriteParams.fbSizeH = std::countr_zero(regs1.fbSizeH) - 9;
        params.spriteParams.fbSizeV = std::countr_zero(regs1.fbSizeV) - 8;
        params.spriteParams.inHalfResH = false;
        params.spriteParams.outHalfResH = false;
        if (!regs1.hdtvEnable && !regs1.fbRotEnable) {
            if (regs1.pixel8Bits) {
                params.spriteParams.inHalfResH = (regs2.TVMD.HRESOn & 0b110) == 0b000;
            } else {
                params.spriteParams.outHalfResH = (regs2.TVMD.HRESOn & 0b110) == 0b010;
            }
        }
        params.spriteParams.mixedFormat = regs2.spriteParams.mixedFormat;
        params.spriteParams.colorCalcEnable = regs2.spriteParams.colorCalcEnable;
        params.spriteParams.colorCalcValue = regs2.spriteParams.colorCalcValue;
        params.spriteParams.colorCalcCond = static_cast<HLSLuint>(regs2.spriteParams.colorCalcCond);
        params.spriteParams.colorDataOffset = regs2.spriteParams.colorDataOffset >> 8u;
        params.spriteParams.useSpriteWindow = regs2.spriteParams.useSpriteWindow;
        params.spriteParams.windowEnabled = regs2.spriteParams.spriteWindowEnabled;
        params.spriteParams.windowInverted = regs2.spriteParams.spriteWindowInverted;
        params.spriteParams.displayFB = vdpState.fbIndex.display;

        params.spritePriosRatios.x = 0;
        params.spritePriosRatios.y = 0;
        for (uint32 i = 0; i < 4; i++) {
            params.spritePriosRatios.x |= regs2.spriteParams.priorities[i] << (8 * i);
            params.spritePriosRatios.x |= regs2.spriteParams.colorCalcRatios[i] << (8 * i + 3);

            params.spritePriosRatios.y |= regs2.spriteParams.priorities[i + 4] << (8 * i);
            params.spritePriosRatios.y |= regs2.spriteParams.colorCalcRatios[i + 4] << (8 * i + 3);
        }

        params.vcellScroll.tableAddress = regs2.vcellScrollTableAddress;
        params.vcellScroll.inc = regs2.vcellScrollInc >> 2u;

        params.windows.spriteWindowLogic = regs2.spriteParams.windowSet.logic == WindowLogic::And;
        params.windows.spriteW0Enable = regs2.spriteParams.windowSet.enabled[0];
        params.windows.spriteW0Invert = regs2.spriteParams.windowSet.inverted[0];
        params.windows.spriteW1Enable = regs2.spriteParams.windowSet.enabled[1];
        params.windows.spriteW1Invert = regs2.spriteParams.windowSet.inverted[1];

        params.windows.colorCalcWindowLogic = regs2.colorCalcParams.windowSet.logic == WindowLogic::And;
        params.windows.colorCalcW0Enable = regs2.colorCalcParams.windowSet.enabled[0];
        params.windows.colorCalcW0Invert = regs2.colorCalcParams.windowSet.inverted[0];
        params.windows.colorCalcW1Enable = regs2.colorCalcParams.windowSet.enabled[1];
        params.windows.colorCalcW1Invert = regs2.colorCalcParams.windowSet.inverted[1];
        params.windows.colorCalcSWEnable = regs2.colorCalcParams.windowSet.enabled[2];
        params.windows.colorCalcSWInvert = regs2.colorCalcParams.windowSet.inverted[2];

        // NOTE: this is uploaded as 32-bit root constants, not through the upload buffer.
        // No uploads or barriers are needed here.
    }

    util::VoidResult<> VDP2UpdateLayerRenderParams() {
        FrameContext &frameCtx = frames.GetCurrentFrame();
        if (frameCtx.layerRenderParamsGeneration == vdp2.layerRenderParamsGeneration) {
            return {};
        }
        frameCtx.layerRenderParamsGeneration = vdp2.layerRenderParamsGeneration;

        const VDP2Regs &regs2 = vdpState.regs2;

        // These can be either 0 or 8, but we'll condense them to single bits
        auto packVRAMDataOffsets = [](const std::array<uint32, 4> values) {
            uint32 value = 0;
            for (size_t i = 0; i < values.size(); ++i) {
                if (values[i] != 0u) {
                    value |= 1u << i;
                }
            }
            return value;
        };

        // NBG0-3
        for (int i = 0; i < 4; ++i) {
            const BGParams &bgParams = regs2.bgParams[i + 1];
            const NBGLayerState &bgState = vdpState.state2.nbgLayerStates[i];

            const bool bitmap = bgParams.bitmap;

            NBGParams &renderParams = vdp2.cpuLayerRenderParams.nbg[i];
            renderParams.base.enabled = regs2.bgEnabled[i];
            renderParams.base.enableTransparency = bgParams.enableTransparency;
            renderParams.base.bitmap = bgParams.bitmap;
            renderParams.base.priorityNumber = bgParams.priorityNumber;
            renderParams.base.priorityMode = static_cast<HLSLuint>(bgParams.priorityMode);
            renderParams.base.specialFunctionSelect = bgParams.specialFunctionSelect;
            renderParams.base.cellSizeShift = bgParams.cellSizeShift;
            renderParams.base.colorFormat = static_cast<HLSLuint>(bgParams.colorFormat);
            renderParams.base.cramOffset = bgParams.cramOffset;
            renderParams.base.supplScrollCharNum = bgParams.supplScrollCharNum;
            renderParams.base.supplPalNum = bitmap ? bgParams.supplBitmapPalNum : bgParams.supplScrollPalNum;
            renderParams.base.supplSpecialColorCalc =
                bitmap ? bgParams.supplBitmapSpecialColorCalc : bgParams.supplScrollSpecialColorCalc;
            renderParams.base.supplSpecialPriority =
                bitmap ? bgParams.supplBitmapSpecialPriority : bgParams.supplScrollSpecialPriority;
            renderParams.base.mosaicEnable = bgParams.mosaicEnable;
            renderParams.base.colorCalcEnable = bgParams.colorCalcEnable;
            renderParams.base.extChar = bgParams.extChar;
            renderParams.base.twoWordChar = bgParams.twoWordChar;
            renderParams.base.patNameAccess = PackBools<HLSLuint>(bgParams.patNameAccess);
            renderParams.base.charPatAccess = PackBools<HLSLuint>(bgParams.charPatAccess);
            renderParams.base.charPatDelay = PackBools<HLSLuint>(bgParams.charPatDelay);
            renderParams.base.vramDataOffset = packVRAMDataOffsets(bgParams.vramDataOffset);
            renderParams.base.specialColorCalcMode = static_cast<HLSLuint>(bgParams.specialColorCalcMode);
            renderParams.base.pageShift = {bgParams.pageShiftH, bgParams.pageShiftV};
            renderParams.base.bitmapSize = {bgParams.bitmapSizeH, bgParams.bitmapSizeV};
            renderParams.base.bitmapBaseAddress = bgParams.bitmapBaseAddress;
            renderParams.base.windowParams.base.windowLogicAnd = bgParams.windowSet.logic == WindowLogic::And;
            renderParams.base.windowParams.base.window0Enable = bgParams.windowSet.enabled[0];
            renderParams.base.windowParams.base.window0Invert = bgParams.windowSet.inverted[0];
            renderParams.base.windowParams.base.window1Enable = bgParams.windowSet.enabled[1];
            renderParams.base.windowParams.base.window1Invert = bgParams.windowSet.inverted[1];
            renderParams.base.windowParams.spriteWindowEnable = bgParams.windowSet.enabled[2];
            renderParams.base.windowParams.spriteWindowInvert = bgParams.windowSet.inverted[2];

            renderParams.scrollAmount = {bgParams.scrollAmountH, bgParams.scrollAmountV};
            renderParams.scrollInc = {bgParams.scrollIncH, bgParams.scrollIncV};
            renderParams.pageBaseAddresses = bgParams.pageBaseAddresses;
            renderParams.vcellScrollEnable = bgParams.vcellScrollEnable;
            renderParams.lineScrollXEnable = bgParams.lineScrollXEnable;
            renderParams.lineScrollYEnable = bgParams.lineScrollYEnable;
            renderParams.lineZoomEnable = bgParams.lineZoomEnable;
            renderParams.lineScrollInterval = bgParams.lineScrollInterval;
            renderParams.lineScrollTableAddress = bgState.lineScrollTableAddress;
            renderParams.vcellScrollOffset = bgState.vcellScrollOffset;
            renderParams.vcellScrollDelay = bgState.vcellScrollDelay;
            renderParams.vcellScrollRepeat = bgState.vcellScrollRepeat;
        }

        // RBG0-1 / RotParam A-B
        for (int i = 0; i < 2; ++i) {
            const BGParams &bgParams = regs2.bgParams[i];
            const RotationParams &rotParams = regs2.rotParams[i];

            const bool bitmap = bgParams.bitmap;

            RBGParams &renderParams = vdp2.cpuLayerRenderParams.rbg[i];
            renderParams.base.enabled = regs2.bgEnabled[i + 4];
            renderParams.base.enableTransparency = bgParams.enableTransparency;
            renderParams.base.bitmap = bgParams.bitmap;
            renderParams.base.priorityNumber = bgParams.priorityNumber;
            renderParams.base.priorityMode = static_cast<HLSLuint>(bgParams.priorityMode);
            renderParams.base.specialFunctionSelect = bgParams.specialFunctionSelect;
            renderParams.base.cellSizeShift = bgParams.cellSizeShift;
            renderParams.base.colorFormat = static_cast<HLSLuint>(bgParams.colorFormat);
            renderParams.base.cramOffset = bgParams.cramOffset;
            renderParams.base.supplScrollCharNum = bgParams.supplScrollCharNum;
            renderParams.base.supplPalNum = bitmap ? bgParams.supplBitmapPalNum : bgParams.supplScrollPalNum;
            renderParams.base.supplSpecialColorCalc =
                bitmap ? bgParams.supplBitmapSpecialColorCalc : bgParams.supplScrollSpecialColorCalc;
            renderParams.base.supplSpecialPriority =
                bitmap ? bgParams.supplBitmapSpecialPriority : bgParams.supplScrollSpecialPriority;
            renderParams.base.mosaicEnable = bgParams.mosaicEnable;
            renderParams.base.colorCalcEnable = bgParams.colorCalcEnable;
            renderParams.base.extChar = bgParams.extChar;
            renderParams.base.twoWordChar = bgParams.twoWordChar;
            renderParams.base.patNameAccess = PackBools<HLSLuint>(bgParams.patNameAccess);
            renderParams.base.charPatAccess = PackBools<HLSLuint>(bgParams.charPatAccess);
            renderParams.base.charPatDelay = PackBools<HLSLuint>(bgParams.charPatDelay);
            renderParams.base.vramDataOffset = packVRAMDataOffsets(bgParams.vramDataOffset);
            renderParams.base.specialColorCalcMode = static_cast<HLSLuint>(bgParams.specialColorCalcMode);
            renderParams.base.pageShift = {rotParams.pageShiftH, rotParams.pageShiftV};
            renderParams.base.bitmapSize = {bgParams.bitmapSizeH, bgParams.bitmapSizeV};
            renderParams.base.bitmapBaseAddress = rotParams.bitmapBaseAddress;
            renderParams.base.windowParams.base.windowLogicAnd = bgParams.windowSet.logic == WindowLogic::And;
            renderParams.base.windowParams.base.window0Enable = bgParams.windowSet.enabled[0];
            renderParams.base.windowParams.base.window0Invert = bgParams.windowSet.inverted[0];
            renderParams.base.windowParams.base.window1Enable = bgParams.windowSet.enabled[1];
            renderParams.base.windowParams.base.window1Invert = bgParams.windowSet.inverted[1];
            renderParams.base.windowParams.spriteWindowEnable = bgParams.windowSet.enabled[2];
            renderParams.base.windowParams.spriteWindowInvert = bgParams.windowSet.inverted[2];

            renderParams.screenOverProcess = static_cast<HLSLuint>(rotParams.screenOverProcess);
            renderParams.screenOverPatternName = rotParams.screenOverPatternName;
            renderParams.pageBaseAddresses[0] = vdpState.state2.rbgPageBaseAddresses[0][i];
            renderParams.pageBaseAddresses[1] = vdpState.state2.rbgPageBaseAddresses[1][i];
        }

        // Windows 0 and 1
        for (int i = 0; i < 2; ++i) {
            const WindowParams &windowParams = regs2.windowParams[i];
            VDP2GlobalWindowParams &renderParams = vdp2.cpuLayerRenderParams.windows[i];
            renderParams.start = {windowParams.startX, windowParams.startY};
            renderParams.end = {windowParams.endX, windowParams.endY};
            renderParams.lineWindowTableAddress = windowParams.lineWindowTableAddress;
            renderParams.lineWindowTableEnable = windowParams.lineWindowTableEnable;
        }

        VDP2LayerWindowParams &rotWindows = vdp2.cpuLayerRenderParams.rotWindows;
        rotWindows.windowLogicAnd = regs2.commonRotParams.windowSet.logic == WindowLogic::And;
        rotWindows.window0Enable = regs2.commonRotParams.windowSet.enabled[0];
        rotWindows.window0Invert = regs2.commonRotParams.windowSet.inverted[0];
        rotWindows.window1Enable = regs2.commonRotParams.windowSet.enabled[1];
        rotWindows.window1Invert = regs2.commonRotParams.windowSet.inverted[1];

        VDP2LineBackScreenParams &lnclParams = vdp2.cpuLayerRenderParams.lineScreenParams;
        lnclParams.baseAddress = regs2.lineScreenParams.baseAddress;
        lnclParams.perLine = regs2.lineScreenParams.perLine;

        VDP2LineBackScreenParams &backParams = vdp2.cpuLayerRenderParams.backScreenParams;
        backParams.baseAddress = regs2.backScreenParams.baseAddress;
        backParams.perLine = regs2.backScreenParams.perLine;

        // Special function codes
        vdp2.cpuLayerRenderParams.specialFunctionCodes =
            PackBools<uint32>(regs2.specialFunctionCodes[0].colorMatches) |
            (PackBools<uint32>(regs2.specialFunctionCodes[1].colorMatches) << 8u);

        // Update buffer
        {
            FrameContext &frameCtx = frames.GetCurrentFrame();

            ID3D12Resource *dstResource = frameCtx.layerRenderParamsBuffer.GetPointer();
            ID3D12Resource *uploadBufferPtr = uploadBuffer.GetBufferResource().GetPointer();
            const size_t size = sizeof(vdp2.cpuLayerRenderParams);
            UploadAllocation alloc{};
            if (auto result = AllocateUploadBuffer(uploadBuffer, size, 4, alloc); !result) {
                return util::ErrorMessage{
                    fmt::format("Failed to allocate upload buffer for VDP2 layer rendering parameters: {}",
                                result.Error().message)};
            }
            memcpy(alloc.data, &vdp2.cpuLayerRenderParams, size);

            // Emit barrier transition
            barrierTracker.TransitionBuffer(dstResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_BARRIER_SYNC_COPY,
                                            D3D12_BARRIER_ACCESS_COPY_DEST);
            barrierTracker.Flush(cmdList);

            cmdList->CopyBufferRegion(dstResource, 0, uploadBufferPtr, alloc.offset, size);
        }

        return {};
    }

    util::VoidResult<> VDP2UpdateComposeParams() {
        FrameContext &frameCtx = frames.GetCurrentFrame();
        if (frameCtx.composeParamsGeneration == vdp2.composeParamsGeneration) {
            return {};
        }
        frameCtx.composeParamsGeneration = vdp2.composeParamsGeneration;

        const VDP2Regs &regs2 = vdpState.regs2;

        auto &params = vdp2.cpuComposeParams;
        params.colorCalcEnable = 0                                               //
                                 | (regs2.spriteParams.colorCalcEnable << 0)     //
                                 | (regs2.bgParams[0].colorCalcEnable << 1)      //
                                 | (regs2.bgParams[1].colorCalcEnable << 2)      //
                                 | (regs2.bgParams[2].colorCalcEnable << 3)      //
                                 | (regs2.bgParams[3].colorCalcEnable << 4)      //
                                 | (regs2.bgParams[4].colorCalcEnable << 5)      //
                                 | (regs2.backScreenParams.colorCalcEnable << 6) //
                                 | (regs2.lineScreenParams.colorCalcEnable << 7) //
            ;
        params.colorOffsetEnable = PackBools<HLSLuint>(regs2.colorOffsetEnable);
        params.colorOffsetSelect = PackBools<HLSLuint>(regs2.colorOffsetSelect);
        params.lineColorEnable = 0                                                 //
                                 | (regs2.spriteParams.lineColorScreenEnable << 0) //
                                 | (regs2.bgParams[0].lineColorScreenEnable << 1)  //
                                 | (regs2.bgParams[1].lineColorScreenEnable << 2)  //
                                 | (regs2.bgParams[2].lineColorScreenEnable << 3)  //
                                 | (regs2.bgParams[3].lineColorScreenEnable << 4)  //
                                 | (regs2.bgParams[4].lineColorScreenEnable << 5)  //
            ;

        params.colorOffsetA.r = bit::sign_extend<9>(regs2.colorOffset[0].r);
        params.colorOffsetA.g = bit::sign_extend<9>(regs2.colorOffset[0].g);
        params.colorOffsetA.b = bit::sign_extend<9>(regs2.colorOffset[0].b);

        params.colorOffsetB.r = bit::sign_extend<9>(regs2.colorOffset[1].r);
        params.colorOffsetB.g = bit::sign_extend<9>(regs2.colorOffset[1].g);
        params.colorOffsetB.b = bit::sign_extend<9>(regs2.colorOffset[1].b);

        for (int i = 0; i < 5; ++i) {
            params.bgColorCalcRatios[i] = regs2.bgParams[i].colorCalcRatio;
        }
        params.backLineColorCalcRatios[0] = regs2.backScreenParams.colorCalcRatio;
        params.backLineColorCalcRatios[1] = regs2.lineScreenParams.colorCalcRatio;

        // Update buffer
        {
            FrameContext &frameCtx = frames.GetCurrentFrame();

            ID3D12Resource *dstResource = frameCtx.composeParamsBuffer.GetPointer();
            ID3D12Resource *uploadBufferPtr = uploadBuffer.GetBufferResource().GetPointer();
            const size_t size = sizeof(vdp2.cpuComposeParams);

            UploadAllocation alloc{};
            if (auto result = AllocateUploadBuffer(uploadBuffer, size, 4, alloc); !result) {
                return util::ErrorMessage{
                    fmt::format("Failed to allocate upload buffer for VDP2 layer compositing parameters: {}",
                                result.Error().message)};
            }
            memcpy(alloc.data, &vdp2.cpuComposeParams, size);

            // Emit barrier transition
            barrierTracker.TransitionBuffer(dstResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_BARRIER_SYNC_COPY,
                                            D3D12_BARRIER_ACCESS_COPY_DEST);
            barrierTracker.Flush(cmdList);

            cmdList->CopyBufferRegion(dstResource, 0, uploadBufferPtr, alloc.offset, size);
        }

        return {};
    }

    void VDP2CalcAccessPatterns() {
        if (vdpState.regs2.accessPatternsDirty) {
            ++vdp2.layerRenderParamsGeneration;
        }
        vdpState.state2.CalcAccessPatterns(vdpState.regs2, vdp2.accessPatternsConfig);
    }

    void VDP2InitNBGs() {
        const VDP2Regs &regs2 = vdpState.regs2;

        for (uint32 i = 0; i < 4; ++i) {
            const BGParams &bgParams = regs2.bgParams[i + 1];
            NBGLayerState &nbgState = vdpState.state2.nbgLayerStates[i];

            // NOTE: fracScrollX/Y are computed from scratch in the shader
            nbgState.scrollIncH = bgParams.scrollIncH;

            if (i < 2) {
                nbgState.lineScrollTableAddress = bgParams.lineScrollTableAddress;
            }
        }

        ++vdp2.layerRenderParamsGeneration;
    }

    void VDP2UpdateEnabledLayers() {
        vdpState.state2.UpdateEnabledBGs(vdpState.regs2, vdp2.debugRenderOptions);
    }

    void VDP2CalcVCellScrollDelay() {
        if (vdpState.regs2.accessPatternsDirty) {
            ++vdp2.layerRenderParamsGeneration;
        }
        vdpState.state2.CalcVCellScrollDelay(vdpState.regs2);
    }

    void VDP2DrawLineColorBackScreens(uint32 y) {
        const VDP2Regs &regs = vdpState.regs2;

        if (regs.displayEnabledLatch || y == 0) {
            // Read line color screen color
            const LineBackScreenParams &lineParams = regs.lineScreenParams;
            const uint32 lnclY = lineParams.perLine ? y : 0;
            const uint32 lineAddress = lineParams.baseAddress + lnclY * sizeof(uint16);
            const uint32 cramAddress = vdpState.mem2.ReadVRAM<uint16>(lineAddress);
            vdp2.cpuLnclBack[0][y] = vdp2.cpuCRAMColorCache[cramAddress & 0x7FF];

            // Read back screen color
            const LineBackScreenParams &backParams = regs.backScreenParams;
            const uint32 backY = backParams.perLine ? y : 0;
            const uint32 backAddress = backParams.baseAddress + backY * sizeof(Color555);
            const Color555 color5{.u16 = vdpState.mem2.ReadVRAM<uint16>(backAddress)};
            const Color888 color8 = ConvertRGB555to888(color5);
            vdp2.cpuLnclBack[1][y].r = color8.r;
            vdp2.cpuLnclBack[1][y].g = color8.g;
            vdp2.cpuLnclBack[1][y].b = color8.b;
            vdp2.cpuLnclBack[1][y].a = color8.msb;
        } else {
            vdp2.cpuLnclBack[0][y] = vdp2.cpuLnclBack[0][y - 1];
            vdp2.cpuLnclBack[1][y] = vdp2.cpuLnclBack[1][y - 1];
        }
    }

    util::VoidResult<> VDP2UploadLineColorBackScreens() {
        FrameContext &frameCtx = frames.GetCurrentFrame();

        ID3D12Resource *dstResource = frameCtx.lnclBackBuffer.GetPointer();
        ID3D12Resource *uploadBufferPtr = uploadBuffer.GetBufferResource().GetPointer();
        const size_t size = sizeof(vdp2.cpuLnclBack);

        UploadAllocation alloc{};
        if (auto result = AllocateUploadBuffer(uploadBuffer, size, 4, alloc); !result) {
            return util::ErrorMessage{
                fmt::format("Failed to allocate upload buffer for VDP2 LNCL/BACK screens: {}", result.Error().message)};
        }
        memcpy(alloc.data, &vdp2.cpuLnclBack, size);

        // Emit barrier transition
        barrierTracker.TransitionBuffer(dstResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_BARRIER_SYNC_COPY,
                                        D3D12_BARRIER_ACCESS_COPY_DEST);
        barrierTracker.Flush(cmdList);

        cmdList->CopyBufferRegion(dstResource, 0, uploadBufferPtr, alloc.offset, size);

        return {};
    }

    void VDP2UpdateRotationParameterBases(uint16 y) {
        VDP2Regs &regs2 = vdpState.regs2;
        if (!regs2.bgEnabled[4] && !regs2.bgEnabled[5]) {
            // Skip if no RBGs are enabled
            return;
        }

        const bool readAll = y == 0;

        const uint32 baseAddress = regs2.commonRotParams.baseAddress & 0xFFF7C; // mask bit 6 (shifted left by 1)
        for (uint32 i = 0; i < 2; ++i) {
            VDP2RotParamBase &base = vdp2.cpuRotParamBases[i * kMaxNormalResV + y];
            RotationParams &src = regs2.rotParams[i];

            const uint32 address = baseAddress + i * 0x80;

            base.tableAddress = address;

            if (readAll || src.readXst) {
                base.Xst = bit::extract_signed<6, 28, sint32>(vdpState.mem2.ReadVRAM<uint32>(address + 0x00));
                src.readXst = false;
            } else {
                const VDP2RotParamBase &prevBase = vdp2.cpuRotParamBases[i * kMaxNormalResV + y - 1];
                base.Xst =
                    prevBase.Xst + bit::extract_signed<6, 18, sint32>(vdpState.mem2.ReadVRAM<uint32>(address + 0x0C));
            }

            if (readAll || src.readYst) {
                base.Yst = bit::extract_signed<6, 28, sint32>(vdpState.mem2.ReadVRAM<uint32>(address + 0x04));
                src.readYst = false;
            } else {
                const VDP2RotParamBase &prevBase = vdp2.cpuRotParamBases[i * kMaxNormalResV + y - 1];
                base.Yst =
                    prevBase.Yst + bit::extract_signed<6, 18, sint32>(vdpState.mem2.ReadVRAM<uint32>(address + 0x10));
            }

            if (readAll || src.readKAst) {
                const uint32 KAst = bit::extract<6, 31>(vdpState.mem2.ReadVRAM<uint32>(address + 0x54));
                base.KA = src.coeffTableAddressOffset + KAst;
                src.readKAst = false;
            } else {
                const VDP2RotParamBase &prevBase = vdp2.cpuRotParamBases[i * kMaxNormalResV + y - 1];
                base.KA = prevBase.KA + bit::extract_signed<6, 25>(vdpState.mem2.ReadVRAM<uint32>(address + 0x58));
            }
        }
    }

    util::VoidResult<> VDP2UploadRotationParameterBases() {
        FrameContext &frameCtx = frames.GetCurrentFrame();

        ID3D12Resource *dstResource = frameCtx.rotParamBasesBuffer.GetPointer();
        ID3D12Resource *uploadBufferPtr = uploadBuffer.GetBufferResource().GetPointer();
        const size_t size = sizeof(vdp2.cpuRotParamBases);

        UploadAllocation alloc{};
        if (auto result = AllocateUploadBuffer(uploadBuffer, size, 4, alloc); !result) {
            return util::ErrorMessage{fmt::format(
                "Failed to allocate upload buffer for VDP2 rotation parameter bases: {}", result.Error().message)};
        }
        memcpy(alloc.data, &vdp2.cpuRotParamBases, size);

        // Emit barrier transition
        barrierTracker.TransitionBuffer(dstResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_BARRIER_SYNC_COPY,
                                        D3D12_BARRIER_ACCESS_COPY_DEST);
        barrierTracker.Flush(cmdList);

        cmdList->CopyBufferRegion(dstResource, 0, uploadBufferPtr, alloc.offset, size);

        return {};
    }

    void VDP2UpdateState() {
        if (auto result = VDP2FlushVRAM(); !result) {
            devlog::warn<grp::dx12_vdp2>("VDP2 VRAM flush failed: {}", result.Error().message);
        }
        if (auto result = VDP2FlushCRAM(); !result) {
            devlog::warn<grp::dx12_vdp2>("VDP2 CRAM flush failed: {}", result.Error().message);
        }
        VDP2UpdateCommonRenderParams();
        VDP2UpdateLayerRenderParams();
        VDP2UpdateComposeParams();
    }

    void VDP2BeginFrame() {
        vdp2.nextLayerRenderLine = 0;
        vdp2.nextComposeLine = 0;

        // Notify frontend of the start of a new frame
        const Dimensions nativeRes = {HRes, VRes};
        hwCallbacks.FrameBegin(nativeRes);

        UpdateFrameParameters();

        VDP2CalcAccessPatterns();
        VDP2InitNBGs();

        VDP2UpdateState();

        // VDP2 consumes VDP1 FBRAM, so it needs to be synced here
        if (auto result = VDP1FlushFBRAM(); !result) {
            devlog::warn<grp::dx12_vdp1>("VDP1 FBRAM flush failed: {}", result.Error().message);
        }

        // If the frontend requested a VDP1 FBRAM sync via the debugger, do so now
        bool expect = true;
        if (vdp1.fbramDebugSyncRequest.compare_exchange_strong(expect, false)) {
            VDP1DownloadFBRAM();
        }
    }

    void VDP2RenderLayerLines(uint32 y) {
        // Bail out if there's nothing to render
        if (y < vdp2.nextLayerRenderLine) {
            return;
        }

        // FIXME: this should not be needed
        VDP1SubmitSpans();

        FrameContext &frameCtx = frames.GetCurrentFrame();

        const bool deinterlace = enhancements.deinterlace && vdpState.regs2.TVMD.IsInterlaced();
        const uint32 yShift = deinterlace ? 1u : 0u;

        const uint32 startY = vdp2.nextLayerRenderLine;

        // Determine how many lines to draw and update next scanline counter
        const uint32 baseNumLines = y - startY + 1;
        const uint32 numLines = baseNumLines << yShift;
        vdp2.nextLayerRenderLine = y + 1;

        vdp2.cpuCommonRenderParams.startY = startY << yShift;

        // ---------------------------------------------------------------------

        // Transition resources for rendering layers
        barrierTracker.TransitionBuffer(frameCtx.layerRenderParamsBuffer.GetPointer(),
                                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                        D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
        barrierTracker.TransitionBuffer(vdp2.vramBuffer.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                        D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
        barrierTracker.TransitionBuffer(frameCtx.cramColorBuffer.GetPointer(),
                                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                        D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);

        // Compute rotation parameters if any RBGs are enabled
        if (vdpState.regs2.bgEnabled[4] || vdpState.regs2.bgEnabled[5]) {
            VDP2UploadRotationParameterBases();
        }

        // ---------------------------------------------------------------------

        // Transition resources for drawing the sprite layer
        if (vdp2.cpuCommonRenderParams.spriteParams.rotate) {
            barrierTracker.TransitionBuffer(frameCtx.rotParamBasesBuffer.GetPointer(),
                                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                            D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
        }
        barrierTracker.TransitionTexture(frameCtx.layerOutTexture.GetPointer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                         D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,
                                         D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS);
        barrierTracker.TransitionTexture(frameCtx.spriteAttrsTexture.GetPointer(),
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_BARRIER_SYNC_COMPUTE_SHADING,
                                         D3D12_BARRIER_ACCESS_UNORDERED_ACCESS, D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS);
        barrierTracker.TransitionBuffer(vdp1.fbramBuffer.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                        D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
        barrierTracker.Flush(cmdList);

        // Draw sprite layer
        cmdList->SetPipelineState(frameCtx.drawSpritePSO.GetPointer());
        cmdList->SetComputeRootSignature(vdp2.drawSpriteRootSig.GetPointer());
        cmdList->SetComputeRoot32BitConstants(0, sizeof(vdp2.cpuCommonRenderParams) / sizeof(uint32),
                                              &vdp2.cpuCommonRenderParams, 0);
        cmdList->SetComputeRootDescriptorTable(1, frameCtx.drawSpriteDescs.GetGPUHandle());
        cmdList->Dispatch((HRes + 31) / 32, numLines, enhancements.transparentMeshes ? 2 : 1);

        // ---------------------------------------------------------------------

        // Transition resources for drawing background layers
        if (vdpState.regs2.bgEnabled[4] || vdpState.regs2.bgEnabled[5]) {
            barrierTracker.TransitionBuffer(frameCtx.cramRotCoeffBuffer.GetPointer(),
                                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                            D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            barrierTracker.TransitionBuffer(frameCtx.rotParamBasesBuffer.GetPointer(),
                                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                            D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
        }
        barrierTracker.TransitionTexture(
            frameCtx.spriteAttrsTexture.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_BARRIER_LAYOUT_COMMON);
        barrierTracker.TransitionTexture(frameCtx.layerOutTexture.GetPointer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                         D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,
                                         D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS);
        barrierTracker.TransitionTexture(frameCtx.rbgLineColorOutTexture.GetPointer(),
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_BARRIER_SYNC_COMPUTE_SHADING,
                                         D3D12_BARRIER_ACCESS_UNORDERED_ACCESS, D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS);
        barrierTracker.TransitionTexture(frameCtx.colorCalcWindowTexture.GetPointer(),
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_BARRIER_SYNC_COMPUTE_SHADING,
                                         D3D12_BARRIER_ACCESS_UNORDERED_ACCESS, D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS);
        barrierTracker.Flush(cmdList);

        // Draw NBGs and RBGs
        cmdList->SetPipelineState(frameCtx.drawBGsPSO.GetPointer());
        cmdList->SetComputeRootSignature(vdp2.drawBGsRootSig.GetPointer());
        cmdList->SetComputeRoot32BitConstants(0, sizeof(vdp2.cpuCommonRenderParams) / sizeof(uint32),
                                              &vdp2.cpuCommonRenderParams, 0);
        cmdList->SetComputeRootDescriptorTable(1, frameCtx.drawBGsDescs.GetGPUHandle());
        cmdList->Dispatch(HRes / 32, numLines, 1);
    }

    void VDP2ComposeLines(uint32 y) {
        // Bail out if there's nothing to render
        if (y < vdp2.nextComposeLine) {
            return;
        }

        FrameContext &frameCtx = frames.GetCurrentFrame();

        vdp2.cpuCommonRenderParams.startY = vdp2.nextComposeLine;
        VDP2UploadLineColorBackScreens();

        // Determine how many lines to draw and update next scanline counter
        const uint32 numLines = y - vdp2.nextComposeLine + 1;
        vdp2.nextComposeLine = y + 1;

        // ---------------------------------------------------------------------

        // Transition resources for compositing layers
        barrierTracker.TransitionBuffer(frameCtx.composeParamsBuffer.GetPointer(),
                                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                        D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
        barrierTracker.TransitionTexture(
            frameCtx.layerOutTexture.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_BARRIER_LAYOUT_COMMON);
        barrierTracker.TransitionTexture(
            frameCtx.rbgLineColorOutTexture.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_BARRIER_LAYOUT_COMMON);
        barrierTracker.TransitionTexture(
            frameCtx.colorCalcWindowTexture.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_BARRIER_LAYOUT_COMMON);
        barrierTracker.TransitionBuffer(frameCtx.lnclBackBuffer.GetPointer(),
                                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                        D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
        barrierTracker.TransitionTexture(vdp2.compositeOutTexture.GetPointer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                         D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,
                                         D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS);
        barrierTracker.Flush(cmdList);

        // Compose final image
        cmdList->SetPipelineState(frameCtx.composePSO.GetPointer());
        cmdList->SetComputeRootSignature(vdp2.composeRootSig.GetPointer());
        cmdList->SetComputeRoot32BitConstants(0, sizeof(vdp2.cpuCommonRenderParams) / sizeof(uint32),
                                              &vdp2.cpuCommonRenderParams, 0);
        cmdList->SetComputeRootDescriptorTable(1, frameCtx.composeDescs.GetGPUHandle());
        cmdList->Dispatch((HRes + 31) / 32, numLines, 1);
    }

    void VDP2RenderLine(uint32 y) {
        VDP2CalcAccessPatterns();
        VDP2CalcVCellScrollDelay();
        VDP2DrawLineColorBackScreens(y);
        VDP2UpdateRotationParameterBases(y);
        vdpState.state2.UpdateRotationPageBaseAddresses(vdpState.regs2);

        // When Y=0, the changes happened during vblank (or, more precisely, between the last Y of the previous frame
        // and the first line of this frame). Otherwise, the changes happened between Y-1 and Y. Therefore, we need to
        // render lines up to Y-1 then sync the state, unless Y=0, in which case we just sync the state.

        if (y > 0) {
            const FrameContext &frameCtx = frames.GetCurrentFrame();
            const bool cramDirty = vdp2.cramGeneration != frameCtx.cramGeneration;
            const bool layerRenderParamsDirty =
                vdp2.layerRenderParamsGeneration != frameCtx.layerRenderParamsGeneration;
            const bool composeParamsDirty = vdp2.composeParamsGeneration != frameCtx.composeParamsGeneration;
            const bool renderLayers = vdp2.vramDirty || cramDirty || layerRenderParamsDirty || composeParamsDirty;
            const bool compose = composeParamsDirty;
            if (renderLayers) {
                VDP2RenderLayerLines(y - 1);
            }
            if (compose) {
                VDP2ComposeLines(y - 1);
            }
        }

        VDP2UpdateState();
    }

    void VDP2EndFrame() {
        const uint32 vShift = vdpState.regs2.TVMD.IsInterlaced() ? 1u : 0u;
        const uint32 vres = VRes >> vShift;
        VDP2RenderLayerLines(vres - 1);
        VDP2ComposeLines(VRes - 1);

        // Request a frame from the frontend
        // TODO: consider adding support for GPU waits
        ID3D12Fence *fencePtr = computeFence.GetPointer();
        const UINT64 fenceValue = frames.GetNextFenceValue();
        const Dimensions requestedSize = resScale.current;
        const Dimensions renderArea = resScale.display;
        const Dimensions nativeRes = {HRes, VRes};
        ID3D12Resource *copyTarget =
            hwCallbacks.FrameCopyRequest(fencePtr, fenceValue, requestedSize, renderArea, nativeRes);
        if (copyTarget != nullptr) {
            // Transition composited output texture to copy source
            barrierTracker.TransitionTexture(vdp2.compositeOutTexture.GetPointer(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                                             D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_COPY_SOURCE,
                                             D3D12_BARRIER_LAYOUT_COPY_SOURCE);
            barrierTracker.Flush(cmdList);

            // Copy composited output texture to provided texture
            if constexpr (static_config::copyFullCompositeResource) {
                // Full resource copy
                cmdList->CopyResource(copyTarget, vdp2.compositeOutTexture.GetPointer());
            } else {
                // Display area copy
                const D3D12_TEXTURE_COPY_LOCATION dstLoc{
                    .pResource = copyTarget,
                    .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
                    .SubresourceIndex = 0,
                };
                const D3D12_TEXTURE_COPY_LOCATION srcLoc{
                    .pResource = vdp2.compositeOutTexture.GetPointer(),
                    .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
                    .SubresourceIndex = 0,
                };
                const D3D12_BOX srcBox{
                    .left = 0,
                    .top = 0,
                    .front = 0,
                    .right = HRes,
                    .bottom = VRes,
                    .back = 1,
                };
                cmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, &srcBox);
            }
        }

        // Close and submit command list
        cmdList->Close();
        cmdQueue->ExecuteCommandLists(1, cmdList.GetAddressOfBase());

        // Advance frame
        uploadBuffer.EndFrame(frames.GetNextFenceValue());
        frames.MoveToNextFrame(computeFence, cmdQueue);

        // Setup command list
        FrameContext &nextFrame = frames.GetCurrentFrame();
        ID3D12DescriptorHeap *heaps[] = {resourceHeap.GetPointer()};
        cmdList->Reset(nextFrame.cmdAlloc.GetPointer(), nullptr);
        cmdList->SetDescriptorHeaps(std::size(heaps), heaps);
    }

    void UpdateFrameParameters() {
        UpdateResolutionScaling();

        EnhancementsParams &enh1 = vdp1.cpuCommonRenderParams.enhancements;
        enh1.deinterlace = enhancements.deinterlace;
        enh1.transparentMeshes = enhancements.transparentMeshes;
        enh1.scaleResolution = enhancements.scaleResolution;
        enh1.scaleResTargetWidth = resScale.display.width - 1u;
        enh1.scaleResTargetHeight = resScale.display.height - 1u;

        vdp2.cpuCommonRenderParams.enhancements = enh1;

        if (auto result = RecreateScaledObjects(); !result) {
            devlog::warn<grp::dx12_base>("Failed to recreate scaled objects: {}", result.Error().message);
            YMIR_DEV_CHECK();
        }
    }

    /// @brief Submits the command list as is and restarts it in the same frame.
    /// Signals and increments the compute fence without advancing the frame.
    /// @return the signaled fence value or an error message
    util::ValueResult<UINT64> SubmitCommandList() {
        // Close and submit command list
        cmdList->Close();
        cmdQueue->ExecuteCommandLists(1, cmdList.GetAddressOfBase());

        // Advance the fence
        auto result = frames.IncrementFence(computeFence, cmdQueue);

        // Setup command list
        FrameContext &currFrame = frames.GetCurrentFrame();
        ID3D12DescriptorHeap *heaps[] = {resourceHeap.GetPointer()};
        cmdList->Reset(currFrame.cmdAlloc.GetPointer(), nullptr);
        cmdList->SetDescriptorHeaps(std::size(heaps), heaps);

        return result;
    }
};

// ---------------------------------------------------------------------------------------------------------------------

Direct3D12VDPRenderer::Direct3D12VDPRenderer(VDPState &state, const config::VDP2DebugRender &vdp2DebugRenderOptions,
                                             const config::VDP2AccessPatternsConfig &vdp2AccessPatternsConfig)
    : HardwareVDPRendererBase(VDPRendererType::Direct3D12)
    , m_impl(std::make_unique<Impl>(HwCallbacks, state, vdp2AccessPatternsConfig, vdp2DebugRenderOptions,
                                    m_enhancements)) {}

Direct3D12VDPRenderer::~Direct3D12VDPRenderer() {
    m_impl->Shutdown();
}

util::VoidResult<> Direct3D12VDPRenderer::Initialize(ID3D12Device *device) {
    return m_impl->Initialize(device);
}

util::ObjectResult<Direct3D12VDPRenderer>
Direct3D12VDPRenderer::Create(VDPState &state, const config::VDP2DebugRender &vdp2DebugRenderOptions,
                              const config::VDP2AccessPatternsConfig &vdp2AccessPatternsConfig, ID3D12Device *device) {
    if (device == nullptr) {
        return util::ErrorMessage{"No Direct3D 12 device instance provided"};
    }
    std::unique_ptr<Direct3D12VDPRenderer> renderer{
        new Direct3D12VDPRenderer(state, vdp2DebugRenderOptions, vdp2AccessPatternsConfig)};
    util::VoidResult<> result = renderer->Initialize(device);
    if (!result) {
        return result.Error();
    }
    return renderer;
}

// -----------------------------------------------------------------------------
// Basics

bool Direct3D12VDPRenderer::IsValid() const {
    return true;
}

void Direct3D12VDPRenderer::Reset(bool hard) {
    m_impl->Reset();
}

// -----------------------------------------------------------------------------
// Save states

void Direct3D12VDPRenderer::PreSaveStateSync() {}

void Direct3D12VDPRenderer::PostLoadStateSync() {
    m_impl->vdp1.vramDirty.SetAll();
    if (auto result = m_impl->VDP1UploadFBRAM(); !result) {
        devlog::warn<grp::dx12_base>("Failed to upload VDP1 FBRAM: {}", result.Error().message);
    }

    m_impl->VDP2CacheAllCRAMColors();
    m_impl->VDP2UpdateEnabledLayers();
    m_impl->vdp2.vramDirty.SetAll();
    ++m_impl->vdp2.cramGeneration;
    ++m_impl->vdp2.layerRenderParamsGeneration;
    ++m_impl->vdp2.composeParamsGeneration;
}

void Direct3D12VDPRenderer::SaveState(savestate::VDPSaveState::VDPRendererSaveState &state) {}

bool Direct3D12VDPRenderer::ValidateState(const savestate::VDPSaveState::VDPRendererSaveState &state) const {
    return true;
}

void Direct3D12VDPRenderer::LoadState(const savestate::VDPSaveState::VDPRendererSaveState &state) {}

// -----------------------------------------------------------------------------
// VDP1 memory and register writes

void Direct3D12VDPRenderer::VDP1WriteVRAM(uint32 address, uint8 value) {
    m_impl->VDP1WriteVRAM(address);
}

void Direct3D12VDPRenderer::VDP1WriteVRAM(uint32 address, uint16 value) {
    // The address is always word-aligned
    m_impl->VDP1WriteVRAM(address);
}

void Direct3D12VDPRenderer::VDP1SyncFB() {
    m_impl->VDP1SyncFB();
}

void Direct3D12VDPRenderer::VDP1DebugSyncFB() {
    m_impl->VDP1DebugSyncFB();
}

void Direct3D12VDPRenderer::VDP1WriteFB(uint32 address, uint8 value) {
    m_impl->VDP1WriteFB(address, 1);
}

void Direct3D12VDPRenderer::VDP1WriteFB(uint32 address, uint16 value) {
    m_impl->VDP1WriteFB(address, 2);
}

void Direct3D12VDPRenderer::VDP1WriteReg(uint32 address, uint16 value) {
    // All important registers are passed as root 32-bit constants.
    // Nothing needs to be marked dirty as a result of VDP1 register changes.
}

// -------------------------------------------------------------------------
// VDP2 memory and register writes

void Direct3D12VDPRenderer::VDP2WriteVRAM(uint32 address, uint8 value) {
    m_impl->VDP2WriteVRAM(address);
}

void Direct3D12VDPRenderer::VDP2WriteVRAM(uint32 address, uint16 value) {
    // The address is always word-aligned
    m_impl->VDP2WriteVRAM(address);
}

void Direct3D12VDPRenderer::VDP2WriteCRAM(uint32 address, uint8 value) {
    m_impl->VDP2WriteCRAM(address);
}

void Direct3D12VDPRenderer::VDP2WriteCRAM(uint32 address, uint16 value) {
    // The address is always word-aligned
    m_impl->VDP2WriteCRAM(address);
}

void Direct3D12VDPRenderer::VDP2WriteReg(uint32 address, uint16 value) {
    m_impl->VDP2WriteReg(address, value);
}

// -----------------------------------------------------------------------------
// Debugger

void Direct3D12VDPRenderer::UpdateEnabledLayers() {
    m_impl->VDP2UpdateEnabledLayers();
}

// -----------------------------------------------------------------------------
// Utilities

void Direct3D12VDPRenderer::DumpExtraVDP1Framebuffers(std::ostream &out) const {
    // TODO: pause the world, download mesh buffers, copy to output
}

// -----------------------------------------------------------------------------
// Rendering process

void Direct3D12VDPRenderer::VDP1EraseFramebuffer(uint64 cycles) {
    m_impl->VDP1EraseFramebuffer(cycles);
}

void Direct3D12VDPRenderer::VDP1SwapFramebuffer() {
    m_impl->VDP1SwapFramebuffer();
    Callbacks.VDP1FramebufferSwap();
}

void Direct3D12VDPRenderer::VDP1BeginFrame() {
    m_impl->VDP1BeginFrame();
}

void Direct3D12VDPRenderer::VDP1ExecuteCommand(uint32 cmdAddress, VDP1Command::Control control) {
    m_impl->VDP1ExecuteCommand(cmdAddress, control);
}

void Direct3D12VDPRenderer::VDP1EndFrame() {
    Callbacks.VDP1DrawFinished();
}

void Direct3D12VDPRenderer::VDP2SetResolution(uint32 h, uint32 v, bool exclusive) {
    m_impl->HRes = h;
    m_impl->VRes = v;
    m_impl->exclusiveMonitor = exclusive;
}

void Direct3D12VDPRenderer::VDP2SetField(bool odd) {
    // Nothing to do. We're using the main VDP2 state for this.
}

void Direct3D12VDPRenderer::VDP2LatchTVMD() {
    // Nothing to do. We're using the main VDP2 state for this.
}

void Direct3D12VDPRenderer::VDP2BeginFrame() {
    m_impl->VDP2BeginFrame();
}

void Direct3D12VDPRenderer::VDP2RenderLine(uint32 y) {
    m_impl->VDP2RenderLine(y);
}

void Direct3D12VDPRenderer::VDP2EndFrame() {
    m_impl->VDP2EndFrame();
    Callbacks.VDP2DrawFinished();
}

} // namespace ymir::vdp
