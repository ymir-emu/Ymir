#pragma once

/**
@file
@brief VDP1 register definitions.
*/

#include "vdp1_defs.hpp"

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/inline.hpp>

namespace ymir::vdp {

inline constexpr uint32 kVDP1NoReturn = ~0u;

struct VDP1RegTVMR {
    VDP1RegTVMR() {
        Reset();
    }

    void Reset() {
        vblankErase = false;
        hdtvEnable = false;
        fbRotEnable = false;
        pixel8Bits = false;

        Update();
    }

    void Update() {
        static constexpr uint32 kSizesH[] = {512, 1024, 512, 512, 512, 512, 512, 512};
        static constexpr uint32 kSizesV[] = {256, 256, 256, 512, 512, 512, 512, 512};
        static constexpr uint32 kEraseShifts[] = {9, 9, 9, 8, 9, 8, 9, 8};
        const uint8 tvm = (hdtvEnable << 2) | (fbRotEnable << 1) | (pixel8Bits << 0);
        fbSizeH = kSizesH[tvm];
        fbSizeV = kSizesV[tvm];
        eraseOffsetShift = kEraseShifts[tvm];
        // Examples of games using each mode:
        // TVM = 0   Panzer Dragoon
        // TVM = 1   Resident Evil (options menu)
        // TVM = 2   Highway 2000 (in-game)
        // TVM = 3   Grandia (battle)
        // TVM = 4   (none so far)
        // TVM = 5-7 (hopefully none, as these are supposedly "illegal")
    }

    [[nodiscard]] FORCE_INLINE uint16 Read() const {
        uint16 value = 0;
        bit::deposit_into<3>(value, vblankErase);
        bit::deposit_into<2>(value, hdtvEnable);
        bit::deposit_into<1>(value, fbRotEnable);
        bit::deposit_into<0>(value, pixel8Bits);
        return value;
    }

    FORCE_INLINE void Write(uint16 value) {
        vblankErase = bit::test<3>(value);
        hdtvEnable = bit::test<2>(value);
        fbRotEnable = bit::test<1>(value);
        pixel8Bits = bit::test<0>(value);
        Update();
    }

    // Erase the framebuffer on VBlank.
    // Derived from TVMR.VBE
    bool vblankErase;

    // HDTV mode enable.
    // Derived from TVMR.TVM bit 2
    bool hdtvEnable;

    // Frame buffer rotation enable.
    // Derived from TVMR.TVM bit 1
    bool fbRotEnable;

    // Pixel data width - 8 bits (true) or 16 bits (false)
    // Derived from TVMR.TVM bit 0
    bool pixel8Bits;

    // Frame buffer dimensions.
    // Derived from TVMR.TVM
    uint32 fbSizeH;
    uint32 fbSizeV;

    // Shift applied to the Y coordinate for the framebuffer erase process.
    // Derived from TVMR.TVM
    uint32 eraseOffsetShift;
};

struct VDP1Regs {
    VDP1Regs() {
        Reset();
    }

    void Reset() {
        tvmr.Reset();

        fbSwapTrigger = false;
        fbSwapMode = false;
        dblInterlaceDrawLine = false;
        dblInterlaceEnable = false;
        evenOddCoordSelect = false;

        plotTrigger = 0;

        currFrameEnded = false;
        prevFrameEnded = false;

        currCommandAddress = 0;
        prevCommandAddress = 0;

        returnAddress = kVDP1NoReturn;

        fbParamsChanged = false;
    }

    template <bool peek>
    uint16 Read(uint32 address) const {
        if constexpr (peek) {
            switch (address) {
            case 0x00: return ReadTVMR(); // TVMR is write-only
            case 0x02: return ReadFBCR(); // FBCR is write-only
            case 0x04: return ReadPTMR(); // PTMR is write-only
            case 0x06: return ReadEWDR(); // EWDR is write-only
            case 0x08: return ReadEWLR(); // EWLR is write-only
            case 0x0A: return ReadEWRR(); // EWRR is write-only
            }
        }

        switch (address) {
        case 0x0C: return 0; // ENDR is write-only

        case 0x10: return ReadEDSR();
        case 0x12: return ReadLOPR();
        case 0x14: return ReadCOPR();
        case 0x16: return ReadMODR();

        default: return 0;
        }
    }

    template <bool poke>
    void Write(uint32 address, uint16 value) {
        switch (address) {
        case 0x00: WriteTVMR(value); break;
        case 0x02: WriteFBCR(value); break;
        case 0x04: WritePTMR(value); break;
        case 0x06: WriteEWDR(value); break;
        case 0x08: WriteEWLR(value); break;
        case 0x0A: WriteEWRR(value); break;
        case 0x0C: break; // ENDR, handled in VDP class
        }

        if constexpr (poke) {
            switch (address) {
            case 0x10: WriteEDSR(value); break; // EDSR is read-only
            case 0x12: WriteLOPR(value); break; // LOPR is read-only
            case 0x14: WriteCOPR(value); break; // COPR is read-only
            case 0x16: WriteMODR(value); break; // MODR is read-only
            }
        }
    }

