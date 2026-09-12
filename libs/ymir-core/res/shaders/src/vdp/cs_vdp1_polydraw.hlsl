#include "vdp1_defs.hlsli"
#include "vdp1_common_params.hlsli"
#include "vdp1_polydraw_params.hlsli"

#include "util/bit_ops.hlsli"

// Shader specialization macros:
// - POLYSPEC_TEXTURED: 0=solid color; 1=textured
// - POLYSPEC_TRANSPARENT_MESH: 0=checkerboard mesh; 1=transparent mesh
// - POLYSPEC_SHADING_GOURAUD  [CMDPMOD.2]: 0=flat shading; 1=gouraud shading
// - POLYSPEC_SHADING_HALF_SRC [CMDPMOD.1]: 0=don't modify source color; 1=halve source color ("half-luminance")
// - POLYSPEC_SHADING_HALF_DST [CMDPMOD.0]: 0=don't modify destination color; 1=halve destination color ("shadow")

// Modify these to adjust IntelliSense highlighting
#ifdef __INTELLISENSE__
#define POLYSPEC_TEXTURED         0
#define POLYSPEC_TRANSPARENT_MESH 0
#define POLYSPEC_SHADING_GOURAUD  1
#define POLYSPEC_SHADING_HALF_SRC 0
#define POLYSPEC_SHADING_HALF_DST 0
#endif

cbuffer CommonRenderParamsBuffer : register(b0) {
    CommonRenderParams g_commonParams;
    PolyDrawParams g_polyDrawParams;
}

StructuredBuffer<PolySpan> spanParams : register(t1);
Buffer<uint> spanPrefixSums : register(t2);

RWBuffer<uint> internalSpriteOut : register(u0);
RWBuffer<uint> internalSpriteMSB : register(u1);

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
static const bool evenOddCoordSelect = BitTest(g_commonParams.displayParams, 6);
static const uint drawFB = BitExtract(g_commonParams.displayParams, 7, 1);
static const bool antialias = BitTest(g_commonParams.displayParams, 8);

static const bool deinterlace = BitTest(g_commonParams.enhancements, 0);

static const uint2 sysClip = uint2(
    BitExtract(g_polyDrawParams.sysClip, 0, 16),
    BitExtract(g_polyDrawParams.sysClip, 16, 16)
);
static const uint2 userClip0 = uint2(
    BitExtract(g_polyDrawParams.userClip0, 0, 16),
    BitExtract(g_polyDrawParams.userClip0, 16, 16)
);
static const uint2 userClip1 = uint2(
    BitExtract(g_polyDrawParams.userClip1, 0, 16),
    BitExtract(g_polyDrawParams.userClip1, 16, 16)
);

// ---------------------------------------------------------------------------------------------------------------------
// Helpers

// Searches for the span containing the given pixel index.
// Returns 0xFFFFFFFF if out of range.
uint GetSpanIndex(uint pixelIndex) {
    if (pixelIndex >= spanPrefixSums[g_polyDrawParams.numSpans]) {
        return 0xFFFFFFFF;
    }

    // Binary search for smallest span index where pixelIndex >= prefixSum.
    // The span prefix sums array always contains [0, ..., total length].
    // If it contains [0, 3, 5], we want to return:
    // - index 0 for pixelIndex in [0..2]
    // - index 1 for pixelIndex in [3..4]
    // - out of bounds for any other pixelIndex
    uint lb = 0;
    uint ub = g_polyDrawParams.numSpans;
    while (lb != ub) {
        const uint midpoint = (lb + ub) >> 1u;
        const uint value = spanPrefixSums[midpoint];
        if (pixelIndex == value) {
            return midpoint;
        }
        if (pixelIndex > value) {
            lb = midpoint + 1u;
        } else {
            ub = midpoint;
        }
    }
    return lb - 1u;
}

// ---------------------------------------------------------------------------------------------------------------------
// DDA steppers

// Steps over the texels of a texture.
struct TextureStepper {
    int num;
    int den;
    int accum;

    int value;
    int inc;

    int baseAccum;
    int baseValue;

