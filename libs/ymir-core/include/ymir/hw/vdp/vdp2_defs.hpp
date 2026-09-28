#pragma once

/**
@file
@brief General VDP2 definitions.
*/

#include "vdp_common_defs.hpp"

#include <ymir/core/types.hpp>

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/data_ops.hpp>
#include <ymir/util/inline.hpp>
#include <ymir/util/size_ops.hpp>
#include <ymir/util/unreachable.hpp>

#include <array>
#include <cassert>

namespace ymir::vdp {

// -----------------------------------------------------------------------------
// Display resolutions

inline constexpr uint32 kDefaultResH = 320; // Default/initial horizontal resolution
inline constexpr uint32 kDefaultResV = 224; // Default/initial vertical resolution

inline constexpr uint32 kMinResH = 320; // Minimum horizontal resolution
inline constexpr uint32 kMinResV = 224; // Minimum vertical resolution

inline constexpr uint32 kMaxNormalResH = 352; // Maximum horizontal resolution in normal mode
inline constexpr uint32 kMaxNormalResV = 256; // Maximum vertical resolution in normal mode

inline constexpr uint32 kMaxResH = 704; // Maximum horizontal resolution
inline constexpr uint32 kMaxResV = 512; // Maximum vertical resolution

inline constexpr uint32 kMinResScaleFactor = 2u;  // Minimum resolution scaling factor
inline constexpr uint32 kMaxResScaleFactor = 12u; // Maximum resolution scaling factor
inline constexpr uint32 kMaxScaledResH = 8192u;   // Maximum scaled horizontal resolution (in pixels)
inline constexpr uint32 kMaxScaledResV = 4096u;   // Maximum scaled vertical resolution (in pixels)

// -----------------------------------------------------------------------------
// Memory

inline constexpr std::size_t kVDP2VRAMSize = 512_KiB;
inline constexpr std::size_t kVDP2CRAMSize = 4_KiB;

// RAMCTL.CRMD modes 2 and 3 shuffle address bits as follows:
//   11 10 09 08 07 06 05 04 03 02 01 00 -- input
//   01 11 10 09 08 07 06 05 04 03 02 00 -- output
// In short, bits 11-02 are shifted right and bit 01 is shifted to the top.
// This results in the lower 2 bytes of every longword to be stored at 000..7FF and the upper 2 bytes at 800..FFF.
inline constexpr auto kVDP2CRAMAddressMapping = [] {
    std::array<std::array<uint32, kVDP2CRAMSize>, 2> addrs{};
    for (uint32 addr = 0; addr < kVDP2CRAMSize; addr++) {
        addrs[0][addr] = addr;
        addrs[1][addr] = (bit::extract<1>(addr) << 11u) | (bit::extract<2, 11>(addr) << 1u) | bit::extract<0>(addr);
    }
    return addrs;
}();

// Reverse address mapping for RAMCTL.CRMD modes 2 and 3.
inline constexpr auto kVDP2CRAMAddressReverseMapping = [] {
    std::array<std::array<uint32, kVDP2CRAMSize>, 2> addrs{};
    for (uint32 addr = 0; addr < kVDP2CRAMSize; addr++) {
        addrs[0][addr] = addr;
        addrs[1][addr] = (bit::extract<1, 10>(addr) << 2u) | (bit::extract<11>(addr) << 1u) | bit::extract<0>(addr);
    }
    return addrs;
}();

// Ensure the forward and reverse mappings are 1:1
/*static_assert([] {
    for (uint32 addr = 0; addr < kVDP2CRAMSize; addr++) {
        for (uint32 i = 0; i < 2; i++) {
            const uint32 fwd = kVDP2CRAMAddressMapping[i][addr];
            const uint32 rev = kVDP2CRAMAddressReverseMapping[i][addr];
            const uint32 fwdRev = kVDP2CRAMAddressReverseMapping[i][fwd];
            const uint32 revFwd = kVDP2CRAMAddressMapping[i][rev];
            if (fwdRev != revFwd) {
                return false;
            }
            if (fwdRev != addr) {
                return false;
            }
        }
    }
    return true;
}());*/

// -----------------------------------------------------------------------------
// Display phases

enum class HorizontalPhase { Active, RightBorder, Sync, LeftBorder };
enum class VerticalPhase { Active, BottomBorder, BlankingAndSync, VCounterSkip, TopBorder, LastLine };

// -----------------------------------------------------------------------------
// Layers

enum class Layer { Sprite, RBG0, NBG0_RBG1, NBG1_EXBG, NBG2, NBG3 };

// -----------------------------------------------------------------------------
// VDP2 data structures

// Character color formats
enum class ColorFormat : uint8 {
    Palette16,
    Palette256,
    Palette2048,
    RGB555,
    RGB888,
};

inline constexpr bool IsPaletteColorFormat(ColorFormat format) {
    using enum ColorFormat;
    return format == Palette16 || format == Palette256 || format == Palette2048;
}

// Special priority modes
enum class PriorityMode : uint8 {
    PerScreen,
    PerCharacter,
    PerDot,
};

// Special color calculation modes
enum class SpecialColorCalcMode : uint8 {
    PerScreen,
    PerCharacter,
    PerDot,
    ColorDataMSB,
};

enum class WindowLogic : uint8 { Or, And };

template <bool hasSpriteWindow>
struct WindowSet {
    WindowSet() {
        Reset();
    }

    void Reset() {
        enabled.fill(false);
        inverted.fill(false);
        logic = WindowLogic::Or;
    }

    static constexpr uint32 kNumWindows = hasSpriteWindow ? 3 : 2;

    // Window enable flags for:
    // [0] Window 0
    // [1] Window 1
    // [2] Sprite Window  (if hasSpriteWindow is true)
    // Derived from WCTLA/B/C/D.xxW0E, xxW1E and xxSWE
    std::array<bool, kNumWindows> enabled;

    // Determines if the active area of the window is inside (false) or outside (true) for:
    // [0] Window 0
    // [1] Window 1
    // [2] Sprite Window  (if hasSpriteWindow is true)
    // Derived from WCTLA/B/C/D.xxW0A, xxW1A and xxSWA
    std::array<bool, kNumWindows> inverted;

