#pragma once

/**
@file
@brief VDP2 register definitions.
*/

#include "vdp2_defs.hpp"

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/inline.hpp>

namespace ymir::vdp {

struct VDP2Regs {
    void Reset() {
        TVMD.u16 = 0x0;
        displayEnabledLatch = false;
        borderColorModeLatch = false;
        TVSTAT.u16 &= ~0xFFFE; // Preserve PAL flag
        EXTEN.u16 = 0x0;
        HCNT = 0x0;
        VCNT = 0x0;
        HCNTShift = 0;
        HCNTMask = 0x3FE;
        VCNTShift = 0;
        VCNTSkip = 0;
        VCNTLatch = 0x3FF;
        vramControl.Reset();
        VRSIZE.u16 = 0x0;
        cyclePatterns.Reset();
        ZMCTL.u16 = 0x0;

        bgEnabled.fill(false);
        for (auto &bg : bgParams) {
            bg.Reset();
        }
        spriteParams.Reset();
        lineScreenParams.Reset();
        backScreenParams.Reset();

        for (auto &param : rotParams) {
            param.Reset();
        }
        commonRotParams.Reset();

        for (auto &param : windowParams) {
            param.Reset();
        }

        vcellScrollTableAddress = 0;
        cellScrollTableAddress = 0;

        mosaicH = 1;
        mosaicV = 1;

        colorOffsetEnable.fill(false);
        colorOffsetSelect.fill(false);
        for (auto &coParams : colorOffset) {
            coParams.Reset();
        }

        colorCalcParams.Reset();

        for (auto &sp : specialFunctionCodes) {
            sp.Reset();
        }

        transparentShadowEnable = false;

        TVMDDirty = true;
        accessPatternsDirty = true;
        vcellScrollDirty = true;

        UpdateRestrictedColorCalc();
    }

    template <bool peek>
    uint16 Read(uint32 address) const {
        switch (address) {
        case 0x000: return ReadTVMD();
        case 0x002: return ReadEXTEN<peek>();
        case 0x004: return ReadTVSTAT<peek>();
        case 0x006: return ReadVRSIZE();
        case 0x008: return ReadHCNT();
        case 0x00A: return ReadVCNT();
        case 0x00C: return 0; // unknown/hidden register
        case 0x00E: return ReadRAMCTL();
        case 0x010: return ReadCYCA0L(); // write-only?
        case 0x012: return ReadCYCA0U(); // write-only?
        case 0x014: return ReadCYCA1L(); // write-only?
        case 0x016: return ReadCYCA1U(); // write-only?
        case 0x018: return ReadCYCB0L(); // write-only?
        case 0x01A: return ReadCYCB0U(); // write-only?
        case 0x01C: return ReadCYCB1L(); // write-only?
        case 0x01E: return ReadCYCB1U(); // write-only?
        case 0x020: return ReadBGON();   // write-only?
        case 0x022: return ReadMZCTL();  // write-only?
        case 0x024: return ReadSFSEL();  // write-only?
        case 0x026: return ReadSFCODE(); // write-only?
        case 0x028: return ReadCHCTLA(); // write-only?
        case 0x02A: return ReadCHCTLB(); // write-only?
        case 0x02C: return ReadBMPNA();  // write-only?
        case 0x02E: return ReadBMPNB();  // write-only?
        case 0x030: return ReadPNCNA();  // write-only?
        case 0x032: return ReadPNCNB();  // write-only?
        case 0x034: return ReadPNCNC();  // write-only?
        case 0x036: return ReadPNCND();  // write-only?
        case 0x038: return ReadPNCR();   // write-only?
        case 0x03A: return ReadPLSZ();   // write-only?
        case 0x03C: return ReadMPOFN();  // write-only?
        case 0x03E: return ReadMPOFR();  // write-only?
        case 0x040: return ReadMPABN0(); // write-only?
        case 0x042: return ReadMPCDN0(); // write-only?
        case 0x044: return ReadMPABN1(); // write-only?
        case 0x046: return ReadMPCDN1(); // write-only?
        case 0x048: return ReadMPABN2(); // write-only?
        case 0x04A: return ReadMPCDN2(); // write-only?
        case 0x04C: return ReadMPABN3(); // write-only?
        case 0x04E: return ReadMPCDN3(); // write-only?
        case 0x050: return ReadMPABRA(); // write-only?
        case 0x052: return ReadMPCDRA(); // write-only?
        case 0x054: return ReadMPEFRA(); // write-only?
        case 0x056: return ReadMPGHRA(); // write-only?
        case 0x058: return ReadMPIJRA(); // write-only?
        case 0x05A: return ReadMPKLRA(); // write-only?
        case 0x05C: return ReadMPMNRA(); // write-only?
        case 0x05E: return ReadMPOPRA(); // write-only?
        case 0x060: return ReadMPABRB(); // write-only?
        case 0x062: return ReadMPCDRB(); // write-only?
        case 0x064: return ReadMPEFRB(); // write-only?
        case 0x066: return ReadMPGHRB(); // write-only?
        case 0x068: return ReadMPIJRB(); // write-only?
        case 0x06A: return ReadMPKLRB(); // write-only?
        case 0x06C: return ReadMPMNRB(); // write-only?
        case 0x06E: return ReadMPOPRB(); // write-only?
        case 0x070: return ReadSCXIN0(); // write-only?
        case 0x072: return ReadSCXDN0(); // write-only?
        case 0x074: return ReadSCYIN0(); // write-only?
        case 0x076: return ReadSCYDN0(); // write-only?
        case 0x078: return ReadZMXIN0(); // write-only?
        case 0x07A: return ReadZMXDN0(); // write-only?
        case 0x07C: return ReadZMYIN0(); // write-only?
        case 0x07E: return ReadZMYDN0(); // write-only?
        case 0x080: return ReadSCXIN1(); // write-only?
        case 0x082: return ReadSCXDN1(); // write-only?
        case 0x084: return ReadSCYIN1(); // write-only?
        case 0x086: return ReadSCYDN1(); // write-only?
        case 0x088: return ReadZMXIN1(); // write-only?
        case 0x08A: return ReadZMXDN1(); // write-only?
        case 0x08C: return ReadZMYIN1(); // write-only?
        case 0x08E: return ReadZMYDN1(); // write-only?
        case 0x090: return ReadSCXN2();  // write-only?
        case 0x092: return ReadSCYN2();  // write-only?
        case 0x094: return ReadSCXN3();  // write-only?
        case 0x096: return ReadSCYN3();  // write-only?
        case 0x098: return ReadZMCTL();  // write-only?
        case 0x09A: return ReadSCRCTL(); // write-only?
        case 0x09C: return ReadVCSTAU(); // write-only?
        case 0x09E: return ReadVCSTAL(); // write-only?
        case 0x0A0: return ReadLSTA0U(); // write-only?
        case 0x0A2: return ReadLSTA0L(); // write-only?
        case 0x0A4: return ReadLSTA1U(); // write-only?
        case 0x0A6: return ReadLSTA1L(); // write-only?
        case 0x0A8: return ReadLCTAU();  // write-only?
        case 0x0AA: return ReadLCTAL();  // write-only?
        case 0x0AC: return ReadBKTAU();  // write-only?
        case 0x0AE: return ReadBKTAL();  // write-only?
        case 0x0B0: return ReadRPMD();   // write-only?
        case 0x0B2: return ReadRPRCTL(); // write-only?
        case 0x0B4: return ReadKTCTL();  // write-only?
        case 0x0B6: return ReadKTAOF();  // write-only?
        case 0x0B8: return ReadOVPNRA(); // write-only?
        case 0x0BA: return ReadOVPNRB(); // write-only?
        case 0x0BC: return ReadRPTAU();  // write-only?
        case 0x0BE: return ReadRPTAL();  // write-only?
        case 0x0C0: return ReadWPSX0();  // write-only?
        case 0x0C2: return ReadWPSY0();  // write-only?
        case 0x0C4: return ReadWPEX0();  // write-only?
        case 0x0C6: return ReadWPEY0();  // write-only?
        case 0x0C8: return ReadWPSX1();  // write-only?
        case 0x0CA: return ReadWPSY1();  // write-only?
        case 0x0CC: return ReadWPEX1();  // write-only?
        case 0x0CE: return ReadWPEY1();  // write-only?
        case 0x0D0: return ReadWCTLA();  // write-only?
        case 0x0D2: return ReadWCTLB();  // write-only?
        case 0x0D4: return ReadWCTLC();  // write-only?
        case 0x0D6: return ReadWCTLD();  // write-only?
        case 0x0D8: return ReadLWTA0U(); // write-only?
        case 0x0DA: return ReadLWTA0L(); // write-only?
        case 0x0DC: return ReadLWTA1U(); // write-only?
        case 0x0DE: return ReadLWTA1L(); // write-only?
        case 0x0E0: return ReadSPCTL();  // write-only?
        case 0x0E2: return ReadSDCTL();  // write-only?
        case 0x0E4: return ReadCRAOFA(); // write-only?
        case 0x0E6: return ReadCRAOFB(); // write-only?
        case 0x0E8: return ReadLNCLEN(); // write-only?
        case 0x0EA: return ReadSFPRMD(); // write-only?
        case 0x0EC: return ReadCCCTL();  // write-only?
        case 0x0EE: return ReadSFCCMD(); // write-only?
        case 0x0F0: return ReadPRISA();  // write-only?
        case 0x0F2: return ReadPRISB();  // write-only?
        case 0x0F4: return ReadPRISC();  // write-only?
        case 0x0F6: return ReadPRISD();  // write-only?
        case 0x0F8: return ReadPRINA();  // write-only?
        case 0x0FA: return ReadPRINB();  // write-only?
        case 0x0FC: return ReadPRIR();   // write-only?
        case 0x0FE: return 0;            // supposedly reserved
        case 0x100: return ReadCCRSA();  // write-only?
        case 0x102: return ReadCCRSB();  // write-only?
        case 0x104: return ReadCCRSC();  // write-only?
        case 0x106: return ReadCCRSD();  // write-only?
        case 0x108: return ReadCCRNA();  // write-only?
        case 0x10A: return ReadCCRNB();  // write-only?
        case 0x10C: return ReadCCRR();   // write-only?
        case 0x10E: return ReadCCRLB();  // write-only?
        case 0x110: return ReadCLOFEN(); // write-only?
        case 0x112: return ReadCLOFSL(); // write-only?
        case 0x114: return ReadCOAR();   // write-only?
        case 0x116: return ReadCOAG();   // write-only?
        case 0x118: return ReadCOAB();   // write-only?
        case 0x11A: return ReadCOBR();   // write-only?
        case 0x11C: return ReadCOBG();   // write-only?
        case 0x11E: return ReadCOBB();   // write-only?
        default: return 0;
        }
    }

    void Write(uint32 address, uint16 value) {
        address &= 0x1FF;

        switch (address) {
        case 0x000: WriteTVMD(value); break;
        case 0x002: WriteEXTEN(value); break;
        case 0x004: /* TVSTAT is read-only */ break;
        case 0x006: WriteVRSIZE(value); break;
        case 0x008: /* HCNT is read-only */ break;
        case 0x00A: /* VCNT is read-only */ break;
        case 0x00C: /* unknown/hidden register */ break;
        case 0x00E: WriteRAMCTL(value); break;
        case 0x010: WriteCYCA0L(value); break;
        case 0x012: WriteCYCA0U(value); break;
        case 0x014: WriteCYCA1L(value); break;
        case 0x016: WriteCYCA1U(value); break;
        case 0x018: WriteCYCB0L(value); break;
        case 0x01A: WriteCYCB0U(value); break;
        case 0x01C: WriteCYCB1L(value); break;
        case 0x01E: WriteCYCB1U(value); break;
        case 0x020: WriteBGON(value); break;
        case 0x022: WriteMZCTL(value); break;
        case 0x024: WriteSFSEL(value); break;
        case 0x026: WriteSFCODE(value); break;
        case 0x028: WriteCHCTLA(value); break;
        case 0x02A: WriteCHCTLB(value); break;
        case 0x02C: WriteBMPNA(value); break;
        case 0x02E: WriteBMPNB(value); break;
        case 0x030: WritePNCNA(value); break;
        case 0x032: WritePNCNB(value); break;
        case 0x034: WritePNCNC(value); break;
        case 0x036: WritePNCND(value); break;
        case 0x038: WritePNCR(value); break;
        case 0x03A: WritePLSZ(value); break;
        case 0x03C: WriteMPOFN(value); break;
        case 0x03E: WriteMPOFR(value); break;
        case 0x040: WriteMPABN0(value); break;
        case 0x042: WriteMPCDN0(value); break;
        case 0x044: WriteMPABN1(value); break;
        case 0x046: WriteMPCDN1(value); break;
        case 0x048: WriteMPABN2(value); break;
        case 0x04A: WriteMPCDN2(value); break;
        case 0x04C: WriteMPABN3(value); break;
        case 0x04E: WriteMPCDN3(value); break;
        case 0x050: WriteMPABRA(value); break;
        case 0x052: WriteMPCDRA(value); break;
        case 0x054: WriteMPEFRA(value); break;
        case 0x056: WriteMPGHRA(value); break;
        case 0x058: WriteMPIJRA(value); break;
        case 0x05A: WriteMPKLRA(value); break;
        case 0x05C: WriteMPMNRA(value); break;
        case 0x05E: WriteMPOPRA(value); break;
        case 0x060: WriteMPABRB(value); break;
        case 0x062: WriteMPCDRB(value); break;
        case 0x064: WriteMPEFRB(value); break;
        case 0x066: WriteMPGHRB(value); break;
        case 0x068: WriteMPIJRB(value); break;
        case 0x06A: WriteMPKLRB(value); break;
        case 0x06C: WriteMPMNRB(value); break;
        case 0x06E: WriteMPOPRB(value); break;
        case 0x070: WriteSCXIN0(value); break;
        case 0x072: WriteSCXDN0(value); break;
        case 0x074: WriteSCYIN0(value); break;
        case 0x076: WriteSCYDN0(value); break;
        case 0x078: WriteZMXIN0(value); break;
        case 0x07A: WriteZMXDN0(value); break;
        case 0x07C: WriteZMYIN0(value); break;
        case 0x07E: WriteZMYDN0(value); break;
        case 0x080: WriteSCXIN1(value); break;
        case 0x082: WriteSCXDN1(value); break;
        case 0x084: WriteSCYIN1(value); break;
        case 0x086: WriteSCYDN1(value); break;
        case 0x088: WriteZMXIN1(value); break;
        case 0x08A: WriteZMXDN1(value); break;
        case 0x08C: WriteZMYIN1(value); break;
        case 0x08E: WriteZMYDN1(value); break;
        case 0x090: WriteSCXN2(value); break;
        case 0x092: WriteSCYN2(value); break;
        case 0x094: WriteSCXN3(value); break;
        case 0x096: WriteSCYN3(value); break;
        case 0x098: WriteZMCTL(value); break;
        case 0x09A: WriteSCRCTL(value); break;
        case 0x09C: WriteVCSTAU(value); break;
        case 0x09E: WriteVCSTAL(value); break;
        case 0x0A0: WriteLSTA0U(value); break;
        case 0x0A2: WriteLSTA0L(value); break;
        case 0x0A4: WriteLSTA1U(value); break;
        case 0x0A6: WriteLSTA1L(value); break;
        case 0x0A8: WriteLCTAU(value); break;
        case 0x0AA: WriteLCTAL(value); break;
        case 0x0AC: WriteBKTAU(value); break;
        case 0x0AE: WriteBKTAL(value); break;
        case 0x0B0: WriteRPMD(value); break;
        case 0x0B2: WriteRPRCTL(value); break;
        case 0x0B4: WriteKTCTL(value); break;
        case 0x0B6: WriteKTAOF(value); break;
        case 0x0B8: WriteOVPNRA(value); break;
        case 0x0BA: WriteOVPNRB(value); break;
        case 0x0BC: WriteRPTAU(value); break;
        case 0x0BE: WriteRPTAL(value); break;
        case 0x0C0: WriteWPSX0(value); break;
        case 0x0C2: WriteWPSY0(value); break;
        case 0x0C4: WriteWPEX0(value); break;
        case 0x0C6: WriteWPEY0(value); break;
        case 0x0C8: WriteWPSX1(value); break;
        case 0x0CA: WriteWPSY1(value); break;
        case 0x0CC: WriteWPEX1(value); break;
        case 0x0CE: WriteWPEY1(value); break;
        case 0x0D0: WriteWCTLA(value); break;
        case 0x0D2: WriteWCTLB(value); break;
        case 0x0D4: WriteWCTLC(value); break;
        case 0x0D6: WriteWCTLD(value); break;
        case 0x0D8: WriteLWTA0U(value); break;
        case 0x0DA: WriteLWTA0L(value); break;
        case 0x0DC: WriteLWTA1U(value); break;
        case 0x0DE: WriteLWTA1L(value); break;
        case 0x0E0: WriteSPCTL(value); break;
        case 0x0E2: WriteSDCTL(value); break;
        case 0x0E4: WriteCRAOFA(value); break;
        case 0x0E6: WriteCRAOFB(value); break;
        case 0x0E8: WriteLNCLEN(value); break;
        case 0x0EA: WriteSFPRMD(value); break;
        case 0x0EC: WriteCCCTL(value); break;
        case 0x0EE: WriteSFCCMD(value); break;
        case 0x0F0: WritePRISA(value); break;
        case 0x0F2: WritePRISB(value); break;
        case 0x0F4: WritePRISC(value); break;
        case 0x0F6: WritePRISD(value); break;
        case 0x0F8: WritePRINA(value); break;
        case 0x0FA: WritePRINB(value); break;
        case 0x0FC: WritePRIR(value); break;
        case 0x0FE: break; // supposedly reserved
        case 0x100: WriteCCRSA(value); break;
        case 0x102: WriteCCRSB(value); break;
        case 0x104: WriteCCRSC(value); break;
        case 0x106: WriteCCRSD(value); break;
        case 0x108: WriteCCRNA(value); break;
        case 0x10A: WriteCCRNB(value); break;
        case 0x10C: WriteCCRR(value); break;
        case 0x10E: WriteCCRLB(value); break;
        case 0x110: WriteCLOFEN(value); break;
        case 0x112: WriteCLOFSL(value); break;
        case 0x114: WriteCOAR(value); break;
        case 0x116: WriteCOAG(value); break;
        case 0x118: WriteCOAB(value); break;
        case 0x11A: WriteCOBR(value); break;
        case 0x11C: WriteCOBG(value); break;
        case 0x11E: WriteCOBB(value); break;
        }
    }

    // 180000   TVMD    TV Screen Mode
    RegTVMD TVMD;
    bool displayEnabledLatch;  // Latched TVMD.DISP
    bool borderColorModeLatch; // Latched TVMD.BDCLMD

    FORCE_INLINE void LatchTVMD() {
        displayEnabledLatch = TVMD.DISP;
        borderColorModeLatch = TVMD.BDCLMD;
    }

    FORCE_INLINE uint16 ReadTVMD() const {
        return TVMD.u16;
    }

    FORCE_INLINE void WriteTVMD(uint16 value) {
        const RegTVMD oldTVMD = TVMD;
        TVMD.u16 = value & 0x81F7;
        TVMDDirty |= ((TVMD.u16 ^ oldTVMD.u16) & 0x1F7) != 0;
        accessPatternsDirty |= TVMD.HRESOn != oldTVMD.HRESOn;
        displayEnabledLatch &= TVMD.DISP;
        UpdateRestrictedColorCalc();
    }

    // 180002   EXTEN   External Signal Enable
    RegEXTEN EXTEN;

    template <bool peek>
    FORCE_INLINE uint16 ReadEXTEN() const {
        if constexpr (!peek) {
            if (!EXTEN.EXLTEN) {
                static constexpr uint32 hRes[] = {320, 352, 640, 704};
                HCNT = hRes[TVMD.HRESOn & 3] - 50; // TODO: derive coordinate from global cycle counter
                VCNTLatch = (VCNT << VCNTShift) + VCNTSkip;
                if (TVMD.LSMDn == InterlaceMode::DoubleDensity) {
                    VCNTLatch |= TVSTAT.ODD ^ 1;
                }
            }
        }
        return EXTEN.u16;
    }

    FORCE_INLINE void WriteEXTEN(uint16 value) {
        EXTEN.u16 = value & 0x0303;
    }

    // 180004   TVSTAT  Screen Status (read-only)
    RegTVSTAT TVSTAT;

    template <bool peek>
    FORCE_INLINE uint16 ReadTVSTAT() const {
        RegTVSTAT value = TVSTAT;
        if (TVMD.IsInterlaced()) {
            value.ODD ^= 1; // for some reason ODD is read inverted
        }
        if constexpr (!peek) {
            TVSTAT.EXLTFG = 0;
        }
        return value.u16;
    }

    FORCE_INLINE void WriteTVSTAT(uint16 value) {
        TVSTAT.u16 = value & 0x030F;
    }

    // 180006   VRSIZE  VRAM Size
    RegVRSIZE VRSIZE;

    FORCE_INLINE uint16 ReadVRSIZE() const {
        return VRSIZE.u16;
    }

    FORCE_INLINE void WriteVRSIZE(uint16 value) {
        VRSIZE.u16 = value & 0x8000;
    }