    void Setup(uint length, int start, int end, bool hss = false, int hssSelect = 0) {
        if (hss) {
            start >>= 1;
            end >>= 1;
        }
        const int delta = end - start;
        const uint absDelta = abs(delta);

        value = start;
        inc = delta >= 0 ? +1 : -1;
        if (hss) {
            value <<= 1;
            value |= hssSelect;
            inc <<= 1;
        }

        num = absDelta;
        den = length;
        if (length <= absDelta) {
            ++num;
            accum = absDelta - (length << 1);
            if (delta >= 0) {
                ++accum;
            }
        } else {
            --den;
            accum = length - (length << 1);
            if (delta < 0) {
                ++accum;
            }
        }
        num <<= 1;
        den <<= 1;
        baseAccum = accum;
        baseValue = value;
    }

    // Retrieves the current texture coordinate value.
    uint Value() {
        return value;
    }

    // Determines if the stepper is ready to step to the next texel.
    bool ShouldStepTexel() {
        return accum >= 0;
    }

    // Steps to the next texel.
    void StepTexel() {
        value += inc;
        accum -= den;
    }

    // Resets the texel counter to the initial value.
    void ResetTexel() {
        value = baseValue;
    }

    void ResetAndStepTexel() {
        value = baseValue;
        if (accum >= 0) {
            const int count = (accum / den) + 1;
            value += inc * count;
            accum -= den * count;
        }
    }

    // Moves to the pixel at the specified step.
    void SetPixel(uint step) {
        accum = baseAccum + num * step;
    }
};

// -----------------------------------------------------------------------------

// Iterates over a gouraud gradient of a single color channel.
struct GouraudChannelStepper {
    int num;
    int den;
    int accum;

    int value;
    int intInc;
    int fracInc;

    int baseValue;
    int baseAccum;

    void Setup(uint length, int start, int end) {
        const int delta = end - start;
        const uint absDelta = abs(delta);

        value = start;
        intInc = 0;
        fracInc = delta >= 0 ? +1 : -1;

        num = absDelta;
        den = length;
        if (length <= absDelta) {
            ++num;
            accum = absDelta - (length << 1);
            if (delta >= 0) {
                ++accum;
            }
        } else {
            --den;
            accum = -int(length);
            if (delta < 0) {
                ++accum;
            }
        }
        num <<= 1;
        den <<= 1;

        if (den != 0) {
            while (accum >= 0) {
                value += fracInc;
                accum -= den;
            }

            while (num >= den) {
                intInc += fracInc;
                num -= den;
            }
        }
        accum = ~accum;

        baseValue = value;
        baseAccum = accum;
    }

    void Reset() {
        value = baseValue;
        accum = baseAccum;
    }

    // Skips the specified number of pixels.
    void Skip(int steps) {
        value += intInc * steps;
        accum -= num * steps;
        if (den != 0) {
            while (accum < 0) {
                value += fracInc;
                accum += den;
            }
        }
    }

    // Blends the given base color value with the current gouraud shading value.
    // The color value must be a 5-bit value.
    uint Blend(int color) {
        return clamp(value + color - 16, 0, 31);
    }
};

// -----------------------------------------------------------------------------

struct GouraudStepper {
    GouraudChannelStepper stepperR;
    GouraudChannelStepper stepperG;
    GouraudChannelStepper stepperB;

    // Sets up gouraud shading with the given length and start and end colors.
    void Setup(uint length, uint4 gouraudStart, uint4 gouraudEnd) {
        stepperR.Setup(length, gouraudStart.r, gouraudEnd.r);
        stepperG.Setup(length, gouraudStart.g, gouraudEnd.g);
        stepperB.Setup(length, gouraudStart.b, gouraudEnd.b);
    }

    void Reset() {
        stepperR.Reset();
        stepperG.Reset();
        stepperB.Reset();
    }

    // Skips the specified number of pixels.
    void Skip(int steps) {
        if (steps > 0) {
            stepperR.Skip(steps);
            stepperG.Skip(steps);
            stepperB.Skip(steps);
        }
    }

    // Blends the given base color with the current gouraud shading values.
    uint4 Blend(uint4 baseColor) {
        return uint4(
            stepperR.Blend(baseColor.r),
            stepperG.Blend(baseColor.g),
            stepperB.Blend(baseColor.b),
            baseColor.a
        );
    }
};

// -----------------------------------------------------------------------------

