#pragma once

#include "sh2_excpt.hpp"
#include "sh2_regs.hpp"

#include "sh2_decode.hpp"

#include "sh2_bsc.hpp"
#include "sh2_cache.hpp"
#include "sh2_divu.hpp"
#include "sh2_dmac.hpp"
#include "sh2_frt.hpp"
#include "sh2_intc.hpp"
#include "sh2_power.hpp"
#include "sh2_sci.hpp"
#include "sh2_ubc.hpp"
#include "sh2_wdt.hpp"

#include <ymir/hw/hw_defs.hpp>

#include <ymir/hw/scu/scu_internal_callbacks.hpp>
#include <ymir/hw/sh2/sh2_internal_callbacks.hpp>

#include <ymir/sys/bus.hpp>

#include <ymir/savestate/savestate_sh2.hpp>

#include <ymir/debug/debug_break.hpp>
#include <ymir/debug/sh2_tracer_base.hpp>
#include <ymir/debug/watchpoint_defs.hpp>

#include <ymir/core/types.hpp>

#include <ymir/util/inline.hpp>
#include <ymir/util/virtual_memory.hpp>

#include <array>
#include <bitset>
#include <iosfwd>
#include <map>
#include <set>

namespace ymir::sh2 {

// -----------------------------------------------------------------------------

class SH2 {
public:
    SH2(sys::SH2Bus &bus, bool master);

    void Reset(bool hard, bool watchdogInitiated = false);

    void MapCallbacks(CBAcknowledgeExternalInterrupt callback) {
        m_cbAcknowledgeExternalInterrupt = callback;
    }

    /// @brief Connects the SCI transmitter to an external serial device.
    void SetSCITransmitCallback(CBSerialTransmit callback) {
        m_cbSCITransmit = callback;
    }

    /// @brief Connects the SCI receiver to an external serial device.
    void SetSCIReceiveCallback(CBSerialReceive callback) {
        m_cbSCIReceive = callback;
    }

    /// @brief Delivers one byte to the SCI receiver on the emulation thread.
    /// @return true if the byte was accepted; false if reception is disabled or full.
    bool SCIReceiveByte(uint8 value);

    void BindGlobalCycleCounter(const uint64 &currCountRef) {
        m_currCount = &currCountRef;
    }

    void BindEmulateCacheOption(const bool &emulateCacheRef) {
        m_emulateCache = &emulateCacheRef;
    }

    void UseDebugBreakManager(debug::DebugBreakManager *mgr) {
        m_debugBreakMgr = mgr;
        if (mgr != nullptr) {
            m_breakpoints.Allocate();
            m_watchpoints.Allocate();
            ReapplyBreakpoints();
            ReapplyWatchpoints();
        } else {
            m_breakpoints.Free();
            m_watchpoints.Free();
        }
    }

    void MapMemory(sys::SH2Bus &bus);

    void DumpCacheData(std::ostream &out) const;
    void DumpCacheAddressTag(std::ostream &out) const;

    // -------------------------------------------------------------------------
    // Usage

    /// @brief Advances the SH2 for at least the specified number of cycles.
    /// @tparam debug whether to enable debug features
    /// @tparam emulateCache whether to emulate the cache
    /// @param[in] cycles the minimum number of cycles
    /// @param[in] spilloverCycles cycles spilled over from the previous execution
    /// @return the number of cycles actually executed
    template <bool debug, bool emulateCache>
    uint64 Advance(uint64 cycles, uint64 spilloverCycles = 0);

    // Executes a single instruction.
    // Returns the number of cycles executed.
    template <bool debug, bool emulateCache>
    uint64 Step();

    bool IsMaster() const {
        return !BCR1.MASTER;
    }

    bool GetNMI() const;
    void SetNMI();

    // Purges the contents of the cache.
    // Should be done before enabling cache emulation to ensure previous cache contents are cleared.
    void PurgeCache();

    // -------------------------------------------------------------------------
    // Save states

    void SaveState(savestate::SH2SaveState &state) const;
    [[nodiscard]] bool ValidateState(const savestate::SH2SaveState &state) const;
    void LoadState(const savestate::SH2SaveState &state);
    void PostLoadState(const savestate::SH2SaveState &state);

    // -------------------------------------------------------------------------
    // Debugger

    // Attaches the specified tracer to this component.
    // Pass nullptr to disable tracing.
    void UseTracer(debug::ISH2Tracer *tracer) {
        if (m_tracer != nullptr) {
            m_tracer->Detached();
        }
        m_tracer = tracer;
        if (tracer != nullptr) {
            tracer->Attached();
        }
    }

    // --- Breakpoints

private:
    // Returns a reference to the portion of the breakpoint bitmap containing the given address.
    FORCE_INLINE uint64 &BreakpointBitmapChunkRef(uint32 address) {
        address >>= 4u; // 1 bit for halfword alignment, 3 bits for 8 bits packed in a byte
        address &= ~(sizeof(uint64) - 1);
        return *m_breakpoints.GetPointer<uint64>(address);
    }

    // Returns the portion of the breakpoint bitmap containing the given address.
    FORCE_INLINE uint64 GetBreakpointBitmapChunk(uint32 address) const {
        address >>= 4u; // 1 bit for halfword alignment, 3 bits for 8 bits packed in a byte
        address &= ~(sizeof(uint64) - 1);
        return *m_breakpoints.GetPointer<uint64>(address);
    }

    // Builds a value containing the breakpoint bitmap bit for the given address.
    FORCE_INLINE static uint64 MakeBreakpointBit(uint32 address) {
        return 1ull << ((address >> 1u) & 63u);
    }

public:
    // Adds the specified address to the set of breakpoints.
    // The address is force-aligned to word boundaries.
    // Returns `true` if the breakpoint was added, `false` if it already exists.
    bool AddBreakpoint(uint32 address) {
        if (m_breakpoints.IsAllocated()) {
            BreakpointBitmapChunkRef(address) |= MakeBreakpointBit(address);
        }
        return m_breakpointSet.insert(address & ~1u).second;
    }

    // Removes the specified address from the set of breakpoints.
    // The address is force-aligned to word boundaries.
    // Returns `true` if the breakpoint was removed, `false` if it did not exist.
    bool RemoveBreakpoint(uint32 address) {
        if (m_breakpoints.IsAllocated()) {
            BreakpointBitmapChunkRef(address) &= ~MakeBreakpointBit(address);
        }
        return m_breakpointSet.erase(address & ~1u);
    }

    // Toggles the breakpoint at the specified address.
    // The address is force-aligned to word boundaries.
    // Returns `true` if the breakpoint was added, `false` if it was removed.
    bool ToggleBreakpoint(uint32 address) {
        if (m_breakpoints.IsAllocated()) {
            BreakpointBitmapChunkRef(address) ^= MakeBreakpointBit(address);
        }
        address &= ~1u;
        const bool result = m_breakpointSet.insert(address).second;
        if (!result) {
            m_breakpointSet.erase(address);
        }
        return result;
    }

    // Clears all breakpoints.
    void ClearBreakpoints() {
        if (m_breakpoints.IsAllocated()) {
            for (uint32 address : m_breakpointSet) {
                BreakpointBitmapChunkRef(address) &= ~MakeBreakpointBit(address);
            }
        }
        m_breakpointSet.clear();
    }

