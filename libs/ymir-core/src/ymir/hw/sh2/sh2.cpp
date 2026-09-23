#include <ymir/hw/sh2/sh2.hpp>

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/data_ops.hpp>
#include <ymir/util/dev_assert.hpp>
#include <ymir/util/dev_log.hpp>
#include <ymir/util/unreachable.hpp>

#include <algorithm>
#include <cassert>
#include <ostream>
#include <string>
#include <string_view>

namespace ymir::sh2 {

// -----------------------------------------------------------------------------
// Dev log groups

namespace grp {

    // Hierarchy:
    //
    // base
    //   exec
    //     exec_dump
    //   intr
    //   mem
    //   reg
    //   code_fetch
    //   cache
    //   dma
    //     dma_xfer

    struct base {
        static constexpr bool enabled = true;
        static constexpr devlog::Level level = devlog::level::debug;
        static constexpr std::string Name(std::string_view prefix) {
            return std::string(prefix);
        }
    };

    struct exec : public base {
        // static constexpr bool enabled = true;
        static constexpr std::string Name(std::string_view prefix) {
            return std::string(prefix) + "-Exec";
        }
    };

    struct exec_dump : public exec {
        static constexpr bool enabled = false;
    };

    struct intr : public base {
        // static constexpr bool enabled = true;
        static constexpr std::string Name(std::string_view prefix) {
            return std::string(prefix) + "-Interrupt";
        }
    };

    struct mem : public base {
        // static constexpr bool enabled = true;
        static constexpr std::string Name(std::string_view prefix) {
            return std::string(prefix) + "-Mem";
        }
    };

    struct reg : public base {
        // static constexpr bool enabled = true;
        static constexpr std::string Name(std::string_view prefix) {
            return std::string(prefix) + "-Reg";
        }
    };

    struct code_fetch : public base {
        // static constexpr bool enabled = true;
        static constexpr std::string Name(std::string_view prefix) {
            return std::string(prefix) + "-CodeFetch";
        }
    };

    struct cache : public base {
        // static constexpr bool enabled = true;
        static constexpr std::string Name(std::string_view prefix) {
            return std::string(prefix) + "-Cache";
        }
    };

    struct dma : public base {
        // static constexpr bool enabled = true;
        static constexpr std::string Name(std::string_view prefix) {
            return std::string(prefix) + "-DMA";
        }
    };

