#include <ymir/hw/vdp/renderer/vdp_renderer_sw.hpp>

#include <ymir/util/constexpr_for.hpp>
#include <ymir/util/dev_log.hpp>
#include <ymir/util/inline.hpp>
#include <ymir/util/thread_name.hpp>
#include <ymir/util/unreachable.hpp>

#include <algorithm>
#include <cassert>
#include <limits>

#if defined(_M_X64) || defined(__x86_64__)
    #include <immintrin.h>
#elif defined(_M_ARM64) || defined(__aarch64__)
    #include <arm_neon.h>
#endif

namespace ymir::vdp {

namespace grp {

    // -----------------------------------------------------------------------------
    // Dev log groups

    // Hierarchy:
    //
    // swbase
    //   swvdp1
    //     swvdp1_cmd
    //   swvdp2
    //     swvdp2_verbose

    struct swbase {
        static constexpr bool enabled = true;
        static constexpr devlog::Level level = devlog::level::debug;
        static constexpr std::string_view name = "SWRender";
    };

    struct swvdp1 : public swbase {
        static constexpr std::string_view name = "SWRender-VDP1";
    };

    struct swvdp1_cmd : public swvdp1 {
        static constexpr std::string_view name = "SWRender-VDP1-Cmd";
    };

    struct swvdp2 : public swbase {
        static constexpr std::string_view name = "SWRender-VDP2";
    };

    struct swvdp2_verbose : public swvdp2 {
        static constexpr devlog::Level level = devlog::level::debug;
    };

} // namespace grp

SoftwareVDPRenderer::SoftwareVDPRenderer(VDPState &state, const config::VDP2DebugRender &vdp2DebugRenderOptions,
                                         const config::VDP2AccessPatternsConfig &vdp2AccessPatternsConfig)
    : IVDPRenderer(VDPRendererType::Software)
    , m_state(state)
    , m_vdp2DebugRenderOptions(vdp2DebugRenderOptions)
    , m_vdp2AccessPatternsConfig(vdp2AccessPatternsConfig) {

    UpdateFunctionPointers();

    Reset(true);
}

SoftwareVDPRenderer::~SoftwareVDPRenderer() {
    if (m_threadedVDP1Rendering) {
        m_vdp1RenderingContext.EnqueueEvent(VDP1RenderEvent::Shutdown());
        if (m_VDP1RenderThread.joinable()) {
            m_VDP1RenderThread.join();
        }
    }
    if (m_threadedVDP2Rendering) {
        m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::Shutdown());
        if (m_VDP2RenderThread.joinable()) {
            m_VDP2RenderThread.join();
        }
        if (m_VDP2DeinterlaceRenderThread.joinable()) {
            m_VDP2DeinterlaceRenderThread.join();
        }
    }
}

util::ObjectResult<SoftwareVDPRenderer>
SoftwareVDPRenderer::Create(VDPState &state, const config::VDP2DebugRender &vdp2DebugRenderOptions,
                            const config::VDP2AccessPatternsConfig &vdp2AccessPatternsConfig) {
    return std::make_unique<SoftwareVDPRenderer>(state, vdp2DebugRenderOptions, vdp2AccessPatternsConfig);
}

// -----------------------------------------------------------------------------
// Basics

void SoftwareVDPRenderer::Reset(bool hard) {
    m_HRes = vdp::kDefaultResH;
    m_VRes = vdp::kDefaultResV;
    m_exclusiveMonitor = false;

    if (hard) {
        m_CRAMCache.fill({});
    }

    if (m_threadedVDP2Rendering) {
        m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::Reset());
    } else {
        m_framebuffer.fill(0xFF000000);
    }

    for (auto &state : m_vramFetchers) {
        state[0].Reset();
        state[1].Reset();
    }

    for (auto &output : m_layerOutputs) {
        output[0].Reset();
        output[1].Reset();
    }
    m_spriteLayerAttrs[0].Reset();
    m_spriteLayerAttrs[1].Reset();
    for (auto &output : m_rotParamLineOutputs) {
        output.Reset();
    }
    for (auto &fb : m_altFBRAM) {
        fb.fill(0);
    }
    for (auto &altFB : m_meshFBRAM) {
        for (auto &fb : altFB) {
            fb.fill(0);
        }
    }

    VDP2UpdateEnabledBGs();
}

void SoftwareVDPRenderer::UpdateEnhancements() {
    UpdateFunctionPointers();
}

// -----------------------------------------------------------------------------
// Configuration

void SoftwareVDPRenderer::EnableThreadedVDP1(bool enable) {
    if (m_threadedVDP1Rendering == enable) {
        return;
    }

    devlog::debug<grp::swvdp1>("{} threaded VDP1 rendering", (enable ? "Enabling" : "Disabling"));

    m_threadedVDP1Rendering = enable;
    if (enable) {
        m_vdp1RenderingContext.EnqueueEvent(VDP1RenderEvent::PostLoadStateSync());
        m_VDP1RenderThread = std::thread{[&] { VDP1RenderThread(); }};
        m_vdp1RenderingContext.postLoadSyncSignal.Wait();
        m_vdp1RenderingContext.postLoadSyncSignal.Reset();
    } else {
        m_vdp1RenderingContext.EnqueueEvent(VDP1RenderEvent::Shutdown());
        if (m_VDP1RenderThread.joinable()) {
            m_VDP1RenderThread.join();
        }

        VDP1RenderEvent dummy{};
        while (m_vdp1RenderingContext.eventQueue.try_dequeue(dummy)) {
        }
    }
}

void SoftwareVDPRenderer::EnableThreadedVDP2(bool enable) {
    if (m_threadedVDP2Rendering == enable) {
        return;
    }

    devlog::debug<grp::swvdp2>("{} threaded VDP2 rendering", (enable ? "Enabling" : "Disabling"));

    m_threadedVDP2Rendering = enable;
    if (enable) {
        m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::PostLoadStateSync());
        m_VDP2RenderThread = std::thread{[&] { VDP2RenderThread(); }};
        m_VDP2DeinterlaceRenderThread = std::thread{[&] { VDP2DeinterlaceRenderThread(); }};
        m_vdp2RenderingContext.postLoadSyncSignal.Wait();
        m_vdp2RenderingContext.postLoadSyncSignal.Reset();
    } else {
        m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::Shutdown());
        if (m_VDP2RenderThread.joinable()) {
            m_VDP2RenderThread.join();
        }
        if (m_VDP2DeinterlaceRenderThread.joinable()) {
            m_VDP2DeinterlaceRenderThread.join();
        }

        VDP2RenderEvent dummy{};
        while (m_vdp2RenderingContext.eventQueue.try_dequeue(dummy)) {
        }
    }
}

void SoftwareVDPRenderer::UpdateFunctionPointers() {
    UpdateFunctionPointersTemplate(m_enhancements.deinterlace, m_enhancements.transparentMeshes);
}

template <bool... t_features>
void SoftwareVDPRenderer::UpdateFunctionPointersTemplate(bool feature, auto... features) {
    feature ? UpdateFunctionPointersTemplate<t_features..., true>(features...)
            : UpdateFunctionPointersTemplate<t_features..., false>(features...);
}

template <bool... t_features>
void SoftwareVDPRenderer::UpdateFunctionPointersTemplate() {
    m_fnVDP1HandleCommand = &SoftwareVDPRenderer::VDP1Cmd_Handle<t_features...>;
    m_fnVDP2DrawLine = &SoftwareVDPRenderer::VDP2DrawLine<t_features...>;
}

// -----------------------------------------------------------------------------
// Save states

void SoftwareVDPRenderer::PreSaveStateSync() {
    if (m_threadedVDP1Rendering) {
        m_vdp1RenderingContext.EnqueueEvent(VDP1RenderEvent::PreSaveStateSync());
    }
    if (m_threadedVDP2Rendering) {
        m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::PreSaveStateSync());
    }

    if (m_threadedVDP1Rendering) {
        m_vdp1RenderingContext.preSaveSyncSignal.Wait();
        m_vdp1RenderingContext.preSaveSyncSignal.Reset();
    }
    if (m_threadedVDP2Rendering) {
        m_vdp2RenderingContext.preSaveSyncSignal.Wait();
        m_vdp2RenderingContext.preSaveSyncSignal.Reset();
    }
}

void SoftwareVDPRenderer::PostLoadStateSync() {
    for (uint32 address = 0; address < kVDP2CRAMSize; address += 2) {
        VDP2UpdateCRAMCache<uint16>(address);
    }
    VDP2UpdateEnabledBGs();

    if (m_threadedVDP1Rendering) {
        m_vdp1RenderingContext.EnqueueEvent(VDP1RenderEvent::PostLoadStateSync());
    }
    if (m_threadedVDP2Rendering) {
        m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::PostLoadStateSync());
    }

    if (m_threadedVDP1Rendering) {
        m_vdp1RenderingContext.postLoadSyncSignal.Wait();
        m_vdp1RenderingContext.postLoadSyncSignal.Reset();
    }
    if (m_threadedVDP2Rendering) {
        m_vdp2RenderingContext.postLoadSyncSignal.Wait();
        m_vdp2RenderingContext.postLoadSyncSignal.Reset();
    }
}

void SoftwareVDPRenderer::SaveState(savestate::VDPSaveState::VDPRendererSaveState &state) {
    state.vdp1State.doubleV = m_VDP1doubleV;
    state.vdp1State.meshFBRAM = m_meshFBRAM;

    auto copyChar = [&](savestate::VDPSaveState::VDPRendererSaveState::CharacterSaveState &dst, const Character &src) {
        dst.charNum = src.charNum;
        dst.palNum = src.palNum >> 4u;
        dst.specColorCalc = src.specColorCalc;
        dst.specPriority = src.specPriority;
        dst.flipH = src.flipH;
        dst.flipV = src.flipV;
    };

    for (size_t i = 0; i < 2; i++) {
        for (size_t j = 0; j < 6; j++) {
            copyChar(state.vramFetchers[i][j].currChar, m_vramFetchers[i][j].currChar);
            copyChar(state.vramFetchers[i][j].nextChar, m_vramFetchers[i][j].nextChar);
            state.vramFetchers[i][j].lastCharIndex = m_vramFetchers[i][j].lastCharIndex;
            state.vramFetchers[i][j].lastCellX = m_vramFetchers[i][j].lastCellX;
            state.vramFetchers[i][j].charData = m_vramFetchers[i][j].charData;
            state.vramFetchers[i][j].charDataAddress = m_vramFetchers[i][j].charDataAddress;
            state.vramFetchers[i][j].lastVCellScroll = m_vramFetchers[i][j].lastVCellScroll;
        }
    }

    state.vcellScrollInc = m_state.regs2.vcellScrollInc;
}

bool SoftwareVDPRenderer::ValidateState(const savestate::VDPSaveState::VDPRendererSaveState &state) const {
    return true;
}

void SoftwareVDPRenderer::LoadState(const savestate::VDPSaveState::VDPRendererSaveState &state) {
    m_VDP1doubleV = state.vdp1State.doubleV;
    m_meshFBRAM = state.vdp1State.meshFBRAM;

    auto copyChar = [&](Character &dst, const savestate::VDPSaveState::VDPRendererSaveState::CharacterSaveState &src) {
        dst.charNum = src.charNum;
        dst.palNum = src.palNum << 4u;
        dst.specColorCalc = src.specColorCalc;
        dst.specPriority = src.specPriority;
        dst.flipH = src.flipH;
        dst.flipV = src.flipV;
    };

    for (size_t i = 0; i < 2; i++) {
        for (size_t j = 0; j < 6; j++) {
            copyChar(m_vramFetchers[i][j].currChar, state.vramFetchers[i][j].currChar);
            copyChar(m_vramFetchers[i][j].nextChar, state.vramFetchers[i][j].nextChar);
            m_vramFetchers[i][j].lastCharIndex = state.vramFetchers[i][j].lastCharIndex;
            m_vramFetchers[i][j].lastCellX = state.vramFetchers[i][j].lastCellX;
            m_vramFetchers[i][j].charData = state.vramFetchers[i][j].charData;
            m_vramFetchers[i][j].charDataAddress = state.vramFetchers[i][j].charDataAddress;
            m_vramFetchers[i][j].lastVCellScroll = state.vramFetchers[i][j].lastVCellScroll;
        }
    }

    m_state.regs2.vcellScrollInc = state.vcellScrollInc;
}

// -----------------------------------------------------------------------------
// VDP1 memory and register writes

void SoftwareVDPRenderer::VDP1WriteVRAM(uint32 address, uint8 value) {
    VDP1WriteVRAMImpl(address, value);
}

void SoftwareVDPRenderer::VDP1WriteVRAM(uint32 address, uint16 value) {
    VDP1WriteVRAMImpl(address, value);
}

template <mem_primitive_16 T>
FORCE_INLINE void SoftwareVDPRenderer::VDP1WriteVRAMImpl(uint32 address, T value) {
    if (m_threadedVDP1Rendering) {
        m_vdp1RenderingContext.EnqueueEvent(VDP1RenderEvent::VRAMWrite<T>(address, value));
    }
}

void SoftwareVDPRenderer::VDP1SyncFB() {
    if (m_threadedVDP1Rendering) {
        auto &ctx = m_vdp1RenderingContext;
        ctx.FlushPendingEvents();
        for (;;) {
            const uint32 curr = ctx.cmdFence.load(std::memory_order_acquire);
            if (ctx.cmdCount == curr) {
                break;
            }
            ctx.cmdFence.wait(curr, std::memory_order_acquire);
        }
    }
}

void SoftwareVDPRenderer::VDP1DebugSyncFB() {}

void SoftwareVDPRenderer::VDP1WriteFB(uint32 address, uint8 value) {
    VDP1WriteFBImpl(address, value);
}

void SoftwareVDPRenderer::VDP1WriteFB(uint32 address, uint16 value) {
    VDP1WriteFBImpl(address, value);
}

template <mem_primitive_16 T>
FORCE_INLINE void SoftwareVDPRenderer::VDP1WriteFBImpl(uint32 address, T value) {
    if (m_threadedVDP1Rendering) {
        m_vdp1RenderingContext.EnqueueEvent(VDP1RenderEvent::FBRAMWrite<T>(address, value));
    } else {
        if (m_enhancements.deinterlace && m_state.regs2.TVMD.IsInterlaced()) {
            util::WriteBE<T>(&m_altFBRAM[m_state.fbIndex.draw][address & 0x3FFFF], value);
        }
    }
}

void SoftwareVDPRenderer::VDP1WriteReg(uint32 address, uint16 value) {
    if (m_threadedVDP1Rendering) {
        m_vdp1RenderingContext.EnqueueEvent(VDP1RenderEvent::RegWrite(address, value));
    }
    if (m_threadedVDP2Rendering) {
        if (address == 0) {
            m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::VDP1WriteTVMR(value));
        }
    }
}

// -----------------------------------------------------------------------------
// VDP2 memory and register writes

void SoftwareVDPRenderer::VDP2WriteVRAM(uint32 address, uint8 value) {
    VDP2WriteVRAMImpl(address, value);
}

void SoftwareVDPRenderer::VDP2WriteVRAM(uint32 address, uint16 value) {
    VDP2WriteVRAMImpl(address, value);
}

template <mem_primitive_16 T>
FORCE_INLINE void SoftwareVDPRenderer::VDP2WriteVRAMImpl(uint32 address, T value) {
    if (m_threadedVDP2Rendering) {
        m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::VDP2VRAMWrite<T>(address, value));
    }
}

void SoftwareVDPRenderer::VDP2WriteCRAM(uint32 address, uint8 value) {
    VDP2WriteCRAMImpl(address, value);
}

void SoftwareVDPRenderer::VDP2WriteCRAM(uint32 address, uint16 value) {
    VDP2WriteCRAMImpl(address, value);
}

template <mem_primitive_16 T>
FORCE_INLINE void SoftwareVDPRenderer::VDP2WriteCRAMImpl(uint32 address, T value) {
    VDP2UpdateCRAMCache<T>(address);
    if (m_threadedVDP2Rendering) {
        m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::VDP2CRAMWrite<T>(address, value));
    }
}

void SoftwareVDPRenderer::VDP2WriteReg(uint32 address, uint16 value) {
    if (m_threadedVDP2Rendering) {
        m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::VDP2RegWrite(address, value));
    }

    switch (address) {
    case 0x020: [[fallthrough]]; // BGON
    case 0x028: [[fallthrough]]; // CHCTLA
    case 0x02A:                  // CHCTLB
        if (m_threadedVDP2Rendering) {
            m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::VDP2UpdateEnabledBGs());
        } else {
            VDP2UpdateEnabledBGs();
        }
        break;

    case 0x092: // SCYN2
        if (!m_threadedVDP2Rendering) {
            m_state.state2.nbgLayerStates[2].fracScrollY = 0;
        }
        break;
    case 0x096: // SCYN3
        if (!m_threadedVDP2Rendering) {
            m_state.state2.nbgLayerStates[3].fracScrollY = 0;
        }
        break;
    }
}

// -----------------------------------------------------------------------------
// Rendering process

void SoftwareVDPRenderer::VDP1EraseFramebuffer(uint64 cycles) {
    if (m_threadedVDP2Rendering) {
        m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::VDP1EraseFramebuffer());
        m_vdp2RenderingContext.eraseFramebufferReadySignal.Wait();
        m_vdp2RenderingContext.eraseFramebufferReadySignal.Reset();
    }
    if (m_threadedVDP1Rendering) {
        m_vdp1RenderingContext.EnqueueEvent(VDP1RenderEvent::EraseFramebuffer(cycles));
    } else {
        if (cycles == 0) {
            VDP1DoEraseFramebuffer<false>();
        } else {
            VDP1DoEraseFramebuffer<true>(cycles);
        }
    }
}

void SoftwareVDPRenderer::VDP1SwapFramebuffer() {
    if (m_threadedVDP1Rendering) {
        m_vdp1RenderingContext.EnqueueEvent(VDP1RenderEvent::SwapBuffers());
        m_vdp1RenderingContext.swapBuffersSignal.Wait();
        m_vdp1RenderingContext.swapBuffersSignal.Reset();
    }
    if (m_threadedVDP2Rendering) {
        m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::VDP1SwapFramebuffer());
        m_vdp2RenderingContext.framebufferSwapSignal.Wait();
        m_vdp2RenderingContext.framebufferSwapSignal.Reset();
    }

    Callbacks.VDP1FramebufferSwap();
}

void SoftwareVDPRenderer::VDP1BeginFrame() {
    const VDP1Regs &regs1 = VDP1GetRegs();
    const VDP2Regs &regs2 = VDP2GetRegs();
    m_VDP1doubleV =
        m_enhancements.deinterlace && regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity && !regs1.dblInterlaceEnable;
}

void SoftwareVDPRenderer::VDP1ExecuteCommand(uint32 cmdAddress, VDP1Command::Control control) {
    if (m_threadedVDP1Rendering) {
        m_vdp1RenderingContext.EnqueueEvent(VDP1RenderEvent::Command(cmdAddress, control));
    } else {
        (this->*m_fnVDP1HandleCommand)(cmdAddress, control);
    }
}

void SoftwareVDPRenderer::VDP1EndFrame() {
    if (m_threadedVDP1Rendering) {
        m_vdp1RenderingContext.EnqueueEvent(VDP1RenderEvent::EndDraw());
    }
    Callbacks.VDP1DrawFinished();
}

// -----------------------------------------------------------------------------

void SoftwareVDPRenderer::VDP2SetResolution(uint32 h, uint32 v, bool exclusive) {
    if (m_threadedVDP2Rendering) {
        m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::VDP2UpdateResolution(h, v, exclusive));
    } else {
        VDP2UpdateResolution(h, v, exclusive);
    }
}

void SoftwareVDPRenderer::VDP2UpdateResolution(uint32 h, uint32 v, bool exclusive) {
    m_HRes = h;
    m_VRes = v;
    m_exclusiveMonitor = exclusive;

    // Clear framebuffer to avoid artifacts when switching modes
    uint32 color = 0xFF000000;
    if (m_state.regs2.TVMD.BDCLMD) {
        color |= m_state.state2.lineBackLayerState.backColor.u32;
    }
    std::fill_n(m_framebuffer.begin(), m_HRes * m_VRes, color);
}

void SoftwareVDPRenderer::VDP2SetField(bool odd) {
    if (m_threadedVDP2Rendering) {
        m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::OddField(odd));
    }
}

void SoftwareVDPRenderer::VDP2LatchTVMD() {
    if (m_threadedVDP2Rendering) {
        m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::VDP2LatchTVMD());
    }
}

void SoftwareVDPRenderer::VDP2BeginFrame() {
    if (m_threadedVDP2Rendering) {
        m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::VDP2BeginFrame());
    } else {
        VDP2InitFrame();
    }
}

void SoftwareVDPRenderer::VDP2RenderLine(uint32 y) {
    if (m_threadedVDP2Rendering) {
        m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::VDP2DrawLine(y));
        m_state.state2.CalcAccessPatterns(m_state.regs2, m_vdp2AccessPatternsConfig);
        m_state.state2.CalcVCellScrollDelay(m_state.regs2);
    } else {
        const bool interlaced = m_state.regs2.TVMD.IsInterlaced();
        VDP2PrepareLine(y);
        (this->*m_fnVDP2DrawLine)(y, false);
        if (m_enhancements.deinterlace && interlaced) {
            (this->*m_fnVDP2DrawLine)(y, true);
        }
        VDP2FinishLine(y);
    }
}

void SoftwareVDPRenderer::VDP2EndFrame() {
    if (m_threadedVDP2Rendering) {
        m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::VDP2EndFrame());
        m_vdp2RenderingContext.renderFinishedSignal.Wait();
        m_vdp2RenderingContext.renderFinishedSignal.Reset();
    }
    Callbacks.VDP2DrawFinished();
    SwCallbacks.FrameComplete(m_framebuffer.data(), m_HRes, m_VRes);
}

// -----------------------------------------------------------------------------
// Debugger

void SoftwareVDPRenderer::UpdateEnabledLayers() {
    if (m_threadedVDP2Rendering) {
        m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::VDP2UpdateEnabledBGs());
    } else {
        VDP2UpdateEnabledBGs();
    }
}

// -----------------------------------------------------------------------------
// Utilities

void SoftwareVDPRenderer::DumpExtraVDP1Framebuffers(std::ostream &out) const {
    const uint8 dispFB = m_state.fbIndex.display;
    const uint8 drawFB = m_state.fbIndex.draw;
    if (m_enhancements.deinterlace) {
        out.write((const char *)m_altFBRAM[drawFB].data(), m_altFBRAM[drawFB].size());
        out.write((const char *)m_altFBRAM[dispFB].data(), m_altFBRAM[dispFB].size());
    }
    if (m_enhancements.transparentMeshes) {
        out.write((const char *)m_meshFBRAM[0][drawFB].data(), m_meshFBRAM[0][drawFB].size());
        out.write((const char *)m_meshFBRAM[0][dispFB].data(), m_meshFBRAM[0][dispFB].size());
        out.write((const char *)m_meshFBRAM[1][drawFB].data(), m_meshFBRAM[1][drawFB].size());
        out.write((const char *)m_meshFBRAM[1][dispFB].data(), m_meshFBRAM[1][dispFB].size());
    }
}

// -----------------------------------------------------------------------------
// Rendering

void SoftwareVDPRenderer::VDP1RenderThread() {
    util::SetCurrentThreadName("VDP1 render thread");

    auto &rctx = m_vdp1RenderingContext;

    std::array<VDP1RenderEvent, 64> events{};

    bool running = true;
    while (running) {
        const size_t count = rctx.DequeueEvents(events.begin(), events.size());

        for (size_t i = 0; i < count; ++i) {
            const auto &event = events[i];
            using EvtType = VDP1RenderEvent::Type;
            switch (event.type) {
            case EvtType::Reset: rctx.Reset(); break;

            case EvtType::EraseFramebuffer: {
                if (event.erase.cycles == 0) {
                    VDP1DoEraseFramebuffer<false>();
                } else {
                    VDP1DoEraseFramebuffer<true>(event.erase.cycles);
                }
                const auto fbIndex = m_state.fbIndex.display;
                m_state.mem1.FBRAM[fbIndex] = rctx.vdp1.mem.FBRAM[fbIndex];
                break;
            }
            case EvtType::SwapBuffers: {
                const auto fbIndex = m_state.fbIndex.draw;
                m_state.mem1.FBRAM[fbIndex] = rctx.vdp1.mem.FBRAM[fbIndex];
                rctx.vdp1.regs.LatchEraseParameters();
                rctx.swapBuffersSignal.Set();
                break;
            }
            case EvtType::EndDraw: {
                const auto fbIndex = m_state.fbIndex.draw;
                m_state.mem1.FBRAM[fbIndex] = rctx.vdp1.mem.FBRAM[fbIndex];
                break;
            }
            case EvtType::Command: (this->*m_fnVDP1HandleCommand)(event.command.address, event.command.control); break;

            case EvtType::VRAMWriteByte: rctx.vdp1.mem.VRAM[event.write.address] = event.write.value; break;
            case EvtType::VRAMWriteWord:
                util::WriteBE<uint16>(&rctx.vdp1.mem.VRAM[event.write.address], event.write.value);
                break;
            case EvtType::FBRAMWriteByte: {
                const auto fbIndex = m_state.fbIndex.draw;
                rctx.vdp1.mem.FBRAM[fbIndex][event.write.address] = event.write.value;
                m_state.mem1.FBRAM[fbIndex][event.write.address] = event.write.value;
                if (m_enhancements.deinterlace && m_state.regs2.TVMD.IsInterlaced()) {
                    m_altFBRAM[fbIndex][event.write.address] = event.write.value;
                }
                break;
            }
            case EvtType::FBRAMWriteWord: {
                const auto fbIndex = m_state.fbIndex.draw;
                util::WriteBE<uint16>(&rctx.vdp1.mem.FBRAM[fbIndex][event.write.address], event.write.value);
                util::WriteBE<uint16>(&m_state.mem1.FBRAM[fbIndex][event.write.address], event.write.value);
                if (m_enhancements.deinterlace && m_state.regs2.TVMD.IsInterlaced()) {
                    util::WriteBE<uint16>(&m_altFBRAM[fbIndex][event.write.address], event.write.value);
                }
                break;
            }
            case EvtType::RegWrite: rctx.vdp1.regs.Write<false>(event.write.address, event.write.value); break;

            case EvtType::PreSaveStateSync: rctx.preSaveSyncSignal.Set(); break;
            case EvtType::PostLoadStateSync:
                rctx.vdp1.regs = m_state.regs1;
                rctx.vdp1.mem = m_state.mem1;
                rctx.postLoadSyncSignal.Set();
                break;

            case EvtType::Shutdown: running = false; break;
            }

            rctx.cmdFence.fetch_add(1, std::memory_order_release);
            rctx.cmdFence.notify_all();
        }
    }
}

void SoftwareVDPRenderer::VDP2RenderThread() {
    util::SetCurrentThreadName("VDP2 render thread");

    auto &rctx = m_vdp2RenderingContext;

    std::array<VDP2RenderEvent, 64> events{};

    bool running = true;
    while (running) {
        const size_t count = rctx.DequeueEvents(events.begin(), events.size());

        for (size_t i = 0; i < count; ++i) {
            const auto &event = events[i];
            using EvtType = VDP2RenderEvent::Type;
            switch (event.type) {
            case EvtType::Reset:
                rctx.Reset();
                m_framebuffer.fill(0xFF000000);
                break;
            case EvtType::OddField: rctx.vdp2.regs.TVSTAT.ODD = event.oddField.odd; break;
            case EvtType::VDP2LatchTVMD: rctx.vdp2.regs.LatchTVMD(); break;
            case EvtType::VDP1EraseFramebuffer: rctx.eraseFramebufferReadySignal.Set(); break;
            case EvtType::VDP1SwapFramebuffer: rctx.framebufferSwapSignal.Set(); break;
            case EvtType::VDP1WriteTVMR: rctx.tvmr.Write(event.writeTVMR.value); break;

            case EvtType::VDP2UpdateResolution:
                VDP2UpdateResolution(event.updateResolution.h, event.updateResolution.v,
                                     event.updateResolution.exclusive);
                break;
            case EvtType::VDP2BeginFrame: VDP2InitFrame(); break;
            case EvtType::VDP2UpdateEnabledBGs: VDP2UpdateEnabledBGs(); break;
            case EvtType::VDP2DrawLine: //
            {
                const bool deinterlaceRender = m_enhancements.deinterlace;
                const bool threadedDeinterlacer = m_threadedDeinterlacer;
                const bool interlaced = rctx.vdp2.regs.TVMD.IsInterlaced();
                VDP2PrepareLine(event.drawLine.vcnt);
                if (deinterlaceRender && interlaced && threadedDeinterlacer) {
                    rctx.deinterlaceY = event.drawLine.vcnt;
                    rctx.deinterlaceRenderBeginSignal.Set();
                }
                (this->*m_fnVDP2DrawLine)(event.drawLine.vcnt, false);
                if (deinterlaceRender && interlaced) {
                    if (threadedDeinterlacer) {
                        rctx.deinterlaceRenderEndSignal.Wait();
                        rctx.deinterlaceRenderEndSignal.Reset();
                    } else {
                        (this->*m_fnVDP2DrawLine)(event.drawLine.vcnt, true);
                    }
                }
                VDP2FinishLine(event.drawLine.vcnt);
                break;
            }
            case EvtType::VDP2EndFrame: rctx.renderFinishedSignal.Set(); break;

            case EvtType::VDP2VRAMWriteByte: rctx.vdp2.mem.VRAM[event.write.address] = event.write.value; break;
            case EvtType::VDP2VRAMWriteWord:
                util::WriteBE<uint16>(&rctx.vdp2.mem.VRAM[event.write.address], event.write.value);
                break;
            case EvtType::VDP2CRAMWriteByte:
                // Update CRAM cache if color RAM mode changed is in one of the RGB555 modes
                if (rctx.vdp2.regs.vramControl.colorRAMMode <= 1) {
                    const uint8 oldValue = rctx.vdp2.mem.CRAM[event.write.address];
                    rctx.vdp2.mem.CRAM[event.write.address] = event.write.value;

                    if (oldValue != event.write.value) {
                        const uint32 cramAddress = event.write.address & ~1;
                        const uint16 colorValue = VDP2ReadRendererCRAM<uint16>(cramAddress);
                        const Color555 color5{.u16 = colorValue};
                        rctx.vdp2.CRAMCache[cramAddress / sizeof(uint16)] = ConvertRGB555to888(color5);
                    }
                } else {
                    rctx.vdp2.mem.CRAM[event.write.address] = event.write.value;
                }
                break;
            case EvtType::VDP2CRAMWriteWord:
                // Update CRAM cache if color RAM mode is in one of the RGB555 modes
                if (rctx.vdp2.regs.vramControl.colorRAMMode <= 1) {
                    const uint16 oldValue = util::ReadBE<uint16>(&rctx.vdp2.mem.CRAM[event.write.address]);
                    util::WriteBE<uint16>(&rctx.vdp2.mem.CRAM[event.write.address], event.write.value);

                    if (oldValue != event.write.value) {
                        const uint32 cramAddress = event.write.address & ~1;
                        const Color555 color5{.u16 = (uint16)event.write.value};
                        rctx.vdp2.CRAMCache[cramAddress / sizeof(uint16)] = ConvertRGB555to888(color5);
                    }
                } else {
                    util::WriteBE<uint16>(&rctx.vdp2.mem.CRAM[event.write.address], event.write.value);
                }
                break;
            case EvtType::VDP2RegWrite:
                // Refill CRAM cache if color RAM mode changed to one of the RGB555 modes
                if (event.write.address == 0x00E) {
                    const uint8 oldMode = rctx.vdp2.regs.vramControl.colorRAMMode;
                    rctx.vdp2.regs.WriteRAMCTL(event.write.value);

                    const uint8 newMode = rctx.vdp2.regs.vramControl.colorRAMMode;
                    if (newMode != oldMode && newMode <= 1) {
                        for (uint32 addr = 0; addr < rctx.vdp2.mem.CRAM.size(); addr += sizeof(uint16)) {
                            const uint16 colorValue = VDP2ReadRendererCRAM<uint16>(addr);
                            const Color555 color5{.u16 = colorValue};
                            rctx.vdp2.CRAMCache[addr / sizeof(uint16)] = ConvertRGB555to888(color5);
                        }
                    }
                } else {
                    rctx.vdp2.regs.Write(event.write.address, event.write.value);
                    switch (event.write.address) {
                    case 0x092: // SCYN2
                        m_state.state2.nbgLayerStates[2].fracScrollY = 0;
                        break;
                    case 0x096: // SCYN3
                        m_state.state2.nbgLayerStates[3].fracScrollY = 0;
                        break;
                    }
                }
                break;

            case EvtType::PreSaveStateSync: rctx.preSaveSyncSignal.Set(); break;
            case EvtType::PostLoadStateSync:
                rctx.vdp2.regs = m_state.regs2;
                rctx.vdp2.mem = m_state.mem2;
                rctx.tvmr = m_state.regs1.tvmr;
                rctx.postLoadSyncSignal.Set();
                VDP2UpdateEnabledBGs();
                for (uint32 addr = 0; addr < rctx.vdp2.mem.CRAM.size(); addr += sizeof(uint16)) {
                    const uint16 colorValue = VDP2ReadRendererCRAM<uint16>(addr);
                    const Color555 color5{.u16 = colorValue};
                    rctx.vdp2.CRAMCache[addr / sizeof(uint16)] = ConvertRGB555to888(color5);
                }
                break;

            case EvtType::Shutdown:
                rctx.deinterlaceShutdown = true;
                rctx.deinterlaceRenderBeginSignal.Set();
                rctx.deinterlaceRenderEndSignal.Wait();
                rctx.deinterlaceRenderEndSignal.Reset();
                running = false;
                break;
            }
        }
    }
}

