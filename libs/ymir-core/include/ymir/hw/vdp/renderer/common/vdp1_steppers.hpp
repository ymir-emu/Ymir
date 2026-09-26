#pragma once

#include <ymir/core/types.hpp>
#include <ymir/util/inline.hpp>

#include <ymir/hw/vdp/vdp_defs.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <utility>

namespace ymir::vdp {

// Steps over the texels of a texture.
struct TextureStepper {
    FORCE_INLINE void Setup(uint32 length, sint32 start, sint32 end, bool hss = false, sint32 hssSelect = 0) {
        if (hss) {
            start >>= 1;
            end >>= 1;
        }
        const sint32 delta = end - start;
        const uint32 absDelta = abs(delta);

        m_value = start;
        m_inc = delta >= 0 ? +1 : -1;
        if (hss) {
            m_value <<= 1;
            m_value |= hssSelect;
            m_inc <<= 1;
        }

        m_num = absDelta;
        m_den = length;
        if (length <= absDelta) {
            ++m_num;
            m_accum = absDelta - (length << 1);
            if (delta >= 0) {
                ++m_accum;
            }
        } else {
            --m_den;
            m_accum = length - (length << 1);
            if (delta < 0) {
                ++m_accum;
            }
        }
        m_num <<= 1;
        m_den <<= 1;
    }

    // Retrieves the current texture coordinate value.
    FORCE_INLINE uint32 Value() const {
        return m_value;
    }

    // Determines if the stepper is ready to step to the next texel.
    FORCE_INLINE bool ShouldStepTexel() const {
        return m_accum >= 0;
    }

    // Steps to the next texel.
    FORCE_INLINE void StepTexel() {
        m_value += m_inc;
        m_accum -= m_den;
    }

    // Advances to the next pixel.
    FORCE_INLINE void StepPixel() {
        m_accum += m_num;
    }

    // Skips the specified number of pixels.
    // Meant to be used when clipping lines with LineStepper::SystemClip().
    FORCE_INLINE void SkipPixels(uint32 count) {
        m_accum += m_num * count;
    }

private:
    sint32 m_num;
    sint32 m_den;
    sint32 m_accum;

    sint32 m_value;
    sint32 m_inc;
};

// Iterates over a gouraud gradient of a single color channel.
struct GouraudChannelStepper {
    void Setup(uint32 length, uint16 start, uint16 end) {
        const sint32 delta = end - start;
        const uint32 absDelta = abs(delta);

        m_value = start;
        m_intInc = 0;
        m_fracInc = delta >= 0 ? +1 : -1;

        m_num = absDelta;
        m_den = length;
        if (length <= absDelta) {
            ++m_num;
            m_accum = absDelta - (length << 1);
            if (delta >= 0) {
                ++m_accum;
            }
        } else {
            --m_den;
            m_accum = -length;
            if (delta < 0) {
                ++m_accum;
            }
        }
        m_num <<= 1;
        m_den <<= 1;

        if (m_den != 0) {
            while (m_accum >= 0) {
                m_value += m_fracInc;
                m_accum -= m_den;
            }

            while (m_num >= m_den) {
                m_intInc += m_fracInc;
                m_num -= m_den;
            }
        }
        m_accum = ~m_accum;
    }

    // Advances the gradient by a single pixel.
    FORCE_INLINE void Step() {
        m_value += m_intInc;
        m_accum -= m_num;
        if (m_accum < 0) {
            m_value += m_fracInc;
            m_accum += m_den;
        }
    }

    // Skips the specified number of pixels.
    // Meant to be used when clipping lines with LineStepper::SystemClip().
    FORCE_INLINE void Skip(uint32 steps) {
        m_value += m_intInc * steps;
        m_accum -= m_num * steps;
        if (m_den != 0) {
            while (m_accum < 0) {
                m_value += m_fracInc;
                m_accum += m_den;
            }
        }
    }