    struct dma_xfer : public dma {
        // static constexpr bool enabled = true;
    };

} // namespace grp

// -----------------------------------------------------------------------------
// Configuration

namespace config {
    // Address of SYS_EXECDMP function.
    // 0x186C is valid in most BIOS images.
    // 0x197C on JP (v1.003).
    inline constexpr uint32 sysExecDumpAddress = 0x186C;
} // namespace config

// -----------------------------------------------------------------------------
// Debugger

FORCE_INLINE static void TraceReset(debug::ISH2Tracer *tracer, uint32 pc, uint32 sp, bool watchdogInitiated) {
    if (tracer) {
        return tracer->Reset(pc, sp, watchdogInitiated);
    }
}

template <bool debug>
FORCE_INLINE static void TraceExecuteInstruction(debug::ISH2Tracer *tracer, uint32 pc, uint16 opcode, bool delaySlot) {
    if constexpr (debug) {
        if (tracer) {
            return tracer->ExecuteInstruction(pc, opcode, delaySlot);
        }
    }
}

template <bool debug>
FORCE_INLINE static void TraceDelaySlot(debug::ISH2Tracer *tracer, uint32 pc, uint32 target) {
    if constexpr (debug) {
        if (tracer) {
            return tracer->DelaySlot(pc, target);
        }
    }
}

template <bool debug>
FORCE_INLINE static void TraceBranch(debug::ISH2Tracer *tracer, uint32 pc, uint32 target) {
    if constexpr (debug) {
        if (tracer) {
            return tracer->Branch(pc, target);
        }
    }
}

template <bool debug>
FORCE_INLINE static void TraceBranchDelay(debug::ISH2Tracer *tracer, uint32 target) {
    if constexpr (debug) {
        if (tracer) {
            return tracer->BranchDelay(target);
        }
    }
}

template <bool debug>
FORCE_INLINE static void TraceCall(debug::ISH2Tracer *tracer, uint32 target) {
    if constexpr (debug) {
        if (tracer) {
            return tracer->Call(target);
        }
    }
}

template <bool debug>
FORCE_INLINE static void TraceReturn(debug::ISH2Tracer *tracer, uint32 target) {
    if constexpr (debug) {
        if (tracer) {
            return tracer->Return(target);
        }
    }
}

template <bool debug>
FORCE_INLINE static void TraceReturnFromException(debug::ISH2Tracer *tracer, uint32 target, uint32 newSP) {
    if constexpr (debug) {
        if (tracer) {
            return tracer->ReturnFromException(target, newSP);
        }
    }
}

template <bool debug>
FORCE_INLINE static void TraceInterrupt(debug::ISH2Tracer *tracer, uint8 vecNum, uint8 level,
                                        sh2::InterruptSource source, uint32 pc) {
    if constexpr (debug) {
        if (tracer) {
            return tracer->Interrupt(vecNum, level, source, pc);
        }
    }
}

template <bool debug>
FORCE_INLINE static void TraceException(debug::ISH2Tracer *tracer, uint8 vecNum, uint32 oldPC, uint32 oldSR,
                                        uint32 oldSP, uint32 newPC) {
    if constexpr (debug) {
        if (tracer) {
            return tracer->Exception(vecNum, oldPC, oldSR, oldSP, newPC);
        }
    }
}

template <bool debug>
FORCE_INLINE static void TraceTrap(debug::ISH2Tracer *tracer, uint8 vecNum, uint32 oldPC, uint32 oldSP, uint32 newPC) {
    if constexpr (debug) {
        if (tracer) {
            return tracer->Trap(vecNum, oldPC, oldSP, newPC);
        }
    }
}

template <bool debug>
FORCE_INLINE static void TraceChangeStack(debug::ISH2Tracer *tracer, uint8 reg, uint32 newSP) {
    if constexpr (debug) {
        if (tracer && reg == 15) {
            return tracer->ChangeStack(newSP);
        }
    }
}

template <bool debug>
FORCE_INLINE static void TraceResizeStack(debug::ISH2Tracer *tracer, uint8 reg, uint32 oldSP, uint32 newSP) {
    if constexpr (debug) {
        if (tracer && reg == 15) {
            return tracer->ResizeStack(oldSP, newSP);
        }
    }
}

template <bool debug>
FORCE_INLINE static void TracePushRegisterToStack(debug::ISH2Tracer *tracer, uint8 reg, uint8 rn, uint32 oldSP,
                                                  uint32 newSP) {
    if constexpr (debug) {
        if (tracer && reg == 15) {
            return tracer->PushRegisterToStack(rn, oldSP, newSP);
        }
    }
}

template <bool debug>
FORCE_INLINE static void TracePushToStack(debug::ISH2Tracer *tracer, uint8 reg, debug::SH2StackValueType type,
                                          uint32 newSP) {
    if constexpr (debug) {
        if (tracer && reg == 15) {
            return tracer->PushToStack(type, newSP);
        }
    }
}

template <bool debug>
FORCE_INLINE static void TracePopFromStack(debug::ISH2Tracer *tracer, uint8 reg, uint32 newSP) {
    if constexpr (debug) {
        if (tracer && reg == 15) {
            return tracer->PopFromStack(newSP);
        }
    }
}

template <bool debug>
FORCE_INLINE static void TraceBegin32x32Division(debug::ISH2Tracer *tracer, sint32 dividend, sint32 divisor,
                                                 bool overflowIntrEnable) {
    if constexpr (debug) {
        if (tracer) {
            return tracer->Begin32x32Division(dividend, divisor, overflowIntrEnable);
        }
    }
}

template <bool debug>
FORCE_INLINE static void TraceBegin64x32Division(debug::ISH2Tracer *tracer, sint64 dividend, sint32 divisor,
                                                 bool overflowIntrEnable) {
    if constexpr (debug) {
        if (tracer) {
            return tracer->Begin64x32Division(dividend, divisor, overflowIntrEnable);
        }
    }
}

template <bool debug>
FORCE_INLINE static void TraceEndDivision(debug::ISH2Tracer *tracer, sint32 quotient, sint32 remainder, bool overflow) {
    if constexpr (debug) {
        if (tracer) {
            return tracer->EndDivision(quotient, remainder, overflow);
        }
    }
}

template <bool debug>
FORCE_INLINE static void TraceDMAXferBegin(debug::ISH2Tracer *tracer, uint32 channel, uint32 srcAddress,
                                           uint32 dstAddress, uint32 count, uint32 unitSize, sint32 srcInc,
                                           sint32 dstInc) {
    if constexpr (debug) {
        if (tracer) {
            return tracer->DMAXferBegin(channel, srcAddress, dstAddress, count, unitSize, srcInc, dstInc);
        }
    }
}

template <bool debug>
FORCE_INLINE static void TraceDMAXferData(debug::ISH2Tracer *tracer, uint32 channel, uint32 srcAddress,
                                          uint32 dstAddress, uint32 data, uint32 unitSize) {
    if constexpr (debug) {
        if (tracer) {
            return tracer->DMAXferData(channel, srcAddress, dstAddress, data, unitSize);
        }
    }
}

template <bool debug>
FORCE_INLINE static void TraceDMAXferEnd(debug::ISH2Tracer *tracer, uint32 channel, bool irqRaised) {
    if constexpr (debug) {
        if (tracer) {
            return tracer->DMAXferEnd(channel, irqRaised);
        }
    }
}

// -----------------------------------------------------------------------------
// Implementation

SH2::SH2(sys::SH2Bus &bus, bool master)
    : m_bus(bus)
    , m_logPrefix(master ? "SH2-M" : "SH2-S") {

    BCR1.MASTER = !master;
    Reset(true);
}

void SH2::Reset(bool hard, bool watchdogInitiated) {
    // Initial values:
    // - R0-R14 = undefined
    // - R15 = ReadLong(0x00000004)  [NOTE: ignores VBR]

    // - SR = bits I3-I0 set, reserved bits clear, the rest is undefined
    // - GBR = undefined
    // - VBR = 0x00000000

    // - MACH, MACL = undefined
    // - PR = undefined
    // - PC = ReadLong(0x00000000)  [NOTE: ignores VBR]

    // On-chip peripherals:
    // - BSC, UBC and FMR are not reset on power-on/hard reset
    // - all other modules reset always

    R.fill(0);
    PR = 0;

    MAC.u64 = 0;

    SR.u32 = 0;
    SR.ILevel = 0xF;
    GBR = 0;
    VBR = 0x00000000;

    PC = MemReadLong<false>(0x00000000);
    R[15] = MemReadLong<false>(0x00000004);

    m_wbReg = kWBRegNone;

    // On-chip registers
    BCR1.u15 = 0x03F0;
    BCR2.u16 = 0x00FC;
    WCR.u16 = 0xAAFF;
    MCR.u16 = 0x0000;
    RTCSR.u16 = 0x0000;
    RTCNT = 0x0000;
    RTCOR = 0x0000;

    DMAOR.Reset();
    for (auto &ch : m_dmaChannels) {
        ch.Reset();
    }
    m_dmacTraced.fill(false);

    SCI.Reset();

    WDT.Reset(watchdogInitiated);

    SBYCR.u8 = 0x00;
    m_sleep = false;

    DIVU.Reset();
    FRT.Reset();
    INTC.Reset();
    m_intrFlags.pending = false;
    m_intrFlags.allow = true;

    m_delaySlotTarget = 0;
    m_delaySlot = false;

    RefillPipeline<false>();

    m_cache.Reset();

    TraceReset(m_tracer, PC, R[15], watchdogInitiated);
}

void SH2::SCITransmitByte(uint8 value) {
    m_cbSCITransmit(value);
    SCI.CompleteTransmit();
    RecalcInterrupts();
}

bool SH2::SCIReceiveByte(uint8 value) {
    const bool accepted = SCI.ReceiveByte(value);
    if (SCI.SCR & SerialCommunicationInterface::kRE) {
        RecalcInterrupts();
    }
    return accepted;
}

void SH2::PollSCIReceive() {
    if (!(SCI.SCR & SerialCommunicationInterface::kRE) || (SCI.SSR & SerialCommunicationInterface::kRDRF)) {
        return;
    }
    if (const auto value = m_cbSCIReceive()) {
        SCIReceiveByte(*value);
    }
}

void SH2::MapMemory(sys::SH2Bus &bus) {
    const uint32 addressOffset = !BCR1.MASTER * 0x80'0000;

    // Map MINIT/SINIT area
    bus.MapNormal(
        0x100'0000 + addressOffset, 0x17F'FFFF + addressOffset, this,

        // Reads are prohibited
        [](uint32 address, void *) -> uint8 { return 0; }, [](uint32 address, void *) -> uint16 { return 0; },
        [](uint32 address, void *) -> uint32 { return 0; },

        // Writes trigger FRT ICI
        // - 8-bit writes only work on odd addresses
        [](uint32 address, uint8, void *ctx) {
            if (address & 1) {
                static_cast<SH2 *>(ctx)->TriggerFRTInputCapture();
            }
        },
        [](uint32 address, uint16, void *ctx) { static_cast<SH2 *>(ctx)->TriggerFRTInputCapture(); },
        [](uint32 address, uint32, void *ctx) { static_cast<SH2 *>(ctx)->TriggerFRTInputCapture(); });
}

void SH2::DumpCacheData(std::ostream &out) const {
    for (uint32 addr = 0; addr < 4096; addr += 4) {
        const uint32 value = m_cache.ReadDataArray<uint32>(addr);
        out.write((const char *)&value, sizeof(value));
    }
}

void SH2::DumpCacheAddressTag(std::ostream &out) const {
    for (uint32 addr = 0; addr < 1024; addr += 4) {
        const uint32 value = m_cache.ReadAddressArray<true>(addr);
        out.write((const char *)&value, sizeof(value));
    }
}

template <bool debug, bool emulateCache>
FLATTEN uint64 SH2::Advance(uint64 cycles, uint64 spilloverCycles) {
    m_cyclesExecuted = spilloverCycles;
    AdvanceWDT<false>();
    AdvanceFRT<false>();

    if constexpr (debug) {
        if (m_debugSuspend) {
            m_cyclesExecuted = cycles;
            return m_cyclesExecuted;
        }
    }
    // Skip interpreting instructions if CPU is in sleep or standby mode.
    // Wake up on interrupts.
    if (m_sleep) [[unlikely]] {
        if (m_intrFlags.pending) {
            m_sleep = false;
            PC += 2;
        } else {
            return cycles;
        }
    }

    while (m_cyclesExecuted < cycles) {
        // [[maybe_unused]] const uint32 prevPC = PC; // debug aid

        // TODO: choose between interpreter (cached or uncached) and JIT recompiler
        m_cyclesExecuted += InterpretNext<debug, emulateCache>();

        // If PC is not in any of these places, something went horribly wrong

        // Address bits 28 and 27 are disconnected and games generally don't use these mirrors.
        // Might not always be horribly wrong, but is highly likely to be a bad jump.
        // Unaligned addresses (bit 0 set) are a sign of potential memory corruption.
        YMIR_DEV_ASSERT((PC & 0x18000001) == 0);
        // PC should be in the cached and uncached spaces or the cache data array areas.
        // Anywhere else is highly suspicious or outright forbidden by the CPU.
        YMIR_DEV_ASSERT((PC >> 29u) == 0b000 || (PC >> 29u) == 0b001 || (PC >> 29u) == 0b100 || (PC >> 29u) == 0b101 ||
                        (PC >> 29u) == 0b110);

        // Check for breakpoints and watchpoints in debug tracing mode
        if constexpr (debug) {
            if (m_debugBreakMgr) {
                if (CheckBreakpoint()) {
                    break;
                }

                const uint16 instr = MemRead<uint16, true, true, emulateCache>(PC);
                const auto &mem = DecodeTable::s_instance.mem[instr];
                if (CheckWatchpoints(mem)) {
                    break;
                }
            }
        }

        if constexpr (devlog::debug_enabled<grp::exec_dump>) {
            // Dump stack trace on SYS_EXECDMP
            if ((PC & 0x7FFFFFF) == config::sysExecDumpAddress) {
                devlog::debug<grp::exec_dump>(m_logPrefix, "[PC = {:08X}] SYS_EXECDMP triggered", PC);
                // TODO: trace event
            }
        }
    }
    AdvanceDMA<debug, emulateCache>(m_cyclesExecuted - spilloverCycles);
    return m_cyclesExecuted;
}

template uint64 SH2::Advance<false, false>(uint64, uint64);
template uint64 SH2::Advance<false, true>(uint64, uint64);
template uint64 SH2::Advance<true, false>(uint64, uint64);
template uint64 SH2::Advance<true, true>(uint64, uint64);

template <bool debug, bool emulateCache>
FLATTEN uint64 SH2::Step() {
    m_cyclesExecuted = 0; // so that AdvanceWDT/FRT sync to the scheduler time
    AdvanceWDT<false>();
    AdvanceFRT<false>();
    m_cyclesExecuted = InterpretNext<debug, emulateCache>();
    AdvanceDMA<debug, emulateCache>(m_cyclesExecuted);
    return m_cyclesExecuted;
}

template uint64 SH2::Step<false, false>();
template uint64 SH2::Step<false, true>();
template uint64 SH2::Step<true, false>();
template uint64 SH2::Step<true, true>();

bool SH2::GetNMI() const {
    return INTC.ICR.NMIL;
}

void SH2::SetNMI() {
    // HACK: should be edge-detected
    INTC.ICR.NMIL = 1;
    INTC.NMI = true;
    RaiseInterrupt(InterruptSource::NMI);
}

void SH2::PurgeCache() {
    m_cache.Purge();
}

// -----------------------------------------------------------------------------
// Save states

void SH2::SaveState(savestate::SH2SaveState &state) const {
    state.R = R;
    state.PC = PC;
    state.PR = PR;
    state.MACL = MAC.L;
    state.MACH = MAC.H;
    state.SR = SR.u32;
    state.GBR = GBR;
    state.VBR = VBR;
    state.delaySlotTarget = m_delaySlotTarget;
    state.delaySlot = m_delaySlot;
    state.intrAllow = m_intrFlags.allow;
    state.fetchedOpcodes = m_fetchedOpcodes;
    state.forceFetchOpcodes = false;
    state.wbReg = m_wbReg;

    state.bsc.BCR1 = BCR1.u16;
    state.bsc.BCR2 = BCR2.u16;
    state.bsc.WCR = WCR.u16;
    state.bsc.MCR = MCR.u16;
    state.bsc.RTCSR = RTCSR.u16;
    state.bsc.RTCNT = RTCNT;
    state.bsc.RTCOR = RTCOR;

    state.sci.SMR = SCI.SMR;
    state.sci.BRR = SCI.BRR;
    state.sci.SCR = SCI.SCR;
    state.sci.TDR = SCI.TDR;
    state.sci.SSR = SCI.SSR;
    state.sci.RDR = SCI.RDR;
    state.sci.observedStatus = SCI.observedStatus;

    state.dmac.DMAOR = DMAOR.Read();
    m_dmaChannels[0].SaveState(state.dmac.channels[0]);
    m_dmaChannels[1].SaveState(state.dmac.channels[1]);
    WDT.SaveState(state.wdt);
    state.wdt.busValue = m_WDTBusValue;
    DIVU.SaveState(state.divu);
    FRT.SaveState(state.frt);
    INTC.SaveState(state.intc);
    m_cache.SaveState(state.cache);
    state.SBYCR = SBYCR.u8;
    state.sleep = m_sleep;
}

bool SH2::ValidateState(const savestate::SH2SaveState &state) const {
    return true;
}

void SH2::LoadState(const savestate::SH2SaveState &state) {
    R = state.R;
    PC = state.PC;
    PR = state.PR;
    MAC.L = state.MACL;
    MAC.H = state.MACH;
    SR.u32 = state.SR;
    GBR = state.GBR;
    VBR = state.VBR;
    m_delaySlotTarget = state.delaySlotTarget;
    m_delaySlot = state.delaySlot;
    m_intrFlags.allow = state.intrAllow;
    m_fetchedOpcodes = state.fetchedOpcodes;
    m_wbReg = state.wbReg;

    BCR1.u15 = state.bsc.BCR1; // Do not change the MASTER bit
    BCR2.u16 = state.bsc.BCR2;
    WCR.u16 = state.bsc.WCR;
    MCR.u16 = state.bsc.MCR;
    RTCSR.u16 = state.bsc.RTCSR;
    RTCNT = state.bsc.RTCNT;
    RTCOR = state.bsc.RTCOR;

    SCI.SMR = state.sci.SMR;
    SCI.BRR = state.sci.BRR;
    SCI.SCR = state.sci.SCR;
    SCI.TDR = state.sci.TDR;
    SCI.SSR = state.sci.SSR;
    SCI.RDR = state.sci.RDR;
    SCI.observedStatus = state.sci.observedStatus;

    DMAOR.Write<true>(state.dmac.DMAOR);
    m_dmaChannels[0].LoadState(state.dmac.channels[0]);
    m_dmaChannels[1].LoadState(state.dmac.channels[1]);
    WDT.LoadState(state.wdt);
    m_WDTBusValue = state.wdt.busValue;
    DIVU.LoadState(state.divu);
    FRT.LoadState(state.frt);
    INTC.LoadState(state.intc);
    m_cache.LoadState(state.cache);
    SBYCR.u8 = state.SBYCR;
    m_sleep = state.sleep;

    m_intrFlags.pending = !m_delaySlot && INTC.pending.level > SR.ILevel;
}

void SH2::PostLoadState(const savestate::SH2SaveState &state) {
    if (state.forceFetchOpcodes) {
        if (*m_emulateCache) {
            RefillPipeline<true>();
        } else {
            RefillPipeline<false>();
        }
    }

    if (m_tracer) {
        m_tracer->Attached();
    }
}

// -----------------------------------------------------------------------------
// Memory accessors

template <mem_primitive T, bool instrFetch, bool peek, bool emulateCache>
T SH2::MemRead(uint32 address) {
    static constexpr uint32 kAddressMask = ~(static_cast<uint32>(sizeof(T)) - 1u);

    const uint32 partition = (address >> 29u) & 0b111;
    if (address & ~kAddressMask) {
        if constexpr (!peek) {
            devlog::trace<grp::mem>(m_logPrefix, "[PC = {:08X}] WARNING: misaligned {}-bit read from {:08X}", PC,
                                    sizeof(T) * 8, address);
            // TODO: raise CPU address error due to misaligned access
            // - might have to store data in a class member instead of returning
        }
        address &= kAddressMask;
    }

    switch (partition) {
    case 0b000: // cache
        if constexpr (emulateCache) {
            if (m_cache.CCR.CE) {
                CacheEntry &entry = m_cache.GetEntry(address);
                uint32 way = entry.FindWay(address);

                if constexpr (!peek) {
                    if (!IsValidCacheWay(way)) {
                        // Cache miss
                        way = m_cache.SelectWay<instrFetch>(address);
                        if (IsValidCacheWay(way)) {
                            // Fill line
                            const uint32 baseAddress = address & ~0xF;
                            for (uint32 offset = 0; offset < 16; offset += 4) {
                                const uint32 addressInc = (address + 4 + offset) & 0xC;
                                const uint32 memValue = m_bus.Read<uint32>((baseAddress + addressInc) & 0x7FFFFFF);
                                util::WriteNE<uint32>(&entry.line[way][addressInc], memValue);
                            }
                        }
                    }
                }

                // If way is valid, fetch from cache
                if (IsValidCacheWay(way)) {
                    const uint32 byte = bit::extract<0, 3>(address) ^ (4 - sizeof(T));
                    const T value = util::ReadNE<T>(&entry.line[way][byte]);
                    if constexpr (!peek) {
                        m_cache.UpdateLRU(address, way);
                        devlog::trace<grp::cache>(m_logPrefix,
                                                  "[PC = {:08X}] {}-bit SH-2 cached area read from {:08X} = {:X} (hit)",
                                                  PC, sizeof(T) * 8, address, value);
                    }
                    return value;
                }
                if constexpr (!peek) {
                    devlog::trace<grp::cache>(m_logPrefix,
                                              "[PC = {:08X}] {}-bit SH-2 cached area read from {:08X} (miss)", PC,
                                              sizeof(T) * 8, address);
                }
            }
        }
        [[fallthrough]];
    case 0b001:
    case 0b101: // cache-through
        if constexpr (peek) {
            return m_bus.Peek<T>(address & 0x7FFFFFF);
        } else {
            return m_bus.Read<T>(address & 0x7FFFFFF);
        }
    case 0b010: // associative purge
        m_cache.AssociativePurge(address);
        devlog::trace<grp::cache>(m_logPrefix, "[PC = {:08X}] {}-bit SH-2 associative purge read from {:08X}", PC,
                                  sizeof(T) * 8, address);
        return (address & 1) ? static_cast<T>(0x12231223) : static_cast<T>(0x23122312);
    case 0b011: // cache address array
    {
        const uint32 value = m_cache.ReadAddressArray<peek>(address);
        if constexpr (!peek) {
            devlog::trace<grp::cache>(m_logPrefix,
                                      "[PC = {:08X}] {}-bit SH-2 cache address array read from {:08X} = {:X}", PC,
                                      sizeof(T) * 8, address, value);
        }
        if constexpr (std::is_same_v<T, uint8>) {
            return value >> ((~address & 1u) * 8u);
        } else {
            return value;
        }
    }
    case 0b100: [[fallthrough]];
    case 0b110: // cache data array
    {
        const T value = m_cache.ReadDataArray<T>(address);
        if constexpr (!peek) {
            devlog::trace<grp::cache>(m_logPrefix, "[PC = {:08X}] {}-bit SH-2 cache data array read from {:08X} = {:X}",
                                      PC, sizeof(T) * 8, address, value);
        }
        return value;
    }
    case 0b111: // I/O area
        if constexpr (instrFetch) {
            if constexpr (!peek) {
                // TODO: raise CPU address error due to attempt to fetch instruction from I/O area
                devlog::trace<grp::code_fetch>(
                    m_logPrefix, "[PC = {:08X}] Attempted to fetch instruction from I/O area at {:08X}", PC, address);
            }
            return 0;
        } else if ((address & 0xE0004000) == 0xE0004000) {
            // bits 31-29 and 14 must be set
            // bits 8-0 index the register
            // bits 28 and 12 must be both set to access the lower half of the registers
            if ((address & 0x100) || (address & 0x10001000) == 0x10001000) {
                return OnChipRegRead<T, peek>(address & 0x1FF);
            } else {
                return OpenBusSeqRead<T>(address);
            }
        } else {
            // TODO: implement
            if constexpr (!peek) {
                devlog::trace<grp::reg>(m_logPrefix, "[PC = {:08X}] Unhandled {}-bit SH-2 I/O area read from {:08X}",
                                        PC, sizeof(T) * 8, address);
            }
            return 0;
        }
    }

    util::unreachable();
}

template <mem_primitive T, bool poke, bool debug, bool emulateCache>
void SH2::MemWrite(uint32 address, T value) {
    static constexpr uint32 kAddressMask = ~(static_cast<uint32>(sizeof(T)) - 1u);

    const uint32 partition = address >> 29u;
    if (address & ~kAddressMask) {
        if constexpr (!poke) {
            devlog::trace<grp::mem>(m_logPrefix, "[PC = {:08X}] WARNING: misaligned {}-bit write to {:08X} = {:X}", PC,
                                    sizeof(T) * 8, address, value);
            // TODO: address error (misaligned access)
        }
        address &= kAddressMask;
    }

    switch (partition) {
    case 0b000: // cache
        if constexpr (emulateCache) {
            if (m_cache.CCR.CE) {
                auto &entry = m_cache.GetEntry(address);
                const uint8 way = entry.FindWay(address);
                if (IsValidCacheWay(way)) {
                    const uint32 byte = bit::extract<0, 3>(address) ^ (4 - sizeof(T));
                    util::WriteNE<T>(&entry.line[way][byte], value);
                    if constexpr (!poke) {
                        m_cache.UpdateLRU(address, way);
                    }
                }
            }
        }
        [[fallthrough]];
    case 0b001:
    case 0b101: // cache-through
        if constexpr (poke) {
            m_bus.Poke<T>(address & 0x7FFFFFF, value);
        } else {
            m_bus.Write<T>(address & 0x7FFFFFF, value);
        }
        break;
    case 0b010: // associative purge
        m_cache.AssociativePurge(address);
        if constexpr (!poke) {
            devlog::trace<grp::cache>(m_logPrefix, "[PC = {:08X}] {}-bit SH-2 associative purge write to {:08X} = {:X}",
                                      PC, sizeof(T) * 8, address, value);
        }
        break;
    case 0b011: // cache address array
        if constexpr (std::is_same_v<T, uint8>) {
            const uint16 value16 = (value << 8u) | value;
            m_cache.WriteAddressArray<uint16, poke>(address, value16);
        } else {
            m_cache.WriteAddressArray<T, poke>(address, value);
        }
        if constexpr (!poke) {
            devlog::trace<grp::cache>(m_logPrefix,
                                      "[PC = {:08X}] {}-bit SH-2 cache address array write to {:08X} = {:X}", PC,
                                      sizeof(T) * 8, address, value);
        }
        break;
    case 0b100:
    case 0b110: // cache data array
        m_cache.WriteDataArray<T>(address, value);
        if constexpr (!poke) {
            devlog::trace<grp::cache>(m_logPrefix, "[PC = {:08X}] {}-bit SH-2 cache data array write to {:08X} = {:X}",
                                      PC, sizeof(T) * 8, address, value);
        }
        break;
    case 0b111: // I/O area
        if ((address & 0xE0004000) == 0xE0004000) {
            // bits 31-29 and 14 must be set
            // bits 8-0 index the register
            // bits 28 and 12 must be both set to access the lower half of the registers
            if ((address & 0x100) || (address & 0x10001000) == 0x10001000) {
                OnChipRegWrite<T, poke, debug, emulateCache>(address & 0x1FF, value);
            }
        } else if ((address >> 12u) == 0xFFFF8) {
            // DRAM setup stuff
            if constexpr (!poke) {
                switch (address) {
                case 0xFFFF8426: devlog::trace<grp::reg>(m_logPrefix, "[PC = {:08X}] 16-bit CAS latency 1", PC); break;
                case 0xFFFF8446: devlog::trace<grp::reg>(m_logPrefix, "[PC = {:08X}] 16-bit CAS latency 2", PC); break;
                case 0xFFFF8466: devlog::trace<grp::reg>(m_logPrefix, "[PC = {:08X}] 16-bit CAS latency 3", PC); break;
                case 0xFFFF8848: devlog::trace<grp::reg>(m_logPrefix, "[PC = {:08X}] 32-bit CAS latency 1", PC); break;
                case 0xFFFF8888: devlog::trace<grp::reg>(m_logPrefix, "[PC = {:08X}] 32-bit CAS latency 2", PC); break;
                case 0xFFFF88C8: devlog::trace<grp::reg>(m_logPrefix, "[PC = {:08X}] 32-bit CAS latency 3", PC); break;
                default:
                    devlog::debug<grp::reg>(m_logPrefix,
                                            "[PC = {:08X}] Unhandled {}-bit SH-2 I/O area write to {:08X} = {:X}", PC,
                                            sizeof(T) * 8, address, value);
                    break;
                }
            }
        } else {
            // TODO: implement
            if constexpr (!poke) {
                devlog::trace<grp::reg>(m_logPrefix,
                                        "[PC = {:08X}] Unhandled {}-bit SH-2 I/O area write to {:08X} = {:X}", PC,
                                        sizeof(T) * 8, address, value);
            }
        }
        break;
    }
}

template <bool emulateCache>
FLATTEN FORCE_INLINE uint16 SH2::FetchInstruction(uint32 address) {
    const uint32 index = bit::extract<1>(address);
    if (index == 0) {
        RefillPipeline<emulateCache>();
        return m_fetchedOpcodes >> 16u;
    }
    return m_fetchedOpcodes;
}

template <bool emulateCache>
FLATTEN FORCE_INLINE void SH2::RefillPipeline() {
    m_fetchedOpcodes = MemRead<uint32, true, false, emulateCache>(PC);
}

template <bool emulateCache>
FLATTEN FORCE_INLINE uint8 SH2::MemReadByte(uint32 address) {
    return MemRead<uint8, false, false, emulateCache>(address);
}

template <bool emulateCache, bool instrFetch>
FLATTEN FORCE_INLINE uint16 SH2::MemReadWord(uint32 address) {
    return MemRead<uint16, instrFetch, false, emulateCache>(address);
}

template <bool emulateCache, bool instrFetch>
FLATTEN FORCE_INLINE uint32 SH2::MemReadLong(uint32 address) {
    return MemRead<uint32, instrFetch, false, emulateCache>(address);
}

template <bool debug, bool emulateCache>
FLATTEN FORCE_INLINE void SH2::MemWriteByte(uint32 address, uint8 value) {
    MemWrite<uint8, false, debug, emulateCache>(address, value);
}

template <bool debug, bool emulateCache>
FLATTEN FORCE_INLINE void SH2::MemWriteWord(uint32 address, uint16 value) {
    MemWrite<uint16, false, debug, emulateCache>(address, value);
}

template <bool debug, bool emulateCache>
FLATTEN FORCE_INLINE void SH2::MemWriteLong(uint32 address, uint32 value) {
    MemWrite<uint32, false, debug, emulateCache>(address, value);
}

template <bool emulateCache>
FLATTEN_EX FORCE_INLINE_EX uint16 SH2::PeekInstruction(uint32 address) {
    return MemRead<uint16, true, true, emulateCache>(address);
}

template <bool emulateCache>
FLATTEN_EX FORCE_INLINE_EX uint8 SH2::MemPeekByte(uint32 address) {
    return MemRead<uint8, false, true, emulateCache>(address);
}

template <bool emulateCache>
FLATTEN_EX FORCE_INLINE_EX uint16 SH2::MemPeekWord(uint32 address) {
    return MemRead<uint16, false, true, emulateCache>(address);
}

template <bool emulateCache>
FLATTEN_EX FORCE_INLINE_EX uint32 SH2::MemPeekLong(uint32 address) {
    return MemRead<uint32, false, true, emulateCache>(address);
}

template <bool emulateCache>
FLATTEN_EX FORCE_INLINE_EX void SH2::MemPokeByte(uint32 address, uint8 value) {
    MemWrite<uint8, true, false, emulateCache>(address, value);
}

template <bool emulateCache>
FLATTEN_EX FORCE_INLINE_EX void SH2::MemPokeWord(uint32 address, uint16 value) {
    MemWrite<uint16, true, false, emulateCache>(address, value);
}

template <bool emulateCache>
FLATTEN_EX FORCE_INLINE_EX void SH2::MemPokeLong(uint32 address, uint32 value) {
    MemWrite<uint32, true, false, emulateCache>(address, value);
}

template <mem_primitive T>
/*FLATTEN_EX FORCE_INLINE_EX*/ T SH2::OpenBusSeqRead(uint32 address) {
    if constexpr (std::is_same_v<T, uint8>) {
        return (address & 1u) * ((address >> 1u) & 0x7);
        // return OpenBusSeqRead<uint16>(address) >> (((address & 1) ^ 1) * 8);
    } else if constexpr (std::is_same_v<T, uint16>) {
        return (address >> 1u) & 0x7;
    } else if constexpr (std::is_same_v<T, uint32>) {
        return (OpenBusSeqRead<uint16>(address + 1) << 16u) | OpenBusSeqRead<uint16>(address);
    }
    util::unreachable();
}

template <mem_primitive T, bool write, bool emulateCache>
FORCE_INLINE_EX uint64 SH2::AccessCycles(uint32 address) {
    const uint32 partition = (address >> 29u) & 0b111;
    switch (partition) {
    case 0b000: // cache
        if constexpr (emulateCache && !write) {
            // Check for cache hit
            CacheEntry &entry = m_cache.GetEntry(address);
            uint32 way = entry.FindWay(address);

            if (IsValidCacheWay(way)) {
                return 1;
            } else {
                // Cache miss - fill cache line
                // TODO: stall bus for 4 accesses
                return m_bus.GetAccessCycles<T, write>(address);
            }
        } else if constexpr (!emulateCache) {
            // Simplified model - assume cache hits on all accesses to cached area
            return 1;
        }
        [[fallthrough]];
    case 0b001: [[fallthrough]];
    case 0b101: // cache-through
        return m_bus.GetAccessCycles<T, write>(address);
    case 0b010: return 1;        // associative purge
    case 0b011: return 1;        // cache address array
    case 0b100: [[fallthrough]]; // cache data array
    case 0b110: return 1;        // cache data array
    case 0b111: return 4;        // I/O area
    }

    util::unreachable();
}

template <bool emulateCache>
uint64 SH2::AccessCyclesRMWByte(uint32 address) {
    const uint32 partition = (address >> 29u) & 0b111;
    switch (partition) {
    case 0b000:                  // cache
    case 0b001: [[fallthrough]]; // cache-through
    case 0b101:                  // cache-through
    {
        const uint64 readCycles = m_bus.GetAccessCycles<uint8, false>(address);
        return readCycles - 1;
    }
    default: // everything else
        return AccessCycles<uint8, false, emulateCache>(address) + AccessCycles<uint8, true, emulateCache>(address);
    }
}

// -----------------------------------------------------------------------------
// On-chip peripherals

template <mem_primitive T, bool peek>
/*FLATTEN_EX FORCE_INLINE_EX*/ T SH2::OnChipRegRead(uint32 address) {
    // Misaligned memory accesses raise an address error, therefore:
    //   (address & 3) == 2 is only valid for 16-bit accesses
    //   (address & 1) == 1 is only valid for 8-bit accesses
    // Additionally:
    //   (address & 1) == 0 has special cases for registers 0-255:
    //     8-bit read from a 16-bit register:  r >> 8u
    //     16-bit read from a 8-bit register: (r << 8u) | r
    //     Every other access returns just r

    if constexpr (std::is_same_v<T, uint32>) {
        return OnChipRegReadLong<peek>(address);
    } else if constexpr (std::is_same_v<T, uint16>) {
        return OnChipRegReadWord<peek>(address);
    } else if constexpr (std::is_same_v<T, uint8>) {
        return OnChipRegReadByte<peek>(address);
    }
}

template <bool peek>
FORCE_INLINE_EX uint8 SH2::OnChipRegReadByte(uint32 address) {
    if (address >= 0x100) {
        if constexpr (peek) {
            const uint16 value = OnChipRegReadWord<true>(address & ~1);
            return value >> ((~address & 1) * 8u);
        } else {
            // Registers 0x100-0x1FF do not accept 8-bit accesses
            // TODO: raise CPU address error
            devlog::debug<grp::reg>(m_logPrefix, "[PC = {:08X}] Illegal 8-bit on-chip register read from {:03X}", PC,
                                    address);
            return 0;
        }
    }

    switch (address) {
    case 0x00: return SCI.SMR;
    case 0x01: return SCI.BRR;
    case 0x02: return SCI.SCR;
    case 0x03: return SCI.TDR;
    case 0x04:
        if constexpr (!peek) {
            PollSCIReceive();
        }
        return SCI.ReadSSR<peek>();
    case 0x05:
        if constexpr (!peek) {
            PollSCIReceive();
        }
        return SCI.RDR;
    case 0x10: return FRT.ReadTIER();
    case 0x11:
        if constexpr (!peek) {
            AdvanceFRT<false>();
        }
        return FRT.ReadFTCSR<peek>();
    case 0x12:
        if constexpr (!peek) {
            AdvanceFRT<false>();
        }
        return FRT.ReadFRCH<peek>();
    case 0x13: return FRT.ReadFRCL<peek>();
    case 0x14: return FRT.ReadOCRH();
    case 0x15: return FRT.ReadOCRL();
    case 0x16: return FRT.ReadTCR();
    case 0x17: return FRT.ReadTOCR();
    case 0x18: return FRT.ReadICRH<peek>();
    case 0x19: return FRT.ReadICRL<peek>();

    case 0x60: return (INTC.GetLevel(InterruptSource::SCI_ERI) << 4u) | INTC.GetLevel(InterruptSource::FRT_ICI);
    case 0x61: return 0;
    case 0x62: return INTC.GetVector(InterruptSource::SCI_ERI);
    case 0x63: return INTC.GetVector(InterruptSource::SCI_RXI);
    case 0x64: return INTC.GetVector(InterruptSource::SCI_TXI);
    case 0x65: return INTC.GetVector(InterruptSource::SCI_TEI);
    case 0x66: return INTC.GetVector(InterruptSource::FRT_ICI);
    case 0x67: return INTC.GetVector(InterruptSource::FRT_OCI);
    case 0x68: return INTC.GetVector(InterruptSource::FRT_OVI);
    case 0x69: return 0;

    case 0x71: return m_dmaChannels[0].ReadDRCR();
    case 0x72: return m_dmaChannels[1].ReadDRCR();

    case 0x80: [[fallthrough]];
    case 0x88:
        if constexpr (peek) {
            return WDT.ReadWTCSR<peek>();
        } else {
            AdvanceWDT<false>();
            return m_WDTBusValue = WDT.ReadWTCSR<peek>();
        }

    case 0x81: [[fallthrough]];
    case 0x89:
        if constexpr (peek) {
            return WDT.ReadWTCNT();
        } else {
            AdvanceWDT<false>();
            return m_WDTBusValue = WDT.ReadWTCNT();
        }

    case 0x83: [[fallthrough]];
    case 0x8B:
        if constexpr (peek) {
            return WDT.ReadRSTCSR();
        } else {
            AdvanceWDT<false>();
            return m_WDTBusValue = WDT.ReadRSTCSR();
        }

    case 0x82: [[fallthrough]];
    case 0x85: [[fallthrough]];
    case 0x86: [[fallthrough]];
    case 0x87: [[fallthrough]];
    case 0x8A: [[fallthrough]];
    case 0x8D: [[fallthrough]];
    case 0x8E: [[fallthrough]];
    case 0x8F: return 0xFF;
    case 0x84: [[fallthrough]];
    case 0x8C: return m_WDTBusValue;

    case 0x91: return SBYCR.u8;

    case 0x92: [[fallthrough]];
    case 0x93: [[fallthrough]];
    case 0x94: [[fallthrough]];
    case 0x95: [[fallthrough]];
    case 0x96: [[fallthrough]];
    case 0x97: [[fallthrough]];
    case 0x98: [[fallthrough]];
    case 0x99: [[fallthrough]];
    case 0x9A: [[fallthrough]];
    case 0x9B: [[fallthrough]];
    case 0x9C: [[fallthrough]];
    case 0x9D: [[fallthrough]];
    case 0x9E: [[fallthrough]];
    case 0x9F: return m_cache.ReadCCR();

    case 0xE0: return OnChipRegReadWord<peek>(address) >> 8u;
    case 0xE1: return OnChipRegReadWord<peek>(address & ~1) >> 0u;
    case 0xE2: return (INTC.GetLevel(InterruptSource::DIVU_OVFI) << 4u) | INTC.GetLevel(InterruptSource::DMAC0_XferEnd);
    case 0xE3: return INTC.GetLevel(InterruptSource::WDT_ITI) << 4u;
    case 0xE4: return INTC.GetVector(InterruptSource::WDT_ITI);
    case 0xE5: return INTC.GetVector(InterruptSource::BSC_REF_CMI);

    default: //
        if constexpr (!peek) {
            devlog::debug<grp::reg>(m_logPrefix, "[PC = {:08X}] Unhandled 8-bit on-chip register read from {:03X}", PC,
                                    address);
        }
        return 0;
    }
}

template <bool peek>
FORCE_INLINE_EX uint16 SH2::OnChipRegReadWord(uint32 address) {
    if (address < 0x100) {
        switch (address) {
        case 0x82: [[fallthrough]];
        case 0x85: [[fallthrough]];
        case 0x86: [[fallthrough]];
        case 0x87: [[fallthrough]];
        case 0x8A: [[fallthrough]];
        case 0x8D: [[fallthrough]];
        case 0x8E: [[fallthrough]];
        case 0x8F:
            if constexpr (!peek) {
                m_WDTBusValue = 0xFF;
            }
            return 0xFFFF;
        case 0x84: [[fallthrough]];
        case 0x8C: return (m_WDTBusValue << 8u) | m_WDTBusValue;

        case 0xE0: return INTC.ReadICR();
        }
        if constexpr (peek) {
            uint16 value = OnChipRegReadByte<peek>(address + 0) << 8u;
            value |= OnChipRegReadByte<peek>(address + 1) << 0u;
            return value;
        } else {
            const uint16 value = OnChipRegReadByte<peek>(address);
            return (value << 8u) | value;
        }
    } else {
        return OnChipRegReadLong<peek>(address & ~3);
    }
}

template <bool peek>
FORCE_INLINE_EX uint32 SH2::OnChipRegReadLong(uint32 address) {
    if (address < 0x100) {
        if constexpr (peek) {
            uint32 value = OnChipRegReadWord<true>(address & ~3) << 16u;
            value |= OnChipRegReadWord<true>((address & ~3) | 2) << 0u;
            return value;
        } else {
            // Registers 0x000-0x0FF do not accept 32-bit accesses
            // TODO: raise CPU address error
            devlog::debug<grp::reg>(m_logPrefix, "[PC = {:08X}] Illegal 32-bit on-chip register read from {:03X}", PC,
                                    address);
            return 0;
        }
    }

    switch (address) {
    case 0x100: [[fallthrough]];
    case 0x120: return DIVU.DVSR;

    case 0x104: [[fallthrough]];
    case 0x124: return DIVU.DVDNT;

    case 0x108: [[fallthrough]];
    case 0x128: return DIVU.DVCR.Read();

    case 0x10C: [[fallthrough]];
    case 0x12C: return DIVU.VCRDIV;

    case 0x110: [[fallthrough]];
    case 0x130: return DIVU.DVDNTH;

    case 0x114: [[fallthrough]];
    case 0x134: return DIVU.DVDNTL;

    case 0x118: [[fallthrough]];
    case 0x138: return DIVU.DVDNTUH;

    case 0x11C: [[fallthrough]];
    case 0x13C: return DIVU.DVDNTUL;

    case 0x180: return m_dmaChannels[0].srcAddress;
    case 0x184: return m_dmaChannels[0].dstAddress;
    case 0x188: return m_dmaChannels[0].xferCount;
    case 0x18C: return m_dmaChannels[0].ReadCHCR();

    case 0x190: return m_dmaChannels[1].srcAddress;
    case 0x194: return m_dmaChannels[1].dstAddress;
    case 0x198: return m_dmaChannels[1].xferCount;
    case 0x19C: return m_dmaChannels[1].ReadCHCR();

    case 0x1A0: return INTC.GetVector(InterruptSource::DMAC0_XferEnd);
    case 0x1A8: return INTC.GetVector(InterruptSource::DMAC1_XferEnd);

    case 0x1B0: return DMAOR.Read();

    case 0x1E0: return BCR1.u16;
    case 0x1E4: return BCR2.u16;
    case 0x1E8: return WCR.u16;
    case 0x1EC: return MCR.u16;
    case 0x1F0: return RTCSR.u16;
    case 0x1F4: return RTCNT;
    case 0x1F8: return RTCOR;

    default: //
        if constexpr (!peek) {
            devlog::debug<grp::reg>(m_logPrefix, "[PC = {:08X}] Unhandled 32-bit on-chip register read from {:03X}", PC,
                                    address);
        }
        return 0;
    }
}

template <mem_primitive T, bool poke, bool debug, bool emulateCache>
/*FLATTEN_EX FORCE_INLINE_EX*/ void SH2::OnChipRegWrite(uint32 address, T value) {
    // Misaligned memory accesses raise an address error, therefore:
    //   (address & 3) == 2 is only valid for 16-bit accesses
    //   (address & 1) == 1 is only valid for 8-bit accesses
    if constexpr (std::is_same_v<T, uint32>) {
        OnChipRegWriteLong<poke, debug, emulateCache>(address, value);
    } else if constexpr (std::is_same_v<T, uint16>) {
        OnChipRegWriteWord<poke, debug, emulateCache>(address, value);
    } else if constexpr (std::is_same_v<T, uint8>) {
        OnChipRegWriteByte<poke, debug, emulateCache>(address, value);
    }
}

template <bool poke, bool debug, bool emulateCache>
FORCE_INLINE_EX void SH2::OnChipRegWriteByte(uint32 address, uint8 value) {
    if (address >= 0x100) {
        if constexpr (poke) {
            uint16 currValue = OnChipRegReadWord<true>(address & ~1);
            const uint16 shift = (~address & 1) & 8u;
            const uint16 mask = ~(0xFF << shift);
            currValue = (currValue & mask) | (value << shift);
            OnChipRegWriteWord<true, debug, emulateCache>(address & ~1, currValue);
        } else {
            // Registers 0x100-0x1FF do not accept 8-bit accesses
            // TODO: raise CPU address error
            devlog::debug<grp::reg>(m_logPrefix, "[PC = {:08X}] Illegal 8-bit on-chip register write to {:03X} = {:X}",
                                    PC, address, value);
        }
        return;
    }

    if constexpr (poke) {
        switch (address) {
        case 0x80: [[fallthrough]];
        case 0x88: WDT.WriteWTCSR<poke>(value); break;

        case 0x81: [[fallthrough]];
        case 0x89: WDT.WriteWTCNT(value); break;

        case 0x83: [[fallthrough]];
        case 0x8B: WDT.WriteRSTCSR<poke>(value); break;

        case 0x93: [[fallthrough]];
        case 0x94: [[fallthrough]];
        case 0x95: [[fallthrough]];
        case 0x96: [[fallthrough]];
        case 0x97: [[fallthrough]];
        case 0x98: [[fallthrough]];
        case 0x99: [[fallthrough]];
        case 0x9A: [[fallthrough]];
        case 0x9B: [[fallthrough]];
        case 0x9C: [[fallthrough]];
        case 0x9D: [[fallthrough]];
        case 0x9E: [[fallthrough]];
        case 0x9F: m_cache.WriteCCR<poke>(value); break;
        }
    }

    switch (address) {
    case 0x00: SCI.SMR = value; break;
    case 0x01: SCI.BRR = value; break;
    case 0x02:
        SCI.WriteSCR(value);
        RecalcInterrupts();
        break;
    case 0x03: SCI.TDR = value; break;
    case 0x04:
        if (SCI.WriteSSR<poke>(value)) {
            SCITransmitByte(SCI.TDR);
        }
        RecalcInterrupts();
        break;
    case 0x05: break; // RDR is read-only
    case 0x10:
        FRT.WriteTIER(value);
        if (FRT.FTCSR.ICF && FRT.TIER.ICIE) {
            RaiseInterrupt(InterruptSource::FRT_ICI);
        } else if ((FRT.FTCSR.OCFA && FRT.TIER.OCIAE) || (FRT.FTCSR.OCFB && FRT.TIER.OCIBE)) {
            RaiseInterrupt(InterruptSource::FRT_OCI);
        } else if (FRT.FTCSR.OVF && FRT.TIER.OVIE) {
            RaiseInterrupt(InterruptSource::FRT_OVI);
        } else if (INTC.pending.source == InterruptSource::FRT_OVI || INTC.pending.source == InterruptSource::FRT_OCI ||
                   INTC.pending.source == InterruptSource::FRT_ICI) {
            RecalcInterrupts();
        }
        break;
    case 0x11:
        if constexpr (!poke) {
            AdvanceFRT<true>();
        }
        FRT.WriteFTCSR<poke>(value);
        if (INTC.pending.source == InterruptSource::FRT_OVI || INTC.pending.source == InterruptSource::FRT_OCI ||
            INTC.pending.source == InterruptSource::FRT_ICI) {
            RecalcInterrupts();
        }
        break;
    case 0x12: FRT.WriteFRCH<poke>(value); break;
    case 0x13:
        if constexpr (!poke) {
            AdvanceFRT<true>();
        }
        FRT.WriteFRCL<poke>(value);
        break;
    case 0x14: FRT.WriteOCRH<poke>(value); break;
    case 0x15: FRT.WriteOCRL<poke>(value); break;
    case 0x16:
        if constexpr (!poke) {
            AdvanceFRT<true>();
        }
        FRT.WriteTCR(value);
        break;
    case 0x17: FRT.WriteTOCR(value); break;
    case 0x18: FRT.WriteICRH<poke>(value); break; // ICRH is read-only
    case 0x19: FRT.WriteICRL<poke>(value); break; // ICRL is read-only

    case 0x60: //
    {
        const uint8 frtIntrLevel = bit::extract<0, 3>(value);
        const uint8 sciIntrLevel = bit::extract<4, 7>(value);

        using enum InterruptSource;
        INTC.SetLevel(FRT_ICI, frtIntrLevel);
        INTC.SetLevel(FRT_OCI, frtIntrLevel);
        INTC.SetLevel(FRT_OVI, frtIntrLevel);
        INTC.SetLevel(SCI_ERI, sciIntrLevel);
        INTC.SetLevel(SCI_RXI, sciIntrLevel);
        INTC.SetLevel(SCI_TXI, sciIntrLevel);
        INTC.SetLevel(SCI_TEI, sciIntrLevel);
        if (frtIntrLevel > 0) {
            if (FRT.FTCSR.ICF && FRT.TIER.ICIE) {
                RaiseInterrupt(InterruptSource::FRT_ICI);
            }
            if ((FRT.FTCSR.OCFA && FRT.TIER.OCIAE) || (FRT.FTCSR.OCFB && FRT.TIER.OCIBE)) {
                RaiseInterrupt(InterruptSource::FRT_OCI);
            }
            if (FRT.FTCSR.OVF && FRT.TIER.OVIE) {
                RaiseInterrupt(InterruptSource::FRT_OVI);
            }
        } else {
            LowerInterrupt(InterruptSource::FRT_ICI);
            LowerInterrupt(InterruptSource::FRT_OCI);
            LowerInterrupt(InterruptSource::FRT_OVI);
        }
        RecalcInterrupts();
        break;
    }
    case 0x61: /* IPRB bits 7-0 are all reserved */ break;
    case 0x62: INTC.SetVector(InterruptSource::SCI_ERI, bit::extract<0, 6>(value)); break;
    case 0x63: INTC.SetVector(InterruptSource::SCI_RXI, bit::extract<0, 6>(value)); break;
    case 0x64: INTC.SetVector(InterruptSource::SCI_TXI, bit::extract<0, 6>(value)); break;
    case 0x65: INTC.SetVector(InterruptSource::SCI_TEI, bit::extract<0, 6>(value)); break;
    case 0x66: INTC.SetVector(InterruptSource::FRT_ICI, bit::extract<0, 6>(value)); break;
    case 0x67: INTC.SetVector(InterruptSource::FRT_OCI, bit::extract<0, 6>(value)); break;
    case 0x68: INTC.SetVector(InterruptSource::FRT_OVI, bit::extract<0, 6>(value)); break;
    case 0x69: /* VCRD bits 7-0 are all reserved */ break;

    case 0x71: m_dmaChannels[0].WriteDRCR(value); break;
    case 0x72: m_dmaChannels[1].WriteDRCR(value); break;

    case 0x80: [[fallthrough]]; // WDT registers only accept 16-bit writes
    case 0x81: [[fallthrough]];
    case 0x82: [[fallthrough]];
    case 0x83: [[fallthrough]];
    case 0x84: [[fallthrough]];
    case 0x85: [[fallthrough]];
    case 0x86: [[fallthrough]];
    case 0x87: [[fallthrough]];
    case 0x88: [[fallthrough]];
    case 0x89: [[fallthrough]];
    case 0x8A: [[fallthrough]];
    case 0x8B: [[fallthrough]];
    case 0x8C: [[fallthrough]];
    case 0x8D: [[fallthrough]];
    case 0x8E: [[fallthrough]];
    case 0x8F:
        if constexpr (!poke) {
            m_WDTBusValue = value;
        }
        break;

    case 0x91: SBYCR.u8 = value & 0xDF; break;
    case 0x92: m_cache.WriteCCR<poke>(value); break;

    case 0xE0: INTC.WriteICR<false, true, poke>(value << 8u); break;
    case 0xE1: INTC.WriteICR<true, false, poke>(value); break;
    case 0xE2: //
    {
        const uint8 dmacIntrLevel = bit::extract<0, 3>(value);
        const uint8 divuIntrLevel = bit::extract<4, 7>(value);

        using enum InterruptSource;
        INTC.SetLevel(DMAC0_XferEnd, dmacIntrLevel);
        INTC.SetLevel(DMAC1_XferEnd, dmacIntrLevel);
        INTC.SetLevel(DIVU_OVFI, divuIntrLevel);
        if (INTC.GetLevel(InterruptSource::DIVU_OVFI) > 0 && DIVU.DVCR.OVF && DIVU.DVCR.OVFIE) {
            RaiseInterrupt(InterruptSource::DIVU_OVFI);
        } else {
            LowerInterrupt(InterruptSource::DIVU_OVFI);
        }
        if (INTC.GetLevel(InterruptSource::DMAC0_XferEnd) > 0 && m_dmaChannels[0].xferEnded &&
            m_dmaChannels[0].irqEnable) {
            RaiseInterrupt(InterruptSource::DMAC0_XferEnd);
        } else {
            LowerInterrupt(InterruptSource::DMAC0_XferEnd);
        }
        if (INTC.GetLevel(InterruptSource::DMAC1_XferEnd) > 0 && m_dmaChannels[1].xferEnded &&
            m_dmaChannels[1].irqEnable) {
            RaiseInterrupt(InterruptSource::DMAC1_XferEnd);
        } else {
            LowerInterrupt(InterruptSource::DMAC1_XferEnd);
        }
        break;
    }
    case 0xE3: //
    {
        const uint8 wdtIntrLevel = bit::extract<4, 7>(value);

        using enum InterruptSource;
        INTC.SetLevel(WDT_ITI, wdtIntrLevel);
        if (wdtIntrLevel > 0) {
            // Watchdog timer
            if (WDT.WTCSR.OVF && !WDT.WTCSR.WT_nIT) {
                RaiseInterrupt(InterruptSource::WDT_ITI);
            } else {
                LowerInterrupt(InterruptSource::WDT_ITI);
            }

            // TODO: BSC REF CMI
            /*if (...) {
                RaiseInterrupt(InterruptSource::BSC_REF_CMI);
            } else {
                LowerInterrupt(InterruptSource::BSC_REF_CMI);
            }*/
        } else {
            LowerInterrupt(InterruptSource::WDT_ITI);
            // LowerInterrupt(InterruptSource::BSC_REF_CMI);
        }
        break;
    }
    case 0xE4: INTC.SetVector(InterruptSource::WDT_ITI, bit::extract<0, 6>(value)); break;
    case 0xE5: INTC.SetVector(InterruptSource::BSC_REF_CMI, bit::extract<0, 6>(value)); break;

    default: //
        if constexpr (!poke) {
            devlog::debug<grp::reg>(m_logPrefix,
                                    "[PC = {:08X}] Unhandled 8-bit on-chip register write to {:03X} = {:X}", PC,
                                    address, value);
        }
        break;
    }
}

template <bool poke, bool debug, bool emulateCache>
FORCE_INLINE_EX void SH2::OnChipRegWriteWord(uint32 address, uint16 value) {
    switch (address) {
    case 0x60:
    case 0x61:
    case 0x62:
    case 0x63:
    case 0x64:
    case 0x65:
    case 0x66:
    case 0x67:
    case 0x68:
    case 0x69:

    case 0xE2:
    case 0xE3:
    case 0xE4:
    case 0xE5:
        OnChipRegWriteByte<poke, debug, emulateCache>(address & ~1, value >> 8u);
        OnChipRegWriteByte<poke, debug, emulateCache>(address | 1, value >> 0u);
        break;

    case 0x80: [[fallthrough]];
    case 0x88:
        if constexpr (!poke) {
            m_WDTBusValue = value;
        }
        if ((value >> 8u) == 0x5A) {
            if constexpr (!poke) {
                AdvanceWDT<true>();
            }
            WDT.WriteWTCNT(value);
        } else if ((value >> 8u) == 0xA5) {
            if constexpr (!poke) {
                AdvanceWDT<true>();
            }
            WDT.WriteWTCSR<poke>(value);
            if (!WDT.WTCSR.TME || !WDT.WTCSR.OVF) {
                LowerInterrupt(InterruptSource::WDT_ITI);
            }
        }
        break;

    case 0x82: [[fallthrough]];
    case 0x8A:
        if constexpr (!poke) {
            m_WDTBusValue = value;
        }
        if ((value >> 8u) == 0x5A) {
            WDT.WriteRSTE_RSTS(value);
        } else if ((value >> 8u) == 0xA5) {
            WDT.WriteWOVF<poke>(value);
        }
        break;

    case 0x81: [[fallthrough]];
    case 0x83: [[fallthrough]];
    case 0x85: [[fallthrough]];
    case 0x86: [[fallthrough]];
    case 0x87: [[fallthrough]];
    case 0x89: [[fallthrough]];
    case 0x8B: [[fallthrough]];
    case 0x8D: [[fallthrough]];
    case 0x8E: [[fallthrough]];
    case 0x8F:
        if constexpr (!poke) {
            m_WDTBusValue = value;
        }
        break;
    case 0x84: [[fallthrough]];
    case 0x8C:
        if constexpr (!poke) {
            m_WDTBusValue = value >> 8u;
        }
        break;

    case 0x92: m_cache.WriteCCR<poke>(value); break;

    case 0xE0: INTC.WriteICR<true, true, poke>(value); break;

    case 0x108:
    case 0x10C:

    case 0x1E0:
    case 0x1E4:
    case 0x1E8:
    case 0x1EC:
    case 0x1F0:
    case 0x1F4:
    case 0x1F8: //
        OnChipRegWriteLong<poke, debug, emulateCache>(address & ~3, value);
        break;

    default: //
        if constexpr (!poke) {
            devlog::debug<grp::reg>(m_logPrefix, "[PC = {:08X}] Illegal 16-bit on-chip register write to {:03X} = {:X}",
                                    PC, address, value);
        }
        break;
    }
}

template <bool poke, bool debug, bool emulateCache>
FORCE_INLINE_EX void SH2::OnChipRegWriteLong(uint32 address, uint32 value) {
    if (address < 0x100) {
        if constexpr (poke) {
            OnChipRegWriteWord<true, debug, emulateCache>(address + 0, value >> 16u);
            OnChipRegWriteWord<true, debug, emulateCache>(address + 2, value >> 0u);
        } else {
            // Registers 0x000-0x0FF do not accept 32-bit accesses
            // TODO: raise CPU address error
            devlog::debug<grp::reg>(m_logPrefix, "[PC = {:08X}] Illegal 32-bit on-chip register write to {:03X} = {:X}",
                                    PC, address, value);
        }
        return;
    }

    switch (address) {
    case 0x100:
    case 0x120: DIVU.DVSR = value; break;

    case 0x104:
    case 0x124:
        DIVU.DVDNT = value;
        if constexpr (!poke) {
            ExecuteDiv32<debug>();
        }
        break;

    case 0x108:
    case 0x128:
        DIVU.DVCR.Write(value);
        RecalcInterrupts();
        break;

    case 0x10C:
    case 0x12C:
        INTC.SetVector(InterruptSource::DIVU_OVFI, bit::extract<0, 6>(value));
        DIVU.VCRDIV = value;
        break;

    case 0x110:
    case 0x130: DIVU.DVDNTH = value; break;

    case 0x114:
    case 0x134:
        DIVU.DVDNTL = value;
        if constexpr (!poke) {
            ExecuteDiv64<debug>();
        }
        break;

    case 0x118:
    case 0x138: DIVU.DVDNTUH = value; break;

    case 0x11C:
    case 0x13C: DIVU.DVDNTUL = value; break;

    case 0x180: m_dmaChannels[0].srcAddress = value; break;
    case 0x184: m_dmaChannels[0].dstAddress = value; break;
    case 0x188: m_dmaChannels[0].xferCount = bit::extract<0, 23>(value); break;
    case 0x18C:
        m_dmaChannels[0].WriteCHCR<poke>(value);
        if constexpr (!poke) {
            if (m_dmaChannels[0].xferEnded && m_dmaChannels[0].irqEnable) {
                RaiseInterrupt(InterruptSource::DMAC0_XferEnd);
            } else {
                LowerInterrupt(InterruptSource::DMAC0_XferEnd);
            }
        }
        break;

    case 0x190: m_dmaChannels[1].srcAddress = value; break;
    case 0x194: m_dmaChannels[1].dstAddress = value; break;
    case 0x198: m_dmaChannels[1].xferCount = bit::extract<0, 23>(value); break;
    case 0x19C:
        m_dmaChannels[1].WriteCHCR<poke>(value);
        if constexpr (!poke) {
            if (m_dmaChannels[1].xferEnded && m_dmaChannels[1].irqEnable) {
                RaiseInterrupt(InterruptSource::DMAC1_XferEnd);
            } else {
                LowerInterrupt(InterruptSource::DMAC1_XferEnd);
            }
        }
        break;

    case 0x1A0: INTC.SetVector(InterruptSource::DMAC0_XferEnd, bit::extract<0, 6>(value)); break;
    case 0x1A8: INTC.SetVector(InterruptSource::DMAC1_XferEnd, bit::extract<0, 6>(value)); break;

    case 0x1B0:
        DMAOR.Write<poke>(value);
        if constexpr (!poke) {
            if (m_dmaChannels[0].xferEnded && m_dmaChannels[0].irqEnable) {
                RaiseInterrupt(InterruptSource::DMAC0_XferEnd);
            } else {
                LowerInterrupt(InterruptSource::DMAC0_XferEnd);
            }
            if (m_dmaChannels[1].xferEnded && m_dmaChannels[1].irqEnable) {
                RaiseInterrupt(InterruptSource::DMAC1_XferEnd);
            } else {
                LowerInterrupt(InterruptSource::DMAC1_XferEnd);
            }
        }
        break;

    case 0x1E0: // BCR1
        if ((value >> 16u) == 0xA55A) {
            BCR1.u15 = value & 0x1FF7;
        }
        break;
    case 0x1E4: // BCR2
        if ((value >> 16u) == 0xA55A) {
            BCR2.u16 = value & 0xFC;
        }
        break;
    case 0x1E8: // WCR
        if ((value >> 16u) == 0xA55A) {
            WCR.u16 = value;
        }
        break;
    case 0x1EC: // MCR
        if ((value >> 16u) == 0xA55A) {
            MCR.u16 = value & 0xFEFC;
        }
        break;
    case 0x1F0: // RTCSR
        if ((value >> 16u) == 0xA55A) {
            // TODO: implement the set/clear rules for RTCSR.CMF
            RTCSR.u16 = (value & 0x78) | (RTCSR.u16 & 0x80);
        }
        break;
    case 0x1F4: // RTCNT
        if ((value >> 16u) == 0xA55A) {
            RTCNT = value;
        }
        break;
    case 0x1F8: // RTCOR
        if ((value >> 16u) == 0xA55A) {
            RTCOR = value;
        }
        break;
    default: //
        if constexpr (!poke) {
            devlog::debug<grp::reg>(m_logPrefix,
                                    "[PC = {:08X}] Unhandled 32-bit on-chip register write to {:03X} = {:X}", PC,
                                    address, value);
        }
        break;
    }
}

FORCE_INLINE uint64 SH2::GetCurrentCycleCount() const {
    return *m_currCount + m_cyclesExecuted;
}

template <std::integral... Ts>
FORCE_INLINE uint64 SH2::WritebackCycles(Ts... regs) {
    return ((regs == m_wbReg) || ...) ? 1 : 0;
}

FLATTEN FORCE_INLINE bool SH2::IsDMATransferActive(const DMAChannel &ch) const {
    // AE never occurs and NMIF is never set, so both checks can be safely skipped
    return ch.IsEnabled() && DMAOR.DME /*&& !DMAOR.NMIF && !DMAOR.AE*/;
}

template <bool debug, bool emulateCache>
FORCE_INLINE bool SH2::StepDMAC(uint32 channel) {
    auto &ch = m_dmaChannels[channel];

    // TODO: prioritize channels based on DMAOR.PR
    // TODO: proper timings, cycle-stealing, etc. (suspend instructions if not cached)

    if (!IsDMATransferActive(ch)) {
        return false;
    }

    // Auto request mode will start the transfer right now.
    // Module request mode checks if the signal from the configured source has been raised.
    if (!ch.autoRequest) {
        switch (ch.resSelect) {
        case DMAResourceSelect::DREQ: /*TODO*/ return false;
        case DMAResourceSelect::RXI:
            if (!SCI.CanDMAReceive()) {
                return false;
            }
            break;
        case DMAResourceSelect::TXI:
            if (!SCI.CanDMATransmit()) {
                return false;
            }
            break;
        case DMAResourceSelect::Reserved: return false;
        }
    }

    static constexpr uint32 kXferSize[] = {1, 2, 4, 16};
    const uint32 xferSize = kXferSize[static_cast<uint32>(ch.xferSize)];
    auto getAddressInc = [&](DMATransferIncrementMode mode) -> sint32 {
        using enum DMATransferIncrementMode;
        switch (mode) {
        default: [[fallthrough]];
        case Fixed: return 0;
        case Increment: return +xferSize;
        case Decrement: return -xferSize;
        case Reserved: return 0;
        }
    };

    if (m_bus.IsBusWait(ch.srcAddress, xferSize, false)) {
        devlog::trace<grp::dma_xfer>(m_logPrefix, "DMAC{} transfer from {:08X} stalled by bus wait signal", channel,
                                     ch.srcAddress);
        return false;
    }
    if (m_bus.IsBusWait(ch.dstAddress, xferSize, true)) {
        devlog::trace<grp::dma_xfer>(m_logPrefix, "DMAC{} transfer to {:08X} stalled by bus wait signal", channel,
                                     ch.dstAddress);
        return false;
    }

    const sint32 srcInc = getAddressInc(ch.srcMode);
    const sint32 dstInc = getAddressInc(ch.dstMode);

    if constexpr (debug) {
        if (!m_dmacTraced[channel]) {
            m_dmacTraced[channel] = true;
            TraceDMAXferBegin<debug>(m_tracer, channel, ch.srcAddress, ch.dstAddress, ch.xferCount, xferSize, srcInc,
                                     dstInc);
        }
    }

    // Perform one unit of transfer
    switch (ch.xferSize) {
    case DMATransferSize::Byte: {
        const uint8 value = MemReadByte<emulateCache>(ch.srcAddress);
        devlog::trace<grp::dma_xfer>(m_logPrefix, "DMAC{} 8-bit transfer from {:08X} to {:08X} -> {:X}", channel,
                                     ch.srcAddress, ch.dstAddress, value);
        MemWriteByte<debug, emulateCache>(ch.dstAddress, value);
        if (!ch.autoRequest && ch.resSelect == DMAResourceSelect::RXI && ch.srcAddress == 0xFFFF'FE05u) {
            SCI.SSR &= ~SerialCommunicationInterface::kRDRF;
            SCI.observedStatus &= ~SerialCommunicationInterface::kRDRF;
            RecalcInterrupts();
        }
        // A TXI module request clears TDRE as part of the DMA write to TDR.
        if (!ch.autoRequest && ch.resSelect == DMAResourceSelect::TXI &&
            ch.dstAddress == 0xFFFF'FE03u) {
            SCITransmitByte(value);
        }
        TraceDMAXferData<debug>(m_tracer, channel, ch.srcAddress, ch.dstAddress, value, xferSize);
        break;
    }
    case DMATransferSize::Word: {
        const uint16 value = MemReadWord<emulateCache>(ch.srcAddress);
        devlog::trace<grp::dma_xfer>(m_logPrefix, "DMAC{} 16-bit transfer from {:08X} to {:08X} -> {:X}", channel,
                                     ch.srcAddress, ch.dstAddress, value);
        MemWriteWord<debug, emulateCache>(ch.dstAddress, value);
        TraceDMAXferData<debug>(m_tracer, channel, ch.srcAddress, ch.dstAddress, value, xferSize);
        break;
    }
    case DMATransferSize::Longword: {
        const uint32 value = MemReadLong<emulateCache>(ch.srcAddress);
        devlog::trace<grp::dma_xfer>(m_logPrefix, "DMAC{} 32-bit transfer from {:08X} to {:08X} -> {:X}", channel,
                                     ch.srcAddress, ch.dstAddress, value);
        MemWriteLong<debug, emulateCache>(ch.dstAddress, value);
        TraceDMAXferData<debug>(m_tracer, channel, ch.srcAddress, ch.dstAddress, value, xferSize);
        break;
    }
    case DMATransferSize::QuadLongword: {
        std::array<uint32, 4> line{};
        for (int i = 0; i < 4; i++) {
            const uint32 value = MemReadLong<emulateCache>(ch.srcAddress + i * sizeof(uint32));
            line[i] = value;
            devlog::trace<grp::dma_xfer>(m_logPrefix, "DMAC{} 16-byte transfer {:d} from {:08X} to {:08X} -> {:X}",
                                         channel, i, ch.srcAddress, ch.dstAddress, value);
        }
        for (int i = 0; i < 4; i++) {
            const uint32 value = line[i];
            MemWriteLong<debug, emulateCache>(ch.dstAddress + i * sizeof(uint32), value);
            TraceDMAXferData<debug>(m_tracer, channel, ch.srcAddress, ch.dstAddress, value, 4);
        }
        break;
    }
    }

    // Update address and remaining count
    ch.srcAddress += srcInc;
    ch.dstAddress += dstInc;

    if (ch.xferSize == DMATransferSize::QuadLongword) {
        if (ch.xferCount >= 4) {
            ch.xferCount -= 4;
        } else {
            devlog::trace<grp::dma>(m_logPrefix, "DMAC{} 16-byte transfer count misaligned", channel);
            ch.xferCount = 0;
        }
    } else {
        --ch.xferCount;
    }

    if (ch.xferCount == 0) {
        TraceDMAXferEnd<debug>(m_tracer, channel, ch.irqEnable);
        if constexpr (debug) {
            m_dmacTraced[channel] = false;
        }

        ch.xferEnded = true;
        devlog::trace<grp::dma>(m_logPrefix, "DMAC{} transfer finished", channel);
        if (ch.irqEnable) {
            switch (channel) {
            case 0: RaiseInterrupt(InterruptSource::DMAC0_XferEnd); break;
            case 1: RaiseInterrupt(InterruptSource::DMAC1_XferEnd); break;
            }
        }
        return false;
    }

    return true;
}

template <bool debug, bool emulateCache>
FORCE_INLINE void SH2::AdvanceDMA(uint64 cycles) {
    PollSCIReceive();
    for (uint32 i = 0; i < 2; ++i) {
        // HACK: run full transfers to fix sprite glitches in Golden Axe - The Duel
        while (StepDMAC<debug, emulateCache>(i)) {
        }
        /*for (uint64 c = 0; c < cycles; ++c) {
            if (!StepDMAC<debug, emulateCache>(i)) {
                break;
            }
        }*/
    }
}

template <bool write>
FORCE_INLINE void SH2::AdvanceWDT() {
    const uint64 cycles = GetCurrentCycleCount() + (write ? 4 : 0);

    switch (WDT.AdvanceTo(cycles)) {
    case WatchdogTimer::Event::None: break;
    case WatchdogTimer::Event::Reset: Reset(WDT.RSTCSR.RSTS, true); break;
    case WatchdogTimer::Event::RaiseInterrupt: RaiseInterrupt(InterruptSource::WDT_ITI); break;
    }
}

template <bool debug>
FORCE_INLINE void SH2::ExecuteDiv32() {
    DIVU.DVDNTL = DIVU.DVDNT;
    DIVU.DVDNTH = static_cast<sint32>(DIVU.DVDNT) >> 31;
    TraceBegin32x32Division<debug>(m_tracer, DIVU.DVDNTL, DIVU.DVSR, DIVU.DVCR.OVFIE);
    DIVU.Calc32();
    TraceEndDivision<debug>(m_tracer, DIVU.DVDNTL, DIVU.DVDNTH, DIVU.DVCR.OVF);
    if (DIVU.DVCR.OVF && DIVU.DVCR.OVFIE) {
        RaiseInterrupt(InterruptSource::DIVU_OVFI);
    }
}

template <bool debug>
FORCE_INLINE void SH2::ExecuteDiv64() {
    TraceBegin64x32Division<debug>(m_tracer,
                                   (static_cast<sint64>(DIVU.DVDNTH) << 32ll) | static_cast<sint64>(DIVU.DVDNTL),
                                   DIVU.DVSR, DIVU.DVCR.OVFIE);
    DIVU.Calc64();
    TraceEndDivision<debug>(m_tracer, DIVU.DVDNTL, DIVU.DVDNTH, DIVU.DVCR.OVF);
    if (DIVU.DVCR.OVF && DIVU.DVCR.OVFIE) {
        RaiseInterrupt(InterruptSource::DIVU_OVFI);
    }
}

template <bool write>
FORCE_INLINE void SH2::AdvanceFRT() {
    const uint64 cycles = GetCurrentCycleCount() + (write ? 4 : 0);

    switch (FRT.AdvanceTo(cycles)) {
    case FreeRunningTimer::Event::None: break;
    case FreeRunningTimer::Event::OCI: RaiseInterrupt(InterruptSource::FRT_OCI); break;
    case FreeRunningTimer::Event::OVI: RaiseInterrupt(InterruptSource::FRT_OVI); break;
    }
}

FORCE_INLINE void SH2::TriggerFRTInputCapture() {
    // TODO: FRT.TCR.IEDGA
    FRT.ICR = FRT.FRC;
    FRT.FTCSR.ICF = 1;
    if (FRT.TIER.ICIE) {
        RaiseInterrupt(InterruptSource::FRT_ICI);
    }
}

// -----------------------------------------------------------------------------
// Interrupts

FORCE_INLINE void SH2::SetExternalInterrupt(uint8 level, uint8 vector) {
    assert(level < 16);

    static constexpr InterruptSource source = InterruptSource::IRL;

    INTC.externalVector = vector;
    INTC.SetLevel(source, level);

    if (level > 0) {
        INTC.UpdateIRLVector();
        RaiseInterrupt(source);
        devlog::trace<grp::exec>(m_logPrefix, "Set IRL vector/level to {:02X}/{:X}; pending level {:X}", vector, level,
                                 INTC.pending.level);
    } else {
        INTC.SetVector(source, 0);
        LowerInterrupt(source);
    }
}

void SH2::RecalcInterrupts() {
    // Check interrupts and use the vector number of the exception with highest priority
    // See documentation for InterruptSource for related registers and default/tie-breaker priority order

    // HACK: should be edge-detected
    if (INTC.NMI) [[unlikely]] {
        INTC.pending.level = 0x10;
        INTC.pending.source = InterruptSource::NMI;
        m_intrFlags.pending = !m_delaySlot;
        return;
    }

    INTC.pending.level = 0;
    INTC.pending.source = InterruptSource::None;
    m_intrFlags.pending = false;

    // TODO: user break
    /*if (...) {
        RaiseInterrupt(InterruptSource::UserBreak);
        return;
    }*/

    // IRLs
    RaiseInterrupt(InterruptSource::IRL);

    // Division overflow
    RaiseInterruptIf(InterruptSource::DIVU_OVFI, [this] { return DIVU.DVCR.OVF && DIVU.DVCR.OVFIE; });

    // DMA channel transfer end
    const uint8 levelDMAC = INTC.GetLevel(InterruptSource::DMAC0_XferEnd);
    RaiseInterruptIf(InterruptSource::DMAC0_XferEnd, levelDMAC,
                     [this] { return m_dmaChannels[0].xferEnded && m_dmaChannels[0].irqEnable; });
    RaiseInterruptIf(InterruptSource::DMAC1_XferEnd, levelDMAC,
                     [this] { return m_dmaChannels[1].xferEnded && m_dmaChannels[1].irqEnable; });

    // Watchdog timer
    const uint8 levelWDT_BSC = INTC.GetLevel(InterruptSource::WDT_ITI);
    RaiseInterruptIf(InterruptSource::WDT_ITI, levelWDT_BSC, [this] { return WDT.WTCSR.OVF && !WDT.WTCSR.WT_nIT; });

    // TODO: BSC REF CMI
    // RaiseInterruptIf(InterruptSource::BSC_REF_CMI, levelWDT_BSC, [this] { return ...; });

    const uint8 levelSCI = INTC.GetLevel(InterruptSource::SCI_ERI);
    RaiseInterruptIf(InterruptSource::SCI_ERI, levelSCI, [this] {
        return (SCI.SCR & SerialCommunicationInterface::kRIE) && (SCI.SSR & 0x38);
    });
    RaiseInterruptIf(InterruptSource::SCI_RXI, levelSCI, [this] {
        return (SCI.SCR & SerialCommunicationInterface::kRIE) && (SCI.SSR & SerialCommunicationInterface::kRDRF);
    });
    RaiseInterruptIf(InterruptSource::SCI_TXI, levelSCI, [this] {
        return (SCI.SCR & SerialCommunicationInterface::kTIE) && (SCI.SSR & SerialCommunicationInterface::kTDRE);
    });
    RaiseInterruptIf(InterruptSource::SCI_TEI, levelSCI, [this] {
        return (SCI.SCR & SerialCommunicationInterface::kTEIE) && (SCI.SSR & SerialCommunicationInterface::kTEND);
    });

    // Free-running timer interrupts
    const uint8 levelFRT = INTC.GetLevel(InterruptSource::FRT_ICI);
    RaiseInterruptIf(InterruptSource::FRT_ICI, levelFRT, [this] { return FRT.FTCSR.ICF && FRT.TIER.ICIE; });
    RaiseInterruptIf(InterruptSource::FRT_OCI, levelFRT,
                     [this] { return (FRT.FTCSR.OCFA && FRT.TIER.OCIAE) || (FRT.FTCSR.OCFB && FRT.TIER.OCIBE); });
    RaiseInterruptIf(InterruptSource::FRT_OVI, levelFRT, [this] { return FRT.FTCSR.OVF && FRT.TIER.OVIE; });
}

// -------------------------------------------------------------------------
// Debugger

FORCE_INLINE bool SH2::CheckBreakpoint() {
    if (IsBreakpointSetInBitmap(PC)) {
        m_debugBreakMgr->SignalDebugBreak(debug::DebugBreakInfo::SH2Breakpoint(IsMaster(), PC));
        return true;
    }
    return false;
}

FORCE_INLINE bool SH2::CheckWatchpoints(const DecodedMemAccesses &mem) {
    if (!mem.anyAccess) {
        return false;
    }
    const bool wtpt1 = CheckWatchpoint(mem.first);
    const bool wtpt2 = CheckWatchpoint(mem.second);
    return wtpt1 || wtpt2;
}

FORCE_INLINE bool SH2::CheckWatchpoint(const DecodedMemAccesses::Access &access) {
    uint32 address;

    using AccType = DecodedMemAccesses::Type;
    switch (access.type) {
    case AccType::None: return false;
    case AccType::AtReg: address = R[access.reg]; break;
    case AccType::AtR0Reg: address = R[0] + R[access.reg]; break;
    case AccType::AtR0GBR: address = R[0] + GBR; break;
    case AccType::AtDispReg: address = access.disp + R[access.reg]; break;
    case AccType::AtDispGBR: address = access.disp + GBR; break;
    case AccType::AtDispPC: address = (PC & ~(access.size - 1)) + access.disp; break;
    }

    static constexpr auto kReadMask8 = static_cast<uint8>(debug::WatchpointFlags::Read);
    static constexpr auto kWriteMask8 = static_cast<uint8>(debug::WatchpointFlags::Write);
    static constexpr auto kReadMask16 = (kReadMask8 << 8u) | kReadMask8;
    static constexpr auto kWriteMask16 = (kWriteMask8 << 8u) | kWriteMask8;
    static constexpr auto kReadMask32 = (kReadMask16 << 16u) | kReadMask16;
    static constexpr auto kWriteMask32 = (kWriteMask16 << 16u) | kWriteMask16;

    uint8 mask;
    switch (access.size) {
    case 1: //
    {
        const auto match = GetWatchpointFlags<uint8>(address) & (access.write ? kWriteMask8 : kReadMask8);
        if (match == 0) {
            return false;
        }
        mask = access.write ? bit::gather<kWriteMask8>(match) : bit::gather<kReadMask8>(match);
        break;
    }
    case 2: //
    {
        const auto match = GetWatchpointFlags<uint16>(address) & (access.write ? kWriteMask16 : kReadMask16);
        if (match == 0) {
            return false;
        }
        mask = access.write ? bit::gather<kWriteMask16>(match) : bit::gather<kReadMask16>(match);
        break;
    }
    case 4: //
    {
        const auto match = GetWatchpointFlags<uint32>(address) & (access.write ? kWriteMask32 : kReadMask32);
        if (match == 0) {
            return false;
        }
        mask = access.write ? bit::gather<kWriteMask32>(match) : bit::gather<kReadMask32>(match);
        break;
    }
    default: return false; // should never happen
    }

    m_debugBreakMgr->SignalDebugBreak(
        debug::DebugBreakInfo::SH2Watchpoint(IsMaster(), access.write, access.size, address, PC, mask));
    return true;
}

// -------------------------------------------------------------------------
// Helper functions

FORCE_INLINE_EX void SH2::SetupDelaySlot(uint32 targetAddress) {
    m_delaySlot = true;
    m_delaySlotTarget = targetAddress;
    m_intrFlags.pending = false;
}

template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE_EX void SH2::AdvancePC() {
    if constexpr (delaySlot) {
        TraceDelaySlot<debug>(m_tracer, PC, m_delaySlotTarget);
        PC = m_delaySlotTarget;
        if (PC & 2) {
            RefillPipeline<emulateCache>();
        }
        m_delaySlot = false;
        m_intrFlags.pending = INTC.pending.level > SR.ILevel;
    } else {
        PC += 2;
    }
}

template <bool debug, bool emulateCache>
FORCE_INLINE uint64 SH2::EnterException(uint8 vectorNumber) {
    const uint32 address1 = R[15] - 4;
    const uint32 address2 = R[15] - 8;
    const uint32 address3 = VBR + (static_cast<uint32>(vectorNumber) << 2u);
    const uint64 cycles = AccessCycles<uint32, true, emulateCache>(address1) +
                          AccessCycles<uint32, true, emulateCache>(address2) +
                          AccessCycles<uint32, false, emulateCache>(address3) + 5;
    MemWriteLong<debug, emulateCache>(address1, SR.u32);
    MemWriteLong<debug, emulateCache>(address2, PC);
    const uint32 target = MemReadLong<emulateCache>(address3);
    TraceException<debug>(m_tracer, vectorNumber, PC, SR.u32, R[15], target);
    PC = target;
    if (PC & 2) {
        RefillPipeline<emulateCache>();
    }
    R[15] -= 8;
    m_delaySlot = false;
    return cycles;
}

// -----------------------------------------------------------------------------
// Instruction interpreters

template <bool debug, bool emulateCache>
FORCE_INLINE_EX uint64 SH2::InterpretNext() {
    if (std::bit_cast<uint16>(m_intrFlags) == kIntrFlagsPendingAllowed) [[unlikely]] {
        // Service interrupt
        const uint8 vecNum = INTC.GetVector(INTC.pending.source);
        TraceInterrupt<debug>(m_tracer, vecNum, INTC.pending.level, INTC.pending.source, PC);
        devlog::trace<grp::intr>(m_logPrefix, "[PC = {:08X}] Handling interrupt level {:02X}, vector number {:02X}", PC,
                                 INTC.pending.level, vecNum);
        const uint64 cycles = EnterException<debug, emulateCache>(vecNum);
        devlog::trace<grp::intr>(m_logPrefix, "[PC = {:08X}] Entering interrupt handler", PC);
        SR.ILevel = std::min<uint8>(INTC.pending.level, 0xF);
        m_intrFlags.pending = false;

        // Acknowledge interrupt
        switch (INTC.pending.source) {
        case InterruptSource::IRL:
            if (INTC.ICR.VECMD) {
                m_cbAcknowledgeExternalInterrupt();
            }
            break;
        case InterruptSource::NMI:
            INTC.NMI = false;
            LowerInterrupt(InterruptSource::NMI);
            break;
        default: break;
        }
        return cycles + 1;
    }
    m_intrFlags.allow = true;

    // TODO: emulate or approximate fetch - decode - execute - memory access - writeback pipeline

    const uint32 pc = PC;
    const uint16 instr = FetchInstruction<emulateCache>(pc);
    TraceExecuteInstruction<debug>(m_tracer, pc, instr, m_delaySlot);

    const OpcodeType opcode = DecodeTable::s_instance.opcodes[m_delaySlot][instr];

    // TODO: check program execution
    switch (opcode) {
    case OpcodeType::NOP: return NOP<debug, emulateCache, false>();

    case OpcodeType::SLEEP: return SLEEP();

    case OpcodeType::MOV_R: return MOV<debug, emulateCache, false>(instr);
    case OpcodeType::MOVB_L: return MOVBL<debug, emulateCache, false>(instr);
    case OpcodeType::MOVW_L: return MOVWL<debug, emulateCache, false>(instr);
    case OpcodeType::MOVL_L: return MOVLL<debug, emulateCache, false>(instr);
    case OpcodeType::MOVB_L0: return MOVBL0<debug, emulateCache, false>(instr);
    case OpcodeType::MOVW_L0: return MOVWL0<debug, emulateCache, false>(instr);
    case OpcodeType::MOVL_L0: return MOVLL0<debug, emulateCache, false>(instr);
    case OpcodeType::MOVB_L4: return MOVBL4<debug, emulateCache, false>(instr);
    case OpcodeType::MOVW_L4: return MOVWL4<debug, emulateCache, false>(instr);
    case OpcodeType::MOVL_L4: return MOVLL4<debug, emulateCache, false>(instr);
    case OpcodeType::MOVB_LG: return MOVBLG<debug, emulateCache, false>(instr);
    case OpcodeType::MOVW_LG: return MOVWLG<debug, emulateCache, false>(instr);
    case OpcodeType::MOVL_LG: return MOVLLG<debug, emulateCache, false>(instr);
    case OpcodeType::MOVB_M: return MOVBM<debug, emulateCache, false>(instr);
    case OpcodeType::MOVW_M: return MOVWM<debug, emulateCache, false>(instr);
    case OpcodeType::MOVL_M: return MOVLM<debug, emulateCache, false>(instr);
    case OpcodeType::MOVB_P: return MOVBP<debug, emulateCache, false>(instr);
    case OpcodeType::MOVW_P: return MOVWP<debug, emulateCache, false>(instr);
    case OpcodeType::MOVL_P: return MOVLP<debug, emulateCache, false>(instr);
    case OpcodeType::MOVB_S: return MOVBS<debug, emulateCache, false>(instr);
    case OpcodeType::MOVW_S: return MOVWS<debug, emulateCache, false>(instr);
    case OpcodeType::MOVL_S: return MOVLS<debug, emulateCache, false>(instr);
    case OpcodeType::MOVB_S0: return MOVBS0<debug, emulateCache, false>(instr);
    case OpcodeType::MOVW_S0: return MOVWS0<debug, emulateCache, false>(instr);
    case OpcodeType::MOVL_S0: return MOVLS0<debug, emulateCache, false>(instr);
    case OpcodeType::MOVB_S4: return MOVBS4<debug, emulateCache, false>(instr);
    case OpcodeType::MOVW_S4: return MOVWS4<debug, emulateCache, false>(instr);
    case OpcodeType::MOVL_S4: return MOVLS4<debug, emulateCache, false>(instr);
    case OpcodeType::MOVB_SG: return MOVBSG<debug, emulateCache, false>(instr);
    case OpcodeType::MOVW_SG: return MOVWSG<debug, emulateCache, false>(instr);
    case OpcodeType::MOVL_SG: return MOVLSG<debug, emulateCache, false>(instr);
    case OpcodeType::MOV_I: return MOVI<debug, emulateCache, false>(instr);
    case OpcodeType::MOVW_I: return MOVWI<debug, emulateCache, false>(instr);
    case OpcodeType::MOVL_I: return MOVLI<debug, emulateCache, false>(instr);
    case OpcodeType::MOVA: return MOVA<debug, emulateCache, false>(instr);
    case OpcodeType::MOVT: return MOVT<debug, emulateCache, false>(instr);
    case OpcodeType::CLRT: return CLRT<debug, emulateCache, false>();
    case OpcodeType::SETT: return SETT<debug, emulateCache, false>();

    case OpcodeType::EXTUB: return EXTUB<debug, emulateCache, false>(instr);
    case OpcodeType::EXTUW: return EXTUW<debug, emulateCache, false>(instr);
    case OpcodeType::EXTSB: return EXTSB<debug, emulateCache, false>(instr);
    case OpcodeType::EXTSW: return EXTSW<debug, emulateCache, false>(instr);
    case OpcodeType::SWAPB: return SWAPB<debug, emulateCache, false>(instr);
    case OpcodeType::SWAPW: return SWAPW<debug, emulateCache, false>(instr);
    case OpcodeType::XTRCT: return XTRCT<debug, emulateCache, false>(instr);

    case OpcodeType::LDC_GBR_R: return LDCGBR<debug, emulateCache, false>(instr);
    case OpcodeType::LDC_SR_R: return LDCSR<debug, emulateCache, false>(instr);
    case OpcodeType::LDC_VBR_R: return LDCVBR<debug, emulateCache, false>(instr);
    case OpcodeType::LDS_MACH_R: return LDSMACH<debug, emulateCache, false>(instr);
    case OpcodeType::LDS_MACL_R: return LDSMACL<debug, emulateCache, false>(instr);
    case OpcodeType::LDS_PR_R: return LDSPR<debug, emulateCache, false>(instr);
    case OpcodeType::STC_GBR_R: return STCGBR<debug, emulateCache, false>(instr);
    case OpcodeType::STC_SR_R: return STCSR<debug, emulateCache, false>(instr);
    case OpcodeType::STC_VBR_R: return STCVBR<debug, emulateCache, false>(instr);
    case OpcodeType::STS_MACH_R: return STSMACH<debug, emulateCache, false>(instr);
    case OpcodeType::STS_MACL_R: return STSMACL<debug, emulateCache, false>(instr);
    case OpcodeType::STS_PR_R: return STSPR<debug, emulateCache, false>(instr);
    case OpcodeType::LDC_GBR_M: return LDCMGBR<debug, emulateCache, false>(instr);
    case OpcodeType::LDC_SR_M: return LDCMSR<debug, emulateCache, false>(instr);
    case OpcodeType::LDC_VBR_M: return LDCMVBR<debug, emulateCache, false>(instr);
    case OpcodeType::LDS_MACH_M: return LDSMMACH<debug, emulateCache, false>(instr);
    case OpcodeType::LDS_MACL_M: return LDSMMACL<debug, emulateCache, false>(instr);
    case OpcodeType::LDS_PR_M: return LDSMPR<debug, emulateCache, false>(instr);
    case OpcodeType::STC_GBR_M: return STCMGBR<debug, emulateCache, false>(instr);
    case OpcodeType::STC_SR_M: return STCMSR<debug, emulateCache, false>(instr);
    case OpcodeType::STC_VBR_M: return STCMVBR<debug, emulateCache, false>(instr);
    case OpcodeType::STS_MACH_M: return STSMMACH<debug, emulateCache, false>(instr);
    case OpcodeType::STS_MACL_M: return STSMMACL<debug, emulateCache, false>(instr);
    case OpcodeType::STS_PR_M: return STSMPR<debug, emulateCache, false>(instr);

    case OpcodeType::ADD: return ADD<debug, emulateCache, false>(instr);
    case OpcodeType::ADD_I: return ADDI<debug, emulateCache, false>(instr);
    case OpcodeType::ADDC: return ADDC<debug, emulateCache, false>(instr);
    case OpcodeType::ADDV: return ADDV<debug, emulateCache, false>(instr);
    case OpcodeType::AND_R: return AND<debug, emulateCache, false>(instr);
    case OpcodeType::AND_I: return ANDI<debug, emulateCache, false>(instr);
    case OpcodeType::AND_M: return ANDM<debug, emulateCache, false>(instr);
    case OpcodeType::NEG: return NEG<debug, emulateCache, false>(instr);
    case OpcodeType::NEGC: return NEGC<debug, emulateCache, false>(instr);
    case OpcodeType::NOT: return NOT<debug, emulateCache, false>(instr);
    case OpcodeType::OR_R: return OR<debug, emulateCache, false>(instr);
    case OpcodeType::OR_I: return ORI<debug, emulateCache, false>(instr);
    case OpcodeType::OR_M: return ORM<debug, emulateCache, false>(instr);
    case OpcodeType::ROTCL: return ROTCL<debug, emulateCache, false>(instr);
    case OpcodeType::ROTCR: return ROTCR<debug, emulateCache, false>(instr);
    case OpcodeType::ROTL: return ROTL<debug, emulateCache, false>(instr);
    case OpcodeType::ROTR: return ROTR<debug, emulateCache, false>(instr);
    case OpcodeType::SHAL: return SHAL<debug, emulateCache, false>(instr);
    case OpcodeType::SHAR: return SHAR<debug, emulateCache, false>(instr);
    case OpcodeType::SHLL: return SHLL<debug, emulateCache, false>(instr);
    case OpcodeType::SHLL2: return SHLL2<debug, emulateCache, false>(instr);
    case OpcodeType::SHLL8: return SHLL8<debug, emulateCache, false>(instr);
    case OpcodeType::SHLL16: return SHLL16<debug, emulateCache, false>(instr);
    case OpcodeType::SHLR: return SHLR<debug, emulateCache, false>(instr);
    case OpcodeType::SHLR2: return SHLR2<debug, emulateCache, false>(instr);
    case OpcodeType::SHLR8: return SHLR8<debug, emulateCache, false>(instr);
    case OpcodeType::SHLR16: return SHLR16<debug, emulateCache, false>(instr);
    case OpcodeType::SUB: return SUB<debug, emulateCache, false>(instr);
    case OpcodeType::SUBC: return SUBC<debug, emulateCache, false>(instr);
    case OpcodeType::SUBV: return SUBV<debug, emulateCache, false>(instr);
    case OpcodeType::XOR_R: return XOR<debug, emulateCache, false>(instr);
    case OpcodeType::XOR_I: return XORI<debug, emulateCache, false>(instr);
    case OpcodeType::XOR_M: return XORM<debug, emulateCache, false>(instr);

    case OpcodeType::DT: return DT<debug, emulateCache, false>(instr);

    case OpcodeType::CLRMAC: return CLRMAC<debug, emulateCache, false>();
    case OpcodeType::MACW: return MACW<debug, emulateCache, false>(instr);
    case OpcodeType::MACL: return MACL<debug, emulateCache, false>(instr);
    case OpcodeType::MUL: return MULL<debug, emulateCache, false>(instr);
    case OpcodeType::MULS: return MULS<debug, emulateCache, false>(instr);
    case OpcodeType::MULU: return MULU<debug, emulateCache, false>(instr);
    case OpcodeType::DMULS: return DMULS<debug, emulateCache, false>(instr);
    case OpcodeType::DMULU: return DMULU<debug, emulateCache, false>(instr);

    case OpcodeType::DIV0S: return DIV0S<debug, emulateCache, false>(instr);
    case OpcodeType::DIV0U: return DIV0U<debug, emulateCache, false>();
    case OpcodeType::DIV1: return DIV1<debug, emulateCache, false>(instr);

    case OpcodeType::CMP_EQ_I: return CMPIM<debug, emulateCache, false>(instr);
    case OpcodeType::CMP_EQ_R: return CMPEQ<debug, emulateCache, false>(instr);
    case OpcodeType::CMP_GE: return CMPGE<debug, emulateCache, false>(instr);
    case OpcodeType::CMP_GT: return CMPGT<debug, emulateCache, false>(instr);
    case OpcodeType::CMP_HI: return CMPHI<debug, emulateCache, false>(instr);
    case OpcodeType::CMP_HS: return CMPHS<debug, emulateCache, false>(instr);
    case OpcodeType::CMP_PL: return CMPPL<debug, emulateCache, false>(instr);
    case OpcodeType::CMP_PZ: return CMPPZ<debug, emulateCache, false>(instr);
    case OpcodeType::CMP_STR: return CMPSTR<debug, emulateCache, false>(instr);
    case OpcodeType::TAS: return TAS<debug, emulateCache, false>(instr);
    case OpcodeType::TST_R: return TST<debug, emulateCache, false>(instr);
    case OpcodeType::TST_I: return TSTI<debug, emulateCache, false>(instr);
    case OpcodeType::TST_M: return TSTM<debug, emulateCache, false>(instr);

    case OpcodeType::BF: return BF<debug, emulateCache>(instr);
    case OpcodeType::BFS: return BFS<debug>(instr);
    case OpcodeType::BT: return BT<debug, emulateCache>(instr);
    case OpcodeType::BTS: return BTS<debug>(instr);
    case OpcodeType::BRA: return BRA<debug>(instr);
    case OpcodeType::BRAF: return BRAF<debug>(instr);
    case OpcodeType::BSR: return BSR<debug>(instr);
    case OpcodeType::BSRF: return BSRF<debug>(instr);
    case OpcodeType::JMP: return JMP<debug>(instr);
    case OpcodeType::JSR: return JSR<debug>(instr);
    case OpcodeType::TRAPA: return TRAPA<debug, emulateCache>(instr);

    case OpcodeType::RTE: return RTE<debug, emulateCache>();
    case OpcodeType::RTS: return RTS<debug>();

    case OpcodeType::Illegal: return EnterException<debug, emulateCache>(xvGenIllegalInstr);

    case OpcodeType::Delay_NOP: return NOP<debug, emulateCache, true>();

    case OpcodeType::Delay_SLEEP: return SLEEP();

    case OpcodeType::Delay_MOV_R: return MOV<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MOVB_L: return MOVBL<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MOVW_L: return MOVWL<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MOVL_L: return MOVLL<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MOVB_L0: return MOVBL0<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MOVW_L0: return MOVWL0<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MOVL_L0: return MOVLL0<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MOVB_L4: return MOVBL4<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MOVW_L4: return MOVWL4<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MOVL_L4: return MOVLL4<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MOVB_LG: return MOVBLG<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MOVW_LG: return MOVWLG<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MOVL_LG: return MOVLLG<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MOVB_M: return MOVBM<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MOVW_M: return MOVWM<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MOVL_M: return MOVLM<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MOVB_P: return MOVBP<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MOVW_P: return MOVWP<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MOVL_P: return MOVLP<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MOVB_S: return MOVBS<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MOVW_S: return MOVWS<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MOVL_S: return MOVLS<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MOVB_S0: return MOVBS0<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MOVW_S0: return MOVWS0<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MOVL_S0: return MOVLS0<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MOVB_S4: return MOVBS4<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MOVW_S4: return MOVWS4<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MOVL_S4: return MOVLS4<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MOVB_SG: return MOVBSG<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MOVW_SG: return MOVWSG<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MOVL_SG: return MOVLSG<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MOV_I: return MOVI<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MOVW_I: return MOVWI<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MOVL_I: return MOVLI<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MOVA: return MOVA<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MOVT: return MOVT<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_CLRT: return CLRT<debug, emulateCache, true>();
    case OpcodeType::Delay_SETT: return SETT<debug, emulateCache, true>();

    case OpcodeType::Delay_EXTUB: return EXTUB<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_EXTUW: return EXTUW<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_EXTSB: return EXTSB<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_EXTSW: return EXTSW<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_SWAPB: return SWAPB<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_SWAPW: return SWAPW<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_XTRCT: return XTRCT<debug, emulateCache, true>(instr);

    case OpcodeType::Delay_LDC_GBR_R: return LDCGBR<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_LDC_SR_R: return LDCSR<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_LDC_VBR_R: return LDCVBR<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_LDS_MACH_R: return LDSMACH<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_LDS_MACL_R: return LDSMACL<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_LDS_PR_R: return LDSPR<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_STC_GBR_R: return STCGBR<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_STC_SR_R: return STCSR<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_STC_VBR_R: return STCVBR<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_STS_MACH_R: return STSMACH<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_STS_MACL_R: return STSMACL<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_STS_PR_R: return STSPR<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_LDC_GBR_M: return LDCMGBR<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_LDC_SR_M: return LDCMSR<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_LDC_VBR_M: return LDCMVBR<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_LDS_MACH_M: return LDSMMACH<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_LDS_MACL_M: return LDSMMACL<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_LDS_PR_M: return LDSMPR<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_STC_GBR_M: return STCMGBR<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_STC_SR_M: return STCMSR<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_STC_VBR_M: return STCMVBR<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_STS_MACH_M: return STSMMACH<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_STS_MACL_M: return STSMMACL<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_STS_PR_M: return STSMPR<debug, emulateCache, true>(instr);

    case OpcodeType::Delay_ADD: return ADD<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_ADD_I: return ADDI<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_ADDC: return ADDC<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_ADDV: return ADDV<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_AND_R: return AND<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_AND_I: return ANDI<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_AND_M: return ANDM<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_NEG: return NEG<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_NEGC: return NEGC<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_NOT: return NOT<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_OR_R: return OR<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_OR_I: return ORI<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_OR_M: return ORM<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_ROTCL: return ROTCL<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_ROTCR: return ROTCR<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_ROTL: return ROTL<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_ROTR: return ROTR<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_SHAL: return SHAL<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_SHAR: return SHAR<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_SHLL: return SHLL<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_SHLL2: return SHLL2<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_SHLL8: return SHLL8<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_SHLL16: return SHLL16<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_SHLR: return SHLR<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_SHLR2: return SHLR2<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_SHLR8: return SHLR8<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_SHLR16: return SHLR16<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_SUB: return SUB<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_SUBC: return SUBC<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_SUBV: return SUBV<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_XOR_R: return XOR<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_XOR_I: return XORI<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_XOR_M: return XORM<debug, emulateCache, true>(instr);

    case OpcodeType::Delay_DT: return DT<debug, emulateCache, true>(instr);

    case OpcodeType::Delay_CLRMAC: return CLRMAC<debug, emulateCache, true>();
    case OpcodeType::Delay_MACW: return MACW<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MACL: return MACL<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MUL: return MULL<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MULS: return MULS<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_MULU: return MULU<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_DMULS: return DMULS<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_DMULU: return DMULU<debug, emulateCache, true>(instr);

    case OpcodeType::Delay_DIV0S: return DIV0S<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_DIV0U: return DIV0U<debug, emulateCache, true>();
    case OpcodeType::Delay_DIV1: return DIV1<debug, emulateCache, true>(instr);

    case OpcodeType::Delay_CMP_EQ_I: return CMPIM<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_CMP_EQ_R: return CMPEQ<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_CMP_GE: return CMPGE<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_CMP_GT: return CMPGT<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_CMP_HI: return CMPHI<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_CMP_HS: return CMPHS<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_CMP_PL: return CMPPL<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_CMP_PZ: return CMPPZ<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_CMP_STR: return CMPSTR<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_TAS: return TAS<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_TST_R: return TST<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_TST_I: return TSTI<debug, emulateCache, true>(instr);
    case OpcodeType::Delay_TST_M: return TSTM<debug, emulateCache, true>(instr);

    case OpcodeType::IllegalSlot:
        AdvancePC<debug, emulateCache, true>();
        return EnterException<debug, emulateCache>(xvSlotIllegalInstr);
    }

    util::unreachable();
}

template uint64 SH2::InterpretNext<false, false>();
template uint64 SH2::InterpretNext<false, true>();
template uint64 SH2::InterpretNext<true, false>();
template uint64 SH2::InterpretNext<true, true>();

#define DECODE_RN8 const uint32 rn = bit::extract<8, 11>(opcode);
#define DECODE_RM8 const uint32 rm = bit::extract<8, 11>(opcode);
#define DECODE_RN4 const uint32 rn = bit::extract<4, 7>(opcode);
#define DECODE_RM4 const uint32 rm = bit::extract<4, 7>(opcode);
#define DECODE_UDISP4(shift) const uint32 disp = bit::extract<0, 3>(opcode) << shift;
#define DECODE_UDISP8(shift) const uint32 disp = bit::extract<0, 7>(opcode) << shift;
#define DECODE_UIMM8(shift) const uint32 imm = bit::extract<0, 7>(opcode) << shift;
#define DECODE_SDISP8(shift) const uint32 disp = bit::extract_signed<0, 7>(opcode) << shift;
#define DECODE_SIMM8(shift) const uint32 imm = bit::extract_signed<0, 7>(opcode) << shift;
#define DECODE_SDISP12(shift) const uint32 disp = bit::extract_signed<0, 11>(opcode) << shift;

// 0 format: xxxx xxxx xxxx xxxx

// n format: xxxx nnnn xxxx xxxx
#define DECODE_N DECODE_RN8

// m format: xxxx mmmm xxxx xxxx
#define DECODE_M DECODE_RM8

// nm format: xxxx nnnn mmmm xxxx
#define DECODE_NM \
    DECODE_RN8    \
    DECODE_RM4

// md format: xxxx xxxx mmmm dddd
#define DECODE_MD(shift) \
    DECODE_RM4           \
    DECODE_UDISP4(shift)

// nd4 format: xxxx xxxx nnnn dddd
#define DECODE_ND4(shift) \
    DECODE_RN4            \
    DECODE_UDISP4(shift)

// nmd format: xxxx nnnn mmmm dddd
#define DECODE_NMD(shift) \
    DECODE_RN8            \
    DECODE_RM4            \
    DECODE_UDISP4(shift)

// d format: xxxx xxxx dddd dddd
#define DECODE_D_U(shift) DECODE_UDISP8(shift)
#define DECODE_D_S(shift) DECODE_SDISP8(shift)

// d12 format: xxxx dddd dddd dddd
#define DECODE_D12(shift) DECODE_SDISP12(shift)

// nd8 format: xxxx nnnn dddd dddd
#define DECODE_ND8(shift) \
    DECODE_RN8            \
    DECODE_UDISP8(shift)

// i format: xxxx xxxx iiii iiii
#define DECODE_I_U(shift) DECODE_UIMM8(shift)
#define DECODE_I_S(shift) DECODE_SIMM8(shift)

// ni format: xxxx nnnn iiii iiii
#define DECODE_NI \
    DECODE_RN8    \
    DECODE_SIMM8(0)

// nop
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::NOP() {
    AdvancePC<debug, emulateCache, delaySlot>();
    m_wbReg = kWBRegNone;
    return 1;
}

// sleep
FORCE_INLINE uint64 SH2::SLEEP() {
    if (!m_sleep) {
        if (SBYCR.SBY) {
            devlog::trace<grp::exec>(m_logPrefix, "[PC = {:08X}] Entering standby", PC);

            // Initialize DMAC, FRT, WDT and SCI
            for (auto &ch : m_dmaChannels) {
                ch.WriteCHCR<false>(0);
            }
            DMAOR.Reset();
            FRT.Reset();
            WDT.Reset(false);
            // TODO: reset SCI

            // TODO: enter standby state
        } else {
            devlog::trace<grp::exec>(m_logPrefix, "[PC = {:08X}] Entering sleep", PC);
            // TODO: enter sleep state
        }
        m_sleep = true;
        m_wbReg = kWBRegNone;
    }

    return 3;
}

// mov Rm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOV(uint16 opcode) {
    DECODE_NM
    R[rn] = R[rm];
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm, rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// mov.b @Rm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVBL(uint16 opcode) {
    DECODE_NM
    const uint32 address = R[rm];
    const uint64 cycles = AccessCycles<uint8, false, emulateCache>(address) + WritebackCycles(rm);
    R[rn] = bit::sign_extend<8>(MemReadByte<emulateCache>(address));
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    AdvancePC<debug, emulateCache, delaySlot>();
    m_wbReg = rn;
    return cycles;
}