void SoftwareVDPRenderer::VDP2DeinterlaceRenderThread() {
    util::SetCurrentThreadName("VDP deinterlace render thread");

    auto &rctx = m_vdp2RenderingContext;

    while (true) {
        rctx.deinterlaceRenderBeginSignal.Wait();
        rctx.deinterlaceRenderBeginSignal.Reset();
        if (rctx.deinterlaceShutdown) {
            rctx.deinterlaceShutdown = false;
            rctx.deinterlaceRenderEndSignal.Set();
            return;
        }

        (this->*m_fnVDP2DrawLine)(rctx.deinterlaceY, true);
        rctx.deinterlaceRenderEndSignal.Set();
    }
}

template <mem_primitive T>
FORCE_INLINE T SoftwareVDPRenderer::VDP1ReadRendererVRAM(uint32 address) {
    if (m_threadedVDP1Rendering) {
        return m_vdp1RenderingContext.vdp1.mem.ReadVRAM<T>(address);
    } else {
        return m_state.mem1.ReadVRAM<T>(address);
    }
}

FORCE_INLINE std::array<uint8, kVDP2VRAMSize> &SoftwareVDPRenderer::VDP2GetRendererVRAM() {
    return m_threadedVDP2Rendering ? m_vdp2RenderingContext.vdp2.mem.VRAM : m_state.mem2.VRAM;
}

template <mem_primitive T>
FORCE_INLINE T SoftwareVDPRenderer::VDP2ReadRendererVRAM(uint32 address) {
    if (m_threadedVDP2Rendering) {
        return m_vdp2RenderingContext.vdp2.mem.ReadVRAM<T>(address);
    } else {
        return m_state.mem2.ReadVRAM<T>(address);
    }
}

template <mem_primitive T>
FORCE_INLINE T SoftwareVDPRenderer::VDP2ReadRendererCRAM(uint32 address) {
    if constexpr (std::is_same_v<T, uint32>) {
        uint32 value = VDP2ReadRendererCRAM<uint16>(address + 0) << 16u;
        value |= VDP2ReadRendererCRAM<uint16>(address + 2) << 0u;
        return value;
    } else {
        if (m_threadedVDP2Rendering) {
            return m_vdp2RenderingContext.vdp2.mem.ReadCRAM<T>(address);
        } else {
            return m_state.mem2.ReadCRAM<T>(address);
        }
    }
}

FORCE_INLINE Color888 SoftwareVDPRenderer::VDP2ReadRendererColor5to8(uint32 address) const {
    if (m_threadedVDP2Rendering) {
        return m_vdp2RenderingContext.vdp2.CRAMCache[(address / sizeof(uint16)) & 0x7FF];
    } else {
        return m_CRAMCache[(address / sizeof(uint16)) & 0x7FF];
    }
}

template <mem_primitive T>
FORCE_INLINE void SoftwareVDPRenderer::VDP2UpdateCRAMCache(uint32 address) {
    address &= ~1;
    const Color555 color5{.u16 = util::ReadBE<uint16>(&m_state.mem2.CRAM[address])};
    m_CRAMCache[address / sizeof(uint16)] = ConvertRGB555to888(color5);
    if constexpr (std::is_same_v<T, uint32>) {
        const Color555 color5{.u16 = util::ReadBE<uint16>(&m_state.mem2.CRAM[address + 2])};
        m_CRAMCache[(address + 2) / sizeof(uint16)] = ConvertRGB555to888(color5);
    }
}

// -----------------------------------------------------------------------------
// VDP1

FORCE_INLINE VDP1Regs &SoftwareVDPRenderer::VDP1GetRegs() {
    if (m_threadedVDP1Rendering) {
        return m_vdp1RenderingContext.vdp1.regs;
    } else {
        return m_state.regs1;
    }
}

FORCE_INLINE const VDP1Regs &SoftwareVDPRenderer::VDP1GetRegs() const {
    if (m_threadedVDP1Rendering) {
        return m_vdp1RenderingContext.vdp1.regs;
    } else {
        return m_state.regs1;
    }
}

FORCE_INLINE SpriteFB &SoftwareVDPRenderer::VDP1GetRendererFBRAM(bool altFB, uint8 fbIndex) {
    if (altFB) {
        return m_altFBRAM[fbIndex];
    } else if (m_threadedVDP1Rendering) {
        return m_vdp1RenderingContext.vdp1.mem.FBRAM[fbIndex];
    } else {
        return m_state.mem1.FBRAM[fbIndex];
    }
}

template <bool countCycles>
FORCE_INLINE void SoftwareVDPRenderer::VDP1DoEraseFramebuffer(uint64 cycles) {
    const VDP1Regs &regs1 = VDP1GetRegs();
    const VDP2Regs &regs2 = VDP2GetRegs();
    auto &ctx = m_state.state1;
    const uint8 fbIndex = m_state.fbIndex.display;

    devlog::trace<grp::swvdp1>("Erasing framebuffer {} - {}x{} to {}x{} -> {:04X}  {}x{}  {}-bit", fbIndex,
                               regs1.eraseX1Latch, regs1.eraseY1Latch, regs1.eraseX3Latch, regs1.eraseY3Latch,
                               regs1.eraseWriteValueLatch, regs1.tvmr.fbSizeH, regs1.tvmr.fbSizeV,
                               (regs1.tvmr.pixel8Bits ? 8 : 16));

    auto &fb = VDP1GetRendererFBRAM(false, fbIndex);
    auto &altFB = m_altFBRAM[fbIndex];
    [[maybe_unused]] auto &meshFB = m_meshFBRAM[0][fbIndex];
    [[maybe_unused]] auto &altMeshFB = m_meshFBRAM[1][fbIndex];

    const uint32 fbOffsetShift = regs1.tvmr.eraseOffsetShift;

    const bool doubleDensity = regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity;

    // Vertical scale is doubled in double-interlace mode
    const uint32 scaleV = doubleDensity ? 1 : 0;

    // Constrain erase area to certain limits based on current resolution
    const uint32 maxH = (regs2.TVMD.HRESOn & 1) ? 428 : 400;
    const uint32 maxV = m_VRes >> scaleV;

    const uint32 x1 = std::min<uint32>(regs1.eraseX1Latch, maxH);
    const uint32 x3 = std::min<uint32>(regs1.eraseX3Latch, maxH);
    const uint32 y1 = std::min<uint32>(regs1.eraseY1Latch, maxV) << scaleV;
    const uint32 y3 = std::min<uint32>(regs1.eraseY3Latch, maxV) << scaleV;

    const bool mirror = m_enhancements.deinterlace && doubleDensity;

    static constexpr uint64 kCyclesPerWrite = 1;

    for (uint32 y = y1; y <= y3; y++) {
        const uint32 fbOffset = y << fbOffsetShift;
        for (uint32 x = x1; x < x3; x++) {
            const uint32 address = (fbOffset + x) * sizeof(uint16);
            util::WriteBE<uint16>(&fb[address & 0x3FFFE], regs1.eraseWriteValueLatch);
            if (mirror) {
                util::WriteBE<uint16>(&altFB[address & 0x3FFFE], regs1.eraseWriteValueLatch);
            }

            if (m_enhancements.transparentMeshes) {
                util::WriteBE<uint16>(&meshFB[address & 0x3FFFE], 0);
                if (mirror) {
                    util::WriteBE<uint16>(&altMeshFB[address & 0x3FFFE], 0);
                }
            }

            if constexpr (countCycles) {
                if (cycles >= kCyclesPerWrite) {
                    cycles -= kCyclesPerWrite;
                } else {
                    devlog::trace<grp::swvdp1>("Erase process ran out of cycles");
                    return;
                }
            }
        }
    }
}

template <bool deinterlace>
FORCE_INLINE bool SoftwareVDPRenderer::VDP1IsPixelClipped(CoordS32 coord, bool userClippingEnable,
                                                          bool clippingMode) const {
    if (VDP1IsPixelSystemClipped<deinterlace>(coord)) {
        return true;
    }
    if (userClippingEnable) {
        // clippingMode = false -> draw inside, reject outside
        // clippingMode = true -> draw outside, reject inside
        // The function returns true if the pixel is clipped, therefore we want to reject pixels that return the
        // opposite of clippingMode on that function.
        if (VDP1IsPixelUserClipped<deinterlace>(coord) != clippingMode) {
            return true;
        }
    }
    return false;
}

template <bool deinterlace>
FORCE_INLINE bool SoftwareVDPRenderer::VDP1IsPixelUserClipped(CoordS32 coord) const {
    auto [x, y] = coord;
    const auto &ctx = m_state.state1;
    if (x < ctx.userClipX0 || x > ctx.userClipX1) {
        return true;
    }
    if (y < ((ctx.userClipY0 << m_VDP1doubleV) | m_VDP1doubleV) ||
        y > ((ctx.userClipY1 << m_VDP1doubleV) | m_VDP1doubleV)) {
        return true;
    }
    return false;
}

template <bool deinterlace>
FORCE_INLINE bool SoftwareVDPRenderer::VDP1IsPixelSystemClipped(CoordS32 coord) const {
    auto [x, y] = coord;
    const auto &ctx = m_state.state1;
    if (x < 0 || x > ctx.sysClipH) {
        return true;
    }
    if (y < 0 || y > ((ctx.sysClipV << m_VDP1doubleV) | m_VDP1doubleV)) {
        return true;
    }
    return false;
}

template <bool deinterlace>
FORCE_INLINE bool SoftwareVDPRenderer::VDP1IsLineSystemClipped(CoordS32 coord1, CoordS32 coord2) const {
    auto [x1, y1] = coord1;
    auto [x2, y2] = coord2;
    const auto &ctx = m_state.state1;
    if (x1 < 0 && x2 < 0) {
        return true;
    }
    if (y1 < 0 && y2 < 0) {
        return true;
    }
    if (x1 > ctx.sysClipH && x2 > ctx.sysClipH) {
        return true;
    }
    if (y1 > ((ctx.sysClipV << m_VDP1doubleV) | m_VDP1doubleV) &&
        y2 > ((ctx.sysClipV << m_VDP1doubleV) | m_VDP1doubleV)) {
        return true;
    }
    return false;
}

template <bool deinterlace>
bool SoftwareVDPRenderer::VDP1IsQuadSystemClipped(CoordS32 coord1, CoordS32 coord2, CoordS32 coord3,
                                                  CoordS32 coord4) const {
    auto [x1, y1] = coord1;
    auto [x2, y2] = coord2;
    auto [x3, y3] = coord3;
    auto [x4, y4] = coord4;
    const auto &ctx = m_state.state1;
    if (x1 < 0 && x2 < 0 && x3 < 0 && x4 < 0) {
        return true;
    }
    if (y1 < 0 && y2 < 0 && y3 < 0 && y4 < 0) {
        return true;
    }
    if (x1 > ctx.sysClipH && x2 > ctx.sysClipH && x3 > ctx.sysClipH && x4 > ctx.sysClipH) {
        return true;
    }
    if (y1 > ((ctx.sysClipV << m_VDP1doubleV) | m_VDP1doubleV) &&
        y2 > ((ctx.sysClipV << m_VDP1doubleV) | m_VDP1doubleV) &&
        y3 > ((ctx.sysClipV << m_VDP1doubleV) | m_VDP1doubleV) &&
        y4 > ((ctx.sysClipV << m_VDP1doubleV) | m_VDP1doubleV)) {
        return true;
    }
    return false;
}

template <bool deinterlace, bool transparentMeshes>
FORCE_INLINE bool SoftwareVDPRenderer::VDP1PlotPixel(CoordS32 coord, const VDP1PixelParams &pixelParams,
                                                     const VDP1Regs &regs1, bool doubleDensity) {
    auto [x, y] = coord;

    // Reject pixels outside of clipping area
    if (VDP1IsPixelClipped<deinterlace>(coord, pixelParams.mode.userClippingEnable, pixelParams.mode.clippingMode)) {
        return false;
    }

    if constexpr (!transparentMeshes) {
        if (pixelParams.mode.meshEnable && ((x ^ y) & 1)) {
            return true;
        }
    }

    const bool altFB = deinterlace && doubleDensity && (y & 1);
    if (!deinterlace && doubleDensity && regs1.dblInterlaceEnable && (y & 1) != regs1.dblInterlaceDrawLine) {
        return true;
    }
    if ((deinterlace && doubleDensity) || regs1.dblInterlaceEnable) {
        y >>= 1;
    }

    // TODO: pixelParams.mode.preClippingDisable

    uint32 fbOffset = y * regs1.tvmr.fbSizeH + x;
    if (!regs1.tvmr.pixel8Bits) {
        fbOffset *= sizeof(uint16);
    }
    fbOffset &= 0x3FFFF;

    const auto fbIndex = m_state.fbIndex.draw;
    auto &drawFB = VDP1GetRendererFBRAM(altFB, fbIndex);
    if (pixelParams.mode.msbOn) {
        // TODO: check correctness -- does it write only when (x&1)==0 or is it force-aligned like this?
        drawFB[fbOffset & ~1u] |= 0x80;
        return true;
    }

    if (regs1.tvmr.pixel8Bits) {
        // TODO: what happens if pixelParams.mode.colorCalcBits/gouraudEnable != 0?
        if (transparentMeshes && pixelParams.mode.meshEnable) {
            m_meshFBRAM[altFB][fbIndex][fbOffset] = pixelParams.color;
        } else {
            drawFB[fbOffset] = pixelParams.color;
            if constexpr (transparentMeshes) {
                m_meshFBRAM[altFB][fbIndex][fbOffset] = 0;
            }
        }
    } else {
        uint8 *pixel = &drawFB[fbOffset];

        Color555 srcColor{.u16 = pixelParams.color};
        Color555 dstColor{.u16 = util::ReadBE<uint16>(pixel)};

        // Apply color calculations
        //
        // In all cases where calculation is done, the raw color data to be drawn ("original graphic") or from
        // the background are interpreted as 5:5:5 RGB.

        if (pixelParams.mode.gouraudEnable) {
            // Apply gouraud shading to source color
            srcColor = pixelParams.gouraud.Blend(srcColor);
        }

        switch (pixelParams.mode.colorCalcBits) {
        case 0: // Replace
            dstColor = srcColor;
            break;
        case 1: // Shadow
            // Halve destination luminosity if it's not transparent
            if (dstColor.msb) {
                dstColor.r >>= 1u;
                dstColor.g >>= 1u;
                dstColor.b >>= 1u;
            }
            break;
        case 2: // Half-luminance
            // Draw original graphic with halved luminance
            dstColor.r = srcColor.r >> 1u;
            dstColor.g = srcColor.g >> 1u;
            dstColor.b = srcColor.b >> 1u;
            dstColor.msb = srcColor.msb;
            break;
        case 3: // Half-transparency
            // If background is not transparent, blend half of original graphic and half of background
            // Otherwise, draw original graphic as is
            if (dstColor.msb) {
                dstColor.r = (srcColor.r + dstColor.r) >> 1u;
                dstColor.g = (srcColor.g + dstColor.g) >> 1u;
                dstColor.b = (srcColor.b + dstColor.b) >> 1u;
            } else {
                dstColor = srcColor;
            }
            break;
        }

        if (transparentMeshes && pixelParams.mode.meshEnable) {
            util::WriteBE<uint16>(&m_meshFBRAM[altFB][fbIndex][fbOffset], dstColor.u16);
        } else {
            util::WriteBE<uint16>(pixel, dstColor.u16);
            if constexpr (transparentMeshes) {
                util::WriteBE<uint16>(&m_meshFBRAM[altFB][fbIndex][fbOffset], 0);
            }
        }
    }
    return true;
}

template <bool antiAlias, bool deinterlace, bool transparentMeshes>
FORCE_INLINE bool SoftwareVDPRenderer::VDP1PlotLine(CoordS32 coord1, CoordS32 coord2, VDP1LineParams &lineParams,
                                                    const VDP1Regs &regs1, bool doubleDensity) {
    if (VDP1IsLineSystemClipped<deinterlace>(coord1, coord2)) {
        return false;
    }

    LineStepper line{coord1, coord2, antiAlias};
    auto &ctx = m_state.state1;
    const uint32 skipSteps = line.SystemClip(ctx.sysClipH, (ctx.sysClipV << m_VDP1doubleV) | m_VDP1doubleV);

    VDP1PixelParams pixelParams{
        .mode = lineParams.mode,
        .color = lineParams.color,
    };
    if (pixelParams.mode.gouraudEnable) {
        pixelParams.gouraud.Setup(line.DMajor() + 1, lineParams.gouraudLeft, lineParams.gouraudRight);
        pixelParams.gouraud.Skip(skipSteps);
    }

    bool aa = false;
    bool plotted = false;
    for (line.Step(); line.CanStep(); aa = line.Step()) {
        bool plottedPixel =
            VDP1PlotPixel<deinterlace, transparentMeshes>(line.Coord(), pixelParams, regs1, doubleDensity);
        if constexpr (antiAlias) {
            if (aa) {
                plottedPixel |=
                    VDP1PlotPixel<deinterlace, transparentMeshes>(line.AACoord(), pixelParams, regs1, doubleDensity);
            }
        }
        if (plottedPixel) {
            plotted = true;
        } else if (plotted && !pixelParams.mode.clippingMode) {
            // No more pixels can be drawn past this point
            break;
        }

        if (pixelParams.mode.gouraudEnable) {
            pixelParams.gouraud.Step();
        }
    }

    return plotted;
}

template <bool deinterlace, bool transparentMeshes>
FORCE_INLINE bool SoftwareVDPRenderer::VDP1PlotTexturedLine(CoordS32 coord1, CoordS32 coord2,
                                                            VDP1TexturedLineParams &lineParams, const VDP1Regs &regs1,
                                                            bool doubleDensity) {
    if (VDP1IsLineSystemClipped<deinterlace>(coord1, coord2)) {
        return false;
    }

    auto &ctx = m_state.state1;

    const uint32 charSizeH = std::max<uint32>(lineParams.charSizeH, 1u);
    const auto mode = lineParams.mode;
    const auto control = lineParams.control;
    if (mode.colorMode == 5) {
        // Force-align character address in 16 bpp RGB mode
        lineParams.charAddr &= ~0xF;
    }

    const uint32 v = lineParams.texVStepper.Value();

    LineStepper line{coord1, coord2, true};
    const uint32 skipSteps = line.SystemClip(ctx.sysClipH, (ctx.sysClipV << m_VDP1doubleV) | m_VDP1doubleV);

    VDP1PixelParams pixelParams{
        .mode = mode,
    };
    if (mode.gouraudEnable) {
        assert(lineParams.gouraudLeft != nullptr);
        assert(lineParams.gouraudRight != nullptr);
        pixelParams.gouraud.Setup(line.DMajor() + 1, lineParams.gouraudLeft->Value(), lineParams.gouraudRight->Value());
        pixelParams.gouraud.Skip(skipSteps);
    }

    sint32 uStart = 0;
    sint32 uEnd = charSizeH - 1;
    if (control.flipH) {
        std::swap(uStart, uEnd);
    }
    const bool useHighSpeedShrink = mode.highSpeedShrink && line.DMajor() < charSizeH - 1;

    TextureStepper uStepper;
    uStepper.Setup(line.DMajor() + 1, uStart, uEnd, useHighSpeedShrink, regs1.evenOddCoordSelect);
    uStepper.SkipPixels(skipSteps);

    uint16 color = 0;
    bool transparent = true;
    bool hasEndCode = false;
    int endCodeCount = useHighSpeedShrink ? std::numeric_limits<int>::min() : 0;

    auto readTexel = [&] {
        const uint32 u = uStepper.Value();

        const uint32 charIndex = u + v * charSizeH;

        auto processEndCode = [&](bool endCode) {
            if (endCode && !mode.endCodeDisable) {
                hasEndCode = true;
                ++endCodeCount;
            } else {
                hasEndCode = false;
            }
        };

        // Read next texel
        switch (mode.colorMode) {
        case 0: // 4 bpp, 16 colors, bank mode
            color = VDP1ReadRendererVRAM<uint8>(lineParams.charAddr + (charIndex >> 1));
            color = (color >> ((~u & 1) * 4)) & 0xF;
            processEndCode(color == 0xF);
            transparent = color == 0x0;
            color |= lineParams.colorBank;
            break;
        case 1: // 4 bpp, 16 colors, lookup table mode
            color = VDP1ReadRendererVRAM<uint8>(lineParams.charAddr + (charIndex >> 1));
            color = (color >> ((~u & 1) * 4)) & 0xF;
            processEndCode(color == 0xF);
            transparent = color == 0x0;
            color = VDP1ReadRendererVRAM<uint16>(color * sizeof(uint16) + lineParams.colorBank);
            break;
        case 2: // 8 bpp, 64 colors, bank mode
            color = VDP1ReadRendererVRAM<uint8>(lineParams.charAddr + charIndex);
            processEndCode(color == 0xFF);
            transparent = color == 0x00;
            color &= 0x3F;
            color |= lineParams.colorBank;
            break;
        case 3: // 8 bpp, 128 colors, bank mode
            color = VDP1ReadRendererVRAM<uint8>(lineParams.charAddr + charIndex);
            processEndCode(color == 0xFF);
            transparent = color == 0x00;
            color &= 0x7F;
            color |= lineParams.colorBank;
            break;
        case 4: // 8 bpp, 256 colors, bank mode
            color = VDP1ReadRendererVRAM<uint8>(lineParams.charAddr + charIndex);
            processEndCode(color == 0xFF);
            transparent = color == 0x00;
            color |= lineParams.colorBank;
            break;
        case 5: // 16 bpp, 32768 colors, RGB mode
            color = VDP1ReadRendererVRAM<uint16>(lineParams.charAddr + charIndex * sizeof(uint16));
            processEndCode(color == 0x7FFF);
            transparent = !bit::test<15>(color);
            break;
        }
    };

    readTexel();

    bool aa = false;
    bool plotted = false;
    for (line.Step(); line.CanStep(); aa = line.Step()) {
        // Load new texels if U coordinate changed
        while (uStepper.ShouldStepTexel()) {
            uStepper.StepTexel();
            readTexel();

            if (endCodeCount == 2) {
                break;
            }
        }
        if (endCodeCount == 2) {
            break;
        }
        uStepper.StepPixel();

        if (hasEndCode || (transparent && !mode.transparentPixelDisable)) {
            if (mode.gouraudEnable) {
                pixelParams.gouraud.Step();
            }

            // Check if the transparent pixel is in-bounds, but only if the clipping mode is set to reject outside
            if (!mode.clippingMode) {
                if (!VDP1IsPixelClipped<deinterlace>(line.Coord(), mode.userClippingEnable, mode.clippingMode)) {
                    plotted = true;
                    continue;
                }
                if (aa &&
                    !VDP1IsPixelClipped<deinterlace>(line.AACoord(), mode.userClippingEnable, mode.clippingMode)) {
                    plotted = true;
                    continue;
                }

                // At this point the pixel is clipped. Bail out if there have been in-bounds pixels before, as no more
                // pixels can be drawn past this point.
                if (plotted) {
                    break;
                }
            }

            // Otherwise, continue to the next pixel
            continue;
        }

        pixelParams.color = color;

        bool plottedPixel =
            VDP1PlotPixel<deinterlace, transparentMeshes>(line.Coord(), pixelParams, regs1, doubleDensity);
        if (aa) {
            plottedPixel |=
                VDP1PlotPixel<deinterlace, transparentMeshes>(line.AACoord(), pixelParams, regs1, doubleDensity);
        }
        if (plottedPixel) {
            plotted = true;
        } else if (plotted && !mode.clippingMode) {
            // No more pixels can be drawn past this point
            break;
        }

        if (mode.gouraudEnable) {
            pixelParams.gouraud.Step();
        }
    }

    if (endCodeCount == 2 && !plotted && !mode.clippingMode) {
        // Check that the line is indeed entirely out of bounds.
        // End codes cut the line short, so if it happens to cut the line before it managed to plot a pixel in-bounds,
        // the optimization could interrupt rendering the rest of the quad.
        for (; line.CanStep(); aa = line.Step()) {
            if (!VDP1IsPixelClipped<deinterlace>(line.Coord(), mode.userClippingEnable, mode.clippingMode)) {
                plotted = true;
                break;
            }
            if (aa && !VDP1IsPixelClipped<deinterlace>(line.AACoord(), mode.userClippingEnable, mode.clippingMode)) {
                plotted = true;
                break;
            }
        }
    }

    return plotted;
}

