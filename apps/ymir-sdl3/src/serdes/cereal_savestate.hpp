#pragma once

#include <ymir/savestate/savestate.hpp>

#include <ymir/util/size_ops.hpp>

#include <cereal/types/array.hpp>
#include <cereal/types/vector.hpp>

#include <fmt/format.h>

#include <algorithm>

namespace ymir::savestate {

// Current save state format version.
// Increment once per release (including nightly builds!) if there are any changes to the serializers.
// Remember to document every change!
// Versions:
//   1 = 0.1.0
//   2 = 0.1.1
//   3 = 0.1.2
//   4 = 0.1.3
//   5 = 0.1.4
//   6 = 0.1.5
//   7 = 0.1.6
//   8 = 0.1.7
//   9 = 0.1.8
//  10 = 0.2.0
//  11 = 0.2.1
//  12 = 0.3.0
//  13 = 0.3.2
//  14 = 0.4.0-dev  (f192e30ef2d5bbaa1c3f78de63439a36fc26a423)
//  15 = 0.4.0
//  16 = SH-2 SCI register state
inline constexpr uint32 kVersion = 16;

} // namespace ymir::savestate

// -----------------------------------------------------------------------------

CEREAL_CLASS_VERSION(ymir::savestate::SaveState, ymir::savestate::kVersion);