    // Retrieves the current gouraud shading value.
    FORCE_INLINE uint8 Value() const {
        return m_value;
    }

    // Blends the given base color value with the current gouraud shading value.
    // The color value must be a 5-bit value.
    FORCE_INLINE uint8 Blend(uint8 color) const {
        static constexpr auto kLUT = [] {
            std::array<uint8, 0x40> lut{};
            for (sint8 i = 0; i < 0x40; i++) {
                lut[i] = std::clamp<sint8>(i - 16, 0, 31);
            }
            return lut;
        }();

        assert(color <= 31);
        assert(m_value + color >= 0 && m_value + color < 0x40);
        return kLUT[m_value + color];
    }

private:
    sint32 m_num;
    sint32 m_den;
    sint32 m_accum;

    sint32 m_value;
    sint32 m_intInc;
    sint32 m_fracInc;
};

struct GouraudStepper {
    // Sets up gouraud shading with the given length and start and end colors.
    FORCE_INLINE void Setup(uint32 length, Color555 gouraudStart, Color555 gouraudEnd) {
        m_r.Setup(length, gouraudStart.r, gouraudEnd.r);
        m_g.Setup(length, gouraudStart.g, gouraudEnd.g);
        m_b.Setup(length, gouraudStart.b, gouraudEnd.b);
    }

    // Steps the gouraud shader to the next coordinate.
    FORCE_INLINE void Step() {
        m_r.Step();
        m_g.Step();
        m_b.Step();
    }

    // Skips the specified number of pixels.
    // Meant to be used when clipping lines with LineStepper::SystemClip().
    FORCE_INLINE void Skip(uint32 steps) {
        if (steps > 0) {
            m_r.Skip(steps);
            m_g.Skip(steps);
            m_b.Skip(steps);
        }
    }

    // Returns the current gouraud gradient value.
    FORCE_INLINE Color555 Value() const {
        return Color555{
            .r = m_r.Value(),
            .g = m_g.Value(),
            .b = m_b.Value(),
        };
    }

    // Blends the given base color with the current gouraud shading values.
    FORCE_INLINE Color555 Blend(Color555 baseColor) const {
        return Color555{
            .r = m_r.Blend(baseColor.r),
            .g = m_g.Blend(baseColor.g),
            .b = m_b.Blend(baseColor.b),
            .msb = baseColor.msb,
        };
    }

private:
    GouraudChannelStepper m_r;
    GouraudChannelStepper m_g;
    GouraudChannelStepper m_b;
};

// Steps over the pixels of a line.
template <sint32 t_bits = 13>
struct LineStepper {
    static constexpr sint32 kCoordMask = std::max(0x7FF, (1 << (t_bits - 2)) - 1);

    FORCE_INLINE LineStepper(CoordS32 coord1, CoordS32 coord2, bool antiAlias = false) {
        auto [x1, y1] = coord1;
        auto [x2, y2] = coord2;

        m_x = x1;
        m_y = y1;
        m_xEnd = x2;
        m_yEnd = y2;
        m_antiAlias = antiAlias;

        sint32 dx = x2 - x1;
        sint32 dy = y2 - y1;
        sint32 adx = abs(dx);
        sint32 ady = abs(dy);
        m_dmaj = std::max(adx, ady);
        m_length = m_dmaj + 1;
        m_step = 0;

        const bool xMajor = adx >= ady;
        if (xMajor) {
            m_xMajInc = dx >= 0 ? +1 : -1;
            m_yMajInc = 0;
            m_xMinInc = 0;
            m_yMinInc = dy >= 0 ? +1 : -1;
        } else {
            m_xMajInc = 0;
            m_yMajInc = dy >= 0 ? +1 : -1;
            m_xMinInc = dx >= 0 ? +1 : -1;
            m_yMinInc = 0;
            std::swap(dx, dy);
            std::swap(adx, ady);
        }
        m_num = ady << 1;
        m_den = adx << 1;
        m_accum = adx + 1;
        m_accumTarget = 0;
        if (!antiAlias && dx < 0) {
            ++m_accumTarget;
        }
        m_accum += m_num;

        m_x -= m_xMajInc;
        m_y -= m_yMajInc;

        if (antiAlias) {
            --m_accum;
            --m_accumTarget;
            const bool samesign = (x1 > x2) == (y1 > y2);
            if (xMajor) {
                m_aaXInc = samesign ? 0 : -m_xMajInc;
                m_aaYInc = samesign ? -m_yMinInc : 0;
            } else {
                m_aaXInc = samesign ? 0 : -m_xMinInc;
                m_aaYInc = samesign ? -m_yMajInc : 0;
            }
        }

        // NOTE: Shifting counters by this amount forces them to have 13 bits without the need for masking
        static constexpr sint32 kShift = 32 - t_bits;

        m_num <<= kShift;
        m_den <<= kShift;
        m_accum <<= kShift;
        m_accumTarget <<= kShift;
    }