    // Window combination logic mode.
    // Derived from WCTLA/B/C/D.xxLOG
    WindowLogic logic;
};

// Map index mask lookup table
// [Character Size][Pattern Name Data Size ^ 1][Plane Size]
inline constexpr uint32 kMapIndexMasks[2][2][4] = {
    {{0x7F, 0x7E, 0x7E, 0x7C}, {0x3F, 0x3E, 0x3E, 0x3C}},
    {{0x1FF, 0x1FE, 0x1FE, 0x1FC}, {0xFF, 0xFE, 0xFE, 0xFC}},
};

// Page sizes lookup table
// [Character Size][Pattern Name Data Size ^ 1]
inline constexpr uint32 kPageSizes[2][2] = {
    {13, 14},
    {11, 12},
};

// Calculates the base address of character pages based on the following parameters:
// - chsz: character size (cellSizeShift)
// - pnds: pattern name data size (twoWordChar)
// - plsz: plane size
// - mapIndex: map index set by MPOFN and MPABN0-MPCDN3 for NBGs or MPOFR and MPABRA-MPOPRB for RBGs
inline constexpr uint32 CalcPageBaseAddress(uint32 chsz, uint32 pnds, uint32 plsz, uint32 mapIndex) {
    const uint32 mapIndexMask = kMapIndexMasks[chsz][pnds][plsz];
    const uint32 pageSizeShift = kPageSizes[chsz][pnds];
    return (mapIndex & mapIndexMask) << pageSizeShift;
}

// NBG and RBG parameters.
struct alignas(128) BGParams {
    BGParams() {
        Reset();
    }

    void Reset() {
        enableTransparency = false;
        bitmap = false;

        lineColorScreenEnable = false;

        priorityNumber = 0;
        priorityMode = PriorityMode::PerScreen;
        specialFunctionSelect = 0;

        cellSizeShift = 0;

        pageShiftH = 0;
        pageShiftV = 0;
        bitmapSizeH = 512;
        bitmapSizeV = 256;

        scrollAmountH = 0;
        scrollAmountV = 0;

        scrollIncH = 1u << 8u;
        scrollIncV = 1u << 8u;

        mapIndices.fill(0);
        pageBaseAddresses.fill(0);
        bitmapBaseAddress = 0;

        colorFormat = ColorFormat::Palette16;

        cramOffset = 0;

        supplScrollCharNum = 0;
        supplScrollPalNum = 0;
        supplScrollSpecialColorCalc = false;
        supplScrollSpecialPriority = false;

        supplBitmapPalNum = 0;
        supplBitmapSpecialColorCalc = false;
        supplBitmapSpecialPriority = false;

        extChar = false;
        twoWordChar = false;

        patNameAccess.fill(false);
        charPatAccess.fill(false);
        charPatDelay.fill(false);
        vramDataOffset.fill(0);

        vcellScrollEnable = false;
        lineScrollXEnable = false;
        lineScrollYEnable = false;
        lineZoomEnable = false;
        lineScrollInterval = 0;
        lineScrollTableAddress = 0;

        mosaicEnable = false;

        colorCalcEnable = false;
        colorCalcRatio = 31;
        specialColorCalcMode = SpecialColorCalcMode::PerScreen;

        windowSet.Reset();

        shadowEnable = false;

        plsz = 0;
        bmsz = 0;

        rbgPageBaseAddressesDirty = true;
    }

    // If true, honor transparency bit in color data.
    // Derived from BGON.xxTPON
    bool enableTransparency;

    // Whether the background uses cells (false) or a bitmap (true).
    // Derived from CHCTLA/CHCTLB.xxBMEN
    bool bitmap;

    // Enables LNCL screen insertion if this BG is the topmost layer.
    // Derived from LNCLEN.xxLCEN
    bool lineColorScreenEnable;

    // Priority number from 0 (transparent) to 7 (highest).
    // Derived from PRINA/PRINB/PRIR.xxPRINn
    uint8 priorityNumber;

    // Special priority mode.
    // Derived from SFPRMD.xxSPRMn
    PriorityMode priorityMode;

    // Special function select (0=A, 1=B).
    // Derived from SFSEL.xxSFCS
    uint8 specialFunctionSelect;

    // Cell size shift corresponding to the dimensions of a character pattern (0=1x1, 1=2x2).
    // Derived from CHCTLA/CHCTLB.xxCHSZ
    uint32 cellSizeShift;

    // Page shifts are either 0 or 1, used when determining which plane a particular (x,y) coordinate belongs to.
    // A shift of 0 corresponds to 1 page per plane dimension.
    // A shift of 1 corresponds to 2 pages per plane dimension.
    // Only valid for NBGs.
    uint32 pageShiftH; // Horizontal page shift, derived from PLSZ.xxPLSZn
    uint32 pageShiftV; // Vertical page shift, derived from PLSZ.xxPLSZn

    // Bitmap dimensions, when the screen is in bitmap mode.
    // Only valid for NBGs.
    uint32 bitmapSizeH; // Horizontal bitmap dots, derived from CHCTLA/CHCTLB.xxBMSZ
    uint32 bitmapSizeV; // Vertical bitmap dots, derived from CHCTLA/CHCTLB.xxBMSZ

    // Screen scroll amount, in 11.8 fixed-point format.
    // Used in scroll NBGs.
    // Scroll amounts for NBGs 2 and 3 do not have a fractional part, but the values are still stored with 8 fractional
    // bits here for consistency and ease of implementation.
    uint32 scrollAmountH; // Horizontal scroll amount in 11.8 fixed-point format, derived from SCXINn and SCXDNn
    uint32 scrollAmountV; // Vertical scroll amount in 11.8 fixed-point format, derived from SCYINn and SCYDNn