    // Retrieves all breakpoints set in this SH-2.
    const std::set<uint32> &GetBreakpoints() const {
        return m_breakpointSet;
    }

    // Replaces all breakpoints with those of the provided set.
    // All addresses of the specified set are force-aligned to word boundaries.
    void ReplaceBreakpoints(const std::set<uint32> &breakpoints) {
        // Manage breakpoints manually to sanitize addresses
        ClearBreakpoints();
        for (auto address : breakpoints) {
            AddBreakpoint(address);
        }
    }

    // Determines if a breakpoint is set at specified address.
    // The address is force-aligned to word boundaries.
    FORCE_INLINE bool IsBreakpointSet(uint32 address) const {
        return m_breakpointSet.contains(address & ~1);
    }

private:
    // Determines if a breakpoint is set at specified address using the fast bitmap.
    // The address is force-aligned to word boundaries.
    // Must only be invoked if the bitmap is allocated.
    FORCE_INLINE bool IsBreakpointSetInBitmap(uint32 address) const {
        return GetBreakpointBitmapChunk(address) & MakeBreakpointBit(address);
    }

    // Reapplies the breakpoints from the set into the bitmap.
    // Should be used after reallocating the bitmap.
    FORCE_INLINE void ReapplyBreakpoints() {
        if (!m_breakpoints.IsAllocated()) {
            return;
        }
        for (auto address : m_breakpointSet) {
            AddBreakpoint(address);
        }
    }

    // --- Watchpoints

private:
    // Returns a reference to the watchpoint flags for the given address.
    FORCE_INLINE debug::WatchpointFlags &WatchpointFlagsRef(uint32 address) {
        return *m_watchpoints.GetPointer<debug::WatchpointFlags>(address);
    }

    // Returns the watchpoint flags for the given address.
    template <mem_primitive T>
    FORCE_INLINE T GetWatchpointFlags(uint32 address) const {
        return *m_watchpoints.GetPointer<T>(address);
    }

public:
    // Adds watchpoints to the specified address.
    // Watchpoints set at misaligned addresses will not trigger.
    // e.g. longword-sized watchpoint with address & 3 != 0
    void AddWatchpoint(uint32 address, debug::WatchpointFlags flags) {
        if (m_watchpoints.IsAllocated()) {
            WatchpointFlagsRef(address) |= flags;
        }
        if (flags != debug::WatchpointFlags::None) {
            m_watchpointSet[address] |= flags;
        }
    }

    // Removes watchpoints from the specified address.
    void RemoveWatchpoint(uint32 address, debug::WatchpointFlags flags) {
        if (m_watchpoints.IsAllocated()) {
            WatchpointFlagsRef(address) &= ~flags;
        }
        if (!m_watchpointSet.contains(address)) {
            return;
        }
        auto &wtpt = m_watchpointSet.at(address);
        wtpt &= ~flags;
        if (wtpt == debug::WatchpointFlags::None) {
            m_watchpointSet.erase(address);
        }
    }

    // Clears all watchpoints at the specified address.
    void ClearWatchpointsAt(uint32 address) {
        if (m_watchpoints.IsAllocated()) {
            WatchpointFlagsRef(address) = debug::WatchpointFlags::None;
        }
        m_watchpointSet.erase(address);
    }

    // Clears all watchpoints.
    void ClearWatchpoints() {
        if (m_watchpoints.IsAllocated()) {
            for (auto [address, _] : m_watchpointSet) {
                WatchpointFlagsRef(address) = debug::WatchpointFlags::None;
            }
        }
        m_watchpointSet.clear();
    }

    // Retrieves configured watchpoints for the specified address.
    FORCE_INLINE debug::WatchpointFlags GetWatchpoint(uint32 address) const {
        auto it = m_watchpointSet.find(address);
        return it != m_watchpointSet.cend() ? it->second : debug::WatchpointFlags::None;
    }

private:
    // Reapplies the watchpoints from the set into the memory map.
    // Should be used after reallocating the memory map.
    FORCE_INLINE void ReapplyWatchpoints() {
        if (!m_watchpoints.IsAllocated()) {
            return;
        }
        for (auto &[address, flags] : m_watchpointSet) {
            AddWatchpoint(address, flags);
        }
    }

public:
    // Retrieves all watchpoints set in this SH-2.
    const std::map<uint32, debug::WatchpointFlags> &GetWatchpoints() const {
        return m_watchpointSet;
    }

    // Replaces all watchpoints with those of the provided set.
    // All addresses of the specified set are force-aligned to word boundaries.
    void ReplaceWatchpoints(const std::map<uint32, debug::WatchpointFlags> &watchpoints) {
        // Manage watchpoints manually to sanitize addresses
        ClearWatchpoints();
        for (auto &[address, flags] : watchpoints) {
            AddWatchpoint(address, flags);
        }
    }

    // --- Misc

    // Suspends (disables) the CPU in debug mode.
    void SetCPUSuspended(bool suspend) {
        m_debugSuspend = suspend;
    }

    // Determines if the CPU is suspended in debug mode.
    bool IsCPUSuspended() const {
        return m_debugSuspend;
    }

    class Probe {
    public:
        Probe(SH2 &sh2);

        // ---------------------------------------------------------------------
        // Registers

        FORCE_INLINE std::array<uint32, 16> &R() {
            return m_sh2.R;
        }
        FORCE_INLINE const std::array<uint32, 16> &R() const {
            return m_sh2.R;
        }

        FORCE_INLINE uint32 &R(uint8 rn) {
            assert(rn <= 15);
            return m_sh2.R[rn];
        }
        FORCE_INLINE const uint32 &R(uint8 rn) const {
            assert(rn <= 15);
            return m_sh2.R[rn];
        }

        FORCE_INLINE uint32 &PC() {
            return m_sh2.PC;
        }
        FORCE_INLINE uint32 PC() const {
            return m_sh2.PC;
        }

        FORCE_INLINE uint32 &PR() {
            return m_sh2.PR;
        }
        FORCE_INLINE uint32 PR() const {
            return m_sh2.PR;
        }

        FORCE_INLINE RegMAC &MAC() {
            return m_sh2.MAC;
        }
        FORCE_INLINE RegMAC MAC() const {
            return m_sh2.MAC;
        }

        FORCE_INLINE RegSR &SR() {
            return m_sh2.SR;
        }
        FORCE_INLINE RegSR SR() const {
            return m_sh2.SR;
        }

        FORCE_INLINE uint32 &GBR() {
            return m_sh2.GBR;
        }
        FORCE_INLINE uint32 GBR() const {
            return m_sh2.GBR;
        }

        FORCE_INLINE uint32 &VBR() {
            return m_sh2.VBR;
        }
        FORCE_INLINE uint32 VBR() const {
            return m_sh2.VBR;
        }

        // ---------------------------------------------------------------------
        // Regular memory accessors (with side-effects)

        uint16 FetchInstruction(uint32 address, bool bypassCache) const;

        uint8 MemReadByte(uint32 address, bool bypassCache) const;
        uint16 MemReadWord(uint32 address, bool bypassCache) const;
        uint32 MemReadLong(uint32 address, bool bypassCache) const;

        void MemWriteByte(uint32 address, uint8 value, bool bypassCache);
        void MemWriteWord(uint32 address, uint16 value, bool bypassCache);
        void MemWriteLong(uint32 address, uint32 value, bool bypassCache);

        // ---------------------------------------------------------------------
        // Debug memory accessors (without side-effects)