struct LineStepper {
    int num;
    int den;
    int accum;
    int accumTarget;

    int2 majInc;
    int2 minInc;

    int2 pos;
    int2 start;

    uint dmaj;
    uint step;

    int2 aaInc;

    void Setup(int2 coord1, int2 coord2, bool antiAlias = false) {
        pos = coord1;
        start = coord1;

        int2 delta = coord2 - coord1;
        int2 absDelta = abs(delta);
        dmaj = max(absDelta.x, absDelta.y);
        step = 0;

        const bool xMajor = absDelta.x >= absDelta.y;
        if (xMajor) {
            majInc.x = delta.x >= 0 ? +1 : -1;
            majInc.y = 0;
            minInc.x = 0;
            minInc.y = delta.y >= 0 ? +1 : -1;
        } else {
            majInc.x = 0;
            majInc.y = delta.y >= 0 ? +1 : -1;
            minInc.x = delta.x >= 0 ? +1 : -1;
            minInc.y = 0;
            delta.xy = delta.yx;
            absDelta.xy = absDelta.yx;
        }
        num = absDelta.y << 1;
        den = absDelta.x << 1;
        accum = absDelta.x + 1;
        accumTarget = 0;
        if (!antiAlias && delta.x < 0) {
            ++accumTarget;
        }
        accum += num;

        pos -= majInc;

        if (antiAlias) {
            --accum;
            --accumTarget;
            const bool samesign = (coord1.x > coord2.x) == (coord1.y > coord2.y);
            if (xMajor) {
                aaInc.x = samesign ? 0 : -majInc.x;
                aaInc.y = samesign ? -minInc.y : 0;
            } else {
                aaInc.x = samesign ? 0 : -minInc.x;
                aaInc.y = samesign ? -majInc.y : 0;
            }
        }

        // NOTE: Shifting counters by this amount forces them to have 13 bits without the need for masking
        static const int kShift = 32 - 13;

        num <<= kShift;
        den <<= kShift;
        accum <<= kShift;
        accumTarget <<= kShift;
    }

    // Computes how many steps are needed from the start of the line to reach the target pixel.
    // Aligns the major coordinate only.
    uint StepsToTarget(uint2 targetPos, bool antiAlias) {
        const int2 deltaPos = (targetPos - start - (antiAlias ? aaInc : 0)) * majInc;
        const int delta = deltaPos.x + deltaPos.y;

        if (delta < 0 || delta >= int(dmaj) + 1) {
            return dmaj + 1;
        }
        return delta;
    }

    // Sets the slope step to the specified coordinate.
    // Clamped to the length of the line.
    void SetStep(uint targetStep) {
        targetStep = min(targetStep, dmaj);

        const int stepDelta = targetStep + 1 - step;
        if (stepDelta == 0) {
            return;
        }

        step = targetStep + 1;
        pos += majInc * stepDelta;

        // TODO: mask to 13 bits

        accum -= num * stepDelta;
        if (den != 0) {
            const int count = (accumTarget - accum + den) / den;
            accum += den * count;
            pos += minInc * count;
        }
    }

    // Determines if the current step needs antialiasing.
    bool NeedsAA() {
        return step > 1 && accum - den + num > accumTarget;
    }

    // Retrieves the current X and Y coordinates.
    int2 Coord() {
        return pos & 0x7FF;
    }

    // Returns the X and Y coordinates of the antialiased pixel.
    int2 AACoord() {
        return pos + aaInc;
    }

    // Retrieves the total number of steps in the slope, that is, the longest of the vertical and horizontal spans.
    uint Length() {
        return dmaj;
    }
};

uint4 Uint16ToColor555(uint rawValue) {
    return uint4(
        BitExtract(rawValue, 0, 5),
        BitExtract(rawValue, 5, 5),
        BitExtract(rawValue, 10, 5),
        BitExtract(rawValue, 15, 1)
    );
}

uint Color555ToUint16(uint4 color) {
    return color.r | (color.g << 5) | (color.b << 10) | (color.a << 15);
}

