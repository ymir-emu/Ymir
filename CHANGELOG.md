# Ymir changelog

## Version 0.4.0

In development.

Introduces save state file version 14.

### New features and improvements

- App: Added Discord Rich Presence support. (#260, #917; @SternXD)
- App: Added option to File menu to open the screenshots directory.
- App: Added option to unpause emulator when loading discs. Enabled by default, which changes established behavior.
- App: Clarified IPL ROM meaning in the Welcome window and IPL settings tab -- it refers to the BIOS.
- App: Display volume indicator on the top-right corner of the window for a few seconds after adjustments.
- App: Persist SMPC data per region based on the loaded IPL ROM region:
    - `smpc-us_eu.bin`: USA, Europe -- SMPC area codes 4, 5, A, C, D
    - `smpc-jp.bin`: Japan -- SMPC area code 1
    - `smpc-asia.bin`: Korea, Taiwan -- SMPC area codes 2, 6
    - `smpc-other.bin`: Other (invalid) SMPC area codes
    - The old `smpc.bin` will be automatically migrated to these files as you use IPL ROMs for each region.
- App: Shrink embedded M PLUS U font files by removing unused glyphs, reducing binary size. (#915; @4re)
- Debugger: Added RBG0 and RBG1 line color single stack views to the VDP2 debug overlay.
- Debugger: Added basic VDP2 registers view.
- Graphics: New graphics backend, adding support for native graphics APIs:
    - Direct3D 11 on Windows (TBD)
    - Direct3D 12 on Windows (@StrikerX3)
    - Vulkan on Windows and Linux (TBD)
    - Metal on macOS (#929; @SternXD)
    - SDL Renderer wherever it's supported (@StrikerX3)
- Input: Added option to constrain mouse cursor to window in system cursor mode.
- Input: Convert 3D Control Pad analog stick to D-Pad inputs when in digital mode.
- Input: Graduate Virtua Gun to stable feature.
- Input: Introduce a small amount of jitter to the Virtua Gun aim in Death Crimson. Greatly improves shot detection in the game. (#787)
- Media: Added support for MP3 and OGG audio tracks to CUE loader. (#920; @surajrbhardwaj)
- SH2: Interrupt recalculation microoptimizations.
- SMPC: Remove direct dependency to filesystem API for data persistence.
- VDP1: Software renderer performance microoptimizations:
    - Do these once per command instead of per pixel:
        - Determine double density mode
        - Get references to VDP1 registers
        - Shift/mask color bank values
- VDP1/VDP2: Hardware-accelerated rendering is now available. Supported graphics backends:
    - Direct3D 12 on Windows (@StrikerX3)

### Fixes

- App: Don't persist window geometry in full screen mode.
- App: Initialize MIDI subsystem asynchronously. Fixes some cases where the application takes a long time to start up.
- App: Reset window geometry if it matches the bounds of a display to prevent "remember window geometry" from loading full screen coordinates.
- Backup RAM: Fix crash when attempting to load a backup RAM cartridge with the default path.
- Backup RAM: Remove strict language check when importing files. Fixes importing save files from Kronos's backup RAM cartridges. (#942)
- CD Block (HLE): Report current CD status and raise CMOK HIRQ signal when processing unimplemented commands.
- CD Block (HLE): Read reset position flag correctly from parameters.
- CD Block (LLE): Fix audio track playback failing for tracks 16 and higher. Fixes Virtual On - Cyber Troopers credits and the songs in certain arenas.
- GameDB: Disable a number of game-specific hacks that are no longer required:
    - Chisato Moritaka - Watarase Bashi & Lala Sunshine: no longer crashes at startup. (#604)
    - Deep Fear: no longer freezes after the "April Fools!" voice line. (#740)
    - Marvel Super Heroes vs. Street Fighter: no longer crashes at the end of the Capcom logo. (#507)
    - Mega Man X3 / Rockman X3: sprites are not glitched anymore. (#244)
    - Metal Fighter Miku: goes in-game, no longer stuck after start menu. (#466)
    - Soviet Strike: VDP1 graphics no longer flicker.
- GameDB: Force-enable SH-2 cache emulation for multiple games:
    - Dino Island: fix palette glitches. (#764)
    - Dragon Force II: fix black screen when entering 100 vs 100 battles. (#945)
    - Hissatsu!: fix crash at startup. (#943)
    - No-appointment Gals Olympos: fix crash during animations.
- GameDB: Force fast bus timings on Resident Evil to fix start menu crashes. (#907)
- GameDB: Slow down VDP1 execution speed in 3D Baseball to fix team name plates and announcer voice line glitches. (#593)
- GUI: Reinitialize style from scratch when rescaling GUI elements. Fixes Settings windows (and probably others) from growing extremely large when constantly switching display scales.
- Input: Set least significant bits of 3D Control Pad analog reports to all ones. Fixes double inputs in Deep Fear. (#744)
- Input: Make 3D Control Pad peripheral report in digital mode match the Standard Pad. Fixes broken inputs in digital mode in the following games:
    - Bug Too! (#622)
    - Black/Matrix (#861)
- Media: Don't read the Path Table past the size specified in the Volume Descriptor. Fixes CD Block HLE not able to read disc images made with some lazy patches that don't properly clean up the Path Table. (#912)
- Media (CUE): Don't accumulate pre/postgaps multiple times per track. Fixes some audio track offset issues for single-BIN dumps. (#146)
- Media (CUE): Properly compute track count on discs with sparse track numbers. Fixes audio playback on some homebrew discs that omit tracks (e.g. The Rockin'-B All Stars version 06/03/23 skips track 2).
- Media (CUE): Use CUE sheet timestamps to compute track lengths. Fixes some audio track offset issues for single-BIN dumps. (#146)
- SCU: Allow 8-bit writes to DSP registers.
- Settings: Apply CD Block LLE configuration at startup. Fixes Ymir always launching with CD Block HLE mode when LLE was enabled in the settings file.
- SMPC: Ignore SSHON command when the Slave SH2 is already enabled. Fixes multiple issues:
    - Guardian Heroes no longer gets stuck on level transitions. (#951)
    - Gekitotsu Koushien no longer crashes when going in-game. (#576)
    - Madden NFL 97 (Europe) now goes in-game. (#522)
    - No-appointment Gals Olympos now goes in-game.
    - Ten Pin Alley now goes in-game.
    - UEFA Euro 96 - England now goes in-game. (#486)
- SMPC: Update peripheral PDR1/2 registers when reading and when updating EXLE. Fixes many cases of games not recognizing Virtua Gun inputs or missing shots. (#787)
- VDP1: Don't clear COPR when beginning a new VDP1 frame. Fixes lockups in Alone in the Dark - Alone in the Dark - One-Eyed Jack's Revenge. (#938)
- VDP1: Don't sync VDP1 FBRAM on debug reads. Fixes deadlock when viewing the framebuffer area in a memory viewer window.
- VDP1: Force-align MSB write to 16-bit addresses when writing pixels with MSB enabled. Fixes deselected menu options being fully painted in black in Derby Analyst. (#587)
- VDP1: Textured sprites with CMDSIZE.H=0 never fetch additional texels. Fixes glitched graphics in the scorecard of the shooting range in Policenauts.
- VDP1-SW: Write back FBRAM writes from VDP1 renderer thread. Fixes glitched title screen in Waialae no Kiseki - Extra 36 Holes when using threaded VDP1 rendering. (#898)
- VDP2: Avoid unintentional side effects on VDP2 EXTEN register when saving states. Fixes camera angles in Digital Dance Mix Vol. 1 - Namie Amuro.
- VDP2: Disable color gradation if color RAM mode is not 0. Fixes the fog effect in The River of Dreams level in Astal. (#927)
- VDP2: Fix coordinate latching on external latches. Fixes various Virtua Gun shot offset errors. (#787)
- VDP2: Fix off-screen coordinate latching. Fixes some Virtua Gun reload detection issues. (#787)
- VDP2: Restrict color calculations in certain video modes. Fixes Sound Test screen text blending in with the background in Dark Savior.
    - In high resolution modes with color RAM modes other than 0, color calculations can only be applied on top of RGB layers, but not palette layers.
- VDP2-SW: Fix out of bounds access when the display resolution is changed mid-frame while drawing an RBG.
- VDP2-SW: Stop updating LNCL/BACK screen once DISP is disabled for the remainder of the frame.


## Version 0.3.3

Released 2026-06-21.

Uses save state file version 13.

### New features and improvements

- GUI: Specify missing ImGui style colors.
- System: Add Europe PAL and Asia NTSC (in that order) to default preferred regions after North America and Japan.
- VDP2: Implemented color gradation effect. Fixes background in the Egg Birdon boss arena of Saturn Bomberman.

### Fixes

- System: Properly autoselect IPL ROMs for Asia NTSC region (South Korea, Taiwan).
- VDP1: Don't automatically mark jumps to 0 as infinite loops. Fixes Akumajou Dracula X no longer booting. (#904)


## Version 0.3.2

Released 2026-06-21.

Introduced save state file version 13.

### New features and improvements

- App: Added action to reload save states from disk to the File menu.
- App: Added disc serial number to System State window.
- App: Added M PLUS U font for Japanese text support and convert Japanese backup RAM text to the appropriate characters. (#18)
- App: Added option to show/hide game name from window title bar.
- App: Added option to show/hide performance indicators from window title bar.
- App: Added option to start emulation paused upon launching the emulator. (#899)
- App: Added Message History window, listing up to 10000 messages.
- App: Display Japanese text in the Backup Memory Manager and on window title bar (game title).
- App: Removed graphics backend option as it served no purpose at this time. Ymir will rely on the default backend used by the SDL3 Renderer API, which should be compatible with most systems.
    - Note: GPU rendering will include a rewrite of the app's graphics subsystem, moving away from SDL Renderer to our own implementation.
- App: Rotate screenshots based on selected display rotation.
- Debug: Added CD Block Partitions view, listing all buffers currently loaded in each partition.
- Debug: Avoid infinite loop when SH2 debugger window is collapsed. Fixes UI hang when (re)opening the window once it was collapsed. (#690)
- Debug: Simplify SH2 watchpoints down to simple read/write toggles per byte.
    - Previously set watchpoints will be migrated to the new format automatically. 16-bit watchpoints are converted to two consecutive watchpoints, and 32-bit watchpoints are converted to four consecutive watchpoints.
    - 16-bit and 32-bit accesses will now trigger watchpoints on any byte accessed by them. For example, a read watchpoint set on 6010003 will be triggered by 8-bit reads from 6010003, 16-bit reads from 6010002 or 32-bit reads from 6010000.
    - When a watchpoint is triggered, Ymir will now display the address and size of the access and which watchpoints were triggered.
- SCSP: Added Threaded SCSP option, bringing a decent performance boost in nearly all situations. (#3, #896; @SternXD)
- SCU: Optimize DSP operations involving 48-bit registers.
- SH2: Clock ratio option ranging from 25% up to 500%. (#876, #895; @SternXD)
- SH2: Decode opcode arguments directly in instruction handlers for better performance.
- SH2: Fetch pairs of instructions using 32-bit accesses, more closely matching the behavior of the real SH7095. Improves overall emulation accuracy and slightly boosts performance on SH2-bound games.
- SH2: Handle WB/EX stalls. (thanks to @celeriyacon)
- SH2: Microoptimize DIV1 instruction.

### Fixes

- App: Temporarily switch from exclusive to borderless fullscreen when opening a file dialog then back to exclusive mode once the dialog is closed to avoid minimizing or resizing the window.
- CD Block (HLE): Don't change status code when executing Get TOC. Fixes Mass Destruction not playing any music in-game. (#664)
- CD Block (HLE): Extend seek times to 5 ticks. Fixes animations in Digital Dance Mix Vol. 1 - Namie Amuro when seeking or pausing and resuming tracks. (#476)
- CD Block (HLE): Extend sizes of Mode 2 Form 2 sectors read from disc when the get sector length is less than 2324 bytes. Fixes:
    - Chisato Moritaka - Watarase Bashi & Lala Sunshine (#604): FMVs now play correctly.
    - Digital Dance Mix Vol. 1 - Namie Amuro (#476): animations and song now play correctly.
- CD Block (HLE): Fix handling of "Get Session Info" command.
- CD Block (HLE): Fix handling of "Play Disc" command with resume from pause parameters on data tracks. Fixes buffer exhaustion leading to softlocks in Panzer Dragoon II Zwei.
- CD Block (HLE): Fix handling of "Seek Disc" command parameter exceptions.
- CD Block (HLE): Handle more "Play Disc" command parameter nuances. Fixes animations in Digital Dance Mix Vol. 1 - Namie Amuro when seeking or pausing and resuming tracks. (#476)
- CD Block (HLE): Ignore read speed setting and always use 2x for data tracks. Fixes some cases of FMVs stuttering or playing too slowly:
    - Mass Destruction (#664)
    - Tilt! (#872)
- CD Block (HLE): Introduce separate array for command responses.
- CD Block (HLE): Open tray when ejecting a disc. Fixes various cases of games that hang, freeze, crash or exhibit erratic behavior when ejecting a disc while playing. These should now properly boot back to the system's interactive shell. (#298)
- CD Block (HLE): Properly resume disc playback after being paused due to running out of buffers. Fixes multiple games:
    - Chou Jikuu Yousai Macross - Ai Oboete Imasu ka (#605): intro sequence now plays correctly.
    - Dragon Force (#866): Working Designs logo FMV now plays correctly.
    - Legend of K-1 - The Best Collection (#758): FMV of third fight now plays correctly.
    - LifeScape - Seimei 40 Okunen Haruka na Tabi (#598): no longer freezes on opening FMV.
    - Mahjong Yon Shimai - Wakakusa Monogatari (#527): no longer freezes on character select screen.
    - Shellshock (#344): no longer crashes on CORE logo.
    - Senken Kigyouden / Xianjian Qixiazhuan (#409): graphics now load correctly.
    - Sonic Jam (#83): in-game music now plays correctly.
    - IRREEL (homebrew) (#580): level 2 assets now load correctly.
- CD Block (HLE): Use saved start/end play parameters when checking for FAD/track. Fixes music not looping in Mass Destruction. (#664)
- Input: Properly bind inputs loaded from settings file upon startup. (#900)
- Media: Bail out early on CUE parser if the file starts with a null byte.
- Media: Ensure a valid Saturn disc is loaded before trying to parse the filesystem. Fixes crashes when trying to load non-Saturn discs.
- Media: Fix crash when attempting to load CHDs with non-ASCII characters in the file name.
- Rewind: Fix crash when attempting to rewind to the very first frame. (#857)
- Rewind: Fix occasional crash when attempting to rewind with CD Block LLE enabled.
- Settings: Correctly load virtual RTC settings from configuration file.
- SH2: Cache line fills only cost one access to the SH2.
- SH2: DMAC 16-byte burst should read 4 longwords then write 4 longwords.
- SH2: Fix 8-bit and 16-bit reads from and writes to cache address array and associative purge areas. Fixes graphics glitches in Dark Savior. (#134) (thanks to @celeriyacon)
- VDP1: Add one-pixel offset to vertical coordinates of axis-aligned sprites when rendering high-resolution modes with deinterlacing enabled. Fixes gaps in Sonic Jam and Astra Superstars. (#784, #853)
- VDP1: Add one-pixel offset to vertical coordinates of clipping coordinates. Fixes one-pixel gap at the bottom of the screen in Sonic Jam. (#853)
- VDP1: Branch, link and COPR fixes. (thanks to @celeriyacon)
- VDP1: Don't end frame drawing when an infinite loop is detected. Fixes Stellar Assault no longer booting.
- VDP1: Gouraud shading was not incremented in transparent pixels. Fixes character shading in Magical Night Dreams - Cotton 2. (#728)
- VDP1: Illegal commands cause VDP1 to stay stuck in place rather than abort execution.
- VDP1: Synchronize reads and writes to FBRAM. Fixes graphics issues when threaded VDP1 rendering is enabled in multiple games: (#898)
    - BlackFire (#790): GUI elements no longer flicker.
    - Burning Rangers (#791): transparent elements (such as fire and explosions) are no longer drawn behind walls, doors, etc.
    - Scorcher (#281, #473): HUD is now drawn on top of level geometry and game no longer crashes in Training mode.
    - Virtual Golf (Europe) (#830): ground is now fully rendered and random sprites no longer appear.
    - Waialae no Kiseki - Extra 36 Holes (#871): ground is now fully rendered.
- VDP1: Use all 16 bits for system and user clipping coordinates. Fixes black title screen on the European version of Revolution X - Music Is the Weapon.
- VDP2: Handle another case of illegal CP/PN access combinations involving multiple PN accesses in different banks. Fixes shifted text frames in Ryuuteki Gosennen - Dragons of China. (#856)
- VDP2: Latch current VCNT when reading from EXTEN. Fixes camera angles in Digital Dance Mix Vol. 1 - Namie Amuro. (#476)
- VDP2: Update horizontal scroll increment every scanline. Fixes in-game terrain graphics drawn half as wide in Shellshock. (#344)


## Version 0.3.1

Released 2026-05-03.

Uses save state file version 12.

### New features and improvements

- Debugger: Add Priority Stack to VDP2 debug overlay.
- VDP2: Various performance optimizations to the software renderer, improving performance in graphics-bound games (especially in high resolution modes). Technical details:
    - RBG1 was being unnecessarily rendered when NBG0 was enabled and RBG1 was disabled
    - Remove redundant transparency bit since priority zero acts as transparency 
    - Remove various unused function arguments, reducing CPU register and stack pressure
    - Pass down VDP2 register references as arguments rather than fetching them from scratch everywhere
    - Reuse line data (i.e. skip recomputing lines) when rendering vertical mosaic
    - Bit-pack character pattern data in a single 32-bit value
    - Microoptimize scroll coordinates calculations
    - Rotation parameter table calculations:
        - Specialize calculations for (1) no coefficient table, (2) per-line coefficient table and (3) per-dot coefficient table
        - Hoist coefficient table enable and per-dot coefficients checks out of the loop
        - Make the inner loops SIMD-friendly
        - Avoid unnecessary computation of transparency and line color screen data if they are disabled
    - Microoptimizations to the compositor function:
        - Layer sorting now uses a combined key
        - Gather layer data in one single loop
        - Pixel fetching is now templatized and some bail out checks are done earlier
        - Color offset calculation enable flag is cleared in the fetch loop if no offset is actually applied

### Fixes

- Debugger: Various SH2 stack analysis fixes.
- GameDB: Add slow VDP1 flag to Funky Fantasy to fix slow FMVs.
- Media: Detect postgaps in data tracks when the CUE sheet doesn't explicitly announce them. Fixes audio track shifts in bad Virtua Fighter 2 dumps.
- Media: Fix PREGAP/POSTGAP handling in CUE/BIN dumps. Fixes shifted audio tracks in games dumped with ImgBurn.
- SH2: Fix illegal slot instruction exception handling. (thanks to @celeriyacon)
- VDP1: Add game-specific flag for skipping command processing if the top of the table is empty. Enable it exclusively for Sekai no Shasou kara - I Swiss-hen - Alps Tozantetsudou no Tabi. Fixes missing graphics in Gungriffon. (#810)
- VDP1: Disable early polygon drawing termination when rendering polygons when user clipping mode is inverted. Fixes clipped polygons around the minimap in Machine Head (#767).
- VDP1: Implement simple infinite loop detection. Fixes slowdown in the Mojave Desert stage (1-2) in Gale Racer.
- VDP2: Apply color calculations to transparent sprite mesh on layer 0. Fixes stripes on ground plane in Gungriffon.
- VDP2: Apply color offset to transparent sprite mesh on layer 0 in a separate step. Fixes missing spotlight in the Colonel battle in Mega Man X4. (#818)
- VDP2: Fix VRAM access calculations when RBG1 is enabled. Fix missing car graphics regression in Gale Racer. (#359)
- VDP2: Fix and use line color screen calculation ratio when LNCL is inserted. Fixes text background issues in Doukoku Soshite. (#502)


## Version 0.3.0

Released 2026-04-23.

Introduced save state file version 12.

### New features and improvements

- App: Add command line option `-F`/`--fast-forward` to launch the emulator in fast-forward mode.
- App: Always use installed mode under Flatpak.
- App: Check for profile at the executable location. (#706)
- App: Warn users about Flatpak filesystem permissions if the app is running in its sandbox and a disc image fails to load.
- Backup RAM: Support in-memory and copy-on-write memory-mapped files in addition to regular memory-mapped files.
- Build: Add simple feature flags system. All feature flags are enabled by default on development builds (including nightly builds).
- Build: Support Profile-Guided Optimization (PGO) builds. (#742; @mmkzer0)
- Debugger: Allow scrolling the SH2 disassembly view. (#743; @mmkzer0)
- Debugger: Colorize VRAM access timing slots in the delay viewer.
- Debugger: Implement keyboard navigation and interactions in the SH2 disassembly view:
    - Up/down arrow keys: move cursor up/down one instruction.
    - Page up/down: move cursor up/down one page.
    - Home/end: move cursor to the top/bottom of the viewport.
    - The cursor is kept below 15% of the top and above 35% of the bottom of the viewport.
- Debugger: Manage SH2 breakpoints and watchpoints on the frontend and allow enabling/disabling them without removing from the list.
- Debugger: Optimize SH2 breakpoints and watchpoints when debug tracing is enabled. They no longer become more expensive with the amount of entries added and the baseline cost is lower than before.
- Debugger: Trace and display SH2 call stack.
- Debugger: Trace and display SH2 data stack contents.
- GameDB: Add new game-specific flags to improve compatibility:
    - Double the clock rate of the MC68EC000
    - Stall VDP1 drawing on VRAM writes
    - Slow down VDP1 rendering
    - Relax VDP2 bitmap CP VRAM access checks
- Input: Added support for mouse events.
- Input: Capture mouse for light gun and mouse peripherals, supporting these modes:
    - System mouse: binds the system mouse cursor to a single peripheral. Mouse cursor is still available to interact with the GUI.
    - Physical mouse: binds one or more mice to different peripherals. Disables the system cursor while any mice is bound.
    NOTE: This option is only available on nightly builds due to issues with Virtua Gun. (#787)
    The System mouse capture option only works with Virtua Gun.
- Input: Experimental Virtua Gun peripheral implementation. (#33)
    NOTE: This feature is only available on nightly builds at the moment due to issues in nearly all games. (#787)
    Only House of the Dead is known to work with minor reticle inaccuracy errors.
- Input: Implemented Shuttle Mouse peripheral. (#32)
- MIDI: Force RtMidi to use dummy API if it fails to initialize, allowing Ymir to run without MIDI drivers.
- Save states: Added actions to undo a save state and restore an undone save state. (#700, #727; @Fueziwa)
- Save states: Store one extra save state per slot for undo. (#700, #727; @Fueziwa)
- Settings: Show currently loaded profile path in Settings > General.
- System: You can now select a preferred system variant (Saturn, HiSaturn, V-Saturn or Dev Kit) and Ymir will automatically pick a matching IPL ROM. (#637, #725; @Fueziwa)
- Video: Add option to enable/disable video synchronization in full screen mode.
- Video: Allow selecting full screen resolution and target display. (#705)
- Video: Allow switching graphics backends for GUI rendering.

### Fixes

- Build: Introduced separate x64-win-llvm toolchains for SSE2 and AVX2 support. Fixes Windows SSE2 builds requiring SSE4.2 instructions. (#713; thanks to @Wunkolo)
- Build: Perform ad-hoc signature on macOS binaries to work around the "damaged" app warning. (#698; thanks to @Wunkolo)
- Build: Remove duplicate binary from macOS packages.
- GameDB: Double the MC68EC000 clock rate and force fast bus timings to fix crashes in Vampire Savior - The Lord of Vampire. (#699)
- GameDB: Force-enable SH2 cache emulation to fix issues with multiple games:
    - Baku Baku Animal - World Zookeeper Contest (Europe only) -- freeze when trying to play FMVs from the Options menu (#642)
    - Chisato Moritaka - Watarase Bashi & Lala Sunshine -- crash at startup (#604)
    - Dragon Ball Z - Idainaru Dragon Ball Densetsu -- black screen after starting a new game (#538)
    - Emit Vol. 3 - Watashi ni Sayonara o -- FMV tearing (#797)
    - Metal Fighter Miku -- black screen after start menu (#466)
    - Spot Goes to Hollywood -- glitched graphics in European version only (#520)
    - Steamgear Mash -- flickering graphics (#440)
    - Waku Waku 7 -- flickering sprites (#424)
- GameDB: Force fast bus timings to fix crashes in Deep Fear. (#740)
- GameDB: Slow down VDP1 to fix no-boot regression in Jikkyou Oshaberi Parodius. (#283)
- GameDB: Slow down VDP1 to fix performance issues in Fishing Koushien II. (#812)
- Input: Fixed analog to D-Pad axis conversion to not overwrite whenever an input was released in opposite direction. (#754; @PringleElUno)
- MIDI: Defend against crashes when the library fails to initialize.
- SCU: Timer fixes. (thanks to @celeriyacon)
- SH2: Block interrupts on instructions following LDC/LDS/STC/STS. (thanks to @celeriyacon)
- SH2: Cache emulation fixes. (thanks to @celeriyacon)
- SH2: Fix `@(disp.PC)` loads being decoded as stores for watchpoints.
- SH2: Fix `ldc/lds @Rm` decoding from the wrong opcode bits for watchpoints.
- SH2: Interrupt prioritization and triggering fixes. (thanks to @celeriyacon)
- VDP1: Fix handling of zero horizontal character size in CMDSIZE.
- VDP1: Fix swap framebuffers race condition with threaded VDP1 rendering. Fixes flickering graphics in multiple games:
    - Actua Golf (#794)
    - FIFA - Road to World Cup 98 (#800)
    - Gran Chaser (#763)
- VDP1: Increase PTM=1 drawing delay and apply it only during VBlank. Fixes flickering graphics on Earthworm Jim 2. (#745)
- VDP1: Properly load save state data when threaded VDP1 rendering is enabled.
- VDP1: Rework cycle counting method and increase cycle budget per frame. Fixes slowdowns in Road Rash and graphics glitches in multiple games, including Virtua Cop and Burning Rangers. (#704, #721, #722)
- VDP1: Stall VDP1 drawing on VRAM writes exclusively on Mega Man X3 and Rockman X3 to fix garbled sprites. (#244)
- VDP1: Stop processing commands if encountering an all-zeros entry. Fixes invalid clipping coordinates in Sekai no Shasou kara - I Swiss-hen - Alps Tozantetsudou no Tabi. (#761)
- VDP2: Apply VRAM access shift per bank to scroll NBGs with invalid timing patterns. Fixes World Heroes Perfect title screen shift and Cyberbots - Fullmetal Madness HUD shift and broken background in stage 2. (#756)
- VDP2: Clear normal shadow flag on transparent sprite pixels. Fixes shadows extending vertically across the screen in Tokyo Shadow. (#752)
- VDP2: Compute vertical cell scroll delays/offsets when enabling/disabling the effect in addition to access cycle changes.
- VDP2: Consolidate sprite data handling and fix 16-bit readout of 8-bit sprite data. Fixes garbled graphics in NBA Live 98 in-game.
- VDP2: Convert the "allow bitmap data access during SH-2 cycles" hack into a game-specific flag and enable it only for games that display issues with the strict timing checks:
    - Lunar - Silver Star Story
    - Mechanical Violator Hakaider
    - Shin Kaitei Gunkan
- VDP2: Fix and optimize per-dot coefficient access checks. Fixes graphics glitches in Radiant Silvergun when starting a new game after interrupting the AKA-O boss fight in attract mode.
- VDP2: Fix NBG per dot special priority calculations. Fixes priority issues in Mr. Bones. (#703)
- VDP2: Illegal scroll CP accesses cause a shift if there aren't enough valid accesses in other banks and the illegal accesses occurs in the same bank as the PN access. Fixes fog background shift in Sonic 3D Blast. (#798)
- VDP2: Illegal scroll CP accesses in low-res modes are handled differently between T0-T3 and T4-T7. Fixes gaps in backgrounds in X-Men vs. Street Fighter. (#775)
- VDP2: Multiple VC accesses in the same timing slot do not cause extra delays. Fixes FMV glitches in Girls in Motion Puzzle Vol. 1 - Hiyake no Omoide + Himekuri. (#466)
- VDP2: Sprite special pattern detection was short by one bit.
- VDP2: Use only the first PN access to check for valid CP accesses. Fixes graphics shift in Daisuki and BattleSport. (#769, #770)


## Version 0.2.1

Release 2026-01-10.

Introduced save state file version 11.

### New features and improvements

- App: Added "Take Screenshot" action to File menu.
- App: Moved "Backup Memory Manager" action from File to System menu.
- Debugger: Added `Dump memory region` button to the Memory Viewer to dump the currently selected region to a raw .bin file in the profile’s dump path. (#678; @mmkzer0)
- Debugger: Display VRAM data access shifts separately from CP delays in the VDP2 VRAM access delay window and display them for scroll BGs as well.
- Rewind: Reset rewind buffer on hard resets and whenever the CD Block LLE settings is changed.
- Save states: Separated save states menu into Load and Save to avoid confusion and simplify interactions.
- Updater: Added compile-time flag `Ymir_ENABLE_UPDATE_CHECKS` to enable or disable the automatic update checker, including the onboarding process.
- VDP1: Reworked renderer code structure. Allows threading without affecting timings and paves way for other rendering backends. (#233)

### Fixes

- Settings: Properly reload binds for "increase/decrease speed by 25%".
- Updater: Create `state/updates` folder before writing `.onboarded` file in case the user opts out of automatic update checks. Fixes update check onboarding popup always appearing on startup. (#708)
- Updater: Detect nightly -> stable update when the current version matches the stable version.
- VDP1: Reduce VBlank erase timing penalty to fix graphics artifacts in Battle Garegga's options menu. (#649)
- VDP2: Allow bitmap data access during SH-2 cycles. Fixes FMV flickering in Lunar - Silver Star Story, Shin Kaitei Gunkan, and Mechanical Violator Hakaider (#438, #446, #658)
- VDP2: Apply VRAM bank data access shift to scroll BGs. Fixes shifted borders in World Heroes Perfect's demo sequence. (#643)
- VDP2: Disable VRAM access restrictions based on ZMCTL parameters. Fixes missing background graphics in Baku Baku Animal - World Zookeeper. (#646)


## Version 0.2.0

Released 2025-10-12.

Introduced save state file version 10.

### New features and improvements

- App: Added option to check for updates on startup. Also added manual update check action. (#110)
- Build: Create macOS app bundle. (#591; @tegaidogun)
- Cart: Add 6 MiB development DRAM cartridge, required by the Heart of Darkness prototype. (#584)
- CD Block: Implemented optional low-level emulation mode. Requires valid CD Block ROMs and has considerable performance cost, but fixes numerous issues when enabled:
    - Gunbird music no longer stops when pausing and resuming the game (#625)
    - Mr. Bones is now 100% stable (#494)
    - Pocket Fighter's audio and video are now in sync (#222)
    - X-Men: Children of the Atom no longer hangs on the loading screen (#488)
    - X-Men vs. Street Fighter and Marvel vs. Street Fighter no longer hang at the end of the Capcom logo (#507)
    - Several games now boot properly:
        - Primal Rage (USA) (#225)
        - Hop Step Idol (#512)
        - Hissatsu Pachinko Collection (#536)
        - DonPachi (#475)
        - Shichuu Suimei Pitagraph (#549)
        - Deroon Dero Dero (#501)
        - Sol Divide (#470)
        - ... and probably more
- GameDB: Apply game-specific settings directly in the emulator core rather that from the frontend.
- GameDB: Include Heart of Darkness prototype to automatically insert the 6 MiB development DRAM cartridge, allowing it to go in-game. (#584)
- GameDB: Introduce flag to force fast bus timings to work around issues with X-Men/Marvel Super Heroes vs. Street Fighter. (#507)
- GameDB: Support for using disc hashes to the database in addition to product codes.
- M68K, SH2: Implement approximate bus access timings. Fixes softlocks in Resident Evil, Shichisei Toushin Guyferd - Crown Kaimetsu Sakusen, and Densha de Go. (#41, #42, #333, #523)
- Media: Support WAVE audio tracks.
- SH2: Cycle count DMAC transfers. Necessary for CD Block LLE.
- SH2: Optimize watchpoint checks to reduce performance penalty when debug tracing is enabled.
- SMPC: Preinitialize OREG31 to 0xF0 to avoid lockup when attempting to boot the dev kit BIOS. (#612)
- VDP2: Added CRAM palette viewer/editor.
- VDP2: Added debug overlays: single layer view, layer stack, window states, RBG0 rotation parameters, color calculations, and shadows.

### Fixes

- App: Avoid crash if the `<profile>/roms/cart` folder is deleted while the emulator is running and the user loads a game that needs to load a cartridge from that folder.
- CD Block: Properly initialize internal filesystem state and remove unnecessarily strict save state check. Fixes crashes related to the rewind buffer.
- GUI: Limit maximum size of various windows.
- Media: Allow loading CHDs that don't contain raw sector data, such as those created from ISOs.
- Media: Restrict ISO loader to files with the .iso extension to prevent users from loading .bin files instead of the .cue files.
- Media: Tracks now include the unit sizes along with sector sizes, only needed for CHDs.
- Rewind: Allow varying the size of the state struct. Fixes occasional crashes when rewinding.
- SCU: Delay immediate transfer interrupt signals based on the transfer length. Fixes multiple hang/freeze/crash issues:
    - Advanced V.G. (#227)
    - Angel Graffiti S - Anata e no Profile (#461)
    - Arcade Gears Vol. 1 - Pu-Li-Ru-La (#468)
    - DeJig games (#399, #541)
    - Dream Square - Hinagata Akiko (#361)
    - Ferox prototype (#609)
    - GeGeGe no Kitarou - Gentou Kaikitan (#396)
    - Gekka no Kishi - Ouryuu-sen (#353)
    - Goiken Muyou - Anarchy in the Nippon (#556)
    - Horror Tour (#319)
    - Kuro no Danshou - The Literary Fragment (#610)
    - Mahou Shoujo Pretty Samy - Heart no Kimochi (#431)
    - Marie no Atelier Ver. 1.3 - Salburg no Renkinjutsushi (#619)
    - Mario Mushano no Chou Shougi Juku - Mario Mushano's Hyper Shogi School (#430)
    - Nonomura Byouin no Hitobito (#436)
    - Pastel Muses (#380)
    - Tenchi Muyou Rensa Hitsuyou (#339)
    - Several homebrew apps (#620, #626)
- SCU: Make DMA transfers interruptible to support LLE CD Block interactions.
- VDP1: Adjust Y coordinate framebuffer offsets for erase process based on TVMR.TVM. Fixes erase glitches in Grandia when using transparent meshes.
- VDP2: Bitmap delays only occur if the timings are mapped to different VRAM chips, not banks. Fixes right team name plate being shifted left during game intro in 3D Baseball. (#593
- VDP2: Clear framebuffer when switching resolutions. Fixes single-frame artifacts in multiple games that switch modes without clearing the screen.
- VDP2: Shift one cell of 2x2 character patterns which have illegal access cycles. Fixes garbled text in Shichuu Suimei Pitagraph. (#549)
- VDP2: Use line color calculation ratio when LNCL is inserted on top of a layer that uses color calculations. Fixes text dialog background in Find Love 2 - The Prologue. (#618)
- VDP2: Use TVMD.DISP from threaded state if rendering with dedicated VDP2 thread. Fixes black stripes on the bottom of the screen in Bug! (#623)


---

## Version 0.1.8

Released 2025-09-09.

Introduced save state file version 9.

### New features and improvements

- App: Disable rounded window corners on Windows 11.
- App: Implement exception handler for macOS. (#460; @Wunkolo)
- App: Provide feedback to the user if any part of the app initialization fails.
- App: Show warning dialog if the user is missing the required ROM cartridge images for games that require them.
- Backup RAM: Per-game internal backup RAM file names changed from `bup-int-[<game code>] <title>.bin` to `bup-int-<title> [<game code>].bin` to allow sorting files alphabetically in file browsers. Existing files will be automatically renamed as they are loaded.
- Build: ARM64 Windows support. (#483; @tordona)
- Build: FreeBSD support for ARM64 systems. (#421; @bsdcode)
- Cart: Automatically insert Backup RAM cartridges for games that recommend their use, such as Dezaemon 2 and Sega Ages - Galaxy Force II. (#356)
- Cart: Add Vampire Savior - The Lord of Vampire demo to internal database of games that need a DRAM cartridge.
- CD Block: Allow querying files at specific frame addresses and display file being read in System State window.
- Debug: Allow exporting debug output to a file.
- Debug: Move debug port writes to a callback and remove them from the SCU tracer. Eliminates the need for debug tracing to use Mednafen's debug output method.
- Debugger: Implemented SH-2 watchpoints. (#22)
- Input: Add support for loading an external game controller database and include a [community-sourced database](https://github.com/mdqinc/SDL_GameControllerDB) in builds.
- Input: Added hotkey for exiting the application, requiring a key combo to trigger: at least one key modifier (Ctrl, Alt, Shift, Option, etc.) and one other key (e.g. Ctrl+Shift+Q). (#160)
- Media: Cache CHD hunks for improved performance at the cost of extra RAM usage.
- Media: Provide basic error feedback when attempting to load bad, corrupt or truncated disc images.
- SCSP: Basic debugger view for all slot registers and some state.
- SCSP: Final output oscilloscope view.
- VDP1: Optimize line plotting by skipping lines that are entirely out of the system clipping area.
- VDP1: Optimize mesh polygons by limiting updates to system clip area.
- VDP1: Simplify mesh rendering code for slightly improved performance.
- VDP1: Various performance micro optimizations.
- VDP2: Basic debugger view for NBG0-3 and RBG0-1 parameters.

### Fixes

- App: Set en-US UTF-8 locale globally. Fixes CHD loader unable to load files with Unicode characters in their names.
- CD Block: Prevent a crash when attempting to set up subcode transfers without an active track.
- CD Block: Soft reset fixes. (thanks to @celeriyacon)
- CD Block: Use CD Block clock ratios instead of SCSP's for drive state update events.
- CD Block: Various state transition and playback nuances. (thanks to @celeriyacon)
- CD Block: Various Put/Get/Delete Sector Data nuances. (thanks to @celeriyacon)
- Input: Reset inputs when unbinding inputs or disconnecting gamepads.
- Media: Adjust sectors offsets when reading CHD images with multiple data tracks. Fixes some Last Bronx (USA) CHD images not booting. (#238)
- Media: Fix handling of Unicode characters when loading or saving the recent game disc list.
- Media: Ignore absolute paths when loading images from CUE sheets; load from the same directory as the CUE sheet instead.
- Media: Properly handle UTF-8-encoded CUE files referencing other files with Unicode characters.
- Media: Rewrite CUE parser to hopefully fix some audio skipping issues.
- SCSP: Allow M68K to fetch instructions from SCSP registers. Fixes CroNSF audio playback.
- SCSP: Fix check for attack stuck bug when KRS=0xF. Fixes issues in multiple games:
    - Announcer voice in DonPachi's title screen is now playing consistently (#475)
    - Both games in Sega Ages - I Love Mickey Mouse - Fushigi no Oshiro Daibouken & I Love Donald Duck - Georgia Ou no Hihou now play their songs correctly (#498)
    - All games in Sega Ages: Phantasy Star Collection now play their songs correctly (#499)
    - "Xing" voice line in Arcade Gears Vol. 2 - Gun Frontier's boot up (#467)
    - Character voice lines in Langrisser III (#426)
    - Character voice lines in AnEarth Fantasy Stories - The First Volume (#358)
    - Voice lines during intro and throughout the game in Rapyulus Panic (#338)
- SCSP: Don't use SBCTL on slots that are playing samples from Sound RAM when the EG reaches the silence threshold. Fixes busted audio in Guardian Heroes and Elevator Action^2. (#155)
- SCSP: Silence audio when MVOL=0. Fixes lingering sound/music when pausing in Sega Ages - Galaxy Force II. (#427)
- SCU: Properly handle 8-bit and 16-bit writes to registers. Fixes Phantasy Star IV graphics in Phantasy Star Collection. (#499)
- SCU: Timer 1 was never triggering when configured to trigger on Timer 0 match of 0x000.
- SH2: Fix byte order of direct cache data accesses. (thanks to @celeriyacon)
- SH2: Fix MOVA offset when in delay slot. (thanks to @celeriyacon)
- SH2: Swap memory read order for MAC.W and MAC.L operands. (thanks to @celeriyacon)
- SMPC: Clear SF (with a delay) when receiving an INTBACK break request. Fixes Phantasy Star Collection hanging on a black screen after SEGA licensing screen.
- SMPC: Fix register reads/writes. (thanks to @celeriyacon)
- SMPC: Optimized INTBACK flag is inverted. Fixes some input issues during the intro sequence in Magic Knight Rayearth. (#477; thanks to @celeriyacon)
- SMPC: Time out pending INTBACK at VBlank IN if no Continue or Break requests are received until then. Fixes inputs in both Discworld versions. (#245; thanks to @celeriyacon)
- SMPC, VDP: Trigger optimized INTBACK more consistently closer to 1ms before VBlank IN depending on vertical resolution.
- System: Reset clock speed when soft resetting the system.
- VDP1: Clear transparent mesh layer to 0 instead of the erase write value when erasing framebuffer. Fixes Rayman's level loading screens rendering at half brightness when using the transparent meshes enhancement.
- VDP1: Cycle-count VBlank erase process. Fixes flashing subtitles in Panzer Dragoon FMVs and hangs in Parodius and Sexy Parodius. (#201)
- VDP1: Don't swap gouraud values when horizontal and/or vertical flip is enabled for an untextured polygon. Fixes bad shading in Croc - Legend of the Gobbos. (#543)
- VDP1: Force-align sprite character data address to 16 bytes when using RGB 5:5:5 color mode. Fixes misaligned team logos in All-Star Baseball '97 Featuring Frank Thomas. (#546)
- VDP1: Improve performance by avoiding double-writing the same pixels in the erase process low resolution modes. Also fixes erase process cycle counting in high resolutions.
- VDP1: Increase padding for system clip rendering optimization. Fixes stray white pixels on the right and bottom edges of the intro of Mahjong Yon Shimai - Wakakusa Monogatari and Croc - Legend of the Gobbos. (#527, #543)
- VDP1: Pixel-perfect rendering. (thanks to Lordus)
- VDP1: Delay PTM=1 drawing start to dodge some timing issues with games that trigger drawing too early. Fixes flickering glass shard in Fighter's History Dynamite's intro sequence. (#503)
- VDP1: Primitive cycle counting to work around some games that horribly abuse the VDP1, such as Baroque, Dark Seed II, and Funky Fantasy. (#311, #316, #364)
- VDP1: Remove write penalty hack introduced earlier for Mega Man X3's sprites. Fixes multiple issues:
    - Missing sprites in Seikai Risshiden - Yoi Kuni, Yoi Seiji, and Jissen Pachinko Hisshou-hou! Twin (#425, #537)
    - Flashing sprites in Alone in the Dark - One-Eyed Jacks Revenge, Cleaning Kit for Sega Saturn, and Contra - Legacy of War (#337, #412, #458)
    - Flashing FMVs in Funky Fantasy, World Cup Golf - Professional Edition, and Magic Carpet (#311, #516, #561)
    - Flickering letters in the mission briefing screens in Alien Trilogy (#394)
- VDP1: Rework scaled sprite rendering to correctly handle undocumented zoom point settings.
- VDP1: Rework erase/swap timings. Fixes numerous issues:
    - Screen flashing in Ayakashi Ninden Kunoichiban Plus (#478)
    - Flashing map in the demo version of Drift King Shutokou Battle '97 - Tsuchiya Keiichi & Bandou Masaaki (#493)
    - Every other interlace field missing in Virtual Mahjong 2 - My Fair Lady (#509)
- VDP2: Adjust line width for RBG line color insertion. Fixes half of the field not being colored in World League Soccer '98. (#517)
- VDP2: Always read line screen scroll data even for disabled NBGs. Fixes one-frame offset on system settings screen and Deep Fear's GUI elements.
- VDP2: Don't draw out of bounds areas of the sprite layer when rotated. Fixes ground-on-sky glitch in Sega Ages - Power Drift. (#492)
- VDP2: Don't use supplementary data for characters in inaccessible VRAM banks. Fixes columns of "A"s in Darklight Conflict. (#545)
- VDP2: Double vertical window coordinates when the display is in single-density interlaced mode. Fixes the bottom half of the screen missing in Pro-Pinball: The Web. (#371)
- VDP2: Fix CRAM address bits shuffling. Fixes unexpected graphics showing up before the intro FMV in Saturn Bomberman. (#434)
- VDP2: Fix CRAM address calculation for RBG line colors. Fixes wrong colors for field shading in World League Soccer '98. (#517)
- VDP2: Fix exclusive monitor timings and resolution sizes, and out-of-bounds reads from lookup tables. (thanks to @celeriyacon)
- VDP2: Fix interlaced mode timings. (thanks to @celeriyacon)
- VDP2: Fix off-by-one error when clamping window X coordinates. Fixes one-pixel glitches in Albert Odyssey when displaying dialogue boxes.
- VDP2: Fix palette-based transparent meshes not blending with VDP2 layer (such as in Bulk Slash).
- VDP2: Fix rotation parameter line color data address calculation.
- VDP2: Fix window calculations involving illegal vertical coordinates. Fixes background glitch in Radiant Silvergun's Stage 2C.
- VDP2: Force-fetch first character of every scanline. Fixes some garbage tiles on the left edge of the screen in Athlete Kings' splash screen. (#515)
- VDP2: Handle games that only enable RBG1. Fixes missing background graphics in Houkago Ren'ai Club - Koi no Etude. (#551)
- VDP2: Handle sprite window on sprite layer manually. Fixes graphics effects when defeating the first boss in Metal Black. (#404)
- VDP2: Honor rotation parameter mode register when selecting line color data. Fixes bad sky on Episode 2 of Panzer Dragoon II Zwei and glitched sky/ceiling in Savaki. (#570)
- VDP2: Invert TVSTAT.ODD bit on single-density interlaced modes too. Fixes swapped interlaced fields in Pro-Pinball: The Web and Shienryuu. (#447)
- VDP2: Implement VCNT skip as a dedicated vertical phase. (thanks to @celeriyacon)
- VDP2: Latch TVMD.DISP and TVMD.BDCLMD at start of the frame. Fixes:
    - Garbage graphics after loading screen in Samurai Spirits - Zankurou Musouken (#472)
    - One-frame glitches in Ayakashi Ninden Kunoichiban Plus and Ninpen Manmaru (#478, #569)
- VDP2: Move VCNT update to the left border horizontal phase where HBLANK switches to zero. (thanks to @celeriyacon)
- VDP2: Precompute per-dot rotation coefficient flag instead of deriving it twice per scanline.
- VDP2: Read per-screen line/back color only at the start of the frame. Fixes red screen after SEGA licensing logo in Ayakashi Ninden Kunoichiban Plus. (#478)
- VDP2: Recalculate RBG0/1 page base addresses when changed mid-frame. Fixes broken ground on NiGHTS into Dreams's boss fights. (#423)
- VDP2: Reduce rotation parameter calculation precision to more closely match the real system.
- VDP2: Render transparent meshes onto a separate layer instead of immediately blending them onto the sprite layer. Fixes priority issues on Akumajou Dracula X when using the enhancement. (#484)
- VDP2: Rework bitmap delays due to bad VRAM access cycles configuration. Fixes shifted graphics in Baroque Report - CD Data File. (#524)
- VDP2: Store line color data for RBGs separately. Fixes sky box issues when jumping on the spring pad in Sonic Jam's Sonic World mode. (#83)
- VDP2: Use more accurate NTSC/PAL clock timings for video sync. Eliminates stutters with refresh rates that are very slightly lower than a perfect multiple of the NTSC/PAL frame rates.
- VDP2: Use the correct character fetcher for scroll RBGs. Fixes background glitches in Battle Monsters. (#340)


## Version 0.1.7

Released 2025-08-10.

Introduced save state file version 8.

### New features and improvements

- App: Added a button to copy the version string from the About window.
- App: Added hotkey to take screenshots (bound to F12 by default) with adjustable scaling from 1x to 4x. (#350)
- App: Added option to automatically load most recently loaded game disc image on startup.
- App: Auto-center About window whenever it is opened.
- App: Automatically detect profile path and allow using the user profile path from the OS's user home directory. (#411, #17; @bsdcode)
- App: Display error dialog on unhandled exceptions.
- App: Show actual emulation speed in title bar and frame rate OSD.
- App: Show actual VDP1 frame rate separated from VDP1 draw calls.
- Build: FreeBSD support for x86-64 systems. (#389; @bsdcode)
- Build: macOS builds are now universal -- one binary supports both Intel and Apple Silicon Macs. (#351; @Wunkolo)
- Build: Nightly builds are now available [here](https://github.com/ymir-emu/Ymir/releases/latest-nightly).
- Core: Improve manual reset event performance by using OS-specific implementations based on [cppcoro](https://github.com/lewissbaker/cppcoro).
- Debugger: Added CD Block filters view.
- Debugger: Added rudimentary SH-2 breakpoint management and per-game debugger state persistence. (#22)
- Debugger: Added SH-2 exception vector list view.
- Debugger: Allow suspending SH-2 CPUs in debug mode. (#22)
- Debugger: Implemented SH-2 breakpoints. (#22)
- Debugger: Introduced debug break signal that can be raised from just about anywhere. (#21)
- GameDB: Force SH-2 cache emulation for Astal, Dark Savior and Soviet Strike.
- GameDB: Implemented flag to force SH-2 cache emulation to specific games.
- Input: Categorized gamepad triggers and sticks as absolute axes. Absolute axes output fixed values at specific positions.
- Input: Categorized gamepad triggers as monopolar axes (having values ranging from 0.0 to 1.0) and gamepad sticks as bipolar axes (-1.0 to +1.0).
- Input: Implemented Arcade Racer peripheral. (#29)
- Input: Implemented Mission Stick peripheral with toggleable three-axis and six-axis modes. (#30)
- Video: Added hotkeys to rotate screen clockwise and counterclockwise. (#318)
- Video: Added option to reduce input lag by adjusting GUI frame rate to the largest multiple of the emulator's target frame rate that's not greater than the display's refresh rate.
- Video: Added option to reduce video latency by displaying the latest frame instead of the oldest when the emulator is running faster than the display's refresh rate.
- Video: Added option to synchronize video frames in windowed mode.
- Video: Avoid frame skipping on slow refresh rate monitors by disabling VSync if the target frame rate exceeds the display's refresh rate.
- Video: Simplify frame rate control in full screen mode.

### Fixes

- CD Block: Disconnect filter inputs for the fail target, not the filter itself. Fixes broken graphics in Ultraman Zukan's title screen. (#329)
- CD Block: Don't disconnect CD device from when setting the fail output of filter. Fixes Digital Dance Mix Vol. 1 - Namie Amuro playback.
- CD Block: Fix directory indexing for ReadDirectory and ChangeDirectory commands. Fixes Sega Rally Championship Plus (Japan) not booting.
- CD Block: Fix handling of "no change" playback end parameter. Fixes Astal taking a long time to load the first stage.
- CD Block: Properly read path table and directory records that cross the boundary between two CD sectors. Fixes Mizuki Shigeru no Youkai Zukan Soushuuhen booting back to BIOS. (#391)
- CD Block: Read subheader data from CD-ROM Mode 2 tracks only and fix their addressing. Fixes missing intro FMV in NiGHTS into Dreams... (#46)
- CD Block: Start new playbacks from starting FAD when paused. Fixes WipEout 2097 and XL boot issues. (#202)
- Media: Add support for CD-ROM Mode 2 tracks. Fixes Last Bronx not booting. (#238)
- Media: Compensate for INDEX 00 pregap in multi-indexed tracks in CUE sheets. Fixes partially skipped Minnesota Fats - Pool Legend voice lines. (#363)
- Media: Fix handling of pregap in data tracks in single BIN+CUE dumps. Fixes some Last Bronx dumps not booting.
- Media: Realign data offset to hunks between tracks in CHDs. Fixes some Last Bronx CHD dumps not booting.
- Save states: Added CD Block file system state to save state data.
- Scheduler: Ensure events are executed in chronological order.
- SCU: Fix A-Bus external interrupt handling.
- SCU: Fix DMA source address updates when source address increment is zero. Fixes background priority issue regression in Street Fighter - Real Battle on Film. (#168)
- SCU: Fix Timer 1 not triggering when the reload counter is larger than 0x1AA or 0x1C6 depending on horizontal resolution.
- SCU: Ignore/skip illegal DMA transfers in indirect transfer lists. Partially fixes corrupted or missing sprites in Fighting Vipers.
- SCU: Illegal DMA interrupts should not trigger VDP1 Sprite Draw End DMA transfers.
- SCU: Notify bus of DMA transfers.
- SCU: Prevent indirect DMA transfers from starting if the first entry is illegal. Stops Tennis Arena from destroying all memory.
- SCU: Use the source address increment for indirect DMA transfer data.
- Settings: Disable "Include VDP1 rendering in VDP2 renderer thread" by default and don't enable it in presets.
- Settings: Persist custom Screenshots profile path. (#398)
- Settings: Properly restore controller binds for controllers other than the Saturn Control Pad. (#397)
- SH2: Fix CPU getting stuck handling DMAC interrupts forever. Fixes Shellshock not booting. (#344)
- SH2: Handle sleep/standby mode and wake up on interrupts. Fixes Culdcept getting stuck on intro FMV and boosts overall performance on games that make use of the SLEEP instruction. (#346)
- SH2: Only clear zeroed out bits from write clear bitmask on FRT FTCSR writes. Fixes random lockups in Daytona USA. (#209) (thanks to @celeriyacon)
- SMPC: Fixed TH control mode reports on SH-2 direct mode. Fixes input response in World Heroes Perfect, Touge King the Spirits, Chaos Control Remix, and Father Christmas. (#297, #322, #348, #374)
- SMPC: Fixed TL reporting on SH-2 direct mode.
- VDP1: Disable "antialiasing" for lines and polylines.
- VDP1: Fix bad transparency caused by "illegal" RGB 5:5:5 color data (0x0001..0x7FFE). Fixes transparency in Sonic X-treme.
- VDP1: Process framebuffer swap slightly later in the VBlank OUT line. Fixes numerous issues:
    - Flickering graphics in DragonHeart - Fire & Steel, King of Fighters '96 / '97, Jantei Battle Cos-Player, PhantasM, Soviet Strike, Virtua Cop 2, and Yellow Brick Road (#272, #303, #334, #335, #336, #368)
    - Corrupted sprites in Center Ring Boxing, and Marvel Super Heroes (Shuma Gorath's Chaos Dimension move) (#72, #377)
    - Partially missing sprites in Fuusui Sensei - Feng-Shui Master (#405)
    - Freezes/crashes in Mahou no Janshi - Poe Poe Poemy, and Shockwave Assault (#378, #406)
- VDP1: Reorder LOPR, COPR, CEF and BEF updates. Fixes missing graphics in Virtual On - Cyber Troopers and Sega Touring CARS. (#112, #246)
- VDP1: Use SCU DMA bus notification to adjust VDP1 VRAM write timing penalty. Fixes hanging intro FMV in Sonic Jam without breaking Mega Man X3's sprites. (#83)
- VDP2: Always initialize and update background counters even for disabled layers. Fixes rolling screen in F-1 Challenge. (#300)
- VDP2: Block bitmap reads from VRAM banks without appropriate CP access. Fixes dirty graphics in NFL Quarterback Club 97's title screen. (#332)
- VDP2: Compute bitmap data access offsets when multiple chunks are read for an NBG. Fixes background offset in Doukyuusei - if while maintaining the slicing fix for Capcom Generation - Dai-5-shuu Kakutouka-tachi. (#384)
- VDP2: Don't apply sprite shadow if sprite priority is lower than the top layer. Fixes shadows drawing on top of objects in Blue Seed - Kushinada Hirokuden. (#349)
- VDP2: Don't blend line screen with layer 1 if line screen color calculations are disabled. Fixes fog in battle backgrounds in Zanma Chou Ougi - Valhollian. (#352)
- VDP2: Don't increment vertical scroll BG coordinate on complementary field lines when rendering deinterlaced RBG lines. Fixes jittery/interlaced Grandia FMVs when deinterlace is enabled.
- VDP2: Fix layer enable flags calculation when only RBG1 is enabled. Fixes missing background in MechWarrior 2's menus. (#413)
- VDP2: Fix line color insertion logic. Fixes erroneously blended ground in Athlete Kings. (#299)
- VDP2: Fix priority calculations for bitmap BGs. Fixes character sprites being drawn behind the background layer in Mr. Bones. (#247)
- VDP2: Fix race conditions with threaded deinterlacing causing some artifacts on RBGs in single-density interlaced mode.
- VDP2: Fix window indexing for RBGs in high resolution modes. Fixes stretched shadows in Last Bronx. (#395)
- VDP2: Handle RBG window pixels in high resolution modes. Fixes extra column of garbage in Athlete Kings. (#299)
- VDP2: Invert TVSTAT.ODD reads. Fixes garbled graphics on the top half of the screen in True Pinball. (#320)
- VDP2: RBG1 uses Rotation Parameter B, not A.
- VDP2: Reset NBG2/3 base vertical scroll counters when writing to SCYN2/3. Fixes garbled graphics in Marvel Super Heroes vs. Street Fighter during Shuma Gorath's Chaos Dimension move. (#72)
- VDP2: Rework rotation table calculations. Fixes warped ground on player two's screen in Sonic R multiplayer mode. (#401)
- VDP2: Skip calculation of VRAM PN/CP accesses for NBGs when RBG1 is enabled. Fixes missing car graphics in Gale Racer. (#359)
- VDP2: Swap even/odd field when entering VBlank.


## Version 0.1.6

Released 2025-07-20.

Introduced save state file version 7.

### New features and improvements

- App: Added display rotation options for TATE mode games. (#256)
- App: Added frame rate OSD and hotkeys to toggle it and change positions.
- App: Added menu actions to resize window to specific scales.
- App: Added new 3:2 and 16:10 forced aspect ratio options.
- App: Added option to remember window position and size. (#4)
- App: Added save states to File menu.
- App: Added simple message overlay system to display some basic notifications. (#288)
- App: Display emulation speed in title bar and under speed indicator, and add a new indicator for slow motion. (#16)
- App: Improve full screen frame pacing even further by spin-waiting for up to 1 ms before the frame presentation target.
- App: Include timestamp on save states.
- App: Notify about loading/saving save states or switching save state slots.
- App: Smooth out frame interval adjustments in full screen mode.
- Backup Manager: Export "Vmem"-type BUP files by default.
- Backup Manager: Make all columns sortable.
- Backup Manager: Show logical block usage (matching BIOS numbers) + header blocks. (#294)
- Debugger: Added basic VDP1 registers inspector window.
- Input: Added new keybinds for frame rate limit control: increase/decrease speed, switch between primary/alternate speed, reset speed. (#16)
- Input: Changed default keybinds for Pause/Resume action from "Pause, Ctrl+P" to "Pause, Spacebar".
- Input: Removed Return from default binds to Port 1 Start button to avoid conflict with full screen hotkey (Alt+Enter).
- SCSP: Various micro optimizations.
- Settings: Added "Clear all" button to controller configuration window to clear all binds. (#288)
- Settings: Automatically create/suggest a backup RAM file if no path is specified when inserting the cartridge.
- SH2: Improve cache emulation performance by avoiding byte-swapping cache lines.
- SH2: Improve overall emulation performance by simplifying interrupt checks.
- System: Map 030'0000-03F'FFFF memory area.
- System: Map simple arrays directly as pointers into the Bus struct to improve overall performance.
- VDP2: Add dedicated thread for deinterlaced rendering if VDP2 threading is enabled. Significantly lessens performance impact of the deinterlace enhancement on quad-core CPUs or better.
- Video: Implemented frame rate limiter. (#16)

### Fixes

- App: Disable emulator-GUI thread syncing when not in full screen mode. Fixes emulator slowing down when running at 100% speed on displays with refresh rate lower than 60 Hz.
- App: Fix frame pacing and speed limiter on 50 and 60 Hz displays.
- CD Block: Fix handling of "no change" PlayDisc parameters. Fixes X-Men: Children of the Atom CDDA tracks not resuming after pausing. (#274)
- Debugger: Indirect SCU DMA transfers were being traced with the updated indirect table address.
- Input: Fix inability to bind keyboard combos.
- Input: Modifier keys can now be used correctly as controller input binds and will no longer interfere with other controller inputs. (#282)
- Media: Allow loading CUE files with PREGAP and INDEX 00 on the same TRACK.
- Media: Don't bother detecting silence in pregap area; trust the CUE files.
- Media: Skip blank lines in CUE files.
- Save states: Read/write missing SCSP field to save state object. Fixes occasional application crashes when using the rewind buffer in conjunction with save states.
- SCSP: Use EG level instead of total level in MSLC reads. Fixes missing/truncated SFX on various games, including Sonic R, Akumajou Dracula X and Daytona USA CCE.
- SCU: Allow SCU DSP program and data RAM reads or writes while the program is paused (thanks to @celeriyacon).
- SCU: DSP data RAM reads should return 0xFFFFFFFF while program is running (thanks to @celeriyacon).
- SCU: HBlank IN DMA transfers should not be gated by timers. Fixes non-scrolling Shinobi-X cityscape background. (#193)
- SCU: Improve HBlank IN, VBlank IN and VBlank OUT interrupt signal handling.
- SCU: Increment DMA source address by 4 after performing DMA transfers with no increment. Fixes background priority issues in Street Fighter - Real Battle on Film. (#168)
- SCU: Interleave SCU DSP DMA transfers with program execution when not writing to Program RAM or accessing the CT used by DMA (thanks to @celeriyacon).
- SCU: Rework SCU DMA transfers. Fixes displaced tile data in Steam-Heart's. (#278)
- SCU: Run all pending DMA transfers instead of just the highest priority.
- SCU: Split up MSH2/SSH2 interrupt handling.
- SCU: Various fixes to SCU DSP DMA transfers to DSP Program RAM (thanks to @celeriyacon).
- Settings: Reverse IPL column sorting order.
- SH2: Fix cache LRU AND update mask. Fixes FMV glitches on Capcom games, WipEout and Mr. Bones when SH-2 cache emulation is enabled. (#202, #247, #270)
- SH2: TAS.B read should bypass cache.
- SH2: The nIVECF pin of the SSH2 is disconnected, disallowing it from doing external interrupt vector fetches.
- SMPC: Delay all commands for slightly longer to allow Quake (EU) to boot with normal CD read speed (2x).
- SMPC: Fix automatic switch to PAL or NTSC to match area code more consistently.
- System: Only hard reset if SMPC area code actually changed.
- System: Tighten synchronization between SCU and SH-2 CPUs. Improves stability on WipEout (USA). (#202)
- VDP1: Double horizontal erase area when drawing low-resolution sprites with 8-bit data. Fixes right half of sprite graphics not cleaning up in Resident Evil's options menu. (#180)
- VDP1: Extend line clipping to the left and top edges by one pixel to compensate for some inaccuracies.
- VDP1: Fix end codes for 64 and 128 color sprites. Fixes white sprite outlines in Scud - The Disposable Assassin and broken sprites in Primal Rage. (#268, #280)
- VDP1: Hack in VDP1 command processing delay on VRAM writes. Fixes glitched sprites on Mega Man X3. (#244)
- VDP1: Include source color MSB when rendering polygons in half-luminance mode. Fixes intro FMV background on Crows - The Battle Action. (#107)
- VDP1: Mask CMDCOLR bits 0..3 in 4bpp banked sprite mode. Fixes palette issues in Steam-Heart's and Dragon Ball Z - Shinbutouden. (#69, #278)
- VDP1: Properly handle DIE/DIL in single-density interlaced mode. Fixes text drawn twice as tall in Resident Evil options menu. (#180)
- VDP2: Adjust character data offset for 2x2 characters in RGB 8:8:8 color format. Fixes garbled FMV in Crusader - No Remorse. (#108)
- VDP2: Apply character pattern delay based on first pattern name access, not all of them. Fixes shifted UI elements in Battle Arena Toshinden Remix. (#306)
- VDP2: Apply per-dot special color calculations to bitmap BGs. Fixes translucent UI in The Story of Thor. (#152)
- VDP2: Don't update line/back screen color, line screen scroll or rotation parameters when the display is disabled. Fixes blank screen during Sega Rally Championship boot up.
- VDP2: Fix per-dot special priority function. Fixes BG priority issues in Waku Waku Puyo Puyo Dungeon.
- VDP2: Fix single-density interlaced mode not actually interlacing the image.
- VDP2: Fix sprite layer display when rotation mode is enabled. Fixes sliding 3D graphics on Hang-On GP and Highway 2000. (#167, #208, #277)
- VDP2: Fix transparent VDP1 color data handling. Fixes missing graphics in Rayman's level select screens and Bubble Bobble's sky in the title screen. (#262)
- VDP2: Fix window short-circuiting logic. Fixes missing ground in Final Fight Revenge and incorrect UI elements in Sakura Taisen. (#104, #253)
- VDP2: Halve sprite layer width when drawing 8-bit sprite layer in low-resolution VDP2 modes. Fixes text drawn twice as wide in Resident Evil options menu. (#180)
- VDP2: Handle bad window parameters set by Snatcher on the "Act 1" title screen (and probably many other places). (#259)
- VDP2: Honor TVMD.BDCLMD when the display is disabled. Fixes screen transitions in Sega Rally Championship.
- VDP2: Ignore vertical cell scroll read cycles for NBGs that have the effect disabled. Fixes wavy background effect on stage 2 of Magical Night Dreams - Cotton 2. (#255)
- VDP2: Implemented rules for bitmap VRAM access delay. Fixes sliced images in Capcom Generation - Dai-5-shuu Kakutouka-tachi art gallery. (#254)
- VDP2: Latch BG scroll registers earlier (at VBlank OUT) and latch vertical scroll registers (SCY[ID]Nn). Fixes bad vertical offset in Shinobi-X's NBG2 layer. (#193)
- VDP2: Read first vertical cell scroll entry on bitmap backgrounds. Fixes misplaced lines in Street Fighter - Real Battle on Film FMVs. (#291)
- VDP2: The first vertical cell scroll entry read does not update the address. Fixes background offset on the first Rayman boss stage.
- VDP2: Update line screen scroll address at Y=0. Fixes line glitches in Rayman's backgrounds and Sonic Jam's Sonic 2 special stage graphics.
- VDP2: Update line screen scroll offsets only at the specified boundaries. Very slightly improves performance and fixes text slicing issues in Sega Rally Championship's Records and Options screens.
- VDP2: Update vertical cell scroll every 8 cell dots correctly when the background is zoomed in.
- VDP2: Update vertical scroll registers (SCY[ID]Nn) when written. Fixes background distortion effect of Shuma Gorath's Chaos Dimension super move in Marvel Super Heroes vs. Street Fighter. (#72)
- VDP: Fix handling of VDP1 threading flag when VDP2 threading is disabled.
- ymdasm: Fix reversed SCU DSP DMA immediate/data RAM operand decoding.
- ymdasm: Mask and translate several SCU DSP immediates.


## Version 0.1.5

Released 2025-06-28.

Introduced save state file version 6.

### New features and improvements

- App: Added command-line option `-P` to force emulator to start paused.
- App: Added new Tweaks tab to Settings window consolidating all accuracy, compatibility and enhancement settings.
- App: Added option to create internal backup RAM files per game. (#99)
- App: Added option to override UI scale. (#251)
- App: Added option to toggle fullscreen by double-clicking the display. (#197)
- App: Added recent games list to File menu. (#196)
- App: Automatically center Settings window when opening it. (#251)
- App: Close windows when pressing B or Circle on gamepads while nothing is focused. (#251)
- App: Enable gamepad navigation on GUI elements. (#251)
- App: Store relative paths in Ymir.toml. (#207)
- App: Use window-based DPI to adjust UI scale, allowing the UI to adapt to displays with different DPI settings. (#221; @Wunkolo)
- Backup RAM: Support interleaved backup image formats such as the ones produced by Yaba Sanshiro or the MiSTer core. (#87)
- Backup RAM: Support standard BUP backup files. (#87)
- SCSP: Added option to increase emulation granularity for improved timing accuracy (thanks to @celeriyacon).
- SCSP: Double-buffer DSP MIXS memory (thanks to @celeriyacon).
- SCSP: Implemented MIDI In and Out. (#258; @GlaireDaggers)
- SCSP: Interleave DSP execution and slot processing (thanks to @celeriyacon).
- VDP1: Added option to replace meshes with 50% transparency.
- VDP1: Clip sprites to visible area to speed up rendering, especially of very large sprites.
- VDP: Added option to deinterlace video. (#66)
- VDP: Moved VDP1 rendering to the emulator thread to improve compatibility with some games (e.g. Grandia) and added an option to move it back to the VDP rendering thread. (#233)

### Fixes

- App: Fix rare crash when loading a backup memory image in the Backup Memory Manager.
- App: Fix window scaling on macOS Retina displays when using HiDPI mode. (#221, #266; @Wunkolo)
- App: Prevent loading internal backup memory image as backup RAM cartridge image.
- CD Block: Start new playbacks from starting FAD when previous playback has ended. Fixes WipEout freeze after SEGA logo and many other freezes, no-boots and softlocks.
- Media: Fix pregap handling in single BIN images.
- SCSP: Apply DAC18B to output (thanks to @celeriyacon). Fixes quiet audio in many games. (#237)
- SCSP: Fix loss of accuracy on MIXS send level calculation (thanks to @celeriyacon).
- SCSP: Fix send level, panning and master volume calculations.
- SCSP: Fix slot output processing order (thanks to @celeriyacon).
- SCSP: Fix swapped DAC18B and MEM4MB bits (thanks to @celeriyacon).
- SCSP: Run one additional DSP step to fix FRC issues (thanks to @celeriyacon).
- SCU, SH-2, SMPC, SCSP, VDP: Numerous fixes to interrupt handling (thanks to @celeriyacon). Fixes intermittent Rayman inputs and some audio glitches.
- SCU: Various DSP accuracy fixes (thanks to @celeriyacon).
- SH2: More fixes to FRT, WDT and DIVU (thanks to @celeriyacon).
- SMPC: Cancel scheduled command processing event when resetting SMPC. Fixes a long hang after hard resetting in some cases.
- SMPC: Change fixed bits from 111 to 100 in TH/TR control mode responses for the first data byte of the Control Pad and 3D Control Pad peripherals. Fixes Golden Axe booting back to BIOS. (#231)
- SMPC: Eliminate spurious INTBACK interrupts.
- SMPC: Prevent COMREG writes when a command is in progress. Fixes some boot issues leading to the "Disc unsuitable for this system" message. (#219)
- SMPC: Prevent optimized INTBACK report from occurring unless a continue request was sent. Fixes input issues with Yaul-based homebrew.
- SMPC: Prioritize INTBACK continue requests over break requests.
- System: Tighten synchronization between the two SH-2 CPUs and remove artificial timeslice limit. Improves performance and fixes Fighters Megamix and Sonic Jam intermittent boot issues. (#236, #242)
- VDP1: Lower command limit to work around problematic games that don't set up a terminator in the command table. (#213, #216)
- VDP1: Significantly slow down command execution when running the VDP1 renderer on the emulator thread. Fixes Dragon Ball Z - Shinbutouden freeze after SEGA logo. (#233)
- VDP2: Apply horizontal mosaic effect to rotation background layer. Fixes missing effect on Race Drivin' Time Warner logo. (#267)
- VDP2: Apply window effect to sprite layer. Fixes graphics going out of bounds in many games. (#173)
- VDP2: Check for invalid access patterns to determine if NBG characters should be delayed. Fixes background offsets in many games. (#169, #190, #226)
- VDP2: Disable NBG1-3 only if both RBG0 and RBG1 are enabled simultaneously.
- VDP2: Honor access cycles and VRAM bank allocations to restrict pattern name and character pattern accesses. Fixes garbage graphics in Panzer Dragoon Saga, Sonic 3D Blast and Street Fighter Alpha/Zero 2. (#203, #213)
- VDP2: Invert back screen color calculation ratio. Fixes black background on Sakura Taisen FMVs. (#241)
- VDP2: Move existing VCounter into VDP2 VCNT register. Fixes Assault Suit Leynos 2 freeze when going in-game and King of Fighters '95 not booting. (#75)
- VDP2: Synchronize background enable events with the renderer thread. Fixes FMV slicing issues on slow machines on Sakura Taisen.
- ymdasm: Fix SCU DSP unconditional JMP disassembly.


## Version 0.1.4+1

Released 2025-06-02.

Uses save state file version 5.

### New features and improvements

- App: You can now drag and drop CCD, CHD, CUE, ISO or MDS files into the emulator window to load games.

### Fixes

- VDP2: Fix black screen on SSE2 builds.


## Version 0.1.4

Released 2025-06-02.

Introduced save state file version 5.

### New features and improvements

- App: Added option to pause emulator when the window loses focus. (#181)
- App: Added shadow under playback indicators to make them visible on white backgrounds.
- App: Automatically adjust scaling when system-wide DPI is changed. (@Wunkolo)
- App: Changed background color around screen to black on windowed mode.
- CD Block: Implement Put Sector command, used by After Burner II. (#78)
- Core: Performance improvements, especially for ARM builds. (@Wunkolo)
- Debug: Simple CD Block commmand tracer window.
- Input: Implemented 3D Control Pad. (#28)
- Media: Preliminary support for CHD files. (#48)
- Media: Support multi-indexed audio tracks (BIN/CUE only). (#58)
- SMPC: Set SF=0 on unimplemented commands so that games can move forward.
- SH-2: Build infrastructure needed to honor memory access cycles for improved performance and accuracy.
- SH-2: Slow down accesses to on-chip registers to 4 cycles.
- VDP: Rewrite VDP2 frame composition code to use SIMD on x86 and ARM for improved performance. (@Wunkolo)

### Fixes

- App: Customized profile paths are now created at the specified location instead of the default. (#119, #126; @lvsweat)
- CD Block: Clear partitions and filters on soft resets triggered by Initialize CD System command. Fixes some game boot issues.
- CD Block: Clear the "paused due to buffer exhausted" flag when SeekDisc command pauses playback. Fixes Sakura Taisen 2 read errors after FMVs.
- CD Block: Don't clear the file system when opening the tray.
- CD Block: Fix audio track sector sizes. Fixes some CD audio track playback glitches with certain images (particularly MDF/MDS).
- CD Block: Fix Delete Sector end position when sector count is FFFF. Fixes some game boot issues.
- CD Block: Fix directory indexing. Fixes one of Assault Suit Leynos 2 crashes on startup. (#127)
- CD Block: Free last buffer from partition when ending a Get Then Delete Sector transfer when the last sector isn't fully read. Fixes some game boot issues.
- IPL: Automatically load IPL ROM when switching disc images. (#128)
- M68K: Soft reset CPU when executing the `RESET` instruction. Fixes OutRun getting stuck on its own SEGA logo.
- Media: Fix crash when parsing CUE sheets with non-contiguous tracks.
- SCSP: Don't mirror sound RAM on 5A8'0000-5AF'FFFF. Fixes After Burner II audio and M68K crashes.
- SCU: Rework interrupt handling. Fixes Rayman inputs. (#59)
- SCU: Set ALU = AC before running DSP operations. Fixes Quake crash on boot. (#156)
- SCU: Timer enable flag applies to both timers. Fixes background priority issues in Need for Speed.
- SH-2: Fix PC offsets for exceptions, interrupts, TRAPA and RTE. Fixes some game boot issues.
- SH-2: Fix PC offsets for `mova`, `mov.w` and `mov.l` with `@(disp,PC)` operand (thanks to @celeriyacon).
- SH-2: Fixes and accuracy improvements to DIVU (thanks to @celeriyacon).
- SH-2: Fixes and accuracy improvements to FRT (thanks to @celeriyacon). Fixes freezes in Daytona USA. (#7)
- SH-2: Fixes and accuracy improvements to WDT (thanks to @celeriyacon).
- SH-2: Lazily update WDT and FRT timers. Provides a 5-10% performance boost *and* improves accuracy!
- SMPC: Various INTBACK handling adjustments. Partially fixes Assault Suit Leynos 2 no-boot issues.
- System: Fix cycle counting on the main loop not taking into account the number of cycles taken by the CPUs, resulting in undercounting timers.
- VDP1/2: Fix handling of 16-bit sprite data from VDP1 when VDP2 uses 8-bit sprite types. Fixes sprites in I Love Mickey Mouse/Donald Duck.
- VDP2: Allow 8-bit reads and writes to VDP2 registers.
- VDP2: Apply transparency to mixed-format sprite data when rendering the special value 0x8000. Fixes Assault Suit Leynos 2 black screen after loading.
- VDP2: Don't increment vertical mosaic counter if mosaic is disabled. Fixes text boxes and character portraits in Grandia. (#91)
- VDP2: Fix bitmap base address for RBGs. Fixes several graphics glitches on menus and in-game in Need for Speed.
- VDP2: Fix line screen scroll in double-density interlace mode. Fixes stretched videos in Grandia. (#91)
- VDP2: Fix special color calculation bits. Fixes Sonic R water effects. (#150)
- VDP2: Fix vertical cell scroll effect on games that set up access patterns that don't match the NBG parameters. Fixes Sakura Taisen 2 FMVs.
- VDP2: RBG0 was always being processed/rendered even when disabled.
- ymdasm: Fix file length when using a non-zero initial offset with the default length.


## Version 0.1.3

Released 2025-05-16.

Introduced save state file version 4.

### New features and improvements

- Cartridge: Added 16 Mbit ROM cartridge for Ultraman: Hikari no Kyojin Densetsu and The King of Fighters '95. (#71)
- Cartridge: Added option to automatically load cartridges required by some games. (#98)
- Input: Categorize some actions as "triggers" (one-shot actions, optionally repeatable) to differentiate them from "buttons" (a binary state). This allows frame step to be repeated by holding the keyboard key bound to it.
- Input: Added a "Turbo speed (hold)" input bind that toggles turbo on and off. (#103)
- System: Automatically switch to PAL or NTSC based on auto-selected region.
- Save states: Automatically load IPL ROM matching the one used in a save state.
- Debugger: Added VDP2 layer toggles to Debug menu and in a new window.
- App: Allow customizing all profile paths. (#74)
- App: Add IPL ROM list sorting. (#92) (@Wunkolo)
- App: Add full screen mode (default shortcut: `Alt+Enter`) and command-line override `-f`. (#47)
- App: Improve frame pacing for a smooth full screen experience. (#97)
- App: Mitigate input lag in every mode (#101)
- App: Display reverse, rewind, fast-forward and pause indicators on the top-right corner of the viewport. (#103)
- Build: Added macOS builds. (huge thanks to @Wunkolo!)
- Core: Several performance and stability improvements. (@Wunkolo)

### Fixes

- VDP1: Preserve EWDR, EWLR and EWRR on reset. Fixes some graphics glitches on Capcom games. (#67)
- VDP2: RBGs would render incorrectly when starting the emulator with threaded VDP rendering disabled. (#77)
- VDP2: Honor access cycle settings (CYCA0/A1/B0/B1 registers) to fix vertical cell scroll effect. (#76)
- VDP2: Disable NBGs 1 to 3 if NBG0 or NBG1 use high color formats. (#76)
- VDP2: Apply mid-frame scroll effects properly. (#72)
- VDP2: Use the MSB from the final color value instead of the raw sprite data MSB, which fixes background priority bugs on Dragon Ball Z - Shinbutouden (#69)
- SCSP: More accuracy improvements and bug fixes (thanks to @celeriyacon)
- SCU: Fix repeated indirect DMA transfers when the write address update flag is enabled. Fixes a crash when going in-game on Shinobi X. (#84)
- Input: Assigning keys to connected controllers will no longer unbind keys from disconnected controllers.
- Rewind: Fix rare crash due to a race condition when resetting the rewind buffer.
- App: Fix handling of UTF-8 paths. (#88)
- Backup memory manager: Fix crash when loading an image with less files than the current image while having selected files at positions past the new image's file count.


## Version 0.1.2

Released 2025-05-04.

Introduced save state file version 3.

### New features and improvements

- Input: Improved support for gamepads. You can now configure triggers as buttons, map analog sticks to the D-Pad, adjust deadzones, and more. (#36, #37, #54)
- Input: Added one more slot for input binds

### Fixes

- Input: Keys no longer get stuck when focusing windows, menus, etc.
- Input: Keys bound to controller on port 2 (by default: arrow keys, numpad and navigation block) should no longer prevent keys bound to the controller on port 1 from working. (#50)
- Several stability improvements (@Wunkolo)


## Version 0.1.1

Released 2025-05-03.

Introduced save state file version 2.

### New features and improvements

- App: New logo and icon (thanks to @windy3862 on Discord!)
- App: Added Welcome window with instructions for first-time users
- App: Set initial window size based on display resolution
- App: Scale GUI based on system DPI scaling (#45)

### Fixes

- VDP1: Truncate polygon coordinates to 13 bits, fixing a short freeze in Virtua Fighter 2
- SCSP: Various accuracy improvements and bug fixes (thanks to @celeriyacon)
- CD Block: Fix errors when loading homebrew discs containing a single file
- Input: Properly handle gamepad buttons when binding inputs
- ymdasm: Fix disassembly skipping the very last instruction in files


## Version 0.1.0

Released 2025-05-01.

Initial release.

Introduced save state file version 1.