// mov.w @Rm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVWL(uint16 opcode) {
    DECODE_NM
    const uint32 address = R[rm];
    uint64 cycles = AccessCycles<uint16, false, emulateCache>(address);
    if (!m_bus.IsBusWait(address, sizeof(uint16), false)) [[likely]] {
        R[rn] = bit::sign_extend<16>(MemReadWord<emulateCache>(address));
        TraceChangeStack<debug>(m_tracer, rn, R[15]);
        AdvancePC<debug, emulateCache, delaySlot>();
        cycles += WritebackCycles(rm);
        m_wbReg = rn;
    }
    return cycles;
}

// mov.l @Rm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVLL(uint16 opcode) {
    DECODE_NM
    const uint32 address = R[rm];
    uint64 cycles = AccessCycles<uint32, false, emulateCache>(address);
    if (!m_bus.IsBusWait(address, sizeof(uint32), false)) [[likely]] {
        R[rn] = MemReadLong<emulateCache>(address);
        TraceChangeStack<debug>(m_tracer, rn, R[15]);
        AdvancePC<debug, emulateCache, delaySlot>();
        cycles += WritebackCycles(rm);
        m_wbReg = rn;
    }
    return cycles;
}

// mov.b @(R0,Rm), Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVBL0(uint16 opcode) {
    DECODE_NM
    const uint32 address = R[rm] + R[0];
    const uint64 cycles = AccessCycles<uint8, false, emulateCache>(address) + WritebackCycles(rm, 0);
    R[rn] = bit::sign_extend<8>(MemReadByte<emulateCache>(address));
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    AdvancePC<debug, emulateCache, delaySlot>();
    m_wbReg = rn;
    return cycles;
}