    // Screen scroll increment per pixel, in 11.8 fixed-point format.
    // NBGs 2 and 3 do not have increment registers; they always increment each coordinate by 1.0, which is stored here
    // for consistency and ease of implementation.
    uint32 scrollIncH; // Horizontal scroll increment in 11.8 fixed-point format, derived from ZMXINn and ZMXDNn
    uint32 scrollIncV; // Vertical scroll increment in 11.8 fixed-point format, derived from ZMYINn and ZMYDNn

    // Indices for NBG planes A-D, derived from MPOFN and MPABN0-MPCDN3.
    std::array<uint16, 4> mapIndices;

    // Page base addresses for NBG planes A-D.
    // Derived from mapIndices, CHCTLA/CHCTLB.xxCHSZ, PNCNn.xxPNB and PLSZ.xxPLSZn
    std::array<uint32, 4> pageBaseAddresses;

    // Base address of bitmap data.
    // Only valid for NBGs.
    // Derived from MPOFN
    uint32 bitmapBaseAddress;

    // Character color format.
    // Derived from CHCTLA/CHCTLB.xxCHCNn
    ColorFormat colorFormat;

    // Color RAM base offset.
    // Derived from CRAOFA/CRAOFB.xxCAOSn
    uint32 cramOffset;

    // Supplementary bits 4-0 for scroll screen character number, when using 1-word characters.
    // Derived from PNCNn/PNCR.xxSCNn
    uint32 supplScrollCharNum;

    // Supplementary bits 6-4 for scroll screen palette number, when using 1-word characters.
    // The value is already shifted in place to optimize rendering calculations.
    // Derived from PNCNn/PNCR.xxSPLTn
    uint32 supplScrollPalNum;

    // Bits 6-4 for bitmap palette number.
    // The value is already shifted in place to optimize rendering calculations.
    // Derived from BMPNA/BMPNB.xxBMPn
    uint32 supplBitmapPalNum;

    // Supplementary Special Color Calculation bit for scroll BGs.
    // Derived from PNCNn/PNCR.xxSCC
    bool supplScrollSpecialColorCalc;

    // Supplementary Special Priority bit for scroll BGs.
    // Derived from PNCNn/PNCR.xxSPR
    bool supplScrollSpecialPriority;

    // Supplementary Special Color Calculation bit for bitmap BGs.
    // Derived from BMPNA/BMPNB.xxBMCC
    bool supplBitmapSpecialColorCalc;

    // Supplementary Special Priority bit for bitmap BGs.
    // Derived from BMPNA/BMPNB.xxBMPR
    bool supplBitmapSpecialPriority;

    // Character number width: 10 bits (false) or 12 bits (true).
    // When true, disables the horizontal and vertical flip bits in the character.
    // Derived from PNCNn/PNCR.xxCNSM
    bool extChar;

    // Whether characters use one (false) or two (true) words.
    // Derived from PNCNn/PNCR.xxPNB
    bool twoWordChar;

    // Whether pattern name data is accessible for each VRAM bank (A0, A1, B0, B1).
    // Derived from CYCxn, RAMCTL and BGON (for RBG0/1 restrictions to NBGs)
    std::array<bool, 4> patNameAccess;

    // Whether character pattern data is accessible for each VRAM bank (A0, A1, B0, B1).
    // Derived from CYCxn, RAMCTL and BGON (for RBG0/1 restrictions to NBGs)
    std::array<bool, 4> charPatAccess;

    // Whether accesses to character pattern data for this background is delayed per bank due to illegal VRAM access
    // patterns.
    // Derived from CYCxn, RAMCTL, ZMCTL and CHCTLA/CHCTLB.xxCHSZ
    std::array<bool, 4> charPatDelay;

    // Address offset for VRAM data for this background on each VRAM bank caused by illegal VRAM access patterns.
    // Derived from CYCxn, RAMCTL and ZMCTL
    std::array<uint32, 4> vramDataOffset;

    // Whether to use the vertical cell scroll table in VRAM.
    // Only valid for NBG0 and NBG1.
    // Derived from SCRCTL.NnVCSC
    bool vcellScrollEnable;

    // Whether to use the horizontal line scroll table in VRAM.
    // Only valid for NBG0 and NBG1.
    // Derived from SCRCTL.NnLSCX
    bool lineScrollXEnable;

    // Whether to use the vertical line scroll table in VRAM.
    // Only valid for NBG0 and NBG1.
    // Derived from SCRCTL.NnLSCY
    bool lineScrollYEnable;

    // Whether to use horizontal line zoom/scaling.
    // Only valid for NBG0 and NBG1.
    // Derived from SCRCTL.NnLZMX
    bool lineZoomEnable;

    // Line scroll table interval shift. The interval is calculated as (1 << lineScrollInterval).
    // Only valid for NBG0 and NBG1.
    // Derived from SCRCTL.NnLSS1-0
    uint8 lineScrollInterval;

    // Line scroll table base address.
    // Only valid for NBG0 and NBG1.
    // Derived from LSTAnU/L
    uint32 lineScrollTableAddress;

    // Enables the mosaic effect.
    // If vertical cell scroll is also enabled, the mosaic effect is bypassed.
    // Derived from MZCTL.xxMZE
    bool mosaicEnable;

    // Enables color calculation.
    // Derived from CCCTL.xxCCEN
    bool colorCalcEnable;

    // Color calculation ratio, ranging from 31:1 to 0:32.
    // The ratio is calculated as (32-colorCalcRatio) : (colorCalcRatio).
    // Derived from CCRNA/B.NxCCRTn
    uint8 colorCalcRatio;

    // Special color calculation mode.
    // Derived from SFCCMD.xxSCCMn
    SpecialColorCalcMode specialColorCalcMode;

    // Window parameters.
    // Derived from WCTLA/B/C/D
    WindowSet<true> windowSet;

    // Enable shadow rendering on this background layer.
    // Derived from SDCTL.xxSDEN
    bool shadowEnable;

    // Raw register values, to facilitate reads.
    uint16 plsz; // Raw value of PLSZ.xxPLSZn
    uint16 bmsz; // Raw value of CHCTLA/CHCTLB.xxBMSZ

    // Dirty flags

