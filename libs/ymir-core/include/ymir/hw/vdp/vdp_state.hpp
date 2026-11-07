#pragma once

#include "vdp1_regs.hpp"
#include "vdp2_regs.hpp"
#include "vdp_configs.hpp"
#include "vdp_defs.hpp"
#include "vdp_devlog.hpp"

#include <ymir/savestate/savestate_vdp.hpp>

#include <ymir/hw/hw_defs.hpp>

#include <ymir/util/data_ops.hpp>
#include <ymir/util/dev_log.hpp>
#include <ymir/util/inline.hpp>

#include <ymir/core/types.hpp>

#include <array>

namespace ymir::vdp {

/// @brief Pair of VDP1 framebuffer indices.
/// Always kept in sync, opposite to one another.
union FBRAMIndex {
    uint16 u16;
    struct {
        uint8 display; // display framebuffer index, used by VDP2 only
        uint8 draw;    // draw framebuffer index, used by VDP1 and accessible on the bus
    };

    FBRAMIndex() {
        Reset();
    }

    /// @brief Sets the display and draw indices in one go.
    /// The draw index is set to the opposite of the given display index.
    /// @param[in] displayIndex the display index
    void Set(bool displayIndex) {
        display = displayIndex ? 1 : 0;
        draw = display ^ 1;
    }

    void Reset() {
        display = 1;
        draw = 0;
    }

    void Flip() {
        u16 ^= 0x0101;
    }
};

/// @brief No-op memory function.
/// @tparam T value type
/// @param[in] address address to read or write
/// @param[in] value value being read or written
template <mem_primitive T>
inline void NoopMemFn(uint32 address, T value) {}

/// @brief VDP1 memory arrays and accessors.
struct VDP1Memory {
    alignas(16) std::array<uint8, kVDP1VRAMSize> VRAM;
    alignas(16) std::array<SpriteFB, 2> FBRAM;

    // Hard reset
    void Reset() {
        for (uint32 addr = 0; addr < VRAM.size(); addr++) {
            if ((addr & 0x1F) == 0) {
                VRAM[addr] = 0x80;
            } else if ((addr & 0x1F) == 1) {
                VRAM[addr] = 0x00;
            } else if ((addr & 2) == 2) {
                VRAM[addr] = 0x55;
            } else {
                VRAM[addr] = 0xAA;
            }
        }
        FBRAM[0].fill(0);
        FBRAM[1].fill(0);
    }

    template <mem_primitive T>
    FORCE_INLINE uint32 MapVRAMAddress(uint32 address) const {
        address &= 0x7FFFF & ~(sizeof(T) - 1);
        return address;
    }

    template <mem_primitive T, typename TMemFn = decltype(NoopMemFn<T>)>
    FORCE_INLINE T ReadVRAM(uint32 address, TMemFn &&memFn = NoopMemFn) const {
        address = MapVRAMAddress<T>(address);
        const T value = util::ReadBE<T>(&VRAM[address]);
        memFn(address, value);
        return value;
    }

    template <mem_primitive T, typename TMemFn = decltype(NoopMemFn<T>)>
    FORCE_INLINE void WriteVRAM(uint32 address, T value, TMemFn &&memFn = NoopMemFn) {
        address = MapVRAMAddress<T>(address);
        util::WriteBE<T>(&VRAM[address], value);
        memFn(address, value);
    }
};

/// @brief VDP2 memory arrays and accessors.
struct VDP2Memory {
    alignas(16) std::array<uint8, kVDP2VRAMSize> VRAM; // 4x 128 KiB banks: A0, A1, B0, B1
    alignas(16) std::array<uint8, kVDP2CRAMSize> CRAM;
    const VDP2Regs &regs;

    VDP2Memory(const VDP2Regs &regs)
        : regs(regs) {}

    VDP2Memory &operator=(const VDP2Memory &rhs) {
        VRAM = rhs.VRAM;
        CRAM = rhs.CRAM;
        return *this;
    }

    // Hard reset
    void Reset() {
        VRAM.fill(0);
        CRAM.fill(0);
    }

    template <mem_primitive T>
    static void NoopMemFn(uint32 address, T value) {}

    template <mem_primitive T>
    FORCE_INLINE uint32 MapVRAMAddress(uint32 address) const {
        // TODO: handle VRSIZE.VRAMSZ
        address &= 0x7FFFF & ~(sizeof(T) - 1);
        return address;
    }

    template <mem_primitive T, typename TMemFn = decltype(NoopMemFn<T>)>
    FORCE_INLINE T ReadVRAM(uint32 address, TMemFn &&memFn = NoopMemFn) const {
        address = MapVRAMAddress<T>(address);
        const T value = util::ReadBE<T>(&VRAM[address]);
        memFn(address, value);
        return value;
    }

    template <mem_primitive T, typename TMemFn = decltype(NoopMemFn<T>)>
    FORCE_INLINE void WriteVRAM(uint32 address, T value, TMemFn &&memFn = NoopMemFn) {
        address = MapVRAMAddress<T>(address);
        util::WriteBE<T>(&VRAM[address], value);
        memFn(address, value);
    }

    template <mem_primitive T>
    FORCE_INLINE uint32 MapCRAMAddress(uint32 address) const {
        address &= 0xFFF & ~(sizeof(T) - 1);
        return kVDP2CRAMAddressMapping[regs.vramControl.colorRAMMode >> 1][address];
    }

    template <mem_primitive T>
    FORCE_INLINE uint32 UnmapCRAMAddress(uint32 address) const {
        address &= 0xFFF & ~(sizeof(T) - 1);
        return kVDP2CRAMAddressReverseMapping[regs.vramControl.colorRAMMode >> 1][address];
    }

    template <mem_primitive T, typename TMemFn = decltype(NoopMemFn<T>)>
    FORCE_INLINE T ReadCRAM(uint32 address, TMemFn &&memFn = NoopMemFn) const {
        if constexpr (std::is_same_v<T, uint32>) {
            uint32 value = ReadCRAM<uint16>(address + 0) << 16u;
            value |= ReadCRAM<uint16>(address + 2) << 0u;
            memFn(address, value);
            return value;
        }
        address = MapCRAMAddress<T>(address);
        const T value = util::ReadBE<T>(&CRAM[address]);
        memFn(address, value);
        return value;
    }

    template <mem_primitive T, typename TMemFn = decltype(NoopMemFn<T>)>
    FORCE_INLINE void WriteCRAM(uint32 address, T value, TMemFn &&memFn = NoopMemFn) {
        address = MapCRAMAddress<T>(address);
        util::WriteBE<T>(&CRAM[address], value);
        memFn(address, value);
        if (regs.vramControl.colorRAMMode == 0) {
            address ^= 0x800;
            util::WriteBE<T>(&CRAM[address], value);
            memFn(address, value);
        }
    }
};

/// @brief Internal VDP1 state.
struct VDP1State {
    VDP1State() {
        Reset();
    }

    void Reset() {
        sysClipH = kVDP1DefaultFBSizeH;
        sysClipV = kVDP1DefaultFBSizeV;

        userClipX0 = 0;
        userClipY0 = 0;

        userClipX1 = kVDP1DefaultFBSizeH;
        userClipY1 = kVDP1DefaultFBSizeV;

        localCoordX = 0;
        localCoordY = 0;
    }

    // System clipping dimensions
    uint16 sysClipH, sysClipV;

    // User clipping area
    uint16 userClipX0, userClipY0; // Top-left
    uint16 userClipX1, userClipY1; // Bottom-right

    // Local coordinates offset
    sint32 localCoordX;
    sint32 localCoordY;
};

/// @brief Internal VDP2 state.
struct VDP2State {
    VDP2State() {
        Reset();
    }

    void Reset() {
        for (auto &state : nbgLayerStates) {
            state.Reset();
        }
        for (auto &state : rotParamStates) {
            state.Reset();
        }
        for (auto &state : rbgPageBaseAddresses) {
            for (auto &addrs : state) {
                addrs.fill(0);
            }
        }
        lineBackLayerState.Reset();
        layerEnabled.fill(false);
    }

    /// @brief Layer states for NBGs 0-3.
    std::array<NBGLayerState, 4> nbgLayerStates;

    /// @brief States for Rotation Parameters A and B.
    std::array<RotationParamState, 2> rotParamStates;

    /// @brief Page base addresses for RBG planes A-P using Rotation Parameters A and B.
    /// Indexing: [RotParam A/B][RBG0-1][Plane A-P]
    /// Derived from `mapIndices`, `CHCTLA/CHCTLB.xxCHSZ`, `PNCR.xxPNB` and `PLSZ.xxPLSZn`.
    std::array<std::array<std::array<uint32, 16>, 2>, 2> rbgPageBaseAddresses;

    /// @brief State for the line color and back screens.
    LineBackLayerState lineBackLayerState;

    /// @brief Layer enable state based on BGON and other factors.
    /// ```
    ///     RBG0+RBG1   RBG0        RBG1        no RBGs
    /// [0] Sprite      Sprite      Sprite      Sprite
    /// [1] RBG0        RBG0        -           -
    /// [2] RBG1        NBG0        RBG1        NBG0
    /// [3] EXBG        NBG1/EXBG   NBG1/EXBG   NBG1/EXBG
    /// [4] -           NBG2        NBG2        NBG2
    /// [5] -           NBG3        NBG3        NBG3
    /// ```
    std::array<bool, 6> layerEnabled;

    // Rotation coefficient data access permissions per VRAM bank.
    // Derived from RAMCTL.RDBS(A-B)(0-1)(1-0), RAMCTL.VRAMD and RAMCTL.VRBMD
    std::array<bool, 4> coeffAccess;