// ---------------------------------------------------------------------------------------------------------------------
// Entrypoint

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    // TODO: implement

    // POLYSPEC_SHADING_HALF_DST and POLYSPEC_SHADING_HALF_SRC specify the blending mode:
    //  DST=0 SRC=0  Replace            dst = src
    //  DST=0 SRC=1  Half-Luminance     dst = src >> 1
    //  DST=1 SRC=0  Shadow             if (dst.msb) { dst = dst >> 1 }
    //  DST=1 SRC=1  Half-Transparency  if (dst.msb) { dst = (dst + src) >> 1 } else { dst = src }

    // Common implementation details:
    // - inputs:
    //   - span parameters list
    //     - start and end coordinates and gouraud colors
    //     - span length in pixels
    //     - span skip amount in pixels
    //     - texture V coordinate
    //     - horizontal flip bit
    //   - precomputed span length and prefix sums to aid pixel-level indexing
    // - id.x is a pixel-level index into the span sequence
    //   - for example, if the span list contains 3 spans with lengths 10, 12, 14 and skips 0, 0, 10:
    //     - index  0 -> span 0 pixel 0
    //     - index  7 -> span 0 pixel 7
    //     - index  9 -> span 0 pixel 9
    //     - index 10 -> span 1 pixel 0
    //     - index 15 -> span 1 pixel 5
    //     - index 21 -> span 1 pixel 11
    //     - index 22 -> span 2 pixel 10
    //     - index 25 -> span 2 pixel 13 (last)
    //     - index 26 -> out of bounds, discarded
    // - draw spans in parallel into internalSpriteOut
    // - run a second shader to combine that into the output FBRAM (2 or 4 pixels at a time to fit into 32-bit values)

    // Possible implementation for Replace and Half-Luminance (and maybe Shadow):
    // - combine 8/16-bit sprite data output with the span index into a single 32-bit value to be written to the intermediate output buffer
    //   - top bits contain the span sequence number (index into span array plus one)
    //   - FBRAM transfer shader will zero these counters out; apply UAV barriers between these dispatches
    // - use InterlockedMax to plot the latest pixel to the framebuffer

    // Half-Transparency needs an order-independent transparency implementation and different inputs and outputs.
    // TODO: investigate alternatives:
    // see https://github.com/nvpro-samples/vk_order_independent_transparency
    // - Linked List
    // - Loop32
    // - Spinlock

    const uint spanIndex = GetSpanIndex(id.x);
    if (spanIndex == 0xFFFFFFFF) {
        return;
    }

    const PolySpan span = spanParams[spanIndex];
    const uint spanStep = id.x - spanPrefixSums[spanIndex] + span.skip;

    LineStepper lineStepper;
    lineStepper.Setup(span.coord0, span.coord1, span.antialias);
    lineStepper.SetStep(spanStep);

    const bool msbOn = BitTest(span.cmdpmod, 15);
    uint value;
    if (!msbOn) {
        uint spriteData;
#if POLYSPEC_TEXTURED
        // TODO: fetch texel
        spriteData = 0xFFFF;
#else
        spriteData = span.cmdcolr;
        if (pixel8Bits) {
            spriteData &= 0xFFu;
        }
#endif

#if POLYSPEC_SHADING_GOURAUD
        //uint4 srcColor = Uint16ToColor555(spriteData);

        GouraudStepper gouraud;
        gouraud.Setup(span.length, span.gouraud0, span.gouraud1);
        gouraud.Skip(spanStep);
        //srcColor = gouraud.Blend(srcColor);

        //spriteData = Color555ToUint16(srcColor);
#endif

        value = spriteData | (spanIndex << 16u);
    }

    // TODO: if SRC==0 && DST==1, track shadow writes per pixel
    // TODO: if SRC==1 && DST==1, use OIT algorithm instead
    const int2 coord = lineStepper.Coord();
    const uint outOffset = coord.y * fbSize.x + coord.x;
    if (msbOn) {
        InterlockedMax(internalSpriteMSB[outOffset], spanIndex);
    } else {
        InterlockedMax(internalSpriteOut[outOffset], value);
    }

    if (span.antialias) {
        const int2 aaCoord = lineStepper.AACoord();
        const uint aaOutOffset = aaCoord.y * fbSize.x + aaCoord.x;
        if (msbOn) {
            InterlockedMax(internalSpriteMSB[aaOutOffset], spanIndex);
        } else {
            InterlockedMax(internalSpriteOut[aaOutOffset], value);
        }
    }
}
