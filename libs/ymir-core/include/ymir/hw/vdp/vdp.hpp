#pragma once

/**
@file
@brief VDP1 and VDP2 implementation.
*/

#include "vdp_configs.hpp"
#include "vdp_state.hpp"

#include "vdp_internal_callbacks.hpp"
#if YMIR_PLATFORM_HAS_DIRECT3D
    #include "renderer/vdp_renderer_hw_d3d12_callbacks.hpp"
#endif

#include "vdp_devlog.hpp"

#include <ymir/core/configuration.hpp>
#include <ymir/core/scheduler.hpp>
#include <ymir/sys/bus.hpp>

#include <ymir/savestate/savestate_vdp.hpp>

#include <ymir/hw/smpc/smpc_internal_callbacks.hpp>

#include <ymir/hw/hw_defs.hpp>

#include "renderer/vdp_renderer.hpp"

#include <ymir/util/inline.hpp>
#include <ymir/util/result.hpp>

#include <blockingconcurrentqueue.h>

#include <array>
#include <memory>
#include <span>
#include <utility>

namespace ymir::vdp {

// Contains both VDP1 and VDP2
class VDP {
public:
    VDP(core::Scheduler &scheduler, core::Configuration &config);
    ~VDP();

    void Reset(bool hard);

    void MapCallbacks(CBHBlankStateChange cbHBlankStateChange, CBVBlankStateChange cbVBlankStateChange,
                      CBTriggerEvent cbSpriteDrawEnd, CBTriggerEvent cbOptimizedINTBACKRead,
                      CBTriggerEvent cbSMPCVBlankIN) {
        m_cbHBlankStateChange = cbHBlankStateChange;
        m_cbVBlankStateChange = cbVBlankStateChange;
        m_cbTriggerSpriteDrawEnd = cbSpriteDrawEnd;
        m_cbTriggerOptimizedINTBACKRead = cbOptimizedINTBACKRead;
        m_cbTriggerSMPCVBlankIN = cbSMPCVBlankIN;
    }

    void MapMemory(sys::SH2Bus &bus);

    // TODO: replace with scheduler events
    void Advance(uint64 cycles);

    /// @brief Retrieves the current VDP2 vertical phase.
    /// @return the current VDP2 vertical phase
    VerticalPhase GetVerticalPhase() const {
        return m_state.VPhase;
    }

    /// @brief Retrieves the current VDP2 horizontal phase.
    /// @return the current VDP2 horizontal phase
    HorizontalPhase GetHorizontalPhase() const {
        return m_state.HPhase;
    }

    // -------------------------------------------------------------------------
    // Configuration

    /// @brief Configures the software renderer frame callback to use whenever the software renderer is in use.
    ///
    /// @param[in] callback the callback to register
    void SetSoftwareRenderCallback(CBSoftwareFrameComplete callback) {
        if (auto *swRenderer = m_renderer->As<VDPRendererType::Software>()) {
            // Apply directly to renderer
            swRenderer->SwCallbacks.FrameComplete = callback;
        } else {
            // Remember for next instantiation.
            m_swRendererCallbacks.FrameComplete = callback;
        }
    }

    /// @brief Retrieves a reference to the current VDP renderer.
    /// @return a reference to the current VDP renderer instance, guaranteed to be valid
    IVDPRenderer &GetRenderer() {
        assert(m_renderer.get() != nullptr); // should always be valid
        return *m_renderer;
    }

    /// @brief Retrieves a reference to the current VDP renderer.
    /// @return a reference to the current VDP renderer instance, guaranteed to be valid
    const IVDPRenderer &GetRenderer() const {
        return const_cast<VDP *>(this)->GetRenderer();
    }

    /// @brief If the current renderer has the specified `VDPRendererType`, returns a pointer to it cast to the
    /// corresponding concrete type. Returns `nullptr` otherwise.
    ///
    /// @tparam type the type to cast as
    /// @return a pointer to the instance cast to the concrete type corresponding to the given `VDPRendererType`, or
    /// `nullptr` if this renderer's type doesn't match.
    template <VDPRendererType type>
    FORCE_INLINE typename detail::VDPRendererType_t<type> *GetRendererAs() {
        return m_renderer->As<type>();
    }

    /// @brief If the current renderer has the specified `VDPRendererType`, returns a pointer to it cast to the
    /// corresponding concrete type. Returns `nullptr` otherwise.
    ///
    /// @tparam type the type to cast as
    /// @return a pointer to the instance cast to the concrete type corresponding to the given `VDPRendererType`, or
    /// `nullptr` if this renderer's type doesn't match.
    template <VDPRendererType type>
    FORCE_INLINE typename detail::VDPRendererType_t<type> *GetRendererAs() const {
        return const_cast<VDP *>(this)->GetRendererAs<type>();
    }