    // TVMR register.
    VDP1RegTVMR tvmr;

    // Frame buffer swap trigger: enabled (true) or disabled (false).
    // Exact behavior depends on TVMR.VBE, FBCR.FCM and FBCR.FCT.
    // Derived from FBCR.FCT
    bool fbSwapTrigger;

    // Frame buffer swap mode: manual (true) or 1-cycle mode (false).
    // Exact behavior depends on TVMR.VBE, FBCR.FCM and FBCR.FCT.
    // Derived from FBCR.FCM
    bool fbSwapMode;

    // Double interlace draw line (even/odd lines).
    // Behavior depends on FBCR.DIE.
    // Derived from FBCR.DIL
    bool dblInterlaceDrawLine;

    // Double interlace enable.
    // Derived from FBCR.DIE
    bool dblInterlaceEnable;

    // Even (false)/odd (true) coordinate select.
    // Affects High Speed Shrink drawing.
    // Derived from FBCR.EOS
    bool evenOddCoordSelect;

    // Frame drawing trigger.
    // Derived from PTMR.PTM
    uint8 plotTrigger;

    // Value written to erased parts of the framebuffer.
    // Derived from EWDR
    uint16 eraseWriteValue = 0u;

    // Erase window coordinates
    // Derived from EWLR and EWRR
    uint16 eraseX1 = 0u; // Erase window top-left X coordinate
    uint16 eraseY1 = 0u; // Erase window top-left Y coordinate
    uint16 eraseX3 = 0u; // Erase window bottom-right X coordinate
    uint16 eraseY3 = 0u; // Erase window bottom-right Y coordinate

    // Whether the drawing end command was fetched on the current and previous frames.
    // Used in EDSR
    bool currFrameEnded; // Drawing end bit fetched on current frame
    bool prevFrameEnded; // Drawing end bit fetched on previous frame

    // Addresses of the last executed commands in the current and previous frames.
    // Used in LOPR and COPR
    uint32 currCommandAddress; // Address of the last executed command in the current frame
    uint32 prevCommandAddress; // Address of the last executed command in the previous frame
    uint32 nextCommandAddress; // Address of the next commmand to be executed

    // Return address in the command table.
    // Used by commands that use the jump types Call and Return.
    uint32 returnAddress;

    // Whether FCM or FCT have been written to.
    bool fbParamsChanged;

    // Latched erase parameters
    uint16 eraseWriteValueLatch = 0u;            // 16-bit write value
    uint16 eraseX1Latch = 0u, eraseY1Latch = 0u; // Top-left erase region coordinates
    uint16 eraseX3Latch = 0u, eraseY3Latch = 0u; // Bottom-right erase region coordinates

    void LatchEraseParameters() {
        eraseWriteValueLatch = eraseWriteValue;
        eraseX1Latch = eraseX1;
        eraseY1Latch = eraseY1;
        eraseX3Latch = eraseX3;
        eraseY3Latch = eraseY3;
    }

    void UpdateTVMR() {
        tvmr.Update();
    }

    // 100000   TVMR  TV Mode Selection
    //
    //   bits   r/w  code  description
    //   15-4        -     Reserved, must be zero
    //      3     W  VBE   V-Blank Erase/Write Enable
    //                       0 = do not erase/write during VBlank
    //                       1 = perform erase/write during VBlank
    //    2-0     W  TVM   TV Mode Select
    //                       bit 2: HDTV Enable (0=NTSC/PAL, 1=HDTV/31KC)
    //                       bit 1: Frame Buffer Rotation Enable (0=disable, 1=enable)
    //                       bit 0: Bit Depth Selection (0=16bpp, 1=8bpp)
    //
    // Notes:
    // - When using frame buffer rotation, interlace cannot be set to double density mode.
    // - When using HDTV modes, rotation must be disabled and the bit depth must be set to 16bpp
    // - TVM changes must be done between the 2nd HBlank IN from VBlank IN and the 1st HBlank IN after VBlank OUT.
    // - The frame buffer screen size varies based on TVM:
    //     TVM   Name          Frame buffer screen size
    //     000   Normal         512x256
    //     001   Hi-Res        1024x256
    //     010   Rotation 16    512x256
    //     011   Rotation 8     512x512
    //     100   HDTV           512x256

    [[nodiscard]] FORCE_INLINE uint16 ReadTVMR() const {
        return tvmr.Read();
    }

    FORCE_INLINE void WriteTVMR(uint16 value) {
        tvmr.Write(value);
    }