template <bool deinterlace, bool transparentMeshes>
FORCE_INLINE void SoftwareVDPRenderer::VDP1PlotTexturedQuad(uint32 cmdAddress, VDP1Command::Control control,
                                                            VDP1Command::Size size, CoordS32 coordA, CoordS32 coordB,
                                                            CoordS32 coordC, CoordS32 coordD) {
    if (VDP1IsQuadSystemClipped<deinterlace>(coordA, coordB, coordC, coordD)) {
        return;
    }

    const VDP1Command::DrawMode mode{.u16 = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x04)};
    const uint16 color = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x06);
    const uint32 charAddr = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x08) * 8u;

    const uint32 charSizeH = size.H * 8;
    const uint32 charSizeV = size.V;

    devlog::trace<grp::swvdp1_cmd>("Textured quad parameters: color={:04X} mode={:04X} size={:2d}x{:<2d} char={:05X}",
                                   color, mode.u16, charSizeH, charSizeV, charAddr);

    VDP1TexturedLineParams lineParams{
        .control = control,
        .mode = mode,
        .colorBank = color,
        .charAddr = charAddr,
        .charSizeH = charSizeH,
        .charSizeV = charSizeV,
    };

    // Precompute color bank masks/shifts
    switch (mode.colorMode) {
    case 0: lineParams.colorBank &= 0xFFF0; break; // 4 bpp, 16 colors, bank mode
    case 1: lineParams.colorBank <<= 3u; break;    // 4 bpp, 16 colors, lookup table mode
    case 2: lineParams.colorBank &= 0xFFC0; break; // 8 bpp, 64 colors, bank mode
    case 3: lineParams.colorBank &= 0xFF80; break; // 8 bpp, 128 colors, bank mode
    case 4: lineParams.colorBank &= 0xFF00; break; // 8 bpp, 256 colors, bank mode
    case 5: break;                                 // 16 bpp, 32768 colors, RGB mode
    }

    QuadStepper quad{coordA, coordB, coordC, coordD};

    if (mode.gouraudEnable) {
        const uint32 gouraudTable = static_cast<uint32>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x1C)) << 3u;

        Color555 colorA{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 0u)};
        Color555 colorB{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 2u)};
        Color555 colorC{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 4u)};
        Color555 colorD{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 6u)};

        devlog::trace<grp::swvdp1_cmd>("[{:05X}] Gouraud colors: ({},{},{}) ({},{},{}) ({},{},{}) ({},{},{})",
                                       gouraudTable, (uint8)colorA.r, (uint8)colorA.g, (uint8)colorA.b, (uint8)colorB.r,
                                       (uint8)colorB.g, (uint8)colorB.b, (uint8)colorC.r, (uint8)colorC.g,
                                       (uint8)colorC.b, (uint8)colorD.r, (uint8)colorD.g, (uint8)colorD.b);

        quad.SetupGouraud(colorA, colorB, colorC, colorD);
        lineParams.gouraudLeft = &quad.LeftEdge().Gouraud();
        lineParams.gouraudRight = &quad.RightEdge().Gouraud();
    }

    // A width of zero results in the first fetched texel being used for the entire texture.
    // We simulate this by reducing the texture height to one.
    const bool flipV = control.flipV;
    quad.SetupTexture(lineParams.texVStepper, charSizeH == 0 ? 1 : charSizeV, flipV);

    // Optimization for the case where the quad goes outside the system clipping area.
    // Skip rendering the rest of the quad when a line is clipped after plotting at least one line.
    // The first few lines of the quad could also be clipped; that is accounted for by requiring at least one
    // plotted line. The point is to skip the calculations once the quad iterator reaches a point where no more lines
    // can be plotted because they all sit outside the system clip area.
    //
    // This also handles a degenerate case with a bowtie quad sitting outside the corner of the screen with two points
    // poking into the screen area in a configuration similar to this:
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
    // In this case, the line gets fully clipped partway through the quad, but comes back into view at the end, so we
    // need to check for two sequences of plotted lines rather than one.
    bool linePlotted = false;
    int plottedSegmentsCount = 0;
    const int plottedSegmentsMax = quad.IsDegenerate() ? 2 : 1;

    const VDP1Regs &regs1 = VDP1GetRegs();
    const VDP2Regs &regs2 = VDP2GetRegs();
    const bool doubleDensity = regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity;

    // Interpolate linearly over edges A-D and B-C
    for (; quad.CanStep(); quad.Step()) {
        // Plot lines between the interpolated points
        const CoordS32 coordL = quad.LeftEdge().Coord();
        const CoordS32 coordR = quad.RightEdge().Coord();

        while (lineParams.texVStepper.ShouldStepTexel()) {
            lineParams.texVStepper.StepTexel();
        }
        lineParams.texVStepper.StepPixel();

        if (mode.gouraudEnable) {
            lineParams.gouraudLeft = &quad.LeftEdge().Gouraud();
            lineParams.gouraudRight = &quad.RightEdge().Gouraud();
        }

        if (VDP1PlotTexturedLine<deinterlace, transparentMeshes>(coordL, coordR, lineParams, regs1, doubleDensity)) {
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

template <bool deinterlace, bool transparentMeshes>
FORCE_INLINE void SoftwareVDPRenderer::VDP1Cmd_Handle(uint32 cmdAddress, VDP1Command::Control control) {
    using enum VDP1Command::CommandType;

    switch (control.command) {
    case DrawNormalSprite: VDP1Cmd_DrawNormalSprite<deinterlace, transparentMeshes>(cmdAddress, control); break;
    case DrawScaledSprite: VDP1Cmd_DrawScaledSprite<deinterlace, transparentMeshes>(cmdAddress, control); break;
    case DrawDistortedSprite: [[fallthrough]];
    case DrawDistortedSpriteAlt:
        VDP1Cmd_DrawDistortedSprite<deinterlace, transparentMeshes>(cmdAddress, control);
        break;

    case DrawPolygon: VDP1Cmd_DrawPolygon<deinterlace, transparentMeshes>(cmdAddress); break;
    case DrawPolylines: [[fallthrough]];
    case DrawPolylinesAlt: VDP1Cmd_DrawPolylines<deinterlace, transparentMeshes>(cmdAddress); break;
    case DrawLine: VDP1Cmd_DrawLine<deinterlace, transparentMeshes>(cmdAddress); break;

    case UserClipping: [[fallthrough]];
    case UserClippingAlt: VDP1Cmd_SetUserClipping(cmdAddress); break;
    case SystemClipping: VDP1Cmd_SetSystemClipping(cmdAddress); break;
    case SetLocalCoordinates: VDP1Cmd_SetLocalCoordinates(cmdAddress); break;
    }
}

template <bool deinterlace, bool transparentMeshes>
void SoftwareVDPRenderer::VDP1Cmd_DrawNormalSprite(uint32 cmdAddress, VDP1Command::Control control) {
    if (!m_state.state2.layerEnabled[0]) {
        return;
    }

    const VDP1Command::Size size{.u16 = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0A)};
    const uint32 charSizeH = size.H * 8;
    const uint32 charSizeV = size.V;

    auto &ctx = m_state.state1;
    const sint32 xa = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0C)) + ctx.localCoordX;
    const sint32 ya = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0E)) + ctx.localCoordY;

    const sint32 xb = xa + std::max(charSizeH, 1u) - 1u; // right X
    const sint32 yb = ya + std::max(charSizeV, 1u) - 1u; // bottom Y

    const sint32 doubleV = m_VDP1doubleV;
    const sint32 yAdd = deinterlace ? doubleV : 0;

    const CoordS32 coordA{xa, ya << doubleV};
    const CoordS32 coordB{xb, ya << doubleV};
    const CoordS32 coordC{xb, (yb << doubleV) + yAdd};
    const CoordS32 coordD{xa, (yb << doubleV) + yAdd};

    devlog::trace<grp::swvdp1_cmd>("[{:05X}] Draw normal sprite: {:3d}x{:<3d} {:3d}x{:<3d} {:3d}x{:<3d} {:3d}x{:<3d}",
                                   cmdAddress, xa, ya, xb, ya, xb, yb, xa, yb);

    VDP1PlotTexturedQuad<deinterlace, transparentMeshes>(cmdAddress, control, size, coordA, coordB, coordC, coordD);
}

template <bool deinterlace, bool transparentMeshes>
void SoftwareVDPRenderer::VDP1Cmd_DrawScaledSprite(uint32 cmdAddress, VDP1Command::Control control) {
    if (!m_state.state2.layerEnabled[0]) {
        return;
    }

    const VDP1Command::Size size{.u16 = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0A)};

    auto &ctx = m_state.state1;
    const sint32 xa = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0C));
    const sint32 ya = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0E));

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
        const sint32 xc = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x14));

        qxb = xc;
        qxc = xc;
    } else {
        const sint32 xb = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x10));

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
        const sint32 yc = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x16));

        qyc = yc;
        qyd = yc;
    } else {
        const sint32 yb = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x12));

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

    qxa += ctx.localCoordX;
    qya += ctx.localCoordY;
    qxb += ctx.localCoordX;
    qyb += ctx.localCoordY;
    qxc += ctx.localCoordX;
    qyc += ctx.localCoordY;
    qxd += ctx.localCoordX;
    qyd += ctx.localCoordY;

    const sint32 doubleV = m_VDP1doubleV;
    const sint32 yAdd = deinterlace ? doubleV : 0;

    const CoordS32 coordA{qxa, qya << doubleV};
    const CoordS32 coordB{qxb, qyb << doubleV};
    const CoordS32 coordC{qxc, (qyc << doubleV) + yAdd};
    const CoordS32 coordD{qxd, (qyd << doubleV) + yAdd};

    devlog::trace<grp::swvdp1_cmd>("[{:05X}] Draw scaled sprite: {:3d}x{:<3d} {:3d}x{:<3d} {:3d}x{:<3d} {:3d}x{:<3d}",
                                   cmdAddress, qxa, qya, qxb, qyb, qxc, qyc, qxd, qyd);

    VDP1PlotTexturedQuad<deinterlace, transparentMeshes>(cmdAddress, control, size, coordA, coordB, coordC, coordD);
}

template <bool deinterlace, bool transparentMeshes>
void SoftwareVDPRenderer::VDP1Cmd_DrawDistortedSprite(uint32 cmdAddress, VDP1Command::Control control) {
    if (!m_state.state2.layerEnabled[0]) {
        return;
    }

    const VDP1Command::Size size{.u16 = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0A)};

    auto &ctx = m_state.state1;
    const sint32 xa = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0C)) + ctx.localCoordX;
    const sint32 ya = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0E)) + ctx.localCoordY;
    const sint32 xb = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x10)) + ctx.localCoordX;
    const sint32 yb = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x12)) + ctx.localCoordY;
    const sint32 xc = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x14)) + ctx.localCoordX;
    const sint32 yc = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x16)) + ctx.localCoordY;
    const sint32 xd = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x18)) + ctx.localCoordX;
    const sint32 yd = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x1A)) + ctx.localCoordY;

    const bool isRegularRect = (xa == xd) && (xb == xc) && (ya == yb) && (yc == yd);

    const sint32 doubleV = m_VDP1doubleV;
    const sint32 yAddAB = deinterlace && isRegularRect && (ya >= yc) ? doubleV : 0;
    const sint32 yAddCD = deinterlace && isRegularRect && (ya < yc) ? doubleV : 0;

    const CoordS32 coordA{xa, (ya << doubleV) + yAddAB};
    const CoordS32 coordB{xb, (yb << doubleV) + yAddAB};
    const CoordS32 coordC{xc, (yc << doubleV) + yAddCD};
    const CoordS32 coordD{xd, (yd << doubleV) + yAddCD};

    devlog::trace<grp::swvdp1_cmd>(
        "[{:05X}] Draw distorted sprite: {:6d}x{:<6d} {:6d}x{:<6d} {:6d}x{:<6d} {:6d}x{:<6d}", cmdAddress, xa, ya, xb,
        yb, xc, yc, xd, yd);

    VDP1PlotTexturedQuad<deinterlace, transparentMeshes>(cmdAddress, control, size, coordA, coordB, coordC, coordD);
}

template <bool deinterlace, bool transparentMeshes>
void SoftwareVDPRenderer::VDP1Cmd_DrawPolygon(uint32 cmdAddress) {
    if (!m_state.state2.layerEnabled[0]) {
        return;
    }

    auto &ctx = m_state.state1;
    const VDP1Command::DrawMode mode{.u16 = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x04)};

    const uint16 color = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x06);
    const sint32 xa = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0C)) + ctx.localCoordX;
    const sint32 ya = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0E)) + ctx.localCoordY;
    const sint32 xb = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x10)) + ctx.localCoordX;
    const sint32 yb = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x12)) + ctx.localCoordY;
    const sint32 xc = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x14)) + ctx.localCoordX;
    const sint32 yc = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x16)) + ctx.localCoordY;
    const sint32 xd = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x18)) + ctx.localCoordX;
    const sint32 yd = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x1A)) + ctx.localCoordY;
    const uint32 gouraudTable = static_cast<uint32>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x1C)) << 3u;

    const bool isRegularRect = (xa == xd) && (xb == xc) && (ya == yb) && (yc == yd);

    const sint32 doubleV = m_VDP1doubleV;
    const sint32 yAddAB = deinterlace && isRegularRect && (ya >= yc) ? doubleV : 0;
    const sint32 yAddCD = deinterlace && isRegularRect && (ya < yc) ? doubleV : 0;

    const CoordS32 coordA{xa, (ya << doubleV) + yAddAB};
    const CoordS32 coordB{xb, (yb << doubleV) + yAddAB};
    const CoordS32 coordC{xc, (yc << doubleV) + yAddCD};
    const CoordS32 coordD{xd, (yd << doubleV) + yAddCD};

    devlog::trace<grp::swvdp1_cmd>("[{:05X}] Draw polygon: {:6d}x{:<6d} {:6d}x{:<6d} {:6d}x{:<6d} {:6d}x{:<6d}, color "
                                   "{:04X}, gouraud table {:05X}, CMDPMOD = {:04X}",
                                   cmdAddress, xa, ya, xb, yb, xc, yc, xd, yd, color, gouraudTable, mode.u16);

    if (VDP1IsQuadSystemClipped<deinterlace>(coordA, coordB, coordC, coordD)) {
        return;
    }

    VDP1LineParams lineParams{
        .mode = mode,
        .color = color,
    };

    QuadStepper quad{coordA, coordB, coordC, coordD};

    if (mode.gouraudEnable) {
        Color555 colorA{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 0u)};
        Color555 colorB{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 2u)};
        Color555 colorC{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 4u)};
        Color555 colorD{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 6u)};

        devlog::trace<grp::swvdp1_cmd>("Gouraud colors: ({},{},{}) ({},{},{}) ({},{},{}) ({},{},{})", (uint8)colorA.r,
                                       (uint8)colorA.g, (uint8)colorA.b, (uint8)colorB.r, (uint8)colorB.g,
                                       (uint8)colorB.b, (uint8)colorC.r, (uint8)colorC.g, (uint8)colorC.b,
                                       (uint8)colorD.r, (uint8)colorD.g, (uint8)colorD.b);

        quad.SetupGouraud(colorA, colorB, colorC, colorD);
    }

    // Optimization for the case where the quad goes outside the system clipping area.
    // Skip rendering the rest of the quad when a line is clipped after plotting at least one line.
    // The first few lines of the quad could also be clipped; that is accounted for by requiring at least one
    // plotted line. The point is to skip the calculations once the quad iterator reaches a point where no more lines
    // can be plotted because they all sit outside the system clip area.
    //
    // This also handles a degenerate case with a bowtie quad sitting outside the corner of the screen with two points
    // poking into the screen area in a configuration similar to this:
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
    // In this case, the line gets fully clipped partway through the quad, but comes back into view at the end, so we
    // need to check for two sequences of plotted lines rather than one.
    bool linePlotted = false;
    int plottedSegmentsCount = 0;
    const int plottedSegmentsMax = quad.IsDegenerate() ? 2 : 1;

    const VDP1Regs &regs1 = VDP1GetRegs();
    const VDP2Regs &regs2 = VDP2GetRegs();
    const bool doubleDensity = regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity;

    // Interpolate linearly over edges A-D and B-C
    for (; quad.CanStep(); quad.Step()) {
        // Plot lines between the interpolated points
        const CoordS32 coordL = quad.LeftEdge().Coord();
        const CoordS32 coordR = quad.RightEdge().Coord();

        if (mode.gouraudEnable) {
            lineParams.gouraudLeft = quad.LeftEdge().GouraudValue();
            lineParams.gouraudRight = quad.RightEdge().GouraudValue();
        }

        if (VDP1PlotLine<true, deinterlace, transparentMeshes>(coordL, coordR, lineParams, regs1, doubleDensity)) {
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

template <bool deinterlace, bool transparentMeshes>
void SoftwareVDPRenderer::VDP1Cmd_DrawPolylines(uint32 cmdAddress) {
    if (!m_state.state2.layerEnabled[0]) {
        return;
    }

    auto &ctx = m_state.state1;
    const VDP1Command::DrawMode mode{.u16 = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x04)};

    const uint16 color = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x06);
    const sint32 xa = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0C)) + ctx.localCoordX;
    const sint32 ya = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0E)) + ctx.localCoordY;
    const sint32 xb = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x10)) + ctx.localCoordX;
    const sint32 yb = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x12)) + ctx.localCoordY;
    const sint32 xc = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x14)) + ctx.localCoordX;
    const sint32 yc = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x16)) + ctx.localCoordY;
    const sint32 xd = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x18)) + ctx.localCoordX;
    const sint32 yd = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x1A)) + ctx.localCoordY;
    const uint32 gouraudTable = static_cast<uint32>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x1C)) << 3u;

    const bool isRegularRect = (xa == xd) && (xb == xc) && (ya == yb) && (yc == yd);

    const sint32 doubleV = m_VDP1doubleV;
    const sint32 yAddAB = deinterlace && isRegularRect && (ya >= yc) ? doubleV : 0;
    const sint32 yAddCD = deinterlace && isRegularRect && (ya < yc) ? doubleV : 0;

    const CoordS32 coordA{xa, (ya << doubleV) + yAddAB};
    const CoordS32 coordB{xb, (yb << doubleV) + yAddAB};
    const CoordS32 coordC{xc, (yc << doubleV) + yAddCD};
    const CoordS32 coordD{xd, (yd << doubleV) + yAddCD};

    devlog::trace<grp::swvdp1_cmd>(
        "[{:05X}] Draw polylines: {}x{} - {}x{} - {}x{} - {}x{}, color {:04X}, gouraud table {:05X}, CMDPMOD = {:04X}",
        cmdAddress, xa, ya, xb, yb, xc, yc, xd, yd, color, gouraudTable >> 3u, mode.u16);

    if (VDP1IsQuadSystemClipped<deinterlace>(coordA, coordB, coordC, coordD)) {
        return;
    }

    VDP1LineParams lineParams{
        .mode = mode,
        .color = color,
    };

    const Color555 gouraudA{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 0u)};
    const Color555 gouraudB{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 2u)};
    const Color555 gouraudC{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 4u)};
    const Color555 gouraudD{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 6u)};
    devlog::trace<grp::swvdp1_cmd>("Gouraud colors: ({},{},{}) ({},{},{}) ({},{},{}) ({},{},{})", (uint8)gouraudA.r,
                                   (uint8)gouraudA.g, (uint8)gouraudA.b, (uint8)gouraudB.r, (uint8)gouraudB.g,
                                   (uint8)gouraudB.b, (uint8)gouraudC.r, (uint8)gouraudC.g, (uint8)gouraudC.b,
                                   (uint8)gouraudD.r, (uint8)gouraudD.g, (uint8)gouraudD.b);

    const VDP1Regs &regs1 = VDP1GetRegs();
    const VDP2Regs &regs2 = VDP2GetRegs();
    const bool doubleDensity = regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity;

    if (mode.gouraudEnable) {
        lineParams.gouraudLeft = gouraudA;
        lineParams.gouraudRight = gouraudB;
    }
    VDP1PlotLine<false, deinterlace, transparentMeshes>(coordA, coordB, lineParams, regs1, doubleDensity);
    if (mode.gouraudEnable) {
        lineParams.gouraudLeft = gouraudB;
        lineParams.gouraudRight = gouraudC;
    }
    VDP1PlotLine<false, deinterlace, transparentMeshes>(coordB, coordC, lineParams, regs1, doubleDensity);
    if (mode.gouraudEnable) {
        lineParams.gouraudLeft = gouraudC;
        lineParams.gouraudRight = gouraudD;
    }
    VDP1PlotLine<false, deinterlace, transparentMeshes>(coordC, coordD, lineParams, regs1, doubleDensity);
    if (mode.gouraudEnable) {
        lineParams.gouraudLeft = gouraudD;
        lineParams.gouraudRight = gouraudA;
    }
    VDP1PlotLine<false, deinterlace, transparentMeshes>(coordD, coordA, lineParams, regs1, doubleDensity);
}

template <bool deinterlace, bool transparentMeshes>
void SoftwareVDPRenderer::VDP1Cmd_DrawLine(uint32 cmdAddress) {
    if (!m_state.state2.layerEnabled[0]) {
        return;
    }

    auto &ctx = m_state.state1;
    const VDP1Command::DrawMode mode{.u16 = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x04)};

    const uint16 color = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x06);
    const sint32 xa = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0C)) + ctx.localCoordX;
    const sint32 ya = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0E)) + ctx.localCoordY;
    const sint32 xb = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x10)) + ctx.localCoordX;
    const sint32 yb = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x12)) + ctx.localCoordY;
    const uint32 gouraudTable = static_cast<uint32>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x1C)) << 3u;

    const sint32 doubleV = m_VDP1doubleV;
    const CoordS32 coordA{xa, ya << doubleV};
    const CoordS32 coordB{xb, yb << doubleV};

    devlog::trace<grp::swvdp1_cmd>(
        "[{:05X}] Draw line: {}x{} - {}x{}, color {:04X}, gouraud table {:05X}, CMDPMOD = {:04X}", cmdAddress, xa, ya,
        xb, yb, color, gouraudTable, mode.u16);

    if (VDP1IsLineSystemClipped<deinterlace>(coordA, coordB)) {
        return;
    }

    VDP1LineParams lineParams{
        .mode = mode,
        .color = color,
    };

    const VDP1Regs &regs1 = VDP1GetRegs();
    const VDP2Regs &regs2 = VDP2GetRegs();
    const bool doubleDensity = regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity;

    if (mode.gouraudEnable) {
        const Color555 colorA{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 0u)};
        const Color555 colorB{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 2u)};

        lineParams.gouraudLeft = colorA;
        lineParams.gouraudRight = colorB;

        devlog::trace<grp::swvdp1_cmd>("Gouraud colors: ({},{},{}) ({},{},{})", (uint8)colorA.r, (uint8)colorA.g,
                                       (uint8)colorA.b, (uint8)colorB.r, (uint8)colorB.g, (uint8)colorB.b);
    }

    VDP1PlotLine<false, deinterlace, transparentMeshes>(coordA, coordB, lineParams, regs1, doubleDensity);
}

void SoftwareVDPRenderer::VDP1Cmd_SetSystemClipping(uint32 cmdAddress) {
    auto &ctx = m_state.state1;
    ctx.sysClipH = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x14);
    ctx.sysClipV = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x16);
    devlog::trace<grp::swvdp1_cmd>("[{:05X}] Set system clipping: {}x{}", cmdAddress, ctx.sysClipH, ctx.sysClipV);
}

void SoftwareVDPRenderer::VDP1Cmd_SetUserClipping(uint32 cmdAddress) {
    auto &ctx = m_state.state1;
    ctx.userClipX0 = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0C);
    ctx.userClipY0 = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0E);
    ctx.userClipX1 = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x14);
    ctx.userClipY1 = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x16);
    devlog::trace<grp::swvdp1_cmd>("[{:05X}] Set user clipping: {}x{} - {}x{}", cmdAddress, ctx.userClipX0,
                                   ctx.userClipY0, ctx.userClipX1, ctx.userClipY1);
}

void SoftwareVDPRenderer::VDP1Cmd_SetLocalCoordinates(uint32 cmdAddress) {
    auto &ctx = m_state.state1;
    ctx.localCoordX = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0C));
    ctx.localCoordY = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0E));
    devlog::trace<grp::swvdp1_cmd>("[{:05X}] Set local coordinates: {}x{}", cmdAddress, ctx.localCoordX,
                                   ctx.localCoordY);
}

// -----------------------------------------------------------------------------
// VDP2

FORCE_INLINE VDP2Regs &SoftwareVDPRenderer::VDP2GetRegs() {
    if (m_threadedVDP2Rendering) {
        return m_vdp2RenderingContext.vdp2.regs;
    } else {
        return m_state.regs2;
    }
}

FORCE_INLINE const VDP2Regs &SoftwareVDPRenderer::VDP2GetRegs() const {
    if (m_threadedVDP2Rendering) {
        return m_vdp2RenderingContext.vdp2.regs;
    } else {
        return m_state.regs2;
    }
}

const VDP1RegTVMR &SoftwareVDPRenderer::VDP2GetVDP1TVMR() const {
    if (m_threadedVDP2Rendering) {
        return m_vdp2RenderingContext.tvmr;
    } else {
        return m_state.regs1.tvmr;
    }
}

FORCE_INLINE std::array<uint8, kVDP2VRAMSize> &SoftwareVDPRenderer::VDP2GetVRAM() {
    if (m_threadedVDP2Rendering) {
        return m_vdp2RenderingContext.vdp2.mem.VRAM;
    } else {
        return m_state.mem2.VRAM;
    }
}

void SoftwareVDPRenderer::VDP2InitFrame() {
    const VDP2Regs &regs2 = VDP2GetRegs();
    if (!regs2.bgEnabled[5]) {
        VDP2InitNormalBG<0>(regs2);
    }
    VDP2InitNormalBG<1>(regs2);
    VDP2InitNormalBG<2>(regs2);
    VDP2InitNormalBG<3>(regs2);
}

template <uint32 index>
FORCE_INLINE void SoftwareVDPRenderer::VDP2InitNormalBG(const VDP2Regs &regs2) {
    static_assert(index < 4, "Invalid NBG index");

    VDP2State &state2 = m_state.state2;
    const BGParams &bgParams = regs2.bgParams[index + 1];
    NBGLayerState &bgState = state2.nbgLayerStates[index];
    bgState.fracScrollX = 0;
    bgState.fracScrollY = 0;
    if (!m_enhancements.deinterlace && regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity && regs2.TVSTAT.ODD) {
        bgState.fracScrollY += bgParams.scrollIncV;
    }

    bgState.mosaicCounterY = 0;
    if constexpr (index < 2) {
        bgState.lineScrollTableAddress = bgParams.lineScrollTableAddress;
    }
}

FORCE_INLINE void SoftwareVDPRenderer::VDP2UpdateEnabledBGs() {
    m_state.state2.UpdateEnabledBGs(VDP2GetRegs(), m_vdp2DebugRenderOptions);
}

FORCE_INLINE void SoftwareVDPRenderer::VDP2UpdateLineScreenScrollParams(uint32 y, const VDP2Regs &regs2) {
    for (uint32 i = 0; i < 2; ++i) {
        const BGParams &bgParams = regs2.bgParams[i + 1];
        NBGLayerState &bgState = m_state.state2.nbgLayerStates[i];
        VDP2UpdateLineScreenScroll(y, regs2, bgParams, bgState);
    }
}

FORCE_INLINE void SoftwareVDPRenderer::VDP2UpdateLineScreenScroll(uint32 y, const VDP2Regs &regs2,
                                                                  const BGParams &bgParams, NBGLayerState &bgState) {
    bgState.scrollIncH = bgParams.scrollIncH;

    if ((y & ((1u << bgParams.lineScrollInterval) - 1)) != 0) {
        return;
    }

    uint32 address = bgState.lineScrollTableAddress;
    auto read = [&] {
        const uint32 value = VDP2ReadRendererVRAM<uint32>(address);
        address += sizeof(uint32);
        return value;
    };

    size_t count = 1;
    if (regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity &&
        (y > 0 || (!m_enhancements.deinterlace && regs2.TVSTAT.ODD))) {
        ++count;
    }
    for (size_t i = 0; i < count; ++i) {
        if (bgParams.lineScrollXEnable) {
            bgState.fracScrollX = bit::extract<8, 26>(read());
        }
        if (bgParams.lineScrollYEnable) {
            bgState.fracScrollY = bit::extract<8, 26>(read());
        }
        if (bgParams.lineZoomEnable) {
            bgState.scrollIncH = bit::extract<8, 18>(read());
        }
    }
    bgState.lineScrollTableAddress = address;
}

FORCE_INLINE void SoftwareVDPRenderer::VDP2CalcRotationParameterTables(uint32 y, VDP2Regs &regs2) {
    const VDP1RegTVMR &tvmr = VDP2GetVDP1TVMR();
    const uint32 baseAddress = regs2.commonRotParams.baseAddress & 0xFFF7C; // mask bit 6 (shifted left by 1)
    const bool readAll = y == 0;
    const auto &vram2 = VDP2GetVRAM();

    for (int i = 0; i < 2; i++) {
        RotationParams &params = regs2.rotParams[i];
        RotationParamState &state = m_state.state2.rotParamStates[i];
        RotationParamLineOutput &lineOut = m_rotParamLineOutputs[i];

        const bool readXst = readAll || params.readXst;
        const bool readYst = readAll || params.readYst;
        const bool readKAst = readAll || params.readKAst;

        // Tables are located at the base address 0x80 bytes apart
        RotationParamTable t{};
        const uint32 address = baseAddress + i * 0x80;
        t.ReadFrom(&vram2[address & 0x7FFFF]);

        // Calculate parameters

        if (readXst) {
            state.Xst = t.Xst;
            params.readXst = false;
        } else {
            state.Xst += t.deltaXst;
        }
        if (readYst) {
            state.Yst = t.Yst;
            params.readYst = false;
        } else {
            state.Yst += t.deltaYst;
        }
        if (readKAst) {
            state.KA = params.coeffTableAddressOffset + t.KAst;
            params.readKAst = false;
        } else {
            state.KA += t.dKAst;
        }

        // Transformed starting screen coordinates (18.10)
        // 10*(10-10) + 10*(10-10) + 10*(10-10) = 20 frac bits
        // 14*(23-24) + 14*(23-24) + 14*(23-24) = 38 total bits
        // reduce to 10 frac bits
        const sint32 Xsp = (static_cast<sint64>(t.A) * (state.Xst - (t.Px << 10)) +
                            static_cast<sint64>(t.B) * (state.Yst - (t.Py << 10)) +
                            static_cast<sint64>(t.C) * (t.Zst - (t.Pz << 10))) >>
                           10;
        const sint32 Ysp = (static_cast<sint64>(t.D) * (state.Xst - (t.Px << 10)) +
                            static_cast<sint64>(t.E) * (state.Yst - (t.Py << 10)) +
                            static_cast<sint64>(t.F) * (t.Zst - (t.Pz << 10))) >>
                           10;

        // Transformed view coordinates (18.10)
        // 10*(0-0) + 10*(0-0) + 10*(0-0) + 10 + 10 = 10+10+10 + 10+10 = 10 frac bits
        // 14*(14-14) + 14*(14-14) + 14*(14-14) + 24 + 24 = 28+28+28 + 24+24 = 28 total bits
        /***/ sint32 Xp = (t.A * (t.Px - t.Cx) + t.B * (t.Py - t.Cy) + t.C * (t.Pz - t.Cz)) + (t.Cx << 10) + t.Mx;
        const sint32 Yp = (t.D * (t.Px - t.Cx) + t.E * (t.Py - t.Cy) + t.F * (t.Pz - t.Cz)) + (t.Cy << 10) + t.My;

        // Screen coordinate increments per Hcnt (7.10)
        // 10*10 + 10*10 = 20 + 20 = 20 frac bits
        // 14*13 + 14*13 = 27 + 27 = 27 total bits
        // reduce to 10 frac bits
        const sint32 scrXIncH = (t.A * t.deltaX + t.B * t.deltaY) >> 10;
        const sint32 scrYIncH = (t.D * t.deltaX + t.E * t.deltaY) >> 10;

        // Scaling factors (8.16)
        sint64 kx = t.kx;
        sint64 ky = t.ky;

        // Current screen coordinates (18.10)
        sint32 scrX = Xsp;
        sint32 scrY = Ysp;

        // Current coefficient address (16.10)
        uint32 KA = state.KA;

        const bool doubleResH = m_HRes > kMaxNormalResH;
        const uint32 xShift = doubleResH ? 1 : 0;
        const uint32 maxX = m_HRes >> xShift;

        // Use per-dot coefficient if reading from CRAM or if any of the VRAM banks was designated as coefficient data
        const bool perDotCoeff = regs2.vramControl.perDotRotationCoeffs;

        // Precompute line color data parameters
        const LineBackScreenParams &lineParams = regs2.lineScreenParams;
        const uint32 line = lineParams.perLine ? y : 0;
        const uint32 lineColorAddress = lineParams.baseAddress + line * sizeof(uint16);
        const uint32 baseLineColorData = bit::extract<7, 10>(VDP2ReadRendererVRAM<uint16>(lineColorAddress)) << 7;

        // Fetch first coefficient if possible
        Coefficient coeff{};
        if (!perDotCoeff || VDP2CanFetchCoefficient(regs2, params, KA)) {
            coeff = VDP2FetchRotationCoefficient(regs2, params, KA);
        }

        auto writeScreenCoord = [&](uint32 x) {
            // Resulting screen coordinates (26.0)
            // (16*10) + 10 = 26 + 10 frac bits
            // (24*28) + 28 = 52 + 28 total bits
            // reduce 26 to 10 frac bits
            // = 10 + 10 = 10 frac bits
            // = 36 + 28 = 36 total bits
            // remove frac bits from result = 26 total bits
            lineOut.screenCoords[x].x() = (((kx * scrX) >> 16) + Xp) >> 10;
            lineOut.screenCoords[x].y() = (((ky * scrY) >> 16) + Yp) >> 10;

            // Increment screen coordinates and coefficient table address by Hcnt
            scrX += scrXIncH;
            scrY += scrYIncH;
        };

        // Precompute whole line of screen coordinates
        if (params.coeffTableEnable && perDotCoeff) {
            std::array<sint32, kMaxNormalResH> coeffValues;
            for (uint32 x = 0; x < maxX; x++) {
                coeffValues[x] = coeff.value;

                // Apply coefficient transparency
                lineOut.transparent[x] = coeff.transparent;

                // Store line colors
                if (params.coeffUseLineColorData) {
                    const uint32 cramAddress = baseLineColorData | coeff.lineColorData;
                    lineOut.lineColor[x] = VDP2ReadRendererColor5to8(cramAddress * sizeof(uint16));
                }

                // Increment coefficient table address by Hcnt since we're using per-dot coefficients
                KA += t.dKAx;
                if (VDP2CanFetchCoefficient(regs2, params, KA)) {
                    coeff = VDP2FetchRotationCoefficient(regs2, params, KA);
                }
            }

            using enum CoefficientDataMode;
            switch (params.coeffDataMode) {
            case ScaleCoeffXY:
                for (uint32 x = 0; x < maxX; x++) {
                    kx = ky = coeffValues[x];
                    writeScreenCoord(x);
                }
                break;
            case ScaleCoeffX:
                for (uint32 x = 0; x < maxX; x++) {
                    kx = coeffValues[x];
                    writeScreenCoord(x);
                }
                break;
            case ScaleCoeffY:
                for (uint32 x = 0; x < maxX; x++) {
                    ky = coeffValues[x];
                    writeScreenCoord(x);
                }
                break;
            case ViewpointX:
                for (uint32 x = 0; x < maxX; x++) {
                    Xp = coeffValues[x] << 2;
                    writeScreenCoord(x);
                }
                break;
            }
        } else {
            if (params.coeffTableEnable) {
                using enum CoefficientDataMode;
                switch (params.coeffDataMode) {
                case ScaleCoeffXY: kx = ky = coeff.value; break;
                case ScaleCoeffX: kx = coeff.value; break;
                case ScaleCoeffY: ky = coeff.value; break;
                case ViewpointX: Xp = coeff.value << 2; break;
                }

                std::fill_n(lineOut.transparent.begin(), maxX, coeff.transparent);

                // Compute line colors
                if (params.coeffUseLineColorData) {
                    const uint32 cramAddress = baseLineColorData | coeff.lineColorData;
                    std::fill_n(lineOut.lineColor.begin(), maxX,
                                VDP2ReadRendererColor5to8(cramAddress * sizeof(uint16)));
                }
            }

            for (uint32 x = 0; x < maxX; x++) {
                writeScreenCoord(x);
            }
        }

        // Precompute whole line of sprite coordinates
        if (tvmr.fbRotEnable && i == 0) {
            // Current sprite coordinates (13.10)
            sint32 sprX = t.Xst + y * t.deltaXst;
            sint32 sprY = t.Yst + y * t.deltaYst;

            for (uint32 x = 0; x < maxX; x++) {
                // Resulting sprite coordinates (13.0)
                lineOut.spriteCoords[x].x() = sprX >> 10ll;
                lineOut.spriteCoords[x].y() = sprY >> 10ll;

                // Increment sprite coordinates by Hcnt
                sprX += t.deltaX;
                sprY += t.deltaY;
            }
        }
    }
}