        uint16 PeekInstruction(uint32 address, bool bypassCache) const;

        uint8 MemPeekByte(uint32 address, bool bypassCache) const;
        uint16 MemPeekWord(uint32 address, bool bypassCache) const;
        uint32 MemPeekLong(uint32 address, bool bypassCache) const;

        void MemPokeByte(uint32 address, uint8 value, bool bypassCache);
        void MemPokeWord(uint32 address, uint16 value, bool bypassCache);
        void MemPokeLong(uint32 address, uint32 value, bool bypassCache);

        // ---------------------------------------------------------------------
        // Execution state

        bool IsInDelaySlot() const;
        uint32 DelaySlotTarget() const;

        bool GetSleepState() const;
        void SetSleepState(bool sleep);

        void RefillPipeline();

        // ---------------------------------------------------------------------
        // On-chip peripheral registers

        FORCE_INLINE DivisionUnit &DIVU() {
            return m_sh2.DIVU;
        }

        FORCE_INLINE const DivisionUnit &DIVU() const {
            return m_sh2.DIVU;
        }

        FORCE_INLINE InterruptController &INTC() {
            return m_sh2.INTC;
        }

        FORCE_INLINE const InterruptController &INTC() const {
            return m_sh2.INTC;
        }

        FORCE_INLINE FreeRunningTimer &FRT() {
            return m_sh2.FRT;
        }

        FORCE_INLINE const FreeRunningTimer &FRT() const {
            return m_sh2.FRT;
        }

        FORCE_INLINE WatchdogTimer &WDT() {
            return m_sh2.WDT;
        }

        FORCE_INLINE const WatchdogTimer &WDT() const {
            return m_sh2.WDT;
        }

        FORCE_INLINE RegSBYCR &SBYCR() {
            return m_sh2.SBYCR;
        }

        FORCE_INLINE const RegSBYCR &SBYCR() const {
            return m_sh2.SBYCR;
        }

        FORCE_INLINE DMAChannel &DMAC0() {
            return m_sh2.m_dmaChannels[0];
        }

        FORCE_INLINE const DMAChannel &DMAC0() const {
            return m_sh2.m_dmaChannels[0];
        }

        FORCE_INLINE DMAChannel &DMAC1() {
            return m_sh2.m_dmaChannels[1];
        }

        FORCE_INLINE const DMAChannel &DMAC1() const {
            return m_sh2.m_dmaChannels[1];
        }

        FORCE_INLINE RegDMAOR &DMAOR() {
            return m_sh2.DMAOR;
        }

        FORCE_INLINE const RegDMAOR &DMAOR() const {
            return m_sh2.DMAOR;
        }

        // ---------------------------------------------------------------------
        // Cache

        Cache &GetCache() {
            return m_sh2.m_cache;
        }

        const Cache &GetCache() const {
            return m_sh2.m_cache;
        }

        // ---------------------------------------------------------------------
        // Division unit

        void ExecuteDiv32();
        void ExecuteDiv64();

        // ---------------------------------------------------------------------
        // Interrupts

        // Raise an interrupt, also setting the corresponding signals.
        FORCE_INLINE void RaiseInterrupt(InterruptSource source) {
            // Set the corresponding signals
            switch (source) {
            case InterruptSource::None: break;
            case InterruptSource::FRT_OVI:
                m_sh2.FRT.FTCSR.OVF = 1;
                m_sh2.FRT.TIER.OVIE = 1;
                break;
            case InterruptSource::FRT_OCI:
                m_sh2.FRT.FTCSR.OCFA = 1;
                m_sh2.FRT.TIER.OCIAE = 1;
                break;
            case InterruptSource::FRT_ICI:
                m_sh2.FRT.FTCSR.ICF = 1;
                m_sh2.FRT.TIER.ICIE = 1;
                break;
            case InterruptSource::SCI_TEI: /*TODO*/ break;
            case InterruptSource::SCI_TXI: /*TODO*/ break;
            case InterruptSource::SCI_RXI: /*TODO*/ break;
            case InterruptSource::SCI_ERI: /*TODO*/ break;
            case InterruptSource::BSC_REF_CMI: /*TODO*/ break;
            case InterruptSource::WDT_ITI:
                m_sh2.WDT.WTCSR.OVF = 1;
                m_sh2.WDT.WTCSR.WT_nIT = 0;
                break;
            case InterruptSource::DMAC1_XferEnd:
                m_sh2.m_dmaChannels[1].xferEnded = true;
                m_sh2.m_dmaChannels[1].irqEnable = true;
                break;
            case InterruptSource::DMAC0_XferEnd:
                m_sh2.m_dmaChannels[0].xferEnded = true;
                m_sh2.m_dmaChannels[0].irqEnable = true;
                break;
            case InterruptSource::DIVU_OVFI:
                m_sh2.DIVU.DVCR.OVF = 1;
                m_sh2.DIVU.DVCR.OVFIE = 1;
                break;
            case InterruptSource::IRL: /*relies on level being set*/ break;
            case InterruptSource::UserBreak: /*TODO*/ break;
            case InterruptSource::NMI: m_sh2.INTC.NMI = 1; break;
            }

            m_sh2.RaiseInterrupt(source);
        }

        // Lower an interrupt, also clearing the corresponding signals.
        FORCE_INLINE void LowerInterrupt(InterruptSource source) {
            // Clear the corresponding signals
            switch (source) {
            case InterruptSource::None: break;
            case InterruptSource::FRT_OVI: m_sh2.FRT.FTCSR.OVF = 0; break;
            case InterruptSource::FRT_OCI: m_sh2.FRT.FTCSR.OCFA = 0; break;
            case InterruptSource::FRT_ICI: m_sh2.FRT.FTCSR.ICF = 0; break;
            case InterruptSource::SCI_TEI: /*TODO*/ break;
            case InterruptSource::SCI_TXI: /*TODO*/ break;
            case InterruptSource::SCI_RXI: /*TODO*/ break;
            case InterruptSource::SCI_ERI: /*TODO*/ break;
            case InterruptSource::BSC_REF_CMI: /*TODO*/ break;
            case InterruptSource::WDT_ITI: m_sh2.WDT.WTCSR.OVF = 0; break;
            case InterruptSource::DMAC1_XferEnd: m_sh2.m_dmaChannels[1].xferEnded = false; break;
            case InterruptSource::DMAC0_XferEnd: m_sh2.m_dmaChannels[0].xferEnded = false; break;
            case InterruptSource::DIVU_OVFI: m_sh2.DIVU.DVCR.OVF = 0; break;
            case InterruptSource::IRL:
                m_sh2.INTC.SetLevel(sh2::InterruptSource::IRL, 0x0);
                m_sh2.INTC.UpdateIRLVector();
                break;
            case InterruptSource::UserBreak: /*TODO*/ break;
            case InterruptSource::NMI: m_sh2.INTC.NMI = 0; break;
            }

            m_sh2.LowerInterrupt(source);
        }

