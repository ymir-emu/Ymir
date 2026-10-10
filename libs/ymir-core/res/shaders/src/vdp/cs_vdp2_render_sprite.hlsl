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
StructuredBuffer<RotParamBase> g_rotParamBases : register(t4);
ByteAddressBuffer g_spriteFB : register(t5);

RWTexture2DArray<uint4> g_layerOut : register(u1);
RWTexture2DArray<uint> g_spriteAttrsOut : register(u2);

// ---------------------------------------------------------------------------------------------------------------------
// Parameters

static const uint interlaceMode = BitExtract(g_commonParams.displayParams, 2, 2);
static const uint oddField = BitExtract(g_commonParams.displayParams, 4, 1);
static const bool exclusiveMonitor = BitTest(g_commonParams.displayParams, 5);
static const bool hiResH = BitTest(g_commonParams.displayParams, 8);
static const bool dblInterlaceEnable = BitTest(g_commonParams.displayParams, 15);
static const bool dblInterlaceDrawLine = BitTest(g_commonParams.displayParams, 16);

static const bool rotate = BitTest(g_commonParams.spriteParams, 0);
static const bool pixel8Bits = BitTest(g_commonParams.spriteParams, 1);
static const uint type = BitExtract(g_commonParams.spriteParams, 2, 4);
static const uint2 fbSize = uint2(
    512u << BitExtract(g_commonParams.spriteParams, 6, 1),
    256u << BitExtract(g_commonParams.spriteParams, 7, 1)
);
static const bool inHalfResH = BitTest(g_commonParams.spriteParams, 8);
static const bool outHalfResH = BitTest(g_commonParams.spriteParams, 9);
static const bool mixedFormat = BitTest(g_commonParams.spriteParams, 10);
static const bool useSpriteWindow = BitTest(g_commonParams.spriteParams, 20);
static const uint spriteDisplayFB = BitExtract(g_commonParams.spriteParams, 23, 1);

static const bool deinterlace = BitTest(g_commonParams.enhancements, 0);

static const uint colorRAMMode = BitExtract(g_commonParams.displayParams, 6, 2);
static const uint kCRAMAddressMask = colorRAMMode == 1 ? 0x7FF : 0x3FF;

static const uint kSpriteFBBaseOffset = spriteDisplayFB * kVDP1FBRAMSize;
static const uint kDeinterlaceFBBaseOffset = kVDP1FBRAMSize * 2;
static const uint kVDP1MeshFBOffset = kVDP1FBRAMSize * 2 * 2;

// ---------------------------------------------------------------------------------------------------------------------
// Utilities

uint ReadSprite8(uint address, uint field) {
    address += kSpriteFBBaseOffset + field * kDeinterlaceFBBaseOffset;
    return BitExtract(g_spriteFB.Load(address & ~3), (address & 3) * 8, 8);
}

uint ReadSprite16(uint address, uint field) {
    address += kSpriteFBBaseOffset + field * kDeinterlaceFBBaseOffset;
    return ByteSwap16(g_spriteFB.Load(address & ~3) >> ((address & 2) * 8));
}

uint ReadMesh8(uint address, uint field) {
    return ReadSprite8(kVDP1MeshFBOffset + address, field);
}

uint ReadMesh16(uint address, uint field) {
    return ReadSprite16(kVDP1MeshFBOffset + address, field);
}