template <bool deinterlace, bool altField>
FORCE_INLINE void SoftwareVDPRenderer::VDP2CalcWindows(uint32 y, const VDP2Regs &regs2) {
    y = VDP2GetY<deinterlace>(y, regs2) ^ altField;

    // Calculate window for NBGs and RBGs
    for (int i = 0; i < 5; i++) {
        auto &bgParams = regs2.bgParams[i];
        auto &bgWindow = m_bgWindows[altField][i];

        VDP2CalcWindow<altField>(y, regs2, bgParams.windowSet, std::span{bgWindow}.first(m_HRes));
    }

    // Calculate window for rotation parameters
    VDP2CalcWindow<altField>(y, regs2, regs2.commonRotParams.windowSet,
                             std::span{m_rotParamsWindow[altField]}.first(m_HRes));

    // Calculate window for color calculations
    VDP2CalcWindow<altField>(y, regs2, regs2.colorCalcParams.windowSet,
                             std::span{m_colorCalcWindow[altField]}.first(m_HRes));
}

template <bool altField, bool hasSpriteWindow>
FORCE_INLINE void SoftwareVDPRenderer::VDP2CalcWindow(uint32 y, const VDP2Regs &regs2,
                                                      const WindowSet<hasSpriteWindow> &windowSet,
                                                      std::span<bool> windowState) {
    // If no windows are enabled, consider the pixel outside of windows
    if (!std::any_of(windowSet.enabled.begin(), windowSet.enabled.end(), std::identity{})) {
        std::fill(windowState.begin(), windowState.end(), false);
        return;
    }

    if (windowSet.logic == WindowLogic::And) {
        VDP2CalcWindowLogic<altField, false>(y, regs2, windowSet, windowState);
    } else {
        VDP2CalcWindowLogic<altField, true>(y, regs2, windowSet, windowState);
    }
}

template <bool altField, bool logicOR, bool hasSpriteWindow>
FORCE_INLINE void SoftwareVDPRenderer::VDP2CalcWindowLogic(uint32 y, const VDP2Regs &regs2,
                                                           const WindowSet<hasSpriteWindow> &windowSet,
                                                           std::span<bool> windowState) {
    // Initialize to all inside if using AND logic or all outside if using OR logic
    std::fill(windowState.begin(), windowState.end(), !logicOR);

    const uint16 doubleV = regs2.TVMD.LSMDn == InterlaceMode::SingleDensity;

    // Check normal windows
    for (int i = 0; i < 2; i++) {
        // Skip if disabled
        if (!windowSet.enabled[i]) {
            continue;
        }

        const WindowParams &windowParam = regs2.windowParams[i];
        const bool inverted = windowSet.inverted[i];

        // Check vertical coordinate
        //
        // Truth table: (state: false=outside, true=inside)
        // state  inverted  result   st!=inv
        // false  false     outside  false
        // true   false     inside   true
        // false  true      inside   true
        // true   true      outside  false
        //
        // Short-circuiting rules for lines outside the vertical window range:
        // # logic  inverted  outcome
        // 1   AND  false     fill with outside
        // 2   AND  true      skip - window has no effect on this line
        // 3    OR  false     skip - window has no effect on this line
        // 4    OR  true      fill with inside

        const auto sy = static_cast<sint16>(y);
        const auto startY = static_cast<sint16>(windowParam.startY) << doubleV;
        const auto endY = static_cast<sint16>(windowParam.endY) << doubleV;
        if (sy < startY || sy > endY) {
            if (logicOR == inverted) {
                // Cases 1 and 4
                std::fill(windowState.begin(), windowState.end(), logicOR);
                return;
            } else {
                // Cases 2 and 3
                continue;
            }
        }

        sint16 startX = windowParam.startX;
        sint16 endX = windowParam.endX;

        // Read line window if enabled
        if (windowParam.lineWindowTableEnable) {
            const uint32 address = windowParam.lineWindowTableAddress + y * sizeof(uint16) * 2;
            startX = VDP2ReadRendererVRAM<uint16>(address + 0);
            endX = VDP2ReadRendererVRAM<uint16>(address + 2);
        }

        // Some games set out-of-range window parameters and expect them to work.
        // It seems like window coordinates should be signed...
        //
        // Panzer Dragoon 2 Zwei:
        //   0000 to FFFE -> empty window
        //   FFFE to 02C0 -> full line
        //
        // Panzer Dragoon Saga:
        //   0000 to FFFF -> empty window
        //
        // Snatcher:
        //   FFFC to 0286 -> full line
        //
        // Handle these cases here
        if (startX < 0) {
            startX = 0;
        }
        if (endX < 0) {
            if (startX >= endX) {
                startX = 0x3FF;
            }
            endX = 0;
        }

        // For normal screen modes, X coordinates don't use bit 0
        if (regs2.TVMD.HRESOn < 2) {
            startX >>= 1;
            endX >>= 1;
        }

        // Fill in horizontal coordinate
        if (inverted != logicOR) {
            // - fill [startX..endX] with outside if using AND logic and inverted
            // - fill [startX..endX] with inside if using OR logic and not inverted
            if (startX < windowState.size()) {
                endX = std::min<sint16>(endX, windowState.size() - 1);
                if (startX <= endX) {
                    std::fill(windowState.begin() + startX, windowState.begin() + endX + 1, logicOR);
                }
            }
        } else {
            // Fill complement of [startX..endX] with outside if using AND logic or inside if using OR logic
            startX = std::min<sint16>(startX, windowState.size());
            std::fill_n(windowState.begin(), startX, logicOR);
            if (endX < windowState.size()) {
                std::fill(windowState.begin() + endX + 1, windowState.end(), logicOR);
            }
        }
    }

    // Check sprite window
    if constexpr (hasSpriteWindow) {
        if (windowSet.enabled[2]) {
            const bool inverted = windowSet.inverted[2];
            for (uint32 x = 0; x < m_HRes; x++) {
                if constexpr (logicOR) {
                    windowState[x] |= m_spriteLayerAttrs[altField].shadowOrWindow[x] != inverted;
                } else {
                    windowState[x] &= m_spriteLayerAttrs[altField].shadowOrWindow[x] != inverted;
                }
            }
        }
    }
}

FORCE_INLINE void SoftwareVDPRenderer::VDP2PrepareLine(uint32 y) {
    VDP2Regs &regs2 = VDP2GetRegs();

    // Don't waste time processing anything if the display is disabled
    // TODO: check if this is how the real VDP2 behaves
    if (!regs2.displayEnabledLatch) [[unlikely]] {
        return;
    }

    m_state.state2.CalcAccessPatterns(regs2, m_vdp2AccessPatternsConfig);
    m_state.state2.CalcVCellScrollDelay(regs2);
    if (regs2.bgEnabled[4] || regs2.bgEnabled[5]) {
        VDP2CalcRotationParameterTables(y, regs2);
    }
    m_state.state2.UpdateRotationPageBaseAddresses(regs2);
    VDP2DrawLineColorAndBackScreens(y, regs2);
    VDP2UpdateLineScreenScrollParams(y, regs2);

    for (auto &field : m_vramFetchers) {
        for (auto &fetcher : field) {
            fetcher.lastCharIndex = 0xFFFFFFFF;   // force-fetch first character
            fetcher.lastCellX = 0xFF;             // align 2x2 char fetcher
            fetcher.charDataAddress = 0xFFFFFFFF; // force-fetch first character data chunk
        }
    }
}

FORCE_INLINE void SoftwareVDPRenderer::VDP2FinishLine(uint32 y) {
    const VDP2Regs &regs2 = VDP2GetRegs();
    const bool doubleDensity = regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity;

    // Update NBG coordinates
    for (uint32 i = 0; i < 4; ++i) {
        const BGParams &bgParams = regs2.bgParams[i + 1];
        NBGLayerState &bgState = m_state.state2.nbgLayerStates[i];
        bgState.fracScrollY += bgParams.scrollIncV;
        // Update the vertical scroll coordinate twice in double-density interlaced mode.
        // If deinterlacing, the second increment is done after rendering the alternate scanline.
        if (doubleDensity) {
            bgState.fracScrollY += bgParams.scrollIncV;
        }

        // Increment mosaic counter
        if (bgParams.mosaicEnable) {
            ++bgState.mosaicCounterY;
            if (bgState.mosaicCounterY >= regs2.mosaicV) {
                bgState.mosaicCounterY = 0;
            }
        }
    }
}

template <bool deinterlace, bool transparentMeshes>
void SoftwareVDPRenderer::VDP2DrawLine(uint32 y, bool altField) {
    devlog::trace<grp::swvdp2_verbose>("Drawing line {} {} field", y, (altField ? "alt" : "main"));

    const VDP1RegTVMR &tvmr = VDP2GetVDP1TVMR();
    const VDP2Regs &regs2 = VDP2GetRegs();

    using FnDrawLayer = void (SoftwareVDPRenderer::*)(uint32, const VDP2Regs &);

    // Lookup table of sprite drawing functions
    // Indexing: [colorMode][rotate][altField]
    static constexpr auto fnDrawSprite = [] {
        std::array<std::array<std::array<FnDrawLayer, 2>, 2>, 4> arr{};

        util::constexpr_for<2 * 2 * 4>([&](auto index) {
            const uint32 cmIndex = bit::extract<0, 1>(index());
            const uint32 rotIndex = bit::extract<2>(index());
            const uint32 altFieldIndex = bit::extract<3>(index());

            const uint32 colorMode = cmIndex <= 2 ? cmIndex : 2;
            const bool rotate = rotIndex;
            const bool altField = altFieldIndex;
            arr[cmIndex][rotate][altFieldIndex] =
                &SoftwareVDPRenderer::VDP2DrawSpriteLayer<colorMode, rotate, altField, transparentMeshes>;
        });

        return arr;
    }();

    const uint32 colorMode = regs2.vramControl.colorRAMMode;
    const bool rotate = tvmr.fbRotEnable;
    const bool interlaced = regs2.TVMD.IsInterlaced();

    // Calculate window for sprite layer
    if (altField) {
        VDP2CalcWindow<true>(VDP2GetY<deinterlace>(y, regs2) ^ static_cast<uint32>(altField), regs2,
                             regs2.spriteParams.windowSet,
                             std::span{m_spriteLayerAttrs[altField].window}.first(m_HRes));
    } else {
        VDP2CalcWindow<false>(VDP2GetY<deinterlace>(y, regs2) ^ static_cast<uint32>(altField), regs2,
                              regs2.spriteParams.windowSet,
                              std::span{m_spriteLayerAttrs[altField].window}.first(m_HRes));
    }

    // Draw sprite layer
    (this->*fnDrawSprite[colorMode][rotate][altField])(y, regs2);

    // Calculate window state for all other layers
    if (altField) {
        VDP2CalcWindows<deinterlace, true>(y, regs2);
    } else {
        VDP2CalcWindows<deinterlace, false>(y, regs2);
    }

    // Draw background layers
    if (regs2.bgEnabled[4] && regs2.bgEnabled[5]) {
        VDP2DrawRotationBG<0>(regs2, colorMode, altField); // RBG0
        VDP2DrawRotationBG<1>(regs2, colorMode, altField); // RBG1
    } else {
        VDP2DrawRotationBG<0>(regs2, colorMode, altField); // RBG0
        VDP2DrawRotationBG<1>(regs2, colorMode, altField); // RBG1
        if (interlaced) {
            VDP2DrawNormalBG<0, deinterlace>(regs2, colorMode, altField); // NBG0
            VDP2DrawNormalBG<1, deinterlace>(regs2, colorMode, altField); // NBG1
            VDP2DrawNormalBG<2, deinterlace>(regs2, colorMode, altField); // NBG2
            VDP2DrawNormalBG<3, deinterlace>(regs2, colorMode, altField); // NBG3
        } else {
            VDP2DrawNormalBG<0, false>(regs2, colorMode, altField); // NBG0
            VDP2DrawNormalBG<1, false>(regs2, colorMode, altField); // NBG1
            VDP2DrawNormalBG<2, false>(regs2, colorMode, altField); // NBG2
            VDP2DrawNormalBG<3, false>(regs2, colorMode, altField); // NBG3
        }
    }

    // Compose image
    VDP2ComposeLine<deinterlace, transparentMeshes>(y, regs2, altField);
}

FORCE_INLINE void SoftwareVDPRenderer::VDP2DrawLineColorAndBackScreens(uint32 y, const VDP2Regs &regs2) {
    // Read line color screen color
    const LineBackScreenParams &lineParams = regs2.lineScreenParams;
    uint32 lineAddress = lineParams.baseAddress;
    if (lineParams.perLine) {
        lineAddress += y * sizeof(uint16);
    }
    const uint32 cramAddress = VDP2ReadRendererVRAM<uint16>(lineAddress) * sizeof(uint16);
    m_state.state2.lineBackLayerState.lineColor = VDP2ReadRendererColor5to8(cramAddress);

    // Read back screen color
    const LineBackScreenParams &backParams = regs2.backScreenParams;
    uint32 backAddress = backParams.baseAddress;
    if (backParams.perLine) {
        backAddress += y * sizeof(uint16);
    }
    const Color555 color555{.u16 = VDP2ReadRendererVRAM<uint16>(backAddress)};
    m_state.state2.lineBackLayerState.backColor = ConvertRGB555to888(color555);
}

template <uint32 colorMode, bool rotate, bool altField, bool transparentMeshes>
NO_INLINE void SoftwareVDPRenderer::VDP2DrawSpriteLayer(uint32 y, const VDP2Regs &regs2) {
    // VDP1 scaling:
    // 2x horz resolution: VDP1 TVM=000 and VDP2 HRESO=01x
    // 1/2x horz readout:  VDP1 TVM=001 and VDP2 HRESO=00x
    const VDP1RegTVMR &tvmr = VDP2GetVDP1TVMR();
    const bool exclMon = (regs2.TVMD.HRESOn & 0b100) != 0;
    const bool doubleResH = !tvmr.hdtvEnable && !tvmr.pixel8Bits && (regs2.TVMD.HRESOn & 0b110) == 0b010;
    const bool halfResH = !tvmr.hdtvEnable && tvmr.pixel8Bits && (regs2.TVMD.HRESOn & 0b110) == 0b000;
    const uint32 xOutputShift = doubleResH || exclMon ? 1 : 0;
    const uint32 xReadoutShift = halfResH ? 1 : 0;
    const uint32 maxX = m_HRes >> xOutputShift;

    const bool doubleDensity = regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity;

    const SpriteParams &params = regs2.spriteParams;
    auto &layerOut = m_layerOutputs[altField][0];
    auto &layerAttrs = m_spriteLayerAttrs[altField];

    const uint8 fbIndex = m_state.fbIndex.display;
    const auto &fbram = doubleDensity && altField ? m_altFBRAM[fbIndex] : m_state.mem1.FBRAM[fbIndex];

    [[maybe_unused]] auto &meshLayerOut = m_meshLayerOutput[altField];
    [[maybe_unused]] auto &meshLayerAttrs = m_meshLayerAttrs[altField];
    [[maybe_unused]] const auto &meshFB = m_meshFBRAM[altField][fbIndex];

    for (uint32 x = 0; x < maxX; x++) {
        const uint32 xx = x << xOutputShift;

        uint32 fbramOffset;
        if constexpr (rotate) {
            const auto &rotParamOut = m_rotParamLineOutputs[0];
            const auto &coord = rotParamOut.spriteCoords[x];
            if (coord.x() < 0 || coord.x() >= tvmr.fbSizeH || coord.y() < 0 || coord.y() >= tvmr.fbSizeV) {
                layerOut.pixels.priority[xx] = 0;
                layerAttrs.shadowOrWindow[xx] = false;
                layerAttrs.specialType[xx] = SpriteData::Special::Transparent;
                if (doubleResH) {
                    layerOut.pixels.CopyPixel(xx, xx + 1);
                    layerAttrs.CopyAttrs(xx, xx + 1);
                }
                if constexpr (transparentMeshes) {
                    meshLayerOut.pixels.priority[xx] = 0;
                    meshLayerAttrs.shadowOrWindow[xx] = false;
                    layerAttrs.specialType[xx] = SpriteData::Special::Transparent;
                    if (doubleResH) {
                        meshLayerOut.pixels.CopyPixel(xx, xx + 1);
                        meshLayerAttrs.CopyAttrs(xx, xx + 1);
                    }
                }
                continue;
            }
            fbramOffset = coord.x() + coord.y() * tvmr.fbSizeH;
        } else {
            fbramOffset = (x << xReadoutShift) + y * tvmr.fbSizeH;
        }

        VDP2DrawSpritePixel<colorMode, altField, transparentMeshes, false>(xx, regs2, tvmr, params, fbram, fbramOffset);
        if (doubleResH) {
            layerOut.pixels.CopyPixel(xx, xx + 1);
            layerAttrs.CopyAttrs(xx, xx + 1);
        }

        if constexpr (transparentMeshes) {
            VDP2DrawSpritePixel<colorMode, altField, transparentMeshes, true>(xx, regs2, tvmr, params, meshFB,
                                                                              fbramOffset);
            if (doubleResH) {
                meshLayerOut.pixels.CopyPixel(xx, xx + 1);
                meshLayerAttrs.CopyAttrs(xx, xx + 1);
            }
        }
    }
}

template <uint32 colorMode, bool altField, bool transparentMeshes, bool applyMesh>
FORCE_INLINE void SoftwareVDPRenderer::VDP2DrawSpritePixel(uint32 x, const VDP2Regs &regs2, const VDP1RegTVMR &tvmr,
                                                           const SpriteParams &params, const SpriteFB &fbram,
                                                           uint32 fbramOffset) {
    // This implies that if transparentMeshes is false, applyMesh will be always false
    static_assert(transparentMeshes || !applyMesh, "applyMesh cannot be set when transparentMeshes is disabled");

    // When applyMesh is true, the pixel to be drawn is from the transparent mesh layer.
    // In this case, the following changes happen:
    // - Transparent pixels are skipped as they have no effect on the final picture.
    // - Opaque pixels drawn on top of existing pixels on the sprite layer are averaged together.
    // - Opaque pixels drawn on transparent pixels will become translucent and enable the transparentMesh attribute.
    // Transparent mesh pixels are handled separately from the rest of the rendering pipeline.

    auto &layerOut = applyMesh ? m_meshLayerOutput[altField] : m_layerOutputs[altField][0];
    auto &layerAttrs = applyMesh ? m_meshLayerAttrs[altField] : m_spriteLayerAttrs[altField];

    // NOTE: intentionally using the base sprite layer here as the windows are not computed for the mesh layer
    if (m_spriteLayerAttrs[altField].window[x]) {
        layerOut.pixels.priority[x] = 0;
        layerAttrs.shadowOrWindow[x] = false;
        layerAttrs.specialType[x] = SpriteData::Special::Transparent;
        return;
    }

    if (params.mixedFormat) {
        const uint16 spriteDataValue = util::ReadBE<uint16>(&fbram[(fbramOffset * sizeof(uint16)) & 0x3FFFE]);
        if (bit::test<15>(spriteDataValue)) {
            // RGB data

            // Transparent if:
            // - Using byte-sized sprite types (0x8 to 0xF) and the lower 8 bits are all zero
            // - Using word-sized sprite types that have the shadow/sprite window bit (types 0x2 to 0x7), sprite
            //   window is enabled, and the lower 15 bits are all zero
            if (params.type >= 8) {
                if (bit::extract<0, 7>(spriteDataValue) == 0) {
                    layerOut.pixels.priority[x] = 0;
                    layerAttrs.shadowOrWindow[x] = false;
                    layerAttrs.specialType[x] = SpriteData::Special::Transparent;
                    return;
                }
            } else if (params.type >= 2) {
                if (params.useSpriteWindow && bit::extract<0, 14>(spriteDataValue) == 0) {
                    layerOut.pixels.priority[x] = 0;
                    layerAttrs.shadowOrWindow[x] = false;
                    layerAttrs.specialType[x] = SpriteData::Special::Transparent;
                    return;
                }
            }

            layerOut.pixels.color[x] = ConvertRGB555to888(Color555{spriteDataValue});
            layerOut.pixels.priority[x] = params.priorities[0];
            layerOut.pixels.specialColorCalc[x] = false;

            layerAttrs.colorCalcRatio[x] = params.colorCalcRatios[0];
            layerAttrs.shadowOrWindow[x] = false;
            layerAttrs.specialType[x] = SpriteData::Special::Normal;
            return;
        }
    }

    // Palette data
    const SpriteData spriteData = VDP2FetchSpriteData<applyMesh>(regs2, tvmr, fbram, fbramOffset);

    // Handle sprite window
    if (params.useSpriteWindow && params.spriteWindowEnabled &&
        spriteData.shadowOrWindow != params.spriteWindowInverted) {
        layerOut.pixels.priority[x] = 0;
        layerAttrs.shadowOrWindow[x] = true;
        layerAttrs.specialType[x] = SpriteData::Special::Transparent;
        return;
    }

    const uint32 colorIndex = params.colorDataOffset + spriteData.colorData;
    const Color888 color = VDP2FetchCRAMColor<colorMode>(0, colorIndex);

    layerOut.pixels.color[x] = color;
    layerOut.pixels.priority[x] = spriteData.special == SpriteData::Special::Transparent && !spriteData.shadowOrWindow
                                      ? 0
                                      : params.priorities[spriteData.priority];
    layerOut.pixels.specialColorCalc[x] = true;

    layerAttrs.colorCalcRatio[x] = params.colorCalcRatios[spriteData.colorCalcRatio];
    layerAttrs.shadowOrWindow[x] = spriteData.shadowOrWindow;
    layerAttrs.specialType[x] = spriteData.special;
}

template <uint32 bgIndex, bool deinterlace>
FORCE_INLINE void SoftwareVDPRenderer::VDP2DrawNormalBG(const VDP2Regs &regs2, uint32 colorMode, bool altField) {
    static_assert(bgIndex < 4, "Invalid NBG index");

    using FnDraw = void (SoftwareVDPRenderer::*)(const VDP2Regs &, const BGParams &, LayerOutput &,
                                                 const NBGLayerState &, VRAMFetcher &, std::span<const bool>, bool);

    // Lookup table of scroll BG drawing functions
    // Indexing: [charMode][fourCellChar][colorFormat][colorMode]
    static constexpr auto fnDrawScroll = [] {
        std::array<std::array<std::array<std::array<FnDraw, 4>, 8>, 2>, 3> arr{};

        util::constexpr_for<3 * 2 * 8 * 4>([&](auto index) {
            const uint32 fcc = bit::extract<0>(index());
            const uint32 cf = bit::extract<1, 3>(index());
            const uint32 clm = bit::extract<4, 5>(index());
            const uint32 chm = bit::extract<6, 7>(index());

            const auto chmEnum = static_cast<CharacterMode>(chm);
            const auto cfEnum = static_cast<ColorFormat>(cf <= 4 ? cf : 4);
            const uint32 colorMode = clm <= 2 ? clm : 2;
            arr[chm][fcc][cf][clm] = &SoftwareVDPRenderer::VDP2DrawNormalScrollBG<chmEnum, fcc, cfEnum, colorMode,
                                                                                  bgIndex <= 1, deinterlace>;
        });

        return arr;
    }();

    // Lookup table of bitmap BG drawing functions
    // Indexing: [colorFormat]
    static constexpr auto fnDrawBitmap = [] {
        std::array<std::array<FnDraw, 4>, 8> arr{};

        util::constexpr_for<8 * 4>([&](auto index) {
            const uint32 cf = bit::extract<0, 2>(index());
            const uint32 cm = bit::extract<3, 4>(index());

            const auto cfEnum = static_cast<ColorFormat>(cf <= 4 ? cf : 4);
            const uint32 colorMode = cm <= 2 ? cm : 2;
            arr[cf][cm] = &SoftwareVDPRenderer::VDP2DrawNormalBitmapBG<cfEnum, colorMode, bgIndex <= 1, deinterlace>;
        });

        return arr;
    }();

    const VDP2State &state2 = m_state.state2;
    if (!state2.layerEnabled[bgIndex + 2]) {
        return;
    }

    if constexpr (bgIndex == 0) {
        // NBG0 and RBG1 are mutually exclusive
        if (regs2.bgEnabled[5]) {
            return;
        }
    }

    const BGParams &bgParams = regs2.bgParams[bgIndex + 1];
    const NBGLayerState &bgState = state2.nbgLayerStates[bgIndex];

    if (bgParams.mosaicEnable && bgState.mosaicCounterY > 0) {
        // Reuse contents of previous line
        return;
    }

    LayerOutput &layerOut = m_layerOutputs[altField][bgIndex + 2];
    VRAMFetcher &vramFetcher = m_vramFetchers[altField][bgIndex];
    auto windowState = std::span<const bool>{m_bgWindows[altField][bgIndex + 1]};

    const uint32 cf = static_cast<uint32>(bgParams.colorFormat);
    if (bgParams.bitmap) {
        (this->*fnDrawBitmap[cf][colorMode])(regs2, bgParams, layerOut, bgState, vramFetcher, windowState, altField);
    } else {
        const bool twc = bgParams.twoWordChar;
        const bool fcc = bgParams.cellSizeShift;
        const bool exc = bgParams.extChar;
        const uint32 chm = static_cast<uint32>(twc   ? CharacterMode::TwoWord
                                               : exc ? CharacterMode::OneWordExtended
                                                     : CharacterMode::OneWordStandard);
        (this->*fnDrawScroll[chm][fcc][cf][colorMode])(regs2, bgParams, layerOut, bgState, vramFetcher, windowState,
                                                       altField);
    }
}