        // Determines if the given interrupt source signal is raised.
        FORCE_INLINE bool IsInterruptRaised(InterruptSource source) const {
            switch (source) {
            case InterruptSource::None: return false;
            case InterruptSource::FRT_OVI: return m_sh2.FRT.FTCSR.OVF && m_sh2.FRT.TIER.OVIE;
            case InterruptSource::FRT_OCI:
                return (m_sh2.FRT.FTCSR.OCFA && m_sh2.FRT.TIER.OCIAE) || (m_sh2.FRT.FTCSR.OCFB && m_sh2.FRT.TIER.OCIBE);
            case InterruptSource::FRT_ICI: return m_sh2.FRT.FTCSR.ICF && m_sh2.FRT.TIER.ICIE;
            case InterruptSource::SCI_TEI:
                return (m_sh2.SCI.SCR & SerialCommunicationInterface::kTEIE) &&
                       (m_sh2.SCI.SSR & SerialCommunicationInterface::kTEND);
            case InterruptSource::SCI_TXI:
                return (m_sh2.SCI.SCR & SerialCommunicationInterface::kTIE) &&
                       (m_sh2.SCI.SSR & SerialCommunicationInterface::kTDRE);
            case InterruptSource::SCI_RXI:
                return (m_sh2.SCI.SCR & SerialCommunicationInterface::kRIE) &&
                       (m_sh2.SCI.SSR & SerialCommunicationInterface::kRDRF);
            case InterruptSource::SCI_ERI:
                return (m_sh2.SCI.SCR & SerialCommunicationInterface::kRIE) && (m_sh2.SCI.SSR & 0x38);
            case InterruptSource::BSC_REF_CMI: return false; // TODO
            case InterruptSource::WDT_ITI: return m_sh2.WDT.WTCSR.OVF && !m_sh2.WDT.WTCSR.WT_nIT;
            case InterruptSource::DMAC1_XferEnd:
                return m_sh2.m_dmaChannels[1].xferEnded && m_sh2.m_dmaChannels[1].irqEnable;
            case InterruptSource::DMAC0_XferEnd:
                return m_sh2.m_dmaChannels[0].xferEnded && m_sh2.m_dmaChannels[0].irqEnable;
            case InterruptSource::DIVU_OVFI: return m_sh2.DIVU.DVCR.OVF && m_sh2.DIVU.DVCR.OVFIE;
            case InterruptSource::IRL: return m_sh2.INTC.GetLevel(InterruptSource::IRL) > 0;
            case InterruptSource::UserBreak: return false; // TODO
            case InterruptSource::NMI: return m_sh2.INTC.NMI;
            default: return false;
            }
        }

        // Check if the CPU should service an interrupt.
        // Takes into account the current SR.ILevel and delay slot state.
        FORCE_INLINE bool CheckInterrupts() const {
            return m_sh2.m_intrFlags.pending;
        }

    private:
        SH2 &m_sh2;
    };

    Probe &GetProbe() {
        return m_probe;
    }

    const Probe &GetProbe() const {
        return m_probe;
    }

private:
    // -------------------------------------------------------------------------
    // CPU state

    // R0 through R15.
    // R15 is also used as the hardware stack pointer (SP).
    alignas(64) std::array<uint32, 16> R;

    uint32 PC;
    uint32 PR;

    RegMAC MAC;

    RegSR SR;

    uint32 GBR;
    uint32 VBR;

    uint32 m_delaySlotTarget;
    bool m_delaySlot;

    // Raw opcodes fetched from memory.
    // The CPU always does aligned 32-bit instruction fetches pulling in a pair of 16-bit instructions.
    uint32 m_fetchedOpcodes;

    static constexpr uint8 kWBRegNone = 0xFF;
    static constexpr uint8 kWBRegPR = 0x10;

    CBAcknowledgeExternalInterrupt m_cbAcknowledgeExternalInterrupt;
    debug::DebugBreakManager *m_debugBreakMgr = nullptr;

    // -------------------------------------------------------------------------
    // Configuration

    static constexpr bool kNilEmulateCache = false;

    // Pointer to externally-managed "emulate SH2 cache" option, used for save states and the debugger probe.
    // Advance and Step always honor the `emulateCache` template flag.
    const bool *m_emulateCache = &kNilEmulateCache;

    // -------------------------------------------------------------------------
    // Cycle counting

    static constexpr uint64 kNilCurrCounter = 0ull;

    // Pointer to global (absolute) cycle counter
    const uint64 *m_currCount = &kNilCurrCounter;

    // Number of cycles executed in the current Advance invocation
    uint64 m_cyclesExecuted;

    // Retrieves the current absolute cycle count
    uint64 GetCurrentCycleCount() const;

    // Register currently held by the WB stage:
    //   0x0..0xF: r0..r15
    //       0x10: PR
    //  otherwise: none
    uint8 m_wbReg;

    // Computes the number of extra cycles taken by the WB stage: 0 if there is no contention with any of the listed
    // registers, or 1 if one of the registers is in use by the stage.
    template <std::integral... Ts>
    uint64 WritebackCycles(Ts... regs);

    // -------------------------------------------------------------------------
    // Memory accessors

    sys::SH2Bus &m_bus;

    // According to the SH7604/SH7095 manuals, the address space is divided into these areas:
    //
    // Address range            Space                           Memory
    // 0x00000000..0x01FFFFFF   CS0 space, cache area           Ordinary space or burst ROM
    // 0x02000000..0x03FFFFFF   CS1 space, cache area           Ordinary space
    // 0x04000000..0x05FFFFFF   CS2 space, cache area           Ordinary space or synchronous DRAM
    // 0x06000000..0x07FFFFFF   CS3 space, cache area           Ordinary space, synchronous SDRAM, DRAM or pseudo-DRAM
    // 0x08000000..0x1FFFFFFF   Reserved
    // 0x20000000..0x21FFFFFF   CS0 space, cache-through area   Ordinary space or burst ROM
    // 0x22000000..0x23FFFFFF   CS1 space, cache-through area   Ordinary space
    // 0x24000000..0x25FFFFFF   CS2 space, cache-through area   Ordinary space or synchronous DRAM
    // 0x26000000..0x27FFFFFF   CS3 space, cache-through area   Ordinary space, synchronous SDRAM, DRAM or pseudo-DRAM
    // 0x28000000..0x3FFFFFFF   Reserved
    // 0x40000000..0x47FFFFFF   Associative purge space
    // 0x48000000..0x5FFFFFFF   Reserved
    // 0x60000000..0x7FFFFFFF   Address array, read/write space
    // 0x80000000..0x9FFFFFFF   Reserved  [undocumented mirror of 0xC0000000..0xDFFFFFFF]
    // 0xA0000000..0xBFFFFFFF   Reserved  [undocumented mirror of 0x20000000..0x3FFFFFFF]
    // 0xC0000000..0xC0000FFF   Data array, read/write space
    // 0xC0001000..0xDFFFFFFF   Reserved
    // 0xE0000000..0xFFFF7FFF   Reserved
    // 0xFFFF8000..0xFFFFBFFF   For setting synchronous DRAM mode
    // 0xFFFFC000..0xFFFFFDFF   Reserved
    // 0xFFFFFE00..0xFFFFFFFF   On-chip peripheral modules
    //
    // The cache uses address bits 31..29 to specify its behavior:
    //    Bits  Partition                       Cache operation
    //    000   Cache area                      Cache used when CCR.CE=1
    //    001   Cache-through area              Cache bypassed
    //    010   Associative purge area          Purge accessed cache lines (reads return 0x2312)
    //    011   Address array read/write area   Cache addresses acessed directly (1 KiB, mirrored)
    //    100   [undocumented, same as 110]
    //    101   [undocumented, same as 001]
    //    110   Data array read/write area      Cache data acessed directly (4 KiB, mirrored)
    //    111   I/O area (on-chip registers)    Cache bypassed