    /// @brief Switches to the null renderer.
    /// @return a pointer to the renderer, or an error message if it failed to instantiate
    util::PointerResult<NullVDPRenderer> UseNullRenderer() {
        return UseRenderer<NullVDPRenderer>();
    }

    /// @brief Switches to the software renderer.
    /// @return a pointer to the renderer, or an error message if it failed to instantiate
    util::PointerResult<SoftwareVDPRenderer> UseSoftwareRenderer() {
        auto result = UseRenderer<SoftwareVDPRenderer>(m_state, vdp2DebugRenderOptions, vdp2AccessPatternsConfig);
        if (!result) {
            return result.Error();
        }
        SoftwareVDPRenderer *renderer = result.Value();
        if (renderer != nullptr) {
            renderer->EnableThreadedVDP1(m_config.swRenderer.threadedVDP1);
            renderer->EnableThreadedVDP2(m_config.swRenderer.threadedVDP2);
            renderer->EnableThreadedDeinterlacer(m_config.swRenderer.threadedDeinterlacer);
        }
        return renderer;
    }

#if YMIR_PLATFORM_HAS_DIRECT3D
    /// @brief Switches to the Direct3D 12 renderer.
    /// @param[in] device a pointer to an `ID3D12Device` instance
    /// @return a pointer to the renderer, or an error message if it failed to instantiate
    util::PointerResult<Direct3D12VDPRenderer> UseDirect3D12Renderer(ID3D12Device *device) {
        return UseRenderer<Direct3D12VDPRenderer>(m_state, vdp2DebugRenderOptions, vdp2AccessPatternsConfig, device);
    }

    /// @brief Configures the Direct3D 12 renderer frame begin callback to use whenever the Direct3D 12 renderer is in
    /// use.
    ///
    /// @param[in] callback the callback to register
    void SetDirect3D12FrameBeginCallback(CBDirect3D12FrameBeginCallback callback) {
        if (auto *hwRenderer = m_renderer->As<VDPRendererType::Direct3D12>()) {
            // Apply directly to renderer
            hwRenderer->HwCallbacks.FrameBegin = callback;
        } else {
            // Remember for next instantiation.
            m_d3d12RendererCallbacks.FrameBegin = callback;
        }
    }

    /// @brief Configures the Direct3D 12 renderer frame request callback to use whenever the Direct3D 12 renderer is in
    /// use.
    ///
    /// @param[in] callback the callback to register
    void SetDirect3D12FrameCopyRequestCallback(CBDirect3D12FrameCopyRequestCallback callback) {
        if (auto *hwRenderer = m_renderer->As<VDPRendererType::Direct3D12>()) {
            // Apply directly to renderer
            hwRenderer->HwCallbacks.FrameCopyRequest = callback;
        } else {
            // Remember for next instantiation.
            m_d3d12RendererCallbacks.FrameCopyRequest = callback;
        }
    }
#endif

    /// @brief Retrieves the enhancements configured for this VDP instance.
    /// @return the current enhancements configuration
    const config::Enhancements &GetEnhancements() const {
        return m_enhancements;
    }

    /// @brief Applies the graphics enhancements configuration to this VDP instance.
    /// @param[in] enhancements the enhancements configuration to apply
    void SetEnhancements(const config::Enhancements &enhancements) {
        m_enhancements = enhancements;
        m_renderer->ConfigureEnhancements(enhancements);
    }

    /// @brief Modifies the graphics enhancements configuration in this VDP instance.
    /// @tparam TFnConfig the configuration function type, which must be an invocable object accepting a
    /// `config::Enhancements &`
    /// @param[in] fnConfig the configuration function, which can modify the given enhancements object
    template <typename TFnConfig>
        requires std::is_invocable_v<TFnConfig, config::Enhancements &>
    void ModifyEnhancements(TFnConfig &&fnConfig) {
        fnConfig(m_enhancements);
        m_renderer->ConfigureEnhancements(m_enhancements);
    }

    // Enable or disable VDP1 drawing stall on VRAM writes.
    void SetStallVDP1OnVRAMWrites(bool enable) {
        m_stallVDP1OnVRAMWrites = enable;
    }

    bool IsStallVDP1OnVRAMWrites() const {
        return m_stallVDP1OnVRAMWrites;
    }