    // -------------------------------------------------------------------------

    // 100002   FBCR  Frame Buffer Change Mode
    //
    //   bits   r/w  code  description
    //   15-5        -     Reserved, must be zero
    //      4     W  EOS   Even/Odd Coordinate Select (sample pixels at: 0=even coordinates, 1=odd coordinates)
    //                       Related to High Speed Shrink option
    //      3     W  DIE   Double Interlace Enable (0=non-interlace/single interlace, 1=double interlace)
    //      2     W  DIL   Double Interlace Draw Line
    //                       If DIE = 0:
    //                         0 = draws even and odd lines
    //                         1 = (prohibited)
    //                       If DIE = 1:
    //                         0 = draws even lines only
    //                         1 = draws odd lines only
    //      1     W  FCM   Frame Buffer Change Mode
    //      0     W  FCT   Frame Buffer Change Trigger
    //
    // Notes:
    // TVMR.VBE, FCM and FCT specify when frame buffer swaps happen and whether they are cleared on swap.
    //   TVMR.VBE  FCM  FCT  Mode                          Timing
    //         0    0    0   1-cycle mode                  Swap every field (60 Hz)
    //         0    1    0   Manual mode (erase)           Erase in next field
    //         0    1    1   Manual mode (swap)            Swap in next field
    //         1    1    1   Manual mode (erase and swap)  Erase at VBlank IN and swap in next field
    // Unlisted combinations are prohibited.
    // For manual erase and swap, the program should write VBE,FCM,FCT = 011, then wait until the HBlank IN of the
    // last visible scanline immediately before VBlank (224 or 240) to issue another write to set VBE,FCM,FCT = 111,
    // and finally restore VBE = 0 after VBlank OUT to stop VDP1 from clearing the next frame buffer.

    [[nodiscard]] FORCE_INLINE uint16 ReadFBCR() const {
        uint16 value = 0;
        bit::deposit_into<0>(value, fbSwapTrigger);
        bit::deposit_into<1>(value, fbSwapMode);
        bit::deposit_into<2>(value, dblInterlaceDrawLine);
        bit::deposit_into<3>(value, dblInterlaceEnable);
        bit::deposit_into<4>(value, evenOddCoordSelect);
        return value;
    }

    FORCE_INLINE void WriteFBCR(uint16 value) {
        fbSwapTrigger = bit::test<0>(value);
        fbSwapMode = bit::test<1>(value);
        dblInterlaceDrawLine = bit::test<2>(value);
        dblInterlaceEnable = bit::test<3>(value);
        evenOddCoordSelect = bit::test<4>(value);

        fbParamsChanged = true;
    }

    // 100004   PTMR  Draw Trigger
    //
    //   bits   r/w  code  description
    //   15-2        -     Reserved, must be zero
    //    1-0     W  PTM   Plot Trigger Mode
    //                       00 (0) = No trigger
    //                       01 (1) = Trigger immediately upon writing this value to PTMR
    //                       10 (2) = Trigger on frame buffer swap
    //                       11 (3) = (prohibited)

    [[nodiscard]] FORCE_INLINE uint16 ReadPTMR() const {
        return plotTrigger;
    }

    FORCE_INLINE void WritePTMR(uint16 value) {
        plotTrigger = bit::extract<0, 1>(value);
    }

    // 100006   EWDR  Erase/write Data
    //
    //   bits   r/w  code  description
    //   15-0     W  -     Erase/Write Data Value
    //
    // Notes:
    // - The entire register value is used to clear the frame buffer
    // - Writes 16-bit values at a time
    // - For 8-bit modes:
    //   - Bits 15-8 specify the values for even X coordinates
    //   - Bits 7-0 specify the values for odd X coordinates

    [[nodiscard]] FORCE_INLINE uint16 ReadEWDR() const {
        return eraseWriteValue;
    }

    FORCE_INLINE void WriteEWDR(uint16 value) {
        eraseWriteValue = value;
    }

    // 100008   EWLR  Erase/write Upper-left coordinate
    //
    //   bits   r/w  code  description
    //     15        -     Reserved, must be zero
    //   14-9     W  -     Upper-left Coordinate X1
    //    8-0     W  -     Upper-left Coordinate Y1

    [[nodiscard]] FORCE_INLINE uint16 ReadEWLR() const {
        uint16 value = 0;
        bit::deposit_into<0, 8>(value, eraseY1);
        bit::deposit_into<9, 14>(value, eraseX1 >> 3);
        return value;
    }

    FORCE_INLINE void WriteEWLR(uint16 value) {
        eraseY1 = bit::extract<0, 8>(value);
        eraseX1 = bit::extract<9, 14>(value) << 3;
    }