    template <mem_primitive T, bool instrFetch, bool peek, bool emulateCache>
    T MemRead(uint32 address);

    template <mem_primitive T, bool poke, bool debug, bool emulateCache>
    void MemWrite(uint32 address, T value);

    template <bool emulateCache>
    uint16 FetchInstruction(uint32 address);
    template <bool emulateCache>
    void RefillPipeline();

    template <bool emulateCache>
    uint8 MemReadByte(uint32 address);
    template <bool emulateCache, bool instrFetch = false>
    uint16 MemReadWord(uint32 address);
    template <bool emulateCache, bool instrFetch = false>
    uint32 MemReadLong(uint32 address);

    template <bool debug, bool emulateCache>
    void MemWriteByte(uint32 address, uint8 value);
    template <bool debug, bool emulateCache>
    void MemWriteWord(uint32 address, uint16 value);
    template <bool debug, bool emulateCache>
    void MemWriteLong(uint32 address, uint32 value);

    template <bool emulateCache>
    uint16 PeekInstruction(uint32 address);

    template <bool emulateCache>
    uint8 MemPeekByte(uint32 address);
    template <bool emulateCache>
    uint16 MemPeekWord(uint32 address);
    template <bool emulateCache>
    uint32 MemPeekLong(uint32 address);

    template <bool emulateCache>
    void MemPokeByte(uint32 address, uint8 value);
    template <bool emulateCache>
    void MemPokeWord(uint32 address, uint16 value);
    template <bool emulateCache>
    void MemPokeLong(uint32 address, uint32 value);

    // Returns 00 00 00 01 00 02 00 03 00 04 00 05 00 06 00 07 ... repeating
    template <mem_primitive T>
    T OpenBusSeqRead(uint32 address);

    template <mem_primitive T, bool write, bool emulateCache>
    uint64 AccessCycles(uint32 address);

    template <bool emulateCache>
    uint64 AccessCyclesRMWByte(uint32 address);

    // -------------------------------------------------------------------------
    // On-chip peripherals

    template <mem_primitive T, bool peek>
    T OnChipRegRead(uint32 address);

    template <bool peek>
    uint8 OnChipRegReadByte(uint32 address);
    template <bool peek>
    uint16 OnChipRegReadWord(uint32 address);
    template <bool peek>
    uint32 OnChipRegReadLong(uint32 address);

    template <mem_primitive T, bool poke, bool debug, bool emulateCache>
    void OnChipRegWrite(uint32 address, T value);

    template <bool poke, bool debug, bool emulateCache>
    void OnChipRegWriteByte(uint32 address, uint8 value);
    template <bool poke, bool debug, bool emulateCache>
    void OnChipRegWriteWord(uint32 address, uint16 value);
    template <bool poke, bool debug, bool emulateCache>
    void OnChipRegWriteLong(uint32 address, uint32 value);

    // --- SCI module ---

    SerialCommunicationInterface SCI;
    CBSerialTransmit m_cbSCITransmit;
    CBSerialReceive m_cbSCIReceive;

    void SCITransmitByte(uint8 value);
    void PollSCIReceive();

    // --- BSC module ---

    RegBCR1 BCR1;   // 1E0  R/W  16,32    03F0      BCR1    Bus Control Register 1
    RegBCR2 BCR2;   // 1E4  R/W  16,32    00FC      BCR2    Bus Control Register 2
    RegWCR WCR;     // 1E8  R/W  16,32    AAFF      WCR     Wait Control Register
    RegMCR MCR;     // 1EC  R/W  16,32    0000      MCR     Individual Memory Control Register
    RegRTCSR RTCSR; // 1F0  R/W  16,32    0000      RTCSR   Refresh Timer Control/Status Register
    RegRTCNT RTCNT; // 1F4  R/W  16,32    0000      RTCNT   Refresh Timer Counter
    RegRTCOR RTCOR; // 1F8  R/W  16,32    0000      RTCOR   Refresh Timer Constant Register

    // --- DMAC module ---

    RegDMAOR DMAOR;
    std::array<DMAChannel, 2> m_dmaChannels;

    // Determines if a DMA transfer is active for the specified channel.
    // A transfer is active if DE = 1, DME = 1, TE = 0, NMIF = 0 and AE = 0.
    bool IsDMATransferActive(const DMAChannel &ch) const;

    template <bool debug, bool emulateCache>
    bool StepDMAC(uint32 channel);

    template <bool debug, bool emulateCache>
    void AdvanceDMA(uint64 cycles);

    // --- WDT module ---

    WatchdogTimer WDT;
    uint8 m_WDTBusValue;

    template <bool write>
    void AdvanceWDT();

    // --- Power-down module ---

    RegSBYCR SBYCR; // 091  R/W  8        00        SBYCR   Standby Control Register
    bool m_sleep;

    // --- DIVU module ---

    DivisionUnit DIVU;

    template <bool debug>
    void ExecuteDiv32();
    template <bool debug>
    void ExecuteDiv64();

    // --- UBC module ---

    // TODO: implement (channels A and B)

    // --- FRT module ---

    FreeRunningTimer FRT;

    template <bool write>
    void AdvanceFRT();

    void TriggerFRTInputCapture();

    // -------------------------------------------------------------------------
    // Interrupts

    InterruptController INTC;

    void SetExternalInterrupt(uint8 level, uint8 vecNum);

    // Raises the interrupt signal of the specified source.
    FORCE_INLINE void RaiseInterrupt(InterruptSource source) {
        const uint8 level = INTC.GetLevel(source);
        RaiseInterrupt(source, level);
    }

    // Raises the interrupt signal of the specified source with the specified level.
    FORCE_INLINE void RaiseInterrupt(InterruptSource source, uint8 level) {
        if (level == 0) {
            return;
        }
        if (level < INTC.pending.level) {
            return;
        }
        if (level == INTC.pending.level && static_cast<uint8>(source) < static_cast<uint8>(INTC.pending.source)) {
            return;
        }
        INTC.pending.level = level;
        INTC.pending.source = source;
        m_intrFlags.pending = !m_delaySlot && level > SR.ILevel;
    }

    // Raises the interrupt signal of the specified source if the predicate passes.
    template <typename TPredicate>
        requires std::predicate<TPredicate>
    FORCE_INLINE void RaiseInterruptIf(InterruptSource source, TPredicate &&fnPredicate) {
        const uint8 level = INTC.GetLevel(source);
        RaiseInterruptIf(source, level, std::forward<TPredicate>(fnPredicate));
    }

    // Raises the interrupt signal of the specified source with the specified level if the predicate passes.
    template <typename TPredicate>
        requires std::predicate<TPredicate>
    FORCE_INLINE void RaiseInterruptIf(InterruptSource source, uint8 level, TPredicate &&fnPredicate) {
        if (level == 0) {
            return;
        }
        if (level < INTC.pending.level) {
            return;
        }
        if (level == INTC.pending.level && static_cast<uint8>(source) < static_cast<uint8>(INTC.pending.source)) {
            return;
        }
        if (!fnPredicate()) {
            return;
        }
        INTC.pending.level = level;
        INTC.pending.source = source;
        m_intrFlags.pending = !m_delaySlot && INTC.pending.level > SR.ILevel;
    }