    // Whether the page base addresses for RBGs need to be recalculated.
    // Set when any of the following is changed:
    // - CHCTLB.R0CHSZ (RBG0) or CHCTLA.N0CHSZ (RBG1)
    // - PNCR.R0PNB (RBG0) or PNCN0.N0PNB (RBG1)
    // - PLSZ.RxPLSZn
    // - MPOFR
    // - MPABRA-MPOPRB
    bool rbgPageBaseAddressesDirty;

    void UpdatePLSZ() {
        pageShiftH = plsz & 1;
        pageShiftV = plsz >> 1;

        UpdatePageBaseAddresses();
    }

    void UpdateCHCTL() {
        static constexpr uint32 kBitmapSizesH[] = {512, 512, 1024, 1024};
        static constexpr uint32 kBitmapSizesV[] = {256, 512, 256, 512};
        bitmapSizeH = kBitmapSizesH[bmsz];
        bitmapSizeV = kBitmapSizesV[bmsz];

        UpdatePageBaseAddresses();
    }

    void UpdatePageBaseAddresses() {
        for (int i = 0; i < pageBaseAddresses.size(); i++) {
            pageBaseAddresses[i] = CalcPageBaseAddress(cellSizeShift, twoWordChar, plsz, mapIndices[i]);
        }
    }
};

enum class RotationParamMode : uint8 { RotationParamA, RotationParamB, Coefficient, Window };

struct CommonRotationParams {
    CommonRotationParams() {
        Reset();
    }

    void Reset() {
        baseAddress = 0;
        rotParamMode = RotationParamMode::RotationParamA;
        windowSet.Reset();
    }

    // Rotation parameters table base address.
    // Derived from RPTAU/L.RPTA18-1
    uint32 baseAddress;

    // Rotation parameter mode.
    // Derived from RPMD.RPMD1-0
    RotationParamMode rotParamMode;

    // Window parameters.
    // Derived from WCTLA/B/C/D
    WindowSet<false> windowSet;
};

enum class CoefficientDataMode : uint8 { ScaleCoeffXY, ScaleCoeffX, ScaleCoeffY, ViewpointX };
enum class ScreenOverProcess : uint8 { Repeat, RepeatChar, Transparent, Fixed512 };

// Rotation Parameter A/B
struct RotationParams {
    RotationParams() {
        Reset();
    }

    void Reset() {
        readXst = false;
        readYst = false;
        readKAst = false;

        coeffTableEnable = false;
        coeffDataSize = 0;
        coeffUseLineColorData = false;
        coeffTableAddressOffset = 0;

        screenOverProcess = ScreenOverProcess::Repeat;
        screenOverPatternName = 0;

        pageShiftH = 0;
        pageShiftV = 0;

        mapIndices.fill(0);

        bitmapBaseAddress = 0;

        plsz = 0;
    }

    void UpdatePLSZ() {
        pageShiftH = plsz & 1;
        pageShiftV = plsz >> 1;
    }

    // Read Xst on the next scanline. Automatically cleared when read.
    // Derived from RPRCTL.RxXSTRE
    bool readXst;

    // Read Yst on the next scanline. Automatically cleared when read.
    // Derived from RPRCTL.RxYSTRE
    bool readYst;

    // Read KAst on the next scanline. Automatically cleared when read.
    // Derived from RPRCTL.RxKASTRE
    bool readKAst;

    // Enable use of the coefficient table.
    // Derived from KTCTL.RxKTE
    bool coeffTableEnable;

    // Size of coefficient data: 2 words (0) or 1 word (1).
    // Derived from KTCTL.RxKDBS
    uint8 coeffDataSize;

    // Coefficient data mode.
    // Derived from KTCTL.RxKMD1-0
    CoefficientDataMode coeffDataMode = CoefficientDataMode::ScaleCoeffXY;

    // Enables use of line color data within coefficient data.
    // Derived from KTCTL.RxKLCE
    bool coeffUseLineColorData;

    // Coefficient table address offset.
    // Derived from KTAOF.RxKTAOS2-0
    uint32 coeffTableAddressOffset;

    // Rotation BG screen-over process.
    // Derived from PLSZ.RxOVRn
    ScreenOverProcess screenOverProcess;

    // Screen-over pattern name value.
    // Derived from OVPNRA/B
    uint16 screenOverPatternName;

    // Page shifts are either 0 or 1, used when determining which plane a particular (x,y) coordinate belongs to.
    // A shift of 0 corresponds to 1 page per plane dimension.
    // A shift of 1 corresponds to 2 pages per plane dimension.
    uint32 pageShiftH; // Horizontal page shift, derived from PLSZ.xxPLSZn
    uint32 pageShiftV; // Vertical page shift, derived from PLSZ.xxPLSZn

    // Indices for RBG planes A-P, derived from MPOFR and MPABRA-MPOPRB.
    std::array<uint16, 16> mapIndices;

    // Base address of bitmap data.
    // Derived from MPOFR
    uint32 bitmapBaseAddress;