    /// @brief Computes access delays and permissions based on VRAM access patterns for NBGs and RBGs and more factors:
    /// - CYCA0L, CYCA0U, CYCA1L, CYCA1U, CYCB0L, CYCB0U, CYCB1L, CYCB1U: access pattern timings
    /// - RAMCTL.VR(A/B)MD: VRAM bank A0/A1 and B0/B1 partitioning
    /// - RAMCTL.RDBS(A0/A1/B0/B1)n: rotation data assignments per VRAM bank
    /// - TVMD.HRESOn: normal vs. high resolution
    /// - BGON.xxON: NBG/RBG enable
    /// - CHCTL(A/B).xxBMEN: NBG/RBG bitmap enable
    /// - CHCTL(A/B).xxCHSZ: NBG/RBG 1x1 vs. 2x2 character patterns
    /// - CHCTL(A/B).xxCHCNn: NBG/RBG color format
    /// - ZMCTL.NxZM(QT/HF): scroll reduction (1/2x, 1/4x)
    ///
    /// @param[in] regs2 the VDP2 registers to use
    /// @param[in] config access patterns configuration
    void CalcAccessPatterns(VDP2Regs &regs2, const config::VDP2AccessPatternsConfig &config) {
        if (!regs2.accessPatternsDirty) [[likely]] {
            return;
        }
        regs2.accessPatternsDirty = false;
        regs2.vcellScrollDirty = true;

        // Some games set up illegal access patterns that cause NBG2/NBG3 character pattern reads to be delayed,
        // shifting all graphics on those backgrounds one tile to the right.
        const bool hires = (regs2.TVMD.HRESOn & 6) != 0;

        // Clear bitmap delay flags
        for (uint32 bgIndex = 0; bgIndex < 4; ++bgIndex) {
            regs2.bgParams[bgIndex + 1].vramDataOffset.fill(0);
        }

        // Build access pattern masks for NBG0-3 PNs and CPs.
        // Bits 0-7 correspond to T0-T7.
        std::array<uint8, 4> pn = {0, 0, 0, 0}; // pattern name access masks
        std::array<uint8, 4> cp = {0, 0, 0, 0}; // character pattern access masks

        // Character pattern access masks per bank
        std::array<std::array<uint8, 4>, 4> cpBank = {{
            {0, 0, 0, 0},
            {0, 0, 0, 0},
            {0, 0, 0, 0},
            {0, 0, 0, 0},
        }};

        // Pattern name access masks per bank
        std::array<std::array<uint8, 4>, 4> pnBank = {{
            {0, 0, 0, 0},
            {0, 0, 0, 0},
            {0, 0, 0, 0},
            {0, 0, 0, 0},
        }};

        // First CP access timing slot per NBG. 0xFF means no accesses found.
        std::array<uint8, 4> firstCPAccessTiming = {0xFF, 0xFF, 0xFF, 0xFF};

        // First CP access VRAM chip per NBG. 0xFF means no accesses found.
        std::array<uint8, 4> firstCPAccessVRAMIndex = {0xFF, 0xFF, 0xFF, 0xFF};

        // First CP access found per NBG per bank.
        std::array<std::array<bool, 4>, 4> firstCPAccessFound = {{
            {false, false, false, false},
            {false, false, false, false},
            {false, false, false, false},
            {false, false, false, false},
        }};

        for (uint8 i = 0; i < 8; ++i) {
            for (uint8 bankIndex = 0; bankIndex < regs2.cyclePatterns.timings.size(); ++bankIndex) {
                const auto &bank = regs2.cyclePatterns.timings[bankIndex];
                if (bankIndex == 1 && !regs2.vramControl.partitionVRAMA) {
                    continue;
                }
                if (bankIndex == 3 && !regs2.vramControl.partitionVRAMB) {
                    continue;
                }

                const auto timing = bank[i];
                switch (timing) {
                case CyclePatterns::PatNameNBG0: [[fallthrough]];
                case CyclePatterns::PatNameNBG1: [[fallthrough]];
                case CyclePatterns::PatNameNBG2: [[fallthrough]];
                case CyclePatterns::PatNameNBG3: //
                {
                    const uint8 bgIndex = static_cast<uint8>(timing) - static_cast<uint8>(CyclePatterns::PatNameNBG0);
                    pn[bgIndex] |= 1u << i;
                    pnBank[bgIndex][bankIndex] |= 1u << i;
                    break;
                }

                case CyclePatterns::CharPatNBG0: [[fallthrough]];
                case CyclePatterns::CharPatNBG1: [[fallthrough]];
                case CyclePatterns::CharPatNBG2: [[fallthrough]];
                case CyclePatterns::CharPatNBG3: //
                {
                    const uint8 bgIndex = static_cast<uint8>(timing) - static_cast<uint8>(CyclePatterns::CharPatNBG0);
                    cp[bgIndex] |= 1u << i;
                    cpBank[bgIndex][bankIndex] |= 1u << i;

                    // TODO: find the correct rules for bitmap accesses
                    //
                    // Test cases:
                    //
                    // clang-format off
                    // --- bitmap NBGs ---
                    //  # Res  ZM  Color  Bnk  CP mapping    Delay?  Game screen
                    //  1 hi   1x  pal256  A   CP0 01..      no      Capcom Generation - Dai-5-shuu Kakutouka-tachi, art screens
                    //                     B   CP0 ..23      skip    Capcom Generation - Dai-5-shuu Kakutouka-tachi, art screens
                    //  2 hi   1x  pal256  B0  CP1 01..      no      3D Baseball, in-game (team nameplates during intro)
                    //                     B1  CP1 ..23      no      3D Baseball, in-game (team nameplates during intro)
                    //  3 hi   1x  pal256  A   CP0 01..      no      Doukyuusei - if, title screen
                    //                     B   CP1 ..23      no      Doukyuusei - if, title screen
                    //  4 hi   1x  pal256  A0  CP0 01..      no      Duke Nukem 3D, Netlink pages
                    //                     A1  CP0 01..      no      Duke Nukem 3D, Netlink pages
                    //                     B0  CP0 01..      no      Duke Nukem 3D, Netlink pages
                    //                     B1  CP0 01..      no      Duke Nukem 3D, Netlink pages
                    //  5 hi   1x  pal256  A   CP0 0123      no      Baroque Report, art screens
                    //                     B   CP0 0123      no      Baroque Report, art screens
                    //  6 hi   1x  pal256  A0  CP0 0123      no      Sonic Jam, art gallery
                    //                     A1  CP0 0123      no      Sonic Jam, art gallery
                    //                     B0  CP0 0123      no      Sonic Jam, art gallery
                    //                     B1  CP0 0123      no      Sonic Jam, art gallery
                    //  7 hi   1x  rgb555  A   CP0 0123      no      Steam Heart's, title screen
                    //                     B   CP0 0123      no      Steam Heart's, title screen
                    //  8 lo   1x  pal256  A0  CP0 01......  no      Mr. Bones, in-game graphics
                    //  9 lo   1x  pal256  B0  CP0 0123....  no      Jung Rhythm, title screen
                    //                     B1  CP0 0123....  no      Jung Rhythm, title screen
                    //                     A0  CP1 01......  no      Jung Rhythm, title screen
                    // 10 lo   1x  pal256  A0  CP0 01......  no      The Need for Speed, menus
                    //                     A1  CP1 01......  no      The Need for Speed, menus
                    // 11 lo   1x  pal256  A   CP0 ..23....  no      The Legend of Oasis, in-game HUD
                    // 12 lo   1x  rgb888  A   CP0 01234567  no      Street Fighter Zero 3, Capcom logo FMV
                    //                     B0  CP0 01234567  no      Street Fighter Zero 3, Capcom logo FMV
                    // --- scroll NBGs ---
                    //  # Res  ZM  Cell  Color  Bnk  CP mapping    Delay?  Game screen
                    // 13 lo   1x  1x1   pal256  B0  PN2 0.......          DoDonPachi, title screen background
                    //                           B1  CP0 0.......  no      DoDonPachi, title screen background
                    // 14 lo   1x  1x1   pal16   -   PN1 ........          Gouketsuji Ichizoku 3 - Groove on Fight, scrolling background in Options screen
                    //                           B0  CP1 0123....  no      Gouketsuji Ichizoku 3 - Groove on Fight, scrolling background in Options screen
                    //                           B1  CP1 0123....  no      Gouketsuji Ichizoku 3 - Groove on Fight, scrolling background in Options screen
                    // 15 lo   1x  1x1   pal16   A0  PN2 0.......          World Heroes Perfect, menus and intro animation
                    //                           A0  CP2 ...3....  skip    World Heroes Perfect, menus and intro animation
                    //                           B   CP2 .1......  no      World Heroes Perfect, menus and intro animation
                    // 16 lo   1x  1x1   pal16   B   PN0 0.......          Cyberbots - Fullmetal Madness, in-game
                    //                           A   CP0 0.......  no      Cyberbots - Fullmetal Madness, in-game
                    //                           B   CP0 ....4...  no      Cyberbots - Fullmetal Madness, in-game
                    //                           B   PN1 .1......          Cyberbots - Fullmetal Madness, in-game
                    //                           A   CP1 .1......  no      Cyberbots - Fullmetal Madness, in-game
                    //                           B   CP1 .....5..  no      Cyberbots - Fullmetal Madness, in-game
                    //                           B   PN2 ..2.....          Cyberbots - Fullmetal Madness, in-game
                    //                           A   CP2 ..2.....  no      Cyberbots - Fullmetal Madness, in-game
                    //                           B   CP2 ......6.  no      Cyberbots - Fullmetal Madness, in-game
                    //                           B   PN3 ...3....          Cyberbots - Fullmetal Madness, in-game
                    //                           A   CP3 ...3....  no      Cyberbots - Fullmetal Madness, in-game
                    //                           B   CP3 .......7  no      Cyberbots - Fullmetal Madness, in-game
                    // 17 hi   1x  1x1   pal256  B1  PN0 0...              Dark Savior, title screen
                    //                           B0  CP0 0123      no      Dark Savior, title screen
                    //                           B1  CP0 .1.3      no      Dark Savior, title screen
                    //                           B1  PN1 ..2.              Dark Savior, title screen
                    //                           A0  CP1 0123      no      Dark Savior, title screen
                    //                           A1  CP1 0123      no      Dark Savior, title screen
                    // 18 lo   1x  1x1   pal256  B1  PN1 ..2.45..          BattleSport, loading screen
                    //                           B1  CP1 ......67  no      BattleSport, loading screen
                    // 19 lo   1x  1x1   pal256  B1  PN3 ..2.45..          Daisuki, intro animation
                    //                           B1  CP3 ......67  no      Daisuki, intro animation
                    // 20 lo   1x  2x2   pal16   B1  PN2 ..2.....          X-Men vs. Street Fighter, attract mode
                    //                           A0  CP2 ..2.....  no      X-Men vs. Street Fighter, attract mode
                    //                           A1  CP2 ..2.....  no      X-Men vs. Street Fighter, attract mode
                    //                           B0  CP2 ..2.....  no      X-Men vs. Street Fighter, attract mode
                    //                           B1  CP2 ....4...  no      X-Men vs. Street Fighter, attract mode
                    // 21 lo   1x  1x1   pal16   B1  PN3 ...3....          X-Men vs. Street Fighter, attract mode
                    //                           A0  CP3 ...3....  no      X-Men vs. Street Fighter, attract mode
                    //                           A1  CP3 ...3....  no      X-Men vs. Street Fighter, attract mode
                    //                           B0  CP3 ...3....  no      X-Men vs. Street Fighter, attract mode
                    //                           B1  CP3 .....5..  delay   X-Men vs. Street Fighter, attract mode
                    // clang-format on
                    //
                    // skip:  All CP reads are one cell ahead  -> graphics shifted one cell to the left
                    // delay: All CP reads are one cell behind -> graphics shifted one cell to the right
                    //
                    // ---
                    //
                    // Seems like the bitmap "delay" is caused by configuring out-of-phase reads for an NBG in different
                    // banks, and it only seems to happen in hi-res modes.
                    //
                    // In case #1, CP0 is assigned to T0-T1 on bank A and T2-T3 on bank B. This is out of phase and on
                    // different VRAM chips, so bank B reads are delayed.
                    //
                    // In case #2, CP1 is assigned to T0-T1 on bank B0 and T2-T3 on bank B1. Despite being out of phase,
                    // they're accessed on the same VRAM chip, so there is no delay.
                    //
                    // In case #3 we have the same display settings but CP0 gets two cycles and CP1 gets two cycles.
                    // These cause no "delay" because they're different NBGs.
                    //
                    // Case #4 has no delay because all reads for the same NBG are assigned to the same cycle slot.
                    //
                    // Cases #5 and #6 include more reads than necessary for the NBG, but because they all start on the
                    // same slot, no delay occurs.
                    //
                    // ---
                    //
                    // For scroll NBGs, the delay only occurs if CP accesses are assigned to illegal timing slots.
                    //
                    // Case #13 is a normal, valid scroll NBG PN/CP access pair.
                    //
                    // In case #14, PN0 is assigned more times than needed for NBG0, but this doesn't cause any
                    // problems. Also, the CP0 access on T3 is illegal but causes no issues because of the legal
                    // accesses on T0-T2.
                    //
                    // In case #15, the CP2 access in bank A0 is assigned to T3, which is illegal for PN at T0. Because
                    // this is assigned to the first half (T0-T3), one CP read is skipped in the line.
                    //
                    // Case #16 shows legal accesses. Note that there are CP0-CP3 accesses in both the T0-T3 and T4-T7
                    // ranges, but this does not cause the T4-T7 accesses to be shifted.
                    //
                    // Cases #18 and #19 have more PN accesses than necessary and show that only the first PN access
                    // matters for the delay checks. In both cases, the first PN access occurs on T2, which makes the CP
                    // accesses in T6 and T7 valid. PN accesses on T4 and T5 would make those CP accesses invalid.
                    //
                    // Cases #20 and #21 contrast with case #15 in that the illegal CP accesses occur on T4-T7 instead
                    // of T0-T3. In these cases, instead of a skip, there is a character read delay. Also, there is no
                    // shuffling of cells (no delay) when using 2x2 characters as seen in case #20.

                    auto &bgParams = regs2.bgParams[bgIndex + 1];
                    if (bgParams.bitmap && hires) {
                        const uint8 vramIndex = bankIndex >> 1u;
                        if (firstCPAccessTiming[bgIndex] == 0xFF) {
                            firstCPAccessTiming[bgIndex] = i;
                            firstCPAccessVRAMIndex[bgIndex] = vramIndex;
                        } else if (!firstCPAccessFound[bgIndex][bankIndex] && i > firstCPAccessTiming[bgIndex] &&
                                   vramIndex != firstCPAccessVRAMIndex[bgIndex]) {
                            bgParams.vramDataOffset[bankIndex] = 8u;
                        }
                        firstCPAccessFound[bgIndex][bankIndex] = true;
                    }
                    break;
                }

                default: break;
                }
            }

            // Stop at T3 if in hi-res mode
            if (hires && i == 3) {
                break;
            }
        }

        const bool rbg0Enabled = regs2.bgEnabled[4];
        const bool rbg1Enabled = regs2.bgEnabled[5];

        // Skip NBG0 if RBG1 is enabled
        const uint32 firstNBG = rbg1Enabled ? 1 : 0;

        // Determine how many character pattern accesses are needed for this NBG
        std::array<uint8, 4> expectedCPAccesses{};
        for (uint32 nbg = firstNBG; nbg < 4; ++nbg) {
            auto &bgParams = regs2.bgParams[nbg + 1];
            uint8 &expectedCount = expectedCPAccesses[nbg];

            // Start with a base count of 1
            expectedCount = 1;

            // Apply ZMCTL modifiers
            // FIXME: Applying these disables background graphics in Baku Baku Animal - World Zookeeper
            /*if ((nbg == 0 && ZMCTL.N0ZMQT) || (nbg == 1 && ZMCTL.N1ZMQT)) {
                expectedCount *= 4;
            } else if ((nbg == 0 && ZMCTL.N0ZMHF) || (nbg == 1 && ZMCTL.N1ZMHF)) {
                expectedCount *= 2;
            }*/

            // Apply color format modifiers
            switch (bgParams.colorFormat) {
            case ColorFormat::Palette16: break;
            case ColorFormat::Palette256: expectedCount *= 2; break;
            case ColorFormat::Palette2048: expectedCount *= 4; break;
            case ColorFormat::RGB555: expectedCount *= 4; break;
            case ColorFormat::RGB888: expectedCount *= 8; break;
            }
        }

        // Apply delays to the NBGs
        for (uint32 nbg = firstNBG; nbg < 4; ++nbg) {
            auto &bgParams = regs2.bgParams[nbg + 1];
            bgParams.charPatDelay.fill(false);
            const uint8 bgCP = cp[nbg];
            const uint8 bgPN = pn[nbg];

            // Skip bitmap NBGs as they're handled above
            if (bgParams.bitmap) {
                continue;
            }

            // Skip NBGs without any assigned accesses
            if (bgPN == 0 || bgCP == 0) {
                continue;
            }

            // Skip NBG0 and NBG1 if the pattern name access happens on T0
            if (nbg < 2 && bit::test<0>(bgPN)) {
                continue;
            }

            // Apply the delay
            if (hires) {
                // Valid character pattern access masks per timing for high resolution modes
                static constexpr uint8 kHiResPatterns[2][4] = {
                    // 1x1 character patterns
                    // T0      T1      T2      T3
                    {0b0111, 0b1110, 0b1101, 0b1011},

                    // 2x2 character patterns
                    // T0      T1      T2      T3
                    {0b0111, 0b1110, 0b1100, 0b1000},
                };

                const uint8 pnIndex = std::countr_zero(bgPN);
                if (pnIndex < 4) {
                    if (bgCP < bgPN) {
                        // CP access happens entirely before PN access
                        bgParams.charPatDelay.fill(true);
                    } else if ((bgCP & kHiResPatterns[bgParams.cellSizeShift][pnIndex]) == 0) {
                        // CP access occurs in illegal time slot
                        bgParams.charPatDelay.fill(true);
                    }
                }
            } else {
                // Valid character pattern access masks per timing for normal resolution modes
                static constexpr uint8 kLoResPatterns[8] = {
                    //  T0          T1          T2          T3          T4          T5          T6          T7
                    0b11110111, 0b11101111, 0b11001111, 0b10001111, 0b00001111, 0b00001110, 0b00001100, 0b00001000,
                };

                const uint8 pnIndex = std::countr_zero(bgPN);

                // A delay occurs if there aren't enough valid CP accesses or if there is an invalid CP accesses in the
                // same bank as the PN access
                const bool enoughValidAccesses =
                    std::popcount<uint8>(bgCP & kLoResPatterns[pnIndex]) >= expectedCPAccesses[nbg];

                if (pnIndex < 8) {
                    for (uint8 bankIndex = 0; bankIndex < 4; ++bankIndex) {
                        const uint8 bgCPBank = cpBank[nbg][bankIndex];
                        const uint8 bgPNBank = pnBank[nbg][bankIndex];
                        if (bgCPBank != 0 && (bgCPBank & kLoResPatterns[pnIndex]) == 0 &&
                            (!enoughValidAccesses || bgPNBank != 0)) {
                            if ((bgCPBank & ~kLoResPatterns[pnIndex]) >= 0b10000) {
                                if (bgParams.cellSizeShift == 0) {
                                    // Illegal CP access in T4-T7 with 1x1 character cells -- shift right
                                    bgParams.charPatDelay.fill(true);
                                }
                            } else {
                                // Illegal CP access in T0-T3
                                // If PN happens before first CP, shift left, otherwise shift right
                                if (pnIndex < std::countr_zero(bgCP)) {
                                    // If there is a PN access on the same bank as the illegal CP access which would
                                    // cause the CP access to be legal again, make it so.
                                    // FIXME: this is probably incorrect, but works with every known test case so far.
                                    const uint8 pnIndexBank = std::countr_zero(bgPNBank);
                                    if (!bgPNBank || (bgCPBank & kLoResPatterns[pnIndexBank]) == 0) {
                                        bgParams.vramDataOffset.fill(8u);
                                    }
                                } else {
                                    bgParams.charPatDelay.fill(true);
                                }
                            }
                        }
                    }
                }
            }
        }

        // Translate VRAM access cycles and rotation data bank selectors into read "permissions" for pattern name tables
        // and character pattern tables in each VRAM bank.
        for (uint32 bank = 0; bank < 4; ++bank) {
            const RotDataBankSel rotDataBankSel = regs2.vramControl.GetRotDataBankSel(bank);

            // RBG0
            if (rbg0Enabled && (!rbg1Enabled || bank < 2)) {
                regs2.bgParams[0].patNameAccess[bank] = rotDataBankSel == RotDataBankSel::PatternName;
                regs2.bgParams[0].charPatAccess[bank] = rotDataBankSel == RotDataBankSel::Character;
            } else {
                regs2.bgParams[0].patNameAccess[bank] = false;
                regs2.bgParams[0].charPatAccess[bank] = false;
            }

            // RBG1
            if (rbg1Enabled) {
                regs2.bgParams[1].patNameAccess[bank] = bank == 3;
                regs2.bgParams[1].charPatAccess[bank] = bank == 2;
            } else {
                regs2.bgParams[1].patNameAccess[bank] = false;
                regs2.bgParams[1].charPatAccess[bank] = false;
            }

            // NBG0-3
            for (uint32 nbg = firstNBG; nbg < 4; ++nbg) {
                auto &bgParams = regs2.bgParams[nbg + 1];
                bgParams.patNameAccess[bank] = false;
                bgParams.charPatAccess[bank] = false;

                // Skip disabled NBGs
                if (!regs2.bgEnabled[nbg]) {
                    continue;
                }
                // Skip NBGs 2 and 3 if RBG1 is enabled
                if (rbg1Enabled && bank >= 2u) {
                    continue;
                }
                // Skip NBGs if RBG0 is enabled and the current bank is assigned to it
                if (rbg0Enabled && rotDataBankSel != RotDataBankSel::Unused) {
                    continue;
                }

                // Check for maximum 8 cycles on normal resolution, 4 cycles on high resolution/exclusive monitor modes
                const uint8 expectedCount = expectedCPAccesses[nbg];
                const uint32 max = hires ? 4 : 8;
                if (expectedCount > max) [[unlikely]] {
                    continue;
                }

                // Check that the background has the required number of accesses
                const uint8 numCPs = std::popcount(cp[nbg]);
                if (numCPs < expectedCount) {
                    continue;
                }
                if constexpr (devlog::trace_enabled<grp::vdp2_regs>) {
                    if (numCPs > expectedCount) {
                        devlog::trace<grp::vdp2_regs>("NBG{} has more CP accesses than needed ({} > {})", nbg, numCPs,
                                                      expectedCount);
                    }
                }

                // Enable pattern name and character pattern accesses for the bank
                for (uint32 index = 0; index < max; ++index) {
                    const auto timing = regs2.cyclePatterns.timings[bank][index];
                    if (timing == CyclePatterns::PatNameNBG0 + nbg) {
                        bgParams.patNameAccess[bank] = true;
                    } else if (timing == CyclePatterns::CharPatNBG0 + nbg) {
                        bgParams.charPatAccess[bank] = true;
                    } else if (config.relaxedBitmapCPAccessChecks && timing == CyclePatterns::CPU == bgParams.bitmap) {
                        // HACK: allow bitmap data access during SH-2 cycles. Fixes flickering FMVs in:
                        // - Shin Kaitei Gunkan
                        // - Lunar - Silver Star Story
                        // - Dark Savior
                        bgParams.charPatAccess[bank] = true;
                    }
                }
            }
        }

        // Combine unpartitioned parameters
        if (!regs2.vramControl.partitionVRAMA) {
            for (uint32 i = 0; i < 5; i++) {
                regs2.bgParams[i].charPatAccess[1] = regs2.bgParams[i].charPatAccess[0];
                regs2.bgParams[i].patNameAccess[1] = regs2.bgParams[i].patNameAccess[0];
                regs2.bgParams[i].vramDataOffset[1] = regs2.bgParams[i].vramDataOffset[0];
            }
        }
        if (!regs2.vramControl.partitionVRAMB) {
            for (uint32 i = 0; i < 5; i++) {
                regs2.bgParams[i].charPatAccess[3] = regs2.bgParams[i].charPatAccess[2];
                regs2.bgParams[i].patNameAccess[3] = regs2.bgParams[i].patNameAccess[2];
                regs2.bgParams[i].vramDataOffset[3] = regs2.bgParams[i].vramDataOffset[2];
            }
        }

        // Apply access permissions for rotation coefficient data
        auto &vramCtl = regs2.vramControl;
        auto isCoeff = [](RotDataBankSel sel) { return sel == RotDataBankSel::Coefficients; };
        coeffAccess[0] = isCoeff(vramCtl.rotDataBankSelA0);
        coeffAccess[1] = isCoeff(vramCtl.partitionVRAMA ? vramCtl.rotDataBankSelA1 : vramCtl.rotDataBankSelA0);
        coeffAccess[2] = isCoeff(vramCtl.rotDataBankSelB0);
        coeffAccess[3] = isCoeff(vramCtl.partitionVRAMB ? vramCtl.rotDataBankSelB1 : vramCtl.rotDataBankSelB0);
    }