    // Enable or disable VDP1 slowdown.
    void SetSlowVDP1(bool enable) {
        m_VDP1CyclesShift = enable ? 0 : 2;
    }

    bool IsSlowVDP1() const {
        return m_VDP1CyclesShift == 0;
    }

    // Enable or disable VDP1 command processing skip if the first 0x400 bytes are zeros.
    void SetSkipEmptyVDP1CommandTable(bool enable) {
        m_skipEmptyVDP1Table = enable;
    }

    bool IsSkipEmptyVDP1CommandTableVDP1() const {
        return m_skipEmptyVDP1Table;
    }

    // Introduce a small amount of jitter to latched Virtua Gun coordinates.
    //
    // Death Crimson is the only known game that detects perfectly stable shots as reloads. Enabling this option
    // introduces a small amount of jitter to gun coordinates to make the game recognize on-screen shots as such.
    void SetVirtuaGunJitter(bool enable) {
        m_virtuaGunJitter = enable;
    }

    bool IsVirtuaGunJitterEnabled() const {
        return m_virtuaGunJitter;
    }

    // -------------------------------------------------------------------------
    // Memory dumps

    void DumpVDP1VRAM(std::ostream &out) const;
    void DumpVDP2VRAM(std::ostream &out) const;
    void DumpVDP2CRAM(std::ostream &out) const;

    // Dumps draw framebuffer followed by display framebuffer.
    // If transparent mesh rendering is enabled, also dumps the transparent mesh framebuffer in the same order.
    void DumpVDP1Framebuffers(std::ostream &out) const;

    // -------------------------------------------------------------------------
    // VDP1 framebuffer access

    std::span<const uint8> VDP1GetDisplayFramebuffer() const {
        return m_state.mem1.FBRAM[m_state.fbIndex.display];
    }

    std::span<const uint8> VDP1GetDrawFramebuffer() const {
        return m_state.mem1.FBRAM[m_state.fbIndex.draw];
    }

    // -------------------------------------------------------------------------
    // Save states

    void SaveState(savestate::VDPSaveState &state) const;
    [[nodiscard]] bool ValidateState(const savestate::VDPSaveState &state) const;
    void LoadState(const savestate::VDPSaveState &state);

private:
    VDPState m_state;

    core::Configuration &m_config;

    bool m_virtuaGunJitter = false;
    uint32 m_virtuaGunLastJitterX = 0u;
    uint32 m_virtuaGunLastJitterY = 0u;

    std::unique_ptr<IVDPRenderer> m_renderer;

    /// @brief Attempts to replaces the renderer with an instance of the given renderer.
    ///
    /// This method copies over the callbacks from the current to the new renderer, including software renderer
    /// callbacks via `m_swRendererCallbacks`. This is for convenience, as it allows the frontend to configure these
    /// callbacks only once to be reused across all renderers.
    ///
    /// The callbacks are stored directly in the renderers for performance.
    ///
    /// This method also configures the enhancements on the new renderer.
    ///
    /// Returns `nullptr` if the renderer fails to instantiate.
    ///
    /// @tparam T the renderer types
    /// @tparam ...Args argument types for the constructor
    /// @param[in] ...args arguments for the constructor
    /// @return a pointer to the newly created renderer, or an error message it if failed to instantiate
    template <typename T, typename... Args>
        requires std::derived_from<T, IVDPRenderer>
    util::PointerResult<T> UseRenderer(Args &&...args) {
        util::ObjectResult<T> result = T::Create(std::forward<Args>(args)...);
        if (!result) {
            return result.Error();
        }
        std::unique_ptr<T> renderer = result.Value();
        if (!renderer) {
            return util::ErrorMessage{"Not enough memory to instantiate renderer"};
        }

        const config::RendererCallbacks callbacks = m_renderer->Callbacks;
        if (SoftwareVDPRenderer *swRenderer = m_renderer->As<VDPRendererType::Software>()) {
            m_swRendererCallbacks = swRenderer->SwCallbacks;
        }
#if YMIR_PLATFORM_HAS_DIRECT3D
        if (Direct3D12VDPRenderer *hwRenderer = m_renderer->As<VDPRendererType::Direct3D12>()) {
            m_d3d12RendererCallbacks = hwRenderer->HwCallbacks;
        }
#endif

        renderer->Callbacks = callbacks;
        if constexpr (std::is_same_v<T, SoftwareVDPRenderer>) {
            renderer->SwCallbacks = m_swRendererCallbacks;
#if YMIR_PLATFORM_HAS_DIRECT3D
        } else if constexpr (std::is_same_v<T, Direct3D12VDPRenderer>) {
            renderer->HwCallbacks = m_d3d12RendererCallbacks;
#endif
        }
        renderer->ConfigureEnhancements(m_enhancements);
        renderer->VDP2SetResolution(m_HRes, m_VRes, m_exclusiveMonitor);
        renderer->VDP2SetField(m_state.regs2.TVSTAT.ODD);

        T *pRenderer = renderer.get();

        m_renderer = std::move(renderer);

        devlog::info<grp::config>("Switched to {} VDP renderer", pRenderer->GetName());

        return pRenderer;
    }