    // 180008   HCNT    H Counter
    //
    //   bits   r/w  code          description
    //  15-10        -             Reserved, must be zero
    //    9-0   R    HCT9-0        H Counter Value
    //
    // Notes
    // - Counter layout depends on screen mode:
    //     Normal: bits 8-0 shifted left by 1; HCT0 is invalid
    //     Hi-Res: bits 9-0
    //     Excl. Normal: bits 8-0 (no shift); HCT9 is invalid
    //     Excl. Hi-Res: bits 9-1 shifted right by 1; HCT9 is invalid
    mutable uint16 HCNT; // Horizontal counter latched by external signal
    uint16 HCNTShift;    // Right-shift applied to HCNT<<1, derived from screen mode
    uint16 HCNTMask;     // Mask applied to final HCNT, derived from screen mode

    FORCE_INLINE uint16 ReadHCNT() const {
        return HCNT;
    }

    FORCE_INLINE void WriteHCNT(uint16 value) {
        HCNT = (value >> HCNTShift) & HCNTMask;
    }

    // 18000A   VCNT    V Counter
    //
    //   bits   r/w  code          description
    //  15-10        -             Reserved, must be zero
    //    9-0   R    VCT9-0        V Counter Value
    //
    // Notes
    // - Counter layout depends on screen mode:
    //     Exclusive Monitor: bits 9-0
    //     Normal Hi-Res double-density interlace:
    //       bits 8-0 shifted left by 1
    //       bit 0 contains interlaced field (0=odd, 1=even)
    //     All other modes: bits 8-0 shifted left by 1; VCT0 is invalid
    uint16 VCNT;              // Current vertical counter
    uint16 VCNTShift;         // Left-shift applied to VCNT, derived from screen mode
    uint16 VCNTSkip;          // Value added to VCNT, derived from current display phase
    mutable uint16 VCNTLatch; // Vertical counter latched by external signal

    FORCE_INLINE uint16 ReadVCNT() const {
        return VCNTLatch;
    }

    FORCE_INLINE void WriteVCNT(uint16 value) {
        VCNTLatch = value & 0x1FF;
    }

    // 18000C   -       Reserved (but not really)

    // 18000E   RAMCTL  RAM Control
    //
    //   bits   r/w  code          description
    //     15   R/W  CRKTE         Color RAM Coefficient Table Enable
    //                               If enabled, Color RAM Mode should be set to 01
    //     14        -             Reserved, must be zero
    //  13-12   R/W  CRMD1-0       Color RAM Mode
    //                               00 (0) = RGB 5:5:5, 1024 words
    //                               01 (1) = RGB 5:5:5, 2048 words
    //                               10 (2) = RGB 8:8:8, 1024 words
    //                               11 (3) = RGB 8:8:8, 1024 words  (same as mode 2, undocumented)
    //  11-10        -             Reserved, must be zero
    //      9   R/W  VRBMD         VRAM-B Mode (0=single partition, 1=two partitions)
    //      8   R/W  VRAMD         VRAM-A Mode (0=single partition, 1=two partitions)
    //    7-6   R/W  RDBSB1(1-0)   Rotation Data Bank Select for VRAM-B1
    //    5-4   R/W  RDBSB0(1-0)   Rotation Data Bank Select for VRAM-B0 (or VRAM-B)
    //    3-2   R/W  RDBSA1(1-0)   Rotation Data Bank Select for VRAM-A1
    //    1-0   R/W  RDBSA0(1-0)   Rotation Data Bank Select for VRAM-A0 (or VRAM-A)
    //
    // RDBSxn(1-0):
    //   00 (0) = bank not used by rotation backgrounds
    //   01 (1) = bank used for coefficient table
    //   10 (2) = bank used for pattern name table
    //   11 (3) = bank used for character/bitmap pattern table
    VRAMControl vramControl;

    FORCE_INLINE uint16 ReadRAMCTL() const {
        uint16 value = 0;
        bit::deposit_into<0, 1>(value, static_cast<uint8>(vramControl.rotDataBankSelA0));
        bit::deposit_into<2, 3>(value, static_cast<uint8>(vramControl.rotDataBankSelA1));
        bit::deposit_into<4, 5>(value, static_cast<uint8>(vramControl.rotDataBankSelB0));
        bit::deposit_into<6, 7>(value, static_cast<uint8>(vramControl.rotDataBankSelB1));
        bit::deposit_into<8>(value, vramControl.partitionVRAMA);
        bit::deposit_into<9>(value, vramControl.partitionVRAMB);
        bit::deposit_into<12, 13>(value, vramControl.colorRAMMode);
        bit::deposit_into<15>(value, vramControl.colorRAMCoeffTableEnable);
        return value;
    }

    FORCE_INLINE void WriteRAMCTL(uint16 value) {
        accessPatternsDirty |= ReadRAMCTL() != value;

        vramControl.rotDataBankSelA0 = static_cast<RotDataBankSel>(bit::extract<0, 1>(value));
        vramControl.rotDataBankSelA1 = static_cast<RotDataBankSel>(bit::extract<2, 3>(value));
        vramControl.rotDataBankSelB0 = static_cast<RotDataBankSel>(bit::extract<4, 5>(value));
        vramControl.rotDataBankSelB1 = static_cast<RotDataBankSel>(bit::extract<6, 7>(value));
        vramControl.partitionVRAMA = bit::test<8>(value);
        vramControl.partitionVRAMB = bit::test<9>(value);
        vramControl.colorRAMMode = bit::extract<12, 13>(value);
        vramControl.colorRAMCoeffTableEnable = bit::test<15>(value);
        vramControl.UpdateDerivedValues();

        UpdateRestrictedColorCalc();
    }

    // 180010   CYCA0L  VRAM Cycle Pattern A0 Lower
    //
    //   bits   r/w  code          description
    //  15-12     W  VCP0A0(3-0)   VRAM-A0 (or VRAM-A) Timing for T0
    //   11-8     W  VCP1A0(3-0)   VRAM-A0 (or VRAM-A) Timing for T1
    //    7-4     W  VCP2A0(3-0)   VRAM-A0 (or VRAM-A) Timing for T2
    //    3-0     W  VCP3A0(3-0)   VRAM-A0 (or VRAM-A) Timing for T3
    //
    // 180012   CYCA0U  VRAM Cycle Pattern A0 Upper
    //
    //   bits   r/w  code          description
    //  15-12     W  VCP4A0(3-0)   VRAM-A0 (or VRAM-A) Timing for T4
    //   11-8     W  VCP5A0(3-0)   VRAM-A0 (or VRAM-A) Timing for T5
    //    7-4     W  VCP6A0(3-0)   VRAM-A0 (or VRAM-A) Timing for T6
    //    3-0     W  VCP7A0(3-0)   VRAM-A0 (or VRAM-A) Timing for T7
    //
    // 180014   CYCA1L  VRAM Cycle Pattern A1 Lower
    //
    //   bits   r/w  code          description
    //  15-12     W  VCP0A1(3-0)   VRAM-A1 Timing for T0
    //   11-8     W  VCP1A1(3-0)   VRAM-A1 Timing for T1
    //    7-4     W  VCP2A1(3-0)   VRAM-A1 Timing for T2
    //    3-0     W  VCP3A1(3-0)   VRAM-A1 Timing for T3
    //
    // 180016   CYCA1U  VRAM Cycle Pattern A1 Upper
    //
    //   bits   r/w  code          description
    //  15-12     W  VCP4A1(3-0)   VRAM-A1 Timing for T4
    //   11-8     W  VCP5A1(3-0)   VRAM-A1 Timing for T5
    //    7-4     W  VCP6A1(3-0)   VRAM-A1 Timing for T6
    //    3-0     W  VCP7A1(3-0)   VRAM-A1 Timing for T7
    //
    // 180018   CYCB0L  VRAM Cycle Pattern B0 Lower
    //
    //   bits   r/w  code          description
    //  15-12     W  VCP0B0(3-0)   VRAM-B0 (or VRAM-B) Timing for T0
    //   11-8     W  VCP1B0(3-0)   VRAM-B0 (or VRAM-B) Timing for T1
    //    7-4     W  VCP2B0(3-0)   VRAM-B0 (or VRAM-B) Timing for T2
    //    3-0     W  VCP3B0(3-0)   VRAM-B0 (or VRAM-B) Timing for T3
    //
    // 18001A   CYCB0U  VRAM Cycle Pattern B0 Upper
    //
    //   bits   r/w  code          description
    //  15-12     W  VCP4B0(3-0)   VRAM-B0 (or VRAM-B) Timing for T4
    //   11-8     W  VCP5B0(3-0)   VRAM-B0 (or VRAM-B) Timing for T5
    //    7-4     W  VCP6B0(3-0)   VRAM-B0 (or VRAM-B) Timing for T6
    //    3-0     W  VCP7B0(3-0)   VRAM-B0 (or VRAM-B) Timing for T7
    //
    // 18001C   CYCB1L  VRAM Cycle Pattern B1 Lower
    //
    //   bits   r/w  code          description
    //  15-12     W  VCP0B1(3-0)   VRAM-B1 Timing for T0
    //   11-8     W  VCP1B1(3-0)   VRAM-B1 Timing for T1
    //    7-4     W  VCP2B1(3-0)   VRAM-B1 Timing for T2
    //    3-0     W  VCP3B1(3-0)   VRAM-B1 Timing for T3
    //
    // 18001E   CYCB1U  VRAM Cycle Pattern B1 Upper
    //
    //   bits   r/w  code          description
    //  15-12     W  VCP4B1(3-0)   VRAM-B1 Timing for T4
    //   11-8     W  VCP5B1(3-0)   VRAM-B1 Timing for T5
    //    7-4     W  VCP6B1(3-0)   VRAM-B1 Timing for T6
    //    3-0     W  VCP7B1(3-0)   VRAM-B1 Timing for T7
    //
    // Timing values:
    //   0000 (0): NBG0 pattern name
    //   0001 (1): NBG1 pattern name
    //   0010 (2): NBG2 pattern name
    //   0011 (3): NBG3 pattern name
    //   0100 (4): NBG0 character pattern
    //   0101 (5): NBG1 character pattern
    //   0110 (6): NBG2 character pattern
    //   0111 (7): NBG3 character pattern
    //   1000 (8): (prohibited)
    //   1001 (9): (prohibited)
    //   1010 (A): (prohibited)
    //   1011 (B): (prohibited)
    //   1100 (C): NBG0 vertical cell scroll table
    //   1101 (D): NBG1 vertical cell scroll table
    //   1110 (E): CPU read/write
    //   1111 (F): No access
    CyclePatterns cyclePatterns;

    FORCE_INLINE uint16 ReadCYCA0L() const {
        return (cyclePatterns.timings[0][0] << 12u) | (cyclePatterns.timings[0][1] << 8u) |
               (cyclePatterns.timings[0][2] << 4u) | cyclePatterns.timings[0][3];
    }

    FORCE_INLINE uint16 ReadCYCA0U() const {
        return (cyclePatterns.timings[0][4] << 12u) | (cyclePatterns.timings[0][5] << 8u) |
               (cyclePatterns.timings[0][6] << 4u) | cyclePatterns.timings[0][7];
    }

    FORCE_INLINE void WriteCYCA0L(uint16 value) {
        accessPatternsDirty |= ReadCYCA0L() != value;

        cyclePatterns.timings[0][0] = static_cast<CyclePatterns::Type>(bit::extract<12, 15>(value));
        cyclePatterns.timings[0][1] = static_cast<CyclePatterns::Type>(bit::extract<8, 11>(value));
        cyclePatterns.timings[0][2] = static_cast<CyclePatterns::Type>(bit::extract<4, 7>(value));
        cyclePatterns.timings[0][3] = static_cast<CyclePatterns::Type>(bit::extract<0, 3>(value));
    }

    FORCE_INLINE void WriteCYCA0U(uint16 value) {
        accessPatternsDirty |= ReadCYCA0U() != value;

        cyclePatterns.timings[0][4] = static_cast<CyclePatterns::Type>(bit::extract<12, 15>(value));
        cyclePatterns.timings[0][5] = static_cast<CyclePatterns::Type>(bit::extract<8, 11>(value));
        cyclePatterns.timings[0][6] = static_cast<CyclePatterns::Type>(bit::extract<4, 7>(value));
        cyclePatterns.timings[0][7] = static_cast<CyclePatterns::Type>(bit::extract<0, 3>(value));
    }

    FORCE_INLINE uint16 ReadCYCA1L() const {
        return (cyclePatterns.timings[1][0] << 12u) | (cyclePatterns.timings[1][1] << 8u) |
               (cyclePatterns.timings[1][2] << 4u) | cyclePatterns.timings[1][3];
    }

    FORCE_INLINE uint16 ReadCYCA1U() const {
        return (cyclePatterns.timings[1][4] << 12u) | (cyclePatterns.timings[1][5] << 8u) |
               (cyclePatterns.timings[1][6] << 4u) | cyclePatterns.timings[1][7];
    }

    FORCE_INLINE void WriteCYCA1L(uint16 value) {
        accessPatternsDirty |= ReadCYCA1L() != value;

        cyclePatterns.timings[1][0] = static_cast<CyclePatterns::Type>(bit::extract<12, 15>(value));
        cyclePatterns.timings[1][1] = static_cast<CyclePatterns::Type>(bit::extract<8, 11>(value));
        cyclePatterns.timings[1][2] = static_cast<CyclePatterns::Type>(bit::extract<4, 7>(value));
        cyclePatterns.timings[1][3] = static_cast<CyclePatterns::Type>(bit::extract<0, 3>(value));
    }

    FORCE_INLINE void WriteCYCA1U(uint16 value) {
        accessPatternsDirty |= ReadCYCA1U() != value;

        cyclePatterns.timings[1][4] = static_cast<CyclePatterns::Type>(bit::extract<12, 15>(value));
        cyclePatterns.timings[1][5] = static_cast<CyclePatterns::Type>(bit::extract<8, 11>(value));
        cyclePatterns.timings[1][6] = static_cast<CyclePatterns::Type>(bit::extract<4, 7>(value));
        cyclePatterns.timings[1][7] = static_cast<CyclePatterns::Type>(bit::extract<0, 3>(value));
    }

    FORCE_INLINE uint16 ReadCYCB0L() const {
        return (cyclePatterns.timings[2][0] << 12u) | (cyclePatterns.timings[2][1] << 8u) |
               (cyclePatterns.timings[2][2] << 4u) | cyclePatterns.timings[2][3];
    }

    FORCE_INLINE uint16 ReadCYCB0U() const {
        return (cyclePatterns.timings[2][4] << 12u) | (cyclePatterns.timings[2][5] << 8u) |
               (cyclePatterns.timings[2][6] << 4u) | cyclePatterns.timings[2][7];
    }

    FORCE_INLINE void WriteCYCB0L(uint16 value) {
        accessPatternsDirty |= ReadCYCB0L() != value;

        cyclePatterns.timings[2][0] = static_cast<CyclePatterns::Type>(bit::extract<12, 15>(value));
        cyclePatterns.timings[2][1] = static_cast<CyclePatterns::Type>(bit::extract<8, 11>(value));
        cyclePatterns.timings[2][2] = static_cast<CyclePatterns::Type>(bit::extract<4, 7>(value));
        cyclePatterns.timings[2][3] = static_cast<CyclePatterns::Type>(bit::extract<0, 3>(value));
    }

    FORCE_INLINE void WriteCYCB0U(uint16 value) {
        accessPatternsDirty |= ReadCYCB0U() != value;

        cyclePatterns.timings[2][4] = static_cast<CyclePatterns::Type>(bit::extract<12, 15>(value));
        cyclePatterns.timings[2][5] = static_cast<CyclePatterns::Type>(bit::extract<8, 11>(value));
        cyclePatterns.timings[2][6] = static_cast<CyclePatterns::Type>(bit::extract<4, 7>(value));
        cyclePatterns.timings[2][7] = static_cast<CyclePatterns::Type>(bit::extract<0, 3>(value));
    }

    FORCE_INLINE uint16 ReadCYCB1L() const {
        return (cyclePatterns.timings[3][0] << 12u) | (cyclePatterns.timings[3][1] << 8u) |
               (cyclePatterns.timings[3][2] << 4u) | cyclePatterns.timings[3][3];
    }

    FORCE_INLINE uint16 ReadCYCB1U() const {
        return (cyclePatterns.timings[3][4] << 12u) | (cyclePatterns.timings[3][5] << 8u) |
               (cyclePatterns.timings[3][6] << 4u) | cyclePatterns.timings[3][7];
    }

    FORCE_INLINE void WriteCYCB1L(uint16 value) {
        accessPatternsDirty |= ReadCYCB1L() != value;

        cyclePatterns.timings[3][0] = static_cast<CyclePatterns::Type>(bit::extract<12, 15>(value));
        cyclePatterns.timings[3][1] = static_cast<CyclePatterns::Type>(bit::extract<8, 11>(value));
        cyclePatterns.timings[3][2] = static_cast<CyclePatterns::Type>(bit::extract<4, 7>(value));
        cyclePatterns.timings[3][3] = static_cast<CyclePatterns::Type>(bit::extract<0, 3>(value));
    }

    FORCE_INLINE void WriteCYCB1U(uint16 value) {
        accessPatternsDirty |= ReadCYCB1U() != value;

        cyclePatterns.timings[3][4] = static_cast<CyclePatterns::Type>(bit::extract<12, 15>(value));
        cyclePatterns.timings[3][5] = static_cast<CyclePatterns::Type>(bit::extract<8, 11>(value));
        cyclePatterns.timings[3][6] = static_cast<CyclePatterns::Type>(bit::extract<4, 7>(value));
        cyclePatterns.timings[3][7] = static_cast<CyclePatterns::Type>(bit::extract<0, 3>(value));
    }

    // 180020   BGON    Screen Display Enable
    //
    //   bits   r/w  code          description
    //  15-13        -             Reserved, must be zero
    //     12     W  R0TPON        RBG0 Transparent Display (0=enable, 1=disable)
    //     11     W  N3TPON        NBG3 Transparent Display (0=enable, 1=disable)
    //     10     W  N2TPON        NBG2 Transparent Display (0=enable, 1=disable)
    //      9     W  N1TPON        NBG1/EXBG Transparent Display (0=enable, 1=disable)
    //      8     W  N0TPON        NBG0/RBG1 Transparent Display (0=enable, 1=disable)
    //    7-6        -             Reserved, must be zero
    //      5     W  R1ON          RBG1 Display (0=disable, 1=enable)
    //      4     W  R0ON          RBG0 Display (0=disable, 1=enable)
    //      3     W  N3ON          NBG3 Display (0=disable, 1=enable)
    //      2     W  N2ON          NBG2 Display (0=disable, 1=enable)
    //      1     W  N1ON          NBG1 Display (0=disable, 1=enable)
    //      0     W  N0ON          NBG0 Display (0=disable, 1=enable)

    FORCE_INLINE uint16 ReadBGON() const {
        uint16 value = 0;
        bit::deposit_into<0>(value, bgEnabled[0]);
        bit::deposit_into<1>(value, bgEnabled[1]);
        bit::deposit_into<2>(value, bgEnabled[2]);
        bit::deposit_into<3>(value, bgEnabled[3]);
        bit::deposit_into<4>(value, bgEnabled[4]);
        bit::deposit_into<5>(value, bgEnabled[5]);

        bit::deposit_into<8>(value, !bgParams[1].enableTransparency);
        bit::deposit_into<9>(value, !bgParams[2].enableTransparency);
        bit::deposit_into<10>(value, !bgParams[3].enableTransparency);
        bit::deposit_into<11>(value, !bgParams[4].enableTransparency);
        bit::deposit_into<12>(value, !bgParams[0].enableTransparency);
        return value;
    }

    FORCE_INLINE void WriteBGON(uint16 value) {
        accessPatternsDirty |= ReadBGON() != value;

        bgEnabled[0] = bit::test<0>(value);
        bgEnabled[1] = bit::test<1>(value);
        bgEnabled[2] = bit::test<2>(value);
        bgEnabled[3] = bit::test<3>(value);
        bgEnabled[4] = bit::test<4>(value);
        bgEnabled[5] = bit::test<5>(value);

        bgParams[1].enableTransparency = !bit::test<8>(value);
        bgParams[2].enableTransparency = !bit::test<9>(value);
        bgParams[3].enableTransparency = !bit::test<10>(value);
        bgParams[4].enableTransparency = !bit::test<11>(value);
        bgParams[0].enableTransparency = !bit::test<12>(value);
    }

    // 180022   MZCTL   Mosaic Control
    //
    //   bits   r/w  code          description
    //  15-12     W  MZSZV3-0      Vertical Mosaic Size
    //   11-8     W  MZSZH3-0      Horizontal Mosaic Size
    //    7-5        -             Reserved, must be zero
    //      4     W  R0MZE         RBG0 Mosaic Enable
    //      3     W  N3MZE         NBG3 Mosaic Enable
    //      2     W  N2MZE         NBG2 Mosaic Enable
    //      1     W  N1MZE         NBG1 Mosaic Enable
    //      0     W  N0MZE         NBG0/RBG1 Mosaic Enable

    FORCE_INLINE uint16 ReadMZCTL() const {
        uint16 value = 0;
        bit::deposit_into<0>(value, bgParams[1].mosaicEnable);
        bit::deposit_into<1>(value, bgParams[2].mosaicEnable);
        bit::deposit_into<2>(value, bgParams[3].mosaicEnable);
        bit::deposit_into<3>(value, bgParams[4].mosaicEnable);
        bit::deposit_into<4>(value, bgParams[0].mosaicEnable);
        bit::deposit_into<8, 11>(value, mosaicH - 1);
        bit::deposit_into<12, 15>(value, mosaicV - 1);
        return value;
    }