// mov.w @(R0,Rm), Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVWL0(uint16 opcode) {
    DECODE_NM
    const uint32 address = R[rm] + R[0];
    uint64 cycles = AccessCycles<uint16, false, emulateCache>(address);
    if (!m_bus.IsBusWait(address, sizeof(uint16), false)) [[likely]] {
        R[rn] = bit::sign_extend<16>(MemReadWord<emulateCache>(address));
        TraceChangeStack<debug>(m_tracer, rn, R[15]);
        AdvancePC<debug, emulateCache, delaySlot>();
        cycles += WritebackCycles(rm, 0);
        m_wbReg = rn;
    }
    return cycles;
}

// mov.l @(R0,Rm), Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVLL0(uint16 opcode) {
    DECODE_NM
    const uint32 address = R[rm] + R[0];
    uint64 cycles = AccessCycles<uint32, false, emulateCache>(address);
    if (!m_bus.IsBusWait(address, sizeof(uint32), false)) [[likely]] {
        R[rn] = MemReadLong<emulateCache>(address);
        TraceChangeStack<debug>(m_tracer, rn, R[15]);
        AdvancePC<debug, emulateCache, delaySlot>();
        cycles += WritebackCycles(rm, 0);
        m_wbReg = rn;
    }
    return cycles;
}