    // Raw register values, to facilitate reads.
    uint16 plsz; // Raw value of PLSZ.xxPLSZn
};

struct RotationParamTable {
    void ReadFrom(const uint8 *input) {
        Xst = bit::extract_signed<6, 28, sint32>(util::ReadBE<uint32>(&input[0x00]));
        Yst = bit::extract_signed<6, 28, sint32>(util::ReadBE<uint32>(&input[0x04]));
        Zst = bit::extract_signed<6, 28, sint32>(util::ReadBE<uint32>(&input[0x08]));

        deltaXst = bit::extract_signed<6, 18, sint32>(util::ReadBE<uint32>(&input[0x0C]));
        deltaYst = bit::extract_signed<6, 18, sint32>(util::ReadBE<uint32>(&input[0x10]));

        deltaX = bit::extract_signed<6, 18, sint32>(util::ReadBE<uint32>(&input[0x14]));
        deltaY = bit::extract_signed<6, 18, sint32>(util::ReadBE<uint32>(&input[0x18]));

        A = bit::extract_signed<6, 19, sint32>(util::ReadBE<uint32>(&input[0x1C]));
        B = bit::extract_signed<6, 19, sint32>(util::ReadBE<uint32>(&input[0x20]));
        C = bit::extract_signed<6, 19, sint32>(util::ReadBE<uint32>(&input[0x24]));
        D = bit::extract_signed<6, 19, sint32>(util::ReadBE<uint32>(&input[0x28]));
        E = bit::extract_signed<6, 19, sint32>(util::ReadBE<uint32>(&input[0x2C]));
        F = bit::extract_signed<6, 19, sint32>(util::ReadBE<uint32>(&input[0x30]));

        Px = bit::extract_signed<0, 13, sint32>(util::ReadBE<uint16>(&input[0x34]));
        Py = bit::extract_signed<0, 13, sint32>(util::ReadBE<uint16>(&input[0x36]));
        Pz = bit::extract_signed<0, 13, sint32>(util::ReadBE<uint16>(&input[0x38]));

        Cx = bit::extract_signed<0, 13, sint32>(util::ReadBE<uint16>(&input[0x3C]));
        Cy = bit::extract_signed<0, 13, sint32>(util::ReadBE<uint16>(&input[0x3E]));
        Cz = bit::extract_signed<0, 13, sint32>(util::ReadBE<uint16>(&input[0x40]));

        Mx = bit::extract_signed<6, 29, sint32>(util::ReadBE<uint32>(&input[0x44]));
        My = bit::extract_signed<6, 29, sint32>(util::ReadBE<uint32>(&input[0x48]));

        kx = bit::extract_signed<0, 23, sint32>(util::ReadBE<uint32>(&input[0x4C]));
        ky = bit::extract_signed<0, 23, sint32>(util::ReadBE<uint32>(&input[0x50]));

        KAst = bit::extract<6, 31>(util::ReadBE<uint32>(&input[0x54]));
        dKAst = bit::extract_signed<6, 25>(util::ReadBE<uint32>(&input[0x58]));
        dKAx = bit::extract_signed<6, 25>(util::ReadBE<uint32>(&input[0x5C]));
    }

    // Screen start coordinates (signed 13.10 fixed point)
    sint32 Xst, Yst, Zst;

    // Screen vertical coordinate increments (signed 3.10 fixed point)
    sint32 deltaXst, deltaYst;

    // Screen horizontal coordinate increments (signed 3.10 fixed point)
    sint32 deltaX, deltaY;

    // Rotation matrix parameters (signed 4.10 fixed point)
    sint32 A, B, C, D, E, F;

    // Viewpoint coordinates (signed 14-bit integer)
    sint32 Px, Py, Pz;

    // Center point coordinates (signed 14-bit integer)
    sint32 Cx, Cy, Cz;

    // Horizontal shift (signed 14.10 fixed point)
    sint32 Mx, My;

    // Scaling coefficients (signed 8.16 fixed point)
    sint32 kx, ky;

    // Coefficient table parameters
    uint32 KAst;  // Coefficient table start address (unsigned 16.10 fixed point)
    sint32 dKAst; // Coefficient table vertical increment (signed 10.10 fixed point)
    sint32 dKAx;  // Coefficient table horizontal increment (signed 10.10 fixed point)
};

// Rotation coefficient entry.
struct Coefficient {
    sint32 value = 0; // coefficient value, scaled to 16 fractional bits
    uint8 lineColorData = 0;
    bool transparent = true;
};

enum class SpriteColorCalculationCondition : uint8 {
    PriorityLessThanOrEqual,
    PriorityEqual,
    PriorityGreaterThanOrEqual,
    MsbEqualsOne,
};

struct SpriteParams {
    SpriteParams() {
        Reset();
    }

    void Reset() {
        type = 0;
        useSpriteWindow = false;
        mixedFormat = false;
        colorCalcEnable = false;
        colorCalcValue = 0;
        colorCalcCond = SpriteColorCalculationCondition::PriorityLessThanOrEqual;
        priorities.fill(0);
        colorCalcRatios.fill(31);
        colorDataOffset = 0;
        lineColorScreenEnable = false;
        windowSet.Reset();
        spriteWindowEnabled = false;
        spriteWindowInverted = false;
    }

    // The sprite type (0..F).
    // Derived from SPCTL.SPTYPE3-0
    uint8 type;

    // Whether sprite window is in use.
    // Derived from SPCTL.SPWINEN
    bool useSpriteWindow;

    // Whether sprite data uses palette only (false) or mixed palette/RGB (true) data.
    // Derived from SPCTL.SPCLMD
    bool mixedFormat;

    // Enables color calculation.
    // Derived from CCCTL.SPCCEN
    bool colorCalcEnable;

    // The color calculation value to compare against the priority number of sprites.
    // Derived from SPCTL.SPCCN2-0
    uint8 colorCalcValue;

    // The color calculation condition.
    // Derived from SPCTL.SPCCCS1-0
    SpriteColorCalculationCondition colorCalcCond;

    // Sprite priority numbers for registers 0-7.
    // Derived from PRISA, PRISB, PRISC and PRISD.
    std::array<uint8, 8> priorities;

    // Sprite color calculation ratios for registers 0-7, ranging from 31:1 to 0:32.
    // The ratio is calculated as (32-colorCalcRatio) : (colorCalcRatio).
    // Derived from CCRSA, CCRSB, CCRSC and CCRSD.
    std::array<uint8, 8> colorCalcRatios;

    // Sprite color data offset.
    // Derived from CRAOFB.SPCAOSn
    uint32 colorDataOffset;

    // Enables LNCL screen insertion if this BG is the topmost layer.
    // Derived from LNCLEN.SPLCEN
    bool lineColorScreenEnable;

    // Window parameters (except sprite window).
    // Derived from WCTLA/B/C/D
    WindowSet<false> windowSet;

    // Sprite window enable flag for the sprite layer.
    // Derived from WCTLC.SPSWE
    bool spriteWindowEnabled;

    // Determines if the active area of the sprite window is inside (false) or outside (true) for the sprite layer.
    // Derived from WCTLC.SPSWA
    bool spriteWindowInverted;
};

struct SpriteData {
    enum class Special : uint8 {
        Normal,      // Any other value
        Shadow,      // Normal shadow pattern (DC=0b...11110)
        Transparent, // Raw 16-bit value is 0x0000
    };