    FORCE_INLINE void WriteMZCTL(uint16 value) {
        bgParams[1].mosaicEnable = bit::test<0>(value);
        bgParams[2].mosaicEnable = bit::test<1>(value);
        bgParams[3].mosaicEnable = bit::test<2>(value);
        bgParams[4].mosaicEnable = bit::test<3>(value);
        bgParams[0].mosaicEnable = bit::test<4>(value);
        mosaicH = bit::extract<8, 11>(value) + 1;
        mosaicV = bit::extract<12, 15>(value) + 1;
    }

    // 180024   SFSEL   Special Function Code Select
    //
    //   bits   r/w  code          description
    //   15-5        -             Reserved, must be zero
    //      4     W  R0SFCS        RBG0 Special Function Code Select
    //      3     W  N3SFCS        NBG3 Special Function Code Select
    //      2     W  N2SFCS        NBG2 Special Function Code Select
    //      1     W  N1SFCS        NBG1 Special Function Code Select
    //      0     W  N0SFCS        NBG0/RBG1 Special Function Code Select

    FORCE_INLINE uint16 ReadSFSEL() const {
        uint16 value = 0;
        bit::deposit_into<0>(value, bgParams[1].specialFunctionSelect);
        bit::deposit_into<1>(value, bgParams[2].specialFunctionSelect);
        bit::deposit_into<2>(value, bgParams[3].specialFunctionSelect);
        bit::deposit_into<3>(value, bgParams[4].specialFunctionSelect);
        bit::deposit_into<4>(value, bgParams[0].specialFunctionSelect);
        return value;
    }

    FORCE_INLINE void WriteSFSEL(uint16 value) {
        bgParams[1].specialFunctionSelect = bit::extract<0>(value);
        bgParams[2].specialFunctionSelect = bit::extract<1>(value);
        bgParams[3].specialFunctionSelect = bit::extract<2>(value);
        bgParams[4].specialFunctionSelect = bit::extract<3>(value);
        bgParams[0].specialFunctionSelect = bit::extract<4>(value);
    }

    // 180026   SFCODE  Special Function Code
    //
    //   bits   r/w  code          description
    //   15-8        SFCDB7-0      Special Function Code B
    //    7-0        SFCDA7-0      Special Function Code A
    //
    // Each bit in SFCDxn matches the least significant 4 bits of the color code:
    //   n=0: 0x0 or 0x1
    //   n=1: 0x2 or 0x3
    //   n=2: 0x4 or 0x5
    //   n=3: 0x6 or 0x7
    //   n=4: 0x8 or 0x9
    //   n=5: 0xA or 0xB
    //   n=6: 0xC or 0xD
    //   n=7: 0xE or 0xF

    FORCE_INLINE uint16 ReadSFCODE() const {
        uint16 value = 0;
        bit::deposit_into<0>(value, specialFunctionCodes[0].colorMatches[0]);
        bit::deposit_into<1>(value, specialFunctionCodes[0].colorMatches[1]);
        bit::deposit_into<2>(value, specialFunctionCodes[0].colorMatches[2]);
        bit::deposit_into<3>(value, specialFunctionCodes[0].colorMatches[3]);
        bit::deposit_into<4>(value, specialFunctionCodes[0].colorMatches[4]);
        bit::deposit_into<5>(value, specialFunctionCodes[0].colorMatches[5]);
        bit::deposit_into<6>(value, specialFunctionCodes[0].colorMatches[6]);
        bit::deposit_into<7>(value, specialFunctionCodes[0].colorMatches[7]);

        bit::deposit_into<8>(value, specialFunctionCodes[1].colorMatches[0]);
        bit::deposit_into<9>(value, specialFunctionCodes[1].colorMatches[1]);
        bit::deposit_into<10>(value, specialFunctionCodes[1].colorMatches[2]);
        bit::deposit_into<11>(value, specialFunctionCodes[1].colorMatches[3]);
        bit::deposit_into<12>(value, specialFunctionCodes[1].colorMatches[4]);
        bit::deposit_into<13>(value, specialFunctionCodes[1].colorMatches[5]);
        bit::deposit_into<14>(value, specialFunctionCodes[1].colorMatches[6]);
        bit::deposit_into<15>(value, specialFunctionCodes[1].colorMatches[7]);
        return value;
    }

    FORCE_INLINE void WriteSFCODE(uint16 value) {
        specialFunctionCodes[0].colorMatches[0] = bit::test<0>(value);
        specialFunctionCodes[0].colorMatches[1] = bit::test<1>(value);
        specialFunctionCodes[0].colorMatches[2] = bit::test<2>(value);
        specialFunctionCodes[0].colorMatches[3] = bit::test<3>(value);
        specialFunctionCodes[0].colorMatches[4] = bit::test<4>(value);
        specialFunctionCodes[0].colorMatches[5] = bit::test<5>(value);
        specialFunctionCodes[0].colorMatches[6] = bit::test<6>(value);
        specialFunctionCodes[0].colorMatches[7] = bit::test<7>(value);

        specialFunctionCodes[1].colorMatches[0] = bit::test<8>(value);
        specialFunctionCodes[1].colorMatches[1] = bit::test<9>(value);
        specialFunctionCodes[1].colorMatches[2] = bit::test<10>(value);
        specialFunctionCodes[1].colorMatches[3] = bit::test<11>(value);
        specialFunctionCodes[1].colorMatches[4] = bit::test<12>(value);
        specialFunctionCodes[1].colorMatches[5] = bit::test<13>(value);
        specialFunctionCodes[1].colorMatches[6] = bit::test<14>(value);
        specialFunctionCodes[1].colorMatches[7] = bit::test<15>(value);
    }

    // 180028   CHCTLA  Character Control Register A
    //
    //   bits   r/w  code          description
    //  15-14        -             Reserved, must be zero
    //  13-12     W  N1CHCN1-0     NBG1/EXBG Character Color Number
    //                               00 (0) =       16 colors - palette
    //                               01 (1) =      256 colors - palette
    //                               10 (2) =     2048 colors - palette
    //                               11 (3) =    32768 colors - RGB (NBG1)
    //                                        16777216 colors - RGB (EXBG)
    //  11-10     W  N1BMSZ1-0     NBG1/EXBG Bitmap Size
    //                               00 (0) = 512x256
    //                               01 (1) = 512x512
    //                               10 (2) = 1024x256
    //                               11 (3) = 1024x512
    //      9     W  N1BMEN        NBG1/EXBG Bitmap Enable (0=cells, 1=bitmap)
    //      8     W  N1CHSZ        NBG1/EXBG Character Size (0=1x1, 1=2x2)
    //      7        -             Reserved, must be zero
    //    6-4     W  N0CHCN2-0     NBG0/RBG1 Character Color Number
    //                               000 (0) =       16 colors - palette
    //                               001 (1) =      256 colors - palette
    //                               010 (2) =     2048 colors - palette
    //                               011 (3) =    32768 colors - RGB
    //                               100 (4) = 16777216 colors - RGB (Normal mode only)
    //                                           forbidden for Hi-Res or Exclusive Monitor
    //                               101 (5) = forbidden
    //                               110 (6) = forbidden
    //                               111 (7) = forbidden
    //    3-2     W  N0BMSZ1-0     NBG0/RBG1 Bitmap Size
    //                               00 (0) = 512x256
    //                               01 (1) = 512x512
    //                               10 (2) = 1024x256
    //                               11 (3) = 1024x512
    //      1     W  N0BMEN        NBG0/RBG1 Bitmap Enable (0=cells, 1=bitmap)
    //      0     W  N0CHSZ        NBG0/RBG1 Character Size (0=1x1, 1=2x2)

    FORCE_INLINE uint16 ReadCHCTLA() const {
        uint16 value = 0;
        bit::deposit_into<0>(value, bgParams[1].cellSizeShift);
        bit::deposit_into<1>(value, bgParams[1].bitmap);
        bit::deposit_into<2, 3>(value, bgParams[1].bmsz);
        bit::deposit_into<4, 6>(value, static_cast<uint32>(bgParams[1].colorFormat));

        bit::deposit_into<8>(value, bgParams[2].cellSizeShift);
        bit::deposit_into<9>(value, bgParams[2].bitmap);
        bit::deposit_into<10, 11>(value, bgParams[2].bmsz);
        bit::deposit_into<12, 13>(value, static_cast<uint32>(bgParams[2].colorFormat));
        return value;
    }

    FORCE_INLINE void WriteCHCTLA(uint16 value) {
        accessPatternsDirty |= ReadCHCTLA() != value;

        bgParams[1].cellSizeShift = bit::extract<0>(value);
        bgParams[1].bitmap = bit::test<1>(value);
        bgParams[1].bmsz = bit::extract<2, 3>(value);
        bgParams[1].colorFormat = static_cast<ColorFormat>(bit::extract<4, 6>(value));
        bgParams[1].UpdateCHCTL();
        bgParams[1].rbgPageBaseAddressesDirty = true;

        bgParams[2].cellSizeShift = bit::extract<8>(value);
        bgParams[2].bitmap = bit::test<9>(value);
        bgParams[2].bmsz = bit::extract<10, 11>(value);
        bgParams[2].colorFormat = static_cast<ColorFormat>(bit::extract<12, 13>(value));
        bgParams[2].UpdateCHCTL();
    }

    // 18002A   CHCTLB  Character Control Register B
    //
    //   bits   r/w  code          description
    //     15        -             Reserved, must be zero
    //  14-12     W  R0CHCN2-0     RBG0 Character Color Number
    //                               NOTE: Exclusive Monitor cannot display this BG plane
    //                               000 (0) =       16 colors - palette
    //                               001 (1) =      256 colors - palette
    //                               010 (2) =     2048 colors - palette
    //                               011 (3) =    32768 colors - RGB
    //                               100 (4) = 16777216 colors - RGB (Normal mode only)
    //                                           forbidden for Hi-Res
    //                               101 (5) = forbidden
    //                               110 (6) = forbidden
    //                               111 (7) = forbidden
    //     11        -             Reserved, must be zero
    //     10     W  R0BMSZ        RBG0 Bitmap Size (0=512x256, 1=512x512)
    //      9     W  R0BMEN        RBG0 Bitmap Enable (0=cells, 1=bitmap)
    //      8     W  R0CHSZ        RBG0 Character Size (0=1x1, 1=2x2)
    //    7-6        -             Reserved, must be zero
    //      5     W  N3CHCN        NBG3 Character Color Number (0=16 colors, 1=256 colors; both palette)
    //      4     W  N3CHSZ        NBG3 Character Size (0=1x1, 1=2x2)
    //    3-2        -             Reserved, must be zero
    //      1     W  N2CHCN        NBG2 Character Color Number (0=16 colors, 1=256 colors; both palette)
    //      0     W  N2CHSZ        NBG2 Character Size (0=1x1, 1=2x2)

    FORCE_INLINE uint16 ReadCHCTLB() const {
        uint16 value = 0;
        bit::deposit_into<0>(value, bgParams[3].cellSizeShift);
        bit::deposit_into<1>(value, static_cast<uint32>(bgParams[3].colorFormat));

        bit::deposit_into<4>(value, bgParams[4].cellSizeShift);
        bit::deposit_into<5>(value, static_cast<uint32>(bgParams[4].colorFormat));

        bit::deposit_into<8>(value, bgParams[0].cellSizeShift);
        bit::deposit_into<9>(value, bgParams[0].bitmap);
        bit::deposit_into<10>(value, bgParams[0].bmsz);
        bit::deposit_into<12, 14>(value, static_cast<uint32>(bgParams[0].colorFormat));
        return value;
    }

    FORCE_INLINE void WriteCHCTLB(uint16 value) {
        accessPatternsDirty |= ReadCHCTLB() != value;

        bgParams[3].cellSizeShift = bit::extract<0>(value);
        bgParams[3].colorFormat = static_cast<ColorFormat>(bit::extract<1>(value));
        bgParams[3].UpdateCHCTL();

        bgParams[4].cellSizeShift = bit::extract<4>(value);
        bgParams[4].colorFormat = static_cast<ColorFormat>(bit::extract<5>(value));
        bgParams[4].UpdateCHCTL();

        bgParams[0].cellSizeShift = bit::extract<8>(value);
        bgParams[0].bitmap = bit::test<9>(value);
        bgParams[0].bmsz = bit::extract<10>(value);
        bgParams[0].colorFormat = static_cast<ColorFormat>(bit::extract<12, 14>(value));
        bgParams[0].UpdateCHCTL();
        bgParams[0].rbgPageBaseAddressesDirty = true;
    }

    // 18002C   BMPNA   NBG0/NBG1 Bitmap Palette Number
    //
    //   bits   r/w  code          description
    //  15-14        -             Reserved, must be zero
    //     13     W  N1BMPR        NBG1 Special Priority
    //     12     W  N1BMCC        NBG1 Special Color Calculation
    //     11        -             Reserved, must be zero
    //   10-8     W  N1BMP6-4      NBG1 Palette Number
    //    7-6        -             Reserved, must be zero
    //      5     W  N0BMPR        NBG0 Special Priority
    //      4     W  N0BMCC        NBG0 Special Color Calculation
    //      3        -             Reserved, must be zero
    //    2-0     W  N0BMP6-4      NBG0 Palette Number

    FORCE_INLINE uint16 ReadBMPNA() const {
        uint16 value = 0;
        bit::deposit_into<0, 2>(value, bgParams[1].supplBitmapPalNum >> 8u);
        bit::deposit_into<4>(value, bgParams[1].supplBitmapSpecialColorCalc);
        bit::deposit_into<5>(value, bgParams[1].supplBitmapSpecialPriority);

        bit::deposit_into<8, 10>(value, bgParams[2].supplBitmapPalNum >> 8u);
        bit::deposit_into<12>(value, bgParams[2].supplBitmapSpecialColorCalc);
        bit::deposit_into<13>(value, bgParams[2].supplBitmapSpecialPriority);
        return value;
    }

    FORCE_INLINE void WriteBMPNA(uint16 value) {
        bgParams[1].supplBitmapPalNum = bit::extract<0, 2>(value) << 8u;
        bgParams[1].supplBitmapSpecialColorCalc = bit::test<4>(value);
        bgParams[1].supplBitmapSpecialPriority = bit::test<5>(value);

        bgParams[2].supplBitmapPalNum = bit::extract<8, 10>(value) << 8u;
        bgParams[2].supplBitmapSpecialColorCalc = bit::test<12>(value);
        bgParams[2].supplBitmapSpecialPriority = bit::test<13>(value);
    }

    // 18002E   BMPNB   RBG0 Bitmap Palette Number
    //
    //   bits   r/w  code          description
    //   15-6        -             Reserved, must be zero
    //      5     W  R0BMPR        RBG0 Special Priority
    //      4     W  R0BMCC        RBG0 Special Color Calculation
    //      3        -             Reserved, must be zero
    //    2-0     W  R0BMP6-4      RBG0 Palette Number

    FORCE_INLINE uint16 ReadBMPNB() const {
        uint16 value = 0;
        bit::deposit_into<0, 2>(value, bgParams[0].supplBitmapPalNum >> 8u);
        bit::deposit_into<4>(value, bgParams[0].supplBitmapSpecialColorCalc);
        bit::deposit_into<5>(value, bgParams[0].supplBitmapSpecialPriority);
        return value;
    }

    FORCE_INLINE void WriteBMPNB(uint16 value) {
        bgParams[0].supplBitmapPalNum = bit::extract<0, 2>(value) << 8u;
        bgParams[0].supplBitmapSpecialColorCalc = bit::test<4>(value);
        bgParams[0].supplBitmapSpecialPriority = bit::test<5>(value);
    }

    // 180030   PNCN0   NBG0/RBG1 Pattern Name Control
    // 180032   PNCN1   NBG1 Pattern Name Control
    // 180034   PNCN2   NBG2 Pattern Name Control
    // 180036   PNCN3   NBG3 Pattern Name Control
    // 180038   PNCR    RBG0 Pattern Name Control
    //
    //   bits   r/w  code          description
    //     15     W  xxPNB         Pattern Name Data Size (0=2 words, 1=1 word)
    //     14     W  xxCNSM        Character Number Supplement
    //                               0 = char number is 10 bits; H/V flip available
    //                               1 = char number is 12 bits; H/V flip unavailable
    //  13-10        -             Reserved, must be zero
    //      9     W  xxSPR         Special Priority bit
    //      8     W  xxSCC         Special Color Calculation bit
    //    7-5     W  xxSPLT6-4     Supplementary Palette bits 6-4
    //    4-0     W  xxSCN4-0      Supplementary Character Number bits 4-0

    FORCE_INLINE uint16 ReadPNCN(uint32 bgIndex) const {
        uint16 value = 0;
        bit::deposit_into<0, 4>(value, bgParams[bgIndex].supplScrollCharNum);
        bit::deposit_into<5, 7>(value, bgParams[bgIndex].supplScrollPalNum >> 4u);
        bit::deposit_into<8>(value, bgParams[bgIndex].supplScrollSpecialColorCalc);
        bit::deposit_into<9>(value, bgParams[bgIndex].supplScrollSpecialPriority);
        bit::deposit_into<14>(value, bgParams[bgIndex].extChar);
        bit::deposit_into<15>(value, !bgParams[bgIndex].twoWordChar);
        return value;
    }
    FORCE_INLINE uint16 ReadPNCNA() const {
        return ReadPNCN(1);
    }
    FORCE_INLINE uint16 ReadPNCNB() const {
        return ReadPNCN(2);
    }
    FORCE_INLINE uint16 ReadPNCNC() const {
        return ReadPNCN(3);
    }
    FORCE_INLINE uint16 ReadPNCND() const {
        return ReadPNCN(4);
    }

    FORCE_INLINE void WritePNCN(uint32 bgIndex, uint16 value) {
        bgParams[bgIndex].supplScrollCharNum = bit::extract<0, 4>(value);
        bgParams[bgIndex].supplScrollPalNum = bit::extract<5, 7>(value) << 4u;
        bgParams[bgIndex].supplScrollSpecialColorCalc = bit::test<8>(value);
        bgParams[bgIndex].supplScrollSpecialPriority = bit::test<9>(value);
        bgParams[bgIndex].extChar = bit::test<14>(value);
        bgParams[bgIndex].twoWordChar = !bit::test<15>(value);
        bgParams[bgIndex].UpdatePageBaseAddresses();
        if (bgIndex <= 1) { // RBG0/1
            bgParams[bgIndex].rbgPageBaseAddressesDirty = true;
        }
    }
    FORCE_INLINE void WritePNCNA(uint16 value) {
        WritePNCN(1, value);
    }
    FORCE_INLINE void WritePNCNB(uint16 value) {
        WritePNCN(2, value);
    }
    FORCE_INLINE void WritePNCNC(uint16 value) {
        WritePNCN(3, value);
    }
    FORCE_INLINE void WritePNCND(uint16 value) {
        WritePNCN(4, value);
    }

    FORCE_INLINE uint16 ReadPNCR() const {
        uint16 value = 0;
        bit::deposit_into<0, 4>(value, bgParams[0].supplScrollCharNum);
        bit::deposit_into<5, 7>(value, bgParams[0].supplScrollPalNum >> 4u);
        bit::deposit_into<8>(value, bgParams[0].supplScrollSpecialColorCalc);
        bit::deposit_into<9>(value, bgParams[0].supplScrollSpecialPriority);
        bit::deposit_into<14>(value, bgParams[0].extChar);
        bit::deposit_into<15>(value, !bgParams[0].twoWordChar);
        return value;
    }

    FORCE_INLINE void WritePNCR(uint16 value) {
        bgParams[0].supplScrollCharNum = bit::extract<0, 4>(value);
        bgParams[0].supplScrollPalNum = bit::extract<5, 7>(value) << 4u;
        bgParams[0].supplScrollSpecialColorCalc = bit::test<8>(value);
        bgParams[0].supplScrollSpecialPriority = bit::test<9>(value);
        bgParams[0].extChar = bit::test<14>(value);
        bgParams[0].twoWordChar = !bit::test<15>(value);
    }

    // 18003A   PLSZ    Plane Size
    //
    //   bits   r/w  code          description
    //  15-14     W  RBOVR1-0      Rotation Parameter B Screen-over Process
    //  13-12     W  RBPLSZ1-0     Rotation Parameter B Plane Size
    //  11-10     W  RAOVR1-0      Rotation Parameter A Screen-over Process
    //    9-8     W  RAPLSZ1-0     Rotation Parameter A Plane Size
    //    7-6     W  N3PLSZ1-0     NBG3 Plane Size
    //    5-4     W  N2PLSZ1-0     NBG2 Plane Size
    //    3-2     W  N1PLSZ1-0     NBG1 Plane Size
    //    1-0     W  N0PLSZ1-0     NBG0 Plane Size
    //
    //  xxOVR1-0:
    //    00 (0) = Repeat plane infinitely
    //    01 (1) = Use character pattern in screen-over pattern name register
    //    10 (2) = Transparent
    //    11 (3) = Force 512x512 with transparent outsides (256 line bitmaps draw twice)
    //
    //  xxPLSZ1-0:
    //    00 (0) = 1x1
    //    01 (1) = 2x1
    //    10 (2) = forbidden (but probably 1x2)
    //    11 (3) = 2x2