    /// @brief Computes vertical cell scroll access delays for NBGs 0 and 1 based on these factors:
    /// - CYCA0L, CYCA0U, CYCA1L, CYCA1U, CYCB0L, CYCB0U, CYCB1L, CYCB1U: access pattern timings
    /// - SCRCTL.NnVCSC: vertical cell scroll enable
    ///
    /// @param[in] regs2 the VDP2 registers to use
    void CalcVCellScrollDelay(VDP2Regs &regs2) {
        if (!regs2.vcellScrollDirty) [[likely]] {
            return;
        }
        regs2.vcellScrollDirty = false;

        // Translate VRAM access cycles for vertical cell scroll data into increment and offset for NBG0 and NBG1.
        //
        // Some games set up "illegal" access patterns which we have to honor. This is an approximation of the real
        // thing, since this VDP emulator does not actually perform the accesses described by the CYCxn registers.
        //
        // Vertical cell scroll reads are subject to a one-cycle delay if they happen on the following timing slots:
        //   NBG0: T3-T7
        //   NBG1: T4-T7

        regs2.vcellScrollInc = 0;
        uint32 vcellAccessOffset = 0;
        nbgLayerStates[0].vcellScrollOffset = 0;
        nbgLayerStates[1].vcellScrollOffset = 0;

        // Update cycle accesses
        for (uint32 slotIndex = 0; slotIndex < 8; ++slotIndex) {
            std::array<bool, 2> vcellScrollAccesses = {false, false};
            for (uint32 bank = 0; bank < 4; ++bank) {
                const auto access = regs2.cyclePatterns.timings[bank][slotIndex];
                switch (access) {
                case CyclePatterns::VCellScrollNBG0: vcellScrollAccesses[0] = true; break;
                case CyclePatterns::VCellScrollNBG1: vcellScrollAccesses[1] = true; break;
                default: break;
                }
            }
            if (regs2.bgParams[1].vcellScrollEnable && vcellScrollAccesses[0]) {
                regs2.vcellScrollInc += sizeof(uint32);
                nbgLayerStates[0].vcellScrollOffset = vcellAccessOffset;
                nbgLayerStates[0].vcellScrollDelay = slotIndex >= 3;
                nbgLayerStates[0].vcellScrollRepeat = slotIndex >= 2;
                vcellAccessOffset += sizeof(uint32);
            }
            if (regs2.bgParams[2].vcellScrollEnable && vcellScrollAccesses[1]) {
                regs2.vcellScrollInc += sizeof(uint32);
                nbgLayerStates[1].vcellScrollOffset = vcellAccessOffset;
                nbgLayerStates[1].vcellScrollDelay = slotIndex >= 3;
                vcellAccessOffset += sizeof(uint32);
            }
        }
    }