    // Lowers the interrupt signal of the specified source.
    FORCE_INLINE void LowerInterrupt(InterruptSource source) {
        if (INTC.pending.source == source) {
            RecalcInterrupts();
        }
    }

    // Recalculates the highest priority interrupt to be serviced.
    void RecalcInterrupts();

    // Combines both interrupt flags into a single value for faster checks in the hot path
    struct IntrFlags {
        // Whether an interrupt should be serviced on the next instruction:
        //   !m_delaySlot && INTC.pending.level > SR.ILevel
        // This value is updated when any of these variables is changed, which happens less often than once per
        // instruction. There's no need to store this in the save state struct since its value can be derived as above.
        bool pending;

        // Whether an interrupt is allowed to be serviced on the next instruction.
        // All LDC, LDS, STC and STS instructions block interrupts on the following instruction.
        bool allow;
    } m_intrFlags;

    // Constant value representing the condition for executing an interrupt
    static constexpr uint16 kIntrFlagsPendingAllowed =
        std::bit_cast<uint16_t>(IntrFlags{.pending = true, .allow = true});

    // -------------------------------------------------------------------------
    // Cache

    Cache m_cache;

    // -------------------------------------------------------------------------
    // Debugger

    Probe m_probe{*this};
    debug::ISH2Tracer *m_tracer = nullptr;

    std::array<bool, 2> m_dmacTraced; // whether each DMA channel has had a transfer traced

    static constexpr size_t kBitsPerByte = 8ull; // asserted in ymir.cpp
    static constexpr size_t kAddressSpaceSize = 1ull << 32ull;
    static constexpr size_t kInstructionSize = sizeof(uint16); // breakpoints must be instruction-aligned
    static constexpr size_t kBreakpointMapSize = kAddressSpaceSize / kInstructionSize / kBitsPerByte;
    static constexpr size_t kWatchpointMapSize = kAddressSpaceSize; // only 2 bits per byte are used

    template <size_t sizeBits, size_t chunkSizeBits>
    struct ChunkedMemory {
        static_assert(chunkSizeBits <= sizeBits, "chunk size must not exceed memory chunk size");

        static constexpr size_t kSize = static_cast<size_t>(1) << sizeBits;
        static constexpr size_t kChunkSize = static_cast<size_t>(1) << chunkSizeBits;
        static constexpr size_t kChunkMask = kChunkSize - 1;
        static constexpr size_t kNumChunks = kSize / kChunkSize;

        using Chunk = std::array<uint8, kChunkSize>;

        // Retrieves a pointer to the specified object in memory.
        // The address is force-aligned to sizeof(T).
        template <typename T>
        T *GetPointer(size_t address) {
            static_assert(bit::is_power_of_two(sizeof(T)));
            const size_t chunkIndex = address >> chunkSizeBits;
            if (!m_chunks[chunkIndex]) {
                m_chunks[chunkIndex] = std::make_unique<Chunk>();
            }
            return reinterpret_cast<T *>(&(*m_chunks[chunkIndex])[address & kChunkMask]);
        }

        template <typename T>
        const T *GetPointer(size_t address) const {
            return const_cast<ChunkedMemory *>(this)->GetPointer<T>(address);
        }

        void Allocate() {
            m_allocated = true;
        }

        void Free() {
            for (auto &ptr : m_chunks) {
                ptr.reset();
            }
            m_allocated = false;
        }

        bool IsAllocated() const {
            return m_allocated;
        }

    private:
        std::array<std::unique_ptr<Chunk>, kNumChunks> m_chunks;

        bool m_allocated = false;
    };

    // Address space size has 32 bits.
    // Instruction size is 2 bits.
    // A byte has 8 bits.
    // The breakpoint map needs one bit per instruction address, so:
    //   4 GB / 2 (instructions at word-aligned addresses) / 8 (packing 8 bits in one byte)
    //   32 - 1 - 3
    ChunkedMemory<32 - 1 - 3, 16> m_breakpoints;

    // For watchpoints, we reserve one byte per address in the address space.
    ChunkedMemory<32, 19> m_watchpoints;

    // TODO: util::VirtualMemory may fail to allocate large chunks of memory if there's not enough free RAM on the
    // system. Figure out a way to use it again but allocate memory dynamically.

    // These help track what breakpoints and watchpoints are set for fast clears.
    std::set<uint32> m_breakpointSet;
    std::map<uint32, debug::WatchpointFlags> m_watchpointSet;

    bool m_debugSuspend = false; // Disables CPU while in debug mode

    bool CheckBreakpoint();
    bool CheckWatchpoints(const DecodedMemAccesses &mem);
    bool CheckWatchpoint(const DecodedMemAccesses::Access &access);

    const std::string_view m_logPrefix; // For devlogs

    // -------------------------------------------------------------------------
    // Helper functions

    void SetupDelaySlot(uint32 targetAddress);

    template <bool debug, bool emulateCache, bool delaySlot>
    void AdvancePC();

    template <bool debug, bool emulateCache>
    uint64 EnterException(uint8 vectorNumber);

    // -------------------------------------------------------------------------
    // Instruction interpreters

    // Interprets the next instruction.
    // Returns the number of cycles executed.
    template <bool debug, bool emulateCache>
    uint64 InterpretNext();

#define TPL_DBG_CACHE_DS template <bool debug, bool emulateCache, bool delaySlot>
#define TPL_DBG_CACHE template <bool debug, bool emulateCache>
#define TPL_DBG template <bool debug>

    TPL_DBG_CACHE_DS uint64 NOP(); // nop

    uint64 SLEEP(); // sleep