    FORCE_INLINE uint16 ReadPLSZ() const {
        uint16 value = 0;
        bit::deposit_into<0, 1>(value, bgParams[1].plsz);
        bit::deposit_into<2, 3>(value, bgParams[2].plsz);
        bit::deposit_into<4, 5>(value, bgParams[3].plsz);
        bit::deposit_into<6, 7>(value, bgParams[4].plsz);

        bit::deposit_into<8, 9>(value, rotParams[0].plsz);
        bit::deposit_into<10, 11>(value, static_cast<uint32>(rotParams[0].screenOverProcess));
        bit::deposit_into<12, 13>(value, rotParams[1].plsz);
        bit::deposit_into<14, 15>(value, static_cast<uint32>(rotParams[1].screenOverProcess));
        return value;
    }

    FORCE_INLINE void WritePLSZ(uint16 value) {
        bgParams[1].plsz = bit::extract<0, 1>(value);
        bgParams[2].plsz = bit::extract<2, 3>(value);
        bgParams[3].plsz = bit::extract<4, 5>(value);
        bgParams[4].plsz = bit::extract<6, 7>(value);
        bgParams[1].UpdatePLSZ();
        bgParams[2].UpdatePLSZ();
        bgParams[3].UpdatePLSZ();
        bgParams[4].UpdatePLSZ();

        rotParams[0].plsz = bit::extract<8, 9>(value);
        rotParams[0].screenOverProcess = static_cast<ScreenOverProcess>(bit::extract<10, 11>(value));
        rotParams[1].plsz = bit::extract<12, 13>(value);
        rotParams[1].screenOverProcess = static_cast<ScreenOverProcess>(bit::extract<14, 15>(value));
        rotParams[0].UpdatePLSZ();
        rotParams[1].UpdatePLSZ();

        bgParams[0].rbgPageBaseAddressesDirty = true;
        bgParams[1].rbgPageBaseAddressesDirty = true;
    }

    // 18003C   MPOFN   NBG0-3 Map Offset
    //
    //   bits   r/w  code          description
    //     15        -             Reserved, must be zero
    //  14-12     W  M3MP8-6       NBG3 Map Offset
    //     11        -             Reserved, must be zero
    //   10-8     W  M2MP8-6       NBG2 Map Offset
    //      7        -             Reserved, must be zero
    //    6-4     W  M1MP8-6       NBG1 Map Offset
    //      3        -             Reserved, must be zero
    //    2-0     W  M0MP8-6       NBG0 Map Offset

    FORCE_INLINE uint16 ReadMPOFN() const {
        uint16 value = 0;
        bit::deposit_into<0, 2>(value, bit::extract<6, 8>(bgParams[1].mapIndices[0]));
        bit::deposit_into<4, 6>(value, bit::extract<6, 8>(bgParams[2].mapIndices[0]));
        bit::deposit_into<8, 10>(value, bit::extract<6, 8>(bgParams[3].mapIndices[0]));
        bit::deposit_into<12, 14>(value, bit::extract<6, 8>(bgParams[4].mapIndices[0]));
        return value;
    }

    FORCE_INLINE void WriteMPOFN(uint16 value) {
        for (int i = 0; i < 4; i++) {
            bit::deposit_into<6, 8>(bgParams[1].mapIndices[i], bit::extract<0, 2>(value));
            bit::deposit_into<6, 8>(bgParams[2].mapIndices[i], bit::extract<4, 6>(value));
            bit::deposit_into<6, 8>(bgParams[3].mapIndices[i], bit::extract<8, 10>(value));
            bit::deposit_into<6, 8>(bgParams[4].mapIndices[i], bit::extract<12, 14>(value));
        }
        // shift by 17 is the same as multiply by 0x20000, which is the boundary for bitmap data
        bgParams[1].bitmapBaseAddress = bit::extract<0, 2>(value) << 17u;
        bgParams[2].bitmapBaseAddress = bit::extract<4, 6>(value) << 17u;
        bgParams[3].bitmapBaseAddress = bit::extract<8, 10>(value) << 17u;
        bgParams[4].bitmapBaseAddress = bit::extract<12, 14>(value) << 17u;

        bgParams[1].UpdatePageBaseAddresses();
        bgParams[2].UpdatePageBaseAddresses();
        bgParams[3].UpdatePageBaseAddresses();
        bgParams[4].UpdatePageBaseAddresses();
    }

    // 18003E   MPOFR   Rotation Parameter A/B Map Offset
    //
    //   bits   r/w  code          description
    //   15-7        -             Reserved, must be zero
    //    6-4     W  RBMP8-6       Rotation Parameter B Map Offset
    //      3        -             Reserved, must be zero
    //    2-0     W  RAMP8-6       Rotation Parameter A Map Offset

    FORCE_INLINE uint16 ReadMPOFR() const {
        uint16 value = 0;
        bit::deposit_into<0, 2>(value, bit::extract<6, 8>(rotParams[0].mapIndices[0]));
        bit::deposit_into<4, 6>(value, bit::extract<6, 8>(rotParams[1].mapIndices[0]));
        return value;
    }

    FORCE_INLINE void WriteMPOFR(uint16 value) {
        for (int i = 0; i < 16; i++) {
            bit::deposit_into<6, 8>(rotParams[0].mapIndices[i], bit::extract<0, 2>(value));
            bit::deposit_into<6, 8>(rotParams[1].mapIndices[i], bit::extract<4, 6>(value));
        }
        // shift by 17 is the same as multiply by 0x20000, which is the boundary for bitmap data
        rotParams[0].bitmapBaseAddress = bit::extract<0, 2>(value) << 17u;
        rotParams[1].bitmapBaseAddress = bit::extract<4, 6>(value) << 17u;

        bgParams[0].rbgPageBaseAddressesDirty = true;
        bgParams[1].rbgPageBaseAddressesDirty = true;
    }

    // 180040   MPABN0  NBG0 Normal Scroll Screen Map for Planes A,B
    // 180042   MPCDN0  NBG0 Normal Scroll Screen Map for Planes C,D
    // 180044   MPABN1  NBG1 Normal Scroll Screen Map for Planes A,B
    // 180046   MPCDN1  NBG1 Normal Scroll Screen Map for Planes C,D
    // 180048   MPABN2  NBG2 Normal Scroll Screen Map for Planes A,B
    // 18004A   MPCDN2  NBG2 Normal Scroll Screen Map for Planes C,D
    // 18004C   MPABN3  NBG3 Normal Scroll Screen Map for Planes A,B
    // 18004E   MPCDN3  NBG3 Normal Scroll Screen Map for Planes C,D
    //
    //   bits   r/w  code          description
    //  15-14        -             Reserved, must be zero
    //   13-8     W  xxMPy5-0      BG xx Plane y Map
    //    7-6        -             Reserved, must be zero
    //    5-0     W  xxMPy5-0      BG xx Plane y Map
    //
    // xx:
    //   N0 = NBG0 (MPyyN0)
    //   N1 = NBG1 (MPyyN1)
    //   N2 = NBG2 (MPyyN2)
    //   N3 = NBG3 (MPyyN3)
    // y:
    //   A = Plane A (bits  5-0 of MPABxx)
    //   B = Plane B (bits 13-8 of MPABxx)
    //   C = Plane C (bits  5-0 of MPCDxx)
    //   D = Plane D (bits 13-8 of MPCDxx)

    FORCE_INLINE uint16 ReadMPN(uint32 bgIndex, uint32 planeIndex) const {
        uint16 value = 0;
        auto &bg = bgParams[bgIndex];
        bit::deposit_into<0, 5>(value, bit::extract<0, 5>(bg.mapIndices[planeIndex * 2 + 0]));
        bit::deposit_into<8, 13>(value, bit::extract<0, 5>(bg.mapIndices[planeIndex * 2 + 1]));
        return value;
    }
    FORCE_INLINE uint16 ReadMPABN0() const {
        return ReadMPN(1, 0);
    }
    FORCE_INLINE uint16 ReadMPCDN0() const {
        return ReadMPN(1, 1);
    }
    FORCE_INLINE uint16 ReadMPABN1() const {
        return ReadMPN(2, 0);
    }
    FORCE_INLINE uint16 ReadMPCDN1() const {
        return ReadMPN(2, 1);
    }
    FORCE_INLINE uint16 ReadMPABN2() const {
        return ReadMPN(3, 0);
    }
    FORCE_INLINE uint16 ReadMPCDN2() const {
        return ReadMPN(3, 1);
    }
    FORCE_INLINE uint16 ReadMPABN3() const {
        return ReadMPN(4, 0);
    }
    FORCE_INLINE uint16 ReadMPCDN3() const {
        return ReadMPN(4, 1);
    }

    FORCE_INLINE void WriteMPN(uint32 bgIndex, uint32 planeIndex, uint16 value) {
        auto &bg = bgParams[bgIndex];
        bit::deposit_into<0, 5>(bg.mapIndices[planeIndex * 2 + 0], bit::extract<0, 5>(value));
        bit::deposit_into<0, 5>(bg.mapIndices[planeIndex * 2 + 1], bit::extract<8, 13>(value));
        bg.UpdatePageBaseAddresses();
    }
    FORCE_INLINE void WriteMPABN0(uint16 value) {
        WriteMPN(1, 0, value);
    }
    FORCE_INLINE void WriteMPCDN0(uint16 value) {
        WriteMPN(1, 1, value);
    }
    FORCE_INLINE void WriteMPABN1(uint16 value) {
        WriteMPN(2, 0, value);
    }
    FORCE_INLINE void WriteMPCDN1(uint16 value) {
        WriteMPN(2, 1, value);
    }
    FORCE_INLINE void WriteMPABN2(uint16 value) {
        WriteMPN(3, 0, value);
    }
    FORCE_INLINE void WriteMPCDN2(uint16 value) {
        WriteMPN(3, 1, value);
    }
    FORCE_INLINE void WriteMPABN3(uint16 value) {
        WriteMPN(4, 0, value);
    }
    FORCE_INLINE void WriteMPCDN3(uint16 value) {
        WriteMPN(4, 1, value);
    }

    // 180050   MPABRA  Rotation Parameter A Scroll Surface Map for Screen Planes A,B
    // 180052   MPCDRA  Rotation Parameter A Scroll Surface Map for Screen Planes C,D
    // 180054   MPEFRA  Rotation Parameter A Scroll Surface Map for Screen Planes E,F
    // 180056   MPGHRA  Rotation Parameter A Scroll Surface Map for Screen Planes G,H
    // 180058   MPIJRA  Rotation Parameter A Scroll Surface Map for Screen Planes I,J
    // 18005A   MPKLRA  Rotation Parameter A Scroll Surface Map for Screen Planes K,L
    // 18005C   MPMNRA  Rotation Parameter A Scroll Surface Map for Screen Planes M,N
    // 18005E   MPOPRA  Rotation Parameter A Scroll Surface Map for Screen Planes O,P
    // 180060   MPABRB  Rotation Parameter B Scroll Surface Map for Screen Planes A,B
    // 180062   MPCDRB  Rotation Parameter B Scroll Surface Map for Screen Planes C,D
    // 180064   MPEFRB  Rotation Parameter B Scroll Surface Map for Screen Planes E,F
    // 180066   MPGHRB  Rotation Parameter B Scroll Surface Map for Screen Planes G,H
    // 180068   MPIJRB  Rotation Parameter B Scroll Surface Map for Screen Planes I,J
    // 18006A   MPKLRB  Rotation Parameter B Scroll Surface Map for Screen Planes K,L
    // 18006C   MPMNRB  Rotation Parameter B Scroll Surface Map for Screen Planes M,N
    // 18006E   MPOPRB  Rotation Parameter B Scroll Surface Map for Screen Planes O,P
    //
    //   bits   r/w  code          description
    //  15-14        -             Reserved, must be zero
    //   13-8     W  RxMPy5-0      Rotation Parameter x Screen Plane y Map
    //    7-6        -             Reserved, must be zero
    //    5-0     W  RxMPy5-0      Rotation Parameter x Screen Plane y Map
    //
    // x:
    //   A = Rotation Parameter A (MPyyRA)
    //   B = Rotation Parameter A (MPyyRB)
    // y:
    //   A = Screen Plane A (bits  5-0 of MPABxx)
    //   B = Screen Plane B (bits 13-8 of MPABxx)
    //   C = Screen Plane C (bits  5-0 of MPCDxx)
    //   D = Screen Plane D (bits 13-8 of MPCDxx)
    //   ...
    //   M = Screen Plane M (bits  5-0 of MPMNxx)
    //   N = Screen Plane N (bits 13-8 of MPMNxx)
    //   O = Screen Plane O (bits  5-0 of MPOPxx)
    //   P = Screen Plane P (bits 13-8 of MPOPxx)

    FORCE_INLINE uint16 ReadMPR(uint32 paramIndex, uint32 planeIndex) const {
        uint16 value = 0;
        auto &bg = rotParams[paramIndex];
        bit::deposit_into<0, 5>(value, bit::extract<0, 5>(bg.mapIndices[planeIndex * 2 + 0]));
        bit::deposit_into<8, 13>(value, bit::extract<0, 5>(bg.mapIndices[planeIndex * 2 + 1]));
        return value;
    }
    FORCE_INLINE uint16 ReadMPABRA() const {
        return ReadMPR(0, 0);
    }
    FORCE_INLINE uint16 ReadMPCDRA() const {
        return ReadMPR(0, 1);
    }
    FORCE_INLINE uint16 ReadMPEFRA() const {
        return ReadMPR(0, 2);
    }
    FORCE_INLINE uint16 ReadMPGHRA() const {
        return ReadMPR(0, 3);
    }
    FORCE_INLINE uint16 ReadMPIJRA() const {
        return ReadMPR(0, 4);
    }
    FORCE_INLINE uint16 ReadMPKLRA() const {
        return ReadMPR(0, 5);
    }
    FORCE_INLINE uint16 ReadMPMNRA() const {
        return ReadMPR(0, 6);
    }
    FORCE_INLINE uint16 ReadMPOPRA() const {
        return ReadMPR(0, 7);
    }
    FORCE_INLINE uint16 ReadMPABRB() const {
        return ReadMPR(1, 0);
    }
    FORCE_INLINE uint16 ReadMPCDRB() const {
        return ReadMPR(1, 1);
    }
    FORCE_INLINE uint16 ReadMPEFRB() const {
        return ReadMPR(1, 2);
    }
    FORCE_INLINE uint16 ReadMPGHRB() const {
        return ReadMPR(1, 3);
    }
    FORCE_INLINE uint16 ReadMPIJRB() const {
        return ReadMPR(1, 4);
    }
    FORCE_INLINE uint16 ReadMPKLRB() const {
        return ReadMPR(1, 5);
    }
    FORCE_INLINE uint16 ReadMPMNRB() const {
        return ReadMPR(1, 6);
    }
    FORCE_INLINE uint16 ReadMPOPRB() const {
        return ReadMPR(1, 7);
    }

    FORCE_INLINE void WriteMPR(uint32 paramIndex, uint32 planeIndex, uint16 value) {
        auto &bg = rotParams[paramIndex];
        bit::deposit_into<0, 5>(bg.mapIndices[planeIndex * 2 + 0], bit::extract<0, 5>(value));
        bit::deposit_into<0, 5>(bg.mapIndices[planeIndex * 2 + 1], bit::extract<8, 13>(value));

        bgParams[0].rbgPageBaseAddressesDirty = true;
        bgParams[1].rbgPageBaseAddressesDirty = true;
    }
    FORCE_INLINE void WriteMPABRA(uint16 value) {
        WriteMPR(0, 0, value);
    }
    FORCE_INLINE void WriteMPCDRA(uint16 value) {
        WriteMPR(0, 1, value);
    }
    FORCE_INLINE void WriteMPEFRA(uint16 value) {
        WriteMPR(0, 2, value);
    }
    FORCE_INLINE void WriteMPGHRA(uint16 value) {
        WriteMPR(0, 3, value);
    }
    FORCE_INLINE void WriteMPIJRA(uint16 value) {
        WriteMPR(0, 4, value);
    }
    FORCE_INLINE void WriteMPKLRA(uint16 value) {
        WriteMPR(0, 5, value);
    }
    FORCE_INLINE void WriteMPMNRA(uint16 value) {
        WriteMPR(0, 6, value);
    }
    FORCE_INLINE void WriteMPOPRA(uint16 value) {
        WriteMPR(0, 7, value);
    }
    FORCE_INLINE void WriteMPABRB(uint16 value) {
        WriteMPR(1, 0, value);
    }
    FORCE_INLINE void WriteMPCDRB(uint16 value) {
        WriteMPR(1, 1, value);
    }
    FORCE_INLINE void WriteMPEFRB(uint16 value) {
        WriteMPR(1, 2, value);
    }
    FORCE_INLINE void WriteMPGHRB(uint16 value) {
        WriteMPR(1, 3, value);
    }
    FORCE_INLINE void WriteMPIJRB(uint16 value) {
        WriteMPR(1, 4, value);
    }
    FORCE_INLINE void WriteMPKLRB(uint16 value) {
        WriteMPR(1, 5, value);
    }
    FORCE_INLINE void WriteMPMNRB(uint16 value) {
        WriteMPR(1, 6, value);
    }
    FORCE_INLINE void WriteMPOPRB(uint16 value) {
        WriteMPR(1, 7, value);
    }

    // 180070   SCXIN0  NBG0 Horizontal Screen Scroll Value (integer part)
    // 180072   SCXDN0  NBG0 Horizontal Screen Scroll Value (fractional part)
    // 180074   SCYIN0  NBG0 Vertical Screen Scroll Value (integer part)
    // 180076   SCYDN0  NBG0 Vertical Screen Scroll Value (fractional part)
    // 180080   SCXIN1  NBG1 Horizontal Screen Scroll Value (integer part)
    // 180082   SCXDN1  NBG1 Horizontal Screen Scroll Value (fractional part)
    // 180084   SCYIN1  NBG1 Vertical Screen Scroll Value (integer part)
    // 180086   SCYDN1  NBG1 Vertical Screen Scroll Value (fractional part)
    //
    // SCdINx:  (d=X,Y; x=0,1)
    //   bits   r/w  code          description
    //  15-11        -             Reserved, must be zero
    //   10-0     W  NxSCdI10-0    Horizontal/Vertical Screen Scroll Value (integer part)
    //
    // SCdDNx:  (d=X,Y; x=0,1)
    //   bits   r/w  code          description
    //   15-8     W  NxSCdD1-8     Horizontal/Vertical Screen Scroll Value (fractional part)
    //    7-0        -             Reserved, must be zero
    //
    // 180090   SCXN2   NBG2 Horizontal Screen Scroll Value
    // 180092   SCYN2   NBG2 Vertical Screen Scroll Value
    // 180094   SCXN3   NBG3 Horizontal Screen Scroll Value
    // 180096   SCYN3   NBG3 Vertical Screen Scroll Value
    //
    // SCdNx:  (d=X,Y; x=2,3)
    //   bits   r/w  code          description
    //  15-11        -             Reserved, must be zero
    //   10-0     W  NxSCd10-0     Horizontal/Vertical Screen Scroll Value (integer)

    FORCE_INLINE uint16 ReadSCXIN(uint32 bgIndex) const {
        return bit::extract<8, 18>(bgParams[bgIndex].scrollAmountH);
    }
    FORCE_INLINE uint16 ReadSCXIN0() const {
        return ReadSCXIN(1);
    }
    FORCE_INLINE uint16 ReadSCXIN1() const {
        return ReadSCXIN(2);
    }
    FORCE_INLINE uint16 ReadSCXN2() const {
        return ReadSCXIN(3);
    }
    FORCE_INLINE uint16 ReadSCXN3() const {
        return ReadSCXIN(4);
    }

    FORCE_INLINE void WriteSCXIN(uint32 bgIndex, uint16 value) {
        bit::deposit_into<8, 18>(bgParams[bgIndex].scrollAmountH, bit::extract<0, 10>(value));
    }
    FORCE_INLINE void WriteSCXIN0(uint16 value) {
        WriteSCXIN(1, value);
    }
    FORCE_INLINE void WriteSCXIN1(uint16 value) {
        WriteSCXIN(2, value);
    }
    FORCE_INLINE void WriteSCXN2(uint16 value) {
        WriteSCXIN(3, value);
    }
    FORCE_INLINE void WriteSCXN3(uint16 value) {
        WriteSCXIN(4, value);
    }

    FORCE_INLINE uint16 ReadSCXDN(uint32 bgIndex) const {
        return bit::extract<0, 7>(bgParams[bgIndex].scrollAmountH) << 8u;
    }
    FORCE_INLINE uint16 ReadSCXDN0() const {
        return ReadSCXDN(1);
    }
    FORCE_INLINE uint16 ReadSCXDN1() const {
        return ReadSCXDN(2);
    }

    FORCE_INLINE void WriteSCXDN(uint32 bgIndex, uint16 value) {
        bit::deposit_into<0, 7>(bgParams[bgIndex].scrollAmountH, bit::extract<8, 15>(value));
    }
    FORCE_INLINE void WriteSCXDN0(uint16 value) {
        WriteSCXDN(1, value);
    }
    FORCE_INLINE void WriteSCXDN1(uint16 value) {
        WriteSCXDN(2, value);
    }

    FORCE_INLINE uint16 ReadSCYIN(uint32 bgIndex) const {
        return bit::extract<8, 18>(bgParams[bgIndex].scrollAmountV);
    }
    FORCE_INLINE uint16 ReadSCYIN0() const {
        return ReadSCYIN(1);
    }
    FORCE_INLINE uint16 ReadSCYIN1() const {
        return ReadSCYIN(2);
    }
    FORCE_INLINE uint16 ReadSCYN2() const {
        return ReadSCYIN(3);
    }
    FORCE_INLINE uint16 ReadSCYN3() const {
        return ReadSCYIN(4);
    }