    uint16 colorData = 0;              // DC10-0
    uint8 colorCalcRatio = 0;          // CC2-0
    uint8 priority = 0;                // PR2-0
    bool shadowOrWindow = false;       // SD
    Special special = Special::Normal; // Color data special patterns
};

// Special Function Codes, derived from SFCODE.
struct SpecialFunctionCodes {
    SpecialFunctionCodes() {
        Reset();
    }

    void Reset() {
        colorMatches.fill(0);
    }

    // If the entry indexed by bits 3-1 of the color code is true, the special function is applied to the pixel
    std::array<bool, 8> colorMatches;
};

struct LineBackScreenParams {
    LineBackScreenParams() {
        Reset();
    }

    void Reset() {
        perLine = false;
        baseAddress = 0;
        colorCalcEnable = false;
        colorCalcRatio = 0;
        shadowEnable = false;
    }

    // Whether the line/back screen specifies a color for the whole screen (false) or per line (true).
    // Derived from LCTAU.LCCLMD or BKTAU.BKCLMD
    bool perLine;

    // Base address of line/back screen data.
    // Derived from LCTAU/L.LCTA18-0 or BKTAU/L.BKTA18-0
    uint32 baseAddress;

    // Enables color calculation.
    // Derived from CCCTL.LCCCEN
    bool colorCalcEnable;

    // Color calculation ratio, ranging from 31:1 to 0:32.
    // The ratio is calculated as (32-colorCalcRatio) : (colorCalcRatio).
    // Derived from CCRLB.xxCCRTn
    uint8 colorCalcRatio;

    // Enable shadow rendering on this background layer.
    // Derived from SDCTL.xxSDEN
    bool shadowEnable;
};

struct ColorOffsetParams {
    ColorOffsetParams() {
        Reset();
    }

    void Reset() {
        enable = false;
        select = false;
    }

    // Enables the color offset effect.
    // Derived from CLOFEN.xxCOEN
    bool enable;

    // Selects the color offset parameters to use: A (false) or B (true).
    // Derived from CLOFSL.xxCOSL
    bool select;
};

struct ColorOffset {
    ColorOffset() {
        Reset();
    }

    void Reset() {
        r = 0;
        g = 0;
        b = 0;
        nonZero = false;
    }

    // Color offset values as unsigned 9-bit indices into a lookup table.
    // Derived from COAR/G/B and COBR/G/B
    uint16 r;
    uint16 g;
    uint16 b;

    // Set when at least one of the offsets is non-zero.
    bool nonZero;
};

enum class ColorGradScreen : uint8 { Sprite, RBG0, NBG0_RBG1, Invalid3, NBG1_EXBG, NBG2, NBG3, Invalid7 };

struct ColorCalcParams {
    ColorCalcParams() {
        Reset();
    }

    void Reset() {
        colorGradEnable = false;
        colorGradScreen = ColorGradScreen::Sprite;
        extendedColorCalcEnable = false;
        useSecondScreenRatio = false;
        useAdditiveBlend = false;
        windowSet.Reset();
    }

    // Enables color gradation.
    // Derived from CCCTL.BOKEN
    bool colorGradEnable;

    // Which screen to apply the color gradation function.
    // Derived from CCCTL.BOKN2-0
    ColorGradScreen colorGradScreen;

    // Enables extended color calculation.
    // Derived from CCCTL.EXCCEN
    bool extendedColorCalcEnable;

    // Use the ratio from the first (false) or second (true) topmost screen.
    // Derived from CCCTL.CCRTMD
    bool useSecondScreenRatio;

    // Whether to use alpha (false) or additive (true) blending.
    // Derived from CCCTL.CCMD
    bool useAdditiveBlend;

    // Window parameters.
    // Derived from WCTLA/B/C/D
    WindowSet<true> windowSet;
};

struct WindowParams {
    WindowParams() {
        Reset();
    }

    void Reset() {
        startX = startY = 0;
        endX = endY = 0;
        lineWindowTableEnable = false;
        lineWindowTableAddress = 0;
    }

    // Starting window coordinates.
    // Derived from WPSXn/WPSYn
    uint16 startX, startY;

    // Ending window coordinates.
    // Derived from WPEXn/WPEYn
    uint16 endX, endY;

    // Enables use of the line window table.
    // Derived from LWTAnU.WxLWE
    bool lineWindowTableEnable;

    // Base address of the line window table.
    // Derived from LWTAnU/L.WxLWTAn
    uint32 lineWindowTableAddress;
};

enum class RotDataBankSel : uint8 { Unused, Coefficients, PatternName, Character };

struct VRAMControl {
    VRAMControl() {
        Reset();
    }

    void Reset() {
        rotDataBankSelA0 = RotDataBankSel::Unused;
        rotDataBankSelA1 = RotDataBankSel::Unused;
        rotDataBankSelB0 = RotDataBankSel::Unused;
        rotDataBankSelB1 = RotDataBankSel::Unused;
        partitionVRAMA = false;
        partitionVRAMB = false;
        colorRAMMode = 0;
        colorRAMCoeffTableEnable = false;
        UpdateDerivedValues();
    }

    [[nodiscard]] RotDataBankSel GetRotDataBankSel(uint32 bank) const {
        assert(bank < 4);
        switch (bank) {
        case 0: return rotDataBankSelA0;
        case 1: return rotDataBankSelA1;
        case 2: return rotDataBankSelB0;
        case 3: return rotDataBankSelB1;
        default: util::unreachable();
        }
    }

    // Select VRAM bank usage for rotation parameters:
    //   0 = bank not used by rotation backgrounds
    //   1 = bank used for coefficient table
    //   2 = bank used for pattern name table
    //   3 = bank used for character/bitmap pattern table
    // Derived from RDBS(A-B)(0-1)(1-0)
    RotDataBankSel rotDataBankSelA0; // Rotation data bank select for VRAM-A0 or VRAM-A
    RotDataBankSel rotDataBankSelA1; // Rotation data bank select for VRAM-A1
    RotDataBankSel rotDataBankSelB0; // Rotation data bank select for VRAM-B0 or VRAM-B
    RotDataBankSel rotDataBankSelB1; // Rotation data bank select for VRAM-B1