// mov.b @(disp,Rm), R0
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVBL4(uint16 opcode) {
    DECODE_MD(0u)
    const uint32 address = R[rm] + disp;
    const uint64 cycles = AccessCycles<uint8, false, emulateCache>(address);
    R[0] = bit::sign_extend<8>(MemReadByte<emulateCache>(address));
    AdvancePC<debug, emulateCache, delaySlot>();
    m_wbReg = 0;
    return cycles;
}

// mov.w @(disp,Rm), R0
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVWL4(uint16 opcode) {
    DECODE_MD(1u)
    const uint32 address = R[rm] + disp;
    uint64 cycles = AccessCycles<uint16, false, emulateCache>(address) + WritebackCycles(rm);
    if (!m_bus.IsBusWait(address, sizeof(uint16), false)) [[likely]] {
        R[0] = bit::sign_extend<16>(MemReadWord<emulateCache>(address));
        AdvancePC<debug, emulateCache, delaySlot>();
        cycles += WritebackCycles(rm);
        m_wbReg = 0;
    }
    return cycles;
}

// mov.l @(disp,Rm), Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVLL4(uint16 opcode) {
    DECODE_NMD(2u)
    const uint32 address = R[rm] + disp;
    uint64 cycles = AccessCycles<uint32, false, emulateCache>(address);
    if (!m_bus.IsBusWait(address, sizeof(uint32), false)) [[likely]] {
        R[rn] = MemReadLong<emulateCache>(address);
        TraceChangeStack<debug>(m_tracer, rn, R[15]);
        AdvancePC<debug, emulateCache, delaySlot>();
        cycles += WritebackCycles(rm);
        m_wbReg = rn;
    }
    return cycles;
}

