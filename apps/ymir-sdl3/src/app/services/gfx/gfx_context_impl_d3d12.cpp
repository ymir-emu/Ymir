#include "gfx_context_impl_d3d12.hpp"

#include "gfx_context_spec_d3d12.hpp"

#include "gfx_texture_id_manager.hpp"

#include <ymir/gpu/d3d12/d3d12_debug.hpp>
#include <ymir/gpu/d3d12/d3d12_descriptor_heap.hpp>
#include <ymir/gpu/d3d12/d3d12_descriptor_heap_allocator.hpp>
#include <ymir/gpu/d3d12/d3d12_device.hpp>
#include <ymir/gpu/d3d12/d3d12_fence.hpp>
#include <ymir/gpu/d3d12/d3d12_pipeline_state.hpp>
#include <ymir/gpu/d3d12/d3d12_resource.hpp>
#include <ymir/gpu/d3d12/d3d12_root_signature.hpp>
#include <ymir/gpu/d3d12/d3d12_swap_chain.hpp>

#include <ymir/gpu/shaders/gpu_shaders.hpp>

#include <ymir/hw/vdp/vdp_defs.hpp>

#include <ymir/util/string.hpp>

#include <backends/imgui_impl_dx12.h>
#include <backends/imgui_impl_sdl3.h>

#include <d3d12.h>

#include <fmt/format.h>

#include <cmrc/cmrc.hpp>
CMRC_DECLARE(ymir_sdl3_shaders);

#include <array>
#include <atomic>
#include <deque>
#include <unordered_map>

using namespace ymir::gpu;
using namespace ymir::gpu::d3d12;

namespace app::gfx {

static DXGI_FORMAT ToD3D12Value(PixelFormat format) {
    switch (format) {
    case PixelFormat::Unknown: return DXGI_FORMAT_UNKNOWN;
    case PixelFormat::R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case PixelFormat::R8G8B8X8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM; // Note: A instead of X
    case PixelFormat::B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case PixelFormat::B8G8R8X8_UNORM: return DXGI_FORMAT_B8G8R8X8_UNORM;
    }
    return DXGI_FORMAT_UNKNOWN;
}

static const char *DXGIFormatName(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_UNKNOWN: return "UNKNOWN";
    case DXGI_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
    case DXGI_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
    case DXGI_FORMAT_B8G8R8X8_UNORM: return "B8G8R8X8_UNORM";
    default: return "<unhandled>";
    }
}

struct alignas(uint32) Float4 {
    float x, y, z, w;
};

struct alignas(uint32) Float3 {
    float x, y, z;
};

struct alignas(uint32) Float2 {
    float x, y;
};

struct Vertex {
    Float3 position;
    Float2 uv;
};

struct alignas(uint32) DrawTextureConstants {
    Float4 srcRect;
    Float4 dstRect;
    Float2 renderTargetSize;
    Float2 rotPivot;
    float rotAngle;
};

// -----------------------------------------------------------------------------

struct Direct3D12GraphicsContext::Impl {
    explicit Impl(const Direct3D12GraphicsContextSpec &spec)
        : spec(spec) {}

    static constexpr UINT kFrameCount = 3;

    Direct3D12GraphicsContextSpec spec;

    struct FrameContext {
        D3D12Resource renderTarget;
        D3D12CommandAllocator cmdAlloc;
        UINT64 fenceValue;
        DescriptorRange rtvDesc;
    };

    D3D12Device device;
    D3D12CommandAllocator cmdAllocOps;
    D3D12CommandQueue cmdQueue;
    D3D12GraphicsCommandList cmdListFrame;
    D3D12GraphicsCommandList cmdListOps;
    D3D12SwapChain swapchain;
    D3D12DescriptorHeap rtvHeap;
    DescriptorHeapAllocator rtvHeapAlloc;
    D3D12DescriptorHeap resourceHeap;
    DescriptorHeapAllocator resourceHeapAlloc;
    D3D12DescriptorHeap samplerHeap;
    DescriptorHeapAllocator samplerHeapAlloc;
    D3D12RootSignature rootSignatureFrame;
    D3D12PipelineState pipelineStateFrame;
    D3D12Fence fenceFrame;
    D3D12Fence fenceOps;
    UINT64 fenceValueOps = 1;
    std::array<FrameContext, kFrameCount> frames;

    VertexShader vertexShader;
    PixelShader pixelShader;

    struct RenderTargetPipeline {
        D3D12RootSignature rootSignature;
        D3D12PipelineState pipelineState;
    };
    std::unordered_map<DXGI_FORMAT, RenderTargetPipeline> renderTargetPipelines;

    UINT frameIndex = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
    D3D12_VIEWPORT viewport;
    D3D12_RECT scissorRect;

    DescriptorRange smpNearest;
    DescriptorRange smpLinear;

    D3D12Resource vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView;

    DrawTextureConstants drawTextureConstants;

    static constexpr D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    PresentMode presentMode = PresentMode::VSync;

    struct Features {
        bool enhancedBarriers = false;
    } features;

    struct TextureInstance {
        Texture2DSpec spec;
        D3D12Resource resource;
        std::array<D3D12Resource, kFrameCount> stagingBuffers;
        std::array<void *, kFrameCount> stagingBuffersData;
        bool isReserved; // reserved for display textures

        DescriptorRange srvDesc;
        DescriptorRange rtvDesc; // only valid for TextureAccess::RenderTarget

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
        UINT numRows;
        UINT64 rowSizeBytes;
        UINT64 uploadBufferSize;
        size_t rowPitch;
    };

    struct TextureToDelete {
        D3D12Resource texture;
        std::array<D3D12Resource, kFrameCount> stagingBuffers;
        UINT srvIndex;
        UINT rtvIndex;
        UINT64 targetFenceValue;
        bool isRenderTarget;

        void Destroy(DescriptorHeapAllocator &resourceHeapAlloc, DescriptorHeapAllocator &rtvHeapAlloc) {
            for (int i = 0; i < kFrameCount; ++i) {
                stagingBuffers[i]->Unmap(0, nullptr);
                stagingBuffers[i].Destroy();
            }
            texture.Destroy();
            resourceHeapAlloc.Free(srvIndex);
            if (isRenderTarget) {
                rtvHeapAlloc.Free(rtvIndex);
            }
        }
    };

    TextureIDManager texIDMgr;

    std::unordered_map<TextureID, TextureInstance> textures;
    std::deque<TextureToDelete> texturesToDelete;

    struct DisplayFrameContext {
        TextureID textureID;
        D3D12Resource readbackTexture;
        void *readbackTexturePtr = nullptr;
        size_t readbackTextureSize = 0;

        uint32 width;
        uint32 height;

        std::atomic<ID3D12Fence *> computeFence; // Compute fence to be waited on
        std::atomic<UINT64> computeFenceValue;   // Value to wait for
        std::atomic<UINT64> graphicsFenceValue;  // Graphics fence value on which this frame is being used
        UINT64 lastComputeFenceValue = 0;
    };
    std::array<DisplayFrameContext, kFrameCount + 1> displayFrames;

    static constexpr size_t kInvalidFrameIndex = -1;

    // Index of next display frame for the VDP compute renderer to draw into
    size_t computeDisplayFrame = 0;

    // Index of the display frame containing the latest rendered graphics
    size_t graphicsDisplayFrame = kInvalidFrameIndex;

    // -------------------------------------------------------------------------

    util::VoidResult<> Init() {
        if (spec.window == nullptr) {
            return util::ErrorMessage{"No window provided to Direct3D 12 specification"};
        }

        SDL_PropertiesID windowProps = SDL_GetWindowProperties(spec.window);
        auto hwnd = static_cast<HWND>(SDL_GetPointerProperty(windowProps, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        if (hwnd == nullptr) {
            return util::ErrorMessage{"Could not get window handle"};
        }

        RECT windowRect;
        if (!GetClientRect(hwnd, &windowRect)) {
            return util::ErrorMessage{"Could not get window client area size"};
        }

        viewport.TopLeftX = 0.0f;
        viewport.TopLeftY = 0.0f;
        viewport.Width = windowRect.right;
        viewport.Height = windowRect.bottom;

        scissorRect.left = 0;
        scissorRect.top = 0;
        scissorRect.right = windowRect.right;
        scissorRect.bottom = windowRect.bottom;

        DebugLayer &debugLayer = DebugLayer::Get();

        UINT dxgiFactoryFlags = 0;
        if (debugLayer.Init() && debugLayer.IsEnabled()) {
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }

        if (FAILED(device.Create(spec.adapter, spec.featureLevel))) {
            return util::ErrorMessage{"Failed to create device"};
        }
        debugLayer.BreakOnWarnings(device.GetPointer(), true);
        device->SetName(L"[Ymir-GCtx] D3D12 device");

        D3D12_FEATURE_DATA_D3D12_OPTIONS12 options12{};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &options12, sizeof(options12)))) {
            features.enhancedBarriers = options12.EnhancedBarriersSupported;
        }

        if (FAILED(cmdAllocOps.Create(device, D3D12_COMMAND_LIST_TYPE_DIRECT))) {
            return util::ErrorMessage{"Failed to create operations command allocator"};
        }
        cmdAllocOps->SetName(L"[Ymir-GCtx] Operations command allocator");
        fenceValueOps = 1;

        if (FAILED(cmdQueue.Create(device, D3D12_COMMAND_LIST_TYPE_DIRECT))) {
            return util::ErrorMessage{"Failed to create command queue"};
        }
        cmdQueue->SetName(L"[Ymir-GCtx] Command queue");

        // Create synchronization objects
        if (FAILED(fenceFrame.Create(device, 0, D3D12_FENCE_FLAG_NONE))) {
            return util::ErrorMessage{"Failed to create frame fence"};
        }
        fenceFrame->SetName(L"[Ymir-GCtx] Frame fence");

        if (FAILED(fenceOps.Create(device, 0, D3D12_FENCE_FLAG_NONE))) {
            return util::ErrorMessage{"Failed to create operations fence"};
        }
        fenceOps->SetName(L"[Ymir-GCtx] Operations fence");

        DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
        swapChainDesc.BufferCount = kFrameCount;
        swapChainDesc.Width = windowRect.right;
        swapChainDesc.Height = windowRect.bottom;
        swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.Scaling = DXGI_SCALING_NONE;
        swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        if (!swapchain.Create(dxgiFactoryFlags, cmdQueue.GetPointer(), swapChainDesc, hwnd, kFrameCount)) {
            return util::ErrorMessage{"Failed to create swapchain"};
        }
        frameIndex = swapchain->GetCurrentBackBufferIndex();

        // Create descriptor heaps
        {
            D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
            rtvHeapDesc.NumDescriptors = 256;
            rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            if (FAILED(rtvHeap.Create(device, rtvHeapDesc))) {
                return util::ErrorMessage{"Failed to create RTV descriptor heap"};
            }
            rtvHeap->SetName(L"[Ymir-GCtx] RTV heap");
            rtvHeapAlloc.Bind(rtvHeap);

            D3D12_DESCRIPTOR_HEAP_DESC resourceHeapDesc{};
            resourceHeapDesc.NumDescriptors = 256;
            resourceHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            resourceHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            if (FAILED(resourceHeap.Create(device, resourceHeapDesc))) {
                return util::ErrorMessage{"Failed to create CBV/SRV/UAV heap"};
            }
            resourceHeap->SetName(L"[Ymir-GCtx] CBV/SRV/UAV heap");
            resourceHeapAlloc.Bind(resourceHeap);

            D3D12_DESCRIPTOR_HEAP_DESC samplerHeapDesc{};
            samplerHeapDesc.NumDescriptors = 2;
            samplerHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            samplerHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
            if (FAILED(samplerHeap.Create(device, samplerHeapDesc))) {
                return util::ErrorMessage{"Failed to create sampler heap"};
            }
            samplerHeap->SetName(L"[Ymir-GCtx] Sampler heap");
            samplerHeapAlloc.Bind(samplerHeap);
        }

        // Create frame resources
        for (UINT n = 0; n < kFrameCount; n++) {
            ID3D12Resource *resource;
            if (FAILED(swapchain->GetBuffer(n, IID_PPV_ARGS(&resource)))) {
                return util::ErrorMessage{fmt::format("Failed to get swapchain buffer {}", n)};
            }
            if (FAILED(frames[n].cmdAlloc.Create(device, D3D12_COMMAND_LIST_TYPE_DIRECT))) {
                return util::ErrorMessage{fmt::format("Failed to create command allocator for swapchain frame #{}", n)};
            }
            DescriptorRange &rtvDesc = frames[n].rtvDesc;
            if (!rtvHeapAlloc.Allocate(rtvDesc)) {
                return util::ErrorMessage{fmt::format("Failed to allocate RTV for swapchain frame #{}", n)};
            }
            frames[n].cmdAlloc->SetName(
                fmt::format(L"[Ymir-GCtx] Command allocator for swapchain buffer #{}", n).c_str());
            device->CreateRenderTargetView(resource, nullptr, rtvDesc.cpuHandle);
            resource->SetName(fmt::format(L"[Ymir-GCtx] Swapchain buffer #{}", n).c_str());
            frames[n].renderTarget.Attach(resource);
            frames[n].fenceValue = 1;
        }

        // Create command lists
        if (FAILED(cmdListFrame.Create(device, frames[frameIndex].cmdAlloc, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                       pipelineStateFrame.GetPointer()))) {
            return util::ErrorMessage{"Failed to create frame command list"};
        }
        cmdListFrame->Close();
        cmdListFrame->SetName(L"[Ymir-GCtx] Frame command list");

        if (FAILED(cmdListOps.Create(device, cmdAllocOps, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                     pipelineStateFrame.GetPointer()))) {
            return util::ErrorMessage{"Failed to create operations command list"};
        }
        cmdListOps->SetName(L"[Ymir-GCtx] Operations command list");

        // Create display frame resources
        for (UINT n = 0; n < kFrameCount + 1; n++) {
            static constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

            DisplayFrameContext &frameCtx = displayFrames[n];

            // Create the hardware texture
            const Texture2DSpec spec{
                .width = ymir::vdp::kMaxResH,
                .height = ymir::vdp::kMaxResV,
                .format = PixelFormat::R8G8B8A8_UNORM,
                .access = TextureAccess::Static,
                .filterMode = TextureFilterMode::Nearest,
                .name = fmt::format("[Ymir-GCtx] Display output #{}", n),
            };
            auto textureResult = CreateTexture(spec, true);
            if (!textureResult) {
                return util::ErrorMessage{
                    fmt::format("Failed to create display output #{} texture: {}", n, textureResult.Error().message)};
            }
            frameCtx.textureID = texIDMgr.GetNextTextureID();
            auto [itDispTex, ok] = textures.insert({frameCtx.textureID, textureResult.Value()});
            assert(ok);

            // Create the readback texture
            auto builder = frameCtx.readbackTexture.BufferBuilder(itDispTex->second.uploadBufferSize);
            builder.HeapType(D3D12_HEAP_TYPE_READBACK);
            builder.InitialState(D3D12_RESOURCE_STATE_COPY_DEST);
            if (HRESULT hr = builder.BuildCommitted(device); FAILED(hr)) {
                return util::ErrorMessage{fmt::format(
                    "Failed to create display output readback texture #{}, error code {:X}", n, (uint32)hr)};
            }
            if (HRESULT hr = frameCtx.readbackTexture->Map(0, nullptr, &frameCtx.readbackTexturePtr); FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Failed to map display output readback texture #{}, error code {:X}", n, (uint32)hr)};
            }
            frameCtx.readbackTextureSize = itDispTex->second.uploadBufferSize;
            if (!spec.name.empty()) {
                frameCtx.readbackTexture->SetName(
                    fmt::format(L"[Ymir-GCtx] Display output readback texture #{}", n).c_str());
            }

            // Transition hardware texture to COPY_DEST if using enhanced barriers
            if (auto *enhCmdList = GetCommandListForEnhancedBarriers(cmdListOps)) {
                D3D12_TEXTURE_BARRIER barrier{
                    .SyncBefore = D3D12_BARRIER_SYNC_NONE,
                    .SyncAfter = D3D12_BARRIER_SYNC_NONE,
                    .AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS,
                    .AccessAfter = D3D12_BARRIER_ACCESS_NO_ACCESS,
                    .LayoutBefore = D3D12_BARRIER_LAYOUT_COMMON,
                    .LayoutAfter = D3D12_BARRIER_LAYOUT_COPY_DEST,
                    .pResource = textures[frameCtx.textureID].resource.GetPointer(),
                    .Subresources =
                        {
                            .IndexOrFirstMipLevel = 0,
                            .NumMipLevels = 0,
                            .FirstArraySlice = 0,
                            .NumArraySlices = 0,
                            .FirstPlane = 0,
                            .NumPlanes = 0,
                        },
                    .Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE,
                };
                const D3D12_BARRIER_GROUP group{
                    .Type = D3D12_BARRIER_TYPE_TEXTURE,
                    .NumBarriers = 1,
                    .pTextureBarriers = &barrier,
                };
                enhCmdList->Barrier(1, &group);
            }

            // These come from the VDP renderer callback
            frameCtx.computeFence.store(nullptr, std::memory_order_release);
            frameCtx.computeFenceValue.store(0, std::memory_order_release);

            // This is filled in when requested by the frontend
            frameCtx.graphicsFenceValue.store(0, std::memory_order_release);
        }

        computeDisplayFrame = 0;
        graphicsDisplayFrame = kInvalidFrameIndex;

