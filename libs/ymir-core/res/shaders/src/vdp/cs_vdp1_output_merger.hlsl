#include "vdp1_defs.hlsli"
#include "vdp1_common_params.hlsli"

#include "util/bit_ops.hlsli"

cbuffer CommonRenderParamsBuffer : register(b0) {
    CommonRenderParams g_commonParams;
}

RWByteAddressBuffer fbramOut : register(u0);
RWBuffer<uint> internalSpriteOut : register(u1);

// ---------------------------------------------------------------------------------------------------------------------
// Parameters

static const uint2 fbSize = uint2(
    512u << BitExtract(g_commonParams.displayParams, 0, 1),
    256u << BitExtract(g_commonParams.displayParams, 1, 1)
);
static const bool pixel8Bits = BitTest(g_commonParams.displayParams, 2);
static const bool doubleDensity = BitTest(g_commonParams.displayParams, 3);
static const bool dblInterlaceEnable = BitTest(g_commonParams.displayParams, 4);
static const bool dblInterlaceDrawLine = BitTest(g_commonParams.displayParams, 5);
static const uint drawFB = BitExtract(g_commonParams.displayParams, 7, 1);

static const uint fbOffset = drawFB * kVDP1FBRAMSize;

static const bool deinterlace = BitTest(g_commonParams.enhancements, 0);

// ---------------------------------------------------------------------------------------------------------------------
// Mergers

void Merge8(uint2 pos) {
    const uint inOffset = pos.x * 4 + pos.y * fbSize.x;

    // Read and clear internal outputs
    const uint out0 = internalSpriteOut[inOffset + 0];
    const uint out1 = internalSpriteOut[inOffset + 1];
    const uint out2 = internalSpriteOut[inOffset + 2];
    const uint out3 = internalSpriteOut[inOffset + 3];
    internalSpriteOut[inOffset + 0] = 0;
    internalSpriteOut[inOffset + 1] = 0;
    internalSpriteOut[inOffset + 2] = 0;
    internalSpriteOut[inOffset + 3] = 0;

    const uint counter0 = BitExtract(out0, 16, 16);
    const uint counter1 = BitExtract(out1, 16, 16);
    const uint counter2 = BitExtract(out2, 16, 16);
    const uint counter3 = BitExtract(out3, 16, 16);
    if (counter0 == 0 && counter1 == 0 && counter2 == 0 && counter3 == 0) {
        // Nothing written to these pixels
        return;
    }

    const uint outOffset = inOffset * 4;
    uint fbramValue = fbramOut.Load(outOffset + fbOffset);
    if (counter0 != 0) {
        fbramValue &= ~0xFFu;
        fbramValue |= BitExtract(out0, 0, 8);
    }
    if (counter1 != 0) {
        fbramValue &= ~0xFF00u;
        fbramValue |= BitExtract(out1, 0, 8) << 8u;
    }
    if (counter2 != 0) {
        fbramValue &= ~0xFF0000u;
        fbramValue |= BitExtract(out2, 0, 8) << 16u;
    }
    if (counter3 != 0) {
        fbramValue &= ~0xFF000000u;
        fbramValue |= BitExtract(out3, 0, 8) << 24u;
    }
    fbramOut.Store(outOffset + fbOffset, fbramValue);
}

void Merge16(uint2 pos) {
    const uint inOffset = pos.x * 2 + pos.y * fbSize.x;

    // Read and clear internal outputs
    const uint out0 = internalSpriteOut[inOffset + 0];
    const uint out1 = internalSpriteOut[inOffset + 1];
    internalSpriteOut[inOffset + 0] = 0;
    internalSpriteOut[inOffset + 1] = 0;

    const uint counter0 = BitExtract(out0, 16, 16);
    const uint counter1 = BitExtract(out1, 16, 16);
    if (counter0 == 0 && counter1 == 0) {
        // Nothing written to these pixels
        return;
    }

    const uint outOffset = inOffset * 2;
    uint fbramValue = fbramOut.Load(outOffset + fbOffset);
    if (counter0 != 0) {
        fbramValue &= ~0xFFFFu;
        fbramValue |= BitExtract(out0, 0, 16);
    }
    if (counter1 != 0) {
        fbramValue &= ~0xFFFF0000u;
        fbramValue |= BitExtract(out1, 0, 16) << 16u;
    }
    fbramOut.Store(outOffset + fbOffset, fbramValue);
}

// ---------------------------------------------------------------------------------------------------------------------
// Entrypoint

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    // Work on 32-bit units at a time
    if (pixel8Bits) {
        Merge8(id.xy);
    } else {
        Merge16(id.xy);
    }
}