template <uint32 bgIndex>
FORCE_INLINE void SoftwareVDPRenderer::VDP2DrawRotationBG(const VDP2Regs &regs2, uint32 colorMode, bool altField) {
    static_assert(bgIndex < 2, "Invalid RBG index");

    using FnDrawScroll = void (SoftwareVDPRenderer::*)(const VDP2Regs &, const BGParams &, LayerOutput &, VRAMFetcher &,
                                                       std::span<const bool>, bool);
    using FnDrawBitmap =
        void (SoftwareVDPRenderer::*)(const VDP2Regs &, const BGParams &, LayerOutput &, std::span<const bool>, bool);

    // Lookup table of scroll BG drawing functions
    // Indexing: [charMode][fourCellChar][colorFormat][colorMode]
    static constexpr auto fnDrawScroll = [] {
        std::array<std::array<std::array<std::array<FnDrawScroll, 4>, 8>, 2>, 3> arr{};

        util::constexpr_for<3 * 2 * 8 * 4>([&](auto index) {
            const uint32 fcc = bit::extract<0>(index());
            const uint32 cf = bit::extract<1, 3>(index());
            const uint32 clm = bit::extract<4, 5>(index());
            const uint32 chm = bit::extract<6, 7>(index());

            const auto chmEnum = static_cast<CharacterMode>(chm);
            const auto cfEnum = static_cast<ColorFormat>(cf <= 4 ? cf : 4);
            const uint32 colorMode = clm <= 2 ? clm : 2;
            arr[chm][fcc][cf][clm] =
                &SoftwareVDPRenderer::VDP2DrawRotationScrollBG<bgIndex, chmEnum, fcc, cfEnum, colorMode>;
        });

        return arr;
    }();

    // Lookup table of bitmap BG drawing functions
    // Indexing: [colorFormat][colorMode]
    static constexpr auto fnDrawBitmap = [] {
        std::array<std::array<FnDrawBitmap, 4>, 8> arr{};

        util::constexpr_for<8 * 4>([&](auto index) {
            const uint32 cf = bit::extract<0, 2>(index());
            const uint32 cm = bit::extract<3, 4>(index());

            const auto cfEnum = static_cast<ColorFormat>(cf <= 4 ? cf : 4);
            const uint32 colorMode = cm <= 2 ? cm : 2;
            arr[cf][cm] = &SoftwareVDPRenderer::VDP2DrawRotationBitmapBG<bgIndex, cfEnum, colorMode>;
        });

        return arr;
    }();

    if (!m_state.state2.layerEnabled[bgIndex + 1]) {
        return;
    }
    if constexpr (bgIndex == 1) {
        // RBG1 must be explicitly enabled
        if (!m_state.regs2.bgEnabled[5]) {
            return;
        }
    }

    const BGParams &bgParams = regs2.bgParams[bgIndex];
    LayerOutput &layerOut = m_layerOutputs[altField][bgIndex + 1];
    VRAMFetcher &vramFetcher = m_vramFetchers[altField][bgIndex + 4];
    auto windowState = std::span<const bool>{m_bgWindows[altField][bgIndex]};

    const uint32 cf = static_cast<uint32>(bgParams.colorFormat);
    if (bgParams.bitmap) {
        (this->*fnDrawBitmap[cf][colorMode])(regs2, bgParams, layerOut, windowState, altField);
    } else {
        const bool twc = bgParams.twoWordChar;
        const bool fcc = bgParams.cellSizeShift;
        const bool exc = bgParams.extChar;
        const uint32 chm = static_cast<uint32>(twc   ? CharacterMode::TwoWord
                                               : exc ? CharacterMode::OneWordExtended
                                                     : CharacterMode::OneWordStandard);
        (this->*fnDrawScroll[chm][fcc][cf][colorMode])(regs2, bgParams, layerOut, vramFetcher, windowState, altField);
    }
}

// Lookup table for color offset effects.
// Indexing: [colorOffset][channelValue]
static const auto kColorOffsetLUT = [] {
    std::array<std::array<uint8, 256>, 512> arr{};
    for (uint32 i = 0; i < 512; i++) {
        const sint32 ofs = bit::sign_extend<9>(i);
        for (uint32 c = 0; c < 256; c++) {
            arr[i][c] = std::clamp<sint32>(c + ofs, 0, 255);
        }
    }
    return arr;
}();

// Tests if an array of uint8 values are all zeroes
FORCE_INLINE static bool AllZeroU8(std::span<const uint8> values) {

#if defined(_M_X64) || defined(__x86_64__)

    #if defined(__AVX__)
    // 32 at a time
    for (; values.size() >= 32; values = values.subspan(32)) {
        const __m256i vec32 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(values.data()));

        // Test if all bits are 0
        if (!_mm256_testz_si256(vec32, vec32)) {
            return false;
        }
    }
    #endif

    #if defined(__SSE2__)
    // 16 at a time
    for (; values.size() >= 16; values = values.subspan(16)) {
        __m128i vec16 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(values.data()));

        // Compare to zero
        vec16 = _mm_cmpeq_epi8(vec16, _mm_setzero_si128());

        // Extract MSB all into a 16-bit mask, if any bit is clear, then we have a true value
        if (_mm_movemask_epi8(vec16) != 0xFFFF) {
            return false;
        }
    }
    #endif
#elif defined(_M_ARM64) || defined(__aarch64__)
    // 64 at a time
    for (; values.size() >= 64; values = values.subspan(64)) {
        const uint8x16x4_t vec64 = vld1q_u8_x4(reinterpret_cast<const uint8 *>(values.data()));

        // If the largest value is not zero, we have a true value
        if ((vmaxvq_u8(vec64.val[0]) != 0u) || (vmaxvq_u8(vec64.val[1]) != 0u) || (vmaxvq_u8(vec64.val[2]) != 0u) ||
            (vmaxvq_u8(vec64.val[3]) != 0u)) {
            return false;
        }
    }

    // 16 at a time
    for (; values.size() >= 16; values = values.subspan(16)) {
        const uint8x16_t vec16 = vld1q_u8(reinterpret_cast<const uint8 *>(values.data()));

        // If the largest value is not zero, we have a true value
        if (vmaxvq_u8(vec16) != 0u) {
            return false;
        }
    }
#elif defined(__clang__) || defined(__GNUC__)
    // 16 at a time
    for (; values.size() >= sizeof(__int128); values = values.subspan(sizeof(__int128))) {
        const __int128 &vec16 = *reinterpret_cast<const __int128 *>(values.data());

        if (vec16 != __int128(0)) {
            return false;
        }
    }
#endif

    // 8 at a time
    for (; values.size() >= sizeof(uint64); values = values.subspan(sizeof(uint64))) {
        const uint64 &vec8 = *reinterpret_cast<const uint64 *>(values.data());

        if (vec8 != 0ull) {
            return false;
        }
    }

    // 4 at a time
    for (; values.size() >= sizeof(uint32); values = values.subspan(sizeof(uint32))) {
        const uint32 &vec4 = *reinterpret_cast<const uint32 *>(values.data());

        if (vec4 != 0u) {
            return false;
        }
    }

    for (const uint8 &value : values) {
        if (value != 0u) {
            return false;
        }
    }
    return true;
}

// Tests if an array of bool values are all true
FORCE_INLINE static bool AllBool(std::span<const bool> values) {

#if defined(_M_X64) || defined(__x86_64__)
    #if defined(__AVX__)
    // 32 at a time
    for (; values.size() >= 32; values = values.subspan(32)) {
        __m256i vec32 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(values.data()));

        // Move bit 0 into the MSB
        vec32 = _mm256_slli_epi64(vec32, 7);

        // Extract 32 MSBs into a 32-bit mask, if any bit is zero, then we have a false value
        if (_mm256_movemask_epi8(vec32) != 0xFFFF'FFFF) {
            return false;
        }
    }
    #endif

    #if defined(__SSE2__)
    // 16 at a time
    for (; values.size() >= 16; values = values.subspan(16)) {
        __m128i vec16 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(values.data()));

        // Move bit 0 into the MSB
        vec16 = _mm_slli_epi64(vec16, 7);

        // Extract 16 MSBs into a 32-bit mask, if any bit is zero, then we have a false value
        if (_mm_movemask_epi8(vec16) != 0xFFFF) {
            return false;
        }
    }
    #endif
#elif defined(_M_ARM64) || defined(__aarch64__)
    // 64 at a time
    for (; values.size() >= 64; values = values.subspan(64)) {
        const uint8x16x4_t vec64 = vld1q_u8_x4(reinterpret_cast<const uint8 *>(values.data()));

        // If the smallest value is zero, then we have a false value
        if ((vminvq_u8(vec64.val[0]) == 0u) || (vminvq_u8(vec64.val[1]) == 0u) || (vminvq_u8(vec64.val[2]) == 0u) ||
            (vminvq_u8(vec64.val[3]) == 0u)) {
            return false;
        }
    }
    // 16 at a time
    for (; values.size() >= 16; values = values.subspan(16)) {
        const uint8x16_t vec16 = vld1q_u8(reinterpret_cast<const uint8 *>(values.data()));

        // If the smallest value is zero, then we have a false value
        if (vminvq_u8(vec16) == 0u) {
            return false;
        }
    }
#elif defined(__clang__) || defined(__GNUC__)
    // 16 at a time
    for (; values.size() >= sizeof(__int128); values = values.subspan(sizeof(__int128))) {
        const __int128 &vec16 = *reinterpret_cast<const __int128 *>(values.data());

        if (vec16 != __int128((__int128(0x01'01'01'01'01'01'01'01) << 64) | 0x01'01'01'01'01'01'01'01)) {
            return false;
        }
    }
#endif

    // 8 at a time
    for (; values.size() >= sizeof(uint64); values = values.subspan(sizeof(uint64))) {
        const uint64 &vec8 = *reinterpret_cast<const uint64 *>(values.data());

        if (vec8 != 0x01'01'01'01'01'01'01'01) {
            return false;
        }
    }

    // 4 at a time
    for (; values.size() >= sizeof(uint32); values = values.subspan(sizeof(uint32))) {
        const uint32 &vec4 = *reinterpret_cast<const uint32 *>(values.data());

        if (vec4 != 0x01'01'01'01) {
            return false;
        }
    }

    for (const bool &value : values) {
        if (!value) {
            return false;
        }
    }
    return true;
}

// Tests if an any element in an array of bools are true
FORCE_INLINE static bool AnyBool(std::span<const bool> values) {
#if defined(_M_X64) || defined(__x86_64__)
    #if defined(__AVX__)
    // 32 at a time
    for (; values.size() >= 32; values = values.subspan(32)) {
        __m256i vec32 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(values.data()));

        // Move bit 0 into the MSB
        vec32 = _mm256_slli_epi64(vec32, 7);

        // Extract MSB into a 32-bit mask, if any bit is set, then we have a true value
        if (_mm256_movemask_epi8(vec32) != 0u) {
            return true;
        }
    }
    #endif
    #if defined(__SSE2__)
    // 16 at a time
    for (; values.size() >= 16; values = values.subspan(16)) {
        __m128i vec16 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(values.data()));

        // Move bit 0 into the MSB
        vec16 = _mm_slli_epi64(vec16, 7);

        // Extract MSB into a 16-bit mask, if any bit is set, then we have a true value
        if (_mm_movemask_epi8(vec16) != 0u) {
            return true;
        }
    }
    #endif
#elif defined(_M_ARM64) || defined(__aarch64__)
    // 64 at a time
    for (; values.size() >= 64; values = values.subspan(64)) {
        const uint8x16x4_t vec64 = vld1q_u8_x4(reinterpret_cast<const uint8 *>(values.data()));

        // If the smallest value is not zero, then we have a true value
        if ((vmaxvq_u8(vec64.val[0]) != 0u) || (vmaxvq_u8(vec64.val[1]) != 0u) || (vmaxvq_u8(vec64.val[2]) != 0u) ||
            (vmaxvq_u8(vec64.val[3]) != 0u)) {
            return true;
        }
    }

    // 16 at a time
    for (; values.size() >= 16; values = values.subspan(16)) {
        const uint8x16_t vec16 = vld1q_u8(reinterpret_cast<const uint8 *>(values.data()));

        // If the smallest value is not zero, then we have a true value
        if (vmaxvq_u8(vec16) != 0u) {
            return true;
        }
    }
#elif defined(__clang__) || defined(__GNUC__)
    // 16 at a time
    for (; values.size() >= sizeof(__int128); values = values.subspan(sizeof(__int128))) {
        const __int128 &vec16 = *reinterpret_cast<const __int128 *>(values.data());

        if (vec16) {
            return true;
        }
    }
#endif

    // 8 at a time
    for (; values.size() >= sizeof(uint64); values = values.subspan(sizeof(uint64))) {
        const uint64 &vec8 = *reinterpret_cast<const uint64 *>(values.data());

        if (vec8) {
            return true;
        }
    }

    // 4 at a time
    for (; values.size() >= sizeof(uint32); values = values.subspan(sizeof(uint32))) {
        const uint32 &vec4 = *reinterpret_cast<const uint32 *>(values.data());

        if (vec4) {
            return true;
        }
    }

    for (const bool &value : values) {
        if (value) {
            return true;
        }
    }
    return false;
}

FORCE_INLINE static void Color888ShadowMasked(const std::span<Color888> pixels,
                                              const std::span<const bool, kMaxResH> mask) {
    size_t i = 0;

#if defined(_M_X64) || defined(__x86_64__)
    #if defined(__AVX2__)
    // Eight pixels at a time
    for (; (i + 8) < pixels.size(); i += 8) {
        // Load eight mask bytes into 32-bit lanes of 000... or 111...
        __m256i mask_x8 = _mm256_cvtepu8_epi32(_mm_loadu_si64(mask.data() + i));
        mask_x8 = _mm256_sub_epi32(_mm256_setzero_si256(), mask_x8);

        const __m256i pixel_x8 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(&pixels[i]));

        __m256i shadowed_x8 = _mm256_srli_epi32(pixel_x8, 1);
        shadowed_x8 = _mm256_and_si256(shadowed_x8, _mm256_set1_epi8(0x7F));

        // Blend with mask
        const __m256i dstColor_x8 = _mm256_blendv_epi8(pixel_x8, shadowed_x8, mask_x8);

        // Write
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(&pixels[i]), dstColor_x8);
    }
    #endif

    #if defined(__SSE2__)
    // Four pixels at a time
    for (; (i + 4) < pixels.size(); i += 4) {
        // Load four mask values and expand each byte into 32-bit 000... or 111...
        __m128i mask_x4 = _mm_loadu_si32(mask.data() + i);
        mask_x4 = _mm_unpacklo_epi8(mask_x4, _mm_setzero_si128());
        mask_x4 = _mm_unpacklo_epi16(mask_x4, _mm_setzero_si128());
        mask_x4 = _mm_sub_epi32(_mm_setzero_si128(), mask_x4);

        const __m128i pixel_x4 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(&pixels[i]));

        __m128i shadowed_x4 = _mm_srli_epi64(pixel_x4, 1);

        shadowed_x4 = _mm_and_si128(shadowed_x4, _mm_set1_epi8(0x7F));

        // Blend with mask
        const __m128i dstColor_x4 =
            _mm_or_si128(_mm_and_si128(mask_x4, shadowed_x4), _mm_andnot_si128(mask_x4, pixel_x4));

        // Write
        _mm_storeu_si128(reinterpret_cast<__m128i *>(&pixels[i]), dstColor_x4);
    }
    #endif
#elif defined(_M_ARM64) || defined(__aarch64__)
    // Four pixels at a time
    for (; (i + 4) < pixels.size(); i += 4) {
        // Load four mask values and expand each byte into 32-bit 000... or 111...
        uint32x4_t mask_x4 = vld1q_lane_u32(reinterpret_cast<const uint32 *>(mask.data() + i), vdupq_n_u32(0), 0);
        mask_x4 = vmovl_u16(vget_low_u16(vmovl_u8(vget_low_u8(mask_x4))));
        mask_x4 = vnegq_s32(mask_x4);

        const uint32x4_t pixel_x4 = vld1q_u32(reinterpret_cast<const uint32 *>(&pixels[i]));
        const uint32x4_t shadowed_x4 = vshrq_n_u8(pixel_x4, 1);

        // Blend with mask
        const uint32x4_t dstColor_x4 = vbslq_u32(mask_x4, shadowed_x4, pixel_x4);

        // Write
        vst1q_u32(reinterpret_cast<uint32 *>(&pixels[i]), dstColor_x4);
    }
#endif

    for (; i < pixels.size(); i++) {
        Color888 &pixel = pixels[i];
        if (mask[i]) {
            pixel.u32 >>= 1;
            pixel.u32 &= 0x7F'7F'7F'7F;
        }
    }
}

FORCE_INLINE static void Color888SatAddMasked(const std::span<Color888> dest,
                                              const std::span<const bool, kMaxResH> mask,
                                              const std::span<const Color888, kMaxResH> topColors,
                                              const std::span<const Color888> btmColors) {
    size_t i = 0;

#if defined(_M_X64) || defined(__x86_64__)

    #if defined(__AVX2__)
    // Eight pixels at a time
    for (; (i + 8) < dest.size(); i += 8) {
        // Load eight mask bytes into 32-bit lanes of 000... or 111...
        __m256i mask_x8 = _mm256_cvtepu8_epi32(_mm_loadu_si64(mask.data() + i));
        mask_x8 = _mm256_sub_epi32(_mm256_setzero_si256(), mask_x8);

        const __m256i topColor_x8 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(&topColors[i]));
        const __m256i btmColor_x8 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(&btmColors[i]));

        // Pack back into 8-bit values, be sure to truncate to avoid saturation
        __m256i dstColor_x8 = _mm256_adds_epu8(topColor_x8, btmColor_x8);

        // Blend with mask
        dstColor_x8 = _mm256_blendv_epi8(topColor_x8, dstColor_x8, mask_x8);

        // Write
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(&dest[i]), dstColor_x8);
    }
    #endif

    #if defined(__SSE2__)
    // Four pixels at a time
    for (; (i + 4) < dest.size(); i += 4) {
        // Load four mask values and expand each byte into 32-bit 000... or 111...
        __m128i mask_x4 = _mm_loadu_si32(mask.data() + i);
        mask_x4 = _mm_unpacklo_epi8(mask_x4, _mm_setzero_si128());
        mask_x4 = _mm_unpacklo_epi16(mask_x4, _mm_setzero_si128());
        mask_x4 = _mm_sub_epi32(_mm_setzero_si128(), mask_x4);

        const __m128i topColor_x4 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(&topColors[i]));
        const __m128i btmColor_x4 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(&btmColors[i]));

        // Saturated add
        __m128i dstColor_x4 = _mm_adds_epu8(topColor_x4, btmColor_x4);

        // Blend with mask
        dstColor_x4 = _mm_or_si128(_mm_and_si128(mask_x4, dstColor_x4), _mm_andnot_si128(mask_x4, topColor_x4));

        // Write
        _mm_storeu_si128(reinterpret_cast<__m128i *>(&dest[i]), dstColor_x4);
    }
    #endif
#elif defined(_M_ARM64) || defined(__aarch64__)
    // Four pixels at a time
    for (; (i + 4) < dest.size(); i += 4) {
        // Load four mask values and expand each byte into 32-bit 000... or 111...
        uint32x4_t mask_x4 = vld1q_lane_u32(reinterpret_cast<const uint32 *>(mask.data() + i), vdupq_n_u32(0), 0);
        mask_x4 = vmovl_u16(vget_low_u16(vmovl_u8(vget_low_u8(mask_x4))));
        mask_x4 = vnegq_s32(mask_x4);

        const uint32x4_t topColor_x4 = vld1q_u32(reinterpret_cast<const uint32 *>(&topColors[i]));
        const uint32x4_t btmColor_x4 = vld1q_u32(reinterpret_cast<const uint32 *>(&btmColors[i]));

        // Saturated add
        const uint32x4_t add_x4 = vqaddq_u8(topColor_x4, btmColor_x4);

        // Blend with mask
        const uint32x4_t dstColor_x4 = vbslq_u32(mask_x4, add_x4, topColor_x4);

        // Write
        vst1q_u32(reinterpret_cast<uint32 *>(&dest[i]), dstColor_x4);
    }
#endif

    for (; i < dest.size(); i++) {
        const Color888 &topColor = topColors[i];
        const Color888 &btmColor = btmColors[i];
        Color888 &dstColor = dest[i];
        if (mask[i]) {
            dstColor.r = std::min<uint16>(topColor.r + btmColor.r, 255u);
            dstColor.g = std::min<uint16>(topColor.g + btmColor.g, 255u);
            dstColor.b = std::min<uint16>(topColor.b + btmColor.b, 255u);
        } else {
            dstColor = topColor;
        }
    }
}

FORCE_INLINE static void Color888SelectMasked(const std::span<Color888> dest,
                                              const std::span<const bool, kMaxResH> mask,
                                              const std::span<const Color888> topColors,
                                              const std::span<const Color888, kMaxResH> btmColors) {
    size_t i = 0;

#if defined(_M_X64) || defined(__x86_64__)
    #if defined(__AVX2__)
    // Eight pixels at a time
    for (; (i + 8) < dest.size(); i += 8) {
        // Load eight mask bytes into 32-bit lanes of 000... or 111...
        __m256i mask_x8 = _mm256_cvtepu8_epi32(_mm_loadu_si64(mask.data() + i));
        mask_x8 = _mm256_sub_epi32(_mm256_setzero_si256(), mask_x8);

        const __m256i topColor_x8 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(&topColors[i]));
        const __m256i btmColor_x8 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(&btmColors[i]));

        // Blend with mask
        const __m256i dstColor_x8 = _mm256_blendv_epi8(topColor_x8, btmColor_x8, mask_x8);

        // Write
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(&dest[i]), dstColor_x8);
    }
    #endif

    #if defined(__SSE2__)
    // Four pixels at a time
    for (; (i + 4) < dest.size(); i += 4) {
        // Load four mask values and expand each byte into 32-bit 000... or 111...
        __m128i mask_x4 = _mm_loadu_si32(mask.data() + i);
        mask_x4 = _mm_unpacklo_epi8(mask_x4, _mm_setzero_si128());
        mask_x4 = _mm_unpacklo_epi16(mask_x4, _mm_setzero_si128());
        mask_x4 = _mm_sub_epi32(_mm_setzero_si128(), mask_x4);

        const __m128i topColor_x4 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(&topColors[i]));
        const __m128i btmColor_x4 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(&btmColors[i]));

        // Blend with mask
        const __m128i dstColor_x4 =
            _mm_or_si128(_mm_and_si128(mask_x4, btmColor_x4), _mm_andnot_si128(mask_x4, topColor_x4));

        // Write
        _mm_storeu_si128(reinterpret_cast<__m128i *>(&dest[i]), dstColor_x4);
    }
    #endif
#elif defined(_M_ARM64) || defined(__aarch64__)
    // Four pixels at a time
    for (; (i + 4) < dest.size(); i += 4) {
        // Load four mask values and expand each byte into 32-bit 000... or 111...
        uint32x4_t mask_x4 = vld1q_lane_u32(reinterpret_cast<const uint32 *>(mask.data() + i), vdupq_n_u32(0), 0);
        mask_x4 = vmovl_u16(vget_low_u16(vmovl_u8(vget_low_u8(mask_x4))));
        mask_x4 = vnegq_s32(mask_x4);

        const uint32x4_t topColor_x4 = vld1q_u32(reinterpret_cast<const uint32 *>(&topColors[i]));
        const uint32x4_t btmColor_x4 = vld1q_u32(reinterpret_cast<const uint32 *>(&btmColors[i]));

        // Blend with mask
        const uint32x4_t dstColor_x4 = vbslq_u32(mask_x4, btmColor_x4, topColor_x4);

        // Write
        vst1q_u32(reinterpret_cast<uint32 *>(&dest[i]), dstColor_x4);
    }
#endif

    for (; i < dest.size(); i++) {
        dest[i] = mask[i] ? btmColors[i] : topColors[i];
    }
}

FORCE_INLINE static void Color888GradationMasked(const std::span<Color888> dest,
                                                 const std::span<const bool, kMaxResH> mask,
                                                 const std::span<const Color888> src) {
    size_t i = 0;

#if defined(_M_X64) || defined(__x86_64__)
    #if defined(__AVX2__)
    // Eight pixels at a time
    for (; (i + 8) < dest.size(); i += 8) {
        // Load eight mask bytes into 32-bit lanes of 000... or 111...
        __m256i mask_x8 = _mm256_cvtepu8_epi32(_mm_loadu_si64(mask.data() + i));
        mask_x8 = _mm256_sub_epi32(_mm256_setzero_si256(), mask_x8);

        const __m256i color0_x8 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(&src[i]));
        const __m256i color1_x8 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(&src[i + 1]));
        const __m256i color2_x8 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(&src[i + 2]));

        const __m256i blend01_x8 = _mm256_add_epi32(
            _mm256_srli_epi32(_mm256_and_si256(_mm256_xor_si256(color0_x8, color1_x8), _mm256_set1_epi8(0xFE)), 1),
            _mm256_and_si256(color0_x8, color1_x8));
        const __m256i blend2_x8 = _mm256_add_epi32(
            _mm256_srli_epi32(_mm256_and_si256(_mm256_xor_si256(blend01_x8, color2_x8), _mm256_set1_epi8(0xFE)), 1),
            _mm256_and_si256(blend01_x8, color2_x8));

        // Blend with mask
        const __m256i dstColor_x8 = _mm256_blendv_epi8(color2_x8, blend2_x8, mask_x8);

        // Write
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(&dest[i]), dstColor_x8);
    }
    #endif

    #if defined(__SSE2__)
    // Four pixels at a time
    for (; (i + 4) < dest.size(); i += 4) {
        // Load four mask values and expand each byte into 32-bit 000... or 111...
        __m128i mask_x4 = _mm_loadu_si32(mask.data() + i);
        mask_x4 = _mm_unpacklo_epi8(mask_x4, _mm_setzero_si128());
        mask_x4 = _mm_unpacklo_epi16(mask_x4, _mm_setzero_si128());
        mask_x4 = _mm_sub_epi32(_mm_setzero_si128(), mask_x4);

        const __m128i color0_x4 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(&src[i]));
        const __m128i color1_x4 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(&src[i + 1]));
        const __m128i color2_x4 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(&src[i + 2]));

        const __m128i blend01_x4 =
            _mm_add_epi32(_mm_srli_epi32(_mm_and_si128(_mm_xor_si128(color0_x4, color1_x4), _mm_set1_epi8(0xFE)), 1),
                          _mm_and_si128(color0_x4, color1_x4));
        const __m128i blend2_x4 =
            _mm_add_epi32(_mm_srli_epi32(_mm_and_si128(_mm_xor_si128(blend01_x4, color2_x4), _mm_set1_epi8(0xFE)), 1),
                          _mm_and_si128(blend01_x4, color2_x4));

        // Blend with mask
        const __m128i dstColor_x4 =
            _mm_or_si128(_mm_and_si128(color2_x4, blend2_x4), _mm_andnot_si128(mask_x4, color2_x4));

        // Write
        _mm_storeu_si128(reinterpret_cast<__m128i *>(&dest[i]), dstColor_x4);
    }
    #endif
#elif defined(_M_ARM64) || defined(__aarch64__)
    // Four pixels at a time
    for (; (i + 4) < dest.size(); i += 4) {
        // Load four mask values and expand each byte into 32-bit 000... or 111...
        uint32x4_t mask_x4 = vld1q_lane_u32(reinterpret_cast<const uint32 *>(mask.data() + i), vdupq_n_u32(0), 0);
        mask_x4 = vmovl_u16(vget_low_u16(vmovl_u8(vget_low_u8(mask_x4))));
        mask_x4 = vnegq_s32(mask_x4);

        const uint32x4_t color0_x4 = vld1q_u32(reinterpret_cast<const uint32 *>(&src[i]));
        const uint32x4_t color1_x4 = vld1q_u32(reinterpret_cast<const uint32 *>(&src[i + 1]));
        const uint32x4_t color2_x4 = vld1q_u32(reinterpret_cast<const uint32 *>(&src[i + 2]));

        // Halving average
        const uint32x4_t blend01_x4 = vhaddq_u8(color0_x4, color1_x4);
        const uint32x4_t blend2_x4 = vhaddq_u8(blend01_x4, color2_x4);

        // Blend with mask
        const uint32x4_t dstColor_x4 = vbslq_u32(mask_x4, color2_x4, blend2_x4);

        // Write
        vst1q_u32(reinterpret_cast<uint32 *>(&dest[i]), dstColor_x4);
    }
#endif

    for (; i < dest.size(); i++) {
        const Color888 &color0 = src[i];
        const Color888 &color1 = src[i + 1];
        const Color888 &color2 = src[i + 2];
        Color888 &dstColor = dest[i];
        if (mask[i]) {
            dstColor = AverageRGB888(AverageRGB888(color0, color1), color2);
        } else {
            dstColor = color2;
        }
    }
}

FORCE_INLINE static void Color888AverageMasked(const std::span<Color888> dest,
                                               const std::span<const bool, kMaxResH> mask,
                                               const std::span<const Color888> topColors,
                                               const std::span<const Color888> btmColors) {
    size_t i = 0;

#if defined(_M_X64) || defined(__x86_64__)
    #if defined(__AVX2__)
    // Eight pixels at a time
    for (; (i + 8) < dest.size(); i += 8) {
        // Load eight mask bytes into 32-bit lanes of 000... or 111...
        __m256i mask_x8 = _mm256_cvtepu8_epi32(_mm_loadu_si64(mask.data() + i));
        mask_x8 = _mm256_sub_epi32(_mm256_setzero_si256(), mask_x8);

        const __m256i topColor_x8 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(&topColors[i]));
        const __m256i btmColor_x8 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(&btmColors[i]));

        const __m256i average_x8 = _mm256_add_epi32(
            _mm256_srli_epi32(_mm256_and_si256(_mm256_xor_si256(topColor_x8, btmColor_x8), _mm256_set1_epi8(0xFE)), 1),
            _mm256_and_si256(topColor_x8, btmColor_x8));

        // Blend with mask
        const __m256i dstColor_x8 = _mm256_blendv_epi8(topColor_x8, average_x8, mask_x8);

        // Write
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(&dest[i]), dstColor_x8);
    }
    #endif

    #if defined(__SSE2__)
    // Four pixels at a time
    for (; (i + 4) < dest.size(); i += 4) {
        // Load four mask values and expand each byte into 32-bit 000... or 111...
        __m128i mask_x4 = _mm_loadu_si32(mask.data() + i);
        mask_x4 = _mm_unpacklo_epi8(mask_x4, _mm_setzero_si128());
        mask_x4 = _mm_unpacklo_epi16(mask_x4, _mm_setzero_si128());
        mask_x4 = _mm_sub_epi32(_mm_setzero_si128(), mask_x4);

        const __m128i topColor_x4 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(&topColors[i]));
        const __m128i btmColor_x4 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(&btmColors[i]));

        const __m128i average_x4 = _mm_add_epi32(
            _mm_srli_epi32(_mm_and_si128(_mm_xor_si128(topColor_x4, btmColor_x4), _mm_set1_epi8(0xFE)), 1),
            _mm_and_si128(topColor_x4, btmColor_x4));

        // Blend with mask
        const __m128i dstColor_x4 =
            _mm_or_si128(_mm_and_si128(mask_x4, average_x4), _mm_andnot_si128(mask_x4, topColor_x4));

        // Write
        _mm_storeu_si128(reinterpret_cast<__m128i *>(&dest[i]), dstColor_x4);
    }
    #endif