// mov.b @(disp,GBR), R0
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVBLG(uint16 opcode) {
    DECODE_D_U(0u);
    const uint32 address = GBR + disp;
    const uint64 cycles = AccessCycles<uint8, false, emulateCache>(address);
    R[0] = bit::sign_extend<8>(MemReadByte<emulateCache>(address));
    AdvancePC<debug, emulateCache, delaySlot>();
    m_wbReg = 0;
    return cycles;
}

// mov.w @(disp,GBR), R0
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVWLG(uint16 opcode) {
    DECODE_D_U(1u);
    const uint32 address = GBR + disp;
    const uint64 cycles = AccessCycles<uint16, false, emulateCache>(address);
    if (!m_bus.IsBusWait(address, sizeof(uint16), false)) [[likely]] {
        R[0] = bit::sign_extend<16>(MemReadWord<emulateCache>(address));
        AdvancePC<debug, emulateCache, delaySlot>();
        m_wbReg = 0;
    }
    return cycles;
}

// mov.l @(disp,GBR), R0
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVLLG(uint16 opcode) {
    DECODE_D_U(2u);
    const uint32 address = GBR + disp;
    const uint64 cycles = AccessCycles<uint32, false, emulateCache>(address);
    if (!m_bus.IsBusWait(address, sizeof(uint32), false)) [[likely]] {
        R[0] = MemReadLong<emulateCache>(address);
        AdvancePC<debug, emulateCache, delaySlot>();
        m_wbReg = 0;
    }
    return cycles;
}

// mov.b Rm, @-Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVBM(uint16 opcode) {
    DECODE_NM
    const uint32 address = R[rn] - 1;
    const uint64 cycles = AccessCycles<uint8, true, emulateCache>(address) + WritebackCycles(rm, rn);
    MemWriteByte<debug, emulateCache>(address, R[rm]);
    TracePushRegisterToStack<debug>(m_tracer, rn, rm, R[15], address);
    R[rn] = address;
    AdvancePC<debug, emulateCache, delaySlot>();
    m_wbReg = kWBRegNone;
    return cycles;
}

// mov.w Rm, @-Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVWM(uint16 opcode) {
    DECODE_NM
    const uint32 address = R[rn] - 2;
    uint64 cycles = AccessCycles<uint16, true, emulateCache>(address);
    if (!m_bus.IsBusWait(address, sizeof(uint16), true)) [[likely]] {
        MemWriteWord<debug, emulateCache>(address, R[rm]);
        TracePushRegisterToStack<debug>(m_tracer, rn, rm, R[15], address);
        R[rn] = address;
        AdvancePC<debug, emulateCache, delaySlot>();
        cycles += WritebackCycles(rm, rn);
        m_wbReg = kWBRegNone;
    }
    return cycles;
}

// mov.l Rm, @-Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVLM(uint16 opcode) {
    DECODE_NM
    const uint32 address = R[rn] - 4;
    uint64 cycles = AccessCycles<uint32, true, emulateCache>(address);
    if (!m_bus.IsBusWait(address, sizeof(uint32), true)) [[likely]] {
        MemWriteLong<debug, emulateCache>(address, R[rm]);
        TracePushRegisterToStack<debug>(m_tracer, rn, rm, R[15], address);
        R[rn] = address;
        AdvancePC<debug, emulateCache, delaySlot>();
        cycles += WritebackCycles(rm, rn);
        m_wbReg = kWBRegNone;
    }
    return cycles;
}

// mov.b @Rm+, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVBP(uint16 opcode) {
    DECODE_NM
    const uint32 address = R[rm];
    const uint64 cycles = AccessCycles<uint8, false, emulateCache>(address) + WritebackCycles(rm);
    R[rn] = bit::sign_extend<8>(MemReadByte<emulateCache>(address));
    if (rn != rm) {
        R[rm] += 1;
    }
    TracePopFromStack<debug>(m_tracer, rm, R[15]);
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    AdvancePC<debug, emulateCache, delaySlot>();
    m_wbReg = rn;
    return cycles;
}

// mov.w @Rm+, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVWP(uint16 opcode) {
    DECODE_NM
    const uint32 address = R[rm];
    uint64 cycles = AccessCycles<uint16, false, emulateCache>(address);
    if (!m_bus.IsBusWait(address, sizeof(uint16), false)) [[likely]] {
        R[rn] = bit::sign_extend<16>(MemReadWord<emulateCache>(address));
        if (rn != rm) {
            R[rm] += 2;
        }
        TracePopFromStack<debug>(m_tracer, rm, R[15]);
        TraceChangeStack<debug>(m_tracer, rn, R[15]);
        AdvancePC<debug, emulateCache, delaySlot>();
        cycles += WritebackCycles(rm);
        m_wbReg = rn;
    }
    return cycles;
}

// mov.l @Rm+, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVLP(uint16 opcode) {
    DECODE_NM
    const uint32 address = R[rm];
    uint64 cycles = AccessCycles<uint32, false, emulateCache>(address);
    if (!m_bus.IsBusWait(address, sizeof(uint32), false)) [[likely]] {
        R[rn] = MemReadLong<emulateCache>(address);
        if (rn != rm) {
            R[rm] += 4;
        }
        TracePopFromStack<debug>(m_tracer, rm, R[15]);
        TraceChangeStack<debug>(m_tracer, rn, R[15]);
        AdvancePC<debug, emulateCache, delaySlot>();
        cycles += WritebackCycles(rm);
        m_wbReg = rn;
    }
    return cycles;
}

// mov.b Rm, @Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVBS(uint16 opcode) {
    DECODE_NM
    const uint32 address = R[rn];
    const uint64 cycles = AccessCycles<uint8, true, emulateCache>(address) + WritebackCycles(rm, rn);
    MemWriteByte<debug, emulateCache>(address, R[rm]);
    AdvancePC<debug, emulateCache, delaySlot>();
    m_wbReg = kWBRegNone;
    return cycles;
}

// mov.w Rm, @Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVWS(uint16 opcode) {
    DECODE_NM
    const uint32 address = R[rn];
    uint64 cycles = AccessCycles<uint16, true, emulateCache>(address);
    if (!m_bus.IsBusWait(address, sizeof(uint16), true)) [[likely]] {
        MemWriteWord<debug, emulateCache>(address, R[rm]);
        AdvancePC<debug, emulateCache, delaySlot>();
        cycles += WritebackCycles(rm, rn);
        m_wbReg = kWBRegNone;
    }
    return cycles;
}

// mov.l Rm, @Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVLS(uint16 opcode) {
    DECODE_NM
    const uint32 address = R[rn];
    uint64 cycles = AccessCycles<uint32, true, emulateCache>(address);
    if (!m_bus.IsBusWait(address, sizeof(uint32), true)) [[likely]] {
        MemWriteLong<debug, emulateCache>(address, R[rm]);
        AdvancePC<debug, emulateCache, delaySlot>();
        cycles += WritebackCycles(rm, rn);
        m_wbReg = kWBRegNone;
    }
    return cycles;
}

// mov.b Rm, @(R0,Rn)
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVBS0(uint16 opcode) {
    DECODE_NM
    const uint32 address = R[rn] + R[0];
    const uint64 cycles = AccessCycles<uint8, true, emulateCache>(address) + WritebackCycles(rn, 0);
    MemWriteByte<debug, emulateCache>(address, R[rm]);
    AdvancePC<debug, emulateCache, delaySlot>();
    m_wbReg = kWBRegNone;
    return cycles;
}

// mov.w Rm, @(R0,Rn)
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVWS0(uint16 opcode) {
    DECODE_NM
    const uint32 address = R[rn] + R[0];
    uint64 cycles = AccessCycles<uint16, true, emulateCache>(address);
    if (!m_bus.IsBusWait(address, sizeof(uint16), true)) [[likely]] {
        MemWriteWord<debug, emulateCache>(address, R[rm]);
        AdvancePC<debug, emulateCache, delaySlot>();
        cycles += WritebackCycles(rn, 0);
        m_wbReg = kWBRegNone;
    }
    return cycles;
}

// mov.l Rm, @(R0,Rn)
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVLS0(uint16 opcode) {
    DECODE_NM
    const uint32 address = R[rn] + R[0];
    uint64 cycles = AccessCycles<uint32, true, emulateCache>(address);
    if (!m_bus.IsBusWait(address, sizeof(uint32), true)) [[likely]] {
        MemWriteLong<debug, emulateCache>(address, R[rm]);
        AdvancePC<debug, emulateCache, delaySlot>();
        cycles += WritebackCycles(rn, 0);
        m_wbReg = kWBRegNone;
    }
    return cycles;
}

// mov.b R0, @(disp,Rn)
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVBS4(uint16 opcode) {
    DECODE_ND4(0u)
    const uint32 address = R[rn] + disp;
    const uint64 cycles = AccessCycles<uint8, true, emulateCache>(address) + WritebackCycles(rn, 0);
    MemWriteByte<debug, emulateCache>(address, R[0]);
    AdvancePC<debug, emulateCache, delaySlot>();
    m_wbReg = kWBRegNone;
    return cycles;
}

// mov.w R0, @(disp,Rn)
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVWS4(uint16 opcode) {
    DECODE_ND4(1u)
    const uint32 address = R[rn] + disp;
    uint64 cycles = AccessCycles<uint16, true, emulateCache>(address);
    if (!m_bus.IsBusWait(address, sizeof(uint16), true)) [[likely]] {
        MemWriteWord<debug, emulateCache>(address, R[0]);
        AdvancePC<debug, emulateCache, delaySlot>();
        cycles += WritebackCycles(rn, 0);
        m_wbReg = kWBRegNone;
    }
    return cycles;
}

// mov.l Rm, @(disp,Rn)
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVLS4(uint16 opcode) {
    DECODE_NMD(2u)
    const uint32 address = R[rn] + disp;
    uint64 cycles = AccessCycles<uint32, true, emulateCache>(address);
    if (!m_bus.IsBusWait(address, sizeof(uint32), true)) [[likely]] {
        MemWriteLong<debug, emulateCache>(address, R[rm]);
        AdvancePC<debug, emulateCache, delaySlot>();
        cycles += WritebackCycles(rm, rn);
        m_wbReg = kWBRegNone;
    }
    return cycles;
}

// mov.b R0, @(disp,GBR)
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVBSG(uint16 opcode) {
    DECODE_D_U(0u)
    const uint32 address = GBR + disp;
    const uint64 cycles = AccessCycles<uint8, true, emulateCache>(address) + WritebackCycles(0);
    MemWriteByte<debug, emulateCache>(address, R[0]);
    AdvancePC<debug, emulateCache, delaySlot>();
    m_wbReg = kWBRegNone;
    return cycles;
}

// mov.w R0, @(disp,GBR)
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVWSG(uint16 opcode) {
    DECODE_D_U(1u)
    const uint32 address = GBR + disp;
    uint64 cycles = AccessCycles<uint16, true, emulateCache>(address);
    if (!m_bus.IsBusWait(address, sizeof(uint16), true)) [[likely]] {
        MemWriteWord<debug, emulateCache>(address, R[0]);
        AdvancePC<debug, emulateCache, delaySlot>();
        cycles += WritebackCycles(0);
        m_wbReg = kWBRegNone;
    }
    return cycles;
}

// mov.l R0, @(disp,GBR)
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVLSG(uint16 opcode) {
    DECODE_D_U(2u)
    const uint32 address = GBR + disp;
    uint64 cycles = AccessCycles<uint32, true, emulateCache>(address);
    if (!m_bus.IsBusWait(address, sizeof(uint32), true)) [[likely]] {
        MemWriteLong<debug, emulateCache>(address, R[0]);
        AdvancePC<debug, emulateCache, delaySlot>();
        cycles += WritebackCycles(0);
        m_wbReg = kWBRegNone;
    }
    return cycles;
}