    FORCE_INLINE void WriteSCYIN(uint32 bgIndex, uint16 value) {
        bit::deposit_into<8, 18>(bgParams[bgIndex].scrollAmountV, bit::extract<0, 10>(value));
    }
    FORCE_INLINE void WriteSCYIN0(uint16 value) {
        WriteSCYIN(1, value);
    }
    FORCE_INLINE void WriteSCYIN1(uint16 value) {
        WriteSCYIN(2, value);
    }
    FORCE_INLINE void WriteSCYN2(uint16 value) {
        WriteSCYIN(3, value);
    }
    FORCE_INLINE void WriteSCYN3(uint16 value) {
        WriteSCYIN(4, value);
    }

    FORCE_INLINE uint16 ReadSCYDN(uint32 bgIndex) const {
        return bit::extract<0, 7>(bgParams[bgIndex].scrollAmountV) << 8u;
    }
    FORCE_INLINE uint16 ReadSCYDN0() const {
        return ReadSCYDN(1);
    }
    FORCE_INLINE uint16 ReadSCYDN1() const {
        return ReadSCYDN(2);
    }

    FORCE_INLINE void WriteSCYDN(uint32 bgIndex, uint16 value) {
        bit::deposit_into<0, 7>(bgParams[bgIndex].scrollAmountV, bit::extract<8, 15>(value));
    }
    FORCE_INLINE void WriteSCYDN0(uint16 value) {
        WriteSCYDN(1, value);
    }
    FORCE_INLINE void WriteSCYDN1(uint16 value) {
        WriteSCYDN(2, value);
    }

    // 180078   ZMXIN0  NBG0 Horizontal Coordinate Increment (integer part)
    // 18007A   ZMXDN0  NBG0 Horizontal Coordinate Increment (fractional part)
    // 18007C   ZMYIN0  NBG0 Vertical Coordinate Increment (integer part)
    // 18007E   ZMYDN0  NBG0 Vertical Coordinate Increment (fractional part)
    // 180088   ZMXIN1  NBG1 Horizontal Coordinate Increment (integer part)
    // 18008A   ZMXDN1  NBG1 Horizontal Coordinate Increment (fractional part)
    // 18008C   ZMYIN1  NBG1 Vertical Coordinate Increment (integer part)
    // 18008E   ZMYDN1  NBG1 Vertical Coordinate Increment (fractional part)
    //
    // ZMdINx:  (d=X,Y; x=0,1)
    //   bits   r/w  code          description
    //   15-3        -             Reserved, must be zero
    //    2-0     W  NxZMdI2-0     Horizontal/Vertical Coordinate Increment (integer part)
    //
    // ZMdDNx:  (d=X,Y; x=0,1)
    //   bits   r/w  code          description
    //   15-8     W  NxZMdD1-8     Horizontal/Vertical Coordinate Increment (fractional part)
    //    7-0        -             Reserved, must be zero

    FORCE_INLINE uint16 ReadZMXIN(uint32 bgIndex) const {
        return bit::extract<8, 10>(bgParams[bgIndex].scrollIncH);
    }
    FORCE_INLINE uint16 ReadZMXIN0() const {
        return ReadZMXIN(1);
    }
    FORCE_INLINE uint16 ReadZMXIN1() const {
        return ReadZMXIN(2);
    }

    FORCE_INLINE void WriteZMXIN(uint32 bgIndex, uint16 value) {
        bit::deposit_into<8, 10>(bgParams[bgIndex].scrollIncH, bit::extract<0, 2>(value));
    }
    FORCE_INLINE void WriteZMXIN0(uint16 value) {
        WriteZMXIN(1, value);
    }
    FORCE_INLINE void WriteZMXIN1(uint16 value) {
        WriteZMXIN(2, value);
    }

    FORCE_INLINE uint16 ReadZMXDN(uint32 bgIndex) const {
        return bit::extract<0, 7>(bgParams[bgIndex].scrollIncH) << 8u;
    }
    FORCE_INLINE uint16 ReadZMXDN0() const {
        return ReadZMXDN(1);
    }
    FORCE_INLINE uint16 ReadZMXDN1() const {
        return ReadZMXDN(2);
    }

    FORCE_INLINE void WriteZMXDN(uint32 bgIndex, uint16 value) {
        bit::deposit_into<0, 7>(bgParams[bgIndex].scrollIncH, bit::extract<8, 15>(value));
    }
    FORCE_INLINE void WriteZMXDN0(uint16 value) {
        WriteZMXDN(1, value);
    }
    FORCE_INLINE void WriteZMXDN1(uint16 value) {
        WriteZMXDN(2, value);
    }

    FORCE_INLINE uint16 ReadZMYIN(uint32 bgIndex) const {
        return bit::extract<8, 10>(bgParams[bgIndex].scrollIncV);
    }
    FORCE_INLINE uint16 ReadZMYIN0() const {
        return ReadZMYIN(1);
    }
    FORCE_INLINE uint16 ReadZMYIN1() const {
        return ReadZMYIN(2);
    }

    FORCE_INLINE void WriteZMYIN(uint32 bgIndex, uint16 value) {
        bit::deposit_into<8, 10>(bgParams[bgIndex].scrollIncV, bit::extract<0, 2>(value));
    }
    FORCE_INLINE void WriteZMYIN0(uint16 value) {
        WriteZMYIN(1, value);
    }
    FORCE_INLINE void WriteZMYIN1(uint16 value) {
        WriteZMYIN(2, value);
    }

    FORCE_INLINE uint16 ReadZMYDN(uint32 bgIndex) const {
        return bit::extract<0, 7>(bgParams[bgIndex].scrollIncV) << 8u;
    }
    FORCE_INLINE uint16 ReadZMYDN0() const {
        return ReadZMYDN(1);
    }
    FORCE_INLINE uint16 ReadZMYDN1() const {
        return ReadZMYDN(2);
    }

    FORCE_INLINE void WriteZMYDN(uint32 bgIndex, uint16 value) {
        bit::deposit_into<0, 7>(bgParams[bgIndex].scrollIncV, bit::extract<8, 15>(value));
    }
    FORCE_INLINE void WriteZMYDN0(uint16 value) {
        WriteZMYDN(1, value);
    }
    FORCE_INLINE void WriteZMYDN1(uint16 value) {
        WriteZMYDN(2, value);
    }

    // 180098   ZMCTL   Reduction Enable
    RegZMCTL ZMCTL;

    FORCE_INLINE uint16 ReadZMCTL() const {
        return ZMCTL.u16;
    }

    FORCE_INLINE void WriteZMCTL(uint16 value) {
        const uint16 oldZMCTL = ZMCTL.u16;
        ZMCTL.u16 = value & 0x0303;
        accessPatternsDirty |= oldZMCTL != ZMCTL.u16;
    }

    // 18009A   SCRCTL  Line and Vertical Cell Scroll Control
    //
    //   bits   r/w  code          description
    //  15-14        -             Reserved, must be zero
    //  13-12     W  N1LSS1-0      NBG1 Line Scroll Interval
    //                               00 (0) = Each line
    //                               01 (1) = Every 2 lines
    //                               10 (2) = Every 4 lines
    //                               11 (3) = Every 8 lines
    //                               NOTE: Values are doubled for single-density interlaced mode
    //     11     W  N1LZMX        NBG1 Line Zoom X Enable (0=disable, 1=enable)
    //     10     W  N1LSCY        NBG1 Line Scroll Y Enable (0=disable, 1=enable)
    //      9     W  N1LSCX        NBG1 Line Scroll X Enable (0=disable, 1=enable)
    //      8     W  N1VCSC        NBG1 Vertical Cell Scroll Enable (0=disable, 1=enable)
    //    7-6        -             Reserved, must be zero
    //    5-4     W  N0LSS1-0      NBG0 Line Scroll Interval
    //                               00 (0) = Each line
    //                               01 (1) = Every 2 lines
    //                               10 (2) = Every 4 lines
    //                               11 (3) = Every 8 lines
    //                               NOTE: Values are doubled for single-density interlaced mode
    //      3     W  N0LZMX        NBG0 Line Zoom X Enable (0=disable, 1=enable)
    //      2     W  N0LSCY        NBG0 Line Scroll Y Enable (0=disable, 1=enable)
    //      1     W  N0LSCX        NBG0 Line Scroll X Enable (0=disable, 1=enable)
    //      0     W  N0VCSC        NBG0 Vertical Cell Scroll Enable (0=disable, 1=enable)

    FORCE_INLINE uint16 ReadSCRCTL() const {
        uint16 value = 0;
        bit::deposit_into<0>(value, bgParams[1].vcellScrollEnable);
        bit::deposit_into<1>(value, bgParams[1].lineScrollXEnable);
        bit::deposit_into<2>(value, bgParams[1].lineScrollYEnable);
        bit::deposit_into<3>(value, bgParams[1].lineZoomEnable);
        bit::deposit_into<4, 5>(value, bgParams[1].lineScrollInterval);

        bit::deposit_into<8>(value, bgParams[2].vcellScrollEnable);
        bit::deposit_into<9>(value, bgParams[2].lineScrollXEnable);
        bit::deposit_into<10>(value, bgParams[2].lineScrollYEnable);
        bit::deposit_into<11>(value, bgParams[2].lineZoomEnable);
        bit::deposit_into<12, 13>(value, bgParams[2].lineScrollInterval);
        return value;
    }

    FORCE_INLINE void WriteSCRCTL(uint16 value) {
        vcellScrollDirty |= bgParams[1].vcellScrollEnable != bit::test<0>(value);
        vcellScrollDirty |= bgParams[2].vcellScrollEnable != bit::test<8>(value);

        bgParams[1].vcellScrollEnable = bit::test<0>(value);
        bgParams[1].lineScrollXEnable = bit::test<1>(value);
        bgParams[1].lineScrollYEnable = bit::test<2>(value);
        bgParams[1].lineZoomEnable = bit::test<3>(value);
        bgParams[1].lineScrollInterval = bit::extract<4, 5>(value);

        bgParams[2].vcellScrollEnable = bit::test<8>(value);
        bgParams[2].lineScrollXEnable = bit::test<9>(value);
        bgParams[2].lineScrollYEnable = bit::test<10>(value);
        bgParams[2].lineZoomEnable = bit::test<11>(value);
        bgParams[2].lineScrollInterval = bit::extract<12, 13>(value);
    }

    // 18009C   VCSTAU  Vertical Cell Scroll Table Address (upper)
    //
    //   bits   r/w  code          description
    //   15-3        -             Reserved, must be zero
    //    2-0     W  VCSTA18-16    Vertical Cell Scroll Table Base Address (bits 18-16)
    //
    // 18009E   VCSTAL  Vertical Cell Scroll Table Address (lower)
    //
    //   bits   r/w  code          description
    //   15-1     W  VCSTA15-1     Vertical Cell Scroll Table Base Address (bits 15-1)
    //      0        -             Reserved, must be zero

    FORCE_INLINE uint16 ReadVCSTAU() const {
        return bit::extract<17, 19>(vcellScrollTableAddress);
    }

    FORCE_INLINE void WriteVCSTAU(uint16 value) {
        bit::deposit_into<17, 19>(vcellScrollTableAddress, bit::extract<0, 2>(value));
    }

    FORCE_INLINE uint16 ReadVCSTAL() const {
        return bit::extract<2, 16>(vcellScrollTableAddress) << 1u;
    }

    FORCE_INLINE void WriteVCSTAL(uint16 value) {
        bit::deposit_into<2, 16>(vcellScrollTableAddress, bit::extract<1, 15>(value));
    }

    // 1800A0   LSTA0U  NBG0 Line Scroll Table Address (upper)
    // 1800A4   LSTA1U  NBG1 Line Scroll Table Address (upper)
    //
    //   bits   r/w  code          description
    //   15-3        -             Reserved, must be zero
    //    2-0     W  NxLSTA18-16   NBGx Line Scroll Table Base Address (bits 18-16)
    //
    // 1800A2   LSTA0L  NBG0 Line Scroll Table Address (lower)
    // 1800A6   LSTA1L  NBG1 Line Scroll Table Address (lower)
    //
    //   bits   r/w  code          description
    //   15-1     W  NxLSTA15-1    NBGx Line Scroll Table Base Address (bits 15-1)
    //      0        -             Reserved, must be zero

    FORCE_INLINE uint16 ReadLSTAnU(uint8 bgIndex) const {
        return bit::extract<17, 19>(bgParams[bgIndex].lineScrollTableAddress);
    }
    FORCE_INLINE uint16 ReadLSTA0U() const {
        return ReadLSTAnU(1);
    }
    FORCE_INLINE uint16 ReadLSTA1U() const {
        return ReadLSTAnU(2);
    }

    FORCE_INLINE void WriteLSTAnU(uint8 bgIndex, uint16 value) {
        bit::deposit_into<17, 19>(bgParams[bgIndex].lineScrollTableAddress, bit::extract<0, 2>(value));
    }
    FORCE_INLINE void WriteLSTA0U(uint16 value) {
        WriteLSTAnU(1, value);
    }
    FORCE_INLINE void WriteLSTA1U(uint16 value) {
        WriteLSTAnU(2, value);
    }

    FORCE_INLINE uint16 ReadLSTAnL(uint8 bgIndex) const {
        return bit::extract<2, 16>(bgParams[bgIndex].lineScrollTableAddress) << 1u;
    }
    FORCE_INLINE uint16 ReadLSTA0L() const {
        return ReadLSTAnL(1);
    }
    FORCE_INLINE uint16 ReadLSTA1L() const {
        return ReadLSTAnL(2);
    }

    FORCE_INLINE void WriteLSTAnL(uint8 bgIndex, uint16 value) {
        bit::deposit_into<2, 16>(bgParams[bgIndex].lineScrollTableAddress, bit::extract<1, 15>(value));
    }
    FORCE_INLINE void WriteLSTA0L(uint16 value) {
        WriteLSTAnL(1, value);
    }
    FORCE_INLINE void WriteLSTA1L(uint16 value) {
        WriteLSTAnL(2, value);
    }

    // 1800A8   LCTAU   Line Color Screen Table Address (upper)
    //
    //   bits   r/w  code          description
    //     15     W  LCCLMD        Line Color Screen Mode (0=single color, 1=per line)
    //   14-3        -             Reserved, must be zero
    //    2-0     W  LCTA18-16     Line Color Screen Table Base Address (bits 18-16)
    //
    // 1800AA   LCTAL   Line Color Screen Table Address (lower)
    //
    //   bits   r/w  code          description
    //   15-0     W  LCTA15-0      Line Color Screen Table Base Address (bits 15-0)

    FORCE_INLINE uint16 ReadLCTAU() const {
        uint16 value = 0;
        bit::deposit_into<0, 2>(value, bit::extract<17, 19>(lineScreenParams.baseAddress));
        bit::deposit_into<15>(value, lineScreenParams.perLine);
        return value;
    }

    FORCE_INLINE void WriteLCTAU(uint16 value) {
        bit::deposit_into<17, 19>(lineScreenParams.baseAddress, bit::extract<0, 2>(value));
        lineScreenParams.perLine = bit::test<15>(value);
    }

    FORCE_INLINE uint16 ReadLCTAL() const {
        return bit::extract<1, 16>(lineScreenParams.baseAddress);
    }

    FORCE_INLINE void WriteLCTAL(uint16 value) {
        bit::deposit_into<1, 16>(lineScreenParams.baseAddress, value);
    }

    // 1800AC   BKTAU   Back Screen Table Address (upper)
    //
    //   bits   r/w  code          description
    //     15     W  BKCLMD        Back Screen Color Mode (0=single color, 1=per line)
    //   14-3        -             Reserved, must be zero
    //    2-0     W  BKTA18-16     Back Screen Table Base Address (bits 18-16)
    //
    // 1800AE   BKTAL   Back Screen Table Address (lower)
    //
    //   bits   r/w  code          description
    //   15-0     W  BKTA15-0      Back Screen Table Base Address (bits 15-0)

    FORCE_INLINE uint16 ReadBKTAU() const {
        uint16 value = 0;
        bit::deposit_into<0, 2>(value, bit::extract<17, 19>(backScreenParams.baseAddress));
        bit::deposit_into<15>(value, backScreenParams.perLine);
        return value;
    }

    FORCE_INLINE void WriteBKTAU(uint16 value) {
        bit::deposit_into<17, 19>(backScreenParams.baseAddress, bit::extract<0, 2>(value));
        backScreenParams.perLine = bit::test<15>(value);
    }

    FORCE_INLINE uint16 ReadBKTAL() const {
        return bit::extract<1, 16>(backScreenParams.baseAddress);
    }

    FORCE_INLINE void WriteBKTAL(uint16 value) {
        bit::deposit_into<1, 16>(backScreenParams.baseAddress, value);
    }

    // 1800B0   RPMD    Rotation Parameter Mode
    //
    //   bits   r/w  code          description
    //   15-2        -             Reserved, must be zero
    //    1-0     W  RPMD1-0       Rotation Parameter Mode
    //                               00 (0) = Rotation Parameter A
    //                               01 (1) = Rotation Parameter B
    //                               10 (2) = Screens switched via coeff. data from RPA table
    //                               11 (3) = Screens switched via rotation parameter window

    FORCE_INLINE uint16 ReadRPMD() const {
        return static_cast<uint16>(commonRotParams.rotParamMode);
    }

    FORCE_INLINE void WriteRPMD(uint16 value) {
        commonRotParams.rotParamMode = static_cast<RotationParamMode>(bit::extract<0, 1>(value));
    }

    // 1800B2   RPRCTL  Rotation Parameter Read Control
    //
    //   bits   r/w  code          description
    //  15-11        -             Reserved, must be zero
    //     10     W  RBKASTRE      Enable for KAst of Rotation Parameter B
    //      9     W  RBYSTRE       Enable for Yst of Rotation Parameter B
    //      8     W  RBXSTRE       Enable for Xst of Rotation Parameter B
    //    7-3        -             Reserved, must be zero
    //      2     W  RAKASTRE      Enable for KAst of Rotation Parameter A
    //      1     W  RAYSTRE       Enable for Yst of Rotation Parameter A
    //      0     W  RAXSTRE       Enable for Xst of Rotation Parameter A

    FORCE_INLINE uint16 ReadRPRCTL() const {
        uint16 value = 0;
        bit::deposit_into<0>(value, rotParams[0].readXst);
        bit::deposit_into<1>(value, rotParams[0].readYst);
        bit::deposit_into<2>(value, rotParams[0].readKAst);

        bit::deposit_into<8>(value, rotParams[1].readXst);
        bit::deposit_into<9>(value, rotParams[1].readYst);
        bit::deposit_into<10>(value, rotParams[1].readKAst);
        return value;
    }

    FORCE_INLINE void WriteRPRCTL(uint16 value) {
        rotParams[0].readXst = bit::test<0>(value);
        rotParams[0].readYst = bit::test<1>(value);
        rotParams[0].readKAst = bit::test<2>(value);

        rotParams[1].readXst = bit::test<8>(value);
        rotParams[1].readYst = bit::test<9>(value);
        rotParams[1].readKAst = bit::test<10>(value);
    }

    // 1800B4   KTCTL   Coefficient Table Control
    //
    //   bits   r/w  code          description
    //  15-13        -             Reserved, must be zero
    //     12     W  RBKLCE        Use line color screen data from Rotation Parameter B coeff. data
    //  11-10     W  RBKMD1-0      Coefficient Mode for Rotation Parameter B
    //                               00 (0) = Use as scale coefficient kx, ky
    //                               01 (1) = Use as scale coefficient kx
    //                               10 (2) = Use as scale coefficient ky
    //                               11 (3) = Use as viewpoint Xp after rotation conversion
    //      9     W  RBKDBS        Coefficient Data Size for Rotation Parameter B
    //                               0 = 2 words
    //                               1 = 1 word
    //      8     W  RBKTE         Coefficient Table Enable for Rotation Parameter B
    //    7-5        -             Reserved, must be zero
    //      4     W  RAKLCE        Use line color screen data from Rotation Parameter A coeff. data
    //    3-2     W  RAKMD1-0      Coefficient Mode for Rotation Parameter A
    //                               00 (0) = Use as scale coefficient kx, ky
    //                               01 (1) = Use as scale coefficient kx
    //                               10 (2) = Use as scale coefficient ky
    //                               11 (3) = Use as viewpoint Xp after rotation conversion
    //      1     W  RAKDBS        Coefficient Data Size for Rotation Parameter A
    //                               0 = 2 words
    //                               1 = 1 word
    //      0     W  RAKTE         Coefficient Table Enable for Rotation Parameter A

    FORCE_INLINE uint16 ReadKTCTL() const {
        uint16 value = 0;
        bit::deposit_into<0>(value, rotParams[0].coeffTableEnable);
        bit::deposit_into<1>(value, rotParams[0].coeffDataSize);
        bit::deposit_into<2, 3>(value, static_cast<uint8>(rotParams[0].coeffDataMode));
        bit::deposit_into<4>(value, rotParams[0].coeffUseLineColorData);

        bit::deposit_into<8>(value, rotParams[1].coeffTableEnable);
        bit::deposit_into<9>(value, rotParams[1].coeffDataSize);
        bit::deposit_into<10, 11>(value, static_cast<uint8>(rotParams[1].coeffDataMode));
        bit::deposit_into<12>(value, rotParams[1].coeffUseLineColorData);
        return value;
    }