    // If set, partition VRAM A into two blocks: A0 and A1.
    // Derived from RAMCTL.VRAMD
    bool partitionVRAMA;

    // If set, partition VRAM B into two blocks: B0 and B1.
    // Derived from RAMCTL.VRBMD
    bool partitionVRAMB;

    // Selects color RAM mode:
    //   0 = RGB 5:5:5, 1024 words
    //   1 = RGB 5:5:5, 2048 words
    //   2 = RGB 8:8:8, 1024 words
    //   3 = RGB 8:8:8, 1024 words  (same as mode 2, undocumented)
    // Derived from RAMCTL.CRMD1-0
    uint8 colorRAMMode;

    // Enables use of coefficient tables in CRAM.
    // Derived from RAMCTL.CRKTE
    bool colorRAMCoeffTableEnable;

    // Determines if rotation parameter coefficients are per dot (true) or per line (false).
    // Derived from RAMCTL.VRAMD, VRBMD and CRKTE
    bool perDotRotationCoeffs;

    void UpdateDerivedValues() {
        perDotRotationCoeffs = colorRAMCoeffTableEnable;
        if (!perDotRotationCoeffs) {
            perDotRotationCoeffs =
                rotDataBankSelA0 == RotDataBankSel::Coefficients || rotDataBankSelB0 == RotDataBankSel::Coefficients;
            if (partitionVRAMA) {
                perDotRotationCoeffs |= rotDataBankSelA1 == RotDataBankSel::Coefficients;
            }
            if (partitionVRAMB) {
                perDotRotationCoeffs |= rotDataBankSelB1 == RotDataBankSel::Coefficients;
            }
        }
    }
};

enum class InterlaceMode : uint16 { None, Invalid, SingleDensity, DoubleDensity };

// 180000   TVMD    TV Screen Mode
//
//   bits   r/w  code          description
//     15   R/W  DISP          TV Screen Display (0=no display, 1=display)
//   14-9        -             Reserved, must be zero
//      8   R/W  BDCLMD        Border Color Mode (0=black, 1=back screen)
//    7-6   R/W  LSMD1-0       Interlace Mode
//                               00 (0) = Non-Interlace
//                               01 (1) = (Forbidden)
//                               10 (2) = Single-density interlace
//                               11 (3) = Double-density interlace
//    5-4   R/W  VRESO1-0      Vertical Resolution
//                               00 (0) = 224 lines (NTSC or PAL)
//                               01 (1) = 240 lines (NTSC or PAL)
//                               10 (2) = 256 lines (PAL only)
//                               11 (3) = (Forbidden)
//      3        -             Reserved, must be zero
//    2-0   R/W  HRESO2-0      Horizontal Resolution
//                               000 (0) = 320 pixels - Normal Graphic A (NTSC or PAL)
//                               001 (1) = 352 pixels - Normal Graphic B (NTSC or PAL)
//                               010 (2) = 640 pixels - Hi-Res Graphic A (NTSC or PAL)
//                               011 (3) = 704 pixels - Hi-Res Graphic B (NTSC or PAL)
//                               100 (4) = 320 pixels - Exclusive Normal Graphic A (31 KHz monitor)
//                               101 (5) = 352 pixels - Exclusive Normal Graphic B (Hi-Vision monitor)
//                               110 (6) = 640 pixels - Exclusive Hi-Res Graphic A (31 KHz monitor)
//                               111 (7) = 704 pixels - Exclusive Hi-Res Graphic B (Hi-Vision monitor)
union RegTVMD {
    uint16 u16;
    struct {
        uint16 HRESOn : 3;
        uint16 _rsvd3 : 1;
        uint16 VRESOn : 2;
        InterlaceMode LSMDn : 2;
        uint16 BDCLMD : 1;
        uint16 _rsvd9_14 : 6;
        uint16 DISP : 1;
    };

    [[nodiscard]] FORCE_INLINE bool IsInterlaced() const noexcept {
        return LSMDn != InterlaceMode::None;
    }
};
static_assert(sizeof(RegTVMD) == sizeof(uint16));

// 180002   EXTEN   External Signal Enable
//
//   bits   r/w  code          description
//  15-10        -             Reserved, must be zero
//      9   R/W  EXLTEN        External Latch Enable (0=on read, 1=on external signal)
//      8   R/W  EXSYEN        External Sync Enable (0=disable, 1=enable)
//    7-2        -             Reserved, must be zero
//      1   R/W  DASEL         Display Area Select (0=selected area, 1=full screen)
//      0   R/W  EXBGEN        External BG Enable (0=disable, 1=enable)
union RegEXTEN {
    uint16 u16;
    struct {
        uint16 EXBGEN : 1;
        uint16 DASEL : 1;
        uint16 _rsvd2_7 : 6;
        uint16 EXSYEN : 1;
        uint16 EXLTEN : 1;
        uint16 _rsvd10_15 : 6;
    };
};

// 180004   TVSTAT  Screen Status
//
//   bits   r/w  code          description
//  15-10        -             Reserved, must be zero
//      9   R    EXLTFG        External Latch Flag (0=not latched, 1=latched)
//      8   R    EXSYFG        External Sync Flag (0=not synced, 1=synced)
//    7-4        -             Reserved, must be zero
//      3   R    VBLANK        Vertical Blank Flag (0=vertical scan, 1=vertical retrace)
//      2   R    HBLANK        Horizontal Blank Flag (0=horizontal scan, 1=horizontal retrace)
//      1   R    ODD           Scan Field Flag (0=even, 1=odd)
//      0   R    PAL           TV Standard Flag (0=NTSC, 1=PAL)
union RegTVSTAT {
    uint16 u16 = 0;
    struct {
        uint16 PAL : 1;
        uint16 ODD : 1;
        uint16 HBLANK : 1;
        uint16 VBLANK : 1;
        uint16 _rsvd4_7 : 4;
        uint16 EXSYFG : 1;
        mutable uint16 EXLTFG : 1; // cleared on reads
        uint16 _rsvd10_15 : 6;
    };
};

// 180006   VRSIZE  VRAM Size
//
//   bits   r/w  code          description
//     15   R/W  VRAMSZ        VRAM Size (0=512 KiB, 1=1 MiB)
//   14-4        -             Reserved, must be zero
//    3-0   R    VER3-0        VDP2 Version Number
union RegVRSIZE {
    uint16 u16;
    struct {
        uint16 VERn : 4;
        uint16 _rsvd4_14 : 11;
        uint16 VRAMSZ : 1;
    };
};

// 180010   CYCA0L  VRAM Cycle Pattern A0 Lower
// 180012   CYCA0U  VRAM Cycle Pattern A0 Upper
// 180014   CYCA1L  VRAM Cycle Pattern A1 Lower
// 180016   CYCA1U  VRAM Cycle Pattern A1 Upper
// 180018   CYCB0L  VRAM Cycle Pattern B0 Lower
// 18001A   CYCB0U  VRAM Cycle Pattern B0 Upper
// 18001C   CYCB1L  VRAM Cycle Pattern B1 Lower
// 18001E   CYCB1U  VRAM Cycle Pattern B1 Upper
struct CyclePatterns {
    enum Type {
        PatNameNBG0,
        PatNameNBG1,
        PatNameNBG2,
        PatNameNBG3,
        CharPatNBG0,
        CharPatNBG1,
        CharPatNBG2,
        CharPatNBG3,
        _rsvd8,
        _rsvd9,
        _rsvdA,
        _rsvdB,
        VCellScrollNBG0,
        VCellScrollNBG1,
        CPU,
        NoAccess
    };