#elif defined(_M_ARM64) || defined(__aarch64__)
    // Four pixels at a time
    for (; (i + 4) < dest.size(); i += 4) {
        // Load four mask values and expand each byte into 32-bit 000... or 111...
        uint32x4_t mask_x4 = vld1q_lane_u32(reinterpret_cast<const uint32 *>(mask.data() + i), vdupq_n_u32(0), 0);
        mask_x4 = vmovl_u16(vget_low_u16(vmovl_u8(vget_low_u8(mask_x4))));
        mask_x4 = vnegq_s32(mask_x4);

        const uint32x4_t topColor_x4 = vld1q_u32(reinterpret_cast<const uint32 *>(&topColors[i]));
        const uint32x4_t btmColor_x4 = vld1q_u32(reinterpret_cast<const uint32 *>(&btmColors[i]));

        // Halving average
        const uint32x4_t average_x4 = vhaddq_u8(topColor_x4, btmColor_x4);

        // Blend with mask
        const uint32x4_t dstColor_x4 = vbslq_u32(mask_x4, average_x4, topColor_x4);

        // Write
        vst1q_u32(reinterpret_cast<uint32 *>(&dest[i]), dstColor_x4);
    }
#endif

    for (; i < dest.size(); i++) {
        const Color888 &topColor = topColors[i];
        const Color888 &btmColor = btmColors[i];
        Color888 &dstColor = dest[i];
        if (mask[i]) {
            dstColor = AverageRGB888(topColor, btmColor);
        } else {
            dstColor = topColor;
        }
    }
}

FORCE_INLINE static void Color888CompositeRatioPerPixelMasked(const std::span<Color888> dest,
                                                              const std::span<const bool> mask,
                                                              const std::span<const Color888, kMaxResH> topColors,
                                                              const std::span<const Color888> btmColors,
                                                              const std::span<const uint8, kMaxResH> ratios) {
    size_t i = 0;

#if defined(_M_X64) || defined(__x86_64__)
    #if defined(__AVX2__)
    // Eight pixels at a time
    for (; (i + 8) < dest.size(); i += 8) {
        // Load eightmask values and expand each byte into 32-bit 000... or 111...
        __m256i mask_x8 = _mm256_cvtepu8_epi32(_mm_loadu_si64(mask.data() + i));
        mask_x8 = _mm256_sub_epi32(_mm256_setzero_si256(), mask_x8);

        // Load eight ratios and widen each byte into 32-bit lanes
        // Put each byte into a 32-bit lane
        __m256i ratio_x8 = _mm256_cvtepu8_epi32(_mm_loadu_si64(&ratios[i]));
        // Repeat the byte
        ratio_x8 = _mm256_mullo_epi32(ratio_x8, _mm256_set1_epi32(0x01'01'01'01));

        const __m256i topColor_x8 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(&topColors[i]));
        const __m256i btmColor_x8 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(&btmColors[i]));

        // Expand to 16-bit values
        __m256i ratio16lo_x8 = _mm256_unpacklo_epi8(ratio_x8, _mm256_setzero_si256());
        __m256i ratio16hi_x8 = _mm256_unpackhi_epi8(ratio_x8, _mm256_setzero_si256());

        const __m256i topColor16lo = _mm256_unpacklo_epi8(topColor_x8, _mm256_setzero_si256());
        const __m256i btmColor16lo = _mm256_unpacklo_epi8(btmColor_x8, _mm256_setzero_si256());

        const __m256i topColor16hi = _mm256_unpackhi_epi8(topColor_x8, _mm256_setzero_si256());
        const __m256i btmColor16hi = _mm256_unpackhi_epi8(btmColor_x8, _mm256_setzero_si256());

        // Lerp
        const __m256i dstColor16lo = _mm256_add_epi16(
            btmColor16lo,
            _mm256_srli_epi16(_mm256_mullo_epi16(_mm256_sub_epi16(topColor16lo, btmColor16lo), ratio16lo_x8), 5));
        const __m256i dstColor16hi = _mm256_add_epi16(
            btmColor16hi,
            _mm256_srli_epi16(_mm256_mullo_epi16(_mm256_sub_epi16(topColor16hi, btmColor16hi), ratio16hi_x8), 5));

        // Pack back into 8-bit values, be sure to truncate to avoid saturation
        __m256i dstColor_x8 = _mm256_packus_epi16(_mm256_and_si256(dstColor16lo, _mm256_set1_epi16(0xFF)),
                                                  _mm256_and_si256(dstColor16hi, _mm256_set1_epi16(0xFF)));

        // Blend with mask
        dstColor_x8 = _mm256_blendv_epi8(topColor_x8, dstColor_x8, mask_x8);

        // Write
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(&dest[i]), dstColor_x8);
    }
    #endif

    #if defined(__SSE2__)
    // Four pixels at a time
    for (; (i + 4) < dest.size(); i += 4) {
        // Load four mask values and expand each byte into 32-bit 000... or 111...
        __m128i mask_x4 = _mm_loadu_si32(mask.data() + i);
        mask_x4 = _mm_unpacklo_epi8(mask_x4, _mm_setzero_si128());
        mask_x4 = _mm_unpacklo_epi16(mask_x4, _mm_setzero_si128());
        mask_x4 = _mm_sub_epi32(_mm_setzero_si128(), mask_x4);

        const __m128i topColor_x4 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(&topColors[i]));
        const __m128i btmColor_x4 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(&btmColors[i]));

        // Load four ratios and splat each byte into 32-bit lanes
        __m128i ratio_x4 = _mm_loadu_si32(&ratios[i]);
        ratio_x4 = _mm_unpacklo_epi8(ratio_x4, ratio_x4);
        ratio_x4 = _mm_unpacklo_epi16(ratio_x4, ratio_x4);

        // Expand to 16-bit values
        const __m128i ratio16lo_x4 = _mm_unpacklo_epi8(ratio_x4, _mm_setzero_si128());
        const __m128i ratio16hi_x4 = _mm_unpackhi_epi8(ratio_x4, _mm_setzero_si128());

        const __m128i topColor16lo = _mm_unpacklo_epi8(topColor_x4, _mm_setzero_si128());
        const __m128i btmColor16lo = _mm_unpacklo_epi8(btmColor_x4, _mm_setzero_si128());

        const __m128i topColor16hi = _mm_unpackhi_epi8(topColor_x4, _mm_setzero_si128());
        const __m128i btmColor16hi = _mm_unpackhi_epi8(btmColor_x4, _mm_setzero_si128());

        // Composite
        const __m128i dstColor16lo = _mm_add_epi16(
            btmColor16lo, _mm_srli_epi16(_mm_mullo_epi16(_mm_sub_epi16(topColor16lo, btmColor16lo), ratio16lo_x4), 5));
        const __m128i dstColor16hi = _mm_add_epi16(
            btmColor16hi, _mm_srli_epi16(_mm_mullo_epi16(_mm_sub_epi16(topColor16hi, btmColor16hi), ratio16hi_x4), 5));

        // Pack back into 8-bit values, be sure to truncate to avoid saturation
        __m128i dstColor_x4 = _mm_packus_epi16(_mm_and_si128(dstColor16lo, _mm_set1_epi16(0xFF)),
                                               _mm_and_si128(dstColor16hi, _mm_set1_epi16(0xFF)));

        // Blend with mask
        dstColor_x4 = _mm_or_si128(_mm_and_si128(mask_x4, dstColor_x4), _mm_andnot_si128(mask_x4, topColor_x4));

        // Write
        _mm_storeu_si128(reinterpret_cast<__m128i *>(&dest[i]), dstColor_x4);
    }
    #endif
#elif defined(_M_ARM64) || defined(__aarch64__)
    // Four pixels at a time
    for (; (i + 4) < dest.size(); i += 4) {
        // Load four mask values and expand each byte into 32-bit 000... or 111...
        uint32x4_t mask_x4 = vld1q_lane_u32(reinterpret_cast<const uint32 *>(mask.data() + i), vdupq_n_u32(0), 0);
        mask_x4 = vmovl_u16(vget_low_u16(vmovl_u8(vget_low_u8(mask_x4))));
        mask_x4 = vnegq_s32(mask_x4);

        // Load four ratios and splat each byte into 32-bit lanes
        uint32x4_t ratio_x4 = vld1q_lane_u32(reinterpret_cast<const uint32 *>(ratios.data() + i), vdupq_n_u32(0), 0);
        // 8 -> 16
        ratio_x4 = vzip1q_u8(ratio_x4, ratio_x4);
        // 16 -> 32
        ratio_x4 = vzip1q_u16(ratio_x4, ratio_x4);

        const uint32x4_t topColor_x4 = vld1q_u32(reinterpret_cast<const uint32 *>(&topColors[i]));
        const uint32x4_t btmColor_x4 = vld1q_u32(reinterpret_cast<const uint32 *>(&btmColors[i]));

        const uint16x8_t topColor16lo = vmovl_u8(vget_low_u8(topColor_x4));
        const uint16x8_t btmColor16lo = vmovl_u8(vget_low_u8(btmColor_x4));

        const uint16x8_t topColor16hi = vmovl_high_u8(topColor_x4);
        const uint16x8_t btmColor16hi = vmovl_high_u8(btmColor_x4);

        // Composite
        int16x8_t composite16lo = vsubq_s16(topColor16lo, btmColor16lo);
        int16x8_t composite16hi = vsubq_s16(topColor16hi, btmColor16hi);

        composite16lo = vmulq_u16(composite16lo, vmovl_u8(vget_low_s8(ratio_x4)));
        composite16hi = vmulq_u16(composite16hi, vmovl_high_u8(ratio_x4));

        composite16lo = vsraq_n_s16(vmovl_s8(vget_low_s8(btmColor_x4)), composite16lo, 5);
        composite16hi = vsraq_n_s16(vmovl_high_s8(btmColor_x4), composite16hi, 5);

        int8x16_t composite_x4 = vmovn_high_s16(vmovn_s16(composite16lo), composite16hi);

        // Blend with mask
        const uint32x4_t dstColor_x4 = vbslq_u32(mask_x4, composite_x4, topColor_x4);

        // Write
        vst1q_u32(reinterpret_cast<uint32 *>(&dest[i]), dstColor_x4);
    }
#endif

    for (; i < dest.size(); i++) {
        const Color888 &topColor = topColors[i];
        const Color888 &btmColor = btmColors[i];
        const uint8 ratio = ratios[i];
        Color888 &dstColor = dest[i];
        if (mask[i]) {
            dstColor.r = btmColor.r + ((((int)topColor.r - (int)btmColor.r) * ratio) >> 5);
            dstColor.g = btmColor.g + ((((int)topColor.g - (int)btmColor.g) * ratio) >> 5);
            dstColor.b = btmColor.b + ((((int)topColor.b - (int)btmColor.b) * ratio) >> 5);
        } else {
            dstColor = topColor;
        }
    }
}

template <bool deinterlace, bool transparentMeshes>
FORCE_INLINE void SoftwareVDPRenderer::VDP2ComposeLine(uint32 y, const VDP2Regs &regs2, bool altField) {
    const VDP2State &state2 = m_state.state2;
    const auto &colorCalcParams = regs2.colorCalcParams;

    y = VDP2GetY<deinterlace>(y, regs2) ^ static_cast<uint32>(altField);

    if (!regs2.TVMD.DISP) {
        uint32 color = 0xFF000000;
        if (regs2.borderColorModeLatch) {
            color |= state2.lineBackLayerState.backColor.u32;
        }
        std::fill_n(&m_framebuffer[y * m_HRes], m_HRes, color);
        return;
    }

    auto &composeLineBuffers = m_composeLineBuffers[altField];
    auto &spriteLayerAttrs = m_spriteLayerAttrs[altField];

    auto &scanline_layers = composeLineBuffers.scanline_layers;
    const auto &scanline_layerPrios = composeLineBuffers.scanline_layerPrios;

    // Determine layer order
    std::array<std::array<uint8, 3>, kMaxResH> layerSortOrder;
    static constexpr std::array<uint8, 3> kLayerSortOrderInit{uint8(LYR_Back ^ 7), uint8(LYR_Back ^ 7),
                                                              uint8(LYR_Back ^ 7)};
    std::fill_n(layerSortOrder.begin(), m_HRes, kLayerSortOrderInit);

    for (uint32 layer = 0; layer < m_layerOutputs[altField].size(); layer++) {
        if (!state2.layerEnabled[layer]) {
            continue;
        }

        const LayerOutput &output = m_layerOutputs[altField][layer];

        if (AllZeroU8(std::span{output.pixels.priority}.first(m_HRes))) {
            // All priorities are zero
            continue;
        }

        const uint8 layerKey = layer ^ 7u;

        auto push = [&](uint32 x, uint8 priority) {
            // Insert the layer into the appropriate position in the stack
            // - Higher priority beats lower priority
            // - If same priority, lower Layer index beats higher Layer index
            // - Index 0 is topmost (first) layer
            auto &entry = layerSortOrder[x];
            const uint8 key = layerKey | (priority << 3u);
            for (int i = 0; i < 3; i++) {
                if (key > entry[i]) {
                    // Push layers back
                    for (int j = 2; j > i; j--) {
                        entry[j] = entry[j - 1];
                    }
                    entry[i] = key;
                    break;
                }
            }
        };

        if (layer == LYR_Sprite) {
            for (uint32 x = 0; x < m_HRes; x++) {
                const uint8 priority = output.pixels.priority[x];
                if (priority == 0) {
                    continue;
                }
                if (spriteLayerAttrs.specialType[x] != SpriteData::Special::Normal) {
                    continue;
                }

                push(x, priority);
            }
        } else {
            for (uint32 x = 0; x < m_HRes; x++) {
                const uint8 priority = output.pixels.priority[x];
                if (priority == 0) {
                    continue;
                }

                push(x, priority);
            }
        }
    }
    for (uint32 x = 0; x < m_HRes; x++) {
        const auto &entry = layerSortOrder[x];
        for (int i = 0; i < 3; i++) {
            composeLineBuffers.scanline_layers[x][i] = static_cast<LayerIndex>(bit::extract<0, 2>(~entry[i]));
            composeLineBuffers.scanline_layerPrios[x][i] = entry[i] >> 3u;
        }
    }

    // Find the sprite mesh layers
    auto &scanline_meshLayers = composeLineBuffers.scanline_meshLayers;
    if constexpr (transparentMeshes) {
        std::fill_n(scanline_meshLayers.begin(), m_HRes, 0xFF);

        if (state2.layerEnabled[0] &&
            !AllZeroU8(std::span{m_meshLayerOutput[altField].pixels.priority}.first(m_HRes))) {

            for (uint32 x = 0; x < m_HRes; x++) {
                const uint8 priority = m_meshLayerOutput[altField].pixels.priority[x];
                if (priority == 0) {
                    continue;
                }
                if (m_meshLayerAttrs[altField].specialType[x] != SpriteData::Special::Normal) {
                    continue;
                }

                const std::array<uint8, 3> &layerPrios = scanline_layerPrios[x];
                for (int i = 0; i < 3; i++) {
                    // The sprite layer has the highest priority on ties, so the priority check can be simplified.
                    // Sprite pixels drawn of top of mesh pixels erase the corresponding pixels from the mesh layer,
                    // therefore the mesh layer can be considered always on top of the sprite layer.
                    if (priority >= layerPrios[i]) {
                        scanline_meshLayers[x] = i;
                        break;
                    }
                }
            }
        }
    }

    // Retrieves the color of the given layer
    auto getLayerColor = [&](LayerIndex layer, uint32 x) -> Color888 {
        if (layer == LYR_Back) {
            return state2.lineBackLayerState.backColor;
        } else {
            return m_layerOutputs[altField][layer].pixels.color[x];
        }
    };

    const bool restrictedColorCalc = regs2.restrictedColorCalc;

    // Determines if color calculation is enabled for the given layer
    const auto isColorCalcEnabled = [&](LayerIndex layer, uint32 x) {
        if (layer == LYR_Sprite) {
            const SpriteParams &spriteParams = regs2.spriteParams;
            if (!spriteParams.colorCalcEnable) {
                return false;
            }
            const auto &pixels = m_layerOutputs[altField][LYR_Sprite].pixels;
            if (restrictedColorCalc && pixels.specialColorCalc[x]) {
                return false;
            }

            const uint8 pixelPriority = pixels.priority[x];

            using enum SpriteColorCalculationCondition;
            switch (spriteParams.colorCalcCond) {
            case PriorityLessThanOrEqual: return pixelPriority <= spriteParams.colorCalcValue;
            case PriorityEqual: return pixelPriority == spriteParams.colorCalcValue;
            case PriorityGreaterThanOrEqual: return pixelPriority >= spriteParams.colorCalcValue;
            case MsbEqualsOne: return m_layerOutputs[altField][LYR_Sprite].pixels.color[x].msb == 1;
            default: util::unreachable();
            }
        } else if (layer == LYR_Back) {
            return regs2.backScreenParams.colorCalcEnable;
        } else {
            const auto &bgParams = regs2.bgParams[layer - LYR_RBG0];
            if (!bgParams.colorCalcEnable) {
                return false;
            }
            if (restrictedColorCalc && IsPaletteColorFormat(bgParams.colorFormat)) {
                return false;
            }
            return true;
        }
    };

    // Gather layer 0 data
    auto &layer0Pixels = composeLineBuffers.layer0Pixels;
    auto &layer0ColorCalcEnabled = composeLineBuffers.layer0ColorCalcEnabled;
    auto &layer0BlendMeshLayer = composeLineBuffers.layer0BlendMeshLayer;
    auto &layer0ShadowEnabled = composeLineBuffers.layer0ShadowEnabled;
    auto &layer0ColorOffsetEnabled = composeLineBuffers.layer0ColorOffsetEnabled;
    for (uint32 x = 0; x < m_HRes; x++) {
        const LayerIndex layer = scanline_layers[x][0];

        // Color
        layer0Pixels[x] = getLayerColor(layer, x);

        // Color calculation
        if constexpr (transparentMeshes) {
            layer0BlendMeshLayer[x] = scanline_meshLayers[x] == 0;
        }
        if (m_colorCalcWindow[altField][x]) {
            layer0ColorCalcEnabled[x] = false;
        } else if (!isColorCalcEnabled(layer, x)) {
            layer0ColorCalcEnabled[x] = false;
        } else {
            switch (layer) {
            case LYR_Back: [[fallthrough]];
            case LYR_Sprite: layer0ColorCalcEnabled[x] = true; break;
            default: layer0ColorCalcEnabled[x] = m_layerOutputs[altField][layer].pixels.specialColorCalc[x]; break;
            }
        }

        // Shadow
        if (m_layerOutputs[altField][LYR_Sprite].pixels.priority[x] < scanline_layerPrios[x][0]) {
            // Sprite layer is beneath top layer
            layer0ShadowEnabled[x] = false;
        } else {
            // Sprite layer doesn't have shadow
            const bool isNormalShadow = spriteLayerAttrs.specialType[x] == SpriteData::Special::Shadow;
            const bool isMSBShadow = !regs2.spriteParams.useSpriteWindow && spriteLayerAttrs.shadowOrWindow[x];
            if (!isNormalShadow && !isMSBShadow) {
                layer0ShadowEnabled[x] = false;
            } else {
                switch (layer) {
                case LYR_Sprite: layer0ShadowEnabled[x] = spriteLayerAttrs.shadowOrWindow[x]; break;
                case LYR_Back: layer0ShadowEnabled[x] = regs2.backScreenParams.shadowEnable; break;
                default: layer0ShadowEnabled[x] = regs2.bgParams[layer - LYR_RBG0].shadowEnable; break;
                }
            }
        }

        // Color offset
        if (!regs2.colorOffsetEnable[layer]) {
            layer0ColorOffsetEnabled[x] = false;
        } else {
            const auto &colorOffset = regs2.colorOffset[regs2.colorOffsetSelect[layer]];
            layer0ColorOffsetEnabled[x] = colorOffset.nonZero;
        }
    }

    const std::span<Color888> framebufferOutput(reinterpret_cast<Color888 *>(&m_framebuffer[y * m_HRes]), m_HRes);

    const bool normalTVMode = regs2.TVMD.HRESOn < 2;
    const uint8 cramMode = regs2.vramControl.colorRAMMode;
    const bool useColorGrad = normalTVMode && cramMode == 0 && colorCalcParams.colorGradEnable;
    static constexpr LayerIndex kColorGradLayers[] = {
        LYR_Sprite, LYR_RBG0, LYR_NBG0_RBG1, LYR_Invalid, LYR_NBG1_EXBG, LYR_NBG2, LYR_NBG3, LYR_Invalid,
    };

    static constexpr ColorGradScreen kColorGradScreens[] = {
        ColorGradScreen::Sprite,    ColorGradScreen::RBG0, ColorGradScreen::NBG0_RBG1,
        ColorGradScreen::NBG1_EXBG, ColorGradScreen::NBG2, ColorGradScreen::NBG3,
    };

    if (AnyBool(std::span{layer0ColorCalcEnabled}.first(m_HRes))) {
        const bool doubleResH = m_HRes > kMaxNormalResH;
        const uint32 xShift = doubleResH ? 1 : 0;

        // Gather color calculation data
        auto &layer1Pixels = composeLineBuffers.layer1Pixels;
        auto &layer1BlendMeshLayer = composeLineBuffers.layer1BlendMeshLayer;
        auto &layer0LineColorEnabled = composeLineBuffers.layer0LineColorEnabled;
        auto &layer0LineColors = composeLineBuffers.layer0LineColors;
        for (uint32 x = 0; x < m_HRes; x++) {
            const LayerIndex layer0 = scanline_layers[x][0];
            const LayerIndex layer1 = scanline_layers[x][1];

            // Layer 1 colors
            layer1Pixels[x] = getLayerColor(layer1, x);
            if constexpr (transparentMeshes) {
                layer1BlendMeshLayer[x] = scanline_meshLayers[x] == 1;
            }

            // Line color
            if (useColorGrad) {
                // Color gradation prevents line color screen insertion
                layer0LineColorEnabled[x] = false;
            } else {
                switch (layer0) {
                case LYR_Sprite:
                    layer0LineColorEnabled[x] = regs2.spriteParams.lineColorScreenEnable;
                    if (layer0LineColorEnabled[x]) {
                        layer0LineColors[x] = state2.lineBackLayerState.lineColor;
                    }
                    break;
                case LYR_Back: layer0LineColorEnabled[x] = false; break;
                default:
                    layer0LineColorEnabled[x] = regs2.bgParams[layer0 - LYR_RBG0].lineColorScreenEnable;
                    if (layer0LineColorEnabled[x]) {
                        if (layer0 == LYR_RBG0 || (layer0 == LYR_NBG0_RBG1 && regs2.bgEnabled[5])) {
                            layer0LineColors[x] = m_rbgLineColors[layer0 - LYR_RBG0][x >> xShift];
                        } else {
                            layer0LineColors[x] = state2.lineBackLayerState.lineColor;
                        }
                    }
                    break;
                }
            }
        }

        // Compute layer 1 output
        if (useColorGrad) {
            // Compute color gradation
            const auto colorGradIndex = static_cast<size_t>(colorCalcParams.colorGradScreen);
            const LayerIndex colorGradLayer = kColorGradLayers[colorGradIndex];

            auto &mask = composeLineBuffers.colorGradEnabled;
            for (uint32 x = 0; x < m_HRes; x++) {
                mask[x] = scanline_layers[x][0] == colorGradLayer || scanline_layers[x][1] == colorGradLayer;
            }

            auto &input = m_layerOutputs[altField][colorGradLayer].pixels.color;
            auto &output = composeLineBuffers.colorGradLayerColors;

            // TODO: should pixels 0 and 1 pull from pixels -1 and -2?
            output[0] = input[0];
            output[1] = AverageRGB888(input[0], input[1]);
            Color888GradationMasked(std::span{output}.subspan(2, m_HRes - 2), std::span{mask}, std::span{input});

            // Set layer 1 output to the color gradation screen where the designated screen is the topmost two layers
            for (uint32 x = 0; x < m_HRes; x++) {
                if (scanline_layers[x][0] == colorGradLayer || scanline_layers[x][1] == colorGradLayer) {
                    scanline_layers[x][1] = colorGradLayer;
                    layer1Pixels[x] = output[x];
                }
            }
        } else if (normalTVMode && colorCalcParams.extendedColorCalcEnable) {
            // Apply color gradation or extended color calculations to layer 1
            auto &layer1ColorCalcEnabled = composeLineBuffers.layer1ColorCalcEnabled;
            auto &layer2Pixels = composeLineBuffers.layer2Pixels;
            auto &layer2BlendMeshLayer = composeLineBuffers.layer2BlendMeshLayer;

            // Gather pixels for layer 2
            for (uint32 x = 0; x < m_HRes; x++) {
                layer1ColorCalcEnabled[x] = isColorCalcEnabled(scanline_layers[x][1], x);
                if (layer1ColorCalcEnabled[x]) {
                    layer2Pixels[x] = getLayerColor(scanline_layers[x][2], x);
                }
                if constexpr (transparentMeshes) {
                    layer2BlendMeshLayer[x] = scanline_meshLayers[x] == 2;
                }
            }

            // Blend layer 2 with sprite mesh layer colors
            // TODO: apply color calculation effects
            if constexpr (transparentMeshes) {
                Color888AverageMasked(std::span{layer2Pixels}.first(m_HRes), layer2BlendMeshLayer, layer2Pixels,
                                      m_meshLayerOutput[altField].pixels.color);
            }

            Color888AverageMasked(std::span{layer1Pixels}.first(m_HRes), layer1ColorCalcEnabled, layer1Pixels,
                                  layer2Pixels);

            if (regs2.lineScreenParams.colorCalcEnable) {
                // Blend line color if top layer uses it
                Color888AverageMasked(std::span{layer1Pixels}.first(m_HRes), layer0LineColorEnabled, layer1Pixels,
                                      layer0LineColors);
            } else {
                // Replace with line color if top layer uses it
                Color888SelectMasked(std::span{layer1Pixels}.first(m_HRes), layer0LineColorEnabled, layer1Pixels,
                                     layer0LineColors);
            }
        } else {
            // Replace layer 1 pixels with line color screen where applicable
            Color888SelectMasked(std::span{layer1Pixels}.first(m_HRes), layer0LineColorEnabled, layer1Pixels,
                                 layer0LineColors);
        }

        // Blend layer 1 with sprite mesh layer colors
        // TODO: apply color calculation effects
        if constexpr (transparentMeshes) {
            Color888AverageMasked(std::span{layer1Pixels}.first(m_HRes), layer1BlendMeshLayer, layer1Pixels,
                                  m_meshLayerOutput[altField].pixels.color);
        }

        // Blend layer 0 and layer 1
        if (colorCalcParams.useAdditiveBlend) {
            // Saturated add
            Color888SatAddMasked(framebufferOutput, layer0ColorCalcEnabled, layer0Pixels, layer1Pixels);
        } else {
            // Gather color ratio info
            auto &scanline_ratio = composeLineBuffers.scanline_ratio;
            for (uint32 x = 0; x < m_HRes; x++) {
                if (!layer0ColorCalcEnabled[x]) {
                    continue;
                }

                if (colorCalcParams.useSecondScreenRatio && layer0LineColorEnabled[x]) {
                    scanline_ratio[x] = regs2.lineScreenParams.colorCalcRatio;
                } else {
                    const LayerIndex layer = scanline_layers[x][colorCalcParams.useSecondScreenRatio];
                    switch (layer) {
                    case LYR_Sprite: scanline_ratio[x] = spriteLayerAttrs.colorCalcRatio[x]; break;
                    case LYR_Back: scanline_ratio[x] = regs2.backScreenParams.colorCalcRatio; break;
                    default: scanline_ratio[x] = regs2.bgParams[layer - LYR_RBG0].colorCalcRatio; break;
                    }
                }
            }

            // Alpha composite
            Color888CompositeRatioPerPixelMasked(framebufferOutput, layer0ColorCalcEnabled, layer0Pixels, layer1Pixels,
                                                 scanline_ratio);
        }
    } else {
        std::copy_n(layer0Pixels.cbegin(), framebufferOutput.size(), framebufferOutput.begin());
    }

    // Apply sprite shadow
    // TODO: apply shadow from mesh layer
    if (AnyBool(std::span{layer0ShadowEnabled}.first(m_HRes))) {
        Color888ShadowMasked(framebufferOutput, layer0ShadowEnabled);
    }

    // Apply color offset if enabled
    if (AnyBool(std::span{layer0ColorOffsetEnabled}.first(m_HRes))) {
        for (uint32 x = 0; Color888 &outputColor : framebufferOutput) {
            if (layer0ColorOffsetEnabled[x]) {
                const auto &colorOffset = regs2.colorOffset[regs2.colorOffsetSelect[scanline_layers[x][0]]];
                outputColor = {
                    .r = kColorOffsetLUT[colorOffset.r][outputColor.r],
                    .g = kColorOffsetLUT[colorOffset.g][outputColor.g],
                    .b = kColorOffsetLUT[colorOffset.b][outputColor.b],
                    .pad = 0,
                    .msb = 0,
                };
            }
            ++x;
        }
    }

    // Blend layer 0 with sprite mesh layer colors
    if constexpr (transparentMeshes) {
        const SpriteParams &spriteParams = regs2.spriteParams;
        std::span<Color888> meshOut = std::span{m_meshLayerOutput[altField].pixels.color}.first(m_HRes);
        if (spriteParams.colorCalcEnable) {
            std::array<bool, kMaxResH> &layer0MeshColorCalcEnabled = composeLineBuffers.layer0MeshColorCalcEnabled;
            for (uint32 x = 0; x < m_HRes; ++x) {
                const uint8 pixelPriority = m_meshLayerOutput[altField].pixels.priority[x];

                using enum SpriteColorCalculationCondition;
                switch (spriteParams.colorCalcCond) {
                case PriorityLessThanOrEqual:
                    layer0MeshColorCalcEnabled[x] = pixelPriority <= spriteParams.colorCalcValue;
                    break;
                case PriorityEqual: layer0MeshColorCalcEnabled[x] = pixelPriority == spriteParams.colorCalcValue; break;
                case PriorityGreaterThanOrEqual:
                    layer0MeshColorCalcEnabled[x] = pixelPriority >= spriteParams.colorCalcValue;
                    break;
                case MsbEqualsOne:
                    layer0MeshColorCalcEnabled[x] = m_layerOutputs[altField][LYR_Sprite].pixels.color[x].msb == 1;
                    break;
                default: util::unreachable();
                }
            }

            // Apply color calculation
            if (AnyBool(std::span{layer0MeshColorCalcEnabled}.first(m_HRes))) {
                meshOut = std::span{composeLineBuffers.meshTempColors}.first(m_HRes);
                if (colorCalcParams.useAdditiveBlend) {
                    // Saturated add
                    Color888SatAddMasked(meshOut, layer0MeshColorCalcEnabled, m_meshLayerOutput[altField].pixels.color,
                                         framebufferOutput);
                } else {
                    // Alpha composite
                    Color888CompositeRatioPerPixelMasked(meshOut, layer0MeshColorCalcEnabled,
                                                         m_meshLayerOutput[altField].pixels.color, framebufferOutput,
                                                         m_meshLayerAttrs[altField].colorCalcRatio);
                }
            }
        }

        // Apply color offset if enabled
        if (regs2.colorOffsetEnable[LYR_Sprite]) {
            for (uint32 x = 0; Color888 &meshColor : meshOut) {
                const auto &colorOffset = regs2.colorOffset[regs2.colorOffsetSelect[LYR_Sprite]];
                if (colorOffset.nonZero) {
                    meshColor = {
                        .r = kColorOffsetLUT[colorOffset.r][meshColor.r],
                        .g = kColorOffsetLUT[colorOffset.g][meshColor.g],
                        .b = kColorOffsetLUT[colorOffset.b][meshColor.b],
                        .pad = 0,
                        .msb = 0,
                    };
                }
                ++x;
            }
        }

        // Blend with output
        Color888AverageMasked(framebufferOutput, layer0BlendMeshLayer, framebufferOutput, meshOut);
    }

    if (m_vdp2DebugRenderOptions.overlay.enable) {
        const auto &overlay = m_vdp2DebugRenderOptions.overlay;
        using OverlayType = config::VDP2DebugRender::Overlay::Type;

        if (overlay.type != OverlayType::None) {
            if (overlay.type == OverlayType::Windows && overlay.windowLayerIndex > 5) {
                const auto &windowSet = overlay.customWindowSet;
                auto &windowState = composeLineBuffers.customWindowState[altField];
                auto windowParams = regs2.windowParams;
                for (uint32 i = 0; i < 2; ++i) {
                    windowParams[i].lineWindowTableEnable = overlay.customLineWindowTableEnable[i];
                    windowParams[i].lineWindowTableAddress = overlay.customLineWindowTableAddress[i] & 0x7FFFF;
                }
                if (altField) {
                    VDP2CalcWindow<true>(y, regs2, windowSet, windowState);
                } else {
                    VDP2CalcWindow<false>(y, regs2, windowSet, windowState);
                }
            }

            for (uint32 x = 0; x < m_HRes; ++x) {
                Color888 overlayColor{};

                switch (overlay.type) {
                case OverlayType::None: break;
                case OverlayType::SingleLayer: //
                {
                    const uint8 layerLevel = std::min<uint8>(overlay.singleLayerIndex, 11);
                    switch (layerLevel) {
                    case LYR_Back: overlayColor = state2.lineBackLayerState.backColor; break;
                    case LYR_LineColor: overlayColor = state2.lineBackLayerState.lineColor; break;
                    case 8 /*RBG0 line color*/:
                    case 9 /*RBG1 line color*/: {
                        const bool doubleResH = m_HRes > kMaxNormalResH;
                        const uint32 xShift = doubleResH ? 1 : 0;
                        overlayColor = m_rbgLineColors[layerLevel - 8][x >> xShift];
                        break;
                    }
                    case 10 /*transparent meshes*/: overlayColor = m_meshLayerOutput[altField].pixels.color[x]; break;
                    case 11 /*gradation screen*/:
                        if (useColorGrad) {
                            overlayColor = composeLineBuffers.colorGradLayerColors[x];
                        }
                        break;
                    default: overlayColor = m_layerOutputs[altField][layerLevel].pixels.color[x];
                    }
                    break;
                }
                case OverlayType::LayerStack: //
                {
                    const uint8 layerLevel = overlay.layerStackIndex < 3 ? overlay.layerStackIndex : 0;
                    const uint32 layerNum = static_cast<uint32>(scanline_layers[x][layerLevel]);
                    overlayColor = overlay.layerColors[layerNum];
                    break;
                }
                case OverlayType::PriorityStack: //
                {
                    const uint8 layerLevel = overlay.priorityStackIndex < 3 ? overlay.priorityStackIndex : 0;
                    const uint32 layerNum = static_cast<uint32>(scanline_layerPrios[x][layerLevel]);
                    overlayColor = overlay.priorityColors[layerNum];
                    break;
                }
                case OverlayType::Windows: //
                {
                    const uint8 layerIndex = overlay.windowLayerIndex;
                    switch (layerIndex) {
                    case 0: // Sprite
                        overlayColor =
                            spriteLayerAttrs.window[x] ? overlay.windowInsideColor : overlay.windowOutsideColor;
                        break;
                    case 1: [[fallthrough]]; // RBG0
                    case 2: [[fallthrough]]; // NBG0/RBG1
                    case 3: [[fallthrough]]; // NBG1/EXBG
                    case 4: [[fallthrough]]; // NBG2
                    case 5:                  // NBG3
                        overlayColor = m_bgWindows[altField][layerIndex - 1][x] ? overlay.windowInsideColor
                                                                                : overlay.windowOutsideColor;
                        break;
                    case 6: // Rotation parameters
                        overlayColor =
                            m_rotParamsWindow[altField][x] ? overlay.windowInsideColor : overlay.windowOutsideColor;
                        break;
                    case 7: // Color calculations
                        overlayColor =
                            m_colorCalcWindow[altField][x] ? overlay.windowInsideColor : overlay.windowOutsideColor;
                        break;
                    default: // Custom window
                        overlayColor = composeLineBuffers.customWindowState[altField][x] ? overlay.windowInsideColor
                                                                                         : overlay.windowOutsideColor;
                        break;
                    }

                    break;
                }
                case OverlayType::RotParams: //
                    overlayColor = VDP2SelectRotationParameter(x, regs2, altField) == RotParamA
                                       ? overlay.rotParamAColor
                                       : overlay.rotParamBColor;
                    break;
                case OverlayType::ColorCalc: //
                {
                    const uint8 stackIndex = overlay.colorCalcStackIndex <= 1 ? overlay.colorCalcStackIndex : 0;
                    const bool colorCalc =
                        stackIndex == 0 ? layer0ColorCalcEnabled[x] : composeLineBuffers.layer1ColorCalcEnabled[x];
                    overlayColor = colorCalc ? overlay.colorCalcEnableColor : overlay.colorCalcDisableColor;
                    break;
                }
                case OverlayType::ColorGradation: //
                {
                    const uint8 stackIndex = overlay.colorGradStackIndex <= 1 ? overlay.colorGradStackIndex : 0;
                    const LayerIndex stackLayer = scanline_layers[x][stackIndex];
                    if (stackLayer <= LYR_NBG3) {
                        const ColorGradScreen screen = kColorGradScreens[stackLayer];
                        overlayColor = colorCalcParams.colorGradEnable && screen == colorCalcParams.colorGradScreen
                                           ? overlay.colorGradEnableColor
                                           : overlay.colorGradDisableColor;
                    }
                    break;
                }
                case OverlayType::Shadow: //
                {
                    overlayColor = layer0ShadowEnabled[x] ? overlay.shadowEnableColor : overlay.shadowDisableColor;
                    break;
                }
                }

                const uint8 alpha = overlay.alpha;
                Color888 &out = framebufferOutput[x];
                out.r = out.r + ((int)overlayColor.r - out.r) * alpha / 255;
                out.g = out.g + ((int)overlayColor.g - out.g) * alpha / 255;
                out.b = out.b + ((int)overlayColor.b - out.b) * alpha / 255;
            }
        }
    }

    // Opaque alpha
    for (Color888 &outputColor : framebufferOutput) {
        outputColor.u32 |= 0xFF000000;
    }
}