    FORCE_INLINE void WriteKTCTL(uint16 value) {
        rotParams[0].coeffTableEnable = bit::test<0>(value);
        rotParams[0].coeffDataSize = bit::extract<1>(value);
        rotParams[0].coeffDataMode = static_cast<CoefficientDataMode>(bit::extract<2, 3>(value));
        rotParams[0].coeffUseLineColorData = bit::test<4>(value);

        rotParams[1].coeffTableEnable = bit::test<8>(value);
        rotParams[1].coeffDataSize = bit::extract<9>(value);
        rotParams[1].coeffDataMode = static_cast<CoefficientDataMode>(bit::extract<10, 11>(value));
        rotParams[1].coeffUseLineColorData = bit::test<12>(value);
    }

    // 1800B6   KTAOF   Coefficient Table Address Offset
    //
    //   bits   r/w  code          description
    //  15-11        -             Reserved, must be zero
    //   10-8     W  RBKTAOS2-0    Coefficient Table Address Offset for Rotation Parameter B
    //    7-3        -             Reserved, must be zero
    //    2-0     W  RAKTAOS2-0    Coefficient Table Address Offset for Rotation Parameter A

    FORCE_INLINE uint16 ReadKTAOF() const {
        uint16 value = 0;
        bit::deposit_into<0, 2>(value, bit::extract<26, 28>(rotParams[0].coeffTableAddressOffset));
        bit::deposit_into<8, 10>(value, bit::extract<26, 28>(rotParams[1].coeffTableAddressOffset));
        return value;
    }

    FORCE_INLINE void WriteKTAOF(uint16 value) {
        bit::deposit_into<26, 28>(rotParams[0].coeffTableAddressOffset, bit::extract<0, 2>(value));
        bit::deposit_into<26, 28>(rotParams[1].coeffTableAddressOffset, bit::extract<8, 10>(value));
    }

    // 1800B8   OVPNRA  Rotation Parameter A Screen-Over Pattern Name
    // 1800BA   OVPNRB  Rotation Parameter B Screen-Over Pattern Name
    //
    //   bits   r/w  code          description
    //   15-0     W  RxOPN15-0     Over Pattern Name
    //
    // x:
    //   A = Rotation Parameter A (OVPNRA)
    //   B = Rotation Parameter B (OVPNRB)

    FORCE_INLINE uint16 ReadOVPNRn(uint8 index) const {
        return rotParams[index].screenOverPatternName;
    }
    FORCE_INLINE uint16 ReadOVPNRA() const {
        return ReadOVPNRn(0);
    }
    FORCE_INLINE uint16 ReadOVPNRB() const {
        return ReadOVPNRn(1);
    }

    FORCE_INLINE void WriteOVPNRn(uint8 index, uint16 value) {
        rotParams[index].screenOverPatternName = value;
    }
    FORCE_INLINE void WriteOVPNRA(uint16 value) {
        WriteOVPNRn(0, value);
    }
    FORCE_INLINE void WriteOVPNRB(uint16 value) {
        WriteOVPNRn(1, value);
    }

    // 1800BC   RPTAU   Rotation Parameters Table Address (upper)
    //
    //   bits   r/w  code          description
    //   15-3        -             Reserved, must be zero
    //    2-0     W  RPTA18-16     Rotation Parameters Table Base Address (bits 18-16)
    //
    // 1800BE   RPTAL   Rotation Parameters Table Address (lower)
    //
    //   bits   r/w  code          description
    //   15-1     W  RPTA15-1      Rotation Parameters Table Base Address (bits 15-1)
    //      0        -             Reserved, must be zero

    FORCE_INLINE uint16 ReadRPTAU() const {
        return bit::extract<17, 19>(commonRotParams.baseAddress);
    }

    FORCE_INLINE void WriteRPTAU(uint16 value) {
        bit::deposit_into<17, 19>(commonRotParams.baseAddress, bit::extract<0, 2>(value));
    }

    FORCE_INLINE uint16 ReadRPTAL() const {
        return bit::extract<2, 16>(commonRotParams.baseAddress) << 1u;
    }

    FORCE_INLINE void WriteRPTAL(uint16 value) {
        bit::deposit_into<2, 16>(commonRotParams.baseAddress, bit::extract<1, 15>(value));
    }

    // 1800C0   WPSX0   Window 0 Horizontal Start Point
    // 1800C4   WPEX0   Window 0 Horizontal End Point
    // 1800C8   WPSX1   Window 1 Horizontal Start Point
    // 1800CC   WPEX1   Window 1 Horizontal End Point
    //
    //   bits   r/w  code          description
    //  15-10        -             Reserved, must be zero
    //    9-0     W  WxSX9-0       Window x Start/End Horizontal Coordinate
    //
    // Valid coordinate bits vary depending on the screen mode:
    //   Normal: bits 8-0 shifted left by 1; bit 0 is invalid
    //   Hi-Res: bits 9-0
    //   Excl. Normal: bits 8-0; bit 9 is invalid
    //   Excl. Hi-Res: bits 9-1 shifted right by 1; bit 9 is invalid
    //
    // 1800C2   WPSY0   Window 0 Vertical Start Point
    // 1800C6   WPEY0   Window 0 Vertical End Point
    // 1800CA   WPSY1   Window 1 Vertical Start Point
    // 1800CE   WPEY1   Window 1 Vertical End Point
    //
    //   bits   r/w  code          description
    //   15-9        -             Reserved, must be zero
    //    8-0     W  WxSY8-0       Window x Start/End Vertical Coordinate
    //
    // Double-density interlace mode uses bits 7-0 shifted left by 1; bit 0 is invalid.
    // All other modes use bits 8-0 unmodified.

    FORCE_INLINE uint16 ReadWPSXn(uint8 index) const {
        return windowParams[index].startX;
    }
    FORCE_INLINE uint16 ReadWPSX0() const {
        return ReadWPSXn(0);
    }
    FORCE_INLINE uint16 ReadWPSX1() const {
        return ReadWPSXn(1);
    }

    FORCE_INLINE void WriteWPSXn(uint8 index, uint16 value) {
        windowParams[index].startX = value;
    }
    FORCE_INLINE void WriteWPSX0(uint16 value) {
        WriteWPSXn(0, value);
    }
    FORCE_INLINE void WriteWPSX1(uint16 value) {
        WriteWPSXn(1, value);
    }

    FORCE_INLINE uint16 ReadWPEXn(uint8 index) const {
        return windowParams[index].endX;
    }
    FORCE_INLINE uint16 ReadWPEX0() const {
        return ReadWPEXn(0);
    }
    FORCE_INLINE uint16 ReadWPEX1() const {
        return ReadWPEXn(1);
    }

    FORCE_INLINE void WriteWPEXn(uint8 index, uint16 value) {
        windowParams[index].endX = value;
    }
    FORCE_INLINE void WriteWPEX0(uint16 value) {
        WriteWPEXn(0, value);
    }
    FORCE_INLINE void WriteWPEX1(uint16 value) {
        WriteWPEXn(1, value);
    }

    FORCE_INLINE uint16 ReadWPSYn(uint8 index) const {
        return windowParams[index].startY;
    }
    FORCE_INLINE uint16 ReadWPSY0() const {
        return ReadWPSYn(0);
    }
    FORCE_INLINE uint16 ReadWPSY1() const {
        return ReadWPSYn(1);
    }

    FORCE_INLINE void WriteWPSYn(uint8 index, uint16 value) {
        windowParams[index].startY = value;
    }
    FORCE_INLINE void WriteWPSY0(uint16 value) {
        WriteWPSYn(0, value);
    }
    FORCE_INLINE void WriteWPSY1(uint16 value) {
        WriteWPSYn(1, value);
    }

    FORCE_INLINE uint16 ReadWPEYn(uint8 index) const {
        return windowParams[index].endY;
    }
    FORCE_INLINE uint16 ReadWPEY0() const {
        return ReadWPEYn(0);
    }
    FORCE_INLINE uint16 ReadWPEY1() const {
        return ReadWPEYn(1);
    }

    FORCE_INLINE void WriteWPEYn(uint8 index, uint16 value) {
        windowParams[index].endY = value;
    }
    FORCE_INLINE void WriteWPEY0(uint16 value) {
        WriteWPEYn(0, value);
    }
    FORCE_INLINE void WriteWPEY1(uint16 value) {
        WriteWPEYn(1, value);
    }

    // 1800D0   WCTLA   NBG0 and NBG1 Window Control
    //
    //   bits   r/w  code          description
    //     15     W  N1LOG         NBG1/EXBG Window Logic (0=OR, 1=AND)
    //     14        -             Reserved, must be zero
    //     13     W  N1SWE         NBG1/EXBG Sprite Window Enable (0=disable, 1=enable)
    //     12     W  N1SWA         NBG1/EXBG Sprite Window Area (0=inside, 1=outside)
    //     11     W  N1W1E         NBG1/EXBG Window 1 Enable (0=disable, 1=enable)
    //     10     W  N1W1A         NBG1/EXBG Window 1 Area (0=inside, 1=outside)
    //      9     W  N1W0E         NBG1/EXBG Window 0 Enable (0=disable, 1=enable)
    //      8     W  N1W0A         NBG1/EXBG Window 0 Area (0=inside, 1=outside)
    //      7     W  N0LOG         NBG0/RBG1 Window Logic (0=OR, 1=AND)
    //      6        -             Reserved, must be zero
    //      5     W  N0SWE         NBG0/RBG1 Sprite Window Enable (0=disable, 1=enable)
    //      4     W  N0SWA         NBG0/RBG1 Sprite Window Area (0=inside, 1=outside)
    //      3     W  N0W1E         NBG0/RBG1 Window 1 Enable (0=disable, 1=enable)
    //      2     W  N0W1A         NBG0/RBG1 Window 1 Area (0=inside, 1=outside)
    //      1     W  N0W0E         NBG0/RBG1 Window 0 Enable (0=disable, 1=enable)
    //      0     W  N0W0A         NBG0/RBG1 Window 0 Area (0=inside, 1=outside)

    FORCE_INLINE uint16 ReadWCTLA() const {
        uint16 value = 0;
        bit::deposit_into<0>(value, bgParams[1].windowSet.inverted[0]);
        bit::deposit_into<1>(value, bgParams[1].windowSet.enabled[0]);
        bit::deposit_into<2>(value, bgParams[1].windowSet.inverted[1]);
        bit::deposit_into<3>(value, bgParams[1].windowSet.enabled[1]);
        bit::deposit_into<4>(value, bgParams[1].windowSet.inverted[2]);
        bit::deposit_into<5>(value, bgParams[1].windowSet.enabled[2]);
        bit::deposit_into<7>(value, static_cast<uint16>(bgParams[1].windowSet.logic));

        bit::deposit_into<8>(value, bgParams[2].windowSet.inverted[0]);
        bit::deposit_into<9>(value, bgParams[2].windowSet.enabled[0]);
        bit::deposit_into<10>(value, bgParams[2].windowSet.inverted[1]);
        bit::deposit_into<11>(value, bgParams[2].windowSet.enabled[1]);
        bit::deposit_into<12>(value, bgParams[2].windowSet.inverted[2]);
        bit::deposit_into<13>(value, bgParams[2].windowSet.enabled[2]);
        bit::deposit_into<15>(value, static_cast<uint16>(bgParams[2].windowSet.logic));
        return value;
    }

    FORCE_INLINE void WriteWCTLA(uint16 value) {
        bgParams[1].windowSet.inverted[0] = bit::test<0>(value);
        bgParams[1].windowSet.enabled[0] = bit::test<1>(value);
        bgParams[1].windowSet.inverted[1] = bit::test<2>(value);
        bgParams[1].windowSet.enabled[1] = bit::test<3>(value);
        bgParams[1].windowSet.inverted[2] = bit::test<4>(value);
        bgParams[1].windowSet.enabled[2] = bit::test<5>(value);
        bgParams[1].windowSet.logic = static_cast<WindowLogic>(bit::extract<7>(value));

        bgParams[2].windowSet.inverted[0] = bit::test<8>(value);
        bgParams[2].windowSet.enabled[0] = bit::test<9>(value);
        bgParams[2].windowSet.inverted[1] = bit::test<10>(value);
        bgParams[2].windowSet.enabled[1] = bit::test<11>(value);
        bgParams[2].windowSet.inverted[2] = bit::test<12>(value);
        bgParams[2].windowSet.enabled[2] = bit::test<13>(value);
        bgParams[2].windowSet.logic = static_cast<WindowLogic>(bit::extract<15>(value));
    }

    // 1800D2   WCTLB   NBG2 and NBG3 Window Control
    //
    //   bits   r/w  code          description
    //     15     W  N3LOG         NBG3 Window Logic (0=OR, 1=AND)
    //     14        -             Reserved, must be zero
    //     13     W  N3SWE         NBG3 Sprite Window Enable (0=disable, 1=enable)
    //     12     W  N3SWA         NBG3 Sprite Window Area (0=inside, 1=outside)
    //     11     W  N3W1E         NBG3 Window 1 Enable (0=disable, 1=enable)
    //     10     W  N3W1A         NBG3 Window 1 Area (0=inside, 1=outside)
    //      9     W  N3W0E         NBG3 Window 0 Enable (0=disable, 1=enable)
    //      8     W  N3W0A         NBG3 Window 0 Area (0=inside, 1=outside)
    //      7     W  N2LOG         NBG2 Window Logic (0=OR, 1=AND)
    //      6        -             Reserved, must be zero
    //      5     W  N2SWE         NBG2 Sprite Window Enable (0=disable, 1=enable)
    //      4     W  N2SWA         NBG2 Sprite Window Area (0=inside, 1=outside)
    //      3     W  N2W1E         NBG2 Window 1 Enable (0=disable, 1=enable)
    //      2     W  N2W1A         NBG2 Window 1 Area (0=inside, 1=outside)
    //      1     W  N2W0E         NBG2 Window 0 Enable (0=disable, 1=enable)
    //      0     W  N2W0A         NBG2 Window 0 Area (0=inside, 1=outside)

    FORCE_INLINE uint16 ReadWCTLB() const {
        uint16 value = 0;
        bit::deposit_into<0>(value, bgParams[3].windowSet.inverted[0]);
        bit::deposit_into<1>(value, bgParams[3].windowSet.enabled[0]);
        bit::deposit_into<2>(value, bgParams[3].windowSet.inverted[1]);
        bit::deposit_into<3>(value, bgParams[3].windowSet.enabled[1]);
        bit::deposit_into<4>(value, bgParams[3].windowSet.inverted[2]);
        bit::deposit_into<5>(value, bgParams[3].windowSet.enabled[2]);
        bit::deposit_into<7>(value, static_cast<uint16>(bgParams[3].windowSet.logic));

        bit::deposit_into<8>(value, bgParams[4].windowSet.inverted[0]);
        bit::deposit_into<9>(value, bgParams[4].windowSet.enabled[0]);
        bit::deposit_into<10>(value, bgParams[4].windowSet.inverted[1]);
        bit::deposit_into<11>(value, bgParams[4].windowSet.enabled[1]);
        bit::deposit_into<12>(value, bgParams[4].windowSet.inverted[2]);
        bit::deposit_into<13>(value, bgParams[4].windowSet.enabled[2]);
        bit::deposit_into<15>(value, static_cast<uint16>(bgParams[4].windowSet.logic));
        return value;
    }

    FORCE_INLINE void WriteWCTLB(uint16 value) {
        bgParams[3].windowSet.inverted[0] = bit::test<0>(value);
        bgParams[3].windowSet.enabled[0] = bit::test<1>(value);
        bgParams[3].windowSet.inverted[1] = bit::test<2>(value);
        bgParams[3].windowSet.enabled[1] = bit::test<3>(value);
        bgParams[3].windowSet.inverted[2] = bit::test<4>(value);
        bgParams[3].windowSet.enabled[2] = bit::test<5>(value);
        bgParams[3].windowSet.logic = static_cast<WindowLogic>(bit::extract<7>(value));

        bgParams[4].windowSet.inverted[0] = bit::test<8>(value);
        bgParams[4].windowSet.enabled[0] = bit::test<9>(value);
        bgParams[4].windowSet.inverted[1] = bit::test<10>(value);
        bgParams[4].windowSet.enabled[1] = bit::test<11>(value);
        bgParams[4].windowSet.inverted[2] = bit::test<12>(value);
        bgParams[4].windowSet.enabled[2] = bit::test<13>(value);
        bgParams[4].windowSet.logic = static_cast<WindowLogic>(bit::extract<15>(value));
    }

    // 1800D4   WCTLC   RBG0 and Sprite Window Control
    //
    //   bits   r/w  code          description
    //     15     W  SPLOG         Sprite Window Logic (0=OR, 1=AND)
    //     14        -             Reserved, must be zero
    //     13     W  SPSWE         Sprite Sprite Window Enable (0=disable, 1=enable)
    //     12     W  SPSWA         Sprite Sprite Window Area (0=inside, 1=outside)
    //     11     W  SPW1E         Sprite Window 1 Enable (0=disable, 1=enable)
    //     10     W  SPW1A         Sprite Window 1 Area (0=inside, 1=outside)
    //      9     W  SPW0E         Sprite Window 0 Enable (0=disable, 1=enable)
    //      8     W  SPW0A         Sprite Window 0 Area (0=inside, 1=outside)
    //      7     W  R0LOG         RBG0 Window Logic (0=OR, 1=AND)
    //      6        -             Reserved, must be zero
    //      5     W  R0SWE         RBG0 Sprite Window Enable (0=disable, 1=enable)
    //      4     W  R0SWA         RBG0 Sprite Window Area (0=inside, 1=outside)
    //      3     W  R0W1E         RBG0 Window 1 Enable (0=disable, 1=enable)
    //      2     W  R0W1A         RBG0 Window 1 Area (0=inside, 1=outside)
    //      1     W  R0W0E         RBG0 Window 0 Enable (0=disable, 1=enable)
    //      0     W  R0W0A         RBG0 Window 0 Area (0=inside, 1=outside)

    FORCE_INLINE uint16 ReadWCTLC() const {
        uint16 value = 0;
        bit::deposit_into<0>(value, bgParams[0].windowSet.inverted[0]);
        bit::deposit_into<1>(value, bgParams[0].windowSet.enabled[0]);
        bit::deposit_into<2>(value, bgParams[0].windowSet.inverted[1]);
        bit::deposit_into<3>(value, bgParams[0].windowSet.enabled[1]);
        bit::deposit_into<4>(value, bgParams[0].windowSet.inverted[2]);
        bit::deposit_into<5>(value, bgParams[0].windowSet.enabled[2]);
        bit::deposit_into<7>(value, static_cast<uint16>(bgParams[0].windowSet.logic));

        bit::deposit_into<8>(value, spriteParams.windowSet.inverted[0]);
        bit::deposit_into<9>(value, spriteParams.windowSet.enabled[0]);
        bit::deposit_into<10>(value, spriteParams.windowSet.inverted[1]);
        bit::deposit_into<11>(value, spriteParams.windowSet.enabled[1]);
        bit::deposit_into<12>(value, spriteParams.spriteWindowInverted);
        bit::deposit_into<13>(value, spriteParams.spriteWindowEnabled);
        bit::deposit_into<15>(value, static_cast<uint16>(spriteParams.windowSet.logic));
        return value;
    }

    FORCE_INLINE void WriteWCTLC(uint16 value) {
        bgParams[0].windowSet.inverted[0] = bit::test<0>(value);
        bgParams[0].windowSet.enabled[0] = bit::test<1>(value);
        bgParams[0].windowSet.inverted[1] = bit::test<2>(value);
        bgParams[0].windowSet.enabled[1] = bit::test<3>(value);
        bgParams[0].windowSet.inverted[2] = bit::test<4>(value);
        bgParams[0].windowSet.enabled[2] = bit::test<5>(value);
        bgParams[0].windowSet.logic = static_cast<WindowLogic>(bit::extract<7>(value));

        spriteParams.windowSet.inverted[0] = bit::test<8>(value);
        spriteParams.windowSet.enabled[0] = bit::test<9>(value);
        spriteParams.windowSet.inverted[1] = bit::test<10>(value);
        spriteParams.windowSet.enabled[1] = bit::test<11>(value);
        spriteParams.spriteWindowInverted = bit::test<12>(value);
        spriteParams.spriteWindowEnabled = bit::test<13>(value);
        spriteParams.windowSet.logic = static_cast<WindowLogic>(bit::extract<15>(value));
    }

    // 1800D6   WCTLD   Rotation Window and Color Calculation Window Control
    //
    //   bits   r/w  code          description
    //     15     W  CCLOG         Color Calculation Window Logic (0=OR, 1=AND)
    //     14        -             Reserved, must be zero
    //     13     W  CCSWE         Color Calculation Window Sprite Window Enable (0=disable, 1=enable)
    //     12     W  CCSWA         Color Calculation Window Sprite Window Area (0=inside, 1=outside)
    //     11     W  CCW1E         Color Calculation Window Window 1 Enable (0=disable, 1=enable)
    //     10     W  CCW1A         Color Calculation Window Window 1 Area (0=inside, 1=outside)
    //      9     W  CCW0E         Color Calculation Window Window 0 Enable (0=disable, 1=enable)
    //      8     W  CCW0A         Color Calculation Window Window 0 Area (0=inside, 1=outside)
    //      7     W  RPLOG         Rotation Parameter Window Logic (0=OR, 1=AND)
    //    6-4        -             Reserved, must be zero
    //      3     W  RPW1E         Rotation Parameter Window 1 Enable (0=disable, 1=enable)
    //      2     W  RPW1A         Rotation Parameter Window 1 Area (0=inside, 1=outside)
    //      1     W  RPW0E         Rotation Parameter Window 0 Enable (0=disable, 1=enable)
    //      0     W  RPW0A         Rotation Parameter Window 0 Area (0=inside, 1=outside)