    // Clips the slope to the area 0x0..width x height.
    // Returns the number of increments skipped.
    FORCE_INLINE uint32 SystemClip(sint32 width, sint32 height) {
        static constexpr sint32 kPadding = 1;

        // Add padding to compensate for minor inaccuracies
        width += kPadding + 1;
        height += kPadding + 1;

        // Bail out early if the line length is zero
        uint32 length = m_dmaj;
        if (length == 0u) {
            return 0u;
        }

        const sint32 xs = m_x;
        const sint32 ys = m_y;
        const sint32 xe = m_xEnd;
        const sint32 ye = m_yEnd;

        // Bail out early if the line is entirely in-bounds
        if (xs >= -kPadding && xs <= width && ys >= -kPadding && ys <= height && xe >= -kPadding && xe <= width &&
            ye >= -kPadding && ye <= height) {
            return 0u;
        }

        // Fully clip line if it is entirely out of bounds
        if ((xs < -kPadding && xe < -kPadding) || (xs > width && xe > width) || (ys < -kPadding && ye < -kPadding) ||
            (ys > height && ye > height)) {
            m_x = m_xEnd;
            m_y = m_yEnd;
            m_length = 0u;
            return 0u;
        }

        // ---------------------------------------------------------------------

        const bool xmajor = m_xMajInc != 0;
        const sint32 inc = m_xMajInc + m_yMajInc; // only one of the two is non-zero
        const sint32 start = xmajor ? xs : ys;
        const sint32 end = xmajor ? xe : ye;
        const sint32 maxLen = xmajor ? width : height;

        // Clip from the start
        uint32 startClip = 0u;
        if (inc > 0 && start < -kPadding) {
            startClip = -start - 1 - kPadding;
        } else if (inc < 0 && start > maxLen) {
            startClip = start - maxLen;
        }
        if (startClip > 0u) {
            startClip = std::min(startClip, length - 1u);
            m_x += m_xMajInc * startClip;
            m_y += m_yMajInc * startClip;
            for (uint32 i = 0; i < startClip; ++i) {
                m_accum -= m_num;
                if (m_den != 0) {
                    while (m_accum <= m_accumTarget) {
                        m_accum += m_den;
                        m_x += m_xMinInc;
                        m_y += m_yMinInc;
                    }
                }
            }
            length -= startClip;
        }

        // ---------------------------------------------------------------------

        // Clip from the end
        uint32 endClip = 0u;
        if (inc < 0 && end < -kPadding) {
            endClip = -end - 1 - kPadding;
        } else if (inc > 0 && end > maxLen) {
            endClip = end - maxLen;
        }
        endClip = std::min(endClip, length);
        if (endClip > 0u) {
            endClip = length - endClip;
            length = endClip;
            m_xEnd = m_x;
            m_yEnd = m_y;
            sint32 tempAccum = m_accum;
            for (uint32 i = 0; i < endClip; ++i) {
                tempAccum -= m_num;
                if (m_den != 0) {
                    while (tempAccum <= m_accumTarget) {
                        tempAccum += m_den;
                        m_xEnd += m_xMinInc;
                        m_yEnd += m_yMinInc;
                    }
                }
            }
        }
        m_length = length + 1u;

        return startClip;
    }