    /// @brief Updates the background enable states in `layerEnabled`.
    /// @param[in] regs2 the VDP2 registers to use
    /// @param[in] debugRenderOpts the VDP2 debug rendering options to use
    void UpdateEnabledBGs(const VDP2Regs &regs2, const config::VDP2DebugRender &debugRenderOpts) {
        const auto &enabledLayers = debugRenderOpts.enabledLayers;

        // Sprite layer is always enabled, unless forcibly disabled
        layerEnabled[0] = enabledLayers[0];

        if (regs2.bgEnabled[4] && regs2.bgEnabled[5]) {
            layerEnabled[1] = enabledLayers[1]; // RBG0
            layerEnabled[2] = enabledLayers[2]; // RBG1
            layerEnabled[3] = false;            // EXBG
            layerEnabled[4] = false;            // not used
            layerEnabled[5] = false;            // not used
        } else {
            // Certain color format settings on NBG0 and NBG1 restrict which BG layers can be enabled
            // - NBG1 is disabled when NBG0 uses 8:8:8 RGB
            // - NBG2 is disabled when NBG0 uses 2048 color palette or any RGB format
            // - NBG3 is disabled when NBG0 uses 8:8:8 RGB or NBG1 uses 2048 color palette or 5:5:5 RGB color format
            // Additionally, NBG0 and RBG1 are mutually exclusive. If RBG1 is enabled, it takes place of NBG0.
            const ColorFormat colorFormatNBG0 = regs2.bgParams[1].colorFormat;
            const ColorFormat colorFormatNBG1 = regs2.bgParams[2].colorFormat;
            const bool disableNBG1 = colorFormatNBG0 == ColorFormat::RGB888;
            const bool disableNBG2 = colorFormatNBG0 == ColorFormat::Palette2048 ||
                                     colorFormatNBG0 == ColorFormat::RGB555 || colorFormatNBG0 == ColorFormat::RGB888;
            const bool disableNBG3 = colorFormatNBG0 == ColorFormat::RGB888 ||
                                     colorFormatNBG1 == ColorFormat::Palette2048 ||
                                     colorFormatNBG1 == ColorFormat::RGB555;

            layerEnabled[1] = enabledLayers[1] && regs2.bgEnabled[4];                         // RBG0
            layerEnabled[2] = enabledLayers[2] && (regs2.bgEnabled[0] || regs2.bgEnabled[5]); // NBG0/RBG1
            layerEnabled[3] = enabledLayers[3] && regs2.bgEnabled[1] && !disableNBG1;         // NBG1/EXBG
            layerEnabled[4] = enabledLayers[4] && regs2.bgEnabled[2] && !disableNBG2;         // NBG2
            layerEnabled[5] = enabledLayers[5] && regs2.bgEnabled[3] && !disableNBG3;         // NBG3
        }
    }