uint GetY(uint y) {
    const bool interlaced = interlaceMode >= 2;
    if (!deinterlace && interlaced && !exclusiveMonitor) {
        return (y << 1) | oddField;
    } else {
        return y;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Colors and CRAM

uint4 FetchCRAMColor(uint cramOffset, uint colorIndex) {
    const uint cramAddress = (cramOffset + colorIndex) & kCRAMAddressMask;
    return g_cramColor[cramAddress];
}

uint4 Color555(uint val16) {
    return uint4(
        ((val16 >> 0) & 0x1F) << 3,
        ((val16 >> 5) & 0x1F) << 3,
        ((val16 >> 10) & 0x1F) << 3,
        (val16 >> 15) & 1
    );
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

bool InsideWindows(uint2 pos) {
    const bool windowLogicAND = BitTest(g_commonParams.windows, 0);
    const bool window0Enable = BitTest(g_commonParams.windows, 1);
    const bool window0Invert = BitTest(g_commonParams.windows, 2);
    const bool window1Enable = BitTest(g_commonParams.windows, 3);
    const bool window1Invert = BitTest(g_commonParams.windows, 4);

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

// ---------------------------------------------------------------------------------------------------------------------
// Rotation parameter calculation

uint2 CalcRotationSpriteCoordinates(uint2 pos) {
    const RotParamBase base = g_rotParamBases[pos.y];

    const int Xst = SignExtend(Read32(g_vram, base.tableAddress + 0x00) >> 6, 23);
    const int Yst = SignExtend(Read32(g_vram, base.tableAddress + 0x04) >> 6, 23);

    const int deltaXst = SignExtend(Read32(g_vram, base.tableAddress + 0x0C) >> 6, 13);
    const int deltaYst = SignExtend(Read32(g_vram, base.tableAddress + 0x10) >> 6, 13);

    const int deltaX = SignExtend(Read32(g_vram, base.tableAddress + 0x14) >> 6, 13);
    const int deltaY = SignExtend(Read32(g_vram, base.tableAddress + 0x18) >> 6, 13);

    // Current sprite coordinates (13.10)
    // 10 + 0*10 + 0*10 = 10 + 10 + 10 = 10 frac bits
    // 23 + 10*13 + 9*13 = 23 + 23 + 22 = 23 total bits
    return uint2(
        (Xst + pos.y * deltaXst + pos.x * deltaX) >> 10,
        (Yst + pos.y * deltaYst + pos.x * deltaY) >> 10
    );
}

// ---------------------------------------------------------------------------------------------------------------------
// Sprite data

struct SpriteData {
    uint colorData; // DC10-0
    uint colorCalcRatio; // CC2-0
    uint priority; // PR2-0
    bool shadowOrWindow; // SD
    uint special; // Color data special patterns
};

uint GetSpecialPattern(uint rawData, uint colorDataBits) {
    // Normal shadow pattern (LSB = 0, rest of the color data bits = 1)
    const uint kNormalShadowValue = (1u << colorDataBits) - 2u;

    if ((rawData & 0x7FFF) == 0) {
        return kSpriteDataTransparent;
    } else if (BitExtract(rawData, 0, colorDataBits) == kNormalShadowValue) {
        return kSpriteDataShadow;
    } else {
        return kSpriteDataNormal;
    }
}

SpriteData FetchSpriteData(uint fbAddr, uint field, bool meshLayer) {
    // Adjust offset based on VDP1 data size.
    // The majority of games actually set the sprite readout size to match the VDP1 sprite data size, but there's
    // *always* an exception...
    // 8-bit VDP1 data vs. 16-bit readout: NBA Live 98
    // 16-bit VDP1 data vs. 8-bit readout: I Love Donald Duck
    uint rawData;
    if (pixel8Bits) {
        rawData = meshLayer ? ReadMesh8(fbAddr, field) : ReadSprite8(fbAddr, field);
        if (type < 8 && (!meshLayer || rawData != 0)) {
            rawData |= 0xFF00;
        }
    } else {
        fbAddr <<= 1;
        rawData = meshLayer ? ReadMesh16(fbAddr, field) : ReadSprite16(fbAddr, field);
    }

    // Sprite types 0-7 are 16-bit, 8-15 are 8-bit
    //  type |15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0|
    //    0  |PR1-0  |CC2-0      |DC10-0                                    |
    //    1  |PR2-0      |CC1-0  |DC10-0                                    |
    //    2  |SD |PR0|CC2-0      |DC10-0                                    |
    //    3  |SD |PR1-0  |CC1-0  |DC10-0                                    |
    //    4  |SD |PR1-0  |CC2-0      |DC9-0                                 |
    //    5  |SD |PR2-0      |CC0|DC10-0                                    |
    //    6  |SD |PR2-0      |CC1-0  |DC9-0                                 |
    //    7  |SD |PR2-0      |CC2-0      |DC8-0                             |
    //  type                                 | 7   6   5   4   3   2   1   0|
    //    8                                  |PR0|DC6-0                     |
    //    9                                  |PR0|CC0|DC5-0                 |
    //    A                                  |PR1-0  |DC5-0                 |
    //    B                                  |CC1-0  |DC5-0                 |
    //    C                                  |SP0|DC6-0                     |
    //    D                                  |SP0|SC0|DC5-0                 |
    //    E                                  |SP1-0  |DC5-0                 |
    //    F                                  |SC1-0  |DC5-0                 |
    //
    // SD: shadow/sprite window
    // PR: priority
    // CC: color calculation ratio
    // DC: dot color data
    // SP: shared PR+DC
    // SC: shared CC+DC

    SpriteData data;
    switch (type) {
        case 0x0:
            data.colorData = BitExtract(rawData, 0, 11);
            data.colorCalcRatio = BitExtract(rawData, 11, 3);
            data.priority = BitExtract(rawData, 14, 2);
            data.shadowOrWindow = false;
            data.special = GetSpecialPattern(rawData, 11);
            break;

        case 0x1:
            data.colorData = BitExtract(rawData, 0, 11);
            data.colorCalcRatio = BitExtract(rawData, 11, 2);
            data.priority = BitExtract(rawData, 13, 3);
            data.shadowOrWindow = false;
            data.special = GetSpecialPattern(rawData, 11);
            break;

        case 0x2:
            data.colorData = BitExtract(rawData, 0, 11);
            data.colorCalcRatio = BitExtract(rawData, 11, 3);
            data.priority = BitExtract(rawData, 14, 1);
            data.shadowOrWindow = BitTest(rawData, 15);
            data.special = GetSpecialPattern(rawData, 11);
            break;

        case 0x3:
            data.colorData = BitExtract(rawData, 0, 11);
            data.colorCalcRatio = BitExtract(rawData, 11, 2);
            data.priority = BitExtract(rawData, 13, 2);
            data.shadowOrWindow = BitTest(rawData, 15);
            data.special = GetSpecialPattern(rawData, 11);
            break;

        case 0x4:
            data.colorData = BitExtract(rawData, 0, 10);
            data.colorCalcRatio = BitExtract(rawData, 10, 3);
            data.priority = BitExtract(rawData, 13, 2);
            data.shadowOrWindow = BitTest(rawData, 15);
            data.special = GetSpecialPattern(rawData, 10);
            break;

        case 0x5:
            data.colorData = BitExtract(rawData, 0, 11);
            data.colorCalcRatio = BitExtract(rawData, 11, 1);
            data.priority = BitExtract(rawData, 12, 3);
            data.shadowOrWindow = BitTest(rawData, 15);
            data.special = GetSpecialPattern(rawData, 11);
            break;

        case 0x6:
            data.colorData = BitExtract(rawData, 0, 10);
            data.colorCalcRatio = BitExtract(rawData, 10, 2);
            data.priority = BitExtract(rawData, 12, 3);
            data.shadowOrWindow = BitTest(rawData, 15);
            data.special = GetSpecialPattern(rawData, 10);
            break;

        case 0x7:
            data.colorData = BitExtract(rawData, 0, 9);
            data.colorCalcRatio = BitExtract(rawData, 9, 3);
            data.priority = BitExtract(rawData, 12, 3);
            data.shadowOrWindow = BitTest(rawData, 15);
            data.special = GetSpecialPattern(rawData, 9);
            break;

        case 0x8:
            data.colorData = BitExtract(rawData, 0, 7);
            data.colorCalcRatio = 0;
            data.priority = BitExtract(rawData, 7, 1);
            data.shadowOrWindow = false;
            data.special = GetSpecialPattern(rawData, 7);
            break;

        case 0x9:
            data.colorData = BitExtract(rawData, 0, 6);
            data.colorCalcRatio = BitExtract(rawData, 6, 1);
            data.priority = BitExtract(rawData, 7, 1);
            data.shadowOrWindow = false;
            data.special = GetSpecialPattern(rawData, 6);
            break;

        case 0xA:
            data.colorData = BitExtract(rawData, 0, 6);
            data.colorCalcRatio = 0;
            data.priority = BitExtract(rawData, 6, 2);
            data.shadowOrWindow = false;
            data.special = GetSpecialPattern(rawData, 6);
            break;

        case 0xB:
            data.colorData = BitExtract(rawData, 0, 6);
            data.colorCalcRatio = BitExtract(rawData, 6, 2);
            data.priority = 0;
            data.shadowOrWindow = false;
            data.special = GetSpecialPattern(rawData, 6);
            break;

        case 0xC:
            data.colorData = BitExtract(rawData, 0, 8);
            data.colorCalcRatio = 0;
            data.priority = BitExtract(rawData, 7, 1);
            data.shadowOrWindow = false;
            data.special = GetSpecialPattern(rawData, 8);
            break;

        case 0xD:
            data.colorData = BitExtract(rawData, 0, 8);
            data.colorCalcRatio = BitExtract(rawData, 6, 1);
            data.priority = BitExtract(rawData, 7, 1);
            data.shadowOrWindow = false;
            data.special = GetSpecialPattern(rawData, 8);
            break;

        case 0xE:
            data.colorData = BitExtract(rawData, 0, 8);
            data.colorCalcRatio = 0;
            data.priority = BitExtract(rawData, 6, 2);
            data.shadowOrWindow = false;
            data.special = GetSpecialPattern(rawData, 8);
            break;

        case 0xF:
            data.colorData = BitExtract(rawData, 0, 8);
            data.colorCalcRatio = BitExtract(rawData, 6, 2);
            data.priority = 0;
            data.shadowOrWindow = false;
            data.special = GetSpecialPattern(rawData, 8);
            break;
    }
    return data;
}

// ---------------------------------------------------------------------------------------------------------------------
// Sprite layer rendering

struct SpriteOutput {
    uint4 layer;
    uint colorCalcRatio;
    bool colorMSB;
    bool shadowOrWindow;
    uint special;
};

// index 0 = sprite
// index 1 = transparent meshes
SpriteOutput DrawSprite(uint2 pos, uint2 outPos, uint index) {
    const bool meshLayer = index == 1;

    SpriteOutput output = { kTransparentPixel, 0, false, false, false };

    uint2 spritePos;
    uint field = 0;
    if (rotate) {
        spritePos = CalcRotationSpriteCoordinates(pos);
        if (any(spritePos >= fbSize)) {
            return output;
        }
    } else {
        spritePos = pos;
        if (inHalfResH) {
            spritePos.x <<= 1;
        } else if (outHalfResH) {
            spritePos.x >>= 1;
        }
        if (deinterlace && interlaceMode >= kInterlaceModeSingleDensity) {
            if (interlaceMode == kInterlaceModeDoubleDensity && (spritePos.y & 1)) {
                field = 1;
            }
            spritePos.y >>= 1;
        }
    }
    const uint fbAddr = spritePos.x + spritePos.y * fbSize.x;

    if (InsideWindows(pos)) {
        return output;
    }

    if (mixedFormat) {
        const uint spriteDataValue = meshLayer ? ReadMesh16(fbAddr << 1, field) : ReadSprite16(fbAddr << 1, field);
        if (BitTest(spriteDataValue, 15)) {
            // RGB data

            // Transparent if:
            // - Using byte-sized sprite types (0x8 to 0xF) and the lower 8 bits are all zero
            // - Using word-sized sprite types that have the shadow/sprite window bit (types 0x2 to 0x7), sprite
            //   window is enabled, and the lower 15 bits are all zero
            if (type >= 8) {
                if (BitExtract(spriteDataValue, 0, 8) == 0) {
                    return output;
                }
            } else if (type >= 2) {
                if (useSpriteWindow && BitExtract(spriteDataValue, 0, 15) == 0) {
                    return output;
                }
            }

            const uint4 outColor = Color555(spriteDataValue);
            const uint outPriority = BitExtract(g_commonParams.spritePriosRatios.x, 0, 3);

            output.colorCalcRatio = BitExtract(g_commonParams.spritePriosRatios, 0 * 8 + 3, 5);
            output.colorMSB = true;
            output.layer = uint4(outColor.rgb, outPriority);
            return output;
        }
    }

    // Palette data
    const SpriteData spriteData = FetchSpriteData(fbAddr, field, meshLayer);

    // Handle sprite window
    const bool spriteWindowEnabled = BitTest(g_commonParams.spriteParams, 21);
    const bool spriteWindowInverted = BitTest(g_commonParams.spriteParams, 22);
    if (useSpriteWindow && spriteWindowEnabled && spriteData.shadowOrWindow != spriteWindowInverted) {
        output.layer = kTransparentPixel;
        output.shadowOrWindow = true;
        return output;
    }

    const uint colorDataOffset = BitExtract(g_commonParams.spriteParams, 17, 3) << 8;
    const uint colorIndex = colorDataOffset + spriteData.colorData;
    const uint4 outColor = FetchCRAMColor(0, colorIndex);
    const bool outTransparent = spriteData.special == kSpriteDataTransparent;
    const uint outPriority = outTransparent && !spriteData.shadowOrWindow
        ? 0
        : BitExtract(g_commonParams.spritePriosRatios, spriteData.priority * 8, 3);

    output.layer = uint4(outColor.rgb,
        outPriority |
        (1u << kPixelAttrBitSpecColorCalc)
    );
    output.colorCalcRatio = BitExtract(g_commonParams.spritePriosRatios, spriteData.colorCalcRatio * 8 + 3, 5);
    output.colorMSB = outColor.a != 0;
    output.shadowOrWindow = spriteData.shadowOrWindow;
    output.special = spriteData.special;
    return output;
}

// ---------------------------------------------------------------------------------------------------------------------
// Entrypoint

// TODO: 32 threads might be suboptimal on AMD GPUs
[numthreads(32, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const uint2 drawCoord = uint2(id.x, id.y + g_commonParams.startY);
    const uint2 outCoord = uint2(drawCoord.x, GetY(drawCoord.y));
    const SpriteOutput output = DrawSprite(drawCoord, outCoord.xy, id.z);
    g_layerOut[uint3(outCoord.xy, id.z + 6)] = output.layer;
    g_spriteAttrsOut[uint3(outCoord.xy, id.z)] =
        output.colorCalcRatio |
        ((output.colorMSB ? 1u : 0u) << kSpriteAttrBitColorMSB) |
        ((output.shadowOrWindow ? 1u : 0u) << kSpriteAttrBitShadowWindow) |
        (output.special << kSpriteAttrBitSpecial);
}
