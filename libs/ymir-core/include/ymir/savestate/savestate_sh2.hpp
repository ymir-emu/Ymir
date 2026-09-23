#pragma once

#include <ymir/core/types.hpp>

#include <array>

namespace ymir::savestate {

struct SH2SaveState {
    alignas(16) std::array<uint32, 16> R;

    uint32 PC;
    uint32 PR;

    uint32 MACL;
    uint32 MACH;

    uint32 SR;

    uint32 GBR;
    uint32 VBR;

    uint32 delaySlotTarget;
    bool delaySlot;
    bool intrAllow;

    uint32 fetchedOpcodes;
    bool forceFetchOpcodes; // for compatibility with older save states

    uint8 wbReg;

    struct BSC {
        uint16 BCR1;
        uint16 BCR2;
        uint16 WCR;
        uint16 MCR;
        uint16 RTCSR;
        uint16 RTCNT;
        uint16 RTCOR;
    } bsc;

    struct SCI {
        uint8 SMR = 0x00;
        uint8 BRR = 0xFF;
        uint8 SCR = 0x00;
        uint8 TDR = 0xFF;
        uint8 SSR = 0x84;
        uint8 RDR = 0x00;
        uint8 observedStatus = 0x00;
    } sci;

    struct DMAC {
        struct Channel {
            uint32 SAR;
            uint32 DAR;
            uint32 TCR;
            uint32 CHCR;
            uint8 DRCR;
        };

        uint32 DMAOR;
        std::array<Channel, 2> channels;
    } dmac;

    struct WDT {
        uint8 WTCSR;
        uint8 WTCNT;
        uint8 RSTCSR;
        uint64 cycleCount;
        bool WTCSR_mask;
        uint8 busValue;
    } wdt;

    struct DIVU {
        uint32 DVSR;
        uint32 DVDNT;
        uint32 DVCR;
        uint16 VCRDIV;
        uint32 DVDNTH;
        uint32 DVDNTL;
        uint32 DVDNTUH;
        uint32 DVDNTUL;
    } divu;

    struct FRT {
        uint8 TIER;
        uint8 FTCSR;
        uint16 FRC;
        uint16 OCRA;
        uint16 OCRB;
        uint8 TCR;
        uint8 TOCR;
        uint16 ICR;
        uint8 TEMP;
        uint64 cycleCount;
        uint8 FTCSR_mask;
    } frt;

    struct INTC {
        uint16 ICR;

        alignas(16) std::array<uint8, 16> levels;
        alignas(16) std::array<uint8, 16> vectors;

        uint8 pendingSource;
        uint8 pendingLevel;

        bool NMI;
        uint8 extVec;
    } intc;

    struct Cache {
        struct Entry {
            alignas(16) std::array<uint32, 4> tags;
            alignas(16) std::array<std::array<uint8, 16>, 4> lines;
        };

        uint8 CCR;
        alignas(16) std::array<Entry, 64> entries;
        alignas(16) std::array<uint8, 64> lru;
    } cache;

    uint8 SBYCR;
    bool sleep;
};

} // namespace ymir::savestate