    /// @brief Updates the page base addresses for RBGs.
    /// @param[in] regs2 the VDP2 registers to use
    void UpdateRotationPageBaseAddresses(VDP2Regs &regs2) {
        for (int index = 0; index < 2; index++) {
            if (!regs2.bgEnabled[index + 4]) {
                continue;
            }

            BGParams &bgParams = regs2.bgParams[index];
            if (!bgParams.rbgPageBaseAddressesDirty) {
                continue;
            }
            bgParams.rbgPageBaseAddressesDirty = false;

            const bool cellSizeShift = bgParams.cellSizeShift;
            const bool twoWordChar = bgParams.twoWordChar;

            for (int param = 0; param < 2; param++) {
                const RotationParams &rotParam = regs2.rotParams[param];
                auto &pageBaseAddresses = rbgPageBaseAddresses[param];
                const uint16 plsz = rotParam.plsz;
                for (int plane = 0; plane < 16; plane++) {
                    const uint32 mapIndex = rotParam.mapIndices[plane];
                    pageBaseAddresses[index][plane] = CalcPageBaseAddress(cellSizeShift, twoWordChar, plsz, mapIndex);
                }
            }
        }
    }
};

/// @brief Contains the entire state of the VDP1 and VDP2.
struct VDPState {
    VDPState()
        : mem2(regs2) {
        Reset(true);
    }

    /// @brief Performs a soft or hard reset of the state.
    /// @param hard whether to do a hard (`true`) or soft (`false`) reset
    void Reset(bool hard) {
        if (hard) {
            mem1.Reset();
            mem2.Reset();
            fbIndex.Reset();
        }

        regs1.Reset(hard);
        regs2.Reset();
        if (hard) {
            state1.Reset();
        }

        HPhase = HorizontalPhase::Active;
        VPhase = VerticalPhase::Active;
    }

    // -------------------------------------------------------------------------
    // Save states