// mov #imm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVI(uint16 opcode) {
    DECODE_NI
    R[rn] = imm;
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// mov.w @(disp,PC), Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVWI(uint16 opcode) {
    DECODE_ND8(1u)
    const uint32 pc = (delaySlot ? m_delaySlotTarget - 2u : PC);
    const uint32 address = pc + disp + 4u;
    const uint64 cycles = AccessCycles<uint16, false, emulateCache>(address);
    R[rn] = bit::sign_extend<16>(MemReadWord<emulateCache, true>(address));
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    AdvancePC<debug, emulateCache, delaySlot>();
    m_wbReg = rn;
    return cycles;
}

// mov.l @(disp,PC), Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVLI(uint16 opcode) {
    DECODE_ND8(2u)
    const uint32 pc = (delaySlot ? m_delaySlotTarget - 2u : PC);
    const uint32 address = (pc & ~3u) + disp + 4u;
    const uint64 cycles = AccessCycles<uint32, false, emulateCache>(address);
    R[rn] = MemReadLong<emulateCache, true>(address);
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    AdvancePC<debug, emulateCache, delaySlot>();
    m_wbReg = rn;
    return cycles;
}

// mova @(disp,PC), R0
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVA(uint16 opcode) {
    DECODE_D_U(2u)
    const uint32 pc = (delaySlot ? m_delaySlotTarget - 2u : PC);
    const uint32 address = (pc & ~3u) + disp + 4u;
    R[0] = address;
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(0) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// movt Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVT(uint16 opcode) {
    DECODE_N
    R[rn] = SR.T;
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// clrt
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::CLRT() {
    SR.T = 0;
    AdvancePC<debug, emulateCache, delaySlot>();
    m_wbReg = kWBRegNone;
    return 1;
}

// sett
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::SETT() {
    SR.T = 1;
    AdvancePC<debug, emulateCache, delaySlot>();
    m_wbReg = kWBRegNone;
    return 1;
}

// exts.b Rm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::EXTSB(uint16 opcode) {
    DECODE_NM
    R[rn] = bit::sign_extend<8>(R[rm]);
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm, rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// exts.w Rm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::EXTSW(uint16 opcode) {
    DECODE_NM
    R[rn] = bit::sign_extend<16>(R[rm]);
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm, rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// extu.b Rm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::EXTUB(uint16 opcode) {
    DECODE_NM
    R[rn] = R[rm] & 0xFF;
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm, rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// extu.w Rm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::EXTUW(uint16 opcode) {
    DECODE_NM
    R[rn] = R[rm] & 0xFFFF;
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm, rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// swap.b Rm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::SWAPB(uint16 opcode) {
    DECODE_NM
    const uint32 tmp0 = R[rm] & 0xFFFF0000;
    const uint32 tmp1 = (R[rm] & 0xFF) << 8u;
    R[rn] = ((R[rm] >> 8u) & 0xFF) | tmp1 | tmp0;
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm, rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// swap.w Rm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::SWAPW(uint16 opcode) {
    DECODE_NM
    const uint32 tmp = R[rm] >> 16u;
    R[rn] = (R[rm] << 16u) | tmp;
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm, rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// xtrct Rm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::XTRCT(uint16 opcode) {
    DECODE_NM
    R[rn] = (R[rn] >> 16u) | (R[rm] << 16u);
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm, rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// ldc Rm, GBR
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::LDCGBR(uint16 opcode) {
    DECODE_M
    GBR = R[rm];
    m_intrFlags.allow = false;
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// ldc Rm, SR
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::LDCSR(uint16 opcode) {
    DECODE_M
    SR.u32 = R[rm] & 0x000003F3;
    m_intrFlags = std::bit_cast<IntrFlags>(std::bit_cast<uint16>(IntrFlags{
        .pending = !delaySlot && INTC.pending.level > SR.ILevel,
        .allow = false,
    }));
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// ldc Rm, VBR
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::LDCVBR(uint16 opcode) {
    DECODE_M
    VBR = R[rm];
    m_intrFlags.allow = false;
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// lds Rm, MACH
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::LDSMACH(uint16 opcode) {
    DECODE_M
    MAC.H = R[rm];
    m_intrFlags.allow = false;
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// lds Rm, MACL
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::LDSMACL(uint16 opcode) {
    DECODE_M
    MAC.L = R[rm];
    m_intrFlags.allow = false;
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// lds Rm, PR
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::LDSPR(uint16 opcode) {
    DECODE_M
    PR = R[rm];
    m_intrFlags.allow = false;
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm, kWBRegPR) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// stc GBR, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::STCGBR(uint16 opcode) {
    DECODE_N
    R[rn] = GBR;
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    m_intrFlags.allow = false;
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// stc SR, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::STCSR(uint16 opcode) {
    DECODE_N
    R[rn] = SR.u32;
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    m_intrFlags.allow = false;
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// stc VBR, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::STCVBR(uint16 opcode) {
    DECODE_N
    R[rn] = VBR;
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    m_intrFlags.allow = false;
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// sts MACH, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::STSMACH(uint16 opcode) {
    DECODE_N
    R[rn] = MAC.H;
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    m_intrFlags.allow = false;
    AdvancePC<debug, emulateCache, delaySlot>();
    m_wbReg = rn;
    return 1;
}

// sts MACL, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::STSMACL(uint16 opcode) {
    DECODE_N
    R[rn] = MAC.L;
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    m_intrFlags.allow = false;
    AdvancePC<debug, emulateCache, delaySlot>();
    m_wbReg = rn;
    return 1;
}

// sts PR, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::STSPR(uint16 opcode) {
    DECODE_N
    R[rn] = PR;
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    m_intrFlags.allow = false;
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rn, kWBRegPR) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// ldc.l @Rm+, GBR
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::LDCMGBR(uint16 opcode) {
    DECODE_M
    const uint32 address = R[rm];
    const uint64 cycles = AccessCycles<uint32, false, emulateCache>(address) + WritebackCycles(rm) + 2;
    GBR = MemReadLong<emulateCache>(address);
    R[rm] += 4;
    TracePopFromStack<debug>(m_tracer, rm, R[15]);
    m_intrFlags.allow = false;
    AdvancePC<debug, emulateCache, delaySlot>();
    m_wbReg = kWBRegNone;
    return cycles;
}

// ldc.l @Rm+, SR
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::LDCMSR(uint16 opcode) {
    DECODE_M
    const uint32 address = R[rm];
    const uint64 cycles = AccessCycles<uint32, false, emulateCache>(address) + WritebackCycles(rm) + 2;
    SR.u32 = MemReadLong<emulateCache>(address) & 0x000003F3;
    m_intrFlags = std::bit_cast<IntrFlags>(std::bit_cast<uint16>(IntrFlags{
        .pending = !delaySlot && INTC.pending.level > SR.ILevel,
        .allow = false,
    }));
    R[rm] += 4;
    TracePopFromStack<debug>(m_tracer, rm, R[15]);
    AdvancePC<debug, emulateCache, delaySlot>();
    m_wbReg = kWBRegNone;
    return cycles;
}

// ldc.l @Rm+, VBR
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::LDCMVBR(uint16 opcode) {
    DECODE_M
    const uint32 address = R[rm];
    const uint64 cycles = AccessCycles<uint32, false, emulateCache>(address) + WritebackCycles(rm) + 2;
    VBR = MemReadLong<emulateCache>(address);
    R[rm] += 4;
    TracePopFromStack<debug>(m_tracer, rm, R[15]);
    m_intrFlags.allow = false;
    AdvancePC<debug, emulateCache, delaySlot>();
    m_wbReg = kWBRegNone;
    return cycles;
}

// lds.l @Rm+, MACH
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::LDSMMACH(uint16 opcode) {
    DECODE_M
    const uint32 address = R[rm];
    const uint64 cycles = AccessCycles<uint32, false, emulateCache>(address) + WritebackCycles(rm);
    MAC.H = MemReadLong<emulateCache>(address);
    R[rm] += 4;
    TracePopFromStack<debug>(m_tracer, rm, R[15]);
    m_intrFlags.allow = false;
    AdvancePC<debug, emulateCache, delaySlot>();
    m_wbReg = kWBRegNone;
    return cycles;
}

// lds.l @Rm+, MACL
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::LDSMMACL(uint16 opcode) {
    DECODE_M
    const uint32 address = R[rm];
    const uint64 cycles = AccessCycles<uint32, false, emulateCache>(address) + WritebackCycles(rm);
    MAC.L = MemReadLong<emulateCache>(address);
    R[rm] += 4;
    TracePopFromStack<debug>(m_tracer, rm, R[15]);
    m_intrFlags.allow = false;
    AdvancePC<debug, emulateCache, delaySlot>();
    m_wbReg = kWBRegNone;
    return cycles;
}

// lds.l @Rm+, PR
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::LDSMPR(uint16 opcode) {
    DECODE_M
    const uint32 address = R[rm];
    const uint64 cycles = AccessCycles<uint32, false, emulateCache>(address) + WritebackCycles(rm);
    PR = MemReadLong<emulateCache>(address);
    R[rm] += 4;
    TracePopFromStack<debug>(m_tracer, rm, R[15]);
    m_intrFlags.allow = false;
    AdvancePC<debug, emulateCache, delaySlot>();
    m_wbReg = kWBRegPR;
    return cycles;
}

// stc.l GBR, @-Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::STCMGBR(uint16 opcode) {
    DECODE_N
    R[rn] -= 4;
    TracePushToStack<debug>(m_tracer, rn, debug::SH2StackValueType::GBR, R[15]);
    const uint32 address = R[rn];
    const uint64 cycles = AccessCycles<uint32, true, emulateCache>(address) + WritebackCycles(rn) + 2;
    MemWriteLong<debug, emulateCache>(address, GBR);
    m_intrFlags.allow = false;
    AdvancePC<debug, emulateCache, delaySlot>();
    m_wbReg = kWBRegNone;
    return cycles;
}

// stc.l SR, @-Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::STCMSR(uint16 opcode) {
    DECODE_N
    R[rn] -= 4;
    TracePushToStack<debug>(m_tracer, rn, debug::SH2StackValueType::SR, R[15]);
    const uint32 address = R[rn];
    const uint64 cycles = AccessCycles<uint32, true, emulateCache>(address) + WritebackCycles(rn) + 2;
    MemWriteLong<debug, emulateCache>(address, SR.u32);
    m_intrFlags.allow = false;
    AdvancePC<debug, emulateCache, delaySlot>();
    m_wbReg = kWBRegNone;
    return cycles;
}

// stc.l VBR, @-Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::STCMVBR(uint16 opcode) {
    DECODE_N
    R[rn] -= 4;
    TracePushToStack<debug>(m_tracer, rn, debug::SH2StackValueType::VBR, R[15]);
    const uint32 address = R[rn];
    const uint64 cycles = AccessCycles<uint32, true, emulateCache>(address) + WritebackCycles(rn) + 2;
    MemWriteLong<debug, emulateCache>(address, VBR);
    m_intrFlags.allow = false;
    AdvancePC<debug, emulateCache, delaySlot>();
    m_wbReg = kWBRegNone;
    return cycles;
}

// sts.l MACH, @-Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::STSMMACH(uint16 opcode) {
    DECODE_N
    R[rn] -= 4;
    TracePushToStack<debug>(m_tracer, rn, debug::SH2StackValueType::MACH, R[15]);
    const uint32 address = R[rn];
    const uint64 cycles = AccessCycles<uint32, true, emulateCache>(address) + WritebackCycles(rn);
    MemWriteLong<debug, emulateCache>(address, MAC.H);
    m_intrFlags.allow = false;
    AdvancePC<debug, emulateCache, delaySlot>();
    m_wbReg = kWBRegNone;
    return cycles;
}

// sts.l MACL, @-Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::STSMMACL(uint16 opcode) {
    DECODE_N
    R[rn] -= 4;
    TracePushToStack<debug>(m_tracer, rn, debug::SH2StackValueType::MACL, R[15]);
    const uint32 address = R[rn];
    const uint64 cycles = AccessCycles<uint32, true, emulateCache>(address) + WritebackCycles(rn);
    MemWriteLong<debug, emulateCache>(address, MAC.L);
    m_intrFlags.allow = false;
    AdvancePC<debug, emulateCache, delaySlot>();
    m_wbReg = kWBRegNone;
    return cycles;
}

// sts.l PR, @-Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::STSMPR(uint16 opcode) {
    DECODE_N
    R[rn] -= 4;
    TracePushToStack<debug>(m_tracer, rn, debug::SH2StackValueType::PR, R[15]);
    const uint32 address = R[rn];
    const uint64 cycles = AccessCycles<uint32, true, emulateCache>(address) + WritebackCycles(rn, kWBRegPR);
    MemWriteLong<debug, emulateCache>(address, PR);
    m_intrFlags.allow = false;
    AdvancePC<debug, emulateCache, delaySlot>();
    m_wbReg = kWBRegNone;
    return cycles;
}