    CyclePatterns() {
        Reset();
    }

    void Reset() {
        for (auto &bank : timings) {
            bank.fill(Type::PatNameNBG0);
        }
    }

    // [0] VRAM bank A0 / A  (00000..1FFFF / 00000..3FFFF)
    // [1] VRAM bank A1      (20000..3FFFF)
    // [2] VRAM bank B0 / B  (40000..5FFFF / 40000..7FFFF)
    // [3] VRAM bank B1      (60000..7FFFF)
    // [n][0..7] Access type per timing slot (T0-T7)
    alignas(16) std::array<std::array<Type, 8>, 4> timings;
};

// 180098   ZMCTL   Reduction Enable
//
//   bits   r/w  code          description
//  15-10        -             Reserved, must be zero
//      9     W  N1ZMQT        NBG1 Zoom Quarter
//      8     W  N1ZMHF        NBG1 Zoom Half
//    7-2        -             Reserved, must be zero
//      1     W  N0ZMQT        NBG0 Zoom Quarter
//      0     W  N0ZMHF        NBG0 Zoom Half
//
//  NxZMQT,NxZMHF:
//       0,0   no horizontal reduction, no restrictions
//       0,1   up to 1/2 horizontal reduction, max 256 character colors
//       1,0   up to 1/4 horizontal reduction, max 16 character colors
//       1,1   up to 1/4 horizontal reduction, max 16 character colors
union RegZMCTL {
    uint16 u16;
    struct {
        uint16 N0ZMHF : 1;
        uint16 N0ZMQT : 1;
        uint16 _rsvd2_7 : 6;
        uint16 N1ZMHF : 1;
        uint16 N1ZMQT : 1;
        uint16 _rsvd10_15 : 6;
    };
};

// -----------------------------------------------------------------------------
// Internal VDP2 state

/// @brief NBG layer state, including coordinate counters, increments and addresses.
struct NBGLayerState {
    NBGLayerState() {
        Reset();
    }

    void Reset() {
        fracScrollX = 0;
        fracScrollY = 0;
        scrollIncH = 0x100;
        lineScrollTableAddress = 0;
        vcellScrollOffset = 0;
        vcellScrollDelay = 0;
        vcellScrollRepeat = 0;
        mosaicCounterY = 0;
    }

    // Initial fractional X scroll coordinate (11.8).
    uint32 fracScrollX;

    // Fractional Y scroll coordinate (11.8).
    // Reset at the start of every frame and updated every scanline.
    uint32 fracScrollY;

    // Fractional X scroll coordinate increment (11.8).
    // Applied every pixel and updated at the start of the frame or every line when line zoom is enabled.
    uint32 scrollIncH;

    // Current line scroll table address.
    // Reset at the start of every frame and incremented every 1/2/4/8/16 lines.
    uint32 lineScrollTableAddress;

    // Vertical cell scroll offset.
    // Only valid for NBG0 and NBG1.
    // Based on CYCA0/A1/B0/B1 parameters.
    uint32 vcellScrollOffset;

    // Is the vertical cell scroll read delayed by one cycle?
    // Only valid for NBG0 and NBG1.
    // Based on CYCA0/A1/B0/B1 parameters.
    bool vcellScrollDelay;

    // Is the first vertical cell scroll entry repeated?
    // Only valid for NBG0.
    // Based on CYCA0/A1/B0/B1 parameters.
    bool vcellScrollRepeat;

    // Vertical mosaic counter.
    // Reset at the start of every frame and incremented every line.
    // The value is mod mosaicV.
    uint8 mosaicCounterY;
};

/// @brief Rotation Parameters A and B counters and coordinates.
struct RotationParamState {
    RotationParamState() {
        Reset();
    }

    void Reset() {
        Xst = Yst = 0;
        KA = 0;
    }

    // Current base screen coordinates (signed 13.10 fixed point), updated every scanline.
    sint32 Xst, Yst;

    // Current base coefficient address (unsigned 16.10 fixed point), updated every scanline.
    uint32 KA;
};

/// @brief LNCL and BACK screen colors for the current scanline.
struct LineBackLayerState {
    LineBackLayerState() {
        Reset();
    }

    void Reset() {
        lineColor.u32 = 0;
        backColor.u32 = 0;
    }

    Color888 lineColor;
    Color888 backColor;
};

} // namespace ymir::vdp