    void SaveState(savestate::VDPSaveState &state) const {
        state.VRAM1 = mem1.VRAM;
        state.FBRAM = mem1.FBRAM;
        state.displayFB = fbIndex.display;

        state.VRAM2 = mem2.VRAM;
        state.CRAM = mem2.CRAM;

        state.regs1.TVMR = regs1.ReadTVMR();
        state.regs1.FBCR = regs1.ReadFBCR();
        state.regs1.PTMR = regs1.ReadPTMR();
        state.regs1.EWDR = regs1.ReadEWDR();
        state.regs1.EWLR = regs1.ReadEWLR();
        state.regs1.EWRR = regs1.ReadEWRR();
        state.regs1.EDSR = regs1.ReadEDSR();
        state.regs1.LOPR = regs1.ReadLOPR();
        state.regs1.COPR = regs1.ReadCOPR();
        state.regs1.MODR = regs1.ReadMODR();

        state.regs1.FBCRChanged = regs1.fbParamsChanged;
        state.regs1.eraseWriteValueLatch = regs1.eraseWriteValueLatch;
        state.regs1.eraseX1Latch = regs1.eraseX1Latch;
        state.regs1.eraseX3Latch = regs1.eraseX3Latch;
        state.regs1.eraseY1Latch = regs1.eraseY1Latch;
        state.regs1.eraseY3Latch = regs1.eraseY3Latch;
        state.regs1.nextCommandAddress = regs1.nextCommandAddress;

        state.regs2.TVMD = regs2.ReadTVMD();
        state.regs2.EXTEN = regs2.ReadEXTEN<true>();
        state.regs2.TVSTAT = regs2.ReadTVSTAT<true>();
        state.regs2.VRSIZE = regs2.ReadVRSIZE();
        state.regs2.HCNT = regs2.ReadHCNT();
        state.regs2.VCNT = regs2.VCNT;
        state.regs2.RAMCTL = regs2.ReadRAMCTL();
        state.regs2.CYCA0L = regs2.ReadCYCA0L();
        state.regs2.CYCA0U = regs2.ReadCYCA0U();
        state.regs2.CYCA1L = regs2.ReadCYCA1L();
        state.regs2.CYCA1U = regs2.ReadCYCA1U();
        state.regs2.CYCB0L = regs2.ReadCYCB0L();
        state.regs2.CYCB0U = regs2.ReadCYCB0U();
        state.regs2.CYCB1L = regs2.ReadCYCB1L();
        state.regs2.CYCB1U = regs2.ReadCYCB1U();
        state.regs2.BGON = regs2.ReadBGON();
        state.regs2.MZCTL = regs2.ReadMZCTL();
        state.regs2.SFSEL = regs2.ReadSFSEL();
        state.regs2.SFCODE = regs2.ReadSFCODE();
        state.regs2.CHCTLA = regs2.ReadCHCTLA();
        state.regs2.CHCTLB = regs2.ReadCHCTLB();
        state.regs2.BMPNA = regs2.ReadBMPNA();
        state.regs2.BMPNB = regs2.ReadBMPNB();
        state.regs2.PNCNA = regs2.ReadPNCNA();
        state.regs2.PNCNB = regs2.ReadPNCNB();
        state.regs2.PNCNC = regs2.ReadPNCNC();
        state.regs2.PNCND = regs2.ReadPNCND();
        state.regs2.PNCR = regs2.ReadPNCR();
        state.regs2.PLSZ = regs2.ReadPLSZ();
        state.regs2.MPOFN = regs2.ReadMPOFN();
        state.regs2.MPOFR = regs2.ReadMPOFR();
        state.regs2.MPABN0 = regs2.ReadMPABN0();
        state.regs2.MPCDN0 = regs2.ReadMPCDN0();
        state.regs2.MPABN1 = regs2.ReadMPABN1();
        state.regs2.MPCDN1 = regs2.ReadMPCDN1();
        state.regs2.MPABN2 = regs2.ReadMPABN2();
        state.regs2.MPCDN2 = regs2.ReadMPCDN2();
        state.regs2.MPABN3 = regs2.ReadMPABN3();
        state.regs2.MPCDN3 = regs2.ReadMPCDN3();
        state.regs2.MPABRA = regs2.ReadMPABRA();
        state.regs2.MPCDRA = regs2.ReadMPCDRA();
        state.regs2.MPEFRA = regs2.ReadMPEFRA();
        state.regs2.MPGHRA = regs2.ReadMPGHRA();
        state.regs2.MPIJRA = regs2.ReadMPIJRA();
        state.regs2.MPKLRA = regs2.ReadMPKLRA();
        state.regs2.MPMNRA = regs2.ReadMPMNRA();
        state.regs2.MPOPRA = regs2.ReadMPOPRA();
        state.regs2.MPABRB = regs2.ReadMPABRB();
        state.regs2.MPCDRB = regs2.ReadMPCDRB();
        state.regs2.MPEFRB = regs2.ReadMPEFRB();
        state.regs2.MPGHRB = regs2.ReadMPGHRB();
        state.regs2.MPIJRB = regs2.ReadMPIJRB();
        state.regs2.MPKLRB = regs2.ReadMPKLRB();
        state.regs2.MPMNRB = regs2.ReadMPMNRB();
        state.regs2.MPOPRB = regs2.ReadMPOPRB();
        state.regs2.SCXIN0 = regs2.ReadSCXIN0();
        state.regs2.SCXDN0 = regs2.ReadSCXDN0();
        state.regs2.SCYIN0 = regs2.ReadSCYIN0();
        state.regs2.SCYDN0 = regs2.ReadSCYDN0();
        state.regs2.ZMXIN0 = regs2.ReadZMXIN0();
        state.regs2.ZMXDN0 = regs2.ReadZMXDN0();
        state.regs2.ZMYIN0 = regs2.ReadZMYIN0();
        state.regs2.ZMYDN0 = regs2.ReadZMYDN0();
        state.regs2.SCXIN1 = regs2.ReadSCXIN1();
        state.regs2.SCXDN1 = regs2.ReadSCXDN1();
        state.regs2.SCYIN1 = regs2.ReadSCYIN1();
        state.regs2.SCYDN1 = regs2.ReadSCYDN1();
        state.regs2.ZMXIN1 = regs2.ReadZMXIN1();
        state.regs2.ZMXDN1 = regs2.ReadZMXDN1();
        state.regs2.ZMYIN1 = regs2.ReadZMYIN1();
        state.regs2.ZMYDN1 = regs2.ReadZMYDN1();
        state.regs2.SCXIN2 = regs2.ReadSCXN2();
        state.regs2.SCYIN2 = regs2.ReadSCYN2();
        state.regs2.SCXIN3 = regs2.ReadSCXN3();
        state.regs2.SCYIN3 = regs2.ReadSCYN3();
        state.regs2.ZMCTL = regs2.ReadZMCTL();
        state.regs2.SCRCTL = regs2.ReadSCRCTL();
        state.regs2.VCSTAU = regs2.ReadVCSTAU();
        state.regs2.VCSTAL = regs2.ReadVCSTAL();
        state.regs2.LSTA0U = regs2.ReadLSTA0U();
        state.regs2.LSTA0L = regs2.ReadLSTA0L();
        state.regs2.LSTA1U = regs2.ReadLSTA1U();
        state.regs2.LSTA1L = regs2.ReadLSTA1L();
        state.regs2.LCTAU = regs2.ReadLCTAU();
        state.regs2.LCTAL = regs2.ReadLCTAL();
        state.regs2.BKTAU = regs2.ReadBKTAU();
        state.regs2.BKTAL = regs2.ReadBKTAL();
        state.regs2.RPMD = regs2.ReadRPMD();
        state.regs2.RPRCTL = regs2.ReadRPRCTL();
        state.regs2.KTCTL = regs2.ReadKTCTL();
        state.regs2.KTAOF = regs2.ReadKTAOF();
        state.regs2.OVPNRA = regs2.ReadOVPNRA();
        state.regs2.OVPNRB = regs2.ReadOVPNRB();
        state.regs2.RPTAU = regs2.ReadRPTAU();
        state.regs2.RPTAL = regs2.ReadRPTAL();
        state.regs2.WPSX0 = regs2.ReadWPSX0();
        state.regs2.WPSY0 = regs2.ReadWPSY0();
        state.regs2.WPEX0 = regs2.ReadWPEX0();
        state.regs2.WPEY0 = regs2.ReadWPEY0();
        state.regs2.WPSX1 = regs2.ReadWPSX1();
        state.regs2.WPSY1 = regs2.ReadWPSY1();
        state.regs2.WPEX1 = regs2.ReadWPEX1();
        state.regs2.WPEY1 = regs2.ReadWPEY1();
        state.regs2.WCTLA = regs2.ReadWCTLA();
        state.regs2.WCTLB = regs2.ReadWCTLB();
        state.regs2.WCTLC = regs2.ReadWCTLC();
        state.regs2.WCTLD = regs2.ReadWCTLD();
        state.regs2.LWTA0U = regs2.ReadLWTA0U();
        state.regs2.LWTA0L = regs2.ReadLWTA0L();
        state.regs2.LWTA1U = regs2.ReadLWTA1U();
        state.regs2.LWTA1L = regs2.ReadLWTA1L();
        state.regs2.SPCTL = regs2.ReadSPCTL();
        state.regs2.SDCTL = regs2.ReadSDCTL();
        state.regs2.CRAOFA = regs2.ReadCRAOFA();
        state.regs2.CRAOFB = regs2.ReadCRAOFB();
        state.regs2.LNCLEN = regs2.ReadLNCLEN();
        state.regs2.SFPRMD = regs2.ReadSFPRMD();
        state.regs2.CCCTL = regs2.ReadCCCTL();
        state.regs2.SFCCMD = regs2.ReadSFCCMD();
        state.regs2.PRISA = regs2.ReadPRISA();
        state.regs2.PRISB = regs2.ReadPRISB();
        state.regs2.PRISC = regs2.ReadPRISC();
        state.regs2.PRISD = regs2.ReadPRISD();
        state.regs2.PRINA = regs2.ReadPRINA();
        state.regs2.PRINB = regs2.ReadPRINB();
        state.regs2.PRIR = regs2.ReadPRIR();
        state.regs2.CCRSA = regs2.ReadCCRSA();
        state.regs2.CCRSB = regs2.ReadCCRSB();
        state.regs2.CCRSC = regs2.ReadCCRSC();
        state.regs2.CCRSD = regs2.ReadCCRSD();
        state.regs2.CCRNA = regs2.ReadCCRNA();
        state.regs2.CCRNB = regs2.ReadCCRNB();
        state.regs2.CCRR = regs2.ReadCCRR();
        state.regs2.CCRLB = regs2.ReadCCRLB();
        state.regs2.CLOFEN = regs2.ReadCLOFEN();
        state.regs2.CLOFSL = regs2.ReadCLOFSL();
        state.regs2.COAR = regs2.ReadCOAR();
        state.regs2.COAG = regs2.ReadCOAG();
        state.regs2.COAB = regs2.ReadCOAB();
        state.regs2.COBR = regs2.ReadCOBR();
        state.regs2.COBG = regs2.ReadCOBG();
        state.regs2.COBB = regs2.ReadCOBB();

        state.regs2.displayEnabledLatch = regs2.displayEnabledLatch;
        state.regs2.borderColorModeLatch = regs2.borderColorModeLatch;
        state.regs2.VCNTLatch = regs2.VCNTLatch;

        for (size_t i = 0; i < 4; i++) {
            state.renderer.nbgLayerStates[i].fracScrollX = state2.nbgLayerStates[i].fracScrollX;
            state.renderer.nbgLayerStates[i].fracScrollY = state2.nbgLayerStates[i].fracScrollY;
            state.renderer.nbgLayerStates[i].scrollIncH = state2.nbgLayerStates[i].scrollIncH;
            state.renderer.nbgLayerStates[i].lineScrollTableAddress = state2.nbgLayerStates[i].lineScrollTableAddress;
            state.renderer.nbgLayerStates[i].vcellScrollOffset = state2.nbgLayerStates[i].vcellScrollOffset;
            state.renderer.nbgLayerStates[i].vcellScrollDelay = state2.nbgLayerStates[i].vcellScrollDelay;
            state.renderer.nbgLayerStates[i].mosaicCounterY = state2.nbgLayerStates[i].mosaicCounterY;
        }

        for (size_t i = 0; i < 2; i++) {
            state.renderer.rotParamStates[i].pageBaseAddresses = state2.rbgPageBaseAddresses[i];
            state.renderer.rotParamStates[i].Xst = state2.rotParamStates[i].Xst;
            state.renderer.rotParamStates[i].Yst = state2.rotParamStates[i].Yst;
            state.renderer.rotParamStates[i].KA = state2.rotParamStates[i].KA;
        }

        state.renderer.lineBackLayerState.lineColor = state2.lineBackLayerState.lineColor.u32;
        state.renderer.lineBackLayerState.backColor = state2.lineBackLayerState.backColor.u32;

        state.renderer.vdp1State.sysClipH = state1.sysClipH;
        state.renderer.vdp1State.sysClipV = state1.sysClipV;
        state.renderer.vdp1State.userClipX0 = state1.userClipX0;
        state.renderer.vdp1State.userClipY0 = state1.userClipY0;
        state.renderer.vdp1State.userClipX1 = state1.userClipX1;
        state.renderer.vdp1State.userClipY1 = state1.userClipY1;
        state.renderer.vdp1State.localCoordX = state1.localCoordX;
        state.renderer.vdp1State.localCoordY = state1.localCoordY;

        switch (HPhase) {
        default:
        case HorizontalPhase::Active: state.HPhase = savestate::VDPSaveState::HorizontalPhase::Active; break;
        case HorizontalPhase::RightBorder: state.HPhase = savestate::VDPSaveState::HorizontalPhase::RightBorder; break;
        case HorizontalPhase::Sync: state.HPhase = savestate::VDPSaveState::HorizontalPhase::Sync; break;
        case HorizontalPhase::LeftBorder: state.HPhase = savestate::VDPSaveState::HorizontalPhase::LeftBorder; break;
        }

        switch (VPhase) {
        default:
        case VerticalPhase::Active: state.VPhase = savestate::VDPSaveState::VerticalPhase::Active; break;
        case VerticalPhase::BottomBorder: state.VPhase = savestate::VDPSaveState::VerticalPhase::BottomBorder; break;
        case VerticalPhase::BlankingAndSync:
            state.VPhase = savestate::VDPSaveState::VerticalPhase::BlankingAndSync;
            break;
        case VerticalPhase::VCounterSkip: state.VPhase = savestate::VDPSaveState::VerticalPhase::VCounterSkip; break;
        case VerticalPhase::TopBorder: state.VPhase = savestate::VDPSaveState::VerticalPhase::TopBorder; break;
        case VerticalPhase::LastLine: state.VPhase = savestate::VDPSaveState::VerticalPhase::LastLine; break;
        }
    }

    [[nodiscard]] bool ValidateState(const savestate::VDPSaveState &state) const {
        switch (state.HPhase) {
        case savestate::VDPSaveState::HorizontalPhase::Active: break;
        case savestate::VDPSaveState::HorizontalPhase::RightBorder: break;
        case savestate::VDPSaveState::HorizontalPhase::Sync: break;
        case savestate::VDPSaveState::HorizontalPhase::LeftBorder: break;
        default: return false;
        }

        switch (state.VPhase) {
        case savestate::VDPSaveState::VerticalPhase::Active: break;
        case savestate::VDPSaveState::VerticalPhase::BottomBorder: break;
        case savestate::VDPSaveState::VerticalPhase::BlankingAndSync: break;
        case savestate::VDPSaveState::VerticalPhase::VCounterSkip: break;
        case savestate::VDPSaveState::VerticalPhase::TopBorder: break;
        case savestate::VDPSaveState::VerticalPhase::LastLine: break;
        default: return false;
        }

        return true;
    }