// add Rm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::ADD(uint16 opcode) {
    DECODE_NM
    const uint32 newValue = R[rn] + R[rm];
    TraceResizeStack<debug>(m_tracer, rn, R[15], newValue);
    R[rn] = newValue;
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm, rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// add #imm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::ADDI(uint16 opcode) {
    DECODE_NI
    const uint32 newValue = R[rn] + imm;
    TraceResizeStack<debug>(m_tracer, rn, R[15], newValue);
    R[rn] = newValue;
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// addc Rm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::ADDC(uint16 opcode) {
    DECODE_NM
    const uint32 tmp1 = R[rn] + R[rm];
    const uint32 tmp0 = R[rn];
    const uint32 newValue = tmp1 + SR.T;
    TraceResizeStack<debug>(m_tracer, rn, R[15], newValue);
    R[rn] = newValue;
    SR.T = (tmp0 > tmp1) || (tmp1 > R[rn]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm, rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// addv Rm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::ADDV(uint16 opcode) {
    DECODE_NM
    const bool dst = static_cast<sint32>(R[rn]) < 0;
    const bool src = static_cast<sint32>(R[rm]) < 0;

    const uint32 newValue = R[rn] + R[rm];
    TraceResizeStack<debug>(m_tracer, rn, R[15], newValue);
    R[rn] = newValue;

    bool ans = static_cast<sint32>(R[rn]) < 0;
    ans ^= dst;
    SR.T = (src == dst) & ans;

    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm, rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// and Rm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::AND(uint16 opcode) {
    DECODE_NM
    R[rn] &= R[rm];
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm, rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// and #imm, R0
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::ANDI(uint16 opcode) {
    DECODE_I_U(0u)
    R[0] &= imm;
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(0) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// and.b #imm, @(R0,GBR)
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::ANDM(uint16 opcode) {
    DECODE_I_U(0u)
    const uint32 address = GBR + R[0];
    const uint64 cycles = AccessCycles<uint8, false, emulateCache>(address) +
                          AccessCycles<uint8, true, emulateCache>(address) + WritebackCycles(0) + 1;
    uint8 tmp = MemReadByte<emulateCache>(address);
    tmp &= imm;
    MemWriteByte<debug, emulateCache>(address, tmp);
    AdvancePC<debug, emulateCache, delaySlot>();
    m_wbReg = kWBRegNone;
    return cycles;
}

// neg Rm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::NEG(uint16 opcode) {
    DECODE_NM
    R[rn] = -R[rm];
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm, rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// negc Rm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::NEGC(uint16 opcode) {
    DECODE_NM
    const uint32 tmp = -R[rm];
    R[rn] = tmp - SR.T;
    SR.T = (0 < tmp) || (tmp < R[rn]);
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm, rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// not Rm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::NOT(uint16 opcode) {
    DECODE_NM
    R[rn] = ~R[rm];
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm, rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// or Rm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::OR(uint16 opcode) {
    DECODE_NM
    R[rn] |= R[rm];
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm, rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// or #imm, R0
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::ORI(uint16 opcode) {
    DECODE_I_U(0u)
    R[0] |= imm;
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(0) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// or.b #imm, @(R0,GBR)
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::ORM(uint16 opcode) {
    DECODE_I_U(0u)
    const uint32 address = GBR + R[0];
    const uint64 cycles = AccessCycles<uint16, false, emulateCache>(address) +
                          AccessCycles<uint16, true, emulateCache>(address) + WritebackCycles(0) + 1;
    uint8 tmp = MemReadByte<emulateCache>(address);
    tmp |= imm;
    MemWriteByte<debug, emulateCache>(address, tmp);
    AdvancePC<debug, emulateCache, delaySlot>();
    m_wbReg = kWBRegNone;
    return cycles;
}

// rotcl Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::ROTCL(uint16 opcode) {
    DECODE_N
    const bool tmp = R[rn] >> 31u;
    R[rn] = (R[rn] << 1u) | SR.T;
    SR.T = tmp;
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// rotcr Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::ROTCR(uint16 opcode) {
    DECODE_N
    const bool tmp = R[rn] & 1u;
    R[rn] = (R[rn] >> 1u) | (SR.T << 31u);
    SR.T = tmp;
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// rotl Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::ROTL(uint16 opcode) {
    DECODE_N
    SR.T = R[rn] >> 31u;
    R[rn] = (R[rn] << 1u) | SR.T;
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// rotr Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::ROTR(uint16 opcode) {
    DECODE_N
    SR.T = R[rn] & 1u;
    R[rn] = (R[rn] >> 1u) | (SR.T << 31u);
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// shal Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::SHAL(uint16 opcode) {
    DECODE_N
    SR.T = R[rn] >> 31u;
    R[rn] <<= 1u;
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// shar Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::SHAR(uint16 opcode) {
    DECODE_N
    SR.T = R[rn] & 1u;
    R[rn] = static_cast<sint32>(R[rn]) >> 1;
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// shll Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::SHLL(uint16 opcode) {
    DECODE_N
    SR.T = R[rn] >> 31u;
    R[rn] <<= 1u;
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// shll2 Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::SHLL2(uint16 opcode) {
    DECODE_N
    R[rn] <<= 2u;
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// shll8 Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::SHLL8(uint16 opcode) {
    DECODE_N
    R[rn] <<= 8u;
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// shll16 Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::SHLL16(uint16 opcode) {
    DECODE_N
    R[rn] <<= 16u;
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// shlr Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::SHLR(uint16 opcode) {
    DECODE_N
    SR.T = R[rn] & 1u;
    R[rn] >>= 1u;
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// shlr2 Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::SHLR2(uint16 opcode) {
    DECODE_N
    R[rn] >>= 2u;
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// shlr8 Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::SHLR8(uint16 opcode) {
    DECODE_N
    R[rn] >>= 8u;
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// shlr16 Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::SHLR16(uint16 opcode) {
    DECODE_N
    R[rn] >>= 16u;
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// sub Rm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::SUB(uint16 opcode) {
    DECODE_NM
    const uint32 newValue = R[rn] - R[rm];
    TraceResizeStack<debug>(m_tracer, rn, R[15], newValue);
    R[rn] = newValue;
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm, rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// subc Rm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::SUBC(uint16 opcode) {
    DECODE_NM
    const uint32 tmp1 = R[rn] - R[rm];
    const uint32 tmp0 = R[rn];
    const uint32 newValue = tmp1 - SR.T;
    TraceResizeStack<debug>(m_tracer, rn, R[15], newValue);
    R[rn] = newValue;
    SR.T = (tmp0 < tmp1) || (tmp1 < R[rn]);
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm, rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// subv Rm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::SUBV(uint16 opcode) {
    DECODE_NM
    const bool dst = static_cast<sint32>(R[rn]) < 0;
    const bool src = static_cast<sint32>(R[rm]) < 0;
    const uint32 newValue = R[rn] - R[rm];
    TraceResizeStack<debug>(m_tracer, rn, R[15], newValue);

    R[rn] = newValue;

    bool ans = static_cast<sint32>(R[rn]) < 0;
    ans ^= dst;
    SR.T = (src != dst) & ans;

    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm, rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// xor Rm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::XOR(uint16 opcode) {
    DECODE_NM
    R[rn] ^= R[rm];
    TraceChangeStack<debug>(m_tracer, rn, R[15]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm, rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// xor #imm, R0
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::XORI(uint16 opcode) {
    DECODE_I_U(0u)
    R[0] ^= imm;
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(0) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// xor.b #imm, @(R0,GBR)
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::XORM(uint16 opcode) {
    DECODE_I_U(0u)
    const uint32 address = GBR + R[0];
    const uint64 cycles = AccessCycles<uint32, false, emulateCache>(address) +
                          AccessCycles<uint32, true, emulateCache>(address) + WritebackCycles(0) + 1;
    uint8 tmp = MemReadByte<emulateCache>(address);
    tmp ^= imm;
    MemWriteByte<debug, emulateCache>(address, tmp);
    AdvancePC<debug, emulateCache, delaySlot>();
    m_wbReg = kWBRegNone;
    return cycles;
}

// dt Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::DT(uint16 opcode) {
    DECODE_N
    TraceResizeStack<debug>(m_tracer, rn, R[15], R[15] - 1);
    --R[rn];
    SR.T = R[rn] == 0;
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// clrmac
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::CLRMAC() {
    MAC.u64 = 0;
    AdvancePC<debug, emulateCache, delaySlot>();
    m_wbReg = kWBRegNone;
    return 1;
}

// mac.w @Rm+, @Rn+
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MACW(uint16 opcode) {
    DECODE_NM

    const uint32 address2 = R[rn];
    uint64 cycles = AccessCycles<uint16, false, emulateCache>(address2);
    const sint32 op2 = static_cast<sint16>(MemReadWord<emulateCache>(address2));
    R[rn] += 2;
    TracePopFromStack<debug>(m_tracer, rn, R[15]);

    const uint32 address1 = R[rm];
    cycles += AccessCycles<uint16, false, emulateCache>(address1);
    const sint32 op1 = static_cast<sint16>(MemReadWord<emulateCache>(address1));
    R[rm] += 2;
    TracePopFromStack<debug>(m_tracer, rm, R[15]);

    const sint32 mul = op1 * op2;
    if (SR.S) {
        const sint64 result = static_cast<sint64>(static_cast<sint32>(MAC.L)) + mul;
        const sint32 saturatedResult = std::clamp<sint64>(result, -0x80000000LL, 0x7FFFFFFFLL);
        if (result == saturatedResult) {
            MAC.L = result;
        } else {
            MAC.L = saturatedResult;
            MAC.H |= 1;
        }
    } else {
        MAC.u64 += mul;
    }

    AdvancePC<debug, emulateCache, delaySlot>();
    cycles += WritebackCycles(rm, rn);
    m_wbReg = kWBRegNone;
    return cycles + 1; // TODO: where does the + 1 come from?
}

// mac.l @Rm+, @Rn+
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MACL(uint16 opcode) {
    DECODE_NM

    const uint32 address2 = R[rn];
    uint64 cycles = AccessCycles<uint32, false, emulateCache>(address2);
    const sint64 op2 = static_cast<sint64>(static_cast<sint32>(MemReadLong<emulateCache>(address2)));
    R[rn] += 4;
    TracePopFromStack<debug>(m_tracer, rn, R[15]);

    const uint32 address1 = R[rm];
    cycles += AccessCycles<uint32, false, emulateCache>(address1);
    const sint64 op1 = static_cast<sint64>(static_cast<sint32>(MemReadLong<emulateCache>(address1)));
    R[rm] += 4;
    TracePopFromStack<debug>(m_tracer, rm, R[15]);

    const sint64 mul = op1 * op2;
    sint64 result = mul + MAC.u64;
    if (SR.S && result > 0x00007FFFFFFFFFFFull && result < 0xFFFF800000000000ull) {
        if (static_cast<sint32>(op1 ^ op2) < 0) {
            result = 0xFFFF800000000000ull;
        } else {
            result = 0x00007FFFFFFFFFFFull;
        }
    }
    MAC.u64 = result;

    AdvancePC<debug, emulateCache, delaySlot>();
    cycles += WritebackCycles(rm, rn);
    m_wbReg = kWBRegNone;
    return cycles + 1; // TODO: where does the + 1 come from?
}

// mul.l Rm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MULL(uint16 opcode) {
    DECODE_NM
    MAC.L = R[rm] * R[rn];
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm, rn) + 3; // TODO: 2 to 4, but how is that decided?
    m_wbReg = kWBRegNone;
    return cycles;
}

// muls.w Rm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MULS(uint16 opcode) {
    DECODE_NM
    MAC.L = bit::sign_extend<16>(R[rm]) * bit::sign_extend<16>(R[rn]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm, rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// mulu.w Rm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MULU(uint16 opcode) {
    DECODE_NM
    auto cast = [](uint32 val) { return static_cast<uint32>(static_cast<uint16>(val)); };
    MAC.L = cast(R[rm]) * cast(R[rn]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm, rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// dmuls.l Rm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::DMULS(uint16 opcode) {
    DECODE_NM
    auto cast = [](uint32 val) { return static_cast<sint64>(static_cast<sint32>(val)); };
    MAC.u64 = cast(R[rm]) * cast(R[rn]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm, rn) + 3; // TODO: 2 to 4, but how is that decided?
    m_wbReg = kWBRegNone;
    return cycles;
}

// dmulu.l Rm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::DMULU(uint16 opcode) {
    DECODE_NM
    MAC.u64 = static_cast<uint64>(R[rm]) * static_cast<uint64>(R[rn]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm, rn) + 3; // TODO: 2 to 4, but how is that decided?
    m_wbReg = kWBRegNone;
    return cycles;
}

// div0s r{}, Rm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::DIV0S(uint16 opcode) {
    DECODE_NM
    SR.M = static_cast<sint32>(R[rm]) < 0;
    SR.Q = static_cast<sint32>(R[rn]) < 0;
    SR.T = SR.M != SR.Q;
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm, rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// div0u
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::DIV0U() {
    SR.M = 0;
    SR.Q = 0;
    SR.T = 0;
    AdvancePC<debug, emulateCache, delaySlot>();
    m_wbReg = kWBRegNone;
    return 1;
}

// div1 Rm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::DIV1(uint16 opcode) {
    DECODE_NM
    const bool oldQ = SR.Q;
    const bool M = SR.M;
    bool Q = oldQ;
    Q = static_cast<sint32>(R[rn]) < 0;
    R[rn] = (R[rn] << 1u) | SR.T;

    const uint32 prevVal = R[rn];
    if (oldQ == M) {
        R[rn] -= R[rm];
    } else {
        R[rn] += R[rm];
    }

    if (oldQ) {
        if (M) {
            Q ^= R[rn] <= prevVal;
        } else {
            Q ^= R[rn] < prevVal;
        }
    } else {
        if (M) {
            Q ^= R[rn] >= prevVal;
        } else {
            Q ^= R[rn] > prevVal;
        }
    }

    SR.T = Q == M;
    SR.Q = Q;

    TraceChangeStack<debug>(m_tracer, rn, R[15]);

    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm, rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// cmp/eq #imm, R0
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::CMPIM(uint16 opcode) {
    DECODE_I_S(0)
    SR.T = static_cast<sint32>(R[0]) == imm;
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(0) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// cmp/eq Rm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::CMPEQ(uint16 opcode) {
    DECODE_NM
    SR.T = R[rn] == R[rm];
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm, rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// cmp/ge Rm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::CMPGE(uint16 opcode) {
    DECODE_NM
    SR.T = static_cast<sint32>(R[rn]) >= static_cast<sint32>(R[rm]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm, rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// cmp/gt Rm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::CMPGT(uint16 opcode) {
    DECODE_NM
    SR.T = static_cast<sint32>(R[rn]) > static_cast<sint32>(R[rm]);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm, rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// cmp/hi Rm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::CMPHI(uint16 opcode) {
    DECODE_NM
    SR.T = R[rn] > R[rm];
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm, rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// cmp/hs Rm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::CMPHS(uint16 opcode) {
    DECODE_NM
    SR.T = R[rn] >= R[rm];
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm, rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// cmp/pl Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::CMPPL(uint16 opcode) {
    DECODE_N
    SR.T = static_cast<sint32>(R[rn]) > 0;
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// cmp/pz Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::CMPPZ(uint16 opcode) {
    DECODE_N
    SR.T = static_cast<sint32>(R[rn]) >= 0;
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// cmp/str Rm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::CMPSTR(uint16 opcode) {
    DECODE_NM
    const uint32 tmp = R[rm] ^ R[rn];
    const uint8 hh = tmp >> 24u;
    const uint8 hl = tmp >> 16u;
    const uint8 lh = tmp >> 8u;
    const uint8 ll = tmp >> 0u;
    SR.T = !(hh && hl && lh && ll);
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm, rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// tas.b @Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::TAS(uint16 opcode) {
    DECODE_N
    const uint32 address = R[rn];
    const uint64 cycles = AccessCyclesRMWByte<emulateCache>(address) + WritebackCycles(rn) + 4;
    // TODO: enable bus lock on this read
    const uint8 tmp = MemReadByte<false>(address);
    SR.T = tmp == 0;
    // TODO: disable bus lock on this write
    MemWriteByte<debug, emulateCache>(address, tmp | 0x80);

    AdvancePC<debug, emulateCache, delaySlot>();
    m_wbReg = kWBRegNone;
    return cycles;
}

// tst Rm, Rn
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::TST(uint16 opcode) {
    DECODE_NM
    SR.T = (R[rn] & R[rm]) == 0;
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(rm, rn) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// tst #imm, R0
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::TSTI(uint16 opcode) {
    DECODE_I_U(0u)
    SR.T = (R[0] & imm) == 0;
    AdvancePC<debug, emulateCache, delaySlot>();
    const uint64 cycles = WritebackCycles(0) + 1;
    m_wbReg = kWBRegNone;
    return cycles;
}

// tst.b #imm, @(R0,GBR)
template <bool debug, bool emulateCache, bool delaySlot>
FORCE_INLINE uint64 SH2::TSTM(uint16 opcode) {
    DECODE_I_U(0u)
    const uint32 address = GBR + R[0];
    const uint64 cycles = AccessCycles<uint8, false, emulateCache>(address) + WritebackCycles(0) + 2;
    const uint8 tmp = MemReadByte<emulateCache>(address);
    SR.T = (tmp & imm) == 0;
    AdvancePC<debug, emulateCache, delaySlot>();
    m_wbReg = kWBRegNone;
    return cycles;
}

// bf <label>
template <bool debug, bool emulateCache>
FORCE_INLINE uint64 SH2::BF(uint16 opcode) {
    DECODE_D_S(1)
    m_wbReg = kWBRegNone;
    if (!SR.T) {
        const uint32 target = PC + disp + 4;
        TraceBranch<debug>(m_tracer, PC, target);
        PC = target;
        RefillPipeline<emulateCache>();
        return 3;
    } else {
        PC += 2;
        return 1;
    }
}

// bf/s <label>
template <bool debug>
FORCE_INLINE uint64 SH2::BFS(uint16 opcode) {
    DECODE_D_S(1)
    if (!SR.T) {
        const uint32 target = PC + disp + 4;
        TraceBranchDelay<debug>(m_tracer, target);
        SetupDelaySlot(target);
    }
    PC += 2;
    m_wbReg = kWBRegNone;
    return !SR.T ? 2 : 1;
}

// bt <label>
template <bool debug, bool emulateCache>
FORCE_INLINE uint64 SH2::BT(uint16 opcode) {
    DECODE_D_S(1)
    m_wbReg = kWBRegNone;
    if (SR.T) {
        const uint32 target = PC + disp + 4;
        TraceBranch<debug>(m_tracer, PC, target);
        PC = target;
        RefillPipeline<emulateCache>();
        return 3;
    } else {
        PC += 2;
        return 1;
    }
}

// bt/s <label>
template <bool debug>
FORCE_INLINE uint64 SH2::BTS(uint16 opcode) {
    DECODE_D_S(1)
    if (SR.T) {
        const uint32 target = PC + disp + 4;
        TraceBranchDelay<debug>(m_tracer, target);
        SetupDelaySlot(target);
    }
    PC += 2;
    m_wbReg = kWBRegNone;
    return SR.T ? 2 : 1;
}

// bra <label>
template <bool debug>
FORCE_INLINE uint64 SH2::BRA(uint16 opcode) {
    DECODE_D12(1)
    const uint32 target = PC + disp + 4;
    TraceBranchDelay<debug>(m_tracer, target);
    SetupDelaySlot(target);
    PC += 2;
    m_wbReg = kWBRegNone;
    return 2;
}

// braf Rm
template <bool debug>
FORCE_INLINE uint64 SH2::BRAF(uint16 opcode) {
    DECODE_M
    const uint32 target = PC + R[rm] + 4;
    TraceBranchDelay<debug>(m_tracer, target);
    SetupDelaySlot(target);
    PC += 2;
    const uint64 cycles = WritebackCycles(rm) + 2;
    m_wbReg = kWBRegNone;
    return cycles;
}

// bsr <label>
template <bool debug>
FORCE_INLINE uint64 SH2::BSR(uint16 opcode) {
    DECODE_D12(1)
    PR = PC + 4;
    const uint32 target = PC + disp + 4;
    TraceCall<debug>(m_tracer, target);
    SetupDelaySlot(target);
    PC += 2;
    const uint64 cycles = WritebackCycles(kWBRegPR) + 2;
    m_wbReg = kWBRegNone;
    return cycles;
}

// bsrf Rm
template <bool debug>
FORCE_INLINE uint64 SH2::BSRF(uint16 opcode) {
    DECODE_M
    PR = PC + 4;
    const uint32 target = PC + R[rm] + 4;
    TraceCall<debug>(m_tracer, target);
    SetupDelaySlot(target);
    PC += 2;
    const uint64 cycles = WritebackCycles(rm, kWBRegPR) + 2;
    m_wbReg = kWBRegNone;
    return cycles;
}

// jmp @Rm
template <bool debug>
FORCE_INLINE uint64 SH2::JMP(uint16 opcode) {
    DECODE_M
    const uint32 target = R[rm];
    TraceBranchDelay<debug>(m_tracer, target);
    SetupDelaySlot(R[rm]);
    PC += 2;
    const uint64 cycles = WritebackCycles(rm) + 2;
    m_wbReg = kWBRegNone;
    return cycles;
}

// jsr @Rm
template <bool debug>
FORCE_INLINE uint64 SH2::JSR(uint16 opcode) {
    DECODE_M
    PR = PC + 4;
    const uint32 target = R[rm];
    TraceCall<debug>(m_tracer, target);
    SetupDelaySlot(target);
    PC += 2;
    const uint64 cycles = WritebackCycles(rm, kWBRegPR) + 2;
    m_wbReg = kWBRegNone;
    return cycles;
}

// trapa #imm
template <bool debug, bool emulateCache>
FORCE_INLINE uint64 SH2::TRAPA(uint16 opcode) {
    DECODE_I_U(2u)
    devlog::trace<grp::intr>(m_logPrefix, "[PC = {:08X}] Handling TRAPA, vector number {:02X}", PC, imm >> 2u);
    const uint32 address1 = R[15] - 4;
    const uint32 address2 = R[15] - 8;
    const uint32 address3 = VBR + imm;
    const uint64 cycles = AccessCycles<uint32, true, emulateCache>(address1) +
                          AccessCycles<uint32, true, emulateCache>(address2) +
                          AccessCycles<uint32, false, emulateCache>(address3) + 5;
    MemWriteLong<debug, emulateCache>(address1, SR.u32);
    MemWriteLong<debug, emulateCache>(address2, PC + 2);
    const uint32 target = MemReadLong<emulateCache>(address3);
    TraceTrap<debug>(m_tracer, imm >> 2u, PC, R[15], target);
    PC = target;
    RefillPipeline<emulateCache>();
    R[15] -= 8;
    devlog::trace<grp::intr>(m_logPrefix, "[PC = {:08X}] Entering TRAPA handler", PC);
    m_wbReg = kWBRegNone;
    return cycles;
}

// rte
template <bool debug, bool emulateCache>
FORCE_INLINE uint64 SH2::RTE() {
    const uint32 address1 = R[15];
    const uint32 address2 = R[15] + 4;
    const uint64 cycles = AccessCycles<uint32, false, emulateCache>(address1) +
                          AccessCycles<uint32, false, emulateCache>(address2) + WritebackCycles(15) + 2;
    const uint32 target = MemReadLong<emulateCache>(address1);
    TraceReturnFromException<debug>(m_tracer, target, R[15] + 8);
    SetupDelaySlot(target);
    SR.u32 = MemReadLong<emulateCache>(address2) & 0x000003F3;
    PC += 2;
    R[15] += 8;
    devlog::trace<grp::intr>(m_logPrefix, "[PC = {:08X}] Returning from exception handler, PC -> {:08X}", PC,
                             m_delaySlotTarget);
    m_wbReg = kWBRegNone;
    return cycles;
}

// rts
template <bool debug>
FORCE_INLINE uint64 SH2::RTS() {
    TraceReturn<debug>(m_tracer, PR);
    SetupDelaySlot(PR);
    PC += 2;
    const uint64 cycles = WritebackCycles(kWBRegPR) + 2;
    m_wbReg = kWBRegNone;
    return cycles;
}

#undef DECODE_RN8
#undef DECODE_RM8
#undef DECODE_RN4
#undef DECODE_RM4
#undef DECODE_UDISP4
#undef DECODE_UDISP8
#undef DECODE_UIMM8
#undef DECODE_SDISP8
#undef DECODE_SIMM8
#undef DECODE_SDISP12

#undef DECODE_N
#undef DECODE_M
#undef DECODE_NM
#undef DECODE_MD
#undef DECODE_ND4
#undef DECODE_NMD
#undef DECODE_D_U
#undef DECODE_D_S
#undef DECODE_D12
#undef DECODE_ND8
#undef DECODE_I_U
#undef DECODE_I_S
#undef DECODE_NI

// -----------------------------------------------------------------------------
// Probe implementation

SH2::Probe::Probe(SH2 &sh2)
    : m_sh2(sh2) {}

uint16 SH2::Probe::FetchInstruction(uint32 address, bool bypassCache) const {
    if (*m_sh2.m_emulateCache && !bypassCache) {
        return m_sh2.FetchInstruction<true>(address);
    } else {
        return m_sh2.FetchInstruction<false>(address);
    }
}

uint8 SH2::Probe::MemReadByte(uint32 address, bool bypassCache) const {
    if (*m_sh2.m_emulateCache && !bypassCache) {
        return m_sh2.MemReadByte<true>(address);
    } else {
        return m_sh2.MemReadByte<false>(address);
    }
}

uint16 SH2::Probe::MemReadWord(uint32 address, bool bypassCache) const {
    if (*m_sh2.m_emulateCache && !bypassCache) {
        return m_sh2.MemReadWord<true>(address);
    } else {
        return m_sh2.MemReadWord<false>(address);
    }
}

uint32 SH2::Probe::MemReadLong(uint32 address, bool bypassCache) const {
    if (*m_sh2.m_emulateCache && !bypassCache) {
        return m_sh2.MemReadLong<true>(address);
    } else {
        return m_sh2.MemReadLong<false>(address);
    }
}

void SH2::Probe::MemWriteByte(uint32 address, uint8 value, bool bypassCache) {
    if (*m_sh2.m_emulateCache && !bypassCache) {
        m_sh2.MemWriteByte<false, true>(address, value);
    } else {
        m_sh2.MemWriteByte<false, false>(address, value);
    }
}

void SH2::Probe::MemWriteWord(uint32 address, uint16 value, bool bypassCache) {
    if (*m_sh2.m_emulateCache && !bypassCache) {
        m_sh2.MemWriteWord<false, true>(address, value);
    } else {
        m_sh2.MemWriteWord<false, false>(address, value);
    }
}

void SH2::Probe::MemWriteLong(uint32 address, uint32 value, bool bypassCache) {
    if (*m_sh2.m_emulateCache && !bypassCache) {
        m_sh2.MemWriteLong<false, true>(address, value);
    } else {
        m_sh2.MemWriteLong<false, false>(address, value);
    }
}

uint16 SH2::Probe::PeekInstruction(uint32 address, bool bypassCache) const {
    if (*m_sh2.m_emulateCache && !bypassCache) {
        return m_sh2.PeekInstruction<true>(address);
    } else {
        return m_sh2.PeekInstruction<false>(address);
    }
}

uint8 SH2::Probe::MemPeekByte(uint32 address, bool bypassCache) const {
    if (*m_sh2.m_emulateCache && !bypassCache) {
        return m_sh2.MemPeekByte<true>(address);
    } else {
        return m_sh2.MemPeekByte<false>(address);
    }
}

uint16 SH2::Probe::MemPeekWord(uint32 address, bool bypassCache) const {
    if (*m_sh2.m_emulateCache && !bypassCache) {
        return m_sh2.MemPeekWord<true>(address);
    } else {
        return m_sh2.MemPeekWord<false>(address);
    }
}

uint32 SH2::Probe::MemPeekLong(uint32 address, bool bypassCache) const {
    if (*m_sh2.m_emulateCache && !bypassCache) {
        return m_sh2.MemPeekLong<true>(address);
    } else {
        return m_sh2.MemPeekLong<false>(address);
    }
}

void SH2::Probe::MemPokeByte(uint32 address, uint8 value, bool bypassCache) {
    if (*m_sh2.m_emulateCache && !bypassCache) {
        m_sh2.MemPokeByte<true>(address, value);
    } else {
        m_sh2.MemPokeByte<false>(address, value);
    }
}

void SH2::Probe::MemPokeWord(uint32 address, uint16 value, bool bypassCache) {
    if (*m_sh2.m_emulateCache && !bypassCache) {
        m_sh2.MemPokeWord<true>(address, value);
    } else {
        m_sh2.MemPokeWord<false>(address, value);
    }
}

void SH2::Probe::MemPokeLong(uint32 address, uint32 value, bool bypassCache) {
    if (*m_sh2.m_emulateCache && !bypassCache) {
        m_sh2.MemPokeLong<true>(address, value);
    } else {
        m_sh2.MemPokeLong<false>(address, value);
    }
}

bool SH2::Probe::IsInDelaySlot() const {
    return m_sh2.m_delaySlot;
}

uint32 SH2::Probe::DelaySlotTarget() const {
    return m_sh2.m_delaySlotTarget;
}

bool SH2::Probe::GetSleepState() const {
    return m_sh2.m_sleep;
}

void SH2::Probe::SetSleepState(bool sleep) {
    m_sh2.m_sleep = sleep;
}

void SH2::Probe::RefillPipeline() {
    if (*m_sh2.m_emulateCache) {
        m_sh2.RefillPipeline<true>();
    } else {
        m_sh2.RefillPipeline<false>();
    }
}

void SH2::Probe::ExecuteDiv32() {
    m_sh2.ExecuteDiv32<true>();
}

void SH2::Probe::ExecuteDiv64() {
    m_sh2.ExecuteDiv64<true>();
}

} // namespace ymir::sh2