template <SoftwareVDPRenderer::CharacterMode charMode, bool fourCellChar, ColorFormat colorFormat, uint32 colorMode,
          bool useVCellScroll, bool deinterlace>
NO_INLINE void SoftwareVDPRenderer::VDP2DrawNormalScrollBG(const VDP2Regs &regs2, const BGParams &bgParams,
                                                           LayerOutput &layerOut, const NBGLayerState &bgState,
                                                           VRAMFetcher &vramFetcher, std::span<const bool> windowState,
                                                           bool altField) {
    const bool altLine = deinterlace && altField && regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity;
    uint32 fracScrollX = bgState.fracScrollX + bgParams.scrollAmountH;
    const uint32 fracScrollY = bgState.fracScrollY + bgParams.scrollAmountV + (altLine ? bgParams.scrollIncV : 0);

    uint32 cellScrollTableAddress = regs2.vcellScrollTableAddress + bgState.vcellScrollOffset;
    const bool vcellScrollEnable = useVCellScroll && bgParams.vcellScrollEnable;

    auto readCellScrollY = [&](bool checkRepeat = false) {
        if (checkRepeat && bgState.vcellScrollRepeat && bgState.vcellScrollDelay) {
            return vramFetcher.lastVCellScroll;
        }
        const uint32 value = VDP2ReadRendererVRAM<uint32>(cellScrollTableAddress);
        if (!checkRepeat || !bgState.vcellScrollRepeat) {
            cellScrollTableAddress += regs2.vcellScrollInc;
        }
        const uint32 prevValue = vramFetcher.lastVCellScroll;
        vramFetcher.lastVCellScroll = bit::extract<8, 26>(value);
        return bgState.vcellScrollDelay ? prevValue : vramFetcher.lastVCellScroll;
    };

    uint32 mosaicCounterX = 0;
    uint32 vcellScrollY = 0;
    uint32 vcellScrollX = fracScrollX >> (8u + 3u);

    if (vcellScrollEnable) {
        vcellScrollY = readCellScrollY(true);
    }

    for (uint32 x = 0; x < m_HRes; x++) {
        // Apply horizontal mosaic or vertical cell-scrolling
        // Mosaic takes priority
        if (bgParams.mosaicEnable) {
            // Apply horizontal mosaic
            const uint8 currMosaicCounterX = mosaicCounterX;
            mosaicCounterX++;
            if (mosaicCounterX >= regs2.mosaicH) {
                mosaicCounterX = 0;
            }
            if (currMosaicCounterX > 0) {
                // Simply copy over the data from the previous pixel
                layerOut.pixels.CopyPixel(x - 1, x);

                // Increment horizontal coordinate
                fracScrollX += bgState.scrollIncH;
                continue;
            }
        } else if (vcellScrollEnable) {
            // Update vertical cell scroll amount
            if ((fracScrollX >> (8u + 3u)) != vcellScrollX) {
                vcellScrollX = fracScrollX >> (8u + 3u);
                vcellScrollY = readCellScrollY();
            }
        }

        if (windowState[x]) {
            // Make pixel transparent if inside active window area
            layerOut.pixels.priority[x] = 0;
        } else {
            // Compute integer scroll screen coordinates
            const uint32 scrollX = fracScrollX >> 8u;
            const uint32 scrollY = (fracScrollY + vcellScrollY) >> 8u;
            const CoordU32 scrollCoord{scrollX, scrollY};

            // Plot pixel
            const Pixel pixel = VDP2FetchScrollBGPixel<false, charMode, fourCellChar, colorFormat, colorMode>(
                bgParams, regs2, bgParams.pageBaseAddresses, bgParams.pageShiftH, bgParams.pageShiftV, scrollCoord,
                vramFetcher);
            layerOut.pixels.SetPixel(x, pixel);
        }

        // Increment horizontal coordinate
        fracScrollX += bgState.scrollIncH;
    }

    // Fetch one extra tile past the end of the display area
    {
        // Apply horizontal mosaic or vertical cell-scrolling
        // Mosaic takes priority
        if (!bgParams.mosaicEnable && vcellScrollEnable) {
            // Update vertical cell scroll amount
            if ((fracScrollX >> (8u + 3u)) != vcellScrollX) {
                vcellScrollX = fracScrollX >> (8u + 3u);
                vcellScrollY = readCellScrollY();
            }
        }

        // Compute integer scroll screen coordinates
        const uint32 scrollX = fracScrollX >> 8u;
        const uint32 scrollY = (fracScrollY + vcellScrollY) >> 8u;
        const CoordU32 scrollCoord{scrollX, scrollY};

        // Fetch pixel
        VDP2FetchScrollBGPixel<false, charMode, fourCellChar, colorFormat, colorMode>(
            bgParams, regs2, bgParams.pageBaseAddresses, bgParams.pageShiftH, bgParams.pageShiftV, scrollCoord,
            vramFetcher);
    }
}

template <ColorFormat colorFormat, uint32 colorMode, bool useVCellScroll, bool deinterlace>
NO_INLINE void SoftwareVDPRenderer::VDP2DrawNormalBitmapBG(const VDP2Regs &regs2, const BGParams &bgParams,
                                                           LayerOutput &layerOut, const NBGLayerState &bgState,
                                                           VRAMFetcher &vramFetcher, std::span<const bool> windowState,
                                                           bool altField) {
    const bool doubleDensity = regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity;
    const bool altLine = deinterlace && altField && doubleDensity && !bgParams.lineScrollYEnable;
    uint32 fracScrollX = bgState.fracScrollX + bgParams.scrollAmountH;
    const uint32 fracScrollY = bgState.fracScrollY + bgParams.scrollAmountV + (altLine ? bgParams.scrollIncV : 0);

    uint32 cellScrollTableAddress = regs2.vcellScrollTableAddress + bgState.vcellScrollOffset;
    const bool vcellScrollEnable = useVCellScroll && bgParams.vcellScrollEnable;

    auto readCellScrollY = [&](bool checkRepeat = false) {
        if (checkRepeat && bgState.vcellScrollRepeat && bgState.vcellScrollDelay) {
            return vramFetcher.lastVCellScroll;
        }
        const uint32 value = VDP2ReadRendererVRAM<uint32>(cellScrollTableAddress);
        if (!checkRepeat || !bgState.vcellScrollRepeat) {
            cellScrollTableAddress += regs2.vcellScrollInc;
        }
        const uint32 prevValue = vramFetcher.lastVCellScroll;
        vramFetcher.lastVCellScroll = bit::extract<8, 26>(value);
        return bgState.vcellScrollDelay ? prevValue : vramFetcher.lastVCellScroll;
    };

    uint32 mosaicCounterX = 0;
    uint32 vcellScrollY = 0;
    uint32 vcellScrollX = fracScrollX >> (8u + 3u);

    if (vcellScrollEnable) {
        vcellScrollY = readCellScrollY(true);
    }

    for (uint32 x = 0; x < m_HRes; x++) {
        // Apply horizontal mosaic or vertical cell-scrolling
        // Mosaic takes priority
        if (bgParams.mosaicEnable) {
            // Apply horizontal mosaic
            const uint8 currMosaicCounterX = mosaicCounterX;
            mosaicCounterX++;
            if (mosaicCounterX >= regs2.mosaicH) {
                mosaicCounterX = 0;
            }
            if (currMosaicCounterX > 0) {
                // Simply copy over the data from the previous pixel
                layerOut.pixels.CopyPixel(x - 1, x);

                // Increment horizontal coordinate
                fracScrollX += bgState.scrollIncH;
                continue;
            }
        } else if (vcellScrollEnable) {
            // Update vertical cell scroll amount
            if ((fracScrollX >> (8u + 3u)) != vcellScrollX) {
                vcellScrollX = fracScrollX >> (8u + 3u);
                vcellScrollY = readCellScrollY();
            }
        }

        if (windowState[x]) {
            // Make pixel transparent if inside active window area
            layerOut.pixels.priority[x] = 0;
        } else {
            // Compute integer scroll screen coordinates
            const uint32 scrollX = fracScrollX >> 8u;
            const uint32 scrollY = (fracScrollY + vcellScrollY) >> 8u;
            const CoordU32 scrollCoord{scrollX, scrollY};

            // Plot pixel
            const Pixel pixel = VDP2FetchBitmapPixel<colorFormat, colorMode>(bgParams, regs2, vramFetcher,
                                                                             bgParams.bitmapBaseAddress, scrollCoord);
            layerOut.pixels.SetPixel(x, pixel);
        }

        // Increment horizontal coordinate
        fracScrollX += bgState.scrollIncH;
    }
}

template <uint32 bgIndex, SoftwareVDPRenderer::CharacterMode charMode, bool fourCellChar, ColorFormat colorFormat,
          uint32 colorMode>
NO_INLINE void SoftwareVDPRenderer::VDP2DrawRotationScrollBG(const VDP2Regs &regs2, const BGParams &bgParams,
                                                             LayerOutput &layerOut, VRAMFetcher &vramFetcher,
                                                             std::span<const bool> windowState, bool altField) {
    static constexpr bool selRotParam = bgIndex == 0;

    const VDP2State &state2 = m_state.state2;

    const bool doubleResH = m_HRes > kMaxNormalResH;
    const uint32 xShift = doubleResH ? 1 : 0;
    const uint32 maxX = m_HRes >> xShift;

    uint32 mosaicCounterX = 0;

    for (uint32 x = 0; x < maxX; x++) {
        const uint32 xx = x << xShift;

        // Apply horizontal mosaic if enabled
        if (bgParams.mosaicEnable) {
            const uint8 currMosaicCounterX = mosaicCounterX;
            mosaicCounterX++;
            if (mosaicCounterX >= regs2.mosaicH) {
                mosaicCounterX = 0;
            }
            if (currMosaicCounterX > 0) {
                // Simply copy over the data from the previous pixel
                layerOut.pixels.CopyPixel(xx - 1, xx);
                if (doubleResH) {
                    layerOut.pixels.CopyPixel(xx, xx + 1);
                }
                continue;
            }
        }

        const RotParamSelector rotParamSelector =
            selRotParam ? VDP2SelectRotationParameter(x, regs2, altField) : RotParamB;

        const RotationParams &rotParams = regs2.rotParams[rotParamSelector];
        const RotationParamLineOutput &rotParamOut = m_rotParamLineOutputs[rotParamSelector];

        // Handle transparent pixels in coefficient table
        if (rotParams.coeffTableEnable && rotParamOut.transparent[x]) {
            layerOut.pixels.priority[xx] = 0;
            if (doubleResH) {
                layerOut.pixels.priority[xx + 1] = 0;
            }
            continue;
        }

        // Get scroll screen coordinates
        const uint32 scrollX = rotParamOut.screenCoords[x].x();
        const uint32 scrollY = rotParamOut.screenCoords[x].y();
        const CoordU32 scrollCoord{scrollX, scrollY};

        // Determine maximum coordinates and screen over process
        const bool usingFixed512 = rotParams.screenOverProcess == ScreenOverProcess::Fixed512;
        const bool usingRepeat = rotParams.screenOverProcess == ScreenOverProcess::Repeat;
        const uint32 maxScrollX = usingFixed512 ? 512 : ((512 * 4) << rotParams.pageShiftH);
        const uint32 maxScrollY = usingFixed512 ? 512 : ((512 * 4) << rotParams.pageShiftV);

        // TODO: optimize doubleResH vs. window handling

        if (windowState[xx] && (!doubleResH || windowState[xx + 1])) {
            // Make pixel transparent if inside a window
            layerOut.pixels.priority[xx] = 0;
            if (doubleResH) {
                layerOut.pixels.priority[xx + 1] = 0;
            }
        } else if ((scrollX < maxScrollX && scrollY < maxScrollY) || usingRepeat) {
            // Plot pixel
            const Pixel pixel = VDP2FetchScrollBGPixel<true, charMode, fourCellChar, colorFormat, colorMode>(
                bgParams, regs2, state2.rbgPageBaseAddresses[rotParamSelector][bgIndex], rotParams.pageShiftH,
                rotParams.pageShiftV, scrollCoord, m_vramFetchers[altField][rotParamSelector + 4]);
            if (!doubleResH || !windowState[xx]) {
                layerOut.pixels.SetPixel(xx, pixel);
            }
            if (doubleResH && !windowState[xx + 1]) {
                layerOut.pixels.SetPixel(xx + 1, pixel);
            }

            VDP2StoreRotationLineColorData<bgIndex>(x, regs2, bgParams, rotParamSelector);
        } else if (rotParams.screenOverProcess == ScreenOverProcess::RepeatChar) {
            // Out of bounds - repeat character
            static constexpr bool largePalette = colorFormat != ColorFormat::Palette16;
            static constexpr bool extChar = charMode == CharacterMode::OneWordExtended;

            const uint16 charData = rotParams.screenOverPatternName;
            vramFetcher.currChar = VDP2ExtractOneWordCharacter<fourCellChar, largePalette, extChar>(bgParams, charData);

            const uint32 dotX = bit::extract<0, 2>(scrollX);
            const uint32 dotY = bit::extract<0, 2>(scrollY);
            const CoordU32 dotCoord{dotX, dotY};

            const Pixel pixel =
                VDP2FetchCharacterPixel<colorFormat, colorMode>(bgParams, regs2, vramFetcher, dotCoord, 0);
            if (!doubleResH || !windowState[xx]) {
                layerOut.pixels.SetPixel(xx, pixel);
            }
            if (doubleResH && !windowState[xx + 1]) {
                layerOut.pixels.SetPixel(xx + 1, pixel);
            }

            VDP2StoreRotationLineColorData<bgIndex>(x, regs2, bgParams, rotParamSelector);
        } else {
            // Out of bounds - transparent
            layerOut.pixels.priority[xx] = 0;
            if (doubleResH) {
                layerOut.pixels.priority[xx + 1] = 0;
            }
        }
    }
}

template <uint32 bgIndex, ColorFormat colorFormat, uint32 colorMode>
NO_INLINE void SoftwareVDPRenderer::VDP2DrawRotationBitmapBG(const VDP2Regs &regs2, const BGParams &bgParams,
                                                             LayerOutput &layerOut, std::span<const bool> windowState,
                                                             bool altField) {
    static constexpr bool selRotParam = bgIndex == 0;

    const bool doubleResH = m_HRes > kMaxNormalResH;
    const uint32 xShift = doubleResH ? 1 : 0;
    const uint32 maxX = m_HRes >> xShift;

    for (uint32 x = 0; x < maxX; x++) {
        const uint32 xx = x << xShift;

        const RotParamSelector rotParamSelector =
            selRotParam ? VDP2SelectRotationParameter(x, regs2, altField) : RotParamB;

        const RotationParams &rotParams = regs2.rotParams[rotParamSelector];
        const RotationParamLineOutput &rotParamOut = m_rotParamLineOutputs[rotParamSelector];

        // Handle transparent pixels in coefficient table
        if (rotParams.coeffTableEnable && rotParamOut.transparent[x]) {
            layerOut.pixels.priority[xx] = 0;
            if (doubleResH) {
                layerOut.pixels.priority[xx + 1] = 0;
            }
            continue;
        }

        // Get scroll screen coordinates
        const uint32 scrollX = rotParamOut.screenCoords[x].x();
        const uint32 scrollY = rotParamOut.screenCoords[x].y();
        const CoordU32 scrollCoord{scrollX, scrollY};

        const bool usingFixed512 = rotParams.screenOverProcess == ScreenOverProcess::Fixed512;
        const bool usingRepeat = rotParams.screenOverProcess == ScreenOverProcess::Repeat;
        const uint32 maxScrollX = usingFixed512 ? 512 : bgParams.bitmapSizeH;
        const uint32 maxScrollY = usingFixed512 ? 512 : bgParams.bitmapSizeV;

        // TODO: optimize doubleResH vs. window handling

        if (windowState[xx] && (!doubleResH || windowState[xx + 1])) {
            // Make pixel transparent if inside a window
            layerOut.pixels.priority[xx] = 0;
            if (doubleResH) {
                layerOut.pixels.priority[xx + 1] = 0;
            }
        } else if ((scrollX < maxScrollX && scrollY < maxScrollY) || usingRepeat) {
            // Plot pixel
            const Pixel pixel = VDP2FetchBitmapPixel<colorFormat, colorMode>(
                bgParams, regs2, m_vramFetchers[altField][rotParamSelector + 4], rotParams.bitmapBaseAddress,
                scrollCoord);
            if (!doubleResH || !windowState[xx]) {
                layerOut.pixels.SetPixel(xx, pixel);
            }
            if (doubleResH && !windowState[xx + 1]) {
                layerOut.pixels.SetPixel(xx + 1, pixel);
            }

            VDP2StoreRotationLineColorData<bgIndex>(x, regs2, bgParams, rotParamSelector);
        } else {
            // Out of bounds and no repeat
            layerOut.pixels.priority[xx] = 0;
            if (doubleResH) {
                layerOut.pixels.priority[xx + 1] = 0;
            }
        }
    }
}

template <uint32 bgIndex>
FORCE_INLINE void SoftwareVDPRenderer::VDP2StoreRotationLineColorData(uint32 x, const VDP2Regs &regs2,
                                                                      const BGParams &bgParams,
                                                                      RotParamSelector rotParamSelector) {
    const VDP2State &state2 = m_state.state2;
    const CommonRotationParams &commonRotParams = regs2.commonRotParams;

    if (bgParams.lineColorScreenEnable) {
        // Line color for rotation parameters can be either the raw LNCL value or combined with coefficient table data.
        // When combined, CRAM address bits 10-7 come from LNCL and bits 6-0 come from the coefficient table.
        // This is handled in VDP2CalcRotationParameterTables.
        //
        // Whether to combine line color data depends on the rotation parameter mode:
        //   0: data from coeff A is added to rotparam A
        //   1: data from coeff B is added to rotparam B
        //   2: data from coeff A is added to both rotparams
        //   3: data from each coeff is added to each rotparam
        // If RBG1 is enabled, coeff data A is used for both RBG0 and RBG1

        const bool hasRBG1 = regs2.bgEnabled[5];

        bool useCoeffLineColor = false;
        RotParamSelector coeffSel;

        using enum RotationParamMode;
        switch (commonRotParams.rotParamMode) {
        case RotationParamA:
            useCoeffLineColor = rotParamSelector == RotParamA;
            coeffSel = RotParamA;
            break;
        case RotationParamB:
            useCoeffLineColor = rotParamSelector == RotParamB;
            coeffSel = hasRBG1 ? RotParamA : RotParamB;
            break;
        case Coefficient:
            useCoeffLineColor = true;
            coeffSel = RotParamA;
            break;
        case Window:
            useCoeffLineColor = true;
            coeffSel = hasRBG1 ? RotParamA : rotParamSelector;
            break;
        }

        m_rbgLineColors[bgIndex][x] = state2.lineBackLayerState.lineColor;

        if (useCoeffLineColor) {
            const RotationParams &rotParams = regs2.rotParams[coeffSel];
            const RotationParamLineOutput &rotParamOut = m_rotParamLineOutputs[coeffSel];
            if (rotParams.coeffTableEnable && rotParams.coeffUseLineColorData) {
                m_rbgLineColors[bgIndex][x] = rotParamOut.lineColor[x];
            }
        }
    }
}

FORCE_INLINE SoftwareVDPRenderer::RotParamSelector
SoftwareVDPRenderer::VDP2SelectRotationParameter(uint32 x, const VDP2Regs &regs2, bool altField) {
    const CommonRotationParams &commonRotParams = regs2.commonRotParams;

    using enum RotationParamMode;
    switch (commonRotParams.rotParamMode) {
    case RotationParamA: return RotParamA;
    case RotationParamB: return RotParamB;
    case Coefficient:
        return regs2.rotParams[0].coeffTableEnable && m_rotParamLineOutputs[0].transparent[x] ? RotParamB : RotParamA;
    case Window: return m_rotParamsWindow[altField][x] ? RotParamB : RotParamA;
    }
    util::unreachable();
}

FORCE_INLINE bool SoftwareVDPRenderer::VDP2CanFetchCoefficient(const VDP2Regs &regs2, const RotationParams &params,
                                                               uint32 coeffAddress) const {
    // Coefficients can always be fetched from CRAM
    if (regs2.vramControl.colorRAMCoeffTableEnable) {
        return true;
    }

    // Check that the VRAM bank containing the coefficient table is designated for coefficient data.
    // Return a default (transparent) coefficient if not.
    const uint32 offset = coeffAddress >> 10u;
    const uint32 address = (offset * sizeof(uint32)) >> params.coeffDataSize;

    // Determine which bank is targeted.
    // Address is 19 bits wide when using 512 KiB VRAM.
    // Bank is designated by bits 17-18.
    const uint32 bank = bit::extract<17, 18>(address);
    return m_state.state2.coeffAccess[bank];
}

FORCE_INLINE Coefficient SoftwareVDPRenderer::VDP2FetchRotationCoefficient(const VDP2Regs &regs2,
                                                                           const RotationParams &params,
                                                                           uint32 coeffAddress) {
    Coefficient coeff{};

    // Coefficient data formats:
    //
    // 1 word   15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
    // kx/ky   |TP|SN|Coeff. IP  | Coefficient fractional part |
    // Px      |TP|SN|Coefficient integer part            | FP |
    //
    // 2 words  31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
    // kx/ky   |TP| Line color data    |SN|Coeff. integer part |Coefficient fractional part                    |
    // Px      |TP| Line color data    |SN|Coefficient integer part                    |Coeff. fractional part |
    //
    // TP=transparent bit   SN=coefficient sign bit   IP=coefficient integer part   FP=coefficient fractional part

    const uint32 offset = coeffAddress >> 10u;

    if (params.coeffDataSize == 1) {
        // One-word coefficient data
        const uint32 address = offset * sizeof(uint16);
        const uint16 data = regs2.vramControl.colorRAMCoeffTableEnable ? VDP2ReadRendererCRAM<uint16>(address | 0x800)
                                                                       : VDP2ReadRendererVRAM<uint16>(address);
        coeff.value = bit::extract_signed<0, 14>(data);
        coeff.lineColorData = 0;
        coeff.transparent = bit::test<15>(data);

        if (params.coeffDataMode == CoefficientDataMode::ViewpointX) {
            coeff.value <<= 14;
        } else {
            coeff.value <<= 6;
        }
    } else {
        // Two-word coefficient data
        const uint32 address = offset * sizeof(uint32);
        const uint32 data = regs2.vramControl.colorRAMCoeffTableEnable ? VDP2ReadRendererCRAM<uint32>(address | 0x800)
                                                                       : VDP2ReadRendererVRAM<uint32>(address);
        coeff.value = bit::extract_signed<0, 23>(data);
        coeff.lineColorData = bit::extract<24, 30>(data);
        coeff.transparent = bit::test<31>(data);

        if (params.coeffDataMode == CoefficientDataMode::ViewpointX) {
            coeff.value <<= 8;
        }
    }

    return coeff;
}

// TODO: optimize - remove pageShiftH and pageShiftV params
template <bool rot, SoftwareVDPRenderer::CharacterMode charMode, bool fourCellChar, ColorFormat colorFormat,
          uint32 colorMode>