    TPL_DBG_CACHE_DS uint64 MOV(uint16 opcode);    // mov   Rm, Rn
    TPL_DBG_CACHE_DS uint64 MOVBL(uint16 opcode);  // mov.b @Rm, Rn
    TPL_DBG_CACHE_DS uint64 MOVWL(uint16 opcode);  // mov.w @Rm, Rn
    TPL_DBG_CACHE_DS uint64 MOVLL(uint16 opcode);  // mov.l @Rm, Rn
    TPL_DBG_CACHE_DS uint64 MOVBL0(uint16 opcode); // mov.b @(R0,Rm), Rn
    TPL_DBG_CACHE_DS uint64 MOVWL0(uint16 opcode); // mov.w @(R0,Rm), Rn
    TPL_DBG_CACHE_DS uint64 MOVLL0(uint16 opcode); // mov.l @(R0,Rm), Rn
    TPL_DBG_CACHE_DS uint64 MOVBL4(uint16 opcode); // mov.b @(disp,Rm), R0
    TPL_DBG_CACHE_DS uint64 MOVWL4(uint16 opcode); // mov.w @(disp,Rm), R0
    TPL_DBG_CACHE_DS uint64 MOVLL4(uint16 opcode); // mov.l @(disp,Rm), Rn
    TPL_DBG_CACHE_DS uint64 MOVBLG(uint16 opcode); // mov.b @(disp,GBR), R0
    TPL_DBG_CACHE_DS uint64 MOVWLG(uint16 opcode); // mov.w @(disp,GBR), R0
    TPL_DBG_CACHE_DS uint64 MOVLLG(uint16 opcode); // mov.l @(disp,GBR), R0
    TPL_DBG_CACHE_DS uint64 MOVBM(uint16 opcode);  // mov.b Rm, @-Rn
    TPL_DBG_CACHE_DS uint64 MOVWM(uint16 opcode);  // mov.w Rm, @-Rn
    TPL_DBG_CACHE_DS uint64 MOVLM(uint16 opcode);  // mov.l Rm, @-Rn
    TPL_DBG_CACHE_DS uint64 MOVBP(uint16 opcode);  // mov.b @Rm+, Rn
    TPL_DBG_CACHE_DS uint64 MOVWP(uint16 opcode);  // mov.w @Rm+, Rn
    TPL_DBG_CACHE_DS uint64 MOVLP(uint16 opcode);  // mov.l @Rm+, Rn
    TPL_DBG_CACHE_DS uint64 MOVBS(uint16 opcode);  // mov.b Rm, @Rn
    TPL_DBG_CACHE_DS uint64 MOVWS(uint16 opcode);  // mov.w Rm, @Rn
    TPL_DBG_CACHE_DS uint64 MOVLS(uint16 opcode);  // mov.l Rm, @Rn
    TPL_DBG_CACHE_DS uint64 MOVBS0(uint16 opcode); // mov.b Rm, @(R0,Rn)
    TPL_DBG_CACHE_DS uint64 MOVWS0(uint16 opcode); // mov.w Rm, @(R0,Rn)
    TPL_DBG_CACHE_DS uint64 MOVLS0(uint16 opcode); // mov.l Rm, @(R0,Rn)
    TPL_DBG_CACHE_DS uint64 MOVBS4(uint16 opcode); // mov.b R0, @(disp,Rn)
    TPL_DBG_CACHE_DS uint64 MOVWS4(uint16 opcode); // mov.w R0, @(disp,Rn)
    TPL_DBG_CACHE_DS uint64 MOVLS4(uint16 opcode); // mov.l Rm, @(disp,Rn)
    TPL_DBG_CACHE_DS uint64 MOVBSG(uint16 opcode); // mov.b R0, @(disp,GBR)
    TPL_DBG_CACHE_DS uint64 MOVWSG(uint16 opcode); // mov.w R0, @(disp,GBR)
    TPL_DBG_CACHE_DS uint64 MOVLSG(uint16 opcode); // mov.l R0, @(disp,GBR)
    TPL_DBG_CACHE_DS uint64 MOVI(uint16 opcode);   // mov   #imm, Rn
    TPL_DBG_CACHE_DS uint64 MOVWI(uint16 opcode);  // mov.w @(disp,PC), Rn
    TPL_DBG_CACHE_DS uint64 MOVLI(uint16 opcode);  // mov.l @(disp,PC), Rn
    TPL_DBG_CACHE_DS uint64 MOVA(uint16 opcode);   // mova  @(disp,PC), R0
    TPL_DBG_CACHE_DS uint64 MOVT(uint16 opcode);   // movt  Rn
    TPL_DBG_CACHE_DS uint64 CLRT();                // clrt
    TPL_DBG_CACHE_DS uint64 SETT();                // sett

    TPL_DBG_CACHE_DS uint64 EXTSB(uint16 opcode); // exts.b Rm, Rn
    TPL_DBG_CACHE_DS uint64 EXTSW(uint16 opcode); // exts.w Rm, Rn
    TPL_DBG_CACHE_DS uint64 EXTUB(uint16 opcode); // extu.b Rm, Rn
    TPL_DBG_CACHE_DS uint64 EXTUW(uint16 opcode); // extu.w Rm, Rn
    TPL_DBG_CACHE_DS uint64 SWAPB(uint16 opcode); // swap.b Rm, Rn
    TPL_DBG_CACHE_DS uint64 SWAPW(uint16 opcode); // swap.w Rm, Rn
    TPL_DBG_CACHE_DS uint64 XTRCT(uint16 opcode); // xtrct  Rm, Rn

    TPL_DBG_CACHE_DS uint64 LDCGBR(uint16 opcode);   // ldc   Rm, GBR
    TPL_DBG_CACHE_DS uint64 LDCSR(uint16 opcode);    // ldc   Rm, SR
    TPL_DBG_CACHE_DS uint64 LDCVBR(uint16 opcode);   // ldc   Rm, VBR
    TPL_DBG_CACHE_DS uint64 LDSMACH(uint16 opcode);  // lds   Rm, MACH
    TPL_DBG_CACHE_DS uint64 LDSMACL(uint16 opcode);  // lds   Rm, MACL
    TPL_DBG_CACHE_DS uint64 LDSPR(uint16 opcode);    // lds   Rm, PR
    TPL_DBG_CACHE_DS uint64 STCGBR(uint16 opcode);   // stc   GBR, Rn
    TPL_DBG_CACHE_DS uint64 STCSR(uint16 opcode);    // stc   SR, Rn
    TPL_DBG_CACHE_DS uint64 STCVBR(uint16 opcode);   // stc   VBR, Rn
    TPL_DBG_CACHE_DS uint64 STSMACH(uint16 opcode);  // sts   MACH, Rn
    TPL_DBG_CACHE_DS uint64 STSMACL(uint16 opcode);  // sts   MACL, Rn
    TPL_DBG_CACHE_DS uint64 STSPR(uint16 opcode);    // sts   PR, Rn
    TPL_DBG_CACHE_DS uint64 LDCMGBR(uint16 opcode);  // ldc.l @Rm+, GBR
    TPL_DBG_CACHE_DS uint64 LDCMSR(uint16 opcode);   // ldc.l @Rm+, SR
    TPL_DBG_CACHE_DS uint64 LDCMVBR(uint16 opcode);  // ldc.l @Rm+, VBR
    TPL_DBG_CACHE_DS uint64 LDSMMACH(uint16 opcode); // lds.l @Rm+, MACH
    TPL_DBG_CACHE_DS uint64 LDSMMACL(uint16 opcode); // lds.l @Rm+, MACL
    TPL_DBG_CACHE_DS uint64 LDSMPR(uint16 opcode);   // lds.l @Rm+, PR
    TPL_DBG_CACHE_DS uint64 STCMGBR(uint16 opcode);  // stc.l GBR, @-Rn
    TPL_DBG_CACHE_DS uint64 STCMSR(uint16 opcode);   // stc.l SR, @-Rn
    TPL_DBG_CACHE_DS uint64 STCMVBR(uint16 opcode);  // stc.l VBR, @-Rn
    TPL_DBG_CACHE_DS uint64 STSMMACH(uint16 opcode); // sts.l MACH, @-Rn
    TPL_DBG_CACHE_DS uint64 STSMMACL(uint16 opcode); // sts.l MACL, @-Rn
    TPL_DBG_CACHE_DS uint64 STSMPR(uint16 opcode);   // sts.l PR, @-Rn

