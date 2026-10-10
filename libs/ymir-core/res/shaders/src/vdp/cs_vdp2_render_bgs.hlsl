#include "vdp2_common_params.hlsli"
#include "vdp2_render_params_layer.hlsli"
#include "vdp2_render_params_rotation.hlsli"

#include "vdp2_defs.hlsli"

#include "util/bit_ops.hlsli"
#include "util/data_ops.hlsli"

cbuffer CommonRenderParamsBuffer : register(b0) {
    CommonRenderParams g_commonParams;
}

StructuredBuffer<LayerRenderParams> g_layerRenderParams : register(t1);
ByteAddressBuffer g_vram : register(t2);
Buffer<uint4> g_cramColor : register(t3);
ByteAddressBuffer g_cramRotCoeff : register(t4);
StructuredBuffer<RotParamBase> g_rotParamBases : register(t5);
Texture2DArray<uint> g_spriteAttrsIn : register(t6);

RWTexture2DArray<uint4> g_layerOut : register(u1);
RWTexture2DArray<uint4> g_rbgLineColorOut : register(u2);
RWTexture2D<uint> g_colorCalcWindowOut : register(u3);

// ---------------------------------------------------------------------------------------------------------------------
// Parameters

static const uint interlaceMode = BitExtract(g_commonParams.displayParams, 2, 2);
static const uint oddField = BitExtract(g_commonParams.displayParams, 4, 1);
static const bool exclusiveMonitor = BitTest(g_commonParams.displayParams, 5);
static const bool hiResH = BitTest(g_commonParams.displayParams, 8);
static const bool palMode = BitTest(g_commonParams.displayParams, 9);
static const uint hreso = BitExtract(g_commonParams.displayParams, 10, 3);
static const uint vreso = BitExtract(g_commonParams.displayParams, 13, palMode ? 2 : 1);
static const uint2 displayRes = uint2(
    kResolutionsH[hreso & 3u], // 3rd bit intentionally ignored
    exclusiveMonitor ? 480 : kResolutionsV[vreso]
);

static const bool coeffTableCRAM = BitTest(g_commonParams.rotParams, 0);
static const uint coeffDataAccess = BitExtract(g_commonParams.rotParams, 1, 4);
static const bool coeffDataPerDot = BitTest(g_commonParams.rotParams, 5);

static const bool deinterlace = BitTest(g_commonParams.enhancements, 0);

static const uint colorRAMMode = BitExtract(g_commonParams.displayParams, 6, 2);
static const uint kCRAMAddressMask = colorRAMMode == 1 ? 0x7FF : 0x3FF;

// ---------------------------------------------------------------------------------------------------------------------
// Utilities