    CBHBlankStateChange m_cbHBlankStateChange;
    CBVBlankStateChange m_cbVBlankStateChange;
    CBTriggerEvent m_cbTriggerSpriteDrawEnd;
    CBTriggerEvent m_cbTriggerOptimizedINTBACKRead;
    CBTriggerEvent m_cbTriggerSMPCVBlankIN;

    core::Scheduler &m_scheduler;
    core::EventID m_phaseUpdateEvent;

    static void OnPhaseUpdateEvent(core::EventContext &eventContext, void *userContext);

    using VideoStandard = core::config::sys::VideoStandard;
    void SetVideoStandard(VideoStandard videoStandard);

    // -------------------------------------------------------------------------
    // Configuration

    // Current enhancements configuration.
    config::Enhancements m_enhancements;

    /// @brief The current software renderer callbacks configuration.
    SoftwareRendererCallbacks m_swRendererCallbacks;

#if YMIR_PLATFORM_HAS_DIRECT3D
    Direct3D12RendererCallbacks m_d3d12RendererCallbacks;
#endif

    // -------------------------------------------------------------------------
    // VDP1 memory/register access

    template <mem_primitive_16 T>
    T VDP1ReadVRAM(uint32 address) const;

    template <mem_primitive_16 T>
    void VDP1WriteVRAM(uint32 address, T value);

    template <mem_primitive_16 T, bool peek>
    T VDP1ReadFB(uint32 address) const;

    template <mem_primitive_16 T>
    void VDP1WriteFB(uint32 address, T value);

    template <bool peek>
    uint16 VDP1ReadReg(uint32 address) const;

    template <bool poke>
    void VDP1WriteReg(uint32 address, uint16 value);

    // -------------------------------------------------------------------------
    // VDP2 memory/register access

    template <mem_primitive_16 T>
    T VDP2ReadVRAM(uint32 address) const;

    template <mem_primitive_16 T>
    void VDP2WriteVRAM(uint32 address, T value);

    template <mem_primitive_16 T, bool peek>
    T VDP2ReadCRAM(uint32 address) const;

    template <mem_primitive_16 T, bool poke>
    void VDP2WriteCRAM(uint32 address, T value);

    template <bool peek>
    uint16 VDP2ReadReg(uint32 address) const;

    template <bool poke>
    void VDP2WriteReg(uint32 address, uint16 value);

    // -------------------------------------------------------------------------

    FORCE_INLINE uint32 MapCRAMAddress(uint32 address) const {
        return kVDP2CRAMAddressMapping[m_state.regs2.vramControl.colorRAMMode >> 1][address & 0xFFF];
    }

    // -------------------------------------------------------------------------
    // Timings and signals

    // Display resolution (derived from TVMODE)
    uint32 m_HRes; // Horizontal display resolution
    uint32 m_VRes; // Vertical display resolution
    bool m_exclusiveMonitor;

    // Display timings
    std::array<uint32, 4> m_HTimings;                // [phase]
    std::array<std::array<uint32, 6>, 2> m_VTimings; // [even/odd][phase]
    uint32 m_VTimingField;
    uint16 m_VCounterSkip;
    uint64 m_VBlankEraseCyclesPerLine;        // cycles per line for VBlank erase
    std::array<uint64, 2> m_VBlankEraseLines; // [even/odd] lines in VBlank erase

    // Moves to the next phase.
    void UpdatePhase();

    // Returns the number of cycles between the current and the next phase.
    uint64 GetPhaseCycles() const;

    // Updates the display resolution and timings based on TVMODE if it is dirty
    //
    // `verbose` enables dev logging
    template <bool verbose>
    void UpdateResolution();

    void IncrementVCounter();

    // Phase handlers
    void BeginHPhaseActiveDisplay();
    void BeginHPhaseRightBorder();
    void BeginHPhaseSync();
    void BeginHPhaseLeftBorder();