    FORCE_INLINE uint16 ReadWCTLD() const {
        uint16 value = 0;
        bit::deposit_into<0>(value, commonRotParams.windowSet.inverted[0]);
        bit::deposit_into<1>(value, commonRotParams.windowSet.enabled[0]);
        bit::deposit_into<2>(value, commonRotParams.windowSet.inverted[1]);
        bit::deposit_into<3>(value, commonRotParams.windowSet.enabled[1]);
        bit::deposit_into<7>(value, static_cast<uint16>(commonRotParams.windowSet.logic));

        bit::deposit_into<8>(value, colorCalcParams.windowSet.inverted[0]);
        bit::deposit_into<9>(value, colorCalcParams.windowSet.enabled[0]);
        bit::deposit_into<10>(value, colorCalcParams.windowSet.inverted[1]);
        bit::deposit_into<11>(value, colorCalcParams.windowSet.enabled[1]);
        bit::deposit_into<12>(value, colorCalcParams.windowSet.inverted[2]);
        bit::deposit_into<13>(value, colorCalcParams.windowSet.enabled[2]);
        bit::deposit_into<15>(value, static_cast<uint16>(colorCalcParams.windowSet.logic));
        return value;
    }

    FORCE_INLINE void WriteWCTLD(uint16 value) {
        commonRotParams.windowSet.inverted[0] = bit::test<0>(value);
        commonRotParams.windowSet.enabled[0] = bit::test<1>(value);
        commonRotParams.windowSet.inverted[1] = bit::test<2>(value);
        commonRotParams.windowSet.enabled[1] = bit::test<3>(value);
        commonRotParams.windowSet.logic = static_cast<WindowLogic>(bit::extract<7>(value));

        colorCalcParams.windowSet.inverted[0] = bit::test<8>(value);
        colorCalcParams.windowSet.enabled[0] = bit::test<9>(value);
        colorCalcParams.windowSet.inverted[1] = bit::test<10>(value);
        colorCalcParams.windowSet.enabled[1] = bit::test<11>(value);
        colorCalcParams.windowSet.inverted[2] = bit::test<12>(value);
        colorCalcParams.windowSet.enabled[2] = bit::test<13>(value);
        colorCalcParams.windowSet.logic = static_cast<WindowLogic>(bit::extract<15>(value));
    }

    // 1800D8   LWTA0U  Window 0 Line Window Table Address (upper)
    // 1800DC   LWTA1U  Window 1 Line Window Table Address (upper)
    //
    //   bits   r/w  code          description
    //     15     W  WxLWE         Line Window Enable (0=disabled, 1=enabled)
    //   14-3        -             Reserved, must be zero
    //    2-0     W  WxLWTA18-16   Line Window Table Address (bits 18-16)
    //
    // 1800DA   LWTA0L  Window 0 Line Window Table Address (lower)
    // 1800DE   LWTA1L  Window 1 Line Window Table Address (lower)
    //
    //   bits   r/w  code          description
    //   15-1     W  WxLWTA15-1    Line Window Table Address (bits 15-1)
    //      0        -             Reserved, must be zero

    FORCE_INLINE uint16 ReadLWTAnU(uint8 index) const {
        uint16 value = 0;
        bit::deposit_into<0, 2>(value, bit::extract<17, 19>(windowParams[index].lineWindowTableAddress));
        bit::deposit_into<15>(value, windowParams[index].lineWindowTableEnable);
        return value;
    }
    FORCE_INLINE uint16 ReadLWTA0U() const {
        return ReadLWTAnU(0);
    }
    FORCE_INLINE uint16 ReadLWTA1U() const {
        return ReadLWTAnU(1);
    }

    FORCE_INLINE void WriteLWTAnU(uint8 index, uint16 value) {
        bit::deposit_into<17, 19>(windowParams[index].lineWindowTableAddress, bit::extract<0, 2>(value));
        windowParams[index].lineWindowTableEnable = bit::test<15>(value);
    }
    FORCE_INLINE void WriteLWTA0U(uint16 value) {
        WriteLWTAnU(0, value);
    }
    FORCE_INLINE void WriteLWTA1U(uint16 value) {
        WriteLWTAnU(1, value);
    }

    FORCE_INLINE uint16 ReadLWTAnL(uint8 index) const {
        uint16 value = 0;
        bit::deposit_into<1, 15>(value, bit::extract<2, 16>(windowParams[index].lineWindowTableAddress));
        return value;
    }
    FORCE_INLINE uint16 ReadLWTA0L() const {
        return ReadLWTAnL(0);
    }
    FORCE_INLINE uint16 ReadLWTA1L() const {
        return ReadLWTAnL(1);
    }

    FORCE_INLINE void WriteLWTAnL(uint8 index, uint16 value) {
        bit::deposit_into<2, 16>(windowParams[index].lineWindowTableAddress, bit::extract<1, 15>(value));
    }
    FORCE_INLINE void WriteLWTA0L(uint16 value) {
        WriteLWTAnL(0, value);
    }
    FORCE_INLINE void WriteLWTA1L(uint16 value) {
        WriteLWTAnL(1, value);
    }

    // 1800E0   SPCTL   Sprite Control
    //
    //   bits   r/w  code          description
    //  15-14        -             Reserved, must be zero
    //  13-12     W  SPCCCS1-0     Sprite Color Calculation Condition
    //                               00 (0) = Priority Number <= Color Calculation Number
    //                               01 (1) = Priority Number == Color Calculation Number
    //                               10 (2) = Priority Number >= Color Calculation Number
    //                               11 (3) = Color Data MSB == 1
    //     11        -             Reserved, must be zero
    //   10-8     W  SPCCN2-0      Color Calculation Number
    //    7-6        -             Reserved, must be zero
    //      5     W  SPCLMD        Sprite Color Format Data (0=palette only, 1=palette and RGB)
    //      4     W  SPWINEN       Sprite Window Enable (0=disable, 1=enable)
    //    3-0     W  SPTYPE3-0     Sprite Type (0,1,2,...,D,E,F)

    FORCE_INLINE uint16 ReadSPCTL() const {
        uint16 value = 0;
        bit::deposit_into<0, 3>(value, spriteParams.type);
        bit::deposit_into<4>(value, spriteParams.useSpriteWindow);
        bit::deposit_into<5>(value, spriteParams.mixedFormat);
        bit::deposit_into<8, 10>(value, spriteParams.colorCalcValue);
        bit::deposit_into<12, 13>(value, static_cast<uint16>(spriteParams.colorCalcCond));
        return value;
    }

    FORCE_INLINE void WriteSPCTL(uint16 value) {
        spriteParams.type = bit::extract<0, 3>(value);
        spriteParams.useSpriteWindow = bit::test<4>(value);
        spriteParams.mixedFormat = bit::test<5>(value);
        spriteParams.colorCalcValue = bit::extract<8, 10>(value);
        spriteParams.colorCalcCond = static_cast<SpriteColorCalculationCondition>(bit::extract<12, 13>(value));
    }

    // 1800E2   SDCTL   Shadow Control
    //
    //   bits   r/w  code          description
    //   15-9        -             Reserved, must be zero
    //      8     W  TPSDSL        Transparent Shadow (0=disable, 1=enable)
    //    7-6        -             Reserved, must be zero
    //      5     W  BKSDEN        Back Screen Shadow Enable
    //      4     W  R0SDEN        RBG0 Shadow Enable
    //      3     W  N3SDEN        NBG3 Shadow Enable
    //      2     W  N2SDEN        NBG2 Shadow Enable
    //      1     W  N1SDEN        NBG1/EXBG Shadow Enable
    //      0     W  N0SDEN        NBG0/RBG1 Shadow Enable

    FORCE_INLINE uint16 ReadSDCTL() const {
        uint16 value = 0;
        bit::deposit_into<0>(value, bgParams[1].shadowEnable);
        bit::deposit_into<1>(value, bgParams[2].shadowEnable);
        bit::deposit_into<2>(value, bgParams[3].shadowEnable);
        bit::deposit_into<3>(value, bgParams[4].shadowEnable);
        bit::deposit_into<4>(value, bgParams[0].shadowEnable);
        bit::deposit_into<5>(value, backScreenParams.shadowEnable);
        bit::deposit_into<8>(value, transparentShadowEnable);
        return value;
    }

    FORCE_INLINE void WriteSDCTL(uint16 value) {
        bgParams[1].shadowEnable = bit::test<0>(value);
        bgParams[2].shadowEnable = bit::test<1>(value);
        bgParams[3].shadowEnable = bit::test<2>(value);
        bgParams[4].shadowEnable = bit::test<3>(value);
        bgParams[0].shadowEnable = bit::test<4>(value);
        backScreenParams.shadowEnable = bit::test<5>(value);
        transparentShadowEnable = bit::test<8>(value);
    }

    // 1800E4   CRAOFA  NBG0-NBG3 Color RAM Address Offset
    //
    //   bits   r/w  code          description
    //     15        -             Reserved, must be zero
    //  14-12     W  N3CAOS2-0     NBG3 Color RAM Address Offset
    //     11        -             Reserved, must be zero
    //   10-8     W  N2CAOS2-0     NBG2 Color RAM Address Offset
    //      7        -             Reserved, must be zero
    //    6-4     W  N1CAOS2-0     NBG1/EXBG Color RAM Address Offset
    //      3        -             Reserved, must be zero
    //    2-0     W  N0CAOS2-0     NBG0/RBG1 Color RAM Address Offset

    FORCE_INLINE uint16 ReadCRAOFA() const {
        uint16 value = 0;
        bit::deposit_into<0, 2>(value, bgParams[1].cramOffset >> 8u);
        bit::deposit_into<4, 6>(value, bgParams[2].cramOffset >> 8u);
        bit::deposit_into<8, 10>(value, bgParams[3].cramOffset >> 8u);
        bit::deposit_into<12, 14>(value, bgParams[4].cramOffset >> 8u);
        return value;
    }

    FORCE_INLINE void WriteCRAOFA(uint16 value) {
        bgParams[1].cramOffset = bit::extract<0, 2>(value) << 8u;
        bgParams[2].cramOffset = bit::extract<4, 6>(value) << 8u;
        bgParams[3].cramOffset = bit::extract<8, 10>(value) << 8u;
        bgParams[4].cramOffset = bit::extract<12, 14>(value) << 8u;
    }

    // 1800E6   CRAOFB  RBG0 and Sprite Color RAM Address Offset
    //
    //   bits   r/w  code          description
    //   15-7        -             Reserved, must be zero
    //    6-4     W  SPCAOS2-0     Sprite Color RAM Address Offset
    //      3        -             Reserved, must be zero
    //    2-0     W  R0CAOS2-0     RBG0 Color RAM Address Offset

    FORCE_INLINE uint16 ReadCRAOFB() const {
        uint16 value = 0;
        bit::deposit_into<0, 2>(value, bgParams[0].cramOffset >> 8u);
        bit::deposit_into<4, 6>(value, spriteParams.colorDataOffset >> 8u);
        return value;
    }

    FORCE_INLINE void WriteCRAOFB(uint16 value) {
        bgParams[0].cramOffset = bit::extract<0, 2>(value) << 8u;
        spriteParams.colorDataOffset = bit::extract<4, 6>(value) << 8u;
    }

    // 1800E8   LNCLEN  Line Color Screen Enable
    //
    //   bits   r/w  code          description
    //   15-6        -             Reserved, must be zero
    //      5     W  SPLCEN        Sprite Line Color Screen Enable
    //      4     W  R0LCEN        RBG0 Line Color Screen Enable
    //      3     W  N3LCEN        NBG3 Line Color Screen Enable
    //      2     W  N2LCEN        NBG2 Line Color Screen Enable
    //      1     W  N1LCEN        NBG1 Line Color Screen Enable
    //      0     W  N0LCEN        NBG0 Line Color Screen Enable

    FORCE_INLINE uint16 ReadLNCLEN() const {
        uint16 value = 0;
        bit::deposit_into<0>(value, bgParams[1].lineColorScreenEnable);
        bit::deposit_into<1>(value, bgParams[2].lineColorScreenEnable);
        bit::deposit_into<2>(value, bgParams[3].lineColorScreenEnable);
        bit::deposit_into<3>(value, bgParams[4].lineColorScreenEnable);
        bit::deposit_into<4>(value, bgParams[0].lineColorScreenEnable);
        bit::deposit_into<5>(value, spriteParams.lineColorScreenEnable);
        return value;
    }

    FORCE_INLINE void WriteLNCLEN(uint16 value) {
        bgParams[1].lineColorScreenEnable = bit::test<0>(value);
        bgParams[2].lineColorScreenEnable = bit::test<1>(value);
        bgParams[3].lineColorScreenEnable = bit::test<2>(value);
        bgParams[4].lineColorScreenEnable = bit::test<3>(value);
        bgParams[0].lineColorScreenEnable = bit::test<4>(value);
        spriteParams.lineColorScreenEnable = bit::test<5>(value);
    }

    // 1800EA   SFPRMD  Special Priority Mode
    //
    //   bits   r/w  code          description
    //  15-10        -             Reserved, must be zero
    //    9-8     W  R0SPRM1-0     RBG0 Special Priority Mode
    //    7-6     W  N3SPRM1-0     NBG3 Special Priority Mode
    //    5-4     W  N2SPRM1-0     NBG2 Special Priority Mode
    //    3-2     W  N1SPRM1-0     NBG1/EXBG Special Priority Mode
    //    1-0     W  N0SPRM1-0     NBG0/RBG1 Special Priority Mode
    //
    // For all parameters, use LSB of priority number:
    //   00 (0) = per screen
    //   01 (1) = per character
    //   10 (2) = per pixel
    //   11 (3) = (forbidden)

    FORCE_INLINE uint16 ReadSFPRMD() const {
        uint16 value = 0;
        bit::deposit_into<0, 1>(value, static_cast<uint32>(bgParams[1].priorityMode));
        bit::deposit_into<2, 3>(value, static_cast<uint32>(bgParams[2].priorityMode));
        bit::deposit_into<4, 5>(value, static_cast<uint32>(bgParams[3].priorityMode));
        bit::deposit_into<6, 7>(value, static_cast<uint32>(bgParams[4].priorityMode));
        bit::deposit_into<8, 9>(value, static_cast<uint32>(bgParams[0].priorityMode));
        return value;
    }

    FORCE_INLINE void WriteSFPRMD(uint16 value) {
        bgParams[1].priorityMode = static_cast<PriorityMode>(bit::extract<0, 1>(value));
        bgParams[2].priorityMode = static_cast<PriorityMode>(bit::extract<2, 3>(value));
        bgParams[3].priorityMode = static_cast<PriorityMode>(bit::extract<4, 5>(value));
        bgParams[4].priorityMode = static_cast<PriorityMode>(bit::extract<6, 7>(value));
        bgParams[0].priorityMode = static_cast<PriorityMode>(bit::extract<8, 9>(value));
    }

    // 1800EC   CCCTL   Color Calculation Control
    //
    //   bits   r/w  code          description
    //     15     W  BOKEN         Gradation Enable (0=disable, 1=enable)
    //  14-12     W  BOKN2-0       Gradation Screen Number
    //                               000 (0) = Sprite
    //                               001 (1) = RBG0
    //                               010 (2) = NBG0/RBG1
    //                               011 (3) = Invalid
    //                               100 (4) = NBG1/EXBG
    //                               101 (5) = NBG2
    //                               110 (6) = NBG3
    //                               111 (7) = Invalid
    //     11        -             Reserved, must be zero
    //     10     W  EXCCEN        Extended Color Calculation Enable (0=disable, 1=enable)
    //      9     W  CCRTMD        Color Calculation Ratio Mode (0=top screen, 1=second screen)
    //      8     W  CCMD          Color Calculation Mode (0=use color calculation register, 1=as is)
    //      7        -             Reserved, must be zero
    //      6     W  SPCCEN        Sprite Color Calculation Enable
    //      5     W  LCCCEN        Line Color Color Calculation Enable
    //      4     W  R0CCEN        RBG0 Color Calculation Enable
    //      3     W  N3CCEN        NBG3 Color Calculation Enable
    //      2     W  N2CCEN        NBG2 Color Calculation Enable
    //      1     W  N1CCEN        NBG1/EXBG Color Calculation Enable
    //      0     W  N0CCEN        NBG0/RBG1 Color Calculation Enable
    //
    // xxCCEN: 0=disable, 1=enable

    FORCE_INLINE uint16 ReadCCCTL() const {
        uint16 value = 0;
        bit::deposit_into<0>(value, bgParams[1].colorCalcEnable);
        bit::deposit_into<1>(value, bgParams[2].colorCalcEnable);
        bit::deposit_into<2>(value, bgParams[3].colorCalcEnable);
        bit::deposit_into<3>(value, bgParams[4].colorCalcEnable);
        bit::deposit_into<4>(value, bgParams[0].colorCalcEnable);
        bit::deposit_into<5>(value, lineScreenParams.colorCalcEnable);
        bit::deposit_into<6>(value, spriteParams.colorCalcEnable);

        bit::deposit_into<8>(value, colorCalcParams.useAdditiveBlend);
        bit::deposit_into<9>(value, colorCalcParams.useSecondScreenRatio);
        bit::deposit_into<10>(value, colorCalcParams.extendedColorCalcEnable);
        bit::deposit_into<12, 14>(value, static_cast<uint8>(colorCalcParams.colorGradScreen));
        bit::deposit_into<15>(value, colorCalcParams.colorGradEnable);
        return value;
    }

    FORCE_INLINE void WriteCCCTL(uint16 value) {
        bgParams[1].colorCalcEnable = bit::test<0>(value);
        bgParams[2].colorCalcEnable = bit::test<1>(value);
        bgParams[3].colorCalcEnable = bit::test<2>(value);
        bgParams[4].colorCalcEnable = bit::test<3>(value);
        bgParams[0].colorCalcEnable = bit::test<4>(value);
        lineScreenParams.colorCalcEnable = bit::test<5>(value);
        spriteParams.colorCalcEnable = bit::test<6>(value);

        colorCalcParams.useAdditiveBlend = bit::test<8>(value);
        colorCalcParams.useSecondScreenRatio = bit::test<9>(value);
        colorCalcParams.extendedColorCalcEnable = bit::test<10>(value);
        colorCalcParams.colorGradScreen = static_cast<ColorGradScreen>(bit::extract<12, 14>(value));
        colorCalcParams.colorGradEnable = bit::test<15>(value);
    }

    // 1800EE   SFCCMD  Special Color Calculation Mode
    //
    //   bits   r/w  code          description
    //  15-10        -             Reserved, must be zero
    //    9-8     W  R0SCCM1-0     RBG0 Special Color Calculation Mode
    //    7-6     W  N3SCCM1-0     NBG3 Special Color Calculation Mode
    //    5-4     W  N2SCCM1-0     NBG2 Special Color Calculation Mode
    //    3-2     W  N1SCCM1-0     NBG1 Special Color Calculation Mode
    //    1-0     W  N0SCCM1-0     NBG0 Special Color Calculation Mode

    FORCE_INLINE uint16 ReadSFCCMD() const {
        uint16 value = 0;
        bit::deposit_into<0, 1>(value, static_cast<uint8>(bgParams[1].specialColorCalcMode));
        bit::deposit_into<2, 3>(value, static_cast<uint8>(bgParams[2].specialColorCalcMode));
        bit::deposit_into<4, 5>(value, static_cast<uint8>(bgParams[3].specialColorCalcMode));
        bit::deposit_into<6, 7>(value, static_cast<uint8>(bgParams[4].specialColorCalcMode));
        bit::deposit_into<8, 9>(value, static_cast<uint8>(bgParams[0].specialColorCalcMode));
        return value;
    }

    FORCE_INLINE void WriteSFCCMD(uint16 value) {
        bgParams[1].specialColorCalcMode = static_cast<SpecialColorCalcMode>(bit::extract<0, 1>(value));
        bgParams[2].specialColorCalcMode = static_cast<SpecialColorCalcMode>(bit::extract<2, 3>(value));
        bgParams[3].specialColorCalcMode = static_cast<SpecialColorCalcMode>(bit::extract<4, 5>(value));
        bgParams[4].specialColorCalcMode = static_cast<SpecialColorCalcMode>(bit::extract<6, 7>(value));
        bgParams[0].specialColorCalcMode = static_cast<SpecialColorCalcMode>(bit::extract<8, 9>(value));
    }

    // 1800F0   PRISA   Sprite 0 and 1 Priority Number
    //
    //   bits   r/w  code          description
    //  15-11        -             Reserved, must be zero
    //   10-8     W  S1PRIN2-0     Sprite 1 Priority Number
    //    7-3        -             Reserved, must be zero
    //    2-0     W  S0PRIN2-0     Sprite 0 Priority Number
    //
    // 1800F2   PRISB   Sprite 2 and 3 Priority Number
    //
    //   bits   r/w  code          description
    //  15-11        -             Reserved, must be zero
    //   10-8     W  S3PRIN2-0     Sprite 3 Priority Number
    //    7-3        -             Reserved, must be zero
    //    2-0     W  S3PRIN2-0     Sprite 2 Priority Number
    //
    // 1800F4   PRISC   Sprite 4 and 5 Priority Number
    //
    //   bits   r/w  code          description
    //  15-11        -             Reserved, must be zero
    //   10-8     W  S5PRIN2-0     Sprite 5 Priority Number
    //    7-3        -             Reserved, must be zero
    //    2-0     W  S4PRIN2-0     Sprite 4 Priority Number
    //
    // 1800F6   PRISD   Sprite 6 and 7 Priority Number
    //
    //   bits   r/w  code          description
    //  15-11        -             Reserved, must be zero
    //   10-8     W  S7PRIN2-0     Sprite 7 Priority Number
    //    7-3        -             Reserved, must be zero
    //    2-0     W  S6PRIN2-0     Sprite 6 Priority Number