uint GetY(uint y, bool doubleDensityOnly) {
    const bool interlaced = doubleDensityOnly
        ? interlaceMode == kInterlaceModeDoubleDensity
        : interlaceMode >= kInterlaceModeSingleDensity;

    if (!deinterlace && interlaced && !exclusiveMonitor) {
        return (y << 1) | oddField;
    } else {
        return y;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Windows

bool InsideWindow(GlobalWindowParams window, bool invert, uint2 pos) {
    int2 start = window.start;
    int2 end = window.end;

    // Read line window if enabled
    if (window.lineWindowTableEnable) {
        const uint address = window.lineWindowTableAddress + pos.y * 4;
        start.x = Read16(g_vram, address + 0);
        end.x = Read16(g_vram, address + 2);
    }

    start.x = SignExtend(start.x, 16);
    end.x = SignExtend(end.x, 16);
    start.y = SignExtend(start.y, 16);
    end.y = SignExtend(end.y, 16);

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
    if (start.x < 0) {
        start.x = 0;
    }
    if (end.x < 0) {
        if (start.x >= end.x) {
            start.x = 0x3FF;
        }
        end.x = 0;
    }

    // For normal screen modes, X coordinates don't use bit 0
    if (!hiResH) {
        start.x >>= 1;
        end.x >>= 1;
    }

    const int2 spos = int2(pos);
    const bool inside = all(spos >= start) && all(spos <= end);
    return inside != invert;
}

bool InsideSpriteWindow(bool invert, uint2 pos) {
    return BitTest(g_spriteAttrsIn[uint3(pos, 0)], kSpriteAttrBitShadowWindow) != invert;
}

bool InsideWindows(LayerWindowParams layerWindows, uint2 pos) {
    const bool windowLogicAND = layerWindows.windowLogicAnd;
    const bool window0Enable = layerWindows.window0Enable;
    const bool window0Invert = layerWindows.window0Invert;
    const bool window1Enable = layerWindows.window1Enable;
    const bool window1Invert = layerWindows.window1Invert;

    // If no windows are enabled, consider the pixel outside of windows
    if (!window0Enable && !window1Enable) {
        return false;
    }

    bool inside = windowLogicAND;
    if (window0Enable) {
        const bool insideW0 = InsideWindow(g_layerRenderParams[0].windows[0], window0Invert, pos);
        if (windowLogicAND) {
            inside = inside && insideW0;
        } else {
            inside = inside || insideW0;
        }
    }
    if (window1Enable) {
        const bool insideW1 = InsideWindow(g_layerRenderParams[0].windows[1], window1Invert, pos);
        if (windowLogicAND) {
            inside = inside && insideW1;
        } else {
            inside = inside || insideW1;
        }
    }

    return inside;
}

bool InsideWindows(LayerWindowParamsS layerWindows, uint2 pos) {
    const bool windowLogicAND = layerWindows.base.windowLogicAnd;
    const bool window0Enable = layerWindows.base.window0Enable;
    const bool window0Invert = layerWindows.base.window0Invert;
    const bool window1Enable = layerWindows.base.window1Enable;
    const bool window1Invert = layerWindows.base.window1Invert;
    const bool spriteWindowEnable = layerWindows.spriteWindowEnable;
    const bool spriteWindowInvert = layerWindows.spriteWindowInvert;

    // If no windows are enabled, consider the pixel outside of windows
    if (!window0Enable && !window1Enable && !spriteWindowEnable) {
        return false;
    }

    bool inside = windowLogicAND;
    if (window0Enable) {
        const bool insideW0 = InsideWindow(g_layerRenderParams[0].windows[0], window0Invert, pos);
        if (windowLogicAND) {
            inside = inside && insideW0;
        } else {
            inside = inside || insideW0;
        }
    }
    if (window1Enable) {
        const bool insideW1 = InsideWindow(g_layerRenderParams[0].windows[1], window1Invert, pos);
        if (windowLogicAND) {
            inside = inside && insideW1;
        } else {
            inside = inside || insideW1;
        }
    }
    if (spriteWindowEnable) {
        const bool insideSW = InsideSpriteWindow(spriteWindowInvert, pos);
        if (windowLogicAND) {
            inside = inside && insideSW;
        } else {
            inside = inside || insideSW;
        }
    }

    return inside;
}

// ---------------------------------------------------------------------------------------------------------------------
// Colors and CRAM

uint4 FetchCRAMColor(uint cramOffset, uint colorIndex) {
    const uint cramAddress = (cramOffset + colorIndex) & kCRAMAddressMask;
    return g_cramColor[cramAddress];
}

uint4 Color555(uint val16) {
    return uint4(
        BitExtract(val16, 0u, 5u) << 3u,
        BitExtract(val16, 5u, 5u) << 3u,
        BitExtract(val16, 10u, 5u) << 3u,
        BitExtract(val16, 15u, 1u)
    );
}

uint4 Color888(uint val32) {
    return uint4(
        BitExtract(val32, 0u, 8u),
        BitExtract(val32, 8u, 8u),
        BitExtract(val32, 16u, 8u),
        BitExtract(val32, 31u, 1u)
    );
}

// ---------------------------------------------------------------------------------------------------------------------
// Special color calculation bits

bool IsSpecialColorCalcMatch(uint specFuncSelect, uint specColorCode) {
    return BitTest(g_layerRenderParams[0].specialFunctionCodes, specFuncSelect * 8 + specColorCode);
}

bool GetSpecialColorCalcFlag(const BaseBGParams params, uint specColorCode, bool specColorCalc, bool colorMSB) {
    const bool colorCalcEnable = params.colorCalcEnable;
    if (!colorCalcEnable) {
        return false;
    }

    const uint specColorCalcMode = params.specialColorCalcMode;
    switch (specColorCalcMode) {
        case kSpecColorCalcModeScreen:
            return colorCalcEnable;
        case kSpecColorCalcModeCharacter:
            return specColorCalc;
        case kSpecColorCalcModeDot:
            return specColorCalc && IsSpecialColorCalcMatch(params.specialFunctionSelect, specColorCode);
        case kSpecColorCalcModeColorMSB:
            return colorMSB;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------------------------------
// Rotation parameter table fetching

struct RotTable {
    // Screen start coordinates (signed 13.10 fixed point)
    int Xst, Yst, Zst;

    // Screen vertical coordinate increments (signed 3.10 fixed point)
    int deltaXst, deltaYst;

    // Screen horizontal coordinate increments (signed 3.10 fixed point)
    int deltaX, deltaY;

    // Rotation matrix parameters (signed 4.10 fixed point)
    int A, B, C, D, E, F;

    // Viewpoint coordinates (signed 14-bit integer)
    int Px, Py, Pz;

    // Center point coordinates (signed 14-bit integer)
    int Cx, Cy, Cz;

    // Horizontal shift (signed 14.10 fixed point)
    int Mx, My;

    // Scaling coefficients (signed 8.16 fixed point)
    int kx, ky;

    // Coefficient table parameters
    uint KAst; // Coefficient table start address (unsigned 16.10 fixed point)
    int dKAst; // Coefficient table vertical increment (signed 10.10 fixed point)
    int dKAx; // Coefficient table horizontal increment (signed 10.10 fixed point)
};

struct RotCoefficient {
    int value; // coefficient value, scaled to 16 fractional bits
    uint lineColorData;
    bool transparent;
};

RotTable ReadRotTable(const uint address) {
    RotTable table;

    table.Xst = SignExtend(Read32(g_vram, address + 0x00) >> 6, 23);
    table.Yst = SignExtend(Read32(g_vram, address + 0x04) >> 6, 23);
    table.Zst = SignExtend(Read32(g_vram, address + 0x08) >> 6, 23);

    table.deltaXst = SignExtend(Read32(g_vram, address + 0x0C) >> 6, 13);
    table.deltaYst = SignExtend(Read32(g_vram, address + 0x10) >> 6, 13);

    table.deltaX = SignExtend(Read32(g_vram, address + 0x14) >> 6, 13);
    table.deltaY = SignExtend(Read32(g_vram, address + 0x18) >> 6, 13);

    table.A = SignExtend(Read32(g_vram, address + 0x1C) >> 6, 14);
    table.B = SignExtend(Read32(g_vram, address + 0x20) >> 6, 14);
    table.C = SignExtend(Read32(g_vram, address + 0x24) >> 6, 14);
    table.D = SignExtend(Read32(g_vram, address + 0x28) >> 6, 14);
    table.E = SignExtend(Read32(g_vram, address + 0x2C) >> 6, 14);
    table.F = SignExtend(Read32(g_vram, address + 0x30) >> 6, 14);

    table.Px = SignExtend(Read16(g_vram, address + 0x34), 14);
    table.Py = SignExtend(Read16(g_vram, address + 0x36), 14);
    table.Pz = SignExtend(Read16(g_vram, address + 0x38), 14);

    table.Cx = SignExtend(Read16(g_vram, address + 0x3C), 14);
    table.Cy = SignExtend(Read16(g_vram, address + 0x3E), 14);
    table.Cz = SignExtend(Read16(g_vram, address + 0x40), 14);

    table.Mx = SignExtend(Read32(g_vram, address + 0x44) >> 6, 24);
    table.My = SignExtend(Read32(g_vram, address + 0x48) >> 6, 24);

    table.kx = SignExtend(Read32(g_vram, address + 0x4C), 24);
    table.ky = SignExtend(Read32(g_vram, address + 0x50), 24);

    table.KAst = Read32(g_vram, address + 0x54) >> 6;
    table.dKAst = SignExtend(Read32(g_vram, address + 0x58) >> 6, 20);
    table.dKAx = SignExtend(Read32(g_vram, address + 0x5C) >> 6, 20);

    return table;
}

bool CanFetchCoefficient(const uint coeffDataSize, uint coeffAddress) {
    if (coeffTableCRAM) {
        return true;
    }

    if (!coeffDataPerDot) {
        return true;
    }

    const uint offset = coeffAddress >> 10u;
    const uint address = (offset * 4) >> coeffDataSize;
    const uint bank = BitExtract(address, 17, 2);
    return BitTest(coeffDataAccess, bank);
}

RotCoefficient ReadRotCoefficient(const uint coeffDataSize, const uint coeffDataMode, uint coeffAddress) {
    const uint offset = coeffAddress >> 10;

    RotCoefficient coeff;

    // Force coefficient to 0 if it cannot be read in per-dot mode
    if (!CanFetchCoefficient(coeffDataSize, coeffAddress)) {
        coeff.value = 0;
        coeff.lineColorData = 0;
        coeff.transparent = true;
        return coeff;
    }

    if (coeffDataSize == 1) {
        // One-word coefficient data
        const uint address = offset * 2;
        const uint data = coeffTableCRAM ? Read16(g_cramRotCoeff, address) : Read16(g_vram, address);
        coeff.value = SignExtend(data, 15);
        coeff.lineColorData = 0;
        coeff.transparent = BitTest(data, 15);

        if (coeffDataMode == kCoeffDataModeViewpointX) {
            coeff.value <<= 14;
        } else {
            coeff.value <<= 6;
        }
    } else {
        // Two-word coefficient data
        const uint address = offset * 4;
        const uint data = coeffTableCRAM ? Read32(g_cramRotCoeff, address) : Read32(g_vram, address);
        coeff.value = SignExtend(data, 24);
        coeff.lineColorData = BitExtract(data, 24, 7);
        coeff.transparent = BitTest(data, 31);

        if (coeffDataMode == kCoeffDataModeViewpointX) {
            coeff.value <<= 8;
        }
    }

    return coeff;
}

// ---------------------------------------------------------------------------------------------------------------------
// Rotation parameter calculation

uint2 CalcRotationScreenCoords(uint2 pos, uint index) {
    const RotParamBase base = g_rotParamBases[index * kMaxNormalResV + pos.y];
    const uint coeffParamsOffset = 6 + index * 5;
    const bool coeffTableEnable = BitTest(g_commonParams.rotParams, coeffParamsOffset + 0);
    const uint coeffDataSize = BitExtract(g_commonParams.rotParams, coeffParamsOffset + 1, 1);
    const uint coeffDataMode = BitExtract(g_commonParams.rotParams, coeffParamsOffset + 2, 2);

    const RotTable t = ReadRotTable(base.tableAddress);

    int Tx, Ty, Tz;

    // Common terms for Xsp and Ysp (14.10)
    // 10 - 0 = 10 frac bits
    // 23 - 14 = 23 total bits
    // expand to 10 frac bits
    // 10 - 10 = 10 frac bits
    // 23 - 24 = 24 total bits
    const int Xst = base.Xst;
    const int Yst = base.Yst;
    const int Zst = t.Zst;
    Tx = Xst - (t.Px << 10);
    Ty = Yst - (t.Py << 10);
    Tz = Zst - (t.Pz << 10);

    // Transformed starting screen coordinates (18.10)
    // 10*(10-10) + 10*(10-10) + 10*(10-10) = 20 frac bits
    // 14*(23-24) + 14*(23-24) + 14*(23-24) = 38 total bits
    // reduce to 10 frac bits
    const int Xsp = int((int64_t(t.A) * int64_t(Tx) + int64_t(t.B) * int64_t(Ty) + int64_t(t.C) * int64_t(Tz)) >> 10);
    const int Ysp = int((int64_t(t.D) * int64_t(Tx) + int64_t(t.E) * int64_t(Ty) + int64_t(t.F) * int64_t(Tz)) >> 10);

    // Transformed view coordinates (18.10)
    // 10*(0-0) + 10*(0-0) + 10*(0-0) + 10 + 10 = 10+10+10 + 10+10 = 10 frac bits
    // 14*(14-14) + 14*(14-14) + 14*(14-14) + 24 + 24 = 28+28+28 + 24+24 = 28 total bits
    Tx = t.Px - t.Cx;
    Ty = t.Py - t.Cy;
    Tz = t.Pz - t.Cz;
    int /***/ Xp = t.A * Tx + t.B * Ty + t.C * Tz + (t.Cx << 10) + t.Mx;
    const int Yp = t.D * Tx + t.E * Ty + t.F * Tz + (t.Cy << 10) + t.My;

    // Screen coordinate increments per Hcnt (7.10)
    // 10*10 + 10*10 = 20 + 20 = 20 frac bits
    // 14*13 + 14*13 = 27 + 27 = 27 total bits
    // reduce to 10 frac bits
    const int scrXIncH = (t.A * t.deltaX + t.B * t.deltaY) >> 10;
    const int scrYIncH = (t.D * t.deltaX + t.E * t.deltaY) >> 10;

    int kx = t.kx;
    int ky = t.ky;

    if (coeffTableEnable) {
        // Current coefficient address (16.10)
        const uint KAxofs = coeffDataPerDot ? pos.x * t.dKAx : 0;
        const uint KA = base.KA + KAxofs;

        // Read and apply rotation coefficient
        const RotCoefficient coeff = ReadRotCoefficient(coeffDataSize, coeffDataMode, KA);

        switch (coeffDataMode) {
            case kCoeffDataModeScaleCoeffXY:
                kx = ky = coeff.value;
                break;
            case kCoeffDataModeScaleCoeffX:
                kx = coeff.value;
                break;
            case kCoeffDataModeScaleCoeffY:
                ky = coeff.value;
                break;
            case kCoeffDataModeViewpointX:
                Xp = coeff.value << 2;
                break;
        }
    }

    // Current screen coordinates (18.10)
    const int scrX = Xsp + pos.x * scrXIncH;
    const int scrY = Ysp + pos.x * scrYIncH;

    // Resulting screen coordinates (26.0)
    // (16*10) + 10 = 26 + 10 frac bits
    // (24*28) + 28 = 52 + 28 total bits
    // reduce 26 to 10 frac bits
    // = 10 + 10 = 10 frac bits
    // = 36 + 28 = 36 total bits
    // remove frac bits from result = 26 total bits
    return uint2(
        (((int64_t(kx) * scrX) >> 16) + Xp) >> 10,
        (((int64_t(ky) * scrY) >> 16) + Yp) >> 10
    );
}

RotCoefficient CalcRotationCoefficient(uint2 pos, uint index) {
    const RotParamBase base = g_rotParamBases[index * kMaxNormalResV + pos.y];
    const uint coeffParamsOffset = 6 + index * 5;
    const bool coeffTableEnable = BitTest(g_commonParams.rotParams, coeffParamsOffset + 0);

    RotCoefficient coeff;
    if (coeffTableEnable) {
        // Current coefficient address (16.10)
        const int dKAx = SignExtend(Read32(g_vram, base.tableAddress + 0x5C) >> 6, 20);
        const uint KAxofs = coeffDataPerDot ? pos.x * dKAx : 0;
        const uint KA = base.KA + KAxofs;

        // Read and apply rotation coefficient
        const uint coeffDataSize = BitExtract(g_commonParams.rotParams, coeffParamsOffset + 1, 1);
        const uint coeffDataMode = BitExtract(g_commonParams.rotParams, coeffParamsOffset + 2, 2);
        coeff = ReadRotCoefficient(coeffDataSize, coeffDataMode, KA);
    } else {
        coeff.value = 0;
        coeff.lineColorData = 0;
        coeff.transparent = true;
    }

    return coeff;
}

// ---------------------------------------------------------------------------------------------------------------------
// Character fetching

struct Character {
    uint charNum;
    uint palNum;
    bool specColorCalc;
    bool specPriority;
    bool flipH;
    bool flipV;
};
static const Character kBlankCharacter = (Character) 0;

Character ExtractOneWordCharacter(const BaseBGParams params, uint charData) {
    const bool extChar = params.extChar;
    const uint cellSizeShift = params.cellSizeShift;

    // Character number bit range from the 1-word character pattern data (charData)
    const uint baseCharNumMask = extChar ? 0xFFF : 0x3FF;
    const uint baseCharNumPos = 2 * cellSizeShift;

    // Upper character number bit range from the supplementary character number (bgParams.supplCharNum)
    const uint supplCharNumStart = 2 * cellSizeShift + (extChar ? 2 : 0);
    const uint supplCharNumMask = 0x1Fu >> supplCharNumStart;
    const uint supplCharNumPos = 10 + supplCharNumStart;
    // The lower bits are always in range 0..1 and only used if cellSizeShift == true

    const uint baseCharNum = charData & baseCharNumMask;
    const uint supplCharNum = (params.supplScrollCharNum >> supplCharNumStart) & supplCharNumMask;

    Character ch;
    ch.charNum = (baseCharNum << baseCharNumPos) | (supplCharNum << supplCharNumPos);
    if (cellSizeShift > 0) {
        ch.charNum |= BitExtract(params.supplScrollCharNum, 0, 2);
    }
    if (params.colorFormat != kColorFormatPalette16) {
        ch.palNum = BitExtract(charData, 12, 3) << 8;
    } else {
        ch.palNum = (BitExtract(charData, 12, 4) | params.supplPalNum) << 4;
    }
    ch.specColorCalc = params.supplSpecialColorCalc;
    ch.specPriority = params.supplSpecialPriority;
    ch.flipH = !extChar && BitTest(charData, 10);
    ch.flipV = !extChar && BitTest(charData, 11);
    return ch;
}

Character FetchTwoWordCharacter(const BaseBGParams params, uint pageAddress, uint charIndex) {
    const uint charAddress = pageAddress + charIndex * 4;
    const uint charBank = BitExtract(charAddress, 17, 2);

    if (!BitTest(params.patNameAccess, charBank)) {
        return kBlankCharacter;
    }

    const uint charData = Read32(g_vram, charAddress);

    Character ch;
    ch.charNum = BitExtract(charData, 0, 15);
    ch.palNum = BitExtract(charData, 16, 7) << 4;
    ch.specColorCalc = BitTest(charData, 28);
    ch.specPriority = BitTest(charData, 29);
    ch.flipH = BitTest(charData, 30);
    ch.flipV = BitTest(charData, 31);
    return ch;
}

Character FetchOneWordCharacter(const BaseBGParams params, uint pageAddress, uint charIndex) {
    const uint charAddress = pageAddress + charIndex * 2;
    const uint charBank = BitExtract(charAddress, 17, 2);
    if (!BitTest(params.patNameAccess, charBank)) {
        return kBlankCharacter;
    }

    const uint charData = Read16(g_vram, charAddress);
    return ExtractOneWordCharacter(params, charData);
}

// ---------------------------------------------------------------------------------------------------------------------
// Pixel fetching

uint4 FetchPixel(const BaseBGParams params, uint baseAddress, uint2 dotPos, uint linePitch, bool applyVRAMDelay, uint palNum, bool specColorCalc, uint specPriority) {
    const uint charPatAccess = params.charPatAccess;
    const uint colorFormat = params.colorFormat;
    const uint cramOffset = params.cramOffset;
    const uint bgPriorityNum = params.priorityNumber;
    const uint bgPriorityMode = params.priorityMode;
    const bool enableTransparency = params.enableTransparency;

    const uint dotOffset = dotPos.x + dotPos.y * linePitch;

    uint colorData;
    uint4 outColor;
    bool outTransparent;
    bool outSpecColorCalc;
    if (colorFormat == kColorFormatPalette16) {
        const uint dotAddress = baseAddress + (dotOffset >> 1);
        const uint dotBank = BitExtract(dotAddress, 17, 2);
        const uint vramAccessOffset = applyVRAMDelay ? (BitExtract(params.vramDataOffset, dotBank, 1) << 3) : 0;
        const uint dotData = BitTest(charPatAccess, dotBank) ? Read4(g_vram, dotAddress + vramAccessOffset, ~dotPos.x & 1) : 0;
        const uint colorIndex = palNum | dotData;
        colorData = BitExtract(dotData, 1, 3);
        outColor = FetchCRAMColor(cramOffset, colorIndex);
        outTransparent = enableTransparency && dotData == 0;
        outSpecColorCalc = GetSpecialColorCalcFlag(params, colorData, specColorCalc, BitTest(outColor.a, 0));

    } else if (colorFormat == kColorFormatPalette256) {
        const uint dotAddress = baseAddress + dotOffset;
        const uint dotBank = BitExtract(dotAddress, 17, 2);
        const uint vramAccessOffset = applyVRAMDelay ? (BitExtract(params.vramDataOffset, dotBank, 1) << 3) : 0;
        const uint dotData = BitTest(charPatAccess, dotBank) ? Read8(g_vram, dotAddress + vramAccessOffset) : 0;
        const uint colorIndex = (palNum & 0x700) | dotData;
        colorData = BitExtract(dotData, 1, 3);
        outColor = FetchCRAMColor(cramOffset, colorIndex);
        outTransparent = enableTransparency && dotData == 0;
        outSpecColorCalc = GetSpecialColorCalcFlag(params, colorData, specColorCalc, BitTest(outColor.a, 0));

    } else if (colorFormat == kColorFormatPalette2048) {
        const uint dotAddress = baseAddress + (dotOffset << 1);
        const uint dotBank = BitExtract(dotAddress, 17, 2);
        const uint vramAccessOffset = applyVRAMDelay ? (BitExtract(params.vramDataOffset, dotBank, 1) << 3) : 0;
        const uint dotData = BitTest(charPatAccess, dotBank) ? Read16(g_vram, dotAddress + vramAccessOffset) : 0;
        const uint colorIndex = dotData & 0x7FF;
        colorData = BitExtract(dotData, 1, 3);
        outColor = FetchCRAMColor(cramOffset, colorIndex);
        outTransparent = enableTransparency && (dotData & 0x7FF) == 0;
        outSpecColorCalc = GetSpecialColorCalcFlag(params, colorData, specColorCalc, BitTest(outColor.a, 0));

    } else if (colorFormat == kColorFormatRGB555) {
        const uint dotAddress = baseAddress + (dotOffset << 1);
        const uint dotBank = BitExtract(dotAddress, 17, 2);
        const uint vramAccessOffset = applyVRAMDelay ? (BitExtract(params.vramDataOffset, dotBank, 1) << 3) : 0;
        const uint dotData = BitTest(charPatAccess, dotBank) ? Read16(g_vram, dotAddress + vramAccessOffset) : 0;
        outColor = Color555(dotData);
        outTransparent = enableTransparency && outColor.w == 0;
        outSpecColorCalc = GetSpecialColorCalcFlag(params, 7, specColorCalc, true);

    } else if (colorFormat == kColorFormatRGB888) {
        const uint dotAddress = baseAddress + (dotOffset << 2);
        const uint dotBank = BitExtract(dotAddress, 17, 2);
        const uint vramAccessOffset = applyVRAMDelay ? (BitExtract(params.vramDataOffset, dotBank, 1) << 3) : 0;
        const uint dotData = BitTest(charPatAccess, dotBank) ? Read32(g_vram, dotAddress + vramAccessOffset) : 0;
        outColor = Color888(dotData);
        outTransparent = enableTransparency && outColor.w == 0;
        outSpecColorCalc = GetSpecialColorCalcFlag(params, 7, specColorCalc, true);

    } else {
        colorData = 0;
        outColor = uint4(0, 0, 0, 0);
        outTransparent = true;
        outSpecColorCalc = false;
    }

    if (outTransparent) {
        return kTransparentPixel;
    }

    uint outPriority = bgPriorityNum;
    if (bgPriorityMode == kPriorityModeCharacter) {
        outPriority &= ~1;
        outPriority |= specPriority;
    } else if (bgPriorityMode == kPriorityModeDot) {
        outPriority &= ~1;
        if (specPriority != 0 && colorFormat < kColorFormatRGB555) {
            outPriority |= IsSpecialColorCalcMatch(params.specialFunctionSelect, colorData);
        }
    }

    const uint outPaletteFormat = colorFormat <= kColorFormatPalette2048 ? 1u : 0u;

    return uint4(
        outColor.rgb,
        (outPaletteFormat << kPixelAttrBitPaletteFormat) |
        (outSpecColorCalc << kPixelAttrBitSpecColorCalc) |
        outPriority
    );
}

uint4 FetchCharacterPixel(const BaseBGParams params, Character ch, uint2 dotPos, uint cellIndex) {
    const uint bank = BitExtract(ch.charNum << 5, 17, 2);
    const uint cellSizeShift = params.cellSizeShift;
    const uint colorFormat = params.colorFormat;
    const bool charPatDelay = BitTest(params.charPatDelay, bank);

    if (params.twoWordChar && charPatDelay && cellSizeShift > 0) {
        cellIndex ^= 1;
    }
    if (ch.flipH) {
        dotPos.x ^= 7;
        if (cellSizeShift > 0) {
            cellIndex ^= 1;
        }
    }
    if (ch.flipV) {
        dotPos.y ^= 7;
        if (cellSizeShift > 0) {
            cellIndex ^= 2;
        }
    }

    // Adjust cell index based on color format
    if (colorFormat == kColorFormatRGB888) {
        cellIndex <<= 3;
    } else if (colorFormat == kColorFormatRGB555) {
        cellIndex <<= 2;
    } else if (colorFormat != kColorFormatPalette16) {
        cellIndex <<= 1;
    }

    const uint baseAddress = (ch.charNum + cellIndex) << 5;
    return FetchPixel(params, baseAddress, dotPos, 8, false, ch.palNum, ch.specColorCalc, ch.specPriority);
}

uint4 FetchBitmapPixel(const BaseBGParams params, uint bitmapBaseAddress, uint2 scrollPos) {
    const uint2 bitmapSize = params.bitmapSize;

    const uint2 dotPos = scrollPos & (bitmapSize - 1u);
    const uint palNum = params.supplPalNum;
    const bool specColorCalc = params.supplSpecialColorCalc;
    const uint specPriority = params.supplSpecialPriority;

    return FetchPixel(params, bitmapBaseAddress, dotPos, bitmapSize.x, true, palNum, specColorCalc, specPriority);
}

uint4 FetchScrollBGPixel(const BaseBGParams params, uint2 scrollPos, uint2 pageShift, bool rot, uint pageBaseAddresses[16]) {
    const uint planeShift = rot ? 2 : 1;
    const uint planeMask = (1u << planeShift) - 1u;

    const bool twoWordChar = params.twoWordChar;
    const uint cellSizeShift = params.cellSizeShift;
    const uint pageSize = kPageSizes[cellSizeShift][twoWordChar ? 1 : 0];

    const uint2 planePos = (scrollPos >> (pageShift + 9)) & planeMask;
    const uint plane = planePos.x | (planePos.y << planeShift);
    const uint pageBaseAddress = pageBaseAddresses[plane];

    // HACK: apply data access shift
    // Not entirely correct, but fixes problems with World Heroes Perfect's demo screen
    const uint bank = BitExtract(pageBaseAddress, 17, 2);
    if (BitTest(params.vramDataOffset, bank)) {
        scrollPos.x += 8;
    }

    const uint2 pagePos = (scrollPos >> 9) & pageShift;
    const uint page = pagePos.x + (pagePos.y << 1);
    const uint pageOffset = page << pageSize;
    const uint pageAddress = pageBaseAddress + pageOffset;

    // HACK: work around FXC bug that produces invalid code for the line below:
    //   const uint2 charPatPos = ((scrollPos >> 3) & 0x3F) >> cellSizeShift;
    // When cellSizeShift is derived from a masked/shifted value, the compiler merges the two shifts into one:
    //   const uint2 charPatPos = (scrollPos >> (3 + cellSizeShift) & 0x3F;
    // See https://shader-playground.timjones.io/1df21a52a4e485bd355e1c9bab45bbd8
    const uint2 baseCharPatPos = (scrollPos >> 3) & 0x3F;
    const uint2 charPatPos = cellSizeShift != 0 ? (baseCharPatPos >> 1) : baseCharPatPos;
    const uint charIndex = charPatPos.x + (charPatPos.y << (6 - cellSizeShift));

    const uint2 cellPos = (scrollPos >> 3) & cellSizeShift;
    uint cellIndex = cellPos.x + (cellPos.y << 1);

    uint2 dotPos = scrollPos & 7;

    Character ch;
    if (twoWordChar != 0) {
        ch = FetchTwoWordCharacter(params, pageAddress, charIndex);
    } else {
        ch = FetchOneWordCharacter(params, pageAddress, charIndex);
    }
    return FetchCharacterPixel(params, ch, dotPos, cellIndex);
}

uint4 FetchScrollNBGPixel(const BaseBGParams params, uint2 scrollPos, uint pageBaseAddresses[4]) {
    uint pbaResized[16];
    for (int i = 0; i < 4; i++) {
        pbaResized[i] = pageBaseAddresses[i];
    }

    return FetchScrollBGPixel(params, scrollPos, params.pageShift, false, pbaResized);
}

uint4 FetchScrollRBGPixel(const BaseBGParams params, uint2 scrollPos, uint2 pageShift, uint pageBaseAddresses[16]) {
    return FetchScrollBGPixel(params, scrollPos, pageShift, true, pageBaseAddresses);
}

// ---------------------------------------------------------------------------------------------------------------------
// NBG drawing

uint4 DrawNBG(uint2 pos, // pixel coordinates
              uint index // NBG index (0 to 3)
             ) {
    const NBGParams params = g_layerRenderParams[0].nbg[index];
    if (!params.base.enabled) {
        return kTransparentPixel;
    }

    pos.y = GetY(pos.y, true);
    if (deinterlace && interlaceMode == kInterlaceModeSingleDensity) {
        pos.y >>= 1;
    }

    if (InsideWindows(params.base.windowParams, pos)) {
        return kTransparentPixel;
    }

    const uint2 pageShift = params.base.pageShift;
    const uint twoWordChar = params.base.twoWordChar;
    const uint cellSizeShift = params.base.cellSizeShift;
    const uint pageSize = kPageSizes[cellSizeShift][twoWordChar];
    const bool mosaicEnable = params.base.mosaicEnable;
    const bool vcellScrollEnable = params.vcellScrollEnable;

    uint2 baseFracScroll = uint2(0, 0);
    uint2 scrollInc = params.scrollInc;

    // Apply line scroll table effects on NBG0 and NBG1 if enabled
    if (index <= 1 && (params.lineScrollXEnable || params.lineScrollYEnable || params.lineZoomEnable)) {
        const uint lineScrollTableAddress = params.lineScrollTableAddress;
        const uint lineScrollIntervalShift = params.lineScrollInterval;
        const bool lineScrollXEnable = params.lineScrollXEnable;
        const bool lineScrollYEnable = params.lineScrollYEnable;
        const bool lineZoomEnable = params.lineZoomEnable;

        // Determine offsets for each entry and intervals between sets of entries
        // TODO: make this relative to startY for games that change line scroll table flags mid-frame, if any
        const uint lineScrollXOffset = 0; // if present, it's always the first entry
        uint lineScrollYOffset = 0;
        uint lineZoomOffset = 0;
        uint lineScrollTableInc = 0;
        if (lineScrollXEnable) {
            lineScrollTableInc += 4;
        }
        if (lineScrollYEnable) {
            lineScrollYOffset = lineScrollTableInc;
            lineScrollTableInc += 4;
        }
        if (lineZoomEnable) {
            lineZoomOffset = lineScrollTableInc;
            lineScrollTableInc += 4;
        }

        const uint baseTableAddr = lineScrollTableAddress + (pos.y >> lineScrollIntervalShift) * lineScrollTableInc;
        if (lineScrollXEnable) {
            const uint tableAddr = baseTableAddr + lineScrollXOffset;
            baseFracScroll.x = BitExtract(Read32(g_vram, tableAddr), 8, 19);
        }
        if (lineScrollYEnable) {
            const uint tableAddr = baseTableAddr + lineScrollYOffset;
            baseFracScroll.y = BitExtract(Read32(g_vram, tableAddr), 8, 19);
            pos.y &= (1u << lineScrollIntervalShift) - 1u; // reset cumulative scrollIncV increment
        }
        if (lineZoomEnable) {
            const uint tableAddr = baseTableAddr + lineZoomOffset;
            scrollInc.x = BitExtract(Read32(g_vram, tableAddr), 8, 11);
        }
    }

    if (vcellScrollEnable && !mosaicEnable) {
        const uint vcellScrollOffset = params.vcellScrollOffset;
        const bool vcellScrollDelay = params.vcellScrollDelay;
        const bool vcellScrollRepeat = params.vcellScrollRepeat;

        const uint scrollX = baseFracScroll.x >> 8;
        int offset = (pos.x + (scrollX & 7)) >> 3;
        if (vcellScrollRepeat && offset > 0) {
            --offset;
        }
        if (vcellScrollDelay) {
            --offset;
        }

        // TODO: if offset == -1, read from the end of the previous line (or end of frame if at topmost row of cells)
        const uint vcellScrollTableAddress = BitExtract(g_commonParams.vcellScroll, 0, 19);
        const uint vcellScrollInc = BitExtract(g_commonParams.vcellScroll, 19, 3) << 2u;
        const uint vcellAddress = vcellScrollTableAddress + offset * vcellScrollInc + vcellScrollOffset;
        const uint vcellScrollY = BitExtract(Read32(g_vram, vcellAddress), 8, 19);
        baseFracScroll.y += vcellScrollY;
    }

    const uint2 fracScrollPos = baseFracScroll + params.scrollAmount + scrollInc * pos;
    uint2 scrollPos = fracScrollPos >> 8;
    if (mosaicEnable) {
        const uint2 mosaic = uint2(BitExtract(g_commonParams.layerParams, 14, 4) + 1, BitExtract(g_commonParams.layerParams, 18, 4) + 1);
        scrollPos -= scrollPos % mosaic;
    }

    const bool bitmap = params.base.bitmap;
    if (bitmap) {
        return FetchBitmapPixel(params.base, params.base.bitmapBaseAddress, scrollPos);
    } else {
        const uint2 plane = (scrollPos >> (9u + pageShift)) & 1u;
        const uint pageBaseAddress = params.pageBaseAddresses[plane.x | (plane.y << 1u)];
        const uint bank = BitExtract(pageBaseAddress, 17, 2);
        const bool charPatDelay = BitTest(params.base.charPatDelay, bank);
        if (charPatDelay) {
            // Read previous character.
            // If we're at the start of the line, read last character from previous line.
            // If at the start of the screen, read last character in the screen.
            if (pos.x >= 8) {
                // Not at left edge of the screen - read character to the left
                scrollPos.x -= 8;
            } else {
                // Left edge of the screen - read rightmost character from previous row
                scrollPos.x += displayRes.x;
                if (pos.y >= 8) {
                    // Not at top edge of the screen - read previous row
                    scrollPos.y -= 8;
                } else {
                    // At top edge of the screen - read last character read the previous screen
                    // TODO: read character from the previous screen (store on CPU side)
                    scrollPos.y += displayRes.y - 8;
                }
            }
        }
        return FetchScrollNBGPixel(params.base, scrollPos, params.pageBaseAddresses);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// RBG drawing

uint SelectRotationParameter(const RBGParams params, uint2 pos) {
    const uint rotParamMode = BitExtract(g_commonParams.layerParams, 22, 2);
    switch (rotParamMode) {
        case kRotParamModeA:
            return kRotParamA;
        case kRotParamModeB:
            return kRotParamB;
        case kRotParamModeCoeff:{
                const bool coeffTableEnable = BitTest(g_commonParams.rotParams, 6);
                if (!coeffTableEnable) {
                    return kRotParamA;
                }
                const bool transparent = CalcRotationCoefficient(pos, 0).transparent;
                return transparent ? kRotParamB : kRotParamA;
            }
        case kRotParamModeWindow:
            return InsideWindows(g_layerRenderParams[0].rotWindows, pos) ? kRotParamB : kRotParamA;
    }
    return kRotParamA; // shouldn't happen
}

void StoreRotationLineColorData(uint2 pos, uint2 rotPos, uint index, uint rotSel) {
    const bool lineColorEnabled = BitTest(g_commonParams.layerParams, 12 + index);
    if (!lineColorEnabled) {
        return;
    }

    const uint rotParamMode = BitExtract(g_commonParams.layerParams, 22, 2);
    const bool hasRBG1 = BitTest(g_commonParams.layerParams, 13);

    bool useCoeffLineColor = false;
    uint coeffSel;

    switch (rotParamMode) {
        case kRotParamModeA:
            useCoeffLineColor = rotSel == kRotParamA;
            coeffSel = kRotParamA;
            break;
        case kRotParamModeB:
            useCoeffLineColor = rotSel == kRotParamB;
            coeffSel = hasRBG1 ? kRotParamA : kRotParamB;
            break;
        case kRotParamModeCoeff:
            useCoeffLineColor = true;
            coeffSel = kRotParamA;
            break;
        case kRotParamModeWindow:
            useCoeffLineColor = true;
            coeffSel = hasRBG1 ? kRotParamA : rotSel;
            break;
    }

    const bool lineColorPerLine = g_layerRenderParams[0].lineScreenParams.perLine;
    const uint lineColorBaseAddress = g_layerRenderParams[0].lineScreenParams.baseAddress;

    const uint lineColorY = lineColorPerLine ? pos.y : 0;
    const uint lineColorAddress = lineColorBaseAddress + lineColorY * 2;

    uint cramAddress = Read16(g_vram, lineColorAddress);

    if (useCoeffLineColor) {
        const uint coeffParamsOffset = 6 + coeffSel * 5;
        const bool coeffTableEnable = BitTest(g_commonParams.rotParams, coeffParamsOffset + 0);
        const bool coeffUseLineColorData = BitTest(g_commonParams.rotParams, coeffParamsOffset + 4);
        if (coeffTableEnable && coeffUseLineColorData) {
            const uint baseLineColorData = BitExtract(cramAddress, 7, 4);
            const uint lineColorData = CalcRotationCoefficient(rotPos.xy, coeffSel).lineColorData;
            cramAddress = (baseLineColorData << 7) | lineColorData;
        }
    }

    g_rbgLineColorOut[uint3(pos.xy, index)] = g_cramColor[cramAddress];
}

uint4 DrawScrollRBG(uint2 pos, uint index, uint rotSel, uint2 scrollPos) {
    const RBGParams params = g_layerRenderParams[0].rbg[index];

    uint2 rotPos = pos;
    if (params.base.mosaicEnable) {
        const uint mosaicH = BitExtract(g_commonParams.layerParams, 14, 4) + 1;
        rotPos.x -= rotPos.x % mosaicH;
    }

    const RBGParams rotParams = g_layerRenderParams[0].rbg[rotSel];
    const uint2 pageShift = rotParams.base.pageShift;

    // Determine maximum coordinates and screen over process
    const uint screenOverProcess = rotParams.screenOverProcess;
    const bool usingFixed512 = screenOverProcess == kScreenOverProcessFixed512;
    const bool usingRepeat = screenOverProcess == kScreenOverProcessRepeat;
    const uint2 scrollSize = usingFixed512
        ? uint2(512, 512)
        : uint2(512 * 4, 512 * 4) << pageShift;

    if (all(scrollPos < scrollSize) || usingRepeat) {
        StoreRotationLineColorData(pos, rotPos, index, rotSel);

        return FetchScrollRBGPixel(params.base, scrollPos, pageShift, params.pageBaseAddresses[rotSel]);
    }

    // Out of bounds

    if (screenOverProcess == kScreenOverProcessRepeatChar) {
        StoreRotationLineColorData(pos, rotPos, index, rotSel);

        const uint2 dotPos = scrollPos & 7;
        Character ch = ExtractOneWordCharacter(params.base, rotParams.screenOverPatternName);
        return FetchCharacterPixel(params.base, ch, dotPos, 0);
    }

    return kTransparentPixel;
}

uint4 DrawBitmapRBG(uint2 pos, uint index, uint rotSel, uint2 scrollPos) {
    const RBGParams params = g_layerRenderParams[0].rbg[index];
    const uint screenOverProcess = params.screenOverProcess;

    uint2 rotPos = pos;
    if (params.base.mosaicEnable) {
        const uint mosaicH = BitExtract(g_commonParams.layerParams, 14, 4) + 1;
        rotPos.x -= rotPos.x % mosaicH;
    }

    const uint2 pageShift = g_layerRenderParams[0].rbg[rotSel].base.pageShift;

    // Determine maximum coordinates and screen over process
    const bool usingFixed512 = screenOverProcess == kScreenOverProcessFixed512;
    const bool usingRepeat = screenOverProcess == kScreenOverProcessRepeat;
    const uint2 scrollSize = usingFixed512
        ? uint2(512, 512)
        : uint2(512 * 4, 512 * 4) << pageShift;

    if (all(scrollPos < scrollSize) || usingRepeat) {
        StoreRotationLineColorData(pos, rotPos, index, rotSel);

        return FetchBitmapPixel(params.base, g_layerRenderParams[0].rbg[rotSel].base.bitmapBaseAddress, scrollPos);
    }

    return kTransparentPixel;
}

uint4 DrawRBG(uint2 pos, // pixel coordinates
              uint index // RBG index (0 to 1)
             ) {
    const RBGParams params = g_layerRenderParams[0].rbg[index];
    if (!params.base.enabled) {
        return kTransparentPixel;
    }

    if (hiResH) {
        pos.x >>= 1;
    }
    if (deinterlace && interlaceMode >= kInterlaceModeSingleDensity) {
        pos.y >>= 1;
    }

    if (InsideWindows(params.base.windowParams, pos)) {
        return kTransparentPixel;
    }

    const uint rotSel = index == 0 ? SelectRotationParameter(params, pos) : kRotParamB;
    const RotCoefficient rotCoeff = CalcRotationCoefficient(pos, rotSel);

    // Handle transparent pixels in coefficient table
    const uint coeffParamsOffset = 6 + rotSel * 5;
    const bool coeffTableEnable = BitTest(g_commonParams.rotParams, coeffParamsOffset + 0);
    if (coeffTableEnable && rotCoeff.transparent) {
        return kTransparentPixel;
    }

    const uint2 screenCoords = CalcRotationScreenCoords(pos, rotSel);
    return params.base.bitmap
        ? DrawBitmapRBG(pos, index, rotSel, screenCoords)
        : DrawScrollRBG(pos, index, rotSel, screenCoords);
}

// ---------------------------------------------------------------------------------------------------------------------
// Color calculation window

bool InsideColorCalcWindow(uint2 pos) {
    LayerWindowParamsS ccWindows;
    ccWindows.base.windowLogicAnd = BitTest(g_commonParams.windows, 5);
    ccWindows.base.window0Enable = BitTest(g_commonParams.windows, 6);
    ccWindows.base.window0Invert = BitTest(g_commonParams.windows, 7);
    ccWindows.base.window1Enable = BitTest(g_commonParams.windows, 8);
    ccWindows.base.window1Invert = BitTest(g_commonParams.windows, 9);
    ccWindows.spriteWindowEnable = BitTest(g_commonParams.windows, 10);
    ccWindows.spriteWindowInvert = BitTest(g_commonParams.windows, 11);
    return InsideWindows(ccWindows, pos);
}

// ---------------------------------------------------------------------------------------------------------------------
// Entrypoint

[numthreads(32, 1, 7)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const uint2 drawCoord = uint2(id.x, id.y + g_commonParams.startY);
    const uint3 outCoord = uint3(drawCoord.x, GetY(drawCoord.y, false), id.z);
    if (id.z <= 3) {
        g_layerOut[outCoord] = DrawNBG(drawCoord, id.z);
    } else if (id.z <= 5) {
        g_layerOut[outCoord] = DrawRBG(drawCoord, id.z - 4);
    } else if (id.z == 6) {
        g_colorCalcWindowOut[outCoord.xy] = InsideColorCalcWindow(drawCoord) ? 1u : 0u;
    }
}