    // Determines if the slope can be stepped.
    FORCE_INLINE bool CanStep() const {
        return m_step <= m_length;
    }

    // Steps the slope to the next coordinate.
    // Should not be invoked when CanStep() returns false.
    // Returns true if an antialias pixel should be drawn.
    FORCE_INLINE bool Step() {
        ++m_step;
        m_x += m_xMajInc;
        m_y += m_yMajInc;
        m_accum -= m_num;
        if (m_accum <= m_accumTarget) {
            m_accum += m_den;
            m_x += m_xMinInc;
            m_y += m_yMinInc;
            return m_antiAlias;
        }
        return false;
    }

    // Retrieves the current X coordinate.
    FORCE_INLINE sint32 X() const {
        return m_x & kCoordMask;
    }

    // Retrieves the current Y coordinate.
    FORCE_INLINE sint32 Y() const {
        return m_y & kCoordMask;
    }

    // Retrieves the current X and Y coordinates.
    FORCE_INLINE CoordS32 Coord() const {
        return {m_x, m_y};
    }

    // Returns the X coordinate of the antialiased pixel.
    FORCE_INLINE sint32 AAX() const {
        return m_x + m_aaXInc;
    }

    // Returns the Y coordinate of the antialiased pixel.
    FORCE_INLINE sint32 AAY() const {
        return m_y + m_aaYInc;
    }

    // Returns the X and Y coordinates of the antialiased pixel.
    FORCE_INLINE CoordS32 AACoord() const {
        return {AAX(), AAY()};
    }

    // Retrieves the length of the major axis of the slope, that is, the longest of the vertical and horizontal spans.
    FORCE_INLINE uint32 DMajor() const {
        return m_dmaj;
    }

    // Retrieves the total number of steps in the slope.
    FORCE_INLINE uint32 Length() const {
        return m_length;
    }

private:
    sint32 m_num;
    sint32 m_den;
    sint32 m_accum;
    sint32 m_accumTarget;

    sint32 m_xMajInc;
    sint32 m_yMajInc;
    sint32 m_xMinInc;
    sint32 m_yMinInc;

    sint32 m_x;
    sint32 m_y;
    sint32 m_xEnd;
    sint32 m_yEnd;

    uint32 m_dmaj;
    uint32 m_step;
    uint32 m_length;

    sint32 m_aaXInc;
    sint32 m_aaYInc;
    bool m_antiAlias;
};

template <sint32 t_bits = 13>
struct Edge {
    FORCE_INLINE void Setup(CoordS32 coord1, CoordS32 coord2, uint32 delta) {
        auto [x1, y1] = coord1;
        auto [x2, y2] = coord2;

        const sint32 dx = bit::sign_extend<13>(x2 - x1);
        const sint32 dy = bit::sign_extend<13>(y2 - y1);
        const uint32 adx = abs(dx);
        const uint32 ady = abs(dy);
        m_dmaj = std::max(adx, ady);

        m_x = x1;
        m_y = y1;

        m_xInc = dx >= 0 ? +1 : -1;
        m_yInc = dy >= 0 ? +1 : -1;

        m_xNum = adx << 1;
        m_yNum = ady << 1;

        m_xDen = m_yDen = m_dmaj << 1;
        m_xAccum = m_yAccum = ~m_dmaj;

        m_xAccumTarget = dy < 0 ? -1 : 0;
        m_yAccumTarget = dx < 0 ? -1 : 0;

        m_num = m_dmaj << 1;
        m_den = delta << 1;
        m_accum = ~delta;
        m_accumTarget = adx >= ady ? m_yAccumTarget : m_xAccumTarget;

        // Shift counters to constrain values to the specified number of bits
        static constexpr sint32 kShift = 32 - t_bits;

        m_xNum <<= kShift;
        m_yNum <<= kShift;
        m_xDen <<= kShift;
        m_yDen <<= kShift;
        m_xAccum <<= kShift;
        m_yAccum <<= kShift;
        m_xAccumTarget <<= kShift;
        m_yAccumTarget <<= kShift;

        m_num <<= kShift;
        m_den <<= kShift;
        m_accum <<= kShift;
        m_accumTarget <<= kShift;

        m_gouraudEnable = false;
    }