    void BeginVPhaseActiveDisplay();
    void BeginVPhaseBottomBorder();
    void BeginVPhaseBlankingAndSync();
    void BeginVPhaseVCounterSkip();
    void BeginVPhaseTopBorder();
    void BeginVPhaseLastLine();

    // -------------------------------------------------------------------------
    // VDP1 state

    struct VDP1ControlState {
        VDP1ControlState() {
            Reset();
        }

        void Reset() {
            drawing = false;
            doDisplayErase = false;
            doVBlankErase = false;
            spilloverCycles = 0;

            lastJumpAddress = 0xFFFFFFFF;
        }

        // Is the VDP1 currently drawing?
        bool drawing;

        bool doDisplayErase; // Erase scheduled for display period
        bool doVBlankErase;  // Erase scheduled for VBlank period

        // Command processing cycles spilled over from previous executions.
        // Deducted from future executions to compensate for overshooting the target cycle count.
        uint64 spilloverCycles;

        // Infinite loop detection
        uint32 lastJumpAddress; // target address of the last Jump To command
        uint32 loopCount;       // number of jumps taken to the same address
        bool inInfiniteLoop;    // found infinite loop
    } m_VDP1CtlState;

    // Hacky VDP1 command execution timing penalty accrued from external writes to VRAM
    // TODO: count pulled out of thin air
    static constexpr uint64 kVDP1TimingPenaltyPerWrite = 22;
    uint64 m_VDP1TimingPenaltyCycles; // accumulated cycle penalty
    bool m_stallVDP1OnVRAMWrites = false;
    bool m_skipEmptyVDP1Table = false; // skip processing empty tables (first 0x400 bytes in VDP1 VRAM == 0x00)
    uint64 m_VDP1CyclesShift = 2;

    void VDP1SwapFramebuffer();
    void VDP1BeginFrame();
    void VDP1EndFrame();
    uint64 VDP1ProcessCommand();
    uint64 VDP1CalcCommandTiming(uint32 cmdAddress, VDP1Command::Control control);

    // -------------------------------------------------------------------------
    // Callbacks

private:
    void ExternalLatch(uint16 x, uint16 y);

public:
    const smpc::CBExternalLatch CbExternalLatch = util::MakeClassMemberRequiredCallback<&VDP::ExternalLatch>(this);

public:
    // -------------------------------------------------------------------------
    // Debugger

    // Enables or disables a layer.
    // Useful for debugging and troubleshooting.
    void SetLayerEnabled(Layer layer, bool enabled);

    // Detemrines if a layer is forcibly disabled.
    bool IsLayerEnabled(Layer layer) const;

    config::VDP2DebugRender vdp2DebugRenderOptions;
    config::VDP2AccessPatternsConfig vdp2AccessPatternsConfig;

    class Probe {
    public:
        explicit Probe(VDP &vdp);

        [[nodiscard]] Dimensions GetResolution() const;
        [[nodiscard]] InterlaceMode GetInterlaceMode() const;
        [[nodiscard]] uint8 GetSpriteDisplayFB() const;

        [[nodiscard]] const VDP1Regs &GetVDP1Regs() const;
        [[nodiscard]] const VDP2Regs &GetVDP2Regs() const;
        [[nodiscard]] const VDP1State &GetVDP1State() const;
        [[nodiscard]] const VDP2State &GetVDP2State() const;

        [[nodiscard]] uint16 GetLatchedEraseWriteValue() const;
        [[nodiscard]] uint16 GetLatchedEraseX1() const;
        [[nodiscard]] uint16 GetLatchedEraseY1() const;
        [[nodiscard]] uint16 GetLatchedEraseX3() const;
        [[nodiscard]] uint16 GetLatchedEraseY3() const;

        template <mem_primitive T>
        void VDP1WriteVRAM(uint32 address, T value);

        void VDP1WriteReg(uint32 address, uint16 value);

        Color555 VDP2GetCRAMColor555(uint32 index) const;
        Color888 VDP2GetCRAMColor888(uint32 index) const;
        void VDP2SetCRAMColor555(uint32 index, Color555 color);
        void VDP2SetCRAMColor888(uint32 index, Color888 color);
        uint8 VDP2GetCRAMMode() const;

    private:
        VDP &m_vdp;
    };

    Probe &GetProbe() {
        return m_probe;
    }

    const Probe &GetProbe() const {
        return m_probe;
    }

private:
    Probe m_probe{*this};
};

} // namespace ymir::vdp