    // 10000A   EWRR  Erase/write Bottom-right Coordinate
    //
    //   bits   r/w  code  description
    //   15-9     W  -     Lower-right Coordinate X3
    //    8-0     W  -     Lower-right Coordinate Y3

    [[nodiscard]] FORCE_INLINE uint16 ReadEWRR() const {
        uint16 value = 0;
        bit::deposit_into<0, 8>(value, eraseY3);
        bit::deposit_into<9, 15>(value, eraseX3 >> 3);
        return value;
    }

    FORCE_INLINE void WriteEWRR(uint16 value) {
        eraseY3 = bit::extract<0, 8>(value);
        eraseX3 = bit::extract<9, 15>(value) << 3;
    }

    // 10000C   ENDR  Draw Forced Termination
    //
    // (all bits are reserved and must be zero)
    //
    // Notes:
    // - Stops drawing ~30 clock cycles after the write is issued to this register

    // 100010   EDSR  Transfer End Status
    //
    //   bits   r/w  code  description
    //   15-2        -     Reserved, must be zero
    //      1   R    CEF   Current End Bit Fetch Status
    //                       0 = drawing in progress (end bit not yet fetched)
    //                       1 = drawing finished (end bit fetched)
    //      0   R    BEF   Before End Bit Fetch Status
    //                       0 = previous drawing end bit not fetched
    //                       1 = previous drawing end bit fetched

    [[nodiscard]] FORCE_INLINE uint16 ReadEDSR() const {
        uint16 value = 0;
        bit::deposit_into<0>(value, prevFrameEnded);
        bit::deposit_into<1>(value, currFrameEnded);
        return value;
    }

    FORCE_INLINE void WriteEDSR(uint16 value) {
        prevFrameEnded = bit::test<0>(value);
        currFrameEnded = bit::test<1>(value);
    }

    // 100012   LOPR  Last Operation Command Address
    //
    //   bits   r/w  code  description
    //   15-0   R    -     Last Operation Command Address (divided by 8)

    [[nodiscard]] FORCE_INLINE uint16 ReadLOPR() const {
        return prevCommandAddress >> 3u;
    }

    FORCE_INLINE void WriteLOPR(uint16 value) {
        prevCommandAddress = value << 3u;
    }

    // 100014   COPR  Current Operation Command Address
    //
    //   bits   r/w  code  description
    //   15-0   R    -     Current Operation Command Address (divided by 8)

    [[nodiscard]] FORCE_INLINE uint16 ReadCOPR() const {
        return currCommandAddress >> 3u;
    }

    FORCE_INLINE void WriteCOPR(uint16 value) {
        currCommandAddress = value << 3u;
    }

    // 100016   MODR  Mode Status
    //
    //   bits   r/w  code  description
    //  15-12   R    VER   Version Number (0b0001)
    //   11-9        -     Reserved, must be zero
    //      8   R    PTM1  Plot Trigger Mode (read-only view of PTMR.PTM bit 1)
    //      7   R    EOS   Even/Odd Coordinate Select (read-only view of FBCR.EOS)
    //      6   R    DIE   Double Interlace Enable (read-only view of FBCR.DIE)
    //      5   R    DIL   Double Interlace Draw Line (read-only view of FBCR.DIL)
    //      4   R    FCM   Frame Buffer Change Mode (read-only view of FBCR.FCM)
    //      3   R    VBE   V-Blank Erase/Write Enable (read-only view of TVMR.VBE)
    //    2-0   R    TVM   TV Mode Selection (read-only view of TVMR.TVM)

    [[nodiscard]] FORCE_INLINE uint16 ReadMODR() const {
        uint16 value = 0;
        bit::deposit_into<0>(value, tvmr.pixel8Bits);
        bit::deposit_into<1>(value, tvmr.fbRotEnable);
        bit::deposit_into<2>(value, tvmr.hdtvEnable);
        bit::deposit_into<3>(value, tvmr.vblankErase);
        bit::deposit_into<4>(value, fbSwapMode);
        bit::deposit_into<5>(value, dblInterlaceDrawLine);
        bit::deposit_into<6>(value, dblInterlaceEnable);
        bit::deposit_into<7>(value, evenOddCoordSelect);
        bit::deposit_into<12, 15>(value, 0b0001);
        return value;
    }

    FORCE_INLINE void WriteMODR(uint16 value) {
        // NOTE: poke-only
        tvmr.pixel8Bits = bit::test<0>(value);
        tvmr.fbRotEnable = bit::test<1>(value);
        tvmr.hdtvEnable = bit::test<2>(value);
        tvmr.vblankErase = bit::test<3>(value);
        fbSwapMode = bit::test<4>(value);
        dblInterlaceDrawLine = bit::test<5>(value);
        dblInterlaceEnable = bit::test<6>(value);
        evenOddCoordSelect = bit::test<7>(value);
    }
};

} // namespace ymir::vdp