FORCE_INLINE_EX SoftwareVDPRenderer::Pixel
SoftwareVDPRenderer::VDP2FetchScrollBGPixel(const BGParams &bgParams, const VDP2Regs &regs2,
                                            std::span<const uint32> pageBaseAddresses, uint32 pageShiftH,
                                            uint32 pageShiftV, CoordU32 scrollCoord, VRAMFetcher &vramFetcher) {
    //      Map (NBGs)              Map (RBGs)
    // +---------+---------+   +----+----+----+----+
    // |         |         |   | A  | B  | C  | D  |
    // | Plane A | Plane B |   +----+----+----+----+
    // |         |         |   | E  | F  | G  | H  |
    // +---------+---------+   +----+----+----+----+
    // |         |         |   | I  | J  | K  | L  |
    // | Plane C | Plane D |   +----+----+----+----+
    // |         |         |   | M  | N  | O  | P  |
    // +---------+---------+   +----+----+----+----+
    //
    // Normal and rotation BGs are divided into planes in the exact configurations illustrated above.
    // The BG's Map Offset Register is combined with the BG plane's Map Register (MPxxN#) to produce a base address
    // for each plane:
    //   Address bits  Source
    //            8-6  Map Offset Register (MPOFN)
    //            5-0  Map Register (MPxxN#)
    //
    // These addresses are precomputed in pageBaseAddresses.
    //
    //       2x2 Plane               2x1 Plane          1x1 Plane
    //        PLSZ=3                  PLSZ=1             PLSZ=0
    // +---------+---------+   +---------+---------+   +---------+
    // |         |         |   |         |         |   |         |
    // | Page 1  | Page 2  |   | Page 1  | Page 2  |   | Page 1  |
    // |         |         |   |         |         |   |         |
    // +---------+---------+   +---------+---------+   +---------+
    // |         |         |
    // | Page 3  | Page 4  |
    // |         |         |
    // +---------+---------+
    //
    // Each plane is composed of 1x1, 2x1 or 2x2 pages, determined by Plane Size in the Plane Size Register (PLSZ).
    // Pages are stored sequentially in VRAM left to right, top to bottom, as shown.
    //
    // The size is stored as a bit shift in bgParams.pageShiftH and bgParams.pageShiftV.
    //
    //        64x64 Page                 32x32 Page
    // +----+----+..+----+----+   +----+----+..+----+----+
    // |CP 1|CP 2|  |CP63|CP64|   |CP 1|CP 2|  |CP31|CP32|
    // +----+----+..+----+----+   +----+----+..+----+----+
    // |  65|  66|  | 127| 128|   |  33|  34|  |  63|  64|
    // +----+----+..+----+----+   +----+----+..+----+----+
    // :    :    :  :    :    :   :    :    :  :    :    :
    // +----+----+..+----+----+   +----+----+..+----+----+
    // |3969|3970|  |4031|4032|   | 961| 962|  | 991| 992|
    // +----+----+..+----+----+   +----+----+..+----+----+
    // |4033|4034|  |4095|4096|   | 993| 994|  |1023|1024|
    // +----+----+..+----+----+   +----+----+..+----+----+
    //
    // Pages contain 32x32 or 64x64 character patterns, which are groups of 1x1 or 2x2 cells, determined by
    // Character Size in the Character Control Register (CHCTLA-B).
    //
    // Pages always contain a total of 64x64 cells - a grid of 64x64 1x1 character patterns or 32x32 2x2 character
    // patterns. Because of this, pages always have 512x512 dots.
    //
    // Character patterns in a page are stored sequentially in VRAM left to right, top to bottom, as shown above.
    //
    // fourCellChar specifies the size of the character patterns (1x1 when false, 2x2 when true) and, by extension,
    // the dimensions of the page (32x32 or 64x64 respectively).
    //
    // 2x2 Character Pattern     1x1 C.P.
    // +---------+---------+   +---------+
    // |         |         |   |         |
    // | Cell 1  | Cell 2  |   | Cell 1  |
    // |         |         |   |         |
    // +---------+---------+   +---------+
    // |         |         |
    // | Cell 3  | Cell 4  |
    // |         |         |
    // +---------+---------+
    //
    // Character patterns are groups of 1x1 or 2x2 cells, determined by Character Size in the Character Control
    // Register (CHCTLA-B).
    //
    // Cells are stored sequentially in VRAM left to right, top to bottom, as shown above.
    //
    // Character patterns contain a character number (15 bits), a palette number (7 bits, only used with 16 or 256
    // color palette modes), two special function bits (Special Priority and Special Color Calculation) and two flip
    // bits (horizontal and vertical).
    //
    // Character patterns can be one or two words long, as defined by Pattern Name Data Size in the Pattern Name
    // Control Register (PNCN0-3, PNCR). When using one word characters, some of the data comes from supplementary
    // registers.
    //
    // fourCellChar stores the character pattern size (1x1 when false, 2x2 when true).
    // twoWordChar determines if characters are one (false) or two (true) words long.
    // extChar determines the length of the character data field in one word characters -- when true, they're
    // extended by two bits, taking over the two flip bits.
    //
    //           Cell
    // +--+--+--+--+--+--+--+--+
    // | 1| 2| 3| 4| 5| 6| 7| 8|
    // +--+--+--+--+--+--+--+--+
    // | 9|10|11|12|13|14|15|16|
    // +--+--+--+--+--+--+--+--+
    // |17|18|19|20|21|22|23|24|
    // +--+--+--+--+--+--+--+--+
    // |25|26|27|28|29|30|31|32|
    // +--+--+--+--+--+--+--+--+
    // |33|34|35|36|37|38|39|40|
    // +--+--+--+--+--+--+--+--+
    // |41|42|43|44|45|46|47|48|
    // +--+--+--+--+--+--+--+--+
    // |49|50|51|52|53|54|55|56|
    // +--+--+--+--+--+--+--+--+
    // |57|58|59|60|61|62|63|64|
    // +--+--+--+--+--+--+--+--+
    //
    // Cells contain 8x8 dots (pixels) in one of the following color formats:
    //   - 16 color palette
    //   - 256 color palette
    //   - 1024 or 2048 color palette (depending on Color Mode)
    //   - 5:5:5 RGB (32768 colors)
    //   - 8:8:8 RGB (16777216 colors)
    //
    // colorFormat specifies one of the color formats above.
    // colorMode determines the palette color format in CRAM, one of:
    //   - 16-bit 5:5:5 RGB, 1024 words
    //   - 16-bit 5:5:5 RGB, 2048 words
    //   - 32-bit 8:8:8 RGB, 1024 longwords

    static constexpr uint32 kPlaneShift = rot ? 2u : 1u;
    static constexpr uint32 kPlaneMask = (1u << kPlaneShift) - 1u;

    static constexpr bool twoWordChar = charMode == CharacterMode::TwoWord;
    static constexpr bool extChar = charMode == CharacterMode::OneWordExtended;
    static constexpr uint32 fourCellCharValue = fourCellChar ? 1 : 0;

    auto [scrollX, scrollY] = scrollCoord;

    // Determine plane index from the scroll coordinates
    const uint32 planeX = (scrollX >> (9 + pageShiftH)) & kPlaneMask;
    const uint32 planeY = (scrollY >> (9 + pageShiftV)) & kPlaneMask;
    const uint32 plane = planeX + (planeY << kPlaneShift);
    const uint32 pageBaseAddress = pageBaseAddresses[plane];

    // HACK: apply data access shift here too
    // Not entirely correct, but fixes problems with World Heroes Perfect's demo screen
    const uint32 bank = (pageBaseAddress >> 17u) & 3u;
    scrollX += bgParams.vramDataOffset[bank];

    // Determine page index from the scroll coordinates
    const uint32 pageX = bit::extract<9>(scrollX) & pageShiftH;
    const uint32 pageY = bit::extract<9>(scrollY) & pageShiftV;
    const uint32 page = pageX + (pageY << 1u);
    const uint32 pageOffset = page << kPageSizes[fourCellChar][twoWordChar];

    // Determine character pattern from the scroll coordinates
    const uint32 charPatX = bit::extract<3, 8>(scrollX) >> fourCellCharValue;
    const uint32 charPatY = bit::extract<3, 8>(scrollY) >> fourCellCharValue;
    const uint32 charIndex = charPatX + (charPatY << (6u - fourCellCharValue));

    // Determine cell index from the scroll coordinates
    const uint32 cellX = bit::extract<3>(scrollX) & fourCellCharValue;
    const uint32 cellY = bit::extract<3>(scrollY) & fourCellCharValue;
    const uint32 cellIndex = cellX + (cellY << 1u);

    // Determine dot coordinates
    const uint32 dotX = bit::extract<0, 2>(scrollX);
    const uint32 dotY = bit::extract<0, 2>(scrollY);
    const CoordU32 dotCoord{dotX, dotY};

    // Fetch character if needed
    if (vramFetcher.lastCharIndex != charIndex) {
        vramFetcher.lastCharIndex = charIndex;
        const uint32 pageAddress = pageBaseAddress + pageOffset;
        static constexpr bool largePalette = colorFormat != ColorFormat::Palette16;
        const Character ch =
            twoWordChar
                ? VDP2FetchTwoWordCharacter(bgParams, pageAddress, charIndex)
                : VDP2FetchOneWordCharacter<fourCellChar, largePalette, extChar>(bgParams, pageAddress, charIndex);

        // Send character to pipeline
        vramFetcher.currChar = bgParams.charPatDelay[bank] ? vramFetcher.nextChar : ch;
        vramFetcher.nextChar = ch;
    } else if constexpr (fourCellChar) {
        // Each cell of a 2x2 character is fetched individually.
        // With the delay, the fetch is done between the first and the second half of the character.
        if (bgParams.charPatDelay[bank] && vramFetcher.lastCellX != cellX) {
            vramFetcher.lastCellX = cellX;
            if (cellX == 1) {
                vramFetcher.currChar = vramFetcher.nextChar;
            }
        }
    }

    // Fetch pixel using character data
    return VDP2FetchCharacterPixel<colorFormat, colorMode>(bgParams, regs2, vramFetcher, dotCoord, cellIndex);
}

FORCE_INLINE Character SoftwareVDPRenderer::VDP2FetchTwoWordCharacter(const BGParams &bgParams, uint32 pageBaseAddress,
                                                                      uint32 charIndex) {
    const uint32 charAddress = pageBaseAddress + charIndex * sizeof(uint32);
    const uint32 charBank = (charAddress >> 17u) & 3u;

    if (!bgParams.patNameAccess[charBank]) {
        return {};
    }

    const uint32 charData = VDP2ReadRendererVRAM<uint32>(charAddress);
    return Character{.u32 = charData};
}

template <bool fourCellChar, bool largePalette, bool extChar>
FORCE_INLINE Character SoftwareVDPRenderer::VDP2FetchOneWordCharacter(const BGParams &bgParams, uint32 pageBaseAddress,
                                                                      uint32 charIndex) {
    const uint32 charAddress = pageBaseAddress + charIndex * sizeof(uint16);
    const uint32 charBank = (charAddress >> 17u) & 3u;

    if (!bgParams.patNameAccess[charBank]) {
        return {};
    }

    const uint16 charData = VDP2ReadRendererVRAM<uint16>(charAddress);
    return VDP2ExtractOneWordCharacter<fourCellChar, largePalette, extChar>(bgParams, charData);
}

template <bool fourCellChar, bool largePalette, bool extChar>
FORCE_INLINE Character SoftwareVDPRenderer::VDP2ExtractOneWordCharacter(const BGParams &bgParams, uint16 charData) {
    // Contents of 1 word character patterns vary based on Character Size, Character Color Count and Auxiliary Mode:
    //     Character Size        = CHCTLA/CHCTLB.xxCHSZ  = !fourCellChar = !FCC
    //     Character Color Count = CHCTLA/CHCTLB.xxCHCNn = largePalette  = LP
    //     Auxiliary Mode        = PNCN0/PNCR.xxCNSM     = extChar       = EC
    //             ---------------- Character data ----------------    Supplement in Pattern Name Control Register
    // FCC LP  EC  |15 14 13 12 11 10 9  8  7  6  5  4  3  2  1  0|    | 9  8  7  6  5  4  3  2  1  0|
    //  F   F   F  |palnum 3-0 |VF|HF| character number 9-0       |    |PR|CC| PN 6-4 |charnum 14-10 |
    //  F   T   F  |--| PN 6-4 |VF|HF| character number 9-0       |    |PR|CC|--------|charnum 14-10 |
    //  T   F   F  |palnum 3-0 |VF|HF| character number 11-2      |    |PR|CC| PN 6-4 |CN 14-12|CN1-0|
    //  T   T   F  |--| PN 6-4 |VF|HF| character number 11-2      |    |PR|CC|--------|CN 14-12|CN1-0|
    //  F   F   T  |palnum 3-0 |       character number 11-0      |    |PR|CC| PN 6-4 |CN 14-12|-----|
    //  F   T   T  |--| PN 6-4 |       character number 11-0      |    |PR|CC|--------|CN 14-12|-----|
    //  T   F   T  |palnum 3-0 |       character number 13-2      |    |PR|CC| PN 6-4 |cn|-----|CN1-0|   cn=CN14
    //  T   T   T  |--| PN 6-4 |       character number 13-2      |    |PR|CC|--------|cn|-----|CN1-0|   cn=CN14

    // Character number bit range from the 1-word character pattern data (charData)
    static constexpr uint32 baseCharNumStart = 0;
    static constexpr uint32 baseCharNumEnd = 9 + 2 * extChar;
    static constexpr uint32 baseCharNumPos = 2 * fourCellChar;

    // Upper character number bit range from the supplementary character number (bgParams.supplCharNum)
    static constexpr uint32 supplCharNumStart = 2 * fourCellChar + 2 * extChar;
    static constexpr uint32 supplCharNumEnd = 4;
    static constexpr uint32 supplCharNumPos = 10 + supplCharNumStart;
    // The lower bits are always in range 0..1 and only used if fourCellChar == true

    const uint32 baseCharNum = bit::extract<baseCharNumStart, baseCharNumEnd>(charData);
    const uint32 supplCharNum = bit::extract<supplCharNumStart, supplCharNumEnd>(bgParams.supplScrollCharNum);

    Character ch;
    ch.charNum = (baseCharNum << baseCharNumPos) | (supplCharNum << supplCharNumPos);
    if constexpr (fourCellChar) {
        ch.charNum |= bit::extract<0, 1>(bgParams.supplScrollCharNum);
    }
    if constexpr (largePalette) {
        ch.palNum = bit::extract<12, 14>(charData) << 4u;
    } else {
        ch.palNum = (bit::extract<12, 15>(charData) | bgParams.supplScrollPalNum);
    }
    ch.specColorCalc = bgParams.supplScrollSpecialColorCalc;
    ch.specPriority = bgParams.supplScrollSpecialPriority;
    ch.flipH = !extChar && bit::test<10>(charData);
    ch.flipV = !extChar && bit::test<11>(charData);
    return ch;
}

template <ColorFormat colorFormat, uint32 colorMode>
FORCE_INLINE SoftwareVDPRenderer::Pixel
SoftwareVDPRenderer::VDP2FetchCharacterPixel(const BGParams &bgParams, const VDP2Regs &regs2, VRAMFetcher &vramFetcher,
                                             CoordU32 dotCoord, uint32 cellIndex) {
    static_assert(static_cast<uint32>(colorFormat) <= 4, "Invalid xxCHCN value");

    assert(dotCoord.x() < 8);
    assert(dotCoord.y() < 8);

    const Character ch = vramFetcher.currChar;

    // Flip dot coordinates if requested
    if (ch.flipH) {
        dotCoord.x() ^= 7;
        if (bgParams.cellSizeShift > 0) {
            cellIndex ^= 1;
        }
    }
    if (ch.flipV) {
        dotCoord.y() ^= 7;
        if (bgParams.cellSizeShift > 0) {
            cellIndex ^= 2;
        }
    }

    // Adjust cell index based on color format
    if constexpr (colorFormat == ColorFormat::RGB888) {
        cellIndex <<= 3;
    } else if constexpr (colorFormat == ColorFormat::RGB555) {
        cellIndex <<= 2;
    } else if constexpr (colorFormat != ColorFormat::Palette16) {
        cellIndex <<= 1;
    }

    // Cell addressing uses a fixed offset of 32 bytes
    const uint32 cellAddress = (ch.charNum + cellIndex) << 5u;

    return VDP2FetchPixel<false, colorFormat, colorMode>(bgParams, regs2, vramFetcher, cellAddress, 8, dotCoord,
                                                         ch.palNum << 4u, ch.specColorCalc, ch.specPriority);
}

template <ColorFormat colorFormat, uint32 colorMode>
FORCE_INLINE SoftwareVDPRenderer::Pixel
SoftwareVDPRenderer::VDP2FetchBitmapPixel(const BGParams &bgParams, const VDP2Regs &regs2, VRAMFetcher &vramFetcher,
                                          uint32 bitmapBaseAddress, CoordU32 dotCoord) {
    static_assert(static_cast<uint32>(colorFormat) <= 4, "Invalid xxCHCN value");

    // Bitmap data wraps around infinitely
    dotCoord.x() &= bgParams.bitmapSizeH - 1;
    dotCoord.y() &= bgParams.bitmapSizeV - 1;

    // Bitmap addressing uses a fixed offset of 0x20000 bytes which is precalculated when MPOFN/MPOFR is written to

    return VDP2FetchPixel<true, colorFormat, colorMode>(
        bgParams, regs2, vramFetcher, bitmapBaseAddress, bgParams.bitmapSizeH, dotCoord, bgParams.supplBitmapPalNum,
        bgParams.supplBitmapSpecialColorCalc, bgParams.supplBitmapSpecialPriority);
}

template <bool bitmap, ColorFormat colorFormat, uint32 colorMode>
FORCE_INLINE SoftwareVDPRenderer::Pixel
SoftwareVDPRenderer::VDP2FetchPixel(const BGParams &bgParams, const VDP2Regs &regs2, VRAMFetcher &vramFetcher,
                                    uint32 baseAddress, uint32 linePitch, CoordU32 dotCoord, uint32 palNum,
                                    bool specColorCalc, bool specPriority) {
    const auto [dotX, dotY] = dotCoord;
    const uint32 dotOffset = dotX + dotY * linePitch;

    auto fetchCharData = [&](uint32 address) {
        if (vramFetcher.UpdateCharacterDataAddress(address)) {
            const uint32 bank = (address >> 17u) & 3u;
            if (!bgParams.charPatAccess[bank]) {
                util::WriteNE<uint64>(vramFetcher.charData.data(), 0);
                return;
            }

            if constexpr (bitmap) {
                address += bgParams.vramDataOffset[bank];
            }

            // TODO: handle VRSIZE.VRAMSZ
            auto &vram = VDP2GetRendererVRAM();
            const uint64 data = util::ReadNE<uint64>(&vram[address & 0x7FFF8]);
            util::WriteNE<uint64>(vramFetcher.charData.data(), data);
        }
    };

    // Determine special color calculation flag
    const auto &specFuncCode = regs2.specialFunctionCodes[bgParams.specialFunctionSelect];
    auto getSpecialColorCalcFlag = [&](uint8 specColorCode, bool colorMSB) {
        using enum SpecialColorCalcMode;
        switch (bgParams.specialColorCalcMode) {
        case PerScreen: return bgParams.colorCalcEnable;
        case PerCharacter: return bgParams.colorCalcEnable && specColorCalc;
        case PerDot: return bgParams.colorCalcEnable && specColorCalc && specFuncCode.colorMatches[specColorCode];
        case ColorDataMSB: return bgParams.colorCalcEnable && colorMSB;
        }
        util::unreachable();
    };

    // Fetch color and determine transparency and special color calculation flag
    Pixel pixel{};
    uint8 colorData;
    if constexpr (colorFormat == ColorFormat::Palette16) {
        const uint32 dotAddress = baseAddress + (dotOffset >> 1u);
        fetchCharData(dotAddress);
        const uint8 dotData = (vramFetcher.charData[dotAddress & 7] >> ((~dotX & 1) * 4)) & 0xF;
        if (bgParams.enableTransparency && dotData == 0) {
            pixel.priority = 0;
            return pixel;
        }
        const uint32 colorIndex = palNum | dotData;
        colorData = bit::extract<1, 3>(dotData);
        pixel.color = VDP2FetchCRAMColor<colorMode>(bgParams.cramOffset, colorIndex);
        pixel.specialColorCalc = getSpecialColorCalcFlag(colorData, pixel.color.msb);

    } else if constexpr (colorFormat == ColorFormat::Palette256) {
        const uint32 dotAddress = baseAddress + dotOffset;
        fetchCharData(dotAddress);
        const uint8 dotData = vramFetcher.charData[dotAddress & 7];
        if (bgParams.enableTransparency && dotData == 0) {
            pixel.priority = 0;
            return pixel;
        }
        const uint32 colorIndex = (palNum & 0x700) | dotData;
        colorData = bit::extract<1, 3>(dotData);
        pixel.color = VDP2FetchCRAMColor<colorMode>(bgParams.cramOffset, colorIndex);
        pixel.specialColorCalc = getSpecialColorCalcFlag(colorData, pixel.color.msb);

    } else if constexpr (colorFormat == ColorFormat::Palette2048) {
        const uint32 dotAddress = baseAddress + dotOffset * sizeof(uint16);
        fetchCharData(dotAddress);
        const uint16 dotData = util::ReadBE<uint16>(&vramFetcher.charData[dotAddress & 6]);
        if (bgParams.enableTransparency && (dotData & 0x7FF) == 0) {
            pixel.priority = 0;
            return pixel;
        }
        const uint32 colorIndex = dotData & 0x7FF;
        colorData = bit::extract<1, 3>(dotData);
        pixel.color = VDP2FetchCRAMColor<colorMode>(bgParams.cramOffset, colorIndex);
        pixel.specialColorCalc = getSpecialColorCalcFlag(colorData, pixel.color.msb);

    } else if constexpr (colorFormat == ColorFormat::RGB555) {
        const uint32 dotAddress = baseAddress + dotOffset * sizeof(uint16);
        fetchCharData(dotAddress);
        const uint16 dotData = util::ReadBE<uint16>(&vramFetcher.charData[dotAddress & 6]);
        if (bgParams.enableTransparency && !bit::test<15>(dotData)) {
            pixel.priority = 0;
            return pixel;
        }
        pixel.color = ConvertRGB555to888(Color555{.u16 = dotData});
        pixel.specialColorCalc = getSpecialColorCalcFlag(0b111, true);

    } else if constexpr (colorFormat == ColorFormat::RGB888) {
        const uint32 dotAddress = baseAddress + dotOffset * sizeof(uint32);
        fetchCharData(dotAddress);
        const uint32 dotData = util::ReadBE<uint32>(&vramFetcher.charData[dotAddress & 4]);
        if (bgParams.enableTransparency && !bit::test<31>(dotData)) {
            pixel.priority = 0;
            return pixel;
        }
        pixel.color.u32 = dotData;
        pixel.specialColorCalc = getSpecialColorCalcFlag(0b111, true);
    }

    // Compute priority
    pixel.priority = bgParams.priorityNumber;
    if (bgParams.priorityMode == PriorityMode::PerCharacter) {
        pixel.priority &= ~1;
        pixel.priority |= (uint8)specPriority;
    } else if (bgParams.priorityMode == PriorityMode::PerDot) {
        pixel.priority &= ~1;
        if constexpr (IsPaletteColorFormat(colorFormat)) {
            if (specPriority) {
                pixel.priority |= static_cast<uint8>(specFuncCode.colorMatches[colorData]);
            }
        }
    }

    return pixel;
}

template <uint32 colorMode>
FORCE_INLINE Color888 SoftwareVDPRenderer::VDP2FetchCRAMColor(uint32 cramOffset, uint32 colorIndex) {
    static_assert(colorMode <= 2, "Invalid CRMD value");

    if constexpr (colorMode == 0) {
        // RGB 5:5:5, 1024 words
        const uint32 address = (cramOffset + colorIndex) * sizeof(uint16);
        return VDP2ReadRendererColor5to8(address & 0x7FE);
    } else if constexpr (colorMode == 1) {
        // RGB 5:5:5, 2048 words
        const uint32 address = (cramOffset + colorIndex) * sizeof(uint16);
        return VDP2ReadRendererColor5to8(address & 0xFFE);
    } else { // colorMode == 2
        // RGB 8:8:8, 1024 words
        const uint32 address = (cramOffset + colorIndex) * sizeof(uint32);
        const uint32 data = VDP2ReadRendererCRAM<uint32>(address & 0xFFC);
        return Color888{.u32 = data};
    }
}

// Determines the type of sprite data (if any) based on color data.
//
// colorData is the raw color data.
//
// colorDataBits specifies the bit width of the color data.
template <uint32 colorDataBits>
FORCE_INLINE static SpriteData::Special GetSpecialPattern(uint16 rawData) {
    // Normal shadow pattern (LSB = 0, rest of the color data bits = 1)
    static constexpr uint16 kNormalShadowValue = (1u << colorDataBits) - 2u;

    if ((rawData & 0x7FFF) == 0) {
        return SpriteData::Special::Transparent;
    } else if (bit::extract<0, colorDataBits - 1u>(rawData) == kNormalShadowValue) {
        return SpriteData::Special::Shadow;
    } else {
        return SpriteData::Special::Normal;
    }
}

template <bool applyMesh>
FLATTEN FORCE_INLINE SpriteData SoftwareVDPRenderer::VDP2FetchSpriteData(const VDP2Regs &regs2, const VDP1RegTVMR &tvmr,
                                                                         const SpriteFB &fbram, uint32 fbramOffset) {
    // Adjust offset based on VDP1 data size.
    // The majority of games actually set the sprite readout size to match the VDP1 sprite data size, but there's
    // *always* an exception...
    // 8-bit VDP1 data vs. 16-bit readout: NBA Live 98
    // 16-bit VDP1 data vs. 8-bit readout: I Love Donald Duck
    const uint8 type = regs2.spriteParams.type;
    uint16 rawData;
    if (tvmr.pixel8Bits) {
        rawData = fbram[fbramOffset & 0x3FFFF];
        if (type < 8 && (!applyMesh || rawData != 0)) {
            rawData |= 0xFF00;
        }
    } else {
        fbramOffset *= sizeof(uint16);
        rawData = util::ReadBE<uint16>(&fbram[fbramOffset & 0x3FFFE]);
    }

    // Sprite types 0-7 are 16-bit, 8-15 are 8-bit

    SpriteData data{};
    switch (regs2.spriteParams.type) {
    case 0x0:
        data.colorData = bit::extract<0, 10>(rawData);
        data.colorCalcRatio = bit::extract<11, 13>(rawData);
        data.priority = bit::extract<14, 15>(rawData);
        data.special = GetSpecialPattern<11>(rawData);
        break;

    case 0x1:
        data.colorData = bit::extract<0, 10>(rawData);
        data.colorCalcRatio = bit::extract<11, 12>(rawData);
        data.priority = bit::extract<13, 15>(rawData);
        data.special = GetSpecialPattern<11>(rawData);
        break;

    case 0x2:
        data.colorData = bit::extract<0, 10>(rawData);
        data.colorCalcRatio = bit::extract<11, 13>(rawData);
        data.priority = bit::extract<14>(rawData);
        data.shadowOrWindow = bit::test<15>(rawData);
        data.special = GetSpecialPattern<11>(rawData);
        break;

    case 0x3:
        data.colorData = bit::extract<0, 10>(rawData);
        data.colorCalcRatio = bit::extract<11, 12>(rawData);
        data.priority = bit::extract<13, 14>(rawData);
        data.shadowOrWindow = bit::test<15>(rawData);
        data.special = GetSpecialPattern<11>(rawData);
        break;

    case 0x4:
        data.colorData = bit::extract<0, 9>(rawData);
        data.colorCalcRatio = bit::extract<10, 12>(rawData);
        data.priority = bit::extract<13, 14>(rawData);
        data.shadowOrWindow = bit::test<15>(rawData);
        data.special = GetSpecialPattern<10>(rawData);
        break;

    case 0x5:
        data.colorData = bit::extract<0, 10>(rawData);
        data.colorCalcRatio = bit::extract<11>(rawData);
        data.priority = bit::extract<12, 14>(rawData);
        data.shadowOrWindow = bit::test<15>(rawData);
        data.special = GetSpecialPattern<11>(rawData);
        break;

    case 0x6:
        data.colorData = bit::extract<0, 9>(rawData);
        data.colorCalcRatio = bit::extract<10, 11>(rawData);
        data.priority = bit::extract<12, 14>(rawData);
        data.shadowOrWindow = bit::test<15>(rawData);
        data.special = GetSpecialPattern<10>(rawData);
        break;

    case 0x7:
        data.colorData = bit::extract<0, 8>(rawData);
        data.colorCalcRatio = bit::extract<9, 11>(rawData);
        data.priority = bit::extract<12, 14>(rawData);
        data.shadowOrWindow = bit::test<15>(rawData);
        data.special = GetSpecialPattern<9>(rawData);
        break;

    case 0x8:
        data.colorData = bit::extract<0, 6>(rawData);
        data.priority = bit::extract<7>(rawData);
        data.special = GetSpecialPattern<7>(rawData);
        break;

    case 0x9:
        data.colorData = bit::extract<0, 5>(rawData);
        data.colorCalcRatio = bit::extract<6>(rawData);
        data.priority = bit::extract<7>(rawData);
        data.special = GetSpecialPattern<6>(rawData);
        break;

    case 0xA:
        data.colorData = bit::extract<0, 5>(rawData);
        data.priority = bit::extract<6, 7>(rawData);
        data.special = GetSpecialPattern<6>(rawData);
        break;

    case 0xB:
        data.colorData = bit::extract<0, 5>(rawData);
        data.colorCalcRatio = bit::extract<6, 7>(rawData);
        data.special = GetSpecialPattern<6>(rawData);
        break;

    case 0xC:
        data.colorData = bit::extract<0, 7>(rawData);
        data.priority = bit::extract<7>(rawData);
        data.special = GetSpecialPattern<8>(rawData);
        break;

    case 0xD:
        data.colorData = bit::extract<0, 7>(rawData);
        data.colorCalcRatio = bit::extract<6>(rawData);
        data.priority = bit::extract<7>(rawData);
        data.special = GetSpecialPattern<8>(rawData);
        break;

    case 0xE:
        data.colorData = bit::extract<0, 7>(rawData);
        data.priority = bit::extract<6, 7>(rawData);
        data.special = GetSpecialPattern<8>(rawData);
        break;

    case 0xF:
        data.colorData = bit::extract<0, 7>(rawData);
        data.colorCalcRatio = bit::extract<6, 7>(rawData);
        data.special = GetSpecialPattern<8>(rawData);
        break;
    }
    return data;
}

template <bool deinterlace>
FORCE_INLINE uint32 SoftwareVDPRenderer::VDP2GetY(uint32 y, const VDP2Regs &regs2) const {
    if (regs2.TVMD.IsInterlaced() && !m_exclusiveMonitor) {
        return (y << 1) | (regs2.TVSTAT.ODD & !deinterlace);
    } else {
        return y;
    }
}

} // namespace ymir::vdp