    // Sets up gouraud shading with the given start and end values.
    FORCE_INLINE void SetupGouraud(Color555 start, Color555 end) {
        m_gouraud.Setup(m_dmaj + 1, start, end);
        m_gouraudEnable = true;
    }

    // Steps the edge to the next coordinate.
    FORCE_INLINE void Step() {
        m_accum += m_num;
        if (m_accum >= m_accumTarget) {
            m_accum -= m_den;

            m_xAccum += m_xNum;
            if (m_xAccum >= m_xAccumTarget) {
                m_xAccum -= m_xDen;
                m_x += m_xInc;
            }

            m_yAccum += m_yNum;
            if (m_yAccum >= m_yAccumTarget) {
                m_yAccum -= m_yDen;
                m_y += m_yInc;
            }

            if (m_gouraudEnable) {
                m_gouraud.Step();
            }
        }
    }

    // Retrieves the current X coordinate of this edge.
    FORCE_INLINE sint32 X() const {
        return m_x;
    }

    // Retrieves the current Y coordinate of this edge.
    FORCE_INLINE sint32 Y() const {
        return m_y;
    }

    // Retrieves the current X and Y coordinates of this edge.
    FORCE_INLINE CoordS32 Coord() const {
        return {m_x, m_y};
    }

    // Retrieves this edge's gouraud stepper.
    FORCE_INLINE const GouraudStepper &Gouraud() const {
        return m_gouraud;
    }

    // Retrieves the current gouraud gradient value.
    FORCE_INLINE Color555 GouraudValue() const {
        return m_gouraud.Value();
    }

private:
    GouraudStepper m_gouraud;
    bool m_gouraudEnable;

    uint32 m_dmaj;

    sint32 m_x, m_y;
    sint32 m_xInc, m_yInc;
    sint32 m_xNum, m_yNum;
    sint32 m_xDen, m_yDen;
    sint32 m_xAccum, m_yAccum;
    sint32 m_xAccumTarget, m_yAccumTarget;