namespace ymir::savestate {

template <class Archive>
void serialize(Archive &ar, SchedulerSaveState &s, const uint32 version) {
    // v10:
    // - Changed fields
    //   - events increased from 6 to 7; new event was never used before this version
    ar(s.currCount);
    if (version >= 10) {
        ar(s.events);
    } else {
        std::array<SchedulerSaveState::EventState, 6> events{};
        ar(events);
        std::copy(events.begin(), events.end(), s.events.begin());
        s.events[6].target = ~static_cast<uint64>(0);
        s.events[6].countNumerator = 1;
        s.events[6].countDenominator = 1;
        s.events[6].id = core::events::CDBlockLLEDriveState;
    }
}

template <class Archive>
void serialize(Archive &ar, SchedulerSaveState::EventState &s) {
    ar(s.target, s.countNumerator, s.countDenominator, s.id);
}

template <class Archive>
void serialize(Archive &ar, SystemSaveState &s) {
    ar(s.videoStandard, s.clockSpeed);
    ar(s.slaveSH2Enabled);
    ar(s.iplRomHash);
    ar(s.WRAMLow, s.WRAMHigh);
}

template <class Archive>
void serialize(Archive &ar, SH2SaveState &s, const uint32 version) {
    // v16:
    // - New field: SH-2 SCI register state
    // v13:
    // - New fields
    //   - uint32 fetchedOpcodes = 0
    //   - uint8 wbReg = 0
    // v12:
    // - New fields
    //   - bool intrAllow = true
    // v6:
    // - New fields
    //   - bool sleep = false

    ar(s.R, s.PC, s.PR, s.MACL, s.MACH, s.SR, s.GBR, s.VBR);
    ar(s.delaySlot, s.delaySlotTarget);
    if (version >= 12) {
        ar(s.intrAllow);
    } else {
        s.intrAllow = true;
    }
    if (version >= 13) {
        ar(s.fetchedOpcodes);
        s.forceFetchOpcodes = false;
        ar(s.wbReg);
    } else {
        s.forceFetchOpcodes = true;
        s.wbReg = 0;
    }
    ar(s.bsc);
    if (version >= 16) {
        ar(s.sci);
    } else {
        s.sci = {};
    }
    ar(s.dmac);
    serialize(ar, s.wdt, version);
    serialize(ar, s.divu, version);
    serialize(ar, s.frt, version);
    ar(s.intc, s.cache, s.SBYCR);
    if (version >= 8) {
        ar(s.sleep);
    } else {
        s.sleep = false;
    }
    if (version < 5) {
        s.divu.VCRDIV = s.intc.vectors[12]; // 12 == static_cast<size_t>(sh2::InterruptSource::DIVU_OVFI)
    }
}

template <class Archive>
void serialize(Archive &ar, SH2SaveState::SCI &s) {
    ar(s.SMR, s.BRR, s.SCR, s.TDR, s.SSR, s.RDR, s.observedStatus);
}

template <class Archive>
void serialize(Archive &ar, SH2SaveState::BSC &s) {
    ar(s.BCR1, s.BCR2, s.WCR, s.MCR, s.RTCSR, s.RTCNT, s.RTCOR);
}

template <class Archive>
void serialize(Archive &ar, SH2SaveState::DMAC &s) {
    ar(s.DMAOR, s.channels);
}

template <class Archive>
void serialize(Archive &ar, SH2SaveState::DMAC::Channel &s) {
    ar(s.SAR, s.DAR, s.TCR, s.CHCR, s.DRCR);
}

template <class Archive>
void serialize(Archive &ar, SH2SaveState::WDT &s, const uint32 version) {
    // v6:
    // - New fields
    //   - uint8 busValue = 0
    // v5:
    // - New fields
    //   - WTCSR_mask = false
    // - Changed fields
    //   - cycleCount is now an absolute counter based on the scheduler counter

    ar(s.WTCSR, s.WTCNT, s.RSTCSR, s.cycleCount);
    if (version >= 5) {
        ar(s.WTCSR_mask);
    } else {
        s.WTCSR_mask = false;
    }
    if (version >= 6) {
        ar(s.busValue);
    } else {
        s.busValue = 0;
    }
}

template <class Archive>
void serialize(Archive &ar, SH2SaveState::DIVU &s, const uint32 version) {
    // v5:
    // - New fields
    //   - VCRDIV = INTC.vectors[static_cast<size_t>(InterruptSource::DIVU_OVFI)]

    ar(s.DVSR, s.DVDNT, s.DVCR, s.DVDNTH, s.DVDNTL, s.DVDNTUH, s.DVDNTUL);
    if (version >= 5) {
        ar(s.VCRDIV);
        // VCRDIV is filled in with INTC.vectors[DIVU_OVFI] for version prior to 5 in the SH2SaveState serializer above
    }
}

template <class Archive>
void serialize(Archive &ar, SH2SaveState::FRT &s, const uint32 version) {
    // v5:
    // - New fields
    //   - FTCSR_mask = 0x00
    // - Changed fields
    //   - cycleCount is now an absolute counter based on the scheduler counter

    ar(s.TIER, s.FTCSR, s.FRC, s.OCRA, s.OCRB, s.TCR, s.TOCR, s.ICR, s.TEMP, s.cycleCount);
    if (version >= 5) {
        ar(s.FTCSR_mask);
    } else {
        s.FTCSR_mask = 0x00;
    }
}

template <class Archive>
void serialize(Archive &ar, SH2SaveState::INTC &s) {
    ar(s.ICR, s.levels, s.vectors, s.pendingSource, s.pendingLevel, s.NMI, s.extVec);
}

template <class Archive>
void serialize(Archive &ar, SH2SaveState::Cache &s) {
    ar(s.CCR, s.entries, s.lru);
}

template <class Archive>
void serialize(Archive &ar, SH2SaveState::Cache::Entry &s) {
    ar(s.tags, s.lines);
}

template <class Archive>
void serialize(Archive &ar, SCUSaveState &s, const uint32 version) {
    // v12:
    // - Removed fields
    //   - timer1Triggered
    // v8:
    // - New fields
    //   - abusIntrsPendingAck = intrStatus >> 16  (or 0 if abusIntrAck == true in versions 7 and below)
    //   - timer1Triggered = true
    // - Removed fields
    //   - bool abusIntrAck
    // v5:
    // - New fields
    //   - pendingIntrLevel = 0
    //   - pendingIntrIndex = 0
    // - Changed fields
    //   - timer1Enable renamed to timerEnable; no changes to value
    // v4:
    // - New fields
    //   - enum SCUSaveState::CartType: added ROM

    for (auto &dma : s.dma) {
        serialize(ar, dma, version);
    }
    serialize(ar, s.dsp, version);
    ar(s.cartType);

    if (version >= 4) {
        // From version 4 onwards, carts have a fixed size.
        switch (s.cartType) {
        case SCUSaveState::CartType::DRAM8Mbit: s.cartData.resize(1_MiB); break;
        case SCUSaveState::CartType::DRAM32Mbit: s.cartData.resize(4_MiB); break;
        case SCUSaveState::CartType::DRAM48Mbit:
            if (version >= 10) {
                s.cartData.resize(6_MiB);
            } else {
                throw cereal::Exception("48 Mbit DRAM cart is not available in save state versions 9 and earlier");
            }
            break;
        case SCUSaveState::CartType::ROM: s.cartData.resize(2_MiB);
        default: s.cartData.clear(); break;
        }
    } else {
        // Up to version 3, DRAM cartridge states could store an arbitrary amount of data.
        // Besides the DRAM cartridge, only the Backup RAM cartridge was also available.
        //
        // Reject save states with unexpected sizes to prevent potential memory allocation attacks.
        cereal::size_type size;
        ar(size);
        switch (s.cartType) {
        case SCUSaveState::CartType::DRAM8Mbit:
            if (size != 1_MiB) {
                throw cereal::Exception("Unexpected 8 Mbit DRAM cart data array size");
            }
            break;
        case SCUSaveState::CartType::DRAM32Mbit:
            if (size != 4_MiB) {
                throw cereal::Exception("Unexpected 32 Mbit DRAM cart data array size");
            }
            break;
        default:
            if (size != 0) {
                throw cereal::Exception("Unexpected cart data array size");
            }
            break;
        }
        s.cartData.resize(size);
    }
    if (!s.cartData.empty()) {
        ar(cereal::binary_data(s.cartData.data(), s.cartData.size()));
    }
    ar(s.intrMask, s.intrStatus);
    if (version >= 8) {
        ar(s.abusIntrsPendingAck);
    } else {
        bool abusIntrAck;
        ar(abusIntrAck);
        s.abusIntrsPendingAck = abusIntrAck ? 0x0000 : (s.intrStatus >> 16u);
    }
    if (version >= 5) {
        ar(s.pendingIntrLevel, s.pendingIntrIndex);
    } else {
        s.pendingIntrLevel = 0;
        s.pendingIntrIndex = 0;
    }
    ar(s.timer0Counter, s.timer0Compare);
    ar(s.timer1Reload);
    if (version >= 8 && version < 12) {
        bool timer1Triggered;
        ar(timer1Triggered);
    }
    ar(s.timerEnable, s.timer1Mode);
    ar(s.wramSizeSelect);
}

template <class Archive>
void serialize(Archive &ar, SCUDMASaveState &s, const uint32 version) {
    // v10:
    // - New fields
    //   - xfer
    //   - intrDelay = 0

    ar(s.srcAddr, s.dstAddr, s.xferCount);
    ar(s.srcAddrInc, s.dstAddrInc, s.updateSrcAddr, s.updateDstAddr);
    ar(s.enabled, s.active, s.indirect, s.trigger);
    ar(s.start);
    if (version >= 10) {
        ar(s.intrDelay);
    } else {
        s.intrDelay = 0;
    }
    ar(s.currSrcAddr, s.currDstAddr, s.currXferCount);
    ar(s.currSrcAddrInc, s.currDstAddrInc);
    ar(s.currIndirectSrc, s.endIndirect);
    serialize(ar, s.xfer, version);
}

template <class Archive>
void serialize(Archive &ar, SCUDMASaveState::Transfer &s, const uint32 version) {
    // v9:
    // - Struct newly introduced
    // - New fields
    //   - buf = 0
    //   - bufPos = 0
    //   - currDstAddr = 0
    //   - currDstOffset = 0
    //   - initialDstAlignment = 0
    //   - xferLength = 0
    //   - baseSrcAddr = 0
    //   - baseDstAddr = 0
    //   - started = false

    if (version >= 9) {
        ar(s.buf, s.bufPos);
        ar(s.currDstAddr, s.currDstOffset);
        ar(s.initialDstAlignment);
        ar(s.xferLength);
        ar(s.baseSrcAddr, s.baseDstAddr);
        ar(s.started);
    } else {
        s.buf = 0;
        s.bufPos = 0;
        s.currDstAddr = 0;
        s.currDstOffset = 0;
        s.initialDstAlignment = 0;
        s.xferLength = 0;
        s.baseSrcAddr = 0;
        s.baseDstAddr = 0;
        s.started = false;
    }
}

template <class Archive>
void serialize(Archive &ar, SCUDSPState &s, const uint32 version) {
    // v7:
    // - New fields
    //   - dmaAddrD0 = 0
    //   - dmaPC = PC
    // v6:
    // - New fields
    //   - nextInstr = programRAM[PC]
    //   - looping = false
    //   - cyclesSpillover = 0
    // - Removed fields
    //   - uint32 nextPC
    //   - uint8 jmpCounter

    ar(s.programRAM, s.dataRAM);
    ar(s.programExecuting, s.programPaused, s.programEnded, s.programStep);
    ar(s.PC, s.dataAddress);
    if (version >= 6) {
        ar(s.nextInstr);
    } else {
        s.nextInstr = s.programRAM[s.PC];
    }
    if (version < 6) {
        uint32 nextPC;
        uint8 jmpCounter;
        ar(nextPC, jmpCounter);
    }
    ar(s.sign, s.zero, s.carry, s.overflow);
    ar(s.CT, s.ALU, s.AC, s.P, s.RX, s.RY, s.LOP, s.TOP);
    if (version >= 6) {
        ar(s.looping);
    } else {
        s.looping = false;
    }
    ar(s.dmaRun, s.dmaToD0, s.dmaHold, s.dmaCount, s.dmaSrc, s.dmaDst);
    ar(s.dmaReadAddr, s.dmaWriteAddr, s.dmaAddrInc);
    if (version >= 7) {
        ar(s.dmaAddrD0, s.dmaPC);
    } else {
        s.dmaAddrD0 = 0;
        s.dmaPC = s.PC;
    }
    if (version >= 6) {
        ar(s.cyclesSpillover);
    } else {
        s.cyclesSpillover = 0u;
    }
}

template <class Archive>
void serialize(Archive &ar, SMPCSaveState &s, const uint32 version) {
    // v9:
    // - New fields
    //   - commandEventState = 0

    ar(s.IREG, s.OREG, s.COMREG, s.SR, s.SF);
    ar(s.PDR1, s.PDR2, s.DDR1, s.DDR2, s.IOSEL, s.EXLE);
    ar(s.intback);
    ar(s.busValue, s.resetDisable);
    ar(s.rtcTimestamp, s.rtcSysClockCount);
    if (version >= 9) {
        ar(s.commandEventState);
    } else {
        s.commandEventState = 0;
    }
}

template <class Archive>
void serialize(Archive &ar, SMPCSaveState::INTBACKSaveState &s) {
    ar(s.getPeripheralData, s.optimize, s.port1mode, s.port2mode);
    ar(s.report, s.reportOffset, s.inProgress);
}

template <class Archive>
void serialize(Archive &ar, VDPSaveState &s, const uint32 version) {
    // VDPSaveState
    // ------------
    // v12:
    // - New fields
    //   - VDP2VCNTLatch = 0x3FF
    //   - VDP2VCNTLatched = false
    // - Removed fields
    //   - VDP1TimingPenalty -> moved to VDP1SaveState
    //   - VDP1FBCRChanged -> moved to VDP1RegsSaveState
    //   - displayEnabled -> moved to regs2.displayEnabledLatch
    //   - borderColorMode -> moved to regs2.borderColorModeLatch
    // v9:
    // - New fields
    //   - enum VDPSaveState::VerticalPhase: added VCounterSkip (= 5)
    //   - displayEnabled = regs2.TVMD.DISP (= TVMD & 0x8000)
    //   - borderColorMode = regs2.TVMD.BDCLMD (= TVMD & 0x100)
    // v7:
    // - New fields
    //   - VDP1TimingPenalty = 0
    //   - FBCRChanged = false
    // v6:
    // - Removed fields
    //   - uint16 VCounter -> moved to regs2.VCNT
    //
    // VDP1SaveState
    // -------------
    // - Struct created
    //
    // VDP1RegsSaveState
    // -----------------
    // v13:
    // - Added fields
    //   - uint32 nextCommandAddress = COPR << 3u
    // v12:
    // - Added fields
    //   - FBCRChanged = moved from VDPSaveState
    //   - eraseWriteValueLatch = moved from VDPRendererSaveState::VDP1RenderSaveState
    //   - eraseX1Latch, eraseY1Latch = moved from VDPRendererSaveState::VDP1RenderSaveState
    //   - eraseX3Latch, eraseY3Latch = moved from VDPRendererSaveState::VDP1RenderSaveState
    // v9:
    // - Removed fields
    //   - bool manualSwap
    //   - bool manualErase
    //
    // VDP2RegsSaveState
    // -----------------
    // v13:
    // - Removed fields
    //   - bool VCNTLatched
    // v12:
    // - Added fields
    //   - VCNTLatch -> moved from VDPSaveState::VDP2VCNTLatch
    //   - VCNTLatched -> moved from VDPSaveState::VDP2VCNTLatched
    //
    // VDPRendererSaveState
    // --------------------
    // v15:
    // - Removed fields
    //   - displayFB
    // v12:
    // - New fields
    //   - vdp1State = new struct
    // - Removed fields
    //   - VDP1TimingPenalty -> moved to vdp1State as timingPenalty
    //   - VDP1FBCRChanged -> moved to regs1 as FBCRChanged
    // v10:
    // - Removed fields
    //   - bool vdp1Done
    // v7:
    // - New fields
    //   - vramFetchers = (default values)
    // v4:
    // - New fields
    //   - vcellScrollInc = sizeof(uint32)
    //
    // VDPRendererSaveState::VDP1RenderSaveState
    // -----------------------------------------
    // v12:
    // - Removed fields
    //   - rendering -> moved to vdp1State.drawing
    //   - doDisplayErase -> moved to vdp1State.doDisplayErase
    //   - doVBlankErase -> moved to vdp1State.doVBlankErase
    //   - cycleCount, cyclesSpent -> replaced with vdp1State.spilloverCycles
    //   - eraseWriteValue -> moved to regs1.eraseWriteValueLatch
    //   - eraseX1, eraseY1 -> moved to regs1.eraseX1Latch, regs1.eraseY1Latch
    //   - eraseX3, eraseY3 -> moved to regs1.eraseX3Latch, regs1.eraseY3Latch
    // v9:
    // - New fields
    //   - doubleV = 0
    //   - cyclesSpent = 0
    //   - doVBlankErase = true when erase && VBE=1, otherwise false
    //   - eraseWriteValue = EWDR
    //   - eraseX1, eraseY1 = EWLR
    //   - eraseX3, eraseY3 = EWRR
    //   - meshFBRAM = filled with zeros
    // - Changed fields
    //   - erase -> doDisplayErase = true when erase && VBE=0, otherwise false
    // v5:
    // - New fields
    //   - erase = false

    ar(s.VRAM1, s.VRAM2, s.CRAM, s.FBRAM, s.displayFB);
    if (version >= 7) {
        ar(s.vdp1State.timingPenalty);
        ar(s.regs1.FBCRChanged);
    } else {
        s.vdp1State.timingPenalty = 0;
        s.regs1.FBCRChanged = false;
    }
    if (version >= 12) {
        ar(s.regs2.VCNTLatch);
        if (version < 13) {
            bool VCNTLatched;
            ar(VCNTLatched);
        }
    } else {
        s.regs2.VCNTLatch = 0x3FF;
    }
    if (version >= 13) {
        ar(s.regs1.nextCommandAddress);
    } else {
        s.regs1.nextCommandAddress = s.regs1.COPR << 3u;
    }

    // -------------------------------------------------------------------------

    {
        auto &rs = s.regs1;
        ar(rs.TVMR, rs.FBCR, rs.PTMR);
        ar(rs.EWDR, rs.EWLR, rs.EWRR, rs.EDSR);
        ar(rs.LOPR, rs.COPR);
        ar(rs.MODR);
        if (version < 9) {
            bool dummy;
            ar(dummy /*manualSwap*/, dummy /*manualErase*/);
        }
    }

    // -------------------------------------------------------------------------

    {
        auto &rs = s.regs2;
        ar(rs.TVMD, rs.EXTEN, rs.TVSTAT, rs.VRSIZE, rs.HCNT, rs.VCNT, rs.RAMCTL);
        ar(rs.CYCA0L, rs.CYCA0U, rs.CYCA1L, rs.CYCA1U, rs.CYCB0L, rs.CYCB0U, rs.CYCB1L, rs.CYCB1U);
        ar(rs.BGON);
        ar(rs.MZCTL);
        ar(rs.SFSEL, rs.SFCODE);
        ar(rs.CHCTLA, rs.CHCTLB);
        ar(rs.BMPNA, rs.BMPNB);
        ar(rs.PNCNA, rs.PNCNB, rs.PNCNC, rs.PNCND, rs.PNCR);
        ar(rs.PLSZ);
        ar(rs.MPOFN, rs.MPOFR);
        ar(rs.MPABN0, rs.MPCDN0, rs.MPABN1, rs.MPCDN1, rs.MPABN2, rs.MPCDN2, rs.MPABN3, rs.MPCDN3);
        ar(rs.MPABRA, rs.MPCDRA, rs.MPEFRA, rs.MPGHRA, rs.MPIJRA, rs.MPKLRA, rs.MPMNRA, rs.MPOPRA);
        ar(rs.MPABRB, rs.MPCDRB, rs.MPEFRB, rs.MPGHRB, rs.MPIJRB, rs.MPKLRB, rs.MPMNRB, rs.MPOPRB);
        ar(rs.SCXIN0, rs.SCXDN0, rs.SCYIN0, rs.SCYDN0, rs.ZMXIN0, rs.ZMXDN0, rs.ZMYIN0, rs.ZMYDN0);
        ar(rs.SCXIN1, rs.SCXDN1, rs.SCYIN1, rs.SCYDN1, rs.ZMXIN1, rs.ZMXDN1, rs.ZMYIN1, rs.ZMYDN1);
        ar(rs.SCXIN2, rs.SCYIN2);
        ar(rs.SCXIN3, rs.SCYIN3);
        ar(rs.ZMCTL, rs.SCRCTL);
        ar(rs.VCSTAU, rs.VCSTAL);
        ar(rs.LSTA0U, rs.LSTA0L, rs.LSTA1U, rs.LSTA1L);
        ar(rs.LCTAU, rs.LCTAL);
        ar(rs.BKTAU, rs.BKTAL);
        ar(rs.RPMD, rs.RPRCTL, rs.KTCTL, rs.KTAOF);
        ar(rs.OVPNRA, rs.OVPNRB);
        ar(rs.RPTAU, rs.RPTAL);
        ar(rs.WPSX0, rs.WPSY0, rs.WPEX0, rs.WPEY0);
        ar(rs.WPSX1, rs.WPSY1, rs.WPEX1, rs.WPEY1);
        ar(rs.WCTLA, rs.WCTLB, rs.WCTLC, rs.WCTLD);
        ar(rs.LWTA0U, rs.LWTA0L, rs.LWTA1U, rs.LWTA1L);
        ar(rs.SPCTL, rs.SDCTL);
        ar(rs.CRAOFA, rs.CRAOFB);
        ar(rs.LNCLEN);
        ar(rs.SFPRMD);
        ar(rs.CCCTL, rs.SFCCMD);
        ar(rs.PRISA, rs.PRISB, rs.PRISC, rs.PRISD, rs.PRINA, rs.PRINB, rs.PRIR);
        ar(rs.CCRSA, rs.CCRSB, rs.CCRSC, rs.CCRSD, rs.CCRNA, rs.CCRNB, rs.CCRR);
        ar(rs.CCRLB);
        ar(rs.CLOFEN, rs.CLOFSL);
        ar(rs.COAR, rs.COAG, rs.COAB);
        ar(rs.COBR, rs.COBG, rs.COBB);
    }

    // -------------------------------------------------------------------------

    ar(s.HPhase, s.VPhase);
    if (version < 6) {
        uint16 VCounter;
        ar(VCounter);
        s.regs2.VCNT = VCounter;
    }
    if (version < 9) {
        // The VCNT skip phase timing was slightly shifted. Adjust the phase here.
        uint16 lowerBound, upperBound;
        if (s.regs2.TVSTAT & 1) {
            // PAL
            switch (s.regs2.TVMD & 3) {
            case 0:
                lowerBound = 259;
                upperBound = 281;
                break;
            case 1:
                lowerBound = 267;
                upperBound = 289;
                break;
            case 2: [[fallthrough]];
            case 3:
                lowerBound = 275;
                upperBound = 297;
                break;
            }
        } else {
            // NTSC
            lowerBound = (s.regs2.TVMD & 1) ? 245 : 237;
            upperBound = 255;
        }
        if (s.regs2.VCNT >= lowerBound && s.regs2.VCNT < upperBound) {
            s.VPhase = VDPSaveState::VerticalPhase::VCounterSkip;
        }

        // Replace obsolete horizontal phases
        switch (static_cast<uint8>(s.HPhase)) {
        case 3 /*VBlankOut*/: s.HPhase = VDPSaveState::HorizontalPhase::Sync; break;
        case 5 /*LastDot*/: s.HPhase = VDPSaveState::HorizontalPhase::LeftBorder; break;
        default: break;
        }
    }

    // -------------------------------------------------------------------------

    {
        auto &rs = s.renderer;
        ar(rs.vdp1State.sysClipH, rs.vdp1State.sysClipV);
        if (version >= 9) {
            ar(rs.vdp1State.doubleV);
        } else {
            rs.vdp1State.doubleV = 0;
        }
        ar(rs.vdp1State.userClipX0, rs.vdp1State.userClipY0, rs.vdp1State.userClipX1, rs.vdp1State.userClipY1);
        ar(rs.vdp1State.localCoordX, rs.vdp1State.localCoordY);
        ar(s.vdp1State.drawing);
        ar(s.vdp1State.doDisplayErase);
        if (version >= 9) {
            ar(s.vdp1State.doVBlankErase);
            ar(s.regs1.eraseWriteValueLatch);
            ar(s.regs1.eraseX1Latch, s.regs1.eraseX3Latch);
            ar(s.regs1.eraseY1Latch, s.regs1.eraseY3Latch);
        } else {
            // Convert old "erase" value into new doDisplayErase and doVBlankErase flags
            const bool erase = s.vdp1State.doDisplayErase;
            const bool vbe = bit::test<3>(s.regs1.TVMR);
            s.vdp1State.doDisplayErase &= !vbe;
            s.vdp1State.doVBlankErase = erase && vbe;

            // Copy VDP1 register values to latched registers
            s.regs1.eraseWriteValueLatch = s.regs1.EWDR;
            s.regs1.eraseX1Latch = bit::extract<9, 14>(s.regs1.EWLR) << 3;
            s.regs1.eraseY1Latch = bit::extract<0, 8>(s.regs1.EWLR);
            s.regs1.eraseX3Latch = bit::extract<9, 15>(s.regs1.EWRR) << 3;
            s.regs1.eraseY3Latch = bit::extract<0, 8>(s.regs1.EWRR);
        }
        if (version >= 12) {
            ar(s.vdp1State.spilloverCycles);
        } else {
            // Old cycleCount and cyclesSpent can't be neatly converted into the new spilloverCycles
            s.vdp1State.spilloverCycles = 0;
            uint64 cycleCount;
            ar(cycleCount);
            if (version >= 9) {
                uint64 cyclesSpent;
                ar(cyclesSpent);
            }
        }
        if (version >= 9) {
            ar(rs.vdp1State.meshFBRAM);
        } else {
            rs.vdp1State.meshFBRAM[0][0].fill(0);
            rs.vdp1State.meshFBRAM[0][1].fill(0);
            rs.vdp1State.meshFBRAM[1][0].fill(0);
            rs.vdp1State.meshFBRAM[1][1].fill(0);
        }

        for (auto &state : rs.nbgLayerStates) {
            serialize(ar, state, version);
        }
        for (auto &state : rs.rotParamStates) {
            serialize(ar, state, version);
        }
        ar(rs.lineBackLayerState);
        if (version >= 7) {
            for (auto &fieldFetchers : rs.vramFetchers) {
                for (auto &fetcher : fieldFetchers) {
                    serialize(ar, fetcher, version);
                }
            }
        } else {
            for (auto &fieldFetchers : rs.vramFetchers) {
                for (auto &fetcher : fieldFetchers) {
                    fetcher = {};
                }
            }
        }
        if (version >= 4) {
            ar(rs.vcellScrollInc);
        } else {
            rs.vcellScrollInc = sizeof(uint32);
        }
        if (version <= 14) {
            uint8 displayFB;
            ar(displayFB);
        }
        if (version <= 10) {
            bool vdp1Done;
            ar(vdp1Done);
        }

        if (version < 4) {
            // Compensate for the removal of SCXIN/SCYIN from fracScrollX/Y
            rs.nbgLayerStates[0].fracScrollX -= (s.regs2.SCXIN0 << 8u) | (s.regs2.SCXDN0 >> 8u);
            rs.nbgLayerStates[1].fracScrollX -= (s.regs2.SCXIN1 << 8u) | (s.regs2.SCXDN1 >> 8u);
            rs.nbgLayerStates[2].fracScrollX -= (s.regs2.SCXIN2 << 8u);
            rs.nbgLayerStates[3].fracScrollX -= (s.regs2.SCXIN3 << 8u);

            rs.nbgLayerStates[0].fracScrollY -= (s.regs2.SCYIN0 << 8u) | (s.regs2.SCYDN0 >> 8u);
            rs.nbgLayerStates[1].fracScrollY -= (s.regs2.SCYIN1 << 8u) | (s.regs2.SCYDN1 >> 8u);
            rs.nbgLayerStates[2].fracScrollY -= (s.regs2.SCYIN2 << 8u);
            rs.nbgLayerStates[3].fracScrollY -= (s.regs2.SCYIN3 << 8u);
        }
    }

    // -------------------------------------------------------------------------

    if (version >= 9) {
        ar(s.regs2.displayEnabledLatch, s.regs2.borderColorModeLatch);
    } else {
        s.regs2.displayEnabledLatch = bit::test<15>(s.regs2.TVMD);
        s.regs2.borderColorModeLatch = bit::test<8>(s.regs2.TVMD);
    }
}

template <class Archive>
void serialize(Archive &ar, VDPSaveState::VDPRendererSaveState::NBGLayerSaveState &s, const uint32 version) {
    // v12:
    // - Removed fields
    //   - scrollAmountV
    // v7:
    // - New fields
    //   - nbgLayerStates[0].scrollAmountV = (regs2.SCYIN0 << 8u) | (regs2.SCYDN0 >> 8u);
    //   - nbgLayerStates[1].scrollAmountV = (regs2.SCYIN1 << 8u) | (regs2.SCYDN1 >> 8u);
    //   - nbgLayerStates[2].scrollAmountV = (regs2.SCYIN2 << 8u);
    //   - nbgLayerStates[3].scrollAmountV = (regs2.SCYIN3 << 8u);
    //   - vcellScrollDelay = false
    //   - vcellScrollRepeat = false
    // v4:
    // - New fields
    //   - vcellScrollOffset = 0
    // - Changed fields
    //   - fracScrollX and fracScrollY no longer include the values of SC[XY][ID]N#. Therefore, they need to be
    //     compensated for as follows:
    //       nbgLayerStates[0].fracScrollX -= (regs2.SCXIN0 << 8u) | (regs2.SCXDN0 >> 8u);
    //       nbgLayerStates[1].fracScrollX -= (regs2.SCXIN1 << 8u) | (regs2.SCXDN1 >> 8u);
    //       nbgLayerStates[2].fracScrollX -= (regs2.SCXIN2 << 8u);
    //       nbgLayerStates[3].fracScrollX -= (regs2.SCXIN3 << 8u);
    //
    //       nbgLayerStates[0].fracScrollY -= (regs2.SCYIN0 << 8u) | (regs2.SCYDN0 >> 8u);
    //       nbgLayerStates[1].fracScrollY -= (regs2.SCYIN1 << 8u) | (regs2.SCYDN1 >> 8u);
    //       nbgLayerStates[2].fracScrollY -= (regs2.SCYIN2 << 8u);
    //       nbgLayerStates[3].fracScrollY -= (regs2.SCYIN3 << 8u);

    // NOTE: fracScrollX/Y compensation happens in the VDPSaveState serializer
    ar(s.fracScrollX, s.fracScrollY, s.scrollIncH);
    if (version >= 7 && version < 12) {
        uint32 scrollAmountV;
        ar(scrollAmountV);
    }
    ar(s.lineScrollTableAddress);
    if (version >= 4) {
        ar(s.vcellScrollOffset);
    } else {
        s.vcellScrollOffset = 0;
    }
    if (version >= 7) {
        ar(s.vcellScrollDelay, s.vcellScrollRepeat);
    } else {
        s.vcellScrollDelay = false;
        s.vcellScrollRepeat = false;
    }
    ar(s.mosaicCounterY);
}

template <class Archive>
void serialize(Archive &ar, VDPSaveState::VDPRendererSaveState::RotationParamSaveState &s, const uint32 version) {
    // v9:
    // - Changed fields
    //   - pageBaseAddresses changed from std::array<uint32, 16> to std::array<std::array<uint32, 16>, 2>
    // v8:
    // - New fields
    //   - Xst = 0
    //   - Yst = 0
    // - Removed fields
    //   - sint32 scrX
    //   - sint32 scrY
    if (version >= 9) {
        ar(s.pageBaseAddresses);
    } else {
        ar(s.pageBaseAddresses[0]);
        s.pageBaseAddresses[1] = s.pageBaseAddresses[0];
    }
    if (version >= 8) {
        ar(s.Xst, s.Yst);
    } else {
        sint32 scrX, scrY;
        ar(scrX, scrY);
        s.Xst = 0;
        s.Yst = 0;
    }
    ar(s.KA);
}

template <class Archive>
void serialize(Archive &ar, VDPSaveState::VDPRendererSaveState::LineBackLayerSaveState &s) {
    ar(s.lineColor);
    ar(s.backColor);
}

template <class Archive>
void serialize(Archive &ar, VDPSaveState::VDPRendererSaveState::CharacterSaveState &s) {
    // v7:
    // - Struct created
    ar(s.charNum, s.palNum);
    ar(s.specColorCalc, s.specPriority);
    ar(s.flipH, s.flipV);
}

template <class Archive>
void serialize(Archive &ar, VDPSaveState::VDPRendererSaveState::VRAMFetcherSaveState &s, const uint32 version) {
    // v12:
    // - Renamed fields
    //   - bitmapData -> charData
    //   - bitmapDataAddress -> charDataAddress
    // v10:
    // - New fields
    //   - lastCellX = 0
    // v7:
    // - Struct created

    if (version >= 7) {
        ar(s.currChar, s.nextChar, s.lastCharIndex);
        if (version >= 10) {
            ar(s.lastCellX);
        }
        ar(s.charData, s.charDataAddress);
        ar(s.lastVCellScroll);
    } else {
        s.currChar = {};
        s.nextChar = {};
        s.lastCharIndex = 0xFFFFFFFF;
        s.charData.fill(0);
        s.charDataAddress = 0xFFFFFFFF;
        s.lastVCellScroll = 0;
    }
}

template <class Archive>
void serialize(Archive &ar, M68KSaveState &s) {
    ar(s.DA, s.SP_swap, s.PC, s.SR);
    ar(s.prefetchQueue, s.extIntrLevel);
}

template <class Archive>
void serialize(Archive &ar, SCSPSaveState &s, const uint32 version) {
    // v6:
    // - New fields
    //   - SCILV = {0,0,0}
    //     (unfortunately the data is missing, so old save states will never restore properly)
    //   - reuseSCILV = true if version < 6, false otherwise; not stored in save state binary
    //   - KYONEXExec = false
    //   - currSlot = 0
    //   - out = {0,0}
    //   - midiInputBuffer = {0,0,0,...}
    //   - midiInputReadPos = 0
    //   - midiInputWritePos = 0
    //   - midiInputOverflow = false
    //   - midiOutputBuffer = {0,0,0,...}
    //   - midiOutputSize = 0
    //   - expectedOutputPacketSize = 0
    // - Removed fields
    //   - uint64 sampleCycles
    // - Misc changes
    //   - DAC18B and MEM4MB were swapped
    // v5:
    // - Changed fields
    //   - cddaBuffer array size reduced from 2048 * 75 to 2352 * 25; note that this is a circular buffer indexed by
    //     cddaReadPos and cddaWritePos
    // v4:
    // - Removed fields
    //   - uint16 egCycle
    //   - bool egStep
    // v3:
    // - New fields
    //   - KYONEX = false

    ar(s.WRAM);
    if (version >= 5) {
        ar(s.cddaBuffer, s.cddaReadPos, s.cddaWritePos, s.cddaReady);
    } else {
        // Reconstruct circular buffer
        auto cddaBuffer = std::make_unique<std::array<uint8, 2048 * 75>>();
        uint32 cddaReadPos, cddaWritePos;
        ar(*cddaBuffer, cddaReadPos, cddaWritePos, s.cddaReady);
        if (cddaReadPos >= 2048 * 75 || cddaWritePos >= 2048 * 75) {
            throw cereal::Exception("Illegal CDDA buffer positions");
        }

        // Use the most recent samples if there is too much data in the old buffer since the new buffer is smaller
        uint32 count = cddaWritePos - cddaReadPos;
        if (cddaWritePos < cddaReadPos) {
            count += 2048 * 75;
        } else if (cddaWritePos == cddaReadPos && s.cddaReady) {
            count = 2048 * 75;
        }
        if (count > s.cddaBuffer.size()) {
            cddaReadPos += count - s.cddaBuffer.size();
            if (cddaReadPos >= 2048 * 75) {
                cddaReadPos -= 2048 * 75;
            }
            count = s.cddaBuffer.size();
        }

        if (cddaWritePos < cddaReadPos) {
            // ======W-----R======
            s.cddaReadPos = 0;
            s.cddaWritePos = cddaWritePos - cddaReadPos + 2048 * 75;
            if (s.cddaWritePos >= s.cddaBuffer.size()) {
                s.cddaWritePos -= s.cddaBuffer.size();
            }
            std::copy(cddaBuffer->begin() + cddaReadPos, cddaBuffer->end(), s.cddaBuffer.begin());
            std::copy_n(cddaBuffer->begin(), cddaWritePos, s.cddaBuffer.begin() + (2048 * 75 - cddaReadPos));
        } else if (cddaWritePos > cddaReadPos) {
            // ------R=====W------
            s.cddaReadPos = 0;
            s.cddaWritePos = cddaWritePos - cddaReadPos;
            if (s.cddaWritePos >= s.cddaBuffer.size()) {
                s.cddaWritePos -= s.cddaBuffer.size();
            }
            std::copy(cddaBuffer->begin() + cddaReadPos, cddaBuffer->begin() + cddaWritePos, s.cddaBuffer.begin());
        } else if (s.cddaReady) {
            // Buffer is full
            // NOTE: this case should never happen since the target buffer is smaller than the source buffer and the
            // copy length is clamped before reaching this if-else chain
            std::copy(cddaBuffer->begin() + cddaReadPos, cddaBuffer->end(), s.cddaBuffer.begin());
            std::copy(cddaBuffer->begin(), cddaBuffer->begin() + cddaWritePos, s.cddaBuffer.begin() + cddaReadPos);
            s.cddaReadPos = 0;
            s.cddaWritePos = 0;
        } else {
            // Buffer is empty
            s.cddaBuffer.fill(0);
            s.cddaReadPos = 0;
            s.cddaWritePos = 0;
        }
    }
    ar(s.m68k, s.m68kSpilloverCycles, s.m68kEnabled);
    for (auto &slot : s.slots) {
        serialize(ar, slot, version);
    }
    if (version >= 3) {
        ar(s.KYONEX);
    } else {
        s.KYONEX = false;
    }
    if (version >= 6) {
        ar(s.KYONEXExec);
    } else {
        s.KYONEXExec = false;
    }
    ar(s.MVOL);
    if (version >= 6) {
        ar(s.DAC18B, s.MEM4MB);
    } else {
        ar(s.MEM4MB, s.DAC18B);
    }
    ar(s.MSLC);
    ar(s.timers);
    ar(s.MCIEB, s.MCIPD);
    ar(s.SCIEB, s.SCIPD);
    if (version >= 6) {
        ar(s.SCILV);
        s.reuseSCILV = false;
    } else {
        s.SCILV.fill(0);
        s.reuseSCILV = true;
    }
    ar(s.DEXE, s.DDIR, s.DGATE, s.DMEA, s.DRGA, s.DTLG);
    ar(s.SOUS, s.soundStackIndex);
    serialize(ar, s.dsp, version);
    ar(s.m68kCycles);
    if (version < 6) {
        uint64 sampleCycles;
        ar(sampleCycles);
    }
    ar(s.sampleCounter);
    if (version < 4) {
        uint16 egCycle;
        bool egStep;
        ar(egCycle, egStep);
    }
    ar(s.lfsr);
    if (version >= 6) {
        ar(s.currSlot);
        ar(s.out);
    } else {
        s.currSlot = 0;
        s.out.fill(0);
    }
    if (version >= 6) {
        ar(s.midiInputBuffer);
        ar(s.midiInputReadPos);
        ar(s.midiInputWritePos);
        ar(s.midiInputOverflow);

        ar(s.midiOutputBuffer);
        ar(s.midiOutputSize);
        ar(s.expectedOutputPacketSize);
    } else {
        s.midiInputBuffer.fill(0);
        s.midiInputReadPos = 0;
        s.midiInputWritePos = 0;
        s.midiInputOverflow = false;

        s.midiOutputBuffer.fill(0);
        s.midiOutputSize = 0;
        s.expectedOutputPacketSize = 0;
    }
}

template <class Archive>
void serialize(Archive &ar, SCSPSlotSaveState &s, const uint32 version) {
    // v6:
    // - New fields
    //   - currEGLevel = egLevel
    // - Removed fields
    //   - uint32 sampleCount
    // v4:
    // - New fields
    //   - MM = bit 15 of extra10 if available, otherwise false
    //   - modulation = 0
    //   - egAttackBug = false
    //   - finalLevel = 0
    // - Removed fields
    //   - uint16 extra10
    //   - uint32 currAddress
    // - Changed fields
    //   - LSA and LEA changed from uint32 to uint16
    // v3:
    // - New fields
    //   - SBCTL = 0
    //   - EGBYPASS = false
    //   - extra0C = 0
    //   - extra10 = 0
    //   - extra14 = 0
    //   - nextPhase = currPhase
    //   - alfoOutput = 0
    // - Changed fields
    //   - currPhase >>= 4u

    ar(s.SA);
    if (version >= 4) {
        ar(s.LSA, s.LEA);
    } else {
        uint32 tmp;
        ar(tmp);
        s.LSA = tmp;
        ar(tmp);
        s.LEA = tmp;
    }
    ar(s.PCM8B, s.KYONB);
    if (version >= 3) {
        ar(s.SBCTL);
    } else {
        s.SBCTL = 0;
    }
    ar(s.LPCTL);
    ar(s.SSCTL);
    ar(s.AR, s.D1R, s.D2R, s.RR, s.DL);
    ar(s.KRS, s.EGHOLD, s.LPSLNK);
    if (version >= 3) {
        ar(s.EGBYPASS);
    } else {
        s.EGBYPASS = false;
    }
    ar(s.MDL, s.MDXSL, s.MDYSL, s.STWINH);
    ar(s.TL, s.SDIR);
    ar(s.OCT, s.FNS);
    if (version >= 4) {
        ar(s.MM);
    } else {
        s.MM = false;
    }
    ar(s.LFORE, s.LFOF, s.ALFOS, s.PLFOS, s.ALFOWS, s.PLFOWS);
    ar(s.IMXL, s.ISEL, s.DISDL, s.DIPAN);
    ar(s.EFSDL, s.EFPAN);
    if (version >= 3) {
        ar(s.extra0C);
        if (version == 3) {
            uint16 extra10;
            ar(extra10);
            s.MM = bit::test<15>(extra10);
        }
        ar(s.extra14);
    } else {
        s.extra0C = 0;
        s.extra14 = 0;
    }
    ar(s.active);
    ar(s.egState);
    ar(s.egLevel);
    if (version >= 6) {
        ar(s.currEGLevel);
    } else {
        s.currEGLevel = s.egLevel;
    }
    if (version >= 4) {
        ar(s.egAttackBug);
    } else {
        s.egAttackBug = false;
    }
    if (version < 6) {
        uint32 sampleCount;
        ar(sampleCount);
    }
    if (version < 4) {
        uint32 currAddress;
        ar(currAddress);
    }
    ar(s.currSample, s.currPhase);
    if (version < 3) {
        s.currPhase >>= 4u;
    }
    if (version >= 3) {
        ar(s.nextPhase);
    }
    if (version >= 4) {
        ar(s.modulation);
    } else {
        s.modulation = 0;
    }
    ar(s.reverse, s.crossedLoopStart);
    ar(s.lfoCycles, s.lfoStep);
    if (version >= 3) {
        ar(s.alfoOutput);
    } else {
        s.alfoOutput = 0;
    }
    ar(s.sample1, s.sample2, s.output);
    if (version >= 4) {
        ar(s.finalLevel);
    } else {
        s.finalLevel = 0;
    }
}

template <class Archive>
void serialize(Archive &ar, SCSPDSPSaveState &s, const uint32 version) {
    // v6:
    // - New fields
    //   - PC = 0x68
    //   - MIXSGen = 0
    //   - MIXStackNull = 0xFFFF
    // - Changed fields
    //   - TEMP entries changed from uint32 to sint32
    //   - MEMS entries changed from uint32 to sint32
    //   - MIXS increased from 16 to 16*2 entries
    //   - INPUTS changed from uint32 to sint32

    ar(s.MPRO, s.TEMP, s.MEMS, s.COEF, s.MADRS);
    if (version >= 6) {
        ar(s.MIXS, s.MIXSGen, s.MIXSNull);
    } else {
        std::array<sint32, 16> MIXS;
        ar(MIXS);
        std::copy_n(MIXS.begin(), MIXS.size(), s.MIXS.begin());
        std::fill(s.MIXS.begin() + MIXS.size(), s.MIXS.end(), 0);

        s.MIXSGen = 0;
        s.MIXSNull = 0xFFFF;
    }
    ar(s.EFREG, s.EXTS);
    ar(s.RBP, s.RBL);
    if (version >= 6) {
        ar(s.PC);
    } else {
        s.PC = 0x68;
    }
    ar(s.INPUTS);
    ar(s.SFT_REG, s.FRC_REG, s.Y_REG, s.ADRS_REG);
    ar(s.MDEC_CT);
    ar(s.readPending, s.readNOFL, s.readValue);
    ar(s.writePending, s.writeValue);
    ar(s.readWriteAddr);
}

template <class Archive>
void serialize(Archive &ar, SCSPTimerSaveState &s) {
    ar(s.incrementInterval);
    ar(s.reload);
    ar(s.doReload);
    ar(s.counter);
}

template <class Archive>
void serialize(Archive &ar, CDInterfaceSaveState &s, const uint32 version) {
    // v14:
    // - Struct added with fields:
    //   - uint32 seekTarget
    //   - uint32 seekFAD
    //   - bool seekDone
    ar(s.seekTarget);
    ar(s.seekFAD);
    ar(s.seekDone);
}

template <class Archive>
void serialize(Archive &ar, CDBlockSaveState &s, SaveState &root, const uint32 version) {
    // v13:
    // - New fields
    //   - RR = CR
    // v10:
    // - Removed fields
    //   - discHash moved to the root of the structure
    // v9:
    // - New fields
    //   - seekTicks = 0
    //   - playEndPending = false
    //   - xferGetLength = getSectorLength
    //   - xferDelCount = (xferLength + getSectorLength - 1) / getSectorLength if xferType == GetThenDeleteSector,
    //     otherwise 0
    //   - reservedBuffers = 0
    // v8:
    // - New fields
    //   - fs
    // v5:
    // - New fields
    //   - enum CDBlockSaveState::TransferType: added PutSector (= 6)
    //   - scratchBufferPutIndex = 0
    // - Removed fields
    //   - scratchBuffer moved into the buffers array

    if (version < 10) {
        ar(root.discHash);
        // v10+ is handled in the root serializer
    }
    ar(s.CR);
    if (version >= 13) {
        ar(s.RR);
    } else {
        s.RR = s.CR;
    }
    ar(s.HIRQ, s.HIRQMASK);
    serialize(ar, s.status, version);
    ar(s.readyForPeriodicReports);
    ar(s.currDriveCycles, s.targetDriveCycles);
    if (version >= 9) {
        ar(s.seekTicks);
    } else {
        s.seekTicks = 0;
    }
    ar(s.playStartParam, s.playEndParam, s.playRepeatParam, s.scanDirection, s.scanCounter);
    ar(s.playStartPos, s.playEndPos, s.playMaxRepeat, s.playFile, s.bufferFullPause);
    if (version >= 9) {
        ar(s.playEndPending);
    } else {
        s.playEndPending = false;
    }
    ar(s.readSpeed);
    ar(s.discAuthStatus, s.mpegAuthStatus);
    ar(s.xferType, s.xferPos, s.xferLength, s.xferCount, s.xferBuffer, s.xferBufferPos);
    ar(s.xferSectorPos, s.xferSectorEnd, s.xferPartition);
    if (version >= 9) {
        ar(s.xferGetLength, s.xferDelCount);
        // Default value handled below, after reading getSectorLength
    }
    ar(s.xferSubcodeFrameAddress, s.xferSubcodeGroup);
    ar(s.xferExtraCount);
    if (version >= 5) {
        for (auto &buffer : s.buffers) {
            serialize(ar, buffer, version);
        }
        ar(s.scratchBufferPutIndex);
    } else {
        // scratchBuffer was moved into the buffers array immediately after the partition buffers
        auto buffers = std::make_unique<std::array<CDBlockSaveState::BufferSaveState, cdblock::kNumBuffers>>();
        auto scratchBuffer = std::make_unique<CDBlockSaveState::BufferSaveState>();
        for (auto &buffer : *buffers) {
            serialize(ar, buffer, version);
        }
        ar(*scratchBuffer);

        // Copy entire buffers array
        std::copy_n(buffers->begin(), cdblock::kNumBuffers, s.buffers.begin());

        // Find a place for the scratch buffer immediately after the partition buffers
        for (uint32 i = 0; i < s.buffers.size(); ++i) {
            if (s.buffers[i].partitionIndex == 0xFF) {
                s.buffers[i] = *scratchBuffer;
                break;
            }
        }

        s.scratchBufferPutIndex = 0;
    }
    if (version >= 9) {
        ar(s.reservedBuffers);
    } else {
        s.reservedBuffers = 0;
    }
    ar(s.filters);
    ar(s.cdDeviceConnection, s.lastCDWritePartition);
    ar(s.calculatedPartitionSize);
    ar(s.getSectorLength, s.putSectorLength);
    ar(s.processingCommand);
    serialize(ar, s.fs, version);

    if (version < 9) {
        s.xferGetLength = s.getSectorLength;
        if (s.xferType == CDBlockSaveState::TransferType::GetThenDeleteSector) {
            s.xferDelCount = (s.xferLength + s.getSectorLength - 1) / s.getSectorLength;
        } else {
            s.xferDelCount = 0;
        }
    }
}

template <class Archive>
void serialize(Archive &ar, CDBlockSaveState::StatusSaveState &s, const uint32 version) {
    // v9:
    // - Changed fields
    //   - statusCode no longer includes flags (&= 0xF)

    ar(s.statusCode);
    ar(s.frameAddress);
    ar(s.flags);
    ar(s.repeatCount);
    ar(s.controlADR);
    ar(s.track);
    ar(s.index);

    if (version < 9) {
        s.statusCode &= 0xF;
    }
}

template <class Archive>
void serialize(Archive &ar, CDBlockSaveState::BufferSaveState &s, const uint32 version) {
    ar(s.data, s.size);
    ar(s.frameAddress);
    ar(s.fileNum, s.chanNum, s.submode, s.codingInfo);
    ar(s.partitionIndex);
}

template <class Archive>
void serialize(Archive &ar, CDBlockSaveState::FilterSaveState &s) {
    // v5:
    // - Changed fields:
    //   - trueOutput renamed to passOutput; no changes to value
    //   - falseOutput renamed to failOutput; no changes to value

    ar(s.startFrameAddress, s.frameAddressCount);
    ar(s.mode);
    ar(s.fileNum, s.chanNum);
    ar(s.submodeMask, s.submodeValue);
    ar(s.codingInfoMask, s.codingInfoValue);
    ar(s.passOutput, s.failOutput);
}

template <class Archive>
void serialize(Archive &ar, CDBlockSaveState::FilesystemSaveState &s, const uint32 version) {
    // v8:
    // - Struct newly introduced
    // - New fields:
    //   - currDirectory = 0; not much can be done since the information is completely missing from earlier versions
    //   - currFileOffset = 0

    if (version >= 8) {
        ar(s.currDirectory, s.currFileOffset);
    } else {
        s.currDirectory = 0;
        s.currFileOffset = 0;
    }
}

template <class Archive>
void serialize(Archive &ar, SH1SaveState &s, const uint32 version) {
    // v10:
    // - Struct newly introduced

    if (version >= 10) {
        ar(s.R, s.PC, s.PR, s.MACL, s.MACH, s.SR, s.GBR, s.VBR);
        ar(s.delaySlot, s.delaySlotTarget);
        ar(s.totalCycles);
        ar(s.romHash);
        ar(s.onChipRAM);
        serialize(ar, s.bsc, version);
        serialize(ar, s.dmac, version);
        serialize(ar, s.itu, version);
        serialize(ar, s.tpc, version);
        serialize(ar, s.wdt, version);
        serialize(ar, s.sci, version);
        serialize(ar, s.ad, version);
        serialize(ar, s.pfc, version);
        serialize(ar, s.intc, version);
        ar(s.SBYCR);
        ar(s.sleep);
        ar(s.nDREQ, s.PB2, s.TIOCB3);
    }
    // No need to initialize on pre-v10 because CD Block LLE did not exist
}

template <class Archive>
void serialize(Archive &ar, SH1SaveState::BSC &s, const uint32 version) {
    // v10:
    // - Struct newly introduced

    if (version >= 10) {
        ar(s.BCR);
        ar(s.WCR1, s.WCR2, s.WCR3);
        ar(s.DCR);
        ar(s.PCR);
        ar(s.RCR);
        ar(s.RTCSR);
        ar(s.RTCNT);
        ar(s.RTCOR);
    }
    // No need to initialize on pre-v10 because CD Block LLE did not exist
}

template <class Archive>
void serialize(Archive &ar, SH1SaveState::DMAC &s, const uint32 version) {
    // v10:
    // - Struct newly introduced

    if (version >= 10) {
        for (auto &ch : s.channels) {
            serialize(ar, ch, version);
        }
        ar(s.DMAOR);
        ar(s.AEread, s.NMIFread);
    }
    // No need to initialize on pre-v10 because CD Block LLE did not exist
}

template <class Archive>
void serialize(Archive &ar, SH1SaveState::DMAC::Channel &s, const uint32 version) {
    // v10:
    // - Struct newly introduced

    if (version >= 10) {
        ar(s.SAR);
        ar(s.DAR);
        ar(s.TCR);
        ar(s.CHCR);
        ar(s.xferEndedMask);
    }
    // No need to initialize on pre-v10 because CD Block LLE did not exist
}

template <class Archive>
void serialize(Archive &ar, SH1SaveState::ITU &s, const uint32 version) {
    // v10:
    // - Struct newly introduced

    if (version >= 10) {
        for (auto &timer : s.timers) {
            serialize(ar, timer, version);
        }
        ar(s.TSTR);
        ar(s.TSNC);
        ar(s.TMDR);
        ar(s.TFCR);
        ar(s.TOCR);
    }
    // No need to initialize on pre-v10 because CD Block LLE did not exist
}

template <class Archive>
void serialize(Archive &ar, SH1SaveState::ITU::Timer &s, const uint32 version) {
    // v10:
    // - Struct newly introduced

    if (version >= 10) {
        ar(s.TCR);
        ar(s.TIOR);
        ar(s.TIER);
        ar(s.TSR);
        ar(s.TCNT);
        ar(s.GRA);
        ar(s.GRB);
        ar(s.BRA);
        ar(s.BRB);
        ar(s.IMFAread, s.IMFBread, s.OVFread);
        ar(s.currCycles);
    }
    // No need to initialize on pre-v10 because CD Block LLE did not exist
}

template <class Archive>
void serialize(Archive &ar, SH1SaveState::TPC &s, const uint32 version) {
    // v10:
    // - Struct newly introduced

    if (version >= 10) {
        ar(s.TPMR);
        ar(s.TPCR);
        ar(s.NDERB, s.NDERA);
        ar(s.NDRB, s.NDRA);
    }
    // No need to initialize on pre-v10 because CD Block LLE did not exist
}

template <class Archive>
void serialize(Archive &ar, SH1SaveState::WDT &s, const uint32 version) {
    // v10:
    // - Struct newly introduced

    if (version >= 10) {
        ar(s.TCSR);
        ar(s.TCNT);
        ar(s.RSTCSR);
        ar(s.OVFread);
        ar(s.cycleCount);
    }
    // No need to initialize on pre-v10 because CD Block LLE did not exist
}

template <class Archive>
void serialize(Archive &ar, SH1SaveState::SCI &s, const uint32 version) {
    // v10:
    // - Struct newly introduced

    if (version >= 10) {
        for (auto &ch : s.channels) {
            serialize(ar, ch, version);
        }
    }
    // No need to initialize on pre-v10 because CD Block LLE did not exist
}

template <class Archive>
void serialize(Archive &ar, SH1SaveState::SCI::Channel &s, const uint32 version) {
    // v10:
    // - Struct newly introduced

    if (version >= 10) {
        ar(s.SMR);
        ar(s.BRR);
        ar(s.SCR);
        ar(s.TDR, s.TDRvalid);
        ar(s.SSR);
        ar(s.RDR);
        ar(s.RSR, s.RSRbit);
        ar(s.TSR, s.TSRbit);
        ar(s.txEmptyMask, s.rxFullMask);
        ar(s.overrunErrorMask, s.framingErrorMask, s.parityErrorMask);
        ar(s.currCycles);
    }
    // No need to initialize on pre-v10 because CD Block LLE did not exist
}

template <class Archive>
void serialize(Archive &ar, SH1SaveState::AD &s, const uint32 version) {
    // v10:
    // - Struct newly introduced

    if (version >= 10) {
        ar(s.ADDR);
        ar(s.ADCSR);
        ar(s.ADCR);
        ar(s.TEMP);
        ar(s.convEndedMask);
    }
    // No need to initialize on pre-v10 because CD Block LLE did not exist
}

template <class Archive>
void serialize(Archive &ar, SH1SaveState::PFC &s, const uint32 version) {
    // v10:
    // - Struct newly introduced

    if (version >= 10) {
        ar(s.PAIOR);
        ar(s.PBIOR);
        ar(s.PACR1, s.PACR2);
        ar(s.PBCR1, s.PBCR2);
        ar(s.CASCR);
    }
    // No need to initialize on pre-v10 because CD Block LLE did not exist
}

template <class Archive>
void serialize(Archive &ar, SH1SaveState::INTC &s, const uint32 version) {
    // v10:
    // - Struct newly introduced

    if (version >= 10) {
        ar(s.ICR);
        ar(s.levels);
        ar(s.vectors);
        ar(s.pendingSource);
        ar(s.pendingLevel);
        ar(s.NMI);
        ar(s.irqs);
    }
    // No need to initialize on pre-v10 because CD Block LLE did not exist
}

template <class Archive>
void serialize(Archive &ar, YGRSaveState &s, const uint32 version) {
    // v10:
    // - Struct newly introduced

    if (version >= 10) {
        serialize(ar, s.fifo, version);
        serialize(ar, s.regs, version);
    }
    // No need to initialize on pre-v10 because CD Block LLE did not exist
}

template <class Archive>
void serialize(Archive &ar, YGRSaveState::FIFOState &s, const uint32 version) {
    // v10:
    // - Struct newly introduced

    if (version >= 10) {
        ar(s.data);
        ar(s.readPos, s.writePos);
        ar(s.count);
    }
    // No need to initialize on pre-v10 because CD Block LLE did not exist
}

template <class Archive>
void serialize(Archive &ar, YGRSaveState::Registers &s, const uint32 version) {
    // v10:
    // - Struct newly introduced

    if (version >= 10) {
        ar(s.TRCTL);
        ar(s.CDIRQL, s.CDIRQU);
        ar(s.CDMSKL, s.CDMSKU);
        ar(s.REG0C);
        ar(s.REG0E);
        ar(s.CR, s.RR);
        ar(s.REG18);
        ar(s.REG1A);
        ar(s.REG1C);
        ar(s.HIRQ, s.HIRQMASK);
    }
    // No need to initialize on pre-v10 because CD Block LLE did not exist
}

template <class Archive>
void serialize(Archive &ar, CDDriveSaveState &s, const uint32 version) {
    // v10:
    // - Struct newly introduced

    if (version >= 10) {
        ar(s.autoCloseTray);
        ar(s.sectorDataBuffer);
        ar(s.commandData, s.commandPos);
        ar(s.statusData, s.statusPos);
        serialize(ar, s.status, version);
        ar(s.state);
        ar(s.currFAD, s.targetFAD);
        ar(s.seekOp, s.seekCountdown);
        ar(s.scan, s.scanDirection, s.scanCounter);
        ar(s.currTOCEntry, s.currTOCRepeat);
        ar(s.readSpeed);
    }
    // No need to initialize on pre-v10 because CD Block LLE did not exist
}

template <class Archive>
void serialize(Archive &ar, CDDriveSaveState::CDStatusSaveState &s, const uint32 version) {
    // v10:
    // - Struct newly introduced

    if (version >= 10) {
        ar(s.operation);
        ar(s.subcodeQ);
        ar(s.trackNum);
        ar(s.indexNum);
        ar(s.min);
        ar(s.sec);
        ar(s.frame);
        ar(s.zero);
        ar(s.absMin);
        ar(s.absSec);
        ar(s.absFrame);
    }
    // No need to initialize on pre-v10 because CD Block LLE did not exist
}

template <class Archive>
void serialize(Archive &ar, SaveState &s, const uint32 version) {
    // v14:
    // - New fields:
    //   - cdif = default
    // v10:
    // - Every component now has a 4-byte magic field to check for data alignment
    // - New fields:
    //   - discHash = CDBlock.discHash
    //   - cdblockLLE = false
    //     - valid fields depend on this value:
    //       - false: cdblock
    //       - true: sh1, ygr, cddrive, CDBROMHash
    //   - sh1 = default
    //   - ygr = default
    //   - cddrive = default
    //   - CDBROMHash = default
    //   - uint64 sh1SpilloverCycles = 0
    //   - uint64 sh1FracCycles = 0
    // v8:
    // - New fields:
    //   - uint64 msh2SpilloverCycles = 0
    // v5:
    // - New fields:
    //   - uint64 ssh2SpilloverCycles = 0

    // Ignore version 0 (empty save state)
    if (version == 0) {
        return;
    }
    // Reject future versions
    if (version > kVersion) {
        throw cereal::Exception(
            fmt::format("Save state version is higher than supported ({} > {})", version, kVersion));
    }

    auto magic = [&](auto expected) {
        if (version < 10) {
            return;
        }
        std::array<char, 4> expect{expected[0], expected[1], expected[2], expected[3]};
        std::array<char, 4> actual = expect;
        ar(actual);

        if (expect != actual) {
            throw cereal::Exception(fmt::format("Could not find expected magic '{}'", expected));
        }
    };

    // NOTE: serialize is invoked manually here to handle versioned and non-versioned (pre-v4) variants
    magic("Schd"), serialize(ar, s.scheduler, version);
    magic("Syst"), serialize(ar, s.system);
    magic("MSH2"), serialize(ar, s.msh2, version);
    magic("SSH2"), serialize(ar, s.ssh2, version);
    magic("SCU_"), serialize(ar, s.scu, version);
    magic("SMPC"), serialize(ar, s.smpc, version);
    magic("VDP#"), serialize(ar, s.vdp, version);
    magic("SCSP"), serialize(ar, s.scsp, version);
    if (version >= 14) {
        magic("CDIf"), serialize(ar, s.cdif, version);
    }
    magic("CDBl");
    if (version >= 10) {
        magic("cLLE"), ar(s.cdblockLLE);
        magic("CD##"), ar(s.discHash);
    } else {
        s.cdblockLLE = false;
        // v1-v9 discHash is handled in the CDBlock serializer
    }
    if (s.cdblockLLE) {
        magic("cSH1"), serialize(ar, s.sh1, version);
        ar(s.sh1SpilloverCycles, s.sh1FracCycles);
        magic("cYGR"), serialize(ar, s.ygr, version);
        magic("cCDD"), serialize(ar, s.cddrive, version);
        magic("cRAM"), ar(s.cdblockDRAM);
    } else {
        // Passing in the root of the save state structure to allow the CDBlockSaveState serializer to load the disc
        // hash into the field that was moved to the root State struct.
        magic("cCDB"), serialize(ar, s.cdblock, s, version);
        s.sh1SpilloverCycles = 0;
        s.sh1FracCycles = 0;
    }

    magic("MISC");
    if (version >= 8) {
        ar(s.msh2SpilloverCycles);
    } else {
        s.msh2SpilloverCycles = 0;
    }
    if (version >= 5) {
        ar(s.ssh2SpilloverCycles);
    } else {
        s.ssh2SpilloverCycles = 0;
    }
    magic("END_");

    if (version < 5) {
        // Fixup FRT and WDT cycle counters which changed from local to global
        s.msh2.frt.cycleCount = s.scheduler.currCount - s.msh2.frt.cycleCount;
        s.msh2.wdt.cycleCount = s.scheduler.currCount - s.msh2.wdt.cycleCount;
        s.ssh2.frt.cycleCount = s.scheduler.currCount - s.ssh2.frt.cycleCount;
        s.ssh2.wdt.cycleCount = s.scheduler.currCount - s.ssh2.wdt.cycleCount;
    }
}

} // namespace ymir::savestate