        // Create root signature for texture drawing operations with:
        // [0] descriptor table with one SRV slot for the texture to draw
        // [1] descriptor table with one sampler slot to pick between nearest neighbor and linear interpolation
        // [2] 32-bit constants holding the drawing parameters
        {
            auto rootSigBuilder = rootSignatureFrame.Builder();
            rootSigBuilder.Flags(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
            rootSigBuilder.AddDescriptorTable(D3D12_SHADER_VISIBILITY_PIXEL).AddSRVs(1, 0);
            rootSigBuilder.AddDescriptorTable(D3D12_SHADER_VISIBILITY_PIXEL).AddSamplers(1, 0);
            rootSigBuilder.Add32BitConstants(0, sizeof(DrawTextureConstants) / sizeof(uint32));
            if (HRESULT hr = rootSigBuilder.Build(device); FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Failed to create texture operations root signature, error code {:X}", (uint32)hr)};
            }
            rootSignatureFrame->SetName(L"[Ymir-GCtx] Texture operations root signature");
        }

        // Create nearest neighbor and linear samplers
        {
            // Allocate descriptors
            if (!samplerHeapAlloc.Allocate(smpNearest)) {
                return util::ErrorMessage{"Could not allocate nearest neighbor sampler descriptor"};
            }
            if (!samplerHeapAlloc.Allocate(smpLinear)) {
                return util::ErrorMessage{"Could not allocate linear sampler descriptor"};
            }

            // Common sampler parameters
            D3D12_SAMPLER_DESC samplerDesc{
                .AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER,
                .AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER,
                .AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER,
                .ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER,
                .BorderColor = {0.0f, 0.0f, 0.0f, 0.0f},
                .MinLOD = 0.0f,
                .MaxLOD = D3D12_FLOAT32_MAX,
            };

            // Nearest neighbor
            samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT,
            device->CreateSampler(&samplerDesc, smpNearest.cpuHandle);

            // Linear
            samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR,
            device->CreateSampler(&samplerDesc, smpLinear.cpuHandle);
        }

        // Load shaders
        {
            auto fs = cmrc::ymir_sdl3_shaders::get_filesystem();
            auto loadShader = [&](const char *path) -> util::ValueResult<std::vector<char>> {
                assert(fs.is_file(path));
                auto shaderFile = fs.open(path);
                return std::vector<char>{shaderFile.begin(), shaderFile.end()};
            };

            // Load vertex shader
            auto vertexShaderBytecodeResult = loadShader("gctx/quad_vs.cso");
            if (!vertexShaderBytecodeResult) {
                return util::ErrorMessage{
                    fmt::format("Could not load vertex shader: {}", vertexShaderBytecodeResult.Error().message)};
            }
            vertexShader.format = ShaderBytecodeFormat::DXIL;
            vertexShader.bytecode = vertexShaderBytecodeResult.Value();
            vertexShader.entrypoint = "VSMain";
            if (auto result = ValidateShader(vertexShader); !result) {
                return util::ErrorMessage{fmt::format("Vertex shader validation failed: {}", result.Error().message)};
            }

            // Load pixel shader
            auto pixelShaderBytecodeResult = loadShader("gctx/quad_ps.cso");
            if (!pixelShaderBytecodeResult) {
                return util::ErrorMessage{
                    fmt::format("Could not load pixel shader: {}", pixelShaderBytecodeResult.Error().message)};
            }
            pixelShader.format = ShaderBytecodeFormat::DXIL;
            pixelShader.bytecode = pixelShaderBytecodeResult.Value();
            pixelShader.entrypoint = "PSMain";
            if (auto result = ValidateShader(pixelShader); !result) {
                return util::ErrorMessage{fmt::format("Pixel shader validation failed: {}", result.Error().message)};
            }
        }

        // Create the graphics pipeline state object (PSO)
        {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.InputLayout = {inputElementDescs, std::size(inputElementDescs)};
            psoDesc.pRootSignature = rootSignatureFrame.GetPointer();
            psoDesc.VS = {.pShaderBytecode = vertexShader.bytecode.data(),
                          .BytecodeLength = vertexShader.bytecode.size()};
            psoDesc.PS = {.pShaderBytecode = pixelShader.bytecode.data(),
                          .BytecodeLength = pixelShader.bytecode.size()};
            psoDesc.RasterizerState = {
                .FillMode = D3D12_FILL_MODE_SOLID,
                .CullMode = D3D12_CULL_MODE_BACK,
                .FrontCounterClockwise = FALSE,
                .DepthBias = D3D12_DEFAULT_DEPTH_BIAS,
                .DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP,
                .SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS,
                .DepthClipEnable = TRUE,
                .MultisampleEnable = FALSE,
                .AntialiasedLineEnable = FALSE,
                .ForcedSampleCount = 0,
                .ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF,
            };
            psoDesc.BlendState = {
                .AlphaToCoverageEnable = FALSE,
                .IndependentBlendEnable = FALSE,
                .RenderTarget = {{
                    .BlendEnable = FALSE,
                    .LogicOpEnable = FALSE,
                    .SrcBlend = D3D12_BLEND_ONE,
                    .DestBlend = D3D12_BLEND_ZERO,
                    .BlendOp = D3D12_BLEND_OP_ADD,
                    .SrcBlendAlpha = D3D12_BLEND_ONE,
                    .DestBlendAlpha = D3D12_BLEND_ZERO,
                    .BlendOpAlpha = D3D12_BLEND_OP_ADD,
                    .LogicOp = D3D12_LOGIC_OP_NOOP,
                    .RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL,
                }},
            };
            psoDesc.DepthStencilState.DepthEnable = FALSE;
            psoDesc.DepthStencilState.StencilEnable = FALSE;
            psoDesc.SampleMask = UINT_MAX;
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets = 1;
            psoDesc.RTVFormats[0] = swapChainDesc.Format;
            psoDesc.SampleDesc.Count = 1;
            if (HRESULT hr = pipelineStateFrame.CreateGraphics(device, psoDesc); FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Failed to create graphics pipeline state object, error code {:X}", (uint32)hr)};
            }
            pipelineStateFrame->SetName(L"[Ymir-GCtx] Base graphics pipeline");
        }

        // Create the vertex buffer
        D3D12Resource vertexUploadBuffer{};
        {
            // Define the geometry for a quad
            Vertex vertices[] = {
                {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
                {{1.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
                {{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
                {{1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
            };

            const UINT vertexBufferSize = sizeof(vertices);
            const D3D12_RESOURCE_DESC vertexBufferDesc{
                .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
                .Alignment = 0,
                .Width = vertexBufferSize,
                .Height = 1,
                .DepthOrArraySize = 1,
                .MipLevels = 1,
                .Format = DXGI_FORMAT_UNKNOWN,
                .SampleDesc = {.Count = 1, .Quality = 0},
                .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                .Flags = D3D12_RESOURCE_FLAG_NONE,
            };

            // Create upload buffer
            if (HRESULT hr =
                    vertexUploadBuffer.CreateCommitted(device, {.Type = D3D12_HEAP_TYPE_UPLOAD}, D3D12_HEAP_FLAG_NONE,
                                                       vertexBufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ);
                FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Failed to create vertex upload buffer, error code {:X}", (uint32)hr)};
            }
            vertexUploadBuffer->SetName(L"[Ymir-GCtx] Vertex upload buffer");

            // Create vertex buffer
            if (HRESULT hr =
                    vertexBuffer.CreateCommitted(device, {.Type = D3D12_HEAP_TYPE_DEFAULT}, D3D12_HEAP_FLAG_NONE,
                                                 vertexBufferDesc, D3D12_RESOURCE_STATE_COMMON);
                FAILED(hr)) {
                return util::ErrorMessage{fmt::format("Failed to create vertex buffer, error code {:X}", (uint32)hr)};
            }
            vertexBuffer->SetName(L"[Ymir-GCtx] Vertex buffer");

            // Copy the quad data to the upload buffer
            UINT8 *pVertexDataBegin;
            D3D12_RANGE readRange(0, 0);
            if (HRESULT hr = vertexUploadBuffer->Map(0, &readRange, reinterpret_cast<void **>(&pVertexDataBegin));
                FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Could not map vertex upload buffer, error code {:X}", (uint32)hr)};
            }
            memcpy(pVertexDataBegin, vertices, sizeof(vertices));
            vertexUploadBuffer->Unmap(0, nullptr);

            if (auto *enhCmdList = GetCommandListForEnhancedBarriers(cmdListOps)) {
                // Indicate that the vertex buffer will be used as copy destination
                D3D12_BUFFER_BARRIER barrier{
                    .SyncBefore = D3D12_BARRIER_SYNC_VERTEX_SHADING,
                    .SyncAfter = D3D12_BARRIER_SYNC_COPY,
                    .AccessBefore = D3D12_BARRIER_ACCESS_COMMON,
                    .AccessAfter = D3D12_BARRIER_ACCESS_COPY_DEST,
                    .pResource = vertexBuffer.GetPointer(),
                    .Offset = 0,
                    .Size = vertexBufferSize,
                };
                const D3D12_BARRIER_GROUP group{
                    .Type = D3D12_BARRIER_TYPE_BUFFER,
                    .NumBarriers = 1,
                    .pBufferBarriers = &barrier,
                };
                enhCmdList->Barrier(1, &group);

                cmdListOps->CopyBufferRegion(vertexBuffer.GetPointer(), 0, vertexUploadBuffer.GetPointer(), 0,
                                             vertexBufferSize);

                // Indicate that the vertex buffer will be used for generic reads
                std::swap(barrier.SyncBefore, barrier.SyncAfter);
                std::swap(barrier.AccessBefore, barrier.AccessAfter);
                enhCmdList->Barrier(1, &group);
            } else {
                // Indicate that the vertex buffer will be used as copy destination
                D3D12_RESOURCE_BARRIER barrier{
                    .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                    .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
                    .Transition =
                        {
                            .pResource = vertexBuffer.GetPointer(),
                            .Subresource = 0,
                            .StateBefore = D3D12_RESOURCE_STATE_COMMON,
                            .StateAfter = D3D12_RESOURCE_STATE_COPY_DEST,
                        },
                };
                cmdListOps->ResourceBarrier(1, &barrier);

                cmdListOps->CopyBufferRegion(vertexBuffer.GetPointer(), 0, vertexUploadBuffer.GetPointer(), 0,
                                             vertexBufferSize);

                // Indicate that the vertex buffer will be used for generic reads
                std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
                cmdListOps->ResourceBarrier(1, &barrier);
            }

            // Initialize the vertex buffer view
            vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
            vertexBufferView.StrideInBytes = sizeof(Vertex);
            vertexBufferView.SizeInBytes = vertexBufferSize;
        }

        // Execute command list
        if (HRESULT hr = cmdListOps->Close(); FAILED(hr)) {
            return util::ErrorMessage{
                fmt::format("Failed to close operations command list, error code {:X}", (uint32)hr)};
        }
        cmdQueue->ExecuteCommandLists(1, cmdListOps.GetAddressOfBase());

        if (HRESULT hr = fenceOps.Signal(cmdQueue, fenceValueOps); FAILED(hr)) {
            return util::ErrorMessage{
                fmt::format("Failed to signal operations command list, error code {:X}", (uint32)hr)};
        }
        fenceOps.Wait(INFINITE, fenceValueOps);
        ++fenceValueOps;

        if (auto result = BeginFrame(); !result) {
            return util::ErrorMessage{fmt::format("Could not begin frame: {}", result.Error().message)};
        }
        return {};
    }

    void Shutdown() {
        DeletePendingTextures(true);
        textures.clear();
        for (UINT n = 0; n < kFrameCount; n++) {
            frames[n].renderTarget.Destroy();
            frames[n].cmdAlloc.Destroy();
        }
        fenceFrame.Destroy();
        fenceOps.Destroy();
        vertexBuffer.Destroy();
        vertexBufferView.BufferLocation = {};
        renderTargetPipelines.clear();
        pipelineStateFrame.Destroy();
        rootSignatureFrame.Destroy();
        cmdListFrame.Destroy();
        cmdListOps.Destroy();
        cmdQueue.Destroy();
        cmdAllocOps.Destroy();
        samplerHeapAlloc.Unbind();
        samplerHeap.Destroy();
        resourceHeapAlloc.Unbind();
        resourceHeap.Destroy();
        rtvHeapAlloc.Unbind();
        rtvHeap.Destroy();
        swapchain.Destroy();
        device.Destroy();
    }

    bool IsInitialized() const {
        return device.IsValid();
    }

    util::VoidResult<> ResizeFramebuffer(uint32 width, uint32 height) {
        if (auto result = EndFrame(); !result) {
            return util::ErrorMessage{fmt::format("Could not end frame: {}", result.Error().message)};
        }

        // Flush all current GPU commands
        WaitForGPU();

        // Destroy RTVs
        const UINT64 currFenceValue = GetCurrentFrameContext().fenceValue;
        for (UINT n = 0; n < kFrameCount; n++) {
            frames[n].renderTarget.Destroy();
        }

        // Resize swapchain buffers
        if (FAILED(swapchain.ResizeBuffers(width, height))) {
            return util::ErrorMessage{"Failed to resize swapchain buffers"};
        }
        frameIndex = swapchain->GetCurrentBackBufferIndex();

        // Recreate RTVs
        {
            for (UINT n = 0; n < kFrameCount; n++) {
                ID3D12Resource *resource;
                if (FAILED(swapchain->GetBuffer(n, IID_PPV_ARGS(&resource)))) {
                    return util::ErrorMessage{fmt::format("Failed to get swapchain buffer {}", n)};
                }
                DescriptorRange &rtvDesc = frames[n].rtvDesc;
                if (!rtvHeapAlloc.Allocate(rtvDesc)) {
                    return util::ErrorMessage{fmt::format("Failed to allocate RTV for swapchain frame #{}", n)};
                }
                device->CreateRenderTargetView(resource, nullptr, rtvDesc.cpuHandle);
                resource->SetName(fmt::format(L"[Ymir-GCtx] Swapchain buffer #{}", n).c_str());
                frames[n].renderTarget.Attach(resource);
                frames[n].fenceValue = currFenceValue;
            }
        }

        // Update current RTV handle
        rtvHandle = frames[frameIndex].rtvDesc.cpuHandle;

        // Update viewport and scissor rects
        viewport.Width = width;
        viewport.Height = height;
        scissorRect.right = width;
        scissorRect.bottom = height;

        if (auto result = BeginFrame(); !result) {
            return util::ErrorMessage{fmt::format("Could not begin frame: {}", result.Error().message)};
        }
        return {};
    }

    util::VoidResult<> BeginFrame() {
        const FrameContext &currFrame = GetCurrentFrameContext();
        const D3D12CommandAllocator &cmdAlloc = currFrame.cmdAlloc;

        if (HRESULT hr = cmdAlloc->Reset(); FAILED(hr)) {
            return util::ErrorMessage{
                fmt::format("Failed to reset frame command allocator, error code {:X}", (uint32)hr)};
        }
        if (HRESULT hr = cmdListFrame->Reset(cmdAlloc.GetPointer(), pipelineStateFrame.GetPointer()); FAILED(hr)) {
            return util::ErrorMessage{fmt::format("Failed to reset frame command list, error code {:X}", (uint32)hr)};
        }

        ID3D12DescriptorHeap *heaps[] = {resourceHeap.GetPointer(), samplerHeap.GetPointer()};
        cmdListFrame->SetDescriptorHeaps(std::size(heaps), heaps);

        cmdListFrame->SetGraphicsRootSignature(rootSignatureFrame.GetPointer());

        cmdListFrame->RSSetViewports(1, &viewport);
        cmdListFrame->RSSetScissorRects(1, &scissorRect);

        // Indicate that the back buffer will be used as a render target
        if (auto *enhCmdList = GetCommandListForEnhancedBarriers(cmdListFrame)) {
            D3D12_TEXTURE_BARRIER barrier{
                .SyncBefore = D3D12_BARRIER_SYNC_NONE,
                .SyncAfter = D3D12_BARRIER_SYNC_RENDER_TARGET,
                .AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS,
                .AccessAfter = D3D12_BARRIER_ACCESS_RENDER_TARGET,
                .LayoutBefore = D3D12_BARRIER_LAYOUT_COMMON,
                .LayoutAfter = D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                .pResource = currFrame.renderTarget.GetPointer(),
                .Subresources =
                    {
                        .IndexOrFirstMipLevel = 0,
                        .NumMipLevels = 0,
                        .FirstArraySlice = 0,
                        .NumArraySlices = 0,
                        .FirstPlane = 0,
                        .NumPlanes = 0,
                    },
                .Flags = D3D12_TEXTURE_BARRIER_FLAG_DISCARD,
            };
            const D3D12_BARRIER_GROUP group{
                .Type = D3D12_BARRIER_TYPE_TEXTURE,
                .NumBarriers = 1,
                .pTextureBarriers = &barrier,
            };
            enhCmdList->Barrier(1, &group);
        } else {
            D3D12_RESOURCE_BARRIER barrier{
                .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
                .Transition =
                    {
                        .pResource = currFrame.renderTarget.GetPointer(),
                        .Subresource = 0,
                        .StateBefore = D3D12_RESOURCE_STATE_PRESENT,
                        .StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET,
                    },
            };
            cmdListFrame->ResourceBarrier(1, &barrier);
        }

        rtvHandle = frames[frameIndex].rtvDesc.cpuHandle;
        cmdListFrame->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
        cmdListFrame->SetGraphicsRootSignature(rootSignatureFrame.GetPointer());

        drawTextureConstants.renderTargetSize.x = viewport.Width;
        drawTextureConstants.renderTargetSize.y = viewport.Height;

        DeletePendingTextures(false);

        return {};
    }

    util::VoidResult<> EndFrame() {
        const FrameContext &currFrame = GetCurrentFrameContext();

        // Indicate that the back buffer will be used for frame presentation
        if (auto *enhCmdList = GetCommandListForEnhancedBarriers(cmdListFrame)) {
            D3D12_TEXTURE_BARRIER barrier{
                .SyncBefore = D3D12_BARRIER_SYNC_RENDER_TARGET,
                .SyncAfter = D3D12_BARRIER_SYNC_NONE,
                .AccessBefore = D3D12_BARRIER_ACCESS_RENDER_TARGET,
                .AccessAfter = D3D12_BARRIER_ACCESS_NO_ACCESS,
                .LayoutBefore = D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                .LayoutAfter = D3D12_BARRIER_LAYOUT_COMMON,
                .pResource = currFrame.renderTarget.GetPointer(),
                .Subresources =
                    {
                        .IndexOrFirstMipLevel = 0,
                        .NumMipLevels = 0,
                        .FirstArraySlice = 0,
                        .NumArraySlices = 0,
                        .FirstPlane = 0,
                        .NumPlanes = 0,
                    },
                .Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE,
            };
            const D3D12_BARRIER_GROUP group{
                .Type = D3D12_BARRIER_TYPE_TEXTURE,
                .NumBarriers = 1,
                .pTextureBarriers = &barrier,
            };
            enhCmdList->Barrier(1, &group);
        } else {
            D3D12_RESOURCE_BARRIER barrier{
                .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
                .Transition =
                    {
                        .pResource = currFrame.renderTarget.GetPointer(),
                        .Subresource = 0,
                        .StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET,
                        .StateAfter = D3D12_RESOURCE_STATE_PRESENT,
                    },
            };
            cmdListFrame->ResourceBarrier(1, &barrier);
        }

        if (FAILED(cmdListFrame->Close())) {
            return util::ErrorMessage{"Failed to close frame command list"};
        }

        return {};
    }

    util::ValueResult<PresentResult> Present() {
        if (auto result = EndFrame(); !result) {
            return util::ErrorMessage{fmt::format("Could not end frame: {}", result.Error().message)};
        }

        ID3D12CommandList *ppCommandLists[] = {cmdListFrame.GetPointer()};
        cmdQueue->ExecuteCommandLists(std::size(ppCommandLists), ppCommandLists);

        // NOTE: VSync and Mailbox both wait for vertical retrace to present a frame. The difference is that enqueuing
        // frames in Mailbox mode replaces the next pending frame while VSync stores and presents all frames. As a
        // result, Mailbox has smaller perceived input lag.
        //
        // The swap chain is created with the DXGI_SWAP_EFFECT_FLIP_DISCARD flag, enabling Mailbox mode. Switching modes
        // involves destroying and recreating the entire swap chain, which doesn't seem to be worth the effort. Instead,
        // we'll treat VSync and Mailbox as the same mode.

        HRESULT hr;
        switch (presentMode) {
        default: [[fallthrough]];
        case PresentMode::VSync: hr = swapchain->Present(1, 0); break;
        case PresentMode::Mailbox: hr = swapchain->Present(1, 0); break;
        case PresentMode::Adaptive:
            hr = swapchain->Present(0, swapchain.IsTearingSupported() ? DXGI_PRESENT_ALLOW_TEARING : 0);
            break;
        case PresentMode::NoSync: hr = swapchain->Present(0, 0); break;
        }

        if (auto result = MoveToNextFrame(); !result) {
            return util::ErrorMessage{fmt::format("Could not advance frame: {}", result.Error().message)};
        }
        if (auto result = BeginFrame(); !result) {
            return util::ErrorMessage{fmt::format("Could not begin frame: {}", result.Error().message)};
        }

        if (hr == DXGI_STATUS_OCCLUDED) {
            return PresentResult::Occluded;
        }
        if (FAILED(hr)) {
            return util::ErrorMessage{fmt::format("Frame presentation failed, error code {:X}", (uint32)hr)};
        }
        return PresentResult::Ok;
    }

    util::VoidResult<> WaitForGPU() {
        FrameContext &currFrame = GetCurrentFrameContext();

        // Schedule a signal command in the queue
        if (FAILED(fenceFrame.Signal(cmdQueue, currFrame.fenceValue))) {
            return util::ErrorMessage{"Failed to signal fence"};
        }

        // Wait until the fence has been processed
        fenceFrame.Wait(INFINITE, currFrame.fenceValue);

        // Increment the fence value for the current frame
        ++currFrame.fenceValue;

        return {};
    }

    util::VoidResult<> MoveToNextFrame() {
        // Schedule a signal command in the queue
        const FrameContext &currFrame = GetCurrentFrameContext();
        const UINT64 currentFenceValue = currFrame.fenceValue;
        if (FAILED(fenceFrame.Signal(cmdQueue, currentFenceValue))) {
            return util::ErrorMessage{"Failed to signal fence"};
        }

        // Update the frame index
        frameIndex = swapchain->GetCurrentBackBufferIndex();

        // If the next frame is not ready to be rendered yet, wait until it is ready
        FrameContext &nextFrame = GetCurrentFrameContext();
        if (fenceFrame->GetCompletedValue() < nextFrame.fenceValue) {
            fenceFrame.Wait(INFINITE, nextFrame.fenceValue);
        }

        // Set the fence value for the next frame
        nextFrame.fenceValue = currentFenceValue + 1;

        return {};
    }

    util::ValueResult<TextureInstance> CreateTexture(const Texture2DSpec &spec, bool reserved) {
        TextureInstance texture;
        texture.isReserved = reserved;

        const bool isRenderTarget = spec.access == TextureAccess::RenderTarget;

        {
            auto builder = texture.resource.Texture2DBuilder(spec.width, spec.height);
            builder.Format(ToD3D12Value(spec.format));
            builder.HeapType(D3D12_HEAP_TYPE_DEFAULT);
            builder.InitialState(D3D12_RESOURCE_STATE_COMMON);
            if (isRenderTarget) {
                builder.Flags(D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
            }
            if (HRESULT hr = builder.BuildCommitted(device); FAILED(hr)) {
                return util::ErrorMessage{fmt::format("Could not create texture, error code {:X}", (uint32)hr)};
            }
            if (!spec.name.empty()) {
                texture.resource->SetName(fmt::format(L"{} texture", util::StringToWString(spec.name)).c_str());
            }
        }

        D3D12_RESOURCE_DESC desc = texture.resource->GetDesc();
        device->GetCopyableFootprints(&desc, 0, 1, 0, &texture.footprint, &texture.numRows, &texture.rowSizeBytes,
                                      &texture.uploadBufferSize);
        texture.rowPitch = PixelFormatUnitSize(spec.format) * spec.width;

        if (!reserved) {
            // In D3D12, textures cannot be directly written to by the CPU - a staging buffer is always needed.
            // Static and Streaming access modes have identical behavior.
            // We store one buffer per frame to enable parallel updates.
            for (int i = 0; i < kFrameCount; ++i) {
                D3D12Resource &buffer = texture.stagingBuffers[i];
                auto builder = buffer.BufferBuilder(texture.uploadBufferSize);
                builder.HeapType(D3D12_HEAP_TYPE_UPLOAD);
                builder.InitialState(D3D12_RESOURCE_STATE_GENERIC_READ);
                if (HRESULT hr = builder.BuildCommitted(device); FAILED(hr)) {
                    return util::ErrorMessage{
                        fmt::format("Could not create texture staging buffer #{}, error code {:X}", i, (uint32)hr)};
                }
                if (HRESULT hr = buffer->Map(0, nullptr, &texture.stagingBuffersData[i]); FAILED(hr)) {
                    return util::ErrorMessage{
                        fmt::format("Could not map texture staging buffer #{}, error code {:X}", i, (uint32)hr)};
                }
                if (!spec.name.empty()) {
                    buffer->SetName(fmt::format(L"{} staging buffer #{}", util::StringToWString(spec.name), i).c_str());
                }
            }
        }

        if (!resourceHeapAlloc.Allocate(texture.srvDesc)) {
            return util::ErrorMessage{"Could not allocate SRV for texture"};
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(texture.resource.GetPointer(), &srvDesc, texture.srvDesc.cpuHandle);

        if (isRenderTarget) {
            if (!rtvHeapAlloc.Allocate(texture.rtvDesc)) {
                return util::ErrorMessage{"Could not allocate RTV for texture"};
            }

            D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{
                .Format = desc.Format,
                .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
                .Texture2D =
                    {
                        .MipSlice = 0,
                        .PlaneSlice = 0,
                    },
            };
            device->CreateRenderTargetView(texture.resource.GetPointer(), &rtvDesc, texture.rtvDesc.cpuHandle);
        }
        texture.spec = spec;

        return texture;
    }

    util::VoidResult<> ResizeTexture(TextureID id, uint32 width, uint32 height) {
        auto it = textures.find(id);
        if (it == textures.end()) {
            return util::ErrorMessage{"Texture does not exist"};
        }
        TextureInstance &texture = it->second;
        if (texture.isReserved) {
            return util::ErrorMessage{"Cannot resize reserved texture"};
        }

        // First, try creating new texture using the existing texture's specifications
        Texture2DSpec newSpec = texture.spec;
        newSpec.width = width;
        newSpec.height = height;
        auto createResult = CreateTexture(newSpec, texture.isReserved);
        if (!createResult) {
            return createResult.Error();
        }

        // Now that we've succeeded, mark the previous texture for deletion and replace it
        SubmitTextureForDeletion(texture);
        texture = createResult.Value();

        return {};
    }

    void DestroyTexture(TextureID id) {
        auto it = textures.find(id);
        if (it == textures.end()) {
            return;
        }
        auto &texture = it->second;
        if (texture.isReserved) {
            return;
        }
        SubmitTextureForDeletion(texture);
        textures.erase(it);
    }

    void SubmitTextureForDeletion(TextureInstance &texture) {
        const FrameContext &currFrame = GetCurrentFrameContext();
        TextureToDelete &texToDelete = texturesToDelete.emplace_back();
        texToDelete.texture = std::move(texture.resource);
        texToDelete.stagingBuffers.swap(texture.stagingBuffers);
        texToDelete.srvIndex = texture.srvDesc.baseIndex;
        texToDelete.rtvIndex = texture.rtvDesc.baseIndex;
        texToDelete.targetFenceValue = currFrame.fenceValue + kFrameCount;
        texToDelete.isRenderTarget = texture.spec.access == TextureAccess::RenderTarget;
    }

    void DeletePendingTextures(bool force) {
        if (texturesToDelete.empty()) {
            return;
        }

        const FrameContext &currFrame = GetCurrentFrameContext();
        const UINT64 fenceValue = currFrame.fenceValue;
        while (!texturesToDelete.empty() && (force || texturesToDelete.front().targetFenceValue <= fenceValue)) {
            texturesToDelete.front().Destroy(resourceHeapAlloc, rtvHeapAlloc);
            texturesToDelete.pop_front();
        }
    }

    bool IsTextureValid(TextureID id) {
        auto it = textures.find(id);
        if (it == textures.end()) {
            return false;
        }
        auto &texture = it->second;
        return texture.resource.IsValid();
    }

    TextureInstance *GetTexture(TextureID id) {
        auto it = textures.find(id);
        if (it == textures.end()) {
            return nullptr;
        }
        return &it->second;
    }

    util::VoidResult<> UpdateTexture(TextureID id, const IRect *rect,
                                     const std::function<void(void *data, size_t pitch)> &fnUpdate) {
        auto it = textures.find(id);
        if (it == textures.end()) {
            return util::ErrorMessage{"Invalid texture ID"};
        }

        TextureInstance &texture = it->second;
        if (texture.isReserved) {
            return util::ErrorMessage{"Cannot update reserved texture"};
        }

        D3D12Resource &stagingBuffer = texture.stagingBuffers[frameIndex];
        void *stagingBufferData = texture.stagingBuffersData[frameIndex];

        // Copy data to staging buffer
        fnUpdate(stagingBufferData, texture.rowPitch);

        if (FAILED(fenceOps.Signal(cmdQueue, fenceValueOps))) {
            return util::ErrorMessage{"Failed to signal fence before executing operations"};
        }
        fenceOps.Wait(INFINITE, fenceValueOps);
        ++fenceValueOps;

        if (HRESULT hr = cmdAllocOps->Reset(); FAILED(hr)) {
            return util::ErrorMessage{fmt::format("Failed to reset command allocator, error code {:X}", (uint32)hr)};
        }
        if (HRESULT hr = cmdListOps->Reset(cmdAllocOps.GetPointer(), nullptr); FAILED(hr)) {
            return util::ErrorMessage{
                fmt::format("Failed to reset operations command list, error code {:X}", (uint32)hr)};
        }
        auto *enhCmdList = GetCommandListForEnhancedBarriers(cmdListOps);

        // Indicate that data will be copied to the texture
        if (enhCmdList != nullptr) {
            D3D12_TEXTURE_BARRIER barrier{
                .SyncBefore = D3D12_BARRIER_SYNC_PIXEL_SHADING,
                .SyncAfter = D3D12_BARRIER_SYNC_COPY,
                .AccessBefore = D3D12_BARRIER_ACCESS_COMMON,
                .AccessAfter = D3D12_BARRIER_ACCESS_COPY_DEST,
                .LayoutBefore = D3D12_BARRIER_LAYOUT_COMMON,
                .LayoutAfter = D3D12_BARRIER_LAYOUT_COPY_DEST,
                .pResource = texture.resource.GetPointer(),
                .Subresources =
                    {
                        .IndexOrFirstMipLevel = 0,
                        .NumMipLevels = 0,
                        .FirstArraySlice = 0,
                        .NumArraySlices = 0,
                        .FirstPlane = 0,
                        .NumPlanes = 0,
                    },
                .Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE,
            };
            const D3D12_BARRIER_GROUP group{
                .Type = D3D12_BARRIER_TYPE_TEXTURE,
                .NumBarriers = 1,
                .pTextureBarriers = &barrier,
            };
            enhCmdList->Barrier(1, &group);
        } else {
            D3D12_RESOURCE_BARRIER barrier{
                .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
                .Transition =
                    {
                        .pResource = texture.resource.GetPointer(),
                        .Subresource = 0,
                        .StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                        .StateAfter = D3D12_RESOURCE_STATE_COPY_DEST,
                    },
            };
            cmdListOps->ResourceBarrier(1, &barrier);
        }

        // Copy buffer to texture
        const D3D12_TEXTURE_COPY_LOCATION src{
            .pResource = stagingBuffer.GetPointer(),
            .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
            .PlacedFootprint = texture.footprint,
        };
        const D3D12_TEXTURE_COPY_LOCATION dst{
            .pResource = texture.resource.GetPointer(),
            .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
            .SubresourceIndex = 0,
        };
        cmdListOps->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

        // Transition texture back to pixel shading usage
        if (enhCmdList != nullptr) {
            D3D12_TEXTURE_BARRIER barrier{
                .SyncBefore = D3D12_BARRIER_SYNC_COPY,
                .SyncAfter = D3D12_BARRIER_SYNC_PIXEL_SHADING,
                .AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST,
                .AccessAfter = D3D12_BARRIER_ACCESS_COMMON,
                .LayoutBefore = D3D12_BARRIER_LAYOUT_COPY_DEST,
                .LayoutAfter = D3D12_BARRIER_LAYOUT_COMMON,
                .pResource = texture.resource.GetPointer(),
                .Subresources =
                    {
                        .IndexOrFirstMipLevel = 0,
                        .NumMipLevels = 0,
                        .FirstArraySlice = 0,
                        .NumArraySlices = 0,
                        .FirstPlane = 0,
                        .NumPlanes = 0,
                    },
                .Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE,
            };
            const D3D12_BARRIER_GROUP group{
                .Type = D3D12_BARRIER_TYPE_TEXTURE,
                .NumBarriers = 1,
                .pTextureBarriers = &barrier,
            };
            enhCmdList->Barrier(1, &group);
        } else {
            D3D12_RESOURCE_BARRIER barrier{
                .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
                .Transition =
                    {
                        .pResource = texture.resource.GetPointer(),
                        .Subresource = 0,
                        .StateBefore = D3D12_RESOURCE_STATE_COPY_DEST,
                        .StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                    },
            };
            cmdListOps->ResourceBarrier(1, &barrier);
        }

        cmdListOps->Close();
        cmdQueue->ExecuteCommandLists(1, cmdListOps.GetAddressOfBase());

        return {};
    }

    util::PointerResult<RenderTargetPipeline> GetRenderTargetPipeline(const TextureInstance &texture) {
        const DXGI_FORMAT dxgiFormat = ToD3D12Value(texture.spec.format);
        auto it = renderTargetPipelines.find(dxgiFormat);
        if (it != renderTargetPipelines.end()) {
            return &it->second;
        }

        const char *name = DXGIFormatName(dxgiFormat);

        RenderTargetPipeline &pipeline = renderTargetPipelines[dxgiFormat];

        auto rootSigBuilder = pipeline.rootSignature.Builder();
        rootSigBuilder.Flags(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
        rootSigBuilder.AddDescriptorTable(D3D12_SHADER_VISIBILITY_PIXEL).AddSRVs(1, 0);
        rootSigBuilder.AddDescriptorTable(D3D12_SHADER_VISIBILITY_PIXEL).AddSamplers(1, 0);
        rootSigBuilder.Add32BitConstants(0, sizeof(DrawTextureConstants) / sizeof(uint32));
        if (HRESULT hr = rootSigBuilder.Build(device); FAILED(hr)) {
            return util::ErrorMessage{fmt::format(
                "Failed to create texture root signature for render target [{}], error code {:X}", name, (uint32)hr)};
        }
        pipeline.rootSignature->SetName(
            util::StringToWString(fmt::format("[Ymir-GCtx] Root signature for render target [{}]", name)).c_str());

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.InputLayout = {inputElementDescs, std::size(inputElementDescs)};
        psoDesc.pRootSignature = pipeline.rootSignature.GetPointer();
        psoDesc.VS = {.pShaderBytecode = vertexShader.bytecode.data(), .BytecodeLength = vertexShader.bytecode.size()};
        psoDesc.PS = {.pShaderBytecode = pixelShader.bytecode.data(), .BytecodeLength = pixelShader.bytecode.size()};
        psoDesc.RasterizerState = {
            .FillMode = D3D12_FILL_MODE_SOLID,
            .CullMode = D3D12_CULL_MODE_BACK,
            .FrontCounterClockwise = FALSE,
            .DepthBias = D3D12_DEFAULT_DEPTH_BIAS,
            .DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP,
            .SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS,
            .DepthClipEnable = TRUE,
            .MultisampleEnable = FALSE,
            .AntialiasedLineEnable = FALSE,
            .ForcedSampleCount = 0,
            .ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF,
        };
        psoDesc.BlendState = {
            .AlphaToCoverageEnable = FALSE,
            .IndependentBlendEnable = FALSE,
            .RenderTarget = {{
                .BlendEnable = FALSE,
                .LogicOpEnable = FALSE,
                .SrcBlend = D3D12_BLEND_ONE,
                .DestBlend = D3D12_BLEND_ZERO,
                .BlendOp = D3D12_BLEND_OP_ADD,
                .SrcBlendAlpha = D3D12_BLEND_ONE,
                .DestBlendAlpha = D3D12_BLEND_ZERO,
                .BlendOpAlpha = D3D12_BLEND_OP_ADD,
                .LogicOp = D3D12_LOGIC_OP_NOOP,
                .RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL,
            }},
        };
        psoDesc.DepthStencilState.DepthEnable = FALSE;
        psoDesc.DepthStencilState.StencilEnable = FALSE;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = dxgiFormat;
        psoDesc.SampleDesc.Count = 1;
        if (HRESULT hr = pipeline.pipelineState.CreateGraphics(device, psoDesc); FAILED(hr)) {
            return util::ErrorMessage{
                fmt::format("Failed to create graphics pipeline state object for render target [{}], error code {:X}",
                            name, (uint32)hr)};
        }
        pipeline.pipelineState->SetName(
            util::StringToWString(fmt::format("[Ymir-GCtx] Graphics pipeline for render target [{}]", name)).c_str());

        return &pipeline;
    }

    util::VoidResult<> RenderToTexture(TextureID src, TextureID dst, const FRect &srcRect, const FRect &dstRect) {
        TextureInstance *dstTexture = GetTexture(dst);
        if (dstTexture == nullptr) {
            return util::ErrorMessage{"Invalid destination texture"};
        }
        if (dstTexture->spec.access != TextureAccess::RenderTarget) {
            return util::ErrorMessage{"Destination texture is not a valid render target"};
        }
        auto psoResult = GetRenderTargetPipeline(*dstTexture);
        if (!psoResult) {
            return util::ErrorMessage{fmt::format("Could not get render target PSO: {}", psoResult.Error().message)};
        }
        RenderTargetPipeline *pipeline = psoResult.Value();

        auto *enhCmdList = GetCommandListForEnhancedBarriers(cmdListFrame);

        // Transition texture usage to render target
        if (enhCmdList != nullptr) {
            D3D12_TEXTURE_BARRIER barrier{
                .SyncBefore = D3D12_BARRIER_SYNC_PIXEL_SHADING,
                .SyncAfter = D3D12_BARRIER_SYNC_RENDER_TARGET,
                .AccessBefore = D3D12_BARRIER_ACCESS_COMMON,
                .AccessAfter = D3D12_BARRIER_ACCESS_RENDER_TARGET,
                .LayoutBefore = D3D12_BARRIER_LAYOUT_COMMON,
                .LayoutAfter = D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                .pResource = dstTexture->resource.GetPointer(),
                .Subresources =
                    {
                        .IndexOrFirstMipLevel = 0,
                        .NumMipLevels = 0,
                        .FirstArraySlice = 0,
                        .NumArraySlices = 0,
                        .FirstPlane = 0,
                        .NumPlanes = 0,
                    },
                .Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE,
            };
            const D3D12_BARRIER_GROUP group{
                .Type = D3D12_BARRIER_TYPE_TEXTURE,
                .NumBarriers = 1,
                .pTextureBarriers = &barrier,
            };
            enhCmdList->Barrier(1, &group);
        } else {
            D3D12_RESOURCE_BARRIER barrier{
                .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
                .Transition =
                    {
                        .pResource = dstTexture->resource.GetPointer(),
                        .Subresource = 0,
                        .StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                        .StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET,
                    },
            };
            cmdListFrame->ResourceBarrier(1, &barrier);
        }

        // Change render target to destination texture
        D3D12_VIEWPORT rtViewport{};
        rtViewport.Width = dstTexture->spec.width;
        rtViewport.Height = dstTexture->spec.height;
        D3D12_RECT rtScissorRect{};
        rtScissorRect.right = dstTexture->spec.width;
        rtScissorRect.bottom = dstTexture->spec.height;
        cmdListFrame->SetPipelineState(pipeline->pipelineState.GetPointer());
        cmdListFrame->SetGraphicsRootSignature(pipeline->rootSignature.GetPointer());
        cmdListFrame->OMSetRenderTargets(1, &dstTexture->rtvDesc.cpuHandle, FALSE, nullptr);
        cmdListFrame->RSSetViewports(1, &rtViewport);
        cmdListFrame->RSSetScissorRects(1, &rtScissorRect);
        drawTextureConstants.renderTargetSize.x = dstTexture->spec.width;
        drawTextureConstants.renderTargetSize.y = dstTexture->spec.height;

        auto drawResult = DrawTextureRotated(src, srcRect, dstRect, 0, nullptr);

        // Restore swap chain render target
        cmdListFrame->SetPipelineState(pipelineStateFrame.GetPointer());
        cmdListFrame->SetGraphicsRootSignature(rootSignatureFrame.GetPointer());
        cmdListFrame->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
        drawTextureConstants.renderTargetSize.x = viewport.Width;
        drawTextureConstants.renderTargetSize.y = viewport.Height;
        cmdListFrame->RSSetViewports(1, &viewport);
        cmdListFrame->RSSetScissorRects(1, &scissorRect);

        // Transition texture usage back to pixel shading
        if (enhCmdList != nullptr) {
            D3D12_TEXTURE_BARRIER barrier{
                .SyncBefore = D3D12_BARRIER_SYNC_RENDER_TARGET,
                .SyncAfter = D3D12_BARRIER_SYNC_PIXEL_SHADING,
                .AccessBefore = D3D12_BARRIER_ACCESS_RENDER_TARGET,
                .AccessAfter = D3D12_BARRIER_ACCESS_COMMON,
                .LayoutBefore = D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                .LayoutAfter = D3D12_BARRIER_LAYOUT_COMMON,
                .pResource = dstTexture->resource.GetPointer(),
                .Subresources =
                    {
                        .IndexOrFirstMipLevel = 0,
                        .NumMipLevels = 0,
                        .FirstArraySlice = 0,
                        .NumArraySlices = 0,
                        .FirstPlane = 0,
                        .NumPlanes = 0,
                    },
                .Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE,
            };
            const D3D12_BARRIER_GROUP group{
                .Type = D3D12_BARRIER_TYPE_TEXTURE,
                .NumBarriers = 1,
                .pTextureBarriers = &barrier,
            };
            enhCmdList->Barrier(1, &group);
        } else {
            D3D12_RESOURCE_BARRIER barrier{
                .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
                .Transition =
                    {
                        .pResource = dstTexture->resource.GetPointer(),
                        .Subresource = 0,
                        .StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET,
                        .StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                    },
            };
            cmdListFrame->ResourceBarrier(1, &barrier);
        }

        if (!drawResult) {
            return util::ErrorMessage{fmt::format("Failed to draw texture: {}", drawResult.Error().message)};
        }

        return {};
    }

    util::VoidResult<> DrawTextureRotated(TextureID id, const FRect &srcRect, const FRect &dstRect, double rotAngle,
                                          const FPoint2D *rotPivot) {
        TextureInstance *instance = GetTexture(id);
        if (instance == nullptr) {
            return util::ErrorMessage{"Invalid texture"};
        }

        // Select sampler based on texture filtering mode
        const DescriptorRange &smpDesc = [&] {
            switch (instance->spec.filterMode) {
            case TextureFilterMode::Nearest: return smpNearest;
            case TextureFilterMode::Linear: return smpLinear;
            }
            return smpLinear;
        }();

        // Update constants with source UVs, destination area (in pixels), rotation angle and pivot point
        drawTextureConstants.srcRect = {
            srcRect.x / instance->spec.width,
            srcRect.y / instance->spec.height,
            srcRect.w / instance->spec.width,
            srcRect.h / instance->spec.height,
        };
        drawTextureConstants.dstRect = {
            dstRect.x,
            dstRect.y,
            dstRect.w,
            dstRect.h,
        };
        if (rotPivot == nullptr) {
            drawTextureConstants.rotPivot.x = dstRect.w * 0.5f;
            drawTextureConstants.rotPivot.y = dstRect.h * 0.5f;
        } else {
            drawTextureConstants.rotPivot.x = rotPivot->x;
            drawTextureConstants.rotPivot.y = rotPivot->y;
        }
        drawTextureConstants.rotAngle = rotAngle;

        // Draw rectangle
        cmdListFrame->SetGraphicsRootDescriptorTable(0, instance->srvDesc.gpuHandle);
        cmdListFrame->SetGraphicsRootDescriptorTable(1, smpDesc.gpuHandle);
        cmdListFrame->SetGraphicsRoot32BitConstants(2, sizeof(drawTextureConstants) / sizeof(uint32),
                                                    &drawTextureConstants, 0);
        cmdListFrame->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        cmdListFrame->IASetVertexBuffers(0, 1, &vertexBufferView);
        cmdListFrame->DrawInstanced(4, 1, 0, 0);

        return {};
    }

    ID3D12GraphicsCommandList7 *GetCommandListForEnhancedBarriers(D3D12GraphicsCommandList &cmdList) const {
        if (!features.enhancedBarriers) {
            return nullptr;
        }
        return cmdList.As7();
    }

    FrameContext &GetCurrentFrameContext() {
        return frames[frameIndex];
    }

    const FrameContext &GetCurrentFrameContext() const {
        return frames[frameIndex];
    }

    size_t GetDisplayFrameIndexForCompute() {
        const DisplayFrameContext &currFrameCtx = displayFrames[computeDisplayFrame];
        if (currFrameCtx.computeFence.load(std::memory_order_acquire) == nullptr) {
            // Never used by compute - context just initialized
            return computeDisplayFrame;
        }

        // Go to next frame, skipping all frames taken by graphics right now.
        // Reuse current frame if they're all taken. (This should never happen, but we implement it as a safeguard.)
        size_t nextFrameIndex = (computeDisplayFrame + 1) % displayFrames.size();
        while (true) {
            if (nextFrameIndex == computeDisplayFrame) {
                // Looped back to current frame
                break;
            }
            if (fenceFrame.GetCompletedValue() >=
                displayFrames[nextFrameIndex].graphicsFenceValue.load(std::memory_order_acquire)) {
                // Not in use by graphics
                break;
            }
            nextFrameIndex = (nextFrameIndex + 1) % displayFrames.size();
        }
        computeDisplayFrame = nextFrameIndex;
        return computeDisplayFrame;
    }

    ID3D12Resource *GetNextDisplayOutputTexture(ID3D12Fence *fence, uint64 fenceValue, uint32 width, uint32 height) {
        const size_t nextIndex = GetDisplayFrameIndexForCompute();
        DisplayFrameContext &frameCtx = displayFrames[nextIndex];

        TextureInstance *texture = GetTexture(frameCtx.textureID);
        if (texture == nullptr) {
            return nullptr; // Shouldn't happen
        }

        frameCtx.computeFence.store(fence, std::memory_order_release);
        frameCtx.computeFenceValue.store(fenceValue, std::memory_order_release);
        frameCtx.width = width;
        frameCtx.height = height;
        return texture->resource.GetPointer();
    }

    size_t GetDisplayFrameIndexForGraphics() {
        UINT64 maxComputeFenceValue;
        if (graphicsDisplayFrame == kInvalidFrameIndex) {
            maxComputeFenceValue = 0;
        } else {
            maxComputeFenceValue =
                displayFrames[graphicsDisplayFrame].computeFenceValue.load(std::memory_order_acquire);
        }

        // Scan the whole array for the latest frame completed in the compute context that's ahead of the last frame
        // returned in the previous iteration
        for (size_t i = 0; i < displayFrames.size(); ++i) {
            const DisplayFrameContext &frameCtx = displayFrames[i];
            if (frameCtx.computeFence.load(std::memory_order_acquire) == nullptr) {
                // This frame was never assigned to compute, skip it
                continue;
            }

            // Check if the frame is newer
            const UINT64 fenceValue = frameCtx.computeFenceValue.load(std::memory_order_acquire);
            if (fenceValue > maxComputeFenceValue) {
                // Update frame index
                graphicsDisplayFrame = i;
                maxComputeFenceValue = fenceValue;
            }
        }

        // Compute never rendered a frame
        if (maxComputeFenceValue == 0) {
            return displayFrames.size();
        }

        return graphicsDisplayFrame;
    }

    std::optional<DisplayTextureSpec> AcquireCurrentDisplayOutputTexture() {
        // TODO: return changed flag to allow frontend to redraw the frame only if needed

        // Get latest completed frame index
        const size_t frameIndex = GetDisplayFrameIndexForGraphics();
        if (frameIndex >= displayFrames.size()) {
            // No frames were rendered; bail out
            return std::nullopt;
        }
        DisplayFrameContext &frameCtx = displayFrames[frameIndex];
        frameCtx.graphicsFenceValue = GetCurrentFrameContext().fenceValue;

        TextureInstance *texture = GetTexture(frameCtx.textureID);
        if (texture == nullptr) {
            return std::nullopt; // Shouldn't happen
        }

        const UINT64 computeFenceValue = frameCtx.computeFenceValue.load(std::memory_order_acquire);
        const bool changed = computeFenceValue != frameCtx.lastComputeFenceValue;
        frameCtx.lastComputeFenceValue = computeFenceValue;

        // Wait for compute fence
        assert(frameCtx.computeFence != nullptr);
        cmdQueue->Wait(frameCtx.computeFence, frameCtx.computeFenceValue);

        // Download texture to readback buffer if changed
        // TODO: mark dirty, copy on screen requests only
        if (changed) {
            // Transition texture to copy source
            if (auto *enhCmdList = GetCommandListForEnhancedBarriers(cmdListFrame)) {
                D3D12_TEXTURE_BARRIER barrier{
                    .SyncBefore = D3D12_BARRIER_SYNC_COPY,
                    .SyncAfter = D3D12_BARRIER_SYNC_COPY,
                    .AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST,
                    .AccessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE,
                    .LayoutBefore = D3D12_BARRIER_LAYOUT_COPY_DEST,
                    .LayoutAfter = D3D12_BARRIER_LAYOUT_COPY_SOURCE,
                    .pResource = texture->resource.GetPointer(),
                    .Subresources =
                        {
                            .IndexOrFirstMipLevel = 0,
                            .NumMipLevels = 0,
                            .FirstArraySlice = 0,
                            .NumArraySlices = 0,
                            .FirstPlane = 0,
                            .NumPlanes = 0,
                        },
                    .Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE,
                };
                const D3D12_BARRIER_GROUP group{
                    .Type = D3D12_BARRIER_TYPE_TEXTURE,
                    .NumBarriers = 1,
                    .pTextureBarriers = &barrier,
                };
                enhCmdList->Barrier(1, &group);
            } else {
                D3D12_RESOURCE_BARRIER barrier{
                    .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                    .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
                    .Transition =
                        {
                            .pResource = texture->resource.GetPointer(),
                            .Subresource = 0,
                            .StateBefore = D3D12_RESOURCE_STATE_COPY_DEST,
                            .StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE,
                        },
                };
                cmdListFrame->ResourceBarrier(1, &barrier);
            }

            // Copy to readback texture
            D3D12_TEXTURE_COPY_LOCATION srcLocation{
                .pResource = texture->resource.GetPointer(),
                .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
                .SubresourceIndex = 0,
            };
            D3D12_TEXTURE_COPY_LOCATION dstLocation{
                .pResource = frameCtx.readbackTexture.GetPointer(),
                .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
                .PlacedFootprint = texture->footprint,
            };
            cmdListFrame->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);
        }

        // Transition texture to pixel shading
        if (auto *enhCmdList = GetCommandListForEnhancedBarriers(cmdListFrame)) {
            D3D12_TEXTURE_BARRIER barrier{
                .SyncBefore = D3D12_BARRIER_SYNC_COPY,
                .SyncAfter = D3D12_BARRIER_SYNC_PIXEL_SHADING,
                .AccessBefore = changed ? D3D12_BARRIER_ACCESS_COPY_SOURCE : D3D12_BARRIER_ACCESS_COPY_DEST,
                .AccessAfter = D3D12_BARRIER_ACCESS_COMMON,
                .LayoutBefore = changed ? D3D12_BARRIER_LAYOUT_COPY_SOURCE : D3D12_BARRIER_LAYOUT_COPY_DEST,
                .LayoutAfter = D3D12_BARRIER_LAYOUT_COMMON,
                .pResource = texture->resource.GetPointer(),
                .Subresources =
                    {
                        .IndexOrFirstMipLevel = 0,
                        .NumMipLevels = 0,
                        .FirstArraySlice = 0,
                        .NumArraySlices = 0,
                        .FirstPlane = 0,
                        .NumPlanes = 0,
                    },
                .Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE,
            };
            const D3D12_BARRIER_GROUP group{
                .Type = D3D12_BARRIER_TYPE_TEXTURE,
                .NumBarriers = 1,
                .pTextureBarriers = &barrier,
            };
            enhCmdList->Barrier(1, &group);
        } else {
            D3D12_RESOURCE_BARRIER barrier{
                .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
                .Transition =
                    {
                        .pResource = texture->resource.GetPointer(),
                        .Subresource = 0,
                        .StateBefore = changed ? D3D12_RESOURCE_STATE_COPY_SOURCE : D3D12_RESOURCE_STATE_COPY_DEST,
                        .StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                    },
            };
            cmdListFrame->ResourceBarrier(1, &barrier);
        }

        return DisplayTextureSpec{
            .id = frameCtx.textureID,
            .width = frameCtx.width,
            .height = frameCtx.height,
        };
    }

    void ReleaseCurrentDisplayOutputTexture() {
        DisplayFrameContext &frameCtx = displayFrames[graphicsDisplayFrame];

        TextureInstance *texture = GetTexture(frameCtx.textureID);
        if (texture == nullptr) {
            return; // Shouldn't happen
        }

        // TODO: emit only if needed

        // Transition texture to copy destination
        if (auto *enhCmdList = GetCommandListForEnhancedBarriers(cmdListFrame)) {
            D3D12_TEXTURE_BARRIER barrier{
                .SyncBefore = D3D12_BARRIER_SYNC_PIXEL_SHADING,
                .SyncAfter = D3D12_BARRIER_SYNC_COPY,
                .AccessBefore = D3D12_BARRIER_ACCESS_COMMON,
                .AccessAfter = D3D12_BARRIER_ACCESS_COPY_DEST,
                .LayoutBefore = D3D12_BARRIER_LAYOUT_COMMON,
                .LayoutAfter = D3D12_BARRIER_LAYOUT_COPY_DEST,
                .pResource = texture->resource.GetPointer(),
                .Subresources =
                    {
                        .IndexOrFirstMipLevel = 0,
                        .NumMipLevels = 0,
                        .FirstArraySlice = 0,
                        .NumArraySlices = 0,
                        .FirstPlane = 0,
                        .NumPlanes = 0,
                    },
                .Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE,
            };
            const D3D12_BARRIER_GROUP group{
                .Type = D3D12_BARRIER_TYPE_TEXTURE,
                .NumBarriers = 1,
                .pTextureBarriers = &barrier,
            };
            enhCmdList->Barrier(1, &group);
        } else {
            D3D12_RESOURCE_BARRIER barrier{
                .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
                .Transition =
                    {
                        .pResource = texture->resource.GetPointer(),
                        .Subresource = 0,
                        .StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                        .StateAfter = D3D12_RESOURCE_STATE_COPY_DEST,
                    },
            };
            cmdListFrame->ResourceBarrier(1, &barrier);
        }
    }

    void ResetDisplayOutputTextures() {
        WaitForGPU();

        for (DisplayFrameContext &frameCtx : displayFrames) {
            frameCtx.computeFence.store(nullptr, std::memory_order_release);
            frameCtx.computeFenceValue.store(0, std::memory_order_release);
            frameCtx.graphicsFenceValue.store(0, std::memory_order_release);
        }
        computeDisplayFrame = 0;
        graphicsDisplayFrame = kInvalidFrameIndex;
    }

    util::ValueResult<size_t> DownloadDisplayOutputTexture(void *buffer, size_t size) {
        if (graphicsDisplayFrame == kInvalidFrameIndex) {
            // No frames were copied; nothing to download.
            // Either we're using the software renderer, or the hardware renderer never rendered a frame.
            return 0;
        }

        const DisplayFrameContext &frameCtx = displayFrames[graphicsDisplayFrame];

        // Wait until the latest frame for which we have a display is fully processed
        fenceFrame.Wait(INFINITE, frameCtx.graphicsFenceValue);

        // Copy buffer
        const size_t copySize = std::min(size, frameCtx.readbackTextureSize);
        memcpy(buffer, frameCtx.readbackTexturePtr, copySize);
        return copySize;
    }
};

// -----------------------------------------------------------------------------

Direct3D12GraphicsContext::Direct3D12GraphicsContext(const Direct3D12GraphicsContextSpec &spec)
    : IGraphicsContext(kBackend)
    , m_impl(std::make_unique<Impl>(spec)) {}

Direct3D12GraphicsContext::~Direct3D12GraphicsContext() {
    Shutdown();
}

util::ObjectResult<Direct3D12GraphicsContext>
Direct3D12GraphicsContext::Create(const Direct3D12GraphicsContextSpec &spec) {
    auto context = std::make_unique<Direct3D12GraphicsContext>(spec);
    auto result = context->Initialize();
    if (!result) {
        return result.Error();
    }
    return std::move(context);
}

util::VoidResult<> Direct3D12GraphicsContext::Initialize() {
    return m_impl->Init();
}

void Direct3D12GraphicsContext::Shutdown() {
    if (m_impl->IsInitialized()) {
        m_impl->WaitForGPU();
        ImGuiShutdown();
        m_impl->Shutdown();
    }
}

bool Direct3D12GraphicsContext::IsInitialized() const {
    return m_impl->IsInitialized();
}

util::VoidResult<> Direct3D12GraphicsContext::ResizeFramebuffer(uint32 width, uint32 height) {
    return m_impl->ResizeFramebuffer(width, height);
}

void Direct3D12GraphicsContext::ClearScreen(gfx::ColorRGBA color) {
    const float clearColor[] = {color.r, color.g, color.b, color.a};
    m_impl->cmdListFrame->ClearRenderTargetView(m_impl->rtvHandle, clearColor, 0, nullptr);
}

bool Direct3D12GraphicsContext::ImGuiInit() {
    if (m_imguiInitialized) {
        return true;
    }

    ImGui_ImplDX12_InitInfo initInfo{};
    initInfo.Device = m_impl->device.GetPointer();
    initInfo.CommandQueue = m_impl->cmdQueue.GetPointer();
    initInfo.NumFramesInFlight = Impl::kFrameCount;
    initInfo.RTVFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
    initInfo.UserData = m_impl.get();
    initInfo.SrvDescriptorHeap = m_impl->resourceHeap.GetPointer();
    initInfo.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo *info, D3D12_CPU_DESCRIPTOR_HANDLE *out_cpu_handle,
                                       D3D12_GPU_DESCRIPTOR_HANDLE *out_gpu_handle) {
        auto &impl = *static_cast<Impl *>(info->UserData);
        DescriptorRange range{};
        impl.resourceHeapAlloc.Allocate(range);
        *out_cpu_handle = range.cpuHandle;
        *out_gpu_handle = range.gpuHandle;
    };
    initInfo.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo *info, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle,
                                      D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle) {
        auto &impl = *static_cast<Impl *>(info->UserData);
        UINT index = impl.resourceHeapAlloc.GetIndex(cpu_handle);
        impl.resourceHeapAlloc.Free(index);
    };
    m_imguiInitialized =                                  //
        ImGui_ImplSDL3_InitForD3D(m_impl->spec.window) && //
        ImGui_ImplDX12_Init(&initInfo);

    return m_imguiInitialized;
}

void Direct3D12GraphicsContext::ImGuiShutdown() {
    if (m_imguiInitialized) {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        m_imguiInitialized = false;
    }
}

void Direct3D12GraphicsContext::ImGuiNewFrame() {
    if (m_imguiInitialized) {
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplSDL3_NewFrame();
    }
}

void Direct3D12GraphicsContext::ImGuiRenderFrame() {
    if (m_imguiInitialized) {
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_impl->cmdListFrame.GetPointer());
    }
}

util::ValueResult<TextureID> Direct3D12GraphicsContext::CreateTexture(const Texture2DSpec &spec) {
    auto result = m_impl->CreateTexture(spec, false);
    if (!result) {
        return result.Error();
    }

    const TextureID id = m_impl->texIDMgr.GetNextTextureID();
    m_impl->textures[id] = std::move(result.Value());

    return id;
}

void Direct3D12GraphicsContext::DestroyTexture(TextureID id) {
    m_impl->DestroyTexture(id);
}

bool Direct3D12GraphicsContext::IsTextureValid(TextureID id) const {
    return m_impl->IsTextureValid(id);
}

ImTextureID Direct3D12GraphicsContext::GetImGuiTextureID(TextureID id) const {
    // ImTextureIDs for D3D12 are the D3D12_GPU_DESCRIPTOR_HANDLE for the texture's SRV
    Impl::TextureInstance *instance = m_impl->GetTexture(id);
    return instance ? instance->srvDesc.gpuHandle.ptr : 0;
}

util::VoidResult<> Direct3D12GraphicsContext::ResizeTexture(TextureID id, uint32 width, uint32 height) {
    return m_impl->ResizeTexture(id, width, height);
}

util::VoidResult<>
Direct3D12GraphicsContext::UpdateTexture(TextureID id, const IRect *rect,
                                         const std::function<void(void *data, size_t pitch)> &fnUpdate) {
    return m_impl->UpdateTexture(id, rect, fnUpdate);
}

util::VoidResult<> Direct3D12GraphicsContext::RenderToTexture(TextureID src, TextureID dst, const FRect &srcRect,
                                                              const FRect &dstRect) {
    return m_impl->RenderToTexture(src, dst, srcRect, dstRect);
}

util::VoidResult<> Direct3D12GraphicsContext::DrawTextureRotated(TextureID id, const FRect &srcRect,
                                                                 const FRect &dstRect, double rotAngle,
                                                                 const FPoint2D *rotPivot) {
    return m_impl->DrawTextureRotated(id, srcRect, dstRect, rotAngle, rotPivot);
}

std::optional<DisplayTextureSpec> Direct3D12GraphicsContext::AcquireCurrentDisplayOutputTexture() {
    return m_impl->AcquireCurrentDisplayOutputTexture();
}

void Direct3D12GraphicsContext::ReleaseCurrentDisplayOutputTexture() {
    m_impl->ReleaseCurrentDisplayOutputTexture();
}

void Direct3D12GraphicsContext::ResetDisplayOutputTextures() {
    m_impl->ResetDisplayOutputTextures();
}

util::ValueResult<size_t> Direct3D12GraphicsContext::DownloadDisplayOutputTexture(void *buffer, size_t size) {
    return m_impl->DownloadDisplayOutputTexture(buffer, size);
}

util::VoidResult<> Direct3D12GraphicsContext::SetPresentMode(PresentMode mode) {
    m_impl->presentMode = mode;
    return {};
}

util::ValueResult<PresentResult> Direct3D12GraphicsContext::Present() {
    return m_impl->Present();
}

ID3D12Device *Direct3D12GraphicsContext::GetDevice() const {
    return m_impl->device.GetPointer();
}

ID3D12Resource *Direct3D12GraphicsContext::GetNextDisplayOutputTexture(ID3D12Fence *fence, uint64 fenceValue,
                                                                       uint32 width, uint32 height) {
    return m_impl->GetNextDisplayOutputTexture(fence, fenceValue, width, height);
}

} // namespace app::gfx