    sint32 m_num;
    sint32 m_den;
    sint32 m_accum;
    sint32 m_accumTarget;
};

FORCE_INLINE sint32 CrossProduct(CoordS32 vecA, CoordS32 vecB) {
    return vecA.x() * vecB.y() - vecA.y() * vecB.x();
}

FORCE_INLINE CoordS32 VectorFromPoints(CoordS32 pointA, CoordS32 pointB) {
    return {pointB.x() - pointA.x(), pointB.y() - pointA.y()};
}

// Dual edge iterator for a quad with vertices A-B-C-D arranged in clockwise order from top-left:
//
//    A-->B
//    ^   |
//    |   v
//    D<--C
//
// The stepper uses the edges A-D and B-C and steps over each pixel on the longer edge, advancing the position on the
// other edge proportional to their lengths.
template <sint32 t_bits = 13>
struct QuadStepper {
    FORCE_INLINE QuadStepper(CoordS32 coordA, CoordS32 coordB, CoordS32 coordC, CoordS32 coordD) {
        const uint32 deltaLx = abs(bit::sign_extend<13>(coordD.x() - coordA.x()));
        const uint32 deltaLy = abs(bit::sign_extend<13>(coordD.y() - coordA.y()));
        const uint32 deltaRx = abs(bit::sign_extend<13>(coordC.x() - coordB.x()));
        const uint32 deltaRy = abs(bit::sign_extend<13>(coordC.y() - coordB.y()));

        m_dmaj = std::max(std::max(deltaLx, deltaLy), std::max(deltaRx, deltaRy)) & 0xFFF;
        m_step = 0;

        m_edgeL.Setup(coordA, coordD, m_dmaj);
        m_edgeR.Setup(coordB, coordC, m_dmaj);

        // Determine if quad is degenerate by checking cross products of pairs of consecutive edges

        const CoordS32 vecAB = VectorFromPoints(coordA, coordB);
        const CoordS32 vecBC = VectorFromPoints(coordB, coordC);
        const CoordS32 vecCD = VectorFromPoints(coordC, coordD);
        const CoordS32 vecDA = VectorFromPoints(coordD, coordA);

        const sint32 crossABC = CrossProduct(vecAB, vecBC);
        const sint32 crossBCD = CrossProduct(vecBC, vecCD);
        const sint32 crossCDA = CrossProduct(vecCD, vecDA);
        const sint32 crossDAB = CrossProduct(vecDA, vecAB);

        // Produces -1 for negatives or 0 for positives/zeros
        const sint32 signABC = crossABC >> 31;
        const sint32 signBCD = crossBCD >> 31;
        const sint32 signCDA = crossCDA >> 31;
        const sint32 signDAB = crossDAB >> 31;

        if (crossABC == 0 || crossBCD == 0 || crossCDA == 0 || crossDAB == 0) {
            // If any of the cross products is zero, two edges are colinear or two points coincide.
            // This results in a triangle, a line or a point, all of which are considered non-degenerate.
            m_degenerate = false;
        } else {
            // The quad is regular if all cross product signs match.
            // If all signs match, the sum of the signs will be either 0 or 4.
            const sint32 signSum = signABC + signBCD + signCDA + signDAB;
            m_degenerate = (signSum & ~4) == 0;
        }
    }

    // Sets up texture interpolation for the given texture vertical size and parameters.
    FORCE_INLINE void SetupTexture(TextureStepper &stepper, uint32 charSizeV, bool flipV) const {
        sint32 start = 0;
        sint32 end = charSizeV - 1;
        if (flipV) {
            std::swap(start, end);
        }
        stepper.Setup(m_dmaj + 1, start, end);
    }

    // Sets up gouraud shading with the given start and end values.
    FORCE_INLINE void SetupGouraud(Color555 colorA, Color555 colorB, Color555 colorC, Color555 colorD) {
        m_edgeL.SetupGouraud(colorA, colorD);
        m_edgeR.SetupGouraud(colorB, colorC);
    }

    // Determines if the quad is degenerate (concave or self-intersecting).
    FORCE_INLINE bool IsDegenerate() const {
        return m_degenerate;
    }

    // Retrieves the left edge.
    FORCE_INLINE const Edge<t_bits> &LeftEdge() const {
        return m_edgeL;
    }

    // Retrieves the right edge.
    FORCE_INLINE const Edge<t_bits> &RightEdge() const {
        return m_edgeR;
    }

    // Determines if this stepper can be stepped.
    FORCE_INLINE bool CanStep() const {
        return m_step <= m_dmaj;
    }

    // Steps both edges of the quad to the next coordinate.
    // The major edge is stepped by a full pixel.
    // The minor edge is stepped in proportion to the major edge.
    // Should not be invoked when CanStep() returns false.
    FORCE_INLINE void Step() {
        ++m_step;

        m_edgeL.Step();
        m_edgeR.Step();
    }

private:
    Edge<t_bits> m_edgeL; // left edge (A-D)
    Edge<t_bits> m_edgeR; // right edge (B-C)

    uint32 m_dmaj;
    uint32 m_step;

    bool m_degenerate;
};

} // namespace ymir::vdp