    void LoadState(const savestate::VDPSaveState &state) {
        mem1.VRAM = state.VRAM1;
        mem1.FBRAM = state.FBRAM;
        fbIndex.Set(state.displayFB);

        mem2.VRAM = state.VRAM2;
        mem2.CRAM = state.CRAM;

        regs1.WriteTVMR(state.regs1.TVMR);
        regs1.WriteFBCR(state.regs1.FBCR);
        regs1.WritePTMR(state.regs1.PTMR);
        regs1.WriteEWDR(state.regs1.EWDR);
        regs1.WriteEWLR(state.regs1.EWLR);
        regs1.WriteEWRR(state.regs1.EWRR);
        regs1.WriteEDSR(state.regs1.EDSR);
        regs1.WriteLOPR(state.regs1.LOPR);
        regs1.WriteCOPR(state.regs1.COPR);
        regs1.WriteMODR(state.regs1.MODR);

        regs1.fbParamsChanged = state.regs1.FBCRChanged;
        regs1.eraseWriteValueLatch = state.regs1.eraseWriteValueLatch;
        regs1.eraseX1Latch = state.regs1.eraseX1Latch;
        regs1.eraseX3Latch = state.regs1.eraseX3Latch;
        regs1.eraseY1Latch = state.regs1.eraseY1Latch;
        regs1.eraseY3Latch = state.regs1.eraseY3Latch;
        regs1.nextCommandAddress = state.regs1.nextCommandAddress;

        regs2.WriteTVMD(state.regs2.TVMD);
        regs2.WriteEXTEN(state.regs2.EXTEN);
        regs2.WriteTVSTAT(state.regs2.TVSTAT);
        regs2.WriteVRSIZE(state.regs2.VRSIZE);
        regs2.WriteHCNT(state.regs2.HCNT);
        regs2.VCNT = state.regs2.VCNT;
        regs2.WriteRAMCTL(state.regs2.RAMCTL);
        regs2.WriteCYCA0L(state.regs2.CYCA0L);
        regs2.WriteCYCA0U(state.regs2.CYCA0U);
        regs2.WriteCYCA1L(state.regs2.CYCA1L);
        regs2.WriteCYCA1U(state.regs2.CYCA1U);
        regs2.WriteCYCB0L(state.regs2.CYCB0L);
        regs2.WriteCYCB0U(state.regs2.CYCB0U);
        regs2.WriteCYCB1L(state.regs2.CYCB1L);
        regs2.WriteCYCB1U(state.regs2.CYCB1U);
        regs2.WriteBGON(state.regs2.BGON);
        regs2.WriteMZCTL(state.regs2.MZCTL);
        regs2.WriteSFSEL(state.regs2.SFSEL);
        regs2.WriteSFCODE(state.regs2.SFCODE);
        regs2.WriteCHCTLA(state.regs2.CHCTLA);
        regs2.WriteCHCTLB(state.regs2.CHCTLB);
        regs2.WriteBMPNA(state.regs2.BMPNA);
        regs2.WriteBMPNB(state.regs2.BMPNB);
        regs2.WritePNCNA(state.regs2.PNCNA);
        regs2.WritePNCNB(state.regs2.PNCNB);
        regs2.WritePNCNC(state.regs2.PNCNC);
        regs2.WritePNCND(state.regs2.PNCND);
        regs2.WritePNCR(state.regs2.PNCR);
        regs2.WritePLSZ(state.regs2.PLSZ);
        regs2.WriteMPOFN(state.regs2.MPOFN);
        regs2.WriteMPOFR(state.regs2.MPOFR);
        regs2.WriteMPABN0(state.regs2.MPABN0);
        regs2.WriteMPCDN0(state.regs2.MPCDN0);
        regs2.WriteMPABN1(state.regs2.MPABN1);
        regs2.WriteMPCDN1(state.regs2.MPCDN1);
        regs2.WriteMPABN2(state.regs2.MPABN2);
        regs2.WriteMPCDN2(state.regs2.MPCDN2);
        regs2.WriteMPABN3(state.regs2.MPABN3);
        regs2.WriteMPCDN3(state.regs2.MPCDN3);
        regs2.WriteMPABRA(state.regs2.MPABRA);
        regs2.WriteMPCDRA(state.regs2.MPCDRA);
        regs2.WriteMPEFRA(state.regs2.MPEFRA);
        regs2.WriteMPGHRA(state.regs2.MPGHRA);
        regs2.WriteMPIJRA(state.regs2.MPIJRA);
        regs2.WriteMPKLRA(state.regs2.MPKLRA);
        regs2.WriteMPMNRA(state.regs2.MPMNRA);
        regs2.WriteMPOPRA(state.regs2.MPOPRA);
        regs2.WriteMPABRB(state.regs2.MPABRB);
        regs2.WriteMPCDRB(state.regs2.MPCDRB);
        regs2.WriteMPEFRB(state.regs2.MPEFRB);
        regs2.WriteMPGHRB(state.regs2.MPGHRB);
        regs2.WriteMPIJRB(state.regs2.MPIJRB);
        regs2.WriteMPKLRB(state.regs2.MPKLRB);
        regs2.WriteMPMNRB(state.regs2.MPMNRB);
        regs2.WriteMPOPRB(state.regs2.MPOPRB);
        regs2.WriteSCXIN0(state.regs2.SCXIN0);
        regs2.WriteSCXDN0(state.regs2.SCXDN0);
        regs2.WriteSCYIN0(state.regs2.SCYIN0);
        regs2.WriteSCYDN0(state.regs2.SCYDN0);
        regs2.WriteZMXIN0(state.regs2.ZMXIN0);
        regs2.WriteZMXDN0(state.regs2.ZMXDN0);
        regs2.WriteZMYIN0(state.regs2.ZMYIN0);
        regs2.WriteZMYDN0(state.regs2.ZMYDN0);
        regs2.WriteSCXIN1(state.regs2.SCXIN1);
        regs2.WriteSCXDN1(state.regs2.SCXDN1);
        regs2.WriteSCYIN1(state.regs2.SCYIN1);
        regs2.WriteSCYDN1(state.regs2.SCYDN1);
        regs2.WriteZMXIN1(state.regs2.ZMXIN1);
        regs2.WriteZMXDN1(state.regs2.ZMXDN1);
        regs2.WriteZMYIN1(state.regs2.ZMYIN1);
        regs2.WriteZMYDN1(state.regs2.ZMYDN1);
        regs2.WriteSCXN2(state.regs2.SCXIN2);
        regs2.WriteSCYN2(state.regs2.SCYIN2);
        regs2.WriteSCXN3(state.regs2.SCXIN3);
        regs2.WriteSCYN3(state.regs2.SCYIN3);
        regs2.WriteZMCTL(state.regs2.ZMCTL);
        regs2.WriteSCRCTL(state.regs2.SCRCTL);
        regs2.WriteVCSTAU(state.regs2.VCSTAU);
        regs2.WriteVCSTAL(state.regs2.VCSTAL);
        regs2.WriteLSTA0U(state.regs2.LSTA0U);
        regs2.WriteLSTA0L(state.regs2.LSTA0L);
        regs2.WriteLSTA1U(state.regs2.LSTA1U);
        regs2.WriteLSTA1L(state.regs2.LSTA1L);
        regs2.WriteLCTAU(state.regs2.LCTAU);
        regs2.WriteLCTAL(state.regs2.LCTAL);
        regs2.WriteBKTAU(state.regs2.BKTAU);
        regs2.WriteBKTAL(state.regs2.BKTAL);
        regs2.WriteRPMD(state.regs2.RPMD);
        regs2.WriteRPRCTL(state.regs2.RPRCTL);
        regs2.WriteKTCTL(state.regs2.KTCTL);
        regs2.WriteKTAOF(state.regs2.KTAOF);
        regs2.WriteOVPNRA(state.regs2.OVPNRA);
        regs2.WriteOVPNRB(state.regs2.OVPNRB);
        regs2.WriteRPTAU(state.regs2.RPTAU);
        regs2.WriteRPTAL(state.regs2.RPTAL);
        regs2.WriteWPSX0(state.regs2.WPSX0);
        regs2.WriteWPSY0(state.regs2.WPSY0);
        regs2.WriteWPEX0(state.regs2.WPEX0);
        regs2.WriteWPEY0(state.regs2.WPEY0);
        regs2.WriteWPSX1(state.regs2.WPSX1);
        regs2.WriteWPSY1(state.regs2.WPSY1);
        regs2.WriteWPEX1(state.regs2.WPEX1);
        regs2.WriteWPEY1(state.regs2.WPEY1);
        regs2.WriteWCTLA(state.regs2.WCTLA);
        regs2.WriteWCTLB(state.regs2.WCTLB);
        regs2.WriteWCTLC(state.regs2.WCTLC);
        regs2.WriteWCTLD(state.regs2.WCTLD);
        regs2.WriteLWTA0U(state.regs2.LWTA0U);
        regs2.WriteLWTA0L(state.regs2.LWTA0L);
        regs2.WriteLWTA1U(state.regs2.LWTA1U);
        regs2.WriteLWTA1L(state.regs2.LWTA1L);
        regs2.WriteSPCTL(state.regs2.SPCTL);
        regs2.WriteSDCTL(state.regs2.SDCTL);
        regs2.WriteCRAOFA(state.regs2.CRAOFA);
        regs2.WriteCRAOFB(state.regs2.CRAOFB);
        regs2.WriteLNCLEN(state.regs2.LNCLEN);
        regs2.WriteSFPRMD(state.regs2.SFPRMD);
        regs2.WriteCCCTL(state.regs2.CCCTL);
        regs2.WriteSFCCMD(state.regs2.SFCCMD);
        regs2.WritePRISA(state.regs2.PRISA);
        regs2.WritePRISB(state.regs2.PRISB);
        regs2.WritePRISC(state.regs2.PRISC);
        regs2.WritePRISD(state.regs2.PRISD);
        regs2.WritePRINA(state.regs2.PRINA);
        regs2.WritePRINB(state.regs2.PRINB);
        regs2.WritePRIR(state.regs2.PRIR);
        regs2.WriteCCRSA(state.regs2.CCRSA);
        regs2.WriteCCRSB(state.regs2.CCRSB);
        regs2.WriteCCRSC(state.regs2.CCRSC);
        regs2.WriteCCRSD(state.regs2.CCRSD);
        regs2.WriteCCRNA(state.regs2.CCRNA);
        regs2.WriteCCRNB(state.regs2.CCRNB);
        regs2.WriteCCRR(state.regs2.CCRR);
        regs2.WriteCCRLB(state.regs2.CCRLB);
        regs2.WriteCLOFEN(state.regs2.CLOFEN);
        regs2.WriteCLOFSL(state.regs2.CLOFSL);
        regs2.WriteCOAR(state.regs2.COAR);
        regs2.WriteCOAG(state.regs2.COAG);
        regs2.WriteCOAB(state.regs2.COAB);
        regs2.WriteCOBR(state.regs2.COBR);
        regs2.WriteCOBG(state.regs2.COBG);
        regs2.WriteCOBB(state.regs2.COBB);

        regs2.displayEnabledLatch = state.regs2.displayEnabledLatch;
        regs2.borderColorModeLatch = state.regs2.borderColorModeLatch;
        regs2.VCNTLatch = state.regs2.VCNTLatch;

        state1.sysClipH = state.renderer.vdp1State.sysClipH;
        state1.sysClipV = state.renderer.vdp1State.sysClipV;
        state1.userClipX0 = state.renderer.vdp1State.userClipX0;
        state1.userClipY0 = state.renderer.vdp1State.userClipY0;
        state1.userClipX1 = state.renderer.vdp1State.userClipX1;
        state1.userClipY1 = state.renderer.vdp1State.userClipY1;
        state1.localCoordX = state.renderer.vdp1State.localCoordX;
        state1.localCoordY = state.renderer.vdp1State.localCoordY;

        regs2.accessPatternsDirty = true;

        for (size_t i = 0; i < 4; i++) {
            state2.nbgLayerStates[i].fracScrollX = state.renderer.nbgLayerStates[i].fracScrollX;
            state2.nbgLayerStates[i].fracScrollY = state.renderer.nbgLayerStates[i].fracScrollY;
            state2.nbgLayerStates[i].scrollIncH = state.renderer.nbgLayerStates[i].scrollIncH;
            state2.nbgLayerStates[i].lineScrollTableAddress = state.renderer.nbgLayerStates[i].lineScrollTableAddress;
            state2.nbgLayerStates[i].vcellScrollOffset = state.renderer.nbgLayerStates[i].vcellScrollOffset;
            state2.nbgLayerStates[i].vcellScrollDelay = state.renderer.nbgLayerStates[i].vcellScrollDelay;
            state2.nbgLayerStates[i].mosaicCounterY = state.renderer.nbgLayerStates[i].mosaicCounterY;
        }

        for (size_t i = 0; i < 2; i++) {
            state2.rbgPageBaseAddresses[i] = state.renderer.rotParamStates[i].pageBaseAddresses;
            state2.rotParamStates[i].Xst = state.renderer.rotParamStates[i].Xst;
            state2.rotParamStates[i].Yst = state.renderer.rotParamStates[i].Yst;
            state2.rotParamStates[i].KA = state.renderer.rotParamStates[i].KA;
        }

        state2.lineBackLayerState.lineColor.u32 = state.renderer.lineBackLayerState.lineColor;
        state2.lineBackLayerState.backColor.u32 = state.renderer.lineBackLayerState.backColor;

        switch (state.HPhase) {
        default:
        case savestate::VDPSaveState::HorizontalPhase::Active: HPhase = HorizontalPhase::Active; break;
        case savestate::VDPSaveState::HorizontalPhase::RightBorder: HPhase = HorizontalPhase::RightBorder; break;
        case savestate::VDPSaveState::HorizontalPhase::Sync: HPhase = HorizontalPhase::Sync; break;
        case savestate::VDPSaveState::HorizontalPhase::LeftBorder: HPhase = HorizontalPhase::LeftBorder; break;
        }

        switch (state.VPhase) {
        default:
        case savestate::VDPSaveState::VerticalPhase::Active: VPhase = VerticalPhase::Active; break;
        case savestate::VDPSaveState::VerticalPhase::BottomBorder: VPhase = VerticalPhase::BottomBorder; break;
        case savestate::VDPSaveState::VerticalPhase::BlankingAndSync: VPhase = VerticalPhase::BlankingAndSync; break;
        case savestate::VDPSaveState::VerticalPhase::VCounterSkip: VPhase = VerticalPhase::VCounterSkip; break;
        case savestate::VDPSaveState::VerticalPhase::TopBorder: VPhase = VerticalPhase::TopBorder; break;
        case savestate::VDPSaveState::VerticalPhase::LastLine: VPhase = VerticalPhase::LastLine; break;
        }
    }

