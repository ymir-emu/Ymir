#include <ymir/hw/vdp/vdp.hpp>

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/dev_log.hpp>

namespace ymir::vdp {

VDP::VDP(core::Scheduler &scheduler, core::Configuration &config)
    : m_renderer(std::make_unique<NullVDPRenderer>())
    , m_config(config)
    , m_scheduler(scheduler) {

    config.system.videoStandard.Observe([this](VideoStandard videoStandard) { SetVideoStandard(videoStandard); });
    config.system.sh2ClockFactor.Observe([this](Ratio) {
        m_state.regs2.TVMDDirty = true;
        UpdateResolution<false>();
    });
    config.swRenderer.threadedVDP1.Observe([this](bool value) {
        if (auto *renderer = m_renderer->As<VDPRendererType::Software>()) {
            renderer->EnableThreadedVDP1(value);
        }
    });
    config.swRenderer.threadedVDP2.Observe([this](bool value) {
        if (auto *renderer = m_renderer->As<VDPRendererType::Software>()) {
            renderer->EnableThreadedVDP2(value);
        }
    });
    config.swRenderer.threadedDeinterlacer.Observe([this](bool value) {
        if (auto *renderer = m_renderer->As<VDPRendererType::Software>()) {
            renderer->EnableThreadedDeinterlacer(value);
        }
    });

    m_phaseUpdateEvent = scheduler.RegisterEvent(core::events::VDPPhase, this, OnPhaseUpdateEvent);

    Reset(true);

    UseSoftwareRenderer();
}

VDP::~VDP() = default;

void VDP::Reset(bool hard) {
    m_HRes = vdp::kDefaultResH;
    m_VRes = vdp::kDefaultResV;
    m_exclusiveMonitor = false;

    m_state.Reset(hard);
    m_renderer->Reset(hard);

    m_VDP1TimingPenaltyCycles = 0;
    m_VDP1CtlState.Reset();

    UpdateResolution<false>();

    BeginHPhaseActiveDisplay();
    BeginVPhaseActiveDisplay();

    m_scheduler.ScheduleFromNow(m_phaseUpdateEvent, GetPhaseCycles());
}

void VDP::MapMemory(sys::SH2Bus &bus) {
    static constexpr auto cast = [](void *ctx) -> VDP & { return *static_cast<VDP *>(ctx); };

    // VDP1 VRAM
    bus.MapBoth(
        0x5C0'0000, 0x5C7'FFFF, this,
        [](uint32 address, void *ctx) -> uint8 { return cast(ctx).VDP1ReadVRAM<uint8>(address); },
        [](uint32 address, void *ctx) -> uint16 { return cast(ctx).VDP1ReadVRAM<uint16>(address); },
        [](uint32 address, void *ctx) -> uint32 {
            uint32 value = cast(ctx).VDP1ReadVRAM<uint16>(address + 0) << 16u;
            value |= cast(ctx).VDP1ReadVRAM<uint16>(address + 2) << 0u;
            return value;
        },
        [](uint32 address, uint8 value, void *ctx) { cast(ctx).VDP1WriteVRAM<uint8>(address, value); },
        [](uint32 address, uint16 value, void *ctx) { cast(ctx).VDP1WriteVRAM<uint16>(address, value); },
        [](uint32 address, uint32 value, void *ctx) {
            cast(ctx).VDP1WriteVRAM<uint16>(address + 0, value >> 16u);
            cast(ctx).VDP1WriteVRAM<uint16>(address + 2, value >> 0u);
        });

    // VDP1 framebuffer
    bus.MapBoth(
        0x5C8'0000, 0x5CF'FFFF, this,
        [](uint32 address, uint8 value, void *ctx) { cast(ctx).VDP1WriteFB<uint8>(address, value); },
        [](uint32 address, uint16 value, void *ctx) { cast(ctx).VDP1WriteFB<uint16>(address, value); },
        [](uint32 address, uint32 value, void *ctx) {
            cast(ctx).VDP1WriteFB<uint16>(address + 0, value >> 16u);
            cast(ctx).VDP1WriteFB<uint16>(address + 2, value >> 0u);
        });

    bus.MapNormal(
        0x5C8'0000, 0x5CF'FFFF, this,
        [](uint32 address, void *ctx) -> uint8 { return cast(ctx).VDP1ReadFB<uint8, false>(address); },
        [](uint32 address, void *ctx) -> uint16 { return cast(ctx).VDP1ReadFB<uint16, false>(address); },
        [](uint32 address, void *ctx) -> uint32 {
            uint32 value = cast(ctx).VDP1ReadFB<uint16, false>(address + 0) << 16u;
            value |= cast(ctx).VDP1ReadFB<uint16, false>(address + 2) << 0u;
            return value;
        });

    bus.MapSideEffectFree(
        0x5C8'0000, 0x5CF'FFFF, this,
        [](uint32 address, void *ctx) -> uint8 { return cast(ctx).VDP1ReadFB<uint8, true>(address); },
        [](uint32 address, void *ctx) -> uint16 { return cast(ctx).VDP1ReadFB<uint16, true>(address); },
        [](uint32 address, void *ctx) -> uint32 {
            uint32 value = cast(ctx).VDP1ReadFB<uint16, true>(address + 0) << 16u;
            value |= cast(ctx).VDP1ReadFB<uint16, true>(address + 2) << 0u;
            return value;
        });

    // VDP1 registers
    bus.MapNormal(
        0x5D0'0000, 0x5D7'FFFF, this,
        [](uint32 address, void *ctx) -> uint8 {
            const uint16 value = cast(ctx).VDP1ReadReg<false>(address & ~1);
            return value >> ((~address & 1) * 8u);
        },
        [](uint32 address, void *ctx) -> uint16 { return cast(ctx).VDP1ReadReg<false>(address); },
        [](uint32 address, void *ctx) -> uint32 {
            uint32 value = cast(ctx).VDP1ReadReg<false>(address + 0) << 16u;
            value |= cast(ctx).VDP1ReadReg<false>(address + 2) << 0u;
            return value;
        },
        [](uint32 address, uint8 value, void *ctx) {
            uint16 currValue = cast(ctx).VDP1ReadReg<false>(address & ~1);
            const uint16 shift = (~address & 1) * 8u;
            const uint16 mask = ~(0xFF << shift);
            currValue = (currValue & mask) | (value << shift);
            cast(ctx).VDP1WriteReg<false>(address & ~1, currValue);
        },
        [](uint32 address, uint16 value, void *ctx) { cast(ctx).VDP1WriteReg<false>(address, value); },
        [](uint32 address, uint32 value, void *ctx) {
            cast(ctx).VDP1WriteReg<false>(address + 0, value >> 16u);
            cast(ctx).VDP1WriteReg<false>(address + 2, value >> 0u);
        });

    bus.MapSideEffectFree(
        0x5D0'0000, 0x5D7'FFFF, this,
        [](uint32 address, void *ctx) -> uint8 {
            const uint16 value = cast(ctx).VDP1ReadReg<true>(address & ~1);
            return value >> ((~address & 1) * 8u);
        },
        [](uint32 address, void *ctx) -> uint16 { return cast(ctx).VDP1ReadReg<true>(address); },
        [](uint32 address, void *ctx) -> uint32 {
            uint32 value = cast(ctx).VDP1ReadReg<true>(address + 0) << 16u;
            value |= cast(ctx).VDP1ReadReg<true>(address + 2) << 0u;
            return value;
        },
        [](uint32 address, uint8 value, void *ctx) {
            uint16 currValue = cast(ctx).VDP1ReadReg<true>(address & ~1);
            const uint16 shift = (~address & 1) * 8u;
            const uint16 mask = ~(0xFF << shift);
            currValue = (currValue & mask) | (value << shift);
            cast(ctx).VDP1WriteReg<true>(address & ~1, currValue);
        },
        [](uint32 address, uint16 value, void *ctx) { cast(ctx).VDP1WriteReg<true>(address, value); },
        [](uint32 address, uint32 value, void *ctx) {
            cast(ctx).VDP1WriteReg<true>(address + 0, value >> 16u);
            cast(ctx).VDP1WriteReg<true>(address + 2, value >> 0u);
        });

    // VDP2 VRAM
    bus.MapBoth(
        0x5E0'0000, 0x5EF'FFFF, this,
        [](uint32 address, void *ctx) -> uint8 { return cast(ctx).VDP2ReadVRAM<uint8>(address); },
        [](uint32 address, void *ctx) -> uint16 { return cast(ctx).VDP2ReadVRAM<uint16>(address); },
        [](uint32 address, void *ctx) -> uint32 {
            uint32 value = cast(ctx).VDP2ReadVRAM<uint16>(address + 0) << 16u;
            value |= cast(ctx).VDP2ReadVRAM<uint16>(address + 2) << 0u;
            return value;
        },
        [](uint32 address, uint8 value, void *ctx) { cast(ctx).VDP2WriteVRAM<uint8>(address, value); },
        [](uint32 address, uint16 value, void *ctx) { cast(ctx).VDP2WriteVRAM<uint16>(address, value); },
        [](uint32 address, uint32 value, void *ctx) {
            cast(ctx).VDP2WriteVRAM<uint16>(address + 0, value >> 16u);
            cast(ctx).VDP2WriteVRAM<uint16>(address + 2, value >> 0u);
        });

    // VDP2 CRAM
    bus.MapNormal(
        0x5F0'0000, 0x5F7'FFFF, this,
        [](uint32 address, void *ctx) -> uint8 { return cast(ctx).VDP2ReadCRAM<uint8, false>(address); },
        [](uint32 address, void *ctx) -> uint16 { return cast(ctx).VDP2ReadCRAM<uint16, false>(address); },
        [](uint32 address, void *ctx) -> uint32 {
            uint32 value = cast(ctx).VDP2ReadCRAM<uint16, false>(address + 0) << 16u;
            value |= cast(ctx).VDP2ReadCRAM<uint16, false>(address + 2) << 0u;
            return value;
        },
        [](uint32 address, uint8 value, void *ctx) { cast(ctx).VDP2WriteCRAM<uint8, false>(address, value); },
        [](uint32 address, uint16 value, void *ctx) { cast(ctx).VDP2WriteCRAM<uint16, false>(address, value); },
        [](uint32 address, uint32 value, void *ctx) {
            cast(ctx).VDP2WriteCRAM<uint16, false>(address + 0, value >> 16u);
            cast(ctx).VDP2WriteCRAM<uint16, false>(address + 2, value >> 0u);
        });

    bus.MapSideEffectFree(
        0x5F0'0000, 0x5F7'FFFF, this,
        [](uint32 address, void *ctx) -> uint8 { return cast(ctx).VDP2ReadCRAM<uint8, true>(address); },
        [](uint32 address, void *ctx) -> uint16 { return cast(ctx).VDP2ReadCRAM<uint16, true>(address); },
        [](uint32 address, void *ctx) -> uint32 {
            uint32 value = cast(ctx).VDP2ReadCRAM<uint16, true>(address + 0) << 16u;
            value |= cast(ctx).VDP2ReadCRAM<uint16, true>(address + 2) << 0u;
            return value;
        },
        [](uint32 address, uint8 value, void *ctx) { cast(ctx).VDP2WriteCRAM<uint8, true>(address, value); },
        [](uint32 address, uint16 value, void *ctx) { cast(ctx).VDP2WriteCRAM<uint16, true>(address, value); },
        [](uint32 address, uint32 value, void *ctx) {
            cast(ctx).VDP2WriteCRAM<uint16, true>(address + 0, value >> 16u);
            cast(ctx).VDP2WriteCRAM<uint16, true>(address + 2, value >> 0u);
        });

    // VDP2 registers
    bus.MapNormal(
        0x5F8'0000, 0x5FB'FFFF, this,
        [](uint32 address, void *ctx) -> uint8 {
            address &= 0x1FF;
            devlog::trace<grp::vdp2_regs>("Illegal 8-bit VDP2 register read from {:05X}", address);
            uint16 value = cast(ctx).VDP2ReadReg<false>(address & ~1u);
            return value >> ((~address & 1u) * 8u);
        },
        [](uint32 address, void *ctx) -> uint16 { return cast(ctx).VDP2ReadReg<false>(address); },
        [](uint32 address, void *ctx) -> uint32 {
            uint32 value = cast(ctx).VDP2ReadReg<false>(address + 0) << 16u;
            value |= cast(ctx).VDP2ReadReg<false>(address + 2) << 0u;
            return value;
        },
        [](uint32 address, uint8 value, void * /*ctx*/) {
            address &= 0x1FF;
            devlog::debug<grp::vdp2_regs>("Illegal 8-bit VDP2 register write to {:05X} = {:02X}", address, value);
            // TODO: should these go through anyway?
        },
        [](uint32 address, uint16 value, void *ctx) { cast(ctx).VDP2WriteReg<false>(address, value); },
        [](uint32 address, uint32 value, void *ctx) {
            cast(ctx).VDP2WriteReg<false>(address + 0, value >> 16u);
            cast(ctx).VDP2WriteReg<false>(address + 2, value >> 0u);
        });

    bus.MapSideEffectFree(
        0x5F8'0000, 0x5FB'FFFF, this,
        [](uint32 address, void *ctx) -> uint8 {
            const uint16 value = cast(ctx).VDP2ReadReg<true>(address & ~1);
            return value >> ((~address & 1) * 8u);
        },
        [](uint32 address, void *ctx) -> uint16 { return cast(ctx).VDP2ReadReg<true>(address); },
        [](uint32 address, void *ctx) -> uint32 {
            uint32 value = cast(ctx).VDP2ReadReg<true>(address + 0) << 16u;
            value |= cast(ctx).VDP2ReadReg<true>(address + 2) << 0u;
            return value;
        },
        [](uint32 address, uint8 value, void *ctx) {
            uint16 currValue = cast(ctx).VDP2ReadReg<true>(address & ~1);
            const uint16 shift = (~address & 1) * 8u;
            const uint16 mask = ~(0xFF << shift);
            currValue = (currValue & mask) | (value << shift);
            cast(ctx).VDP2WriteReg<true>(address & ~1, currValue);
        },
        [](uint32 address, uint16 value, void *ctx) { cast(ctx).VDP2WriteReg<true>(address, value); },
        [](uint32 address, uint32 value, void *ctx) {
            cast(ctx).VDP2WriteReg<true>(address + 0, value >> 16u);
            cast(ctx).VDP2WriteReg<true>(address + 2, value >> 0u);
        });
}

void VDP::Advance(uint64 cycles) {
    if (m_VDP1CtlState.drawing) {
        // HACK: give VDP1 way more cycles than needed to compensate for some optimizations not accounted for in VDP
        // cost estimations
        // TODO: include cycle counts for:
        // - gouraud shading
        // - transparent sprites
        // - different costs for each command (untextured polygons are cheaper, for instance)
        // TODO: pixel- and texel-level cycle counting
        // - VRAM access penalties (for Mega Man X3)
        // - high-speed shrink, end codes, user clipping (all of these reduce costs)
        cycles <<= m_VDP1CyclesShift;

        // Apply timing penalty
        if (m_VDP1TimingPenaltyCycles > 0) {
            if (cycles <= m_VDP1TimingPenaltyCycles) {
                m_VDP1TimingPenaltyCycles -= cycles;
                return;
            } else {
                cycles -= m_VDP1TimingPenaltyCycles;
                m_VDP1TimingPenaltyCycles = 0;
            }
        }

        // Compensate spillover cycles
        if (cycles <= m_VDP1CtlState.spilloverCycles) {
            // Not enough cycles to cover the overspending from last iteration.
            m_VDP1CtlState.spilloverCycles -= cycles;
            return;
        }

        // Our budget is however many cycles we've been requested to run minus the spillover from a previous command.
        uint64 cycleBudget = cycles - m_VDP1CtlState.spilloverCycles;
        while (cycleBudget > 0 && m_VDP1CtlState.drawing) {
            const uint64 cyclesSpent = VDP1ProcessCommand();
            if (cyclesSpent >= cycleBudget) {
                // Spent all available cycles.
                // Store excess cycles spent to deduct from next iterations.
                m_VDP1CtlState.spilloverCycles = cyclesSpent - cycleBudget;
                break;
            }
            cycleBudget -= cyclesSpent;
        }
    }
}

void VDP::DumpVDP1VRAM(std::ostream &out) const {
    out.write((const char *)m_state.mem1.VRAM.data(), m_state.mem1.VRAM.size());
}

void VDP::DumpVDP2VRAM(std::ostream &out) const {
    out.write((const char *)m_state.mem2.VRAM.data(), m_state.mem2.VRAM.size());
}

void VDP::DumpVDP2CRAM(std::ostream &out) const {
    out.write((const char *)m_state.mem2.CRAM.data(), m_state.mem2.CRAM.size());
}

void VDP::DumpVDP1Framebuffers(std::ostream &out) const {
    const uint8 dispFB = m_state.fbIndex.display;
    const uint8 drawFB = m_state.fbIndex.draw;
    out.write((const char *)m_state.mem1.FBRAM[drawFB].data(), m_state.mem1.FBRAM[drawFB].size());
    out.write((const char *)m_state.mem1.FBRAM[dispFB].data(), m_state.mem1.FBRAM[dispFB].size());
    m_renderer->DumpExtraVDP1Framebuffers(out);
}

// -----------------------------------------------------------------------------
// VDP1 memory/register access

template <mem_primitive_16 T>
FORCE_INLINE T VDP::VDP1ReadVRAM(uint32 address) const {
    return m_state.mem1.ReadVRAM<T>(address);
}

template <mem_primitive_16 T>
FORCE_INLINE void VDP::VDP1WriteVRAM(uint32 address, T value) {
    m_state.mem1.WriteVRAM<T>(address, value,
                              [&](uint32 address, T value) { m_renderer->VDP1WriteVRAM(address, value); });
    if (m_stallVDP1OnVRAMWrites && m_VDP1CtlState.drawing) {
        m_VDP1TimingPenaltyCycles += kVDP1TimingPenaltyPerWrite;
    }
    m_VDP1CtlState.inInfiniteLoop = false;
}

template <mem_primitive_16 T, bool peek>
FORCE_INLINE T VDP::VDP1ReadFB(uint32 address) const {
    if constexpr (peek) {
        m_renderer->VDP1DebugSyncFB();
    } else {
        m_renderer->VDP1SyncFB();
    }
    return m_state.VDP1ReadFB<T>(address);
}

template <mem_primitive_16 T>
FORCE_INLINE void VDP::VDP1WriteFB(uint32 address, T value) {
    m_state.VDP1WriteFB<T>(address, value, [&](uint32 address, T value) { m_renderer->VDP1WriteFB(address, value); });
}

template <bool peek>
FORCE_INLINE uint16 VDP::VDP1ReadReg(uint32 address) const {
    return m_state.VDP1ReadReg<peek>(address);
}

template <bool poke>
FORCE_INLINE void VDP::VDP1WriteReg(uint32 address, uint16 value) {
    m_state.VDP1WriteReg<poke>(address, value, [&](uint32 address, uint16 value) {
        m_renderer->VDP1WriteReg(address, value);

        if constexpr (!poke) {
            switch (address) {
            case 0x00: // TVMR
                devlog::trace<grp::vdp1_regs>("Write to TVM={:d}{:d}{:d}", m_state.regs1.tvmr.hdtvEnable,
                                              m_state.regs1.tvmr.fbRotEnable, m_state.regs1.tvmr.pixel8Bits);
                devlog::trace<grp::vdp1_regs>("Write to VBE={:d}", m_state.regs1.tvmr.vblankErase);
                break;
            case 0x02: // FBCR
                devlog::trace<grp::vdp1_regs>("Write to DIE={:d} DIL={:d}", m_state.regs1.dblInterlaceEnable,
                                              m_state.regs1.dblInterlaceDrawLine);
                devlog::trace<grp::vdp1_regs>("Write to FCM={:d} FCT={:d}", m_state.regs1.fbSwapMode,
                                              m_state.regs1.fbSwapTrigger);
                break;
            case 0x04: // PTMR
                devlog::trace<grp::vdp1_regs>("Write to PTM={:d}", m_state.regs1.plotTrigger);
                if (m_state.regs1.plotTrigger == 0b01) {
                    VDP1BeginFrame();

                    // HACK: insert a delay to dodge some timing issues with games that trigger drawing too early
                    // Games known to require this hack:
                    // - Fighter's History Dynamite
                    // - Cyberbots - Fullmetal Madness
                    // - Earthworm Jim 2
                    if (m_state.VPhase != VerticalPhase::Active) {
                        m_VDP1TimingPenaltyCycles += 25000;
                    }
                }
                break;
            case 0x0C: // ENDR
                // TODO: schedule drawing termination after 30 cycles
                m_VDP1CtlState.drawing = false;
                m_VDP1TimingPenaltyCycles = 0;
                break;
            }
        }
    });
}

// -----------------------------------------------------------------------------
// VDP2 memory/register access

template <mem_primitive_16 T>
FORCE_INLINE T VDP::VDP2ReadVRAM(uint32 address) const {
    return m_state.mem2.ReadVRAM<T>(address);
}

template <mem_primitive_16 T>
FORCE_INLINE void VDP::VDP2WriteVRAM(uint32 address, T value) {
    m_state.mem2.WriteVRAM<T>(address, value,
                              [&](uint32 address, T value) { m_renderer->VDP2WriteVRAM(address, value); });
}

template <mem_primitive_16 T, bool peek>
FORCE_INLINE T VDP::VDP2ReadCRAM(uint32 address) const {
    return m_state.mem2.ReadCRAM<T>(address, [&](uint32 address, T value) {
        if constexpr (!peek) {
            devlog::trace<grp::vdp2_regs>("{}-bit VDP2 CRAM read from {:03X} = {:X}", sizeof(T) * 8, address, value);
        }
    });
}

template <mem_primitive_16 T, bool poke>
FORCE_INLINE void VDP::VDP2WriteCRAM(uint32 address, T value) {
    m_state.mem2.WriteCRAM<T>(address, value, [&](uint32 address, T value) {
        m_renderer->VDP2WriteCRAM(address, value);
        if constexpr (!poke) {
            devlog::trace<grp::vdp2_regs>("{}-bit VDP2 CRAM write to {:05X} = {:X}", sizeof(T) * 8, address, value);
        }
    });
}

template <bool peek>
FORCE_INLINE uint16 VDP::VDP2ReadReg(uint32 address) const {
    return m_state.VDP2ReadReg<peek>(address);
}

template <bool poke>
FORCE_INLINE void VDP::VDP2WriteReg(uint32 address, uint16 value) {
    m_state.VDP2WriteReg<poke>(address, value, [&](uint32 address, uint16 value) {
        m_renderer->VDP2WriteReg(address, value);
        if constexpr (!poke) {
            devlog::trace<grp::vdp2_regs>("VDP2 register write to {:03X} = {:04X}", address, value);

            if (address == 0x000) {
                devlog::trace<grp::vdp2_regs>(
                    "TVMD write: {:04X} - HRESO={:d} VRESO={:d} LSMD={:d} BDCLMD={:d} DISP={:d}{}",
                    m_state.regs2.TVMD.u16, (uint16)m_state.regs2.TVMD.HRESOn, (uint16)m_state.regs2.TVMD.VRESOn,
                    (uint16)m_state.regs2.TVMD.LSMDn, (uint16)m_state.regs2.TVMD.BDCLMD,
                    (uint16)m_state.regs2.TVMD.DISP, (m_state.regs2.TVMDDirty ? " (dirty)" : ""));
            }
        }
    });
}

// -----------------------------------------------------------------------------
// Save states

void VDP::SaveState(savestate::VDPSaveState &state) const {
    m_renderer->PreSaveStateSync();

    m_state.SaveState(state);
    m_renderer->SaveState(state.renderer);
    // TODO: figure out how to save/load states between different renderers
    // - also figure out how much state can be derived from the registers alone to remove redundancy

    state.vdp1State.drawing = m_VDP1CtlState.drawing;
    state.vdp1State.doDisplayErase = m_VDP1CtlState.doDisplayErase;
    state.vdp1State.doVBlankErase = m_VDP1CtlState.doVBlankErase;
    state.vdp1State.spilloverCycles = m_VDP1CtlState.spilloverCycles;
    state.vdp1State.timingPenalty = m_VDP1TimingPenaltyCycles;
}

bool VDP::ValidateState(const savestate::VDPSaveState &state) const {
    if (!m_state.ValidateState(state)) {
        return false;
    }
    if (!m_renderer->ValidateState(state.renderer)) {
        return false;
    }
    return true;
}

void VDP::LoadState(const savestate::VDPSaveState &state) {
    m_state.LoadState(state);
    m_renderer->LoadState(state.renderer);

    m_renderer->PostLoadStateSync();

    m_VDP1CtlState.drawing = state.vdp1State.drawing;
    m_VDP1CtlState.doDisplayErase = state.vdp1State.doDisplayErase;
    m_VDP1CtlState.doVBlankErase = state.vdp1State.doVBlankErase;
    m_VDP1CtlState.spilloverCycles = state.vdp1State.spilloverCycles;
    m_VDP1TimingPenaltyCycles = state.vdp1State.timingPenalty;

    UpdateResolution<true>();

    switch (m_state.VPhase) {
    case VerticalPhase::Active: [[fallthrough]];
    case VerticalPhase::BottomBorder: [[fallthrough]];
    case VerticalPhase::BlankingAndSync: m_state.regs2.VCNTSkip = 0; break;
    case VerticalPhase::VCounterSkip: [[fallthrough]];
    case VerticalPhase::TopBorder: [[fallthrough]];
    case VerticalPhase::LastLine: m_state.regs2.VCNTSkip = m_VCounterSkip; break;
    }
}

void VDP::OnPhaseUpdateEvent(core::EventContext &eventContext, void *userContext) {
    auto &vdp = *static_cast<VDP *>(userContext);
    vdp.UpdatePhase();
    const uint64 cycles = vdp.GetPhaseCycles();
    eventContext.Reschedule(cycles);
}

void VDP::SetVideoStandard(VideoStandard videoStandard) {
    const bool pal = videoStandard == VideoStandard::PAL;
    if (m_state.regs2.TVSTAT.PAL != pal) {
        m_state.regs2.TVSTAT.PAL = pal;
        m_state.regs2.TVMDDirty = true;
    }
}

FORCE_INLINE void VDP::UpdatePhase() {
    auto nextPhase = static_cast<uint32>(m_state.HPhase) + 1;
    if (nextPhase == m_HTimings.size()) {
        nextPhase = 0;
    }

    m_state.HPhase = static_cast<HorizontalPhase>(nextPhase);
    switch (m_state.HPhase) {
    case HorizontalPhase::Active: BeginHPhaseActiveDisplay(); break;
    case HorizontalPhase::RightBorder: BeginHPhaseRightBorder(); break;
    case HorizontalPhase::Sync: BeginHPhaseSync(); break;
    case HorizontalPhase::LeftBorder: BeginHPhaseLeftBorder(); break;
    }
}

FORCE_INLINE uint64 VDP::GetPhaseCycles() const {
    return m_HTimings[static_cast<uint32>(m_state.HPhase)];
}

template <bool verbose>
void VDP::UpdateResolution() {
    if (!m_state.regs2.TVMDDirty) {
        return;
    }

    m_state.regs2.TVMDDirty = false;

    // Horizontal/vertical resolution tables
    // NTSC uses the first two vRes entries, PAL uses the full table, and exclusive monitors use 480 lines
    // For exclusive monitor: even hRes entries are valid for 31 KHz monitors, odd are for Hi-Vision
    static constexpr uint32 hRes[] = {320, 352, 640, 704};
    static constexpr uint32 vRes[] = {224, 240, 256, 256};

    const bool exclusiveMonitor = m_state.regs2.TVMD.HRESOn & 4;
    const bool interlaced = m_state.regs2.TVMD.IsInterlaced();
    m_HRes = hRes[m_state.regs2.TVMD.HRESOn & 3];
    m_VRes = exclusiveMonitor ? 480 : vRes[m_state.regs2.TVMD.VRESOn & (m_state.regs2.TVSTAT.PAL ? 3 : 1)];
    if (!exclusiveMonitor && interlaced) {
        // Interlaced modes double the vertical resolution
        m_VRes *= 2;
    }
    m_exclusiveMonitor = exclusiveMonitor;

    // Timing tables

    // Horizontal phase timings (cycles until):
    //   RBd = Right Border
    //   HSy = Horizontal Sync
    //   LBd = Left Border
    //   ADp = Active Display
    // NOTE: these timings specify the HCNT interval between phases
    // TODO: check exclusive monitor timings
    static constexpr std::array<std::array<uint32, 4>, 8> hTimings{{
        // RBd, HSy, LBd, ADp
        {320, 54, 26, 27},  // {320, 374, 400, 427}, // Normal Graphic A
        {352, 51, 29, 23},  // {352, 403, 432, 455}, // Normal Graphic B
        {640, 108, 52, 54}, // {640, 748, 800, 854}, // Hi-Res Graphic A
        {704, 102, 58, 46}, // {704, 806, 864, 910}, // Hi-Res Graphic B
        {160, 27, 13, 13},  // {160, 187, 200, 213}, // Exclusive Normal Graphic A (wild guess)
        {176, 11, 13, 12},  // {176, 187, 200, 212}, // Exclusive Normal Graphic B (wild guess)
        {320, 54, 26, 26},  // {320, 374, 400, 426}, // Exclusive Hi-Res Graphic A (wild guess)
        {352, 22, 26, 24},  // {352, 374, 400, 424}, // Exclusive Hi-Res Graphic B (wild guess)
    }};
    m_HTimings = hTimings[m_state.regs2.TVMD.HRESOn];

    // Derive right shift amount to be applied to HCNT<<1
    m_state.regs2.HCNTShift = m_state.regs2.TVMD.HRESOn <= 1   ? 1  // Normal modes: HCNT << 1
                              : m_state.regs2.TVMD.HRESOn <= 3 ? 2  // Hi-Res modes: HCNT >> 1
                              : m_state.regs2.TVMD.HRESOn >= 6 ? 2  // Excl. Hi-Res: HCNT >> 1
                                                               : 0; // Other modes:  HCNT unchanged

    // Derive HCNT mask to be applied to (HCNT<<1) >> HCNTShift
    m_state.regs2.HCNTMask = m_state.regs2.TVMD.HRESOn <= 1   ? 0x3FE  // Normal modes:  HCT0 invalid
                             : m_state.regs2.TVMD.HRESOn >= 6 ? 0x1FF  // Any Exclusive: HCT9 invalid
                                                              : 0x3FF; // Other modes:   HCNT unchanged

    // Vertical phase timings (to reach):
    //   BBd = Bottom Border
    //   BSy = Blanking and Vertical Sync
    //   VCS = VCNT skip
    //   TBd = Top Border
    //   LLn = Last Line
    //   ADp = Active Display
    // NOTE: these timings indicate the VCNT at which the specified phase begins
    // TODO: check exclusive monitor timings
    // TODO: interlaced mode timings for odd fields:
    // - normal modes: 1 less line
    // - exclusive modes: 2 more lines
    static constexpr std::array<std::array<std::array<std::array<uint32, 6>, 2>, 4>, 3> vTimingsNormal{{
        // NTSC
        {{
            // BBd, BSy, VCS, TBd, LLn, ADp
            {{
                {224, 232, 237, 255, 262, 263}, // even/progressive
                {224, 232, 237, 255, 261, 262}, // odd
            }},
            {{
                {240, 240, 245, 255, 262, 263},
                {240, 240, 245, 255, 261, 262},
            }},
            {{
                {224, 232, 237, 255, 262, 263},
                {224, 232, 237, 255, 261, 262},
            }},
            {{
                {240, 240, 245, 255, 262, 263},
                {240, 240, 245, 255, 261, 262},
            }},
        }},
        // PAL
        {{
            // BBd, BSy, VCS, TBd, LLn, ADp
            {{
                {224, 256, 259, 281, 312, 313},
                {224, 256, 259, 281, 311, 312},
            }},
            {{
                {240, 264, 267, 289, 312, 313},
                {240, 264, 267, 289, 311, 312},
            }},
            {{
                {256, 272, 275, 297, 312, 313},
                {256, 272, 275, 297, 311, 312},
            }},
            {{
                {256, 272, 275, 297, 312, 313},
                {256, 272, 275, 297, 311, 312},
            }},
        }},
    }};
    static constexpr std::array<std::array<std::array<uint32, 6>, 2>, 2> vTimingsExclusive{{
        // Exclusive monitor A (wild guess)
        {{
            // BBd, BSy, VCS, TBd, LLn, ADp
            {480, 496, 506, 509, 524, 525}, // even/progressive
            {480, 496, 506, 509, 526, 527}, // odd
        }},
        // Exclusive monitor B (wild guess)
        {{
            // BBd, BSy, VCS, TBd, LLn, ADp
            {480, 496, 506, 546, 561, 562},
            {480, 496, 506, 546, 563, 564},
        }},
    }};
    m_VTimings = exclusiveMonitor ? vTimingsExclusive[m_state.regs2.TVMD.HRESOn & 1]
                                  : vTimingsNormal[m_state.regs2.TVSTAT.PAL][m_state.regs2.TVMD.VRESOn];
    m_VTimingField = static_cast<uint32>(interlaced) & m_state.regs2.TVSTAT.ODD;

    // Adjust for dot clock and SH-2 clock factor
    const uint32 dotClockMult = (m_state.regs2.TVMD.HRESOn & 2) ? 2 : 4;
    const auto [clockFactorNum, clockFactorDen] = m_config.system.sh2ClockFactor->Pair();
    for (auto &timing : m_HTimings) {
        timing = timing * dotClockMult * clockFactorNum / clockFactorDen;
        timing = std::max(timing, 1u); // safeguard against infinite loop with the scheduler
    }

    // Compute cycles available for VBlank erase
    // TODO: penalty should be 200, but doing so results in less pixels than necessary being erased
    // Test cases:
    //   Game                Where                Reso.     Sprite bits
    //   Battle Garegga      Options menu         320x480   16
    //   Die Hard Arcade     Menus, in-game       704x240   8
    //   Guardian Heroes     Main menu, in-game   320x224   16
    //   Linkle Liver Story  In-game              320x224   16
    //   Powerslave          Menus, in-game       320x240   16
    //   Panzer Dragoon      FMV subtitles        352x224   16
    //   Sonic R             In-game              352x224   16
    static constexpr uint32 kVBEHorzPenalty = 113;
    static constexpr std::array<uint32, 8> kVBEHorzTimings{{
        1708 - kVBEHorzPenalty, // Normal Graphic A
        1820 - kVBEHorzPenalty, // Normal Graphic B
        1708 - kVBEHorzPenalty, // Hi-Res Graphic A
        1820 - kVBEHorzPenalty, // Hi-Res Graphic B
        852 - kVBEHorzPenalty,  // Exclusive Normal Graphic A
        848 - kVBEHorzPenalty,  // Exclusive Normal Graphic B
        852 - kVBEHorzPenalty,  // Exclusive Hi-Res Graphic A
        848 - kVBEHorzPenalty,  // Exclusive Hi-Res Graphic B
    }};
    static constexpr auto kVPActiveIndex = static_cast<uint32>(VerticalPhase::Active);
    static constexpr auto kVPLastLineIndex = static_cast<uint32>(VerticalPhase::LastLine);
    // NOTE: intentionally not scaling VBlank erase cycles to ensure it completes when the system is underclocked
    m_VBlankEraseCyclesPerLine = kVBEHorzTimings[m_state.regs2.TVMD.HRESOn];
    m_VBlankEraseLines = {
        m_VTimings[0][kVPLastLineIndex] - m_VTimings[0][kVPActiveIndex],
        m_VTimings[1][kVPLastLineIndex] - m_VTimings[1][kVPActiveIndex],
    };

    m_state.regs2.VCNTShift = m_state.regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity ? 1 : 0;

    // TODO: field skips must be handled per frame
    if (exclusiveMonitor) {
        const uint16 baseSkip = (m_state.regs2.TVMD.HRESOn & 1) ? 562 : 525;
        const uint16 fieldSkip = ~m_state.regs2.TVSTAT.ODD & static_cast<uint16>(interlaced);
        m_VCounterSkip = ((0x400 - baseSkip) >> 1u) - fieldSkip;
    } else {
        const uint16 baseSkip = m_state.regs2.TVSTAT.PAL ? 313 : 263;
        const uint16 fieldSkip = ~m_state.regs2.TVSTAT.ODD & static_cast<uint16>(interlaced);
        m_VCounterSkip = 0x200 - baseSkip + fieldSkip;
    }

    m_renderer->VDP2SetResolution(m_HRes, m_VRes, m_exclusiveMonitor);

    if constexpr (verbose) {
        devlog::info<grp::vdp2>("Screen resolution set to {}x{}", m_HRes, m_VRes);
        switch (m_state.regs2.TVMD.LSMDn) {
        case InterlaceMode::None: devlog::info<grp::vdp2>("Non-interlace mode"); break;
        case InterlaceMode::Invalid: devlog::info<grp::vdp2>("Invalid interlace mode"); break;
        case InterlaceMode::SingleDensity: devlog::info<grp::vdp2>("Single-density interlace mode"); break;
        case InterlaceMode::DoubleDensity: devlog::info<grp::vdp2>("Double-density interlace mode"); break;
        }
        devlog::info<grp::vdp2>("Dot clock mult = {}, display {}", dotClockMult,
                                (m_state.regs2.displayEnabledLatch ? "ON" : "OFF"));
    }
}

FORCE_INLINE void VDP::IncrementVCounter() {
    ++m_state.regs2.VCNT;
    while (m_state.regs2.VCNT >= m_VTimings[m_VTimingField][static_cast<uint32>(m_state.VPhase)]) {
        auto nextPhase = static_cast<uint32>(m_state.VPhase) + 1;
        if (nextPhase == m_VTimings[m_VTimingField].size()) {
            m_state.regs2.VCNT = 0;
            nextPhase = 0;
        }

        m_state.VPhase = static_cast<VerticalPhase>(nextPhase);
        switch (m_state.VPhase) {
        case VerticalPhase::Active: BeginVPhaseActiveDisplay(); break;
        case VerticalPhase::BottomBorder: BeginVPhaseBottomBorder(); break;
        case VerticalPhase::BlankingAndSync: BeginVPhaseBlankingAndSync(); break;
        case VerticalPhase::VCounterSkip: BeginVPhaseVCounterSkip(); break;
        case VerticalPhase::TopBorder: BeginVPhaseTopBorder(); break;
        case VerticalPhase::LastLine: BeginVPhaseLastLine(); break;
        }
    }
    devlog::trace<grp::base>("VCNT = {:3d}  phase = {:d}", m_state.regs2.VCNT, static_cast<uint32>(m_state.VPhase));
}

// ----

void VDP::BeginHPhaseActiveDisplay() {
    devlog::trace<grp::hphase>("(VCNT = {:3d})  Entering horizontal active display phase", m_state.regs2.VCNT);
    if (m_state.VPhase == VerticalPhase::Active) {
        if (m_state.regs2.VCNT == m_VTimings[m_VTimingField][0] - 16) { // ~1ms before VBlank IN
            m_cbTriggerOptimizedINTBACKRead();
        }

        m_renderer->VDP2RenderLine(m_state.regs2.VCNT);
    }
}

void VDP::BeginHPhaseRightBorder() {
    devlog::trace<grp::hphase>("(VCNT = {:3d})  Entering right border phase", m_state.regs2.VCNT);

    devlog::trace<grp::intr_hb>("## HBlank IN {:3d}", m_state.regs2.VCNT);

    m_state.regs2.TVSTAT.HBLANK = 1;
    m_cbHBlankStateChange(true, m_state.regs2.TVSTAT.VBLANK);

    // Start erasing if we just entered VBlank IN
    if (m_state.regs2.VCNT == m_VTimings[m_VTimingField][static_cast<uint32>(VerticalPhase::Active)]) {
        devlog::trace<grp::intr>("## HBlank IN + VBlank IN  VBE={:d}", m_state.regs1.tvmr.vblankErase);

        m_VDP1CtlState.doVBlankErase = m_state.regs1.tvmr.vblankErase;

        // If we just entered the bottom blanking vertical phase, switch fields
        if (m_state.regs2.TVMD.LSMDn != InterlaceMode::None) {
            m_state.regs2.TVSTAT.ODD ^= 1;
            m_VTimingField = m_state.regs2.TVSTAT.ODD;
            devlog::trace<grp::vdp2_render>("Switched to {} field", (m_state.regs2.TVSTAT.ODD ? "odd" : "even"));
            m_renderer->VDP2SetField(m_state.regs2.TVSTAT.ODD);
        } else {
            if (m_state.regs2.TVSTAT.ODD != 1) {
                m_state.regs2.TVSTAT.ODD = 1;
                m_VTimingField = 0;
                m_renderer->VDP2SetField(m_state.regs2.TVSTAT.ODD);
            }
        }
    }

    // TODO: draw border
}

void VDP::BeginHPhaseSync() {
    devlog::trace<grp::hphase>("(VCNT = {:3d})  Entering horizontal sync phase", m_state.regs2.VCNT);

    // This phase intentionally does nothing to insert a gap between the two border phases
}

void VDP::BeginHPhaseLeftBorder() {
    devlog::trace<grp::hphase>("(VCNT = {:3d})  Entering left border phase", m_state.regs2.VCNT);

    if (m_state.VPhase == VerticalPhase::LastLine) {
        auto &ctx1 = m_VDP1CtlState;

        devlog::trace<grp::intr>("## HBlank end + VBlank OUT  FCM={:d} FCT={:d} VBE={:d} PTM={:d} changed={}",
                                 m_state.regs1.fbSwapMode, m_state.regs1.fbSwapTrigger, m_state.regs1.tvmr.vblankErase,
                                 m_state.regs1.plotTrigger, m_state.regs1.fbParamsChanged);

        bool erase = false;
        bool swap = false;

        if (!m_state.regs1.fbSwapMode) {
            // 1-cycle framebuffer erase+swap
            erase = true;
            swap = true;
        } else if (m_state.regs1.fbParamsChanged) {
            // Manual erase/swap
            if (m_state.regs1.fbSwapTrigger) {
                swap = true;
            } else {
                erase = true;
            }
        }

        // Clear manual erase/swap trigger
        m_state.regs1.fbParamsChanged = false;

        // End VBlank erase if in progress
        if (m_VDP1CtlState.doVBlankErase) {
            m_renderer->VDP1EraseFramebuffer(m_VBlankEraseCyclesPerLine * m_VBlankEraseLines[m_VTimingField]);
        }

        if (erase) {
            ctx1.doDisplayErase = true;
        }
        if (swap) {
            VDP1SwapFramebuffer();
        }
    }

    m_state.regs2.TVSTAT.HBLANK = 0;
    if (m_state.VPhase == VerticalPhase::Active) {
        m_cbHBlankStateChange(false, m_state.regs2.TVSTAT.VBLANK);
    }

    IncrementVCounter();

    // TODO: draw border
}

// ----

void VDP::BeginVPhaseActiveDisplay() {
    devlog::trace<grp::vphase>("(VCNT = {:3d})  Entering vertical active display phase", m_state.regs2.VCNT);

    m_state.regs2.VCNTSkip = 0;
}

void VDP::BeginVPhaseBottomBorder() {
    devlog::trace<grp::vphase>("(VCNT = {:3d})  Entering bottom border phase", m_state.regs2.VCNT);

    devlog::trace<grp::intr>("## VBlank IN");

    m_state.regs2.TVSTAT.VBLANK = 1;
    m_cbVBlankStateChange(true);
    m_cbTriggerSMPCVBlankIN();

    // TODO: draw border
}

void VDP::BeginVPhaseBlankingAndSync() {
    devlog::trace<grp::vphase>("(VCNT = {:3d})  Entering blanking/vertical sync phase", m_state.regs2.VCNT);

    // End frame
    devlog::trace<grp::vdp2_render>("End VDP2 frame");
    m_renderer->VDP2EndFrame();

    // Begin erasing display framebuffer during display
    if (m_VDP1CtlState.doDisplayErase) {
        m_VDP1CtlState.doDisplayErase = false;
        // TODO: erase line by line instead of the entire framebuffer in one go
        // No need to count cycles here; there's always enough cycles in the display area to clear the entire screen
        m_renderer->VDP1EraseFramebuffer(0);
    }
}

void VDP::BeginVPhaseVCounterSkip() {
    devlog::trace<grp::vphase>("(VCNT = {:3d})  Entering vertical counter skip phase", m_state.regs2.VCNT);

    m_state.regs2.VCNTSkip = m_VCounterSkip;
}

void VDP::BeginVPhaseTopBorder() {
    devlog::trace<grp::vphase>("(VCNT = {:3d})  Entering top border phase", m_state.regs2.VCNT);

    UpdateResolution<true>();

    // Latch TVMD flags
    m_state.regs2.LatchTVMD();
    m_renderer->VDP2LatchTVMD();

    // TODO: draw border
}

void VDP::BeginVPhaseLastLine() {
    devlog::trace<grp::vphase>("(VCNT = {:3d})  Entering last line phase", m_state.regs2.VCNT);

    devlog::trace<grp::intr>("## VBlank OUT");

    devlog::trace<grp::vdp2_render>("Begin VDP2 frame, VDP1 framebuffer {}", m_state.fbIndex.display);

    m_renderer->VDP2BeginFrame();

    m_state.regs2.TVSTAT.VBLANK = 0;
    m_cbVBlankStateChange(false);
}

void VDP::VDP1SwapFramebuffer() {
    devlog::trace<grp::vdp1>("Swapping framebuffers - draw {}, display {}", m_state.fbIndex.display,
                             m_state.fbIndex.draw);

    m_state.regs1.prevCommandAddress = m_state.regs1.currCommandAddress;
    m_state.regs1.prevFrameEnded = m_state.regs1.currFrameEnded;
    m_state.regs1.currFrameEnded = false;

    // TODO: latch PTM, EOS, DIE, DIL

    m_state.regs1.LatchEraseParameters();

    m_renderer->VDP1SwapFramebuffer();

    // NOTE: this flip needs to happen *after* the renderer processes the framebuffer swap.
    // Renderers may rely on this flag to determine which framebuffer to read when rendering the sprite layer on VDP2,
    // and may use multithreading to draw VDP1 and VDP2 graphics. If the flag is flipped before the renderer has a
    // chance to process graphics, it may cause such renderers to display the incorrect VDP1 framebuffer on VDP2.
    m_state.fbIndex.Flip();

    if (bit::test<1>(m_state.regs1.plotTrigger)) {
        VDP1BeginFrame();
    }
}

void VDP::VDP1BeginFrame() {
    devlog::trace<grp::vdp1>("Begin VDP1 frame on framebuffer {}", m_state.fbIndex.draw);

    // TODO: setup rendering
    // TODO: figure out VDP1 timings

    m_state.regs1.returnAddress = kVDP1NoReturn;
    m_state.regs1.nextCommandAddress = 0;
    m_state.regs1.currFrameEnded = false;

    bool valid = !m_skipEmptyVDP1Table;
    if (!valid) {
        // Don't even bother processing commands if the table starts with a handful of empty commands
        for (uint32 i = 0; i < 32 * 64; i += sizeof(uint64)) {
            if (util::ReadNE<uint64>(&m_state.mem1.VRAM[i]) != 0) {
                valid = true;
                break;
            }
        }
    }
    if (valid) {
        m_renderer->VDP1BeginFrame();

        m_VDP1CtlState.drawing = true;
        m_VDP1CtlState.lastJumpAddress = 0xFFFFFFFF;
        m_VDP1CtlState.loopCount = 0;
        m_VDP1CtlState.inInfiniteLoop = false;
    } else {
        devlog::warn<grp::vdp1_cmd>("Possible empty command table found; aborting");
    }
}

void VDP::VDP1EndFrame() {
    devlog::trace<grp::vdp1>("End VDP1 frame on framebuffer {}", m_state.fbIndex.draw);

    m_VDP1CtlState.drawing = false;
    m_VDP1TimingPenaltyCycles = 0;

    m_state.regs1.currFrameEnded = true;
    m_cbTriggerSpriteDrawEnd();

    m_renderer->VDP1EndFrame();
}

uint64 VDP::VDP1ProcessCommand() {
    if (!m_VDP1CtlState.drawing) {
        return 0;
    }
    if (m_VDP1CtlState.inInfiniteLoop) {
        return 16;
    }

    // Every command costs 16 cycles to fetch, even if skipped
    uint64 cycles = 16;

    const uint32 cmdAddress = m_state.regs1.nextCommandAddress;
    const VDP1Command::Control control{.u16 = VDP1ReadVRAM<uint16>(cmdAddress)};
    m_state.regs1.currCommandAddress = cmdAddress;

    devlog::trace<grp::vdp1_cmd>("Processing command {:04X} @ {:05X}", control.u16, cmdAddress);
    if (control.end) [[unlikely]] {
        devlog::trace<grp::vdp1_cmd>("End of command list");
        VDP1EndFrame();
    } else if (!control.skip) {
        if (!control.IsValid()) [[unlikely]] {
            devlog::debug<grp::vdp1_cmd>("Invalid command {:X}", static_cast<uint16>(control.command));
            return cycles;
        }
        m_renderer->VDP1ExecuteCommand(cmdAddress, control);
        cycles += VDP1CalcCommandTiming(cmdAddress, control);
    }

    // Go to the next command
    using enum VDP1Command::JumpType;
    uint32 target = cmdAddress;
    switch (control.jumpMode) {
    case Next: target += 0x20; break;
    case Assign: {
        target = (VDP1ReadVRAM<uint16>(cmdAddress + 0x02) << 3u) & ~0x1F;
        devlog::trace<grp::vdp1_cmd>("Jump to {:05X}", target);

        // Simple check for infinite loops
        // - Akumajou Dracula X attempts to jump back to 0 in some cases
        // - Gale Racer stage 1-2 (Mojave Desert) creates an infinite loop at 05140 with the jump at 05280
        // - Rayman leaves garbage in VDP1 VRAM and occasionally runs a command that loops into itself
        // - Sonic R attempts to jump back to 0 in some cases
        // - Stellar Assault SS puts the VDP1 in an infinite loop during boot, breaks out by editing VRAM while in it
        if (target == m_VDP1CtlState.lastJumpAddress) {
            ++m_VDP1CtlState.loopCount;
        } else {
            m_VDP1CtlState.loopCount = 0;
            m_VDP1CtlState.lastJumpAddress = target;
        }

        static constexpr uint32 kMaxLoopIterations = 32;

        if (target == cmdAddress || m_VDP1CtlState.loopCount >= kMaxLoopIterations) {
            if constexpr (devlog::warn_enabled<grp::vdp1_cmd>) {
                if (m_VDP1CtlState.loopCount == kMaxLoopIterations) {
                    devlog::warn<grp::vdp1_cmd>("Possible infinite loop detected; suspending");
                }
            }
            m_VDP1CtlState.inInfiniteLoop = true;
            return cycles;
        }
        break;
    }
    case Call:
        // Nested calls seem to not update the return address
        if (m_state.regs1.returnAddress == kVDP1NoReturn) {
            m_state.regs1.returnAddress = cmdAddress + 0x20;
        }
        target = (VDP1ReadVRAM<uint16>(cmdAddress + 0x02) << 3u) & ~0x1F;
        devlog::trace<grp::vdp1_cmd>("Call {:05X}", target);
        break;
    case Return:
        // Return seems to only return if there was a previous Call
        if (m_state.regs1.returnAddress != kVDP1NoReturn) {
            target = m_state.regs1.returnAddress;
            m_state.regs1.returnAddress = kVDP1NoReturn;
            devlog::trace<grp::vdp1_cmd>("Return to {:05X}", target);
        } else {
            target += 0x20;
            devlog::trace<grp::vdp1_cmd>("Illegal return, advancing to {:05X}", target);
        }
        break;
    }
    m_state.regs1.nextCommandAddress = target & 0x7FFFF;

    return cycles;
}

FORCE_INLINE uint64 VDP::VDP1CalcCommandTiming(uint32 cmdAddress, VDP1Command::Control control) {
    uint64 cycles = 0;

    // HACK: rough cost estimates

    auto lineTiming = [&](CoordS32 coordA, CoordS32 coordB) -> uint32 {
        const uint32 width = abs(coordB.x() - coordA.x());
        const uint32 height = abs(coordB.y() - coordA.y());
        return std::max(width, height);
    };

    auto quadTiming = [&](CoordS32 coordA, CoordS32 coordB, CoordS32 coordC, CoordS32 coordD) -> uint32 {
        uint32 quadCycles = 0;
        QuadStepper quad{coordA, coordB, coordC, coordD};
        for (; quad.CanStep(); quad.Step()) {
            const CoordS32 coordL = quad.LeftEdge().Coord();
            const CoordS32 coordR = quad.RightEdge().Coord();
            quadCycles += lineTiming(coordL, coordR);
        }
        return quadCycles;
    };

    auto simpleQuadTiming = [&](uint32 width, uint32 height) -> uint32 { return width * height; };

    using enum VDP1Command::CommandType;

    switch (control.command) {
    case DrawNormalSprite: //
    {
        const VDP1Command::Size size{.u16 = VDP1ReadVRAM<uint16>(cmdAddress + 0x0A)};
        const uint32 charSizeH = size.H * 8;
        const uint32 charSizeV = size.V;
        cycles += simpleQuadTiming(std::max(charSizeH, 1u), std::max(charSizeV, 1u));
        break;
    }

    case DrawScaledSprite: //
    {
        uint32 width;
        const uint8 zoomPointH = bit::extract<0, 1>(control.zoomPoint);
        if (zoomPointH == 0) {
            const sint32 xa = bit::sign_extend<13>(VDP1ReadVRAM<uint16>(cmdAddress + 0x0C));
            const sint32 xc = bit::sign_extend<13>(VDP1ReadVRAM<uint16>(cmdAddress + 0x14));
            width = abs(xc - xa);
        } else {
            const sint32 xb = bit::sign_extend<13>(VDP1ReadVRAM<uint16>(cmdAddress + 0x10));
            width = abs(xb);
        }

        uint32 height;
        const uint8 zoomPointV = bit::extract<2, 3>(control.zoomPoint);
        if (zoomPointV == 0) {
            const sint32 ya = bit::sign_extend<13>(VDP1ReadVRAM<uint16>(cmdAddress + 0x0E));
            const sint32 yc = bit::sign_extend<13>(VDP1ReadVRAM<uint16>(cmdAddress + 0x16));
            height = abs(yc - ya);
        } else {
            const sint32 yb = bit::sign_extend<13>(VDP1ReadVRAM<uint16>(cmdAddress + 0x12));
            height = abs(yb);
        }

        cycles += simpleQuadTiming(width, height);
        break;
    }
    case DrawDistortedSprite: [[fallthrough]];
    case DrawDistortedSpriteAlt: [[fallthrough]];
    case DrawPolygon: //
    {
        const sint32 xa = bit::sign_extend<13>(VDP1ReadVRAM<uint16>(cmdAddress + 0x0C));
        const sint32 ya = bit::sign_extend<13>(VDP1ReadVRAM<uint16>(cmdAddress + 0x0E));
        const sint32 xb = bit::sign_extend<13>(VDP1ReadVRAM<uint16>(cmdAddress + 0x10));
        const sint32 yb = bit::sign_extend<13>(VDP1ReadVRAM<uint16>(cmdAddress + 0x12));
        const sint32 xc = bit::sign_extend<13>(VDP1ReadVRAM<uint16>(cmdAddress + 0x14));
        const sint32 yc = bit::sign_extend<13>(VDP1ReadVRAM<uint16>(cmdAddress + 0x16));
        const sint32 xd = bit::sign_extend<13>(VDP1ReadVRAM<uint16>(cmdAddress + 0x18));
        const sint32 yd = bit::sign_extend<13>(VDP1ReadVRAM<uint16>(cmdAddress + 0x1A));

        const CoordS32 coordA{xa, ya};
        const CoordS32 coordB{xb, yb};
        const CoordS32 coordC{xc, yc};
        const CoordS32 coordD{xd, yd};

        cycles += quadTiming(coordA, coordB, coordC, coordD);
        break;
    }

    case DrawPolylines: [[fallthrough]];
    case DrawPolylinesAlt: //
    {
        const sint32 xa = bit::sign_extend<13>(VDP1ReadVRAM<uint16>(cmdAddress + 0x0C));
        const sint32 ya = bit::sign_extend<13>(VDP1ReadVRAM<uint16>(cmdAddress + 0x0E));
        const sint32 xb = bit::sign_extend<13>(VDP1ReadVRAM<uint16>(cmdAddress + 0x10));
        const sint32 yb = bit::sign_extend<13>(VDP1ReadVRAM<uint16>(cmdAddress + 0x12));
        const sint32 xc = bit::sign_extend<13>(VDP1ReadVRAM<uint16>(cmdAddress + 0x14));
        const sint32 yc = bit::sign_extend<13>(VDP1ReadVRAM<uint16>(cmdAddress + 0x16));
        const sint32 xd = bit::sign_extend<13>(VDP1ReadVRAM<uint16>(cmdAddress + 0x18));
        const sint32 yd = bit::sign_extend<13>(VDP1ReadVRAM<uint16>(cmdAddress + 0x1A));

        const CoordS32 coordA{xa, ya};
        const CoordS32 coordB{xb, yb};
        const CoordS32 coordC{xc, yc};
        const CoordS32 coordD{xd, yd};

        cycles += lineTiming(coordA, coordB);
        cycles += lineTiming(coordB, coordC);
        cycles += lineTiming(coordC, coordD);
        cycles += lineTiming(coordD, coordA);
        break;
    }
    case DrawLine: //
    {
        const sint32 xa = bit::sign_extend<13>(VDP1ReadVRAM<uint16>(cmdAddress + 0x0C));
        const sint32 ya = bit::sign_extend<13>(VDP1ReadVRAM<uint16>(cmdAddress + 0x0E));
        const sint32 xb = bit::sign_extend<13>(VDP1ReadVRAM<uint16>(cmdAddress + 0x10));
        const sint32 yb = bit::sign_extend<13>(VDP1ReadVRAM<uint16>(cmdAddress + 0x12));

        const CoordS32 coordA{xa, ya};
        const CoordS32 coordB{xb, yb};

        cycles += lineTiming(coordA, coordB);
        break;
    }

    default: break;
    }

    return cycles;
}

void VDP::ExternalLatch(uint16 x, uint16 y) {
    if (m_state.regs2.EXTEN.EXLTEN) {
        m_state.regs2.TVSTAT.EXLTFG = x < m_HRes && y < m_VRes;
        if (m_state.regs2.TVSTAT.EXLTFG) {
            if (m_virtuaGunJitter) {
                auto jitter = [](uint32 &lastValue) {
                    for (;;) {
                        const uint32 value = rand() & 3;
                        if (value != lastValue) {
                            lastValue = value;
                            return value;
                        }
                    }
                };

                x += jitter(m_virtuaGunLastJitterX);
                y += jitter(m_virtuaGunLastJitterY);
            }
            m_state.regs2.WriteHCNT((x + 32u) << 2u);
            m_state.regs2.VCNTLatch = y;
        } else {
            m_state.regs2.WriteHCNT(0x3FF);
            m_state.regs2.VCNTLatch = 0x1FF;
        }
    }
}

// -----------------------------------------------------------------------------
// Debugger

void VDP::SetLayerEnabled(Layer layer, bool enabled) {
    vdp2DebugRenderOptions.enabledLayers[static_cast<size_t>(layer)] = enabled;
    m_renderer->UpdateEnabledLayers();
}

bool VDP::IsLayerEnabled(Layer layer) const {
    return vdp2DebugRenderOptions.enabledLayers[static_cast<size_t>(layer)];
}

// -----------------------------------------------------------------------------
// Probe implementation

VDP::Probe::Probe(VDP &vdp)
    : m_vdp(vdp) {}

Dimensions VDP::Probe::GetResolution() const {
    return {m_vdp.m_HRes, m_vdp.m_VRes};
}

InterlaceMode VDP::Probe::GetInterlaceMode() const {
    return m_vdp.m_state.regs2.TVMD.LSMDn;
}

uint8 VDP::Probe::GetSpriteDisplayFB() const {
    return m_vdp.m_state.fbIndex.display;
}

const VDP1Regs &VDP::Probe::GetVDP1Regs() const {
    return m_vdp.m_state.regs1;
}

const VDP2Regs &VDP::Probe::GetVDP2Regs() const {
    return m_vdp.m_state.regs2;
}

const VDP1State &VDP::Probe::GetVDP1State() const {
    return m_vdp.m_state.state1;
}

const VDP2State &VDP::Probe::GetVDP2State() const {
    return m_vdp.m_state.state2;
}

uint16 VDP::Probe::GetLatchedEraseWriteValue() const {
    return GetVDP1Regs().eraseWriteValueLatch;
}

uint16 VDP::Probe::GetLatchedEraseX1() const {
    return GetVDP1Regs().eraseX1Latch;
}

uint16 VDP::Probe::GetLatchedEraseY1() const {
    return GetVDP1Regs().eraseY1Latch;
}

uint16 VDP::Probe::GetLatchedEraseX3() const {
    return GetVDP1Regs().eraseX3Latch;
}

uint16 VDP::Probe::GetLatchedEraseY3() const {
    return GetVDP1Regs().eraseY3Latch;
}

template <mem_primitive T>
void VDP::Probe::VDP1WriteVRAM(uint32 address, T value) {
    m_vdp.VDP1WriteVRAM<T>(address, value);
}

template void VDP::Probe::VDP1WriteVRAM<uint8>(uint32, uint8);
template void VDP::Probe::VDP1WriteVRAM<uint16>(uint32, uint16);

void VDP::Probe::VDP1WriteReg(uint32 address, uint16 value) {
    m_vdp.VDP1WriteReg<true>(address, value);
}

Color555 VDP::Probe::VDP2GetCRAMColor555(uint32 index) const {
    const uint32 address = index * sizeof(uint16);
    const uint16 value = m_vdp.VDP2ReadCRAM<uint16, true>(address);
    return Color555{.u16 = value};
}

Color888 VDP::Probe::VDP2GetCRAMColor888(uint32 index) const {
    const uint32 address = index * sizeof(uint32);
    uint32 value = m_vdp.VDP2ReadCRAM<uint16, true>(address + 0) << 16u;
    value |= m_vdp.VDP2ReadCRAM<uint16, true>(address + 2) << 0u;
    return Color888{.u32 = value};
}

void VDP::Probe::VDP2SetCRAMColor555(uint32 index, Color555 color) {
    const uint32 address = index * sizeof(uint16);
    m_vdp.VDP2WriteCRAM<uint16, true>(address, color.u16);
}

void VDP::Probe::VDP2SetCRAMColor888(uint32 index, Color888 color) {
    const uint32 address = index * sizeof(uint32);
    m_vdp.VDP2WriteCRAM<uint16, true>(address + 0, color.u32 >> 16u);
    m_vdp.VDP2WriteCRAM<uint16, true>(address + 2, color.u32 >> 0u);
}

uint8 VDP::Probe::VDP2GetCRAMMode() const {
    return m_vdp.m_state.regs2.vramControl.colorRAMMode;
}

} // namespace ymir::vdp