    TPL_DBG_CACHE_DS uint64 ADD(uint16 opcode);    // add    Rm, Rn
    TPL_DBG_CACHE_DS uint64 ADDI(uint16 opcode);   // add    imm, Rn
    TPL_DBG_CACHE_DS uint64 ADDC(uint16 opcode);   // addc   Rm, Rn
    TPL_DBG_CACHE_DS uint64 ADDV(uint16 opcode);   // addv   Rm, Rn
    TPL_DBG_CACHE_DS uint64 AND(uint16 opcode);    // and    Rm, Rn
    TPL_DBG_CACHE_DS uint64 ANDI(uint16 opcode);   // and    imm, R0
    TPL_DBG_CACHE_DS uint64 ANDM(uint16 opcode);   // and.   b imm, @(R0,GBR)
    TPL_DBG_CACHE_DS uint64 NEG(uint16 opcode);    // neg    Rm, Rn
    TPL_DBG_CACHE_DS uint64 NEGC(uint16 opcode);   // negc   Rm, Rn
    TPL_DBG_CACHE_DS uint64 NOT(uint16 opcode);    // not    Rm, Rn
    TPL_DBG_CACHE_DS uint64 OR(uint16 opcode);     // or     Rm, Rn
    TPL_DBG_CACHE_DS uint64 ORI(uint16 opcode);    // or     imm, Rn
    TPL_DBG_CACHE_DS uint64 ORM(uint16 opcode);    // or.b   imm, @(R0,GBR)
    TPL_DBG_CACHE_DS uint64 ROTCL(uint16 opcode);  // rotcl  Rn
    TPL_DBG_CACHE_DS uint64 ROTCR(uint16 opcode);  // rotcr  Rn
    TPL_DBG_CACHE_DS uint64 ROTL(uint16 opcode);   // rotl   Rn
    TPL_DBG_CACHE_DS uint64 ROTR(uint16 opcode);   // rotr   Rn
    TPL_DBG_CACHE_DS uint64 SHAL(uint16 opcode);   // shal   Rn
    TPL_DBG_CACHE_DS uint64 SHAR(uint16 opcode);   // shar   Rn
    TPL_DBG_CACHE_DS uint64 SHLL(uint16 opcode);   // shll   Rn
    TPL_DBG_CACHE_DS uint64 SHLL2(uint16 opcode);  // shll2  Rn
    TPL_DBG_CACHE_DS uint64 SHLL8(uint16 opcode);  // shll8  Rn
    TPL_DBG_CACHE_DS uint64 SHLL16(uint16 opcode); // shll16 Rn
    TPL_DBG_CACHE_DS uint64 SHLR(uint16 opcode);   // shlr   Rn
    TPL_DBG_CACHE_DS uint64 SHLR2(uint16 opcode);  // shlr2  Rn
    TPL_DBG_CACHE_DS uint64 SHLR8(uint16 opcode);  // shlr8  Rn
    TPL_DBG_CACHE_DS uint64 SHLR16(uint16 opcode); // shlr16 Rn
    TPL_DBG_CACHE_DS uint64 SUB(uint16 opcode);    // sub    Rm, Rn
    TPL_DBG_CACHE_DS uint64 SUBC(uint16 opcode);   // subc   Rm, Rn
    TPL_DBG_CACHE_DS uint64 SUBV(uint16 opcode);   // subv   Rm, Rn
    TPL_DBG_CACHE_DS uint64 XOR(uint16 opcode);    // xor    Rm, Rn
    TPL_DBG_CACHE_DS uint64 XORI(uint16 opcode);   // xor    imm, Rn
    TPL_DBG_CACHE_DS uint64 XORM(uint16 opcode);   // xor.b  imm, @(R0,GBR)

    TPL_DBG_CACHE_DS uint64 DT(uint16 opcode); // dt Rn

    TPL_DBG_CACHE_DS uint64 CLRMAC();             // clrmac
    TPL_DBG_CACHE_DS uint64 MACW(uint16 opcode);  // mac.w   @Rm+, @Rn+
    TPL_DBG_CACHE_DS uint64 MACL(uint16 opcode);  // mac.l   @Rm+, @Rn+
    TPL_DBG_CACHE_DS uint64 MULL(uint16 opcode);  // mul.l   Rm, Rn
    TPL_DBG_CACHE_DS uint64 MULS(uint16 opcode);  // muls.w  Rm, Rn
    TPL_DBG_CACHE_DS uint64 MULU(uint16 opcode);  // mulu.w  Rm, Rn
    TPL_DBG_CACHE_DS uint64 DMULS(uint16 opcode); // dmuls.l Rm, Rn
    TPL_DBG_CACHE_DS uint64 DMULU(uint16 opcode); // dmulu.l Rm, Rn

    TPL_DBG_CACHE_DS uint64 DIV0S(uint16 opcode); // div0s Rm, Rn
    TPL_DBG_CACHE_DS uint64 DIV0U();              // div0u
    TPL_DBG_CACHE_DS uint64 DIV1(uint16 opcode);  // div1  Rm, Rn

    TPL_DBG_CACHE_DS uint64 CMPIM(uint16 opcode);  // cmp/eq  imm, R0
    TPL_DBG_CACHE_DS uint64 CMPEQ(uint16 opcode);  // cmp/eq  Rm, Rn
    TPL_DBG_CACHE_DS uint64 CMPGE(uint16 opcode);  // cmp/ge  Rm, Rn
    TPL_DBG_CACHE_DS uint64 CMPGT(uint16 opcode);  // cmp/gt  Rm, Rn
    TPL_DBG_CACHE_DS uint64 CMPHI(uint16 opcode);  // cmp/hi  Rm, Rn
    TPL_DBG_CACHE_DS uint64 CMPHS(uint16 opcode);  // cmp/hs  Rm, Rn
    TPL_DBG_CACHE_DS uint64 CMPPL(uint16 opcode);  // cmp/pl  Rn
    TPL_DBG_CACHE_DS uint64 CMPPZ(uint16 opcode);  // cmp/pz  Rn
    TPL_DBG_CACHE_DS uint64 CMPSTR(uint16 opcode); // cmp/str Rm, Rn
    TPL_DBG_CACHE_DS uint64 TAS(uint16 opcode);    // tas.b   @Rn
    TPL_DBG_CACHE_DS uint64 TST(uint16 opcode);    // tst     Rm, Rn
    TPL_DBG_CACHE_DS uint64 TSTI(uint16 opcode);   // tst     imm, R0
    TPL_DBG_CACHE_DS uint64 TSTM(uint16 opcode);   // tst.b   imm, @(R0,GBR)

    TPL_DBG_CACHE uint64 BF(uint16 opcode);    // bf    disp
    TPL_DBG uint64 BFS(uint16 opcode);         // bf/s  disp
    TPL_DBG_CACHE uint64 BT(uint16 opcode);    // bt    disp
    TPL_DBG uint64 BTS(uint16 opcode);         // bt/s  disp
    TPL_DBG uint64 BRA(uint16 opcode);         // bra   disp
    TPL_DBG uint64 BRAF(uint16 opcode);        // braf  Rm
    TPL_DBG uint64 BSR(uint16 opcode);         // bsr   disp
    TPL_DBG uint64 BSRF(uint16 opcode);        // bsrf  Rm
    TPL_DBG uint64 JMP(uint16 opcode);         // jmp   @Rm
    TPL_DBG uint64 JSR(uint16 opcode);         // jsr   @Rm
    TPL_DBG_CACHE uint64 TRAPA(uint16 opcode); // trapa imm

    TPL_DBG_CACHE uint64 RTE(); // rte
    TPL_DBG uint64 RTS();       // rts

#undef TPL_DBG_CACHE_DS
#undef TPL_DBG_CACHE
#undef TPL_DBG

public:
    // -------------------------------------------------------------------------
    // Callbacks

    const scu::CBExternalInterrupt CbExtIntr = util::MakeClassMemberRequiredCallback<&SH2::SetExternalInterrupt>(this);
};

} // namespace ymir::sh2