    // -------------------------------------------------------------------------
    // Memory

    VDP1Memory mem1;
    VDP2Memory mem2;
    FBRAMIndex fbIndex;

    // -------------------------------------------------------------------------
    // Registers and state

    VDP1Regs regs1;
    VDP2Regs regs2;
    VDP1State state1;
    VDP2State state2;

    template <mem_primitive T>
    FORCE_INLINE uint32 MapVDP1FBAddress(uint32 address) const {
        address &= 0x3FFFF & ~(sizeof(T) - 1);
        return address;
    }

    template <mem_primitive T, typename TMemFn = decltype(NoopMemFn<T>)>
    FORCE_INLINE T VDP1ReadFB(uint32 address, TMemFn &&memFn = NoopMemFn) const {
        address = MapVDP1FBAddress<T>(address);
        const T value = util::ReadBE<T>(&mem1.FBRAM[fbIndex.draw][address]);
        memFn(address, value);
        return value;
    }

    template <mem_primitive T, typename TMemFn = decltype(NoopMemFn<T>)>
    FORCE_INLINE void VDP1WriteFB(uint32 address, T value, TMemFn &&memFn = NoopMemFn) {
        address = MapVDP1FBAddress<T>(address);
        util::WriteBE<T>(&mem1.FBRAM[fbIndex.draw][address], value);
        memFn(address, value);
    }

    FORCE_INLINE uint32 MapVDP1RegAddress(uint32 address) const {
        address &= 0x7FFFF & ~(sizeof(uint16) - 1);
        return address;
    }

    template <bool peek, typename TMemFn = decltype(NoopMemFn<uint16>)>
    FORCE_INLINE uint16 VDP1ReadReg(uint32 address, TMemFn &&memFn = NoopMemFn) const {
        address = MapVDP1RegAddress(address);
        const uint16 value = regs1.Read<peek>(address);
        memFn(address, value);
        return value;
    }

    template <bool poke, typename TMemFn = decltype(NoopMemFn<uint16>)>
    FORCE_INLINE void VDP1WriteReg(uint32 address, uint16 value, TMemFn &&memFn = NoopMemFn) {
        address = MapVDP1RegAddress(address);
        regs1.Write<poke>(address, value);
        memFn(address, value);
    }

    // -------------------------------------------------------------------------

    FORCE_INLINE uint32 MapVDP2RegAddress(uint32 address) const {
        address &= 0x1FF & ~(sizeof(uint16) - 1);
        return address;
    }

    template <bool peek, typename TMemFn = decltype(NoopMemFn<uint16>)>
    FORCE_INLINE uint16 VDP2ReadReg(uint32 address, TMemFn &&memFn = NoopMemFn) const {
        address = MapVDP2RegAddress(address);
        const uint16 value = regs2.Read<peek>(address);
        memFn(address, value);
        return value;
    }

    template <bool poke, typename TMemFn = decltype(NoopMemFn<uint16>)>
    FORCE_INLINE void VDP2WriteReg(uint32 address, uint16 value, TMemFn &&memFn = NoopMemFn) {
        address = MapVDP2RegAddress(address);
        regs2.Write(address, value);
        memFn(address, value);
    }

    // -------------------------------------------------------------------------
    // Timings and signals

    // Based on https://github.com/srg320/Saturn_hw/blob/main/VDP2/VDP2.xlsx

    // Horizontal display phases:
    // NOTE: each dot takes 4 system (SH-2) cycles on standard resolutions, 2 system cycles on hi-res modes
    // NOTE: hi-res modes doubles all HCNTs
    //
    //   320 352  dots
    // --------------------------------
    //     0   0  Active display area
    //   320 352  Right border
    //   347 375  Horizontal sync
    //   374 403  VBlank OUT
    //   400 432  Left border
    //   426 454  Last dot
    //   427 455  Total HCNT
    //
    // Vertical display phases:
    // NOTE: bottom blanking, vertical sync and top blanking are consolidated into a single phase since no important
    // events happen other than not drawing the border
    //
    //    NTSC    --  PAL  --
    //   224 240  224 240 256  lines
    // ---------------------------------------------
    //     0   0    0   0   0  Active display area
    //   224 240  224 240 256  Bottom border
    //   232 240  256 264 272  Bottom blanking | these are
    //   237 245  259 267 275  Vertical sync   | merged into
    //   240 248  262 270 278  Top blanking    | one phase
    //   255 255  281 289 297  Top border
    //   262 262  312 312 312  Last line
    //   263 263  313 313 313  Total VCNT
    //
    // Events:
    //   VBLANK signal is raised when entering bottom border V phase
    //   VBLANK signal is lowered when entering VBlank clear H phase during last line V phase
    //
    //   HBLANK signal is raised when entering right border H phase (closest match, 4 cycles early)
    //   HBLANK signal is lowered when entering left border H phase (closest match, 10 cycles early)
    //
    //   Even/odd field flag is flipped when entering last dot H phase during first line of bottom border V phase
    //
    //   VBlank IN/OUT interrupts are raised when the VBLANK signal is raised/lowered
    //   HBlank IN interrupt is raised when the HBLANK signal is raised
    //
    //   Drawing happens when in both active display area phases
    //   Border drawing happens when in any of the border phases

    HorizontalPhase HPhase;
    VerticalPhase VPhase;
};

} // namespace ymir::vdp