    FORCE_INLINE uint16 ReadPRISn(uint32 offset) const {
        uint16 value = 0;
        bit::deposit_into<0, 2>(value, spriteParams.priorities[offset * 2 + 0]);
        bit::deposit_into<8, 10>(value, spriteParams.priorities[offset * 2 + 1]);
        return value;
    }
    FORCE_INLINE uint16 ReadPRISA() const {
        return ReadPRISn(0);
    }
    FORCE_INLINE uint16 ReadPRISB() const {
        return ReadPRISn(1);
    }
    FORCE_INLINE uint16 ReadPRISC() const {
        return ReadPRISn(2);
    }
    FORCE_INLINE uint16 ReadPRISD() const {
        return ReadPRISn(3);
    }

    FORCE_INLINE void WritePRISn(uint32 offset, uint16 value) {
        spriteParams.priorities[offset * 2 + 0] = bit::extract<0, 2>(value);
        spriteParams.priorities[offset * 2 + 1] = bit::extract<8, 10>(value);
    }
    FORCE_INLINE void WritePRISA(uint16 value) {
        WritePRISn(0, value);
    }
    FORCE_INLINE void WritePRISB(uint16 value) {
        WritePRISn(1, value);
    }
    FORCE_INLINE void WritePRISC(uint16 value) {
        WritePRISn(2, value);
    }
    FORCE_INLINE void WritePRISD(uint16 value) {
        WritePRISn(3, value);
    }

    // 1800F8   PRINA   NBG0 and NBG1 Priority Number
    //
    //   bits   r/w  code          description
    //  15-11        -             Reserved, must be zero
    //   10-8     W  N1PRIN2-0     NBG1 Priority Number
    //    7-3        -             Reserved, must be zero
    //    2-0     W  N0PRIN2-0     NBG0/RBG1 Priority Number

    FORCE_INLINE uint16 ReadPRINA() const {
        uint16 value = 0;
        bit::deposit_into<0, 2>(value, bgParams[1].priorityNumber);
        bit::deposit_into<8, 10>(value, bgParams[2].priorityNumber);
        return value;
    }

    FORCE_INLINE void WritePRINA(uint16 value) {
        bgParams[1].priorityNumber = bit::extract<0, 2>(value);
        bgParams[2].priorityNumber = bit::extract<8, 10>(value);
    }

    // 1800FA   PRINB   NBG2 and NBG3 Priority Number
    //
    //   bits   r/w  code          description
    //  15-11        -             Reserved, must be zero
    //   10-8     W  N3PRIN2-0     NBG3 Priority Number
    //    7-3        -             Reserved, must be zero
    //    2-0     W  N2PRIN2-0     NBG2 Priority Number

    FORCE_INLINE uint16 ReadPRINB() const {
        uint16 value = 0;
        bit::deposit_into<0, 2>(value, bgParams[3].priorityNumber);
        bit::deposit_into<8, 10>(value, bgParams[4].priorityNumber);
        return value;
    }

    FORCE_INLINE void WritePRINB(uint16 value) {
        bgParams[3].priorityNumber = bit::extract<0, 2>(value);
        bgParams[4].priorityNumber = bit::extract<8, 10>(value);
    }

    // 1800FC   PRIR    RBG0 Priority Number
    //
    //   bits   r/w  code          description
    //   15-3        -             Reserved, must be zero
    //    2-0     W  R0PRIN2-0     RBG0 Priority Number

    FORCE_INLINE uint16 ReadPRIR() const {
        uint16 value = 0;
        bit::deposit_into<0, 2>(value, bgParams[0].priorityNumber);
        return value;
    }

    FORCE_INLINE void WritePRIR(uint16 value) {
        bgParams[0].priorityNumber = bit::extract<0, 2>(value);
    }

    // 1800FE   -       Reserved

    // 180100   CCRSA   Sprite 0 and 1 Color Calculation Ratio
    //
    //   bits   r/w  code          description
    //  15-13        -             Reserved, must be zero
    //   12-8     W  S1CCRT4-0     Sprite Register 1 Color Calculation Ratio
    //    7-5        -             Reserved, must be zero
    //    4-0     W  S0CCRT4-0     Sprite Register 0 Color Calculation Ratio
    //
    // 180102   CCRSB   Sprite 2 and 3 Color Calculation Ratio
    //
    //   bits   r/w  code          description
    //  15-13        -             Reserved, must be zero
    //   12-8     W  S3CCRT4-0     Sprite Register 3 Color Calculation Ratio
    //    7-5        -             Reserved, must be zero
    //    4-0     W  S2CCRT4-0     Sprite Register 2 Color Calculation Ratio
    //
    // 180104   CCRSC   Sprite 4 and 5 Color Calculation Ratio
    //
    //   bits   r/w  code          description
    //  15-13        -             Reserved, must be zero
    //   12-8     W  S5CCRT4-0     Sprite Register 5 Color Calculation Ratio
    //    7-5        -             Reserved, must be zero
    //    4-0     W  S4CCRT4-0     Sprite Register 4 Color Calculation Ratio
    //
    // 180106   CCRSD   Sprite 6 and 7 Color Calculation Ratio
    //
    //   bits   r/w  code          description
    //  15-13        -             Reserved, must be zero
    //   12-8     W  S7CCRT4-0     Sprite Register 7 Color Calculation Ratio
    //    7-5        -             Reserved, must be zero
    //    4-0     W  S6CCRT4-0     Sprite Register 6 Color Calculation Ratio

    FORCE_INLINE uint16 ReadCCRSn(uint32 offset) const {
        uint16 value = 0;
        bit::deposit_into<0, 4>(value, spriteParams.colorCalcRatios[offset * 2 + 0] ^ 31);
        bit::deposit_into<8, 12>(value, spriteParams.colorCalcRatios[offset * 2 + 1] ^ 31);
        return value;
    }
    FORCE_INLINE uint16 ReadCCRSA() const {
        return ReadCCRSn(0);
    }
    FORCE_INLINE uint16 ReadCCRSB() const {
        return ReadCCRSn(1);
    }
    FORCE_INLINE uint16 ReadCCRSC() const {
        return ReadCCRSn(2);
    }
    FORCE_INLINE uint16 ReadCCRSD() const {
        return ReadCCRSn(3);
    }

    FORCE_INLINE void WriteCCRSn(uint32 offset, uint16 value) {
        spriteParams.colorCalcRatios[offset * 2 + 0] = bit::extract<0, 4>(value) ^ 31;
        spriteParams.colorCalcRatios[offset * 2 + 1] = bit::extract<8, 12>(value) ^ 31;
    }
    FORCE_INLINE void WriteCCRSA(uint16 value) {
        WriteCCRSn(0, value);
    }
    FORCE_INLINE void WriteCCRSB(uint16 value) {
        WriteCCRSn(1, value);
    }
    FORCE_INLINE void WriteCCRSC(uint16 value) {
        WriteCCRSn(2, value);
    }
    FORCE_INLINE void WriteCCRSD(uint16 value) {
        WriteCCRSn(3, value);
    }

    // 180108   CCRNA   NBG0 and NBG1 Color Calculation Ratio
    //
    //   bits   r/w  code          description
    //  15-13        -             Reserved, must be zero
    //   12-8     W  N1CCRT4-0     NBG1/EXBG Color Calculation Ratio
    //    7-5        -             Reserved, must be zero
    //    4-0     W  N0CCRT4-0     NBG0/RBG1 Color Calculation Ratio
    //
    // 18010A   CCRNB   NBG2 and NBG3 Color Calculation Ratio
    //
    //   bits   r/w  code          description
    //  15-13        -             Reserved, must be zero
    //   12-8     W  N3CCRT4-0     NBG3 Color Calculation Ratio
    //    7-5        -             Reserved, must be zero
    //    4-0     W  N2CCRT4-0     NBG2 Color Calculation Ratio
    //
    // 18010C   CCRR    RBG0 Color Calculation Ratio
    //
    //   bits   r/w  code          description
    //   15-5        -             Reserved, must be zero
    //    4-0     W  R0CCRT4-0     RBG0 Color Calculation Ratio
    //
    // 18010E   CCRLB   Line Color Screen and Back Screen Color Calculation Ratio
    //
    //   bits   r/w  code          description
    //  15-13        -             Reserved, must be zero
    //   12-8     W  BKCCRT4-0     Back Screen Color Calculation Ratio
    //    7-5        -             Reserved, must be zero
    //    4-0     W  LCCCRT4-0     Line Color Screen Color Calculation Ratio

    FORCE_INLINE uint16 ReadCCRNA() const {
        uint16 value = 0;
        bit::deposit_into<0, 4>(value, bgParams[1].colorCalcRatio ^ 31);
        bit::deposit_into<8, 12>(value, bgParams[2].colorCalcRatio ^ 31);
        return value;
    }

    FORCE_INLINE void WriteCCRNA(uint16 value) {
        bgParams[1].colorCalcRatio = bit::extract<0, 4>(value) ^ 31;
        bgParams[2].colorCalcRatio = bit::extract<8, 12>(value) ^ 31;
    }

    FORCE_INLINE uint16 ReadCCRNB() const {
        uint16 value = 0;
        bit::deposit_into<0, 4>(value, bgParams[3].colorCalcRatio ^ 31);
        bit::deposit_into<8, 12>(value, bgParams[4].colorCalcRatio ^ 31);
        return value;
    }

    FORCE_INLINE void WriteCCRNB(uint16 value) {
        bgParams[3].colorCalcRatio = bit::extract<0, 4>(value) ^ 31;
        bgParams[4].colorCalcRatio = bit::extract<8, 12>(value) ^ 31;
    }

    FORCE_INLINE uint16 ReadCCRR() const {
        uint16 value = 0;
        bit::deposit_into<0, 4>(value, bgParams[0].colorCalcRatio ^ 31);
        return value;
    }

    FORCE_INLINE void WriteCCRR(uint16 value) {
        bgParams[0].colorCalcRatio = bit::extract<0, 4>(value) ^ 31;
    }

    FORCE_INLINE uint16 ReadCCRLB() const {
        uint16 value = 0;
        bit::deposit_into<0, 4>(value, lineScreenParams.colorCalcRatio ^ 31);
        bit::deposit_into<8, 12>(value, backScreenParams.colorCalcRatio ^ 31);
        return value;
    }

    FORCE_INLINE void WriteCCRLB(uint16 value) {
        lineScreenParams.colorCalcRatio = bit::extract<0, 4>(value) ^ 31;
        backScreenParams.colorCalcRatio = bit::extract<8, 12>(value) ^ 31;
    }

    // 180110   CLOFEN  Color Offset Enable
    //
    //   bits   r/w  code          description
    //   15-7        -             Reserved, must be zero
    //      6     W  SPCOEN        Sprite Color Offset Enable
    //      5     W  BKCOEN        Back Screen Color Offset Enable
    //      4     W  R0COEN        RBG0 Color Offset Enable
    //      3     W  N3COEN        NBG3 Color Offset Enable
    //      2     W  N2COEN        NBG2 Color Offset Enable
    //      1     W  N1COEN        NBG1/EXBG Color Offset Enable
    //      0     W  N0COEN        NBG0/RBG1 Color Offset Enable

    FORCE_INLINE uint16 ReadCLOFEN() const {
        uint16 value = 0;
        bit::deposit_into<0>(value, colorOffsetEnable[2]);
        bit::deposit_into<1>(value, colorOffsetEnable[3]);
        bit::deposit_into<2>(value, colorOffsetEnable[4]);
        bit::deposit_into<3>(value, colorOffsetEnable[5]);
        bit::deposit_into<4>(value, colorOffsetEnable[1]);
        bit::deposit_into<5>(value, colorOffsetEnable[6]);
        bit::deposit_into<6>(value, colorOffsetEnable[0]);
        return value;
    }

    FORCE_INLINE void WriteCLOFEN(uint16 value) {
        colorOffsetEnable[2] = bit::test<0>(value);
        colorOffsetEnable[3] = bit::test<1>(value);
        colorOffsetEnable[4] = bit::test<2>(value);
        colorOffsetEnable[5] = bit::test<3>(value);
        colorOffsetEnable[1] = bit::test<4>(value);
        colorOffsetEnable[6] = bit::test<5>(value);
        colorOffsetEnable[0] = bit::test<6>(value);
    }

    // 180112   CLOFSL  Color Offset Select
    //
    //   bits   r/w  code          description
    //   15-7        -             Reserved, must be zero
    //      6     W  SPCOSL        Sprite Color Offset Select
    //      5     W  BKCOSL        Back Screen Color Offset Select
    //      4     W  R0COSL        RBG0 Color Offset Select
    //      3     W  N3COSL        NBG3 Color Offset Select
    //      2     W  N2COSL        NBG2 Color Offset Select
    //      1     W  N1COSL        NBG1 Color Offset Select
    //      0     W  N0COSL        NBG0 Color Offset Select
    //
    // For all bits:
    //   0 = Color Offset A
    //   1 = Color Offset B

    FORCE_INLINE uint16 ReadCLOFSL() const {
        uint16 value = 0;
        bit::deposit_into<0>(value, colorOffsetSelect[2]);
        bit::deposit_into<1>(value, colorOffsetSelect[3]);
        bit::deposit_into<2>(value, colorOffsetSelect[4]);
        bit::deposit_into<3>(value, colorOffsetSelect[5]);
        bit::deposit_into<4>(value, colorOffsetSelect[1]);
        bit::deposit_into<5>(value, colorOffsetSelect[6]);
        bit::deposit_into<6>(value, colorOffsetSelect[0]);
        return value;
    }

    FORCE_INLINE void WriteCLOFSL(uint16 value) {
        colorOffsetSelect[2] = bit::test<0>(value);
        colorOffsetSelect[3] = bit::test<1>(value);
        colorOffsetSelect[4] = bit::test<2>(value);
        colorOffsetSelect[5] = bit::test<3>(value);
        colorOffsetSelect[1] = bit::test<4>(value);
        colorOffsetSelect[6] = bit::test<5>(value);
        colorOffsetSelect[0] = bit::test<6>(value);
    }

    // 180114   COAR    Color Offset A - Red
    // 180116   COAG    Color Offset A - Green
    // 180118   COAB    Color Offset A - Blue
    // 18011A   COBR    Color Offset B - Red
    // 18011C   COBG    Color Offset B - Green
    // 18011E   COBB    Color Offset B - Blue
    //
    //   bits   r/w  code          description
    //   15-9        -             Reserved, must be zero
    //    8-0     W  COxc8-0       Color Offset Value
    //
    // x: A,B; c: R,G,B

    FORCE_INLINE uint16 ReadCOxR(uint8 select) const {
        return colorOffset[select].r;
    }
    FORCE_INLINE uint16 ReadCOxG(uint8 select) const {
        return colorOffset[select].g;
    }
    FORCE_INLINE uint16 ReadCOxB(uint8 select) const {
        return colorOffset[select].b;
    }
    FORCE_INLINE uint16 ReadCOAR() const {
        return ReadCOxR(0);
    }
    FORCE_INLINE uint16 ReadCOAG() const {
        return ReadCOxG(0);
    }
    FORCE_INLINE uint16 ReadCOAB() const {
        return ReadCOxB(0);
    }
    FORCE_INLINE uint16 ReadCOBR() const {
        return ReadCOxR(1);
    }
    FORCE_INLINE uint16 ReadCOBG() const {
        return ReadCOxG(1);
    }
    FORCE_INLINE uint16 ReadCOBB() const {
        return ReadCOxB(1);
    }

    FORCE_INLINE void WriteCOxR(uint8 select, uint16 value) {
        colorOffset[select].r = bit::extract<0, 8>(value);
        colorOffset[select].nonZero =
            colorOffset[select].r != 0 || colorOffset[select].g != 0 || colorOffset[select].b != 0;
    }
    FORCE_INLINE void WriteCOxG(uint8 select, uint16 value) {
        colorOffset[select].g = bit::extract<0, 8>(value);
        colorOffset[select].nonZero =
            colorOffset[select].r != 0 || colorOffset[select].g != 0 || colorOffset[select].b != 0;
    }
    FORCE_INLINE void WriteCOxB(uint8 select, uint16 value) {
        colorOffset[select].b = bit::extract<0, 8>(value);
        colorOffset[select].nonZero =
            colorOffset[select].r != 0 || colorOffset[select].g != 0 || colorOffset[select].b != 0;
    }
    FORCE_INLINE void WriteCOAR(uint16 value) {
        WriteCOxR(0, value);
    }
    FORCE_INLINE void WriteCOAG(uint16 value) {
        WriteCOxG(0, value);
    }
    FORCE_INLINE void WriteCOAB(uint16 value) {
        WriteCOxB(0, value);
    }
    FORCE_INLINE void WriteCOBR(uint16 value) {
        WriteCOxR(1, value);
    }
    FORCE_INLINE void WriteCOBG(uint16 value) {
        WriteCOxG(1, value);
    }
    FORCE_INLINE void WriteCOBB(uint16 value) {
        WriteCOxB(1, value);
    }

    // -------------------------------------------------------------------------

    // Indicates if TVMD has changed.
    // The screen resolution is updated on VBlank.
    bool TVMDDirty;

    // Indicates if any VRAM access pattern related registers have changed.
    // This includes:
    // - All CYCxn registers
    // - TVMD.HRESOn
    // - CHCTLA/B
    // - RAMCTL
    // - BGON
    // - ZMCTL
    bool accessPatternsDirty;

    // Indicates if the vertical cell scroll was enabled or disabled on NBG 0 or 1.
    // Also set when VRAM access patterns change.
    bool vcellScrollDirty;

    // Vertical cell scroll increment.
    // Based on CYCA0/A1/B0/B1 parameters.
    uint32 vcellScrollInc;

    // Whether to display each background:
    // [0] NBG0
    // [1] NBG1
    // [2] NBG2
    // [3] NBG3
    // [4] RBG0
    // [5] RBG1
    // Note that when RBG1 is enabled, RBG0 is required to be enabled as well and all NBGs are disabled.
    // We'll assume that RBG0 is always enabled when RBG1 is also enabled.
    // Derived from BGON.xxON
    std::array<bool, 6> bgEnabled;

    // Background parameters:
    // [0] RBG0
    // [1] NBG0/RBG1
    // [2] NBG1/EXBG
    // [3] NBG2
    // [4] NBG3
    std::array<BGParams, 5> bgParams;
    SpriteParams spriteParams;
    LineBackScreenParams lineScreenParams;
    LineBackScreenParams backScreenParams;

    // Rotation Parameters A and B
    std::array<RotationParams, 2> rotParams;
    CommonRotationParams commonRotParams;

    // Window 0 and 1 parameters
    std::array<WindowParams, 2> windowParams;

    // Vertical cell scroll table base address.
    // Only valid for NBG0 and NBG1.
    // Derived from VCSTAU/L
    uint32 vcellScrollTableAddress;

    // Current vertical cell scroll table address.
    // Reset at the start of every frame and incremented every cell or 8 bitmap pixels.
    uint32 cellScrollTableAddress;

    // Mosaic dimensions.
    // Applies to all backgrounds with the mosaic effect enabled.
    // Rotation backgrounds only use the horizontal dimension.
    // Derived from MZCTL.MZSZH/V
    uint8 mosaicH; // Horizontal mosaic size (MZCTL.MZSZH + 1)
    uint8 mosaicV; // Vertical mosaic size (MZCTL.MZSZV + 1)

    // Color offset enable for:
    // [0] Sprite
    // [1] RBG0
    // [2] NBG0/RBG1
    // [3] NBG1/EXBG
    // [4] NBG2
    // [5] NBG3
    // [6] Back screen
    // Derived from CLOFEN.xxCOEN
    alignas(16) std::array<bool, 7> colorOffsetEnable;

    // Color offset select for:
    // [0] Sprite
    // [1] RBG0
    // [2] NBG0/RBG1
    // [3] NBG1/EXBG
    // [4] NBG2
    // [5] NBG3
    // [6] Back screen
    // false selects color offset A; true selects color offset B
    // Derived from CLOFSL.xxCOSL
    alignas(16) std::array<bool, 7> colorOffsetSelect;

    // Color offset parameters.
    // Derived from COAR/G/B and COBR/G/B
    std::array<ColorOffset, 2> colorOffset;

    // Color calculation parameters.
    // Derived from CCTL, CCRNA/B, CCRR and CCRLB
    ColorCalcParams colorCalcParams;

    // Whether color calculations are restricted based on current video mode settings.
    // Color calculations are restricted when all of these conditions are met:
    // - Using high resolution or exclusive monitor modes
    // - Using color RAM modes other than 0
    // This prevents blending layers on top of palette layers. Blending with RGB layers is still allowed.
    // Derived from TVMD.HRESOn and RAMCTL.CRMDn
    bool restrictedColorCalc;

    // Special function codes A and B.
    // Derived from SFCODE
    std::array<SpecialFunctionCodes, 2> specialFunctionCodes;

    // Enables transparent shadow sprites.
    // Derived from SDCTL.TPSDSL
    bool transparentShadowEnable;

    void UpdateRestrictedColorCalc() {
        restrictedColorCalc = TVMD.HRESOn >= 2 && vramControl.colorRAMMode > 0;
    }
};

} // namespace ymir::vdp
