#pragma once

/**
@file
@brief The facade of the Ymir emulator core.
Defines the `ymir::Saturn` type, the primary object used for emulating a Sega Saturn system.

See @ref index for instructions on how to use the emulator.
*/

#include <ymir/core/configuration.hpp>
#include <ymir/core/hash.hpp>
#include <ymir/core/scheduler.hpp>

#include <ymir/savestate/savestate.hpp>

#include <ymir/debug/debug_break.hpp>

#include "memory.hpp"
#include "system.hpp"

#include <ymir/hw/cart/cart.hpp>
#include <ymir/hw/cdblock/cd_drive.hpp>
#include <ymir/hw/cdblock/cdblock.hpp>
#include <ymir/hw/cdblock/ygr.hpp>
#include <ymir/hw/scsp/scsp.hpp>
#include <ymir/hw/scu/scu.hpp>
#include <ymir/hw/sh1/sh1.hpp>
#include <ymir/hw/sh2/sh2.hpp>
#include <ymir/hw/smpc/smpc.hpp>
#include <ymir/hw/vdp/vdp.hpp>

#include <ymir/media/cd_interface.hpp>

namespace ymir {

/// @brief Represents an emulated Sega Saturn system.
///
/// This is the entrypoint of the emulator core. Every operation must be done through this object or any of its members.
///
/// See @ref index for details on how to use the emulator.
struct Saturn {
    /// @brief Creates a new Sega Saturn emulator reset to factory state.
    ///
    /// The emulator comes with no disc, no peripherals, no cartridge, and a basic IPL ROM that puts the master SH-2
    /// into an infinite do-nothing loop.
    Saturn();

    /// @brief Performs a soft or hard reset of the system.
    /// @param[in] hard `true` to do a hard reset, `false` for a soft reset
    void Reset(bool hard);

    /// @brief Erases SMPC settings and does a hard reset.
    void FactoryReset();

    /// @brief Retrieves the current video standard (NTSC or PAL) used by the system.
    /// @return the video standard in use
    [[nodiscard]] core::config::sys::VideoStandard GetVideoStandard() const noexcept {
        return configuration.system.videoStandard;
    }

    /// @brief Changes the video standard (NTSC or PAL) to the specified mode.
    /// @param[in] videoStandard the new video standard to use
    void SetVideoStandard(core::config::sys::VideoStandard videoStandard) {
        configuration.system.videoStandard = videoStandard;
    }

    /// @brief Retrieves the current clock speed mode (320 or 352) in use by the system.
    /// @return the current clock speed mode
    [[nodiscard]] sys::ClockSpeed GetClockSpeed() const noexcept;

    /// @brief Changes the clock speed (320 or 352) to the specified mode.
    /// @param[in] clockSpeed the new clock speed mode to use
    void SetClockSpeed(sys::ClockSpeed clockSpeed);

    /// @brief Retrieves the current clock ratios in use by the system based on the current video standard and clock
    /// speed.
    /// @return the clock ratios in use
    [[nodiscard]] const sys::ClockRatios &GetClockRatios() const noexcept;

    /// @brief Loads the specified IPL ROM image.
    /// @param[in] ipl the contents of the IPL ROM image
    void LoadIPL(std::span<uint8, sys::kIPLSize> ipl);

    /// @brief Loads the specified CD Block ROM image.
    /// @param[in] rom the contents of the CD Block ROM image
    void LoadCDBlockROM(std::span<uint8, sh1::kROMSize> rom);

    /// @brief Loads the specified internal backup memory image.
    ///
    /// `error` will contain the filesystem error if the image failed to load.
    ///
    /// @param[in] path the path of the internal backup memory image to load
    /// @param[in] copyOnWrite whether to map the file in copy-on-write mode, preserving its contents on the disk
    /// @param[out] error receives the filesystem error in case the image fails to load
    void LoadInternalBackupMemoryImage(std::filesystem::path path, bool copyOnWrite, std::error_code &error);

    /// @brief Retrieves the IPL ROM hash code.
    /// @return the hash code of the currently loaded IPL ROM image
    [[nodiscard]] XXH128Hash GetIPLHash() const noexcept;

    /// @brief Retrieves the game disc image hash code.
    /// @return the hash code of the currently loaded game disc image
    [[nodiscard]] XXH128Hash GetDiscHash() const noexcept;

    /// @brief Retrieves the CD interface currently in use.
    /// @return the current CD interface
    [[nodiscard]] const media::CDInterface &GetCDInterface() const noexcept {
        return m_cdif;
    }

    /// @brief Inserts a cartridge into the cartridge slot.
    /// @tparam T the cartridge type, which must be a specialization of `ymir::cart::BaseCartridge`
    /// @tparam ...Args the types of the arguments to pass to the cartridge constructor
    /// @param[in] ...args the arguments to pass to the cartridge constructor
    /// @return a pointer to the concrete instance of the cartridge
    template <typename T, typename... Args>
        requires std::derived_from<T, cart::BaseCartridge>
    T *InsertCartridge(Args &&...args) {
        return SCU.InsertCartridge<T>(std::forward<Args>(args)...);
    }

    /// @brief Removes the cartridge from the cartridge slot.
    void RemoveCartridge() {
        SCU.RemoveCartridge();
    }

    /// @brief Returns a reference to the inserted cartridge.
    /// If no cartridge is inserted, returns a reference to `ymir::cart::NoCartridge`.
    ///
    /// @return A reference to the inserted cartridge
    [[nodiscard]] cart::BaseCartridge &GetCartridge() {
        return SCU.GetCartridge();
    }

    /// @brief Loads a disc into the CD drive.
    /// @param[in] disc the disc to be moved
    void LoadDisc(media::Disc &&disc);

    /// @brief Connects to a host CD drive.
    /// @param path[in] the path to the host CD drive
    /// @return `true` if the device was opened successfully, `false` if there was an error
    bool OpenHostCDDrive(std::string path);

    /// @brief Ejects the disc from the CD drive.
    void EjectDisc();

    /// @brief Opens the CD drive tray.
    void OpenTray();

    /// @brief Closes the CD drive tray.
    void CloseTray();

    /// @brief Determines if the CD drive tray is open or closed.
    /// @return `true` if the tray is open, `false` if closed
    [[nodiscard]] bool IsTrayOpen() const noexcept;

    /// @brief Switches the SMPC area code to the preferred region.
    void UsePreferredRegion();

    /// @brief Switches the SMPC area code to the region that best matches the area codes present in the currently
    /// loaded disc, respecting the preferred region order defined in the configuration.
    void AutodetectRegion();

    /// @brief Enables or disables debug tracing on hot paths.
    ///
    /// Debug tracing is required for certain debugging features to work, such as breakpoints, watchpoints, and
    /// instruction and memory traces.
    ///
    /// Enabling this option incurs a noticeable performance penalty. It is disabled by default to ensure optimal
    /// performance when those features are not needed.
    ///
    /// Disabling debug tracing also detaches all tracers from all components.
    ///
    /// @param[in] enable whether to enable or disable debug tracing
    void EnableDebugTracing(bool enable) {
        configuration.system.debugTracing = enable;
    }

    /// @brief Determines if debug tracing is enabled.
    /// @return the debug tracing state
    [[nodiscard]] bool IsDebugTracingEnabled() const noexcept {
        return m_enableDebugTracing;
    }

    /// @brief Enables or disables SH-2 cache emulation.
    ///
    /// Most games work fine without this. Enable it to improve accuracy and compatibility with specific games.
    ///
    /// Enabling this option incurs a small performance penalty and purges all SH-2 caches.
    ///
    /// @param[in] enable whether to enable or disable SH-2 cache emulation
    void EnableSH2CacheEmulation(bool enable) {
        configuration.system.emulateSH2Cache = enable;
    }

    /// @brief Determines if SH-2 cache emulation is enabled.
    /// @return the SH-2 cache emulation state
    [[nodiscard]] bool IsSH2CacheEmulationEnabled() const noexcept {
        return m_emulateSH2Caches;
    }

    /// @brief Sets the SH-2 clock factor.
    /// @param[in] factor the clock factor ratio
    void SetSH2ClockFactor(Ratio factor) {
        configuration.system.sh2ClockFactor = factor;
    }

    /// @brief Runs the emulator until the end of the current frame using the current settings.
    ///
    /// The implementation of the function depends on the following parameters:
    /// - **Debug tracing**: configured with `EnableDebugTracing(bool)`
    /// - **SH-2 cache emulation**: configured with `EnableSH2CacheEmulation(bool)`
    void RunFrame() {
        (this->*m_runFrameFn)();
    }

    /// @brief Runs a single master SH-2 instruction using the current settings.
    ///
    /// The implementation of the function depends on the following parameters:
    /// - **Debug tracing**: configured with `EnableDebugTracing(bool)`
    /// - **SH-2 cache emulation**: configured with `EnableSH2CacheEmulation(bool)`
    /// @return the number of cycles executed
    uint64 StepMasterSH2() {
        return (this->*m_stepMSH2Fn)();
    }

    /// @brief Runs a single slave SH-2 instruction using the current settings if it is enabled.
    ///
    /// The implementation of the function depends on the following parameters:
    /// - **Debug tracing**: configured with `EnableDebugTracing(bool)`
    /// - **SH-2 cache emulation**: configured with `EnableSH2CacheEmulation(bool)`
    /// @return the number of cycles executed, zero if the slave SH-2 is disabled
    uint64 StepSlaveSH2() {
        return (this->*m_stepSSH2Fn)();
    }

    /// @brief Detaches all debug tracers from all components.
    void DetachAllTracers() {
        masterSH2.UseTracer(nullptr);
        slaveSH2.UseTracer(nullptr);
        SCU.UseTracer(nullptr);
        SCSP.UseTracer(nullptr);
        CDBlock.UseTracer(nullptr);
        CDDrive.UseTracer(nullptr);
        YGR.UseTracer(nullptr);
        VDP.UseTracer(nullptr);
        VDP.UseVDP2MemoryTracer(nullptr);
    }

    // -------------------------------------------------------------------------
    // Configuration

    /// @brief Contains the emulator core configuration.
    core::Configuration configuration;

    // -------------------------------------------------------------------------
    // Save states

    /// @brief Saves the complete system state into the given state object.
    /// @param[out] state the state object to store into
    void SaveState(savestate::SaveState &state) const;

    /// @brief Validates and loads a complete system state from the given state object.
    ///
    /// Requires the IPL ROM, CD block ROM  and disc hashes to match. Additional filtering and validations are performed
    /// by components to ensure the state is consistent and valid.
    ///
    /// The IPL and CD block ROM checks can be optionally skipped, as long as they are manually checked beforehand using
    /// the `State::Validate*ROMHash()` methods.
    ///
    /// @param[in] state the state object to load from
    /// @param[in] skipROMChecks skip IPL/CD block ROM validations
    /// @return `true` if the state was loaded successfully
    [[nodiscard]] bool LoadState(const savestate::SaveState &state, bool skipROMChecks = false);

    // -------------------------------------------------------------------------
    // Debugger

    /// @brief Sets the debug break callback to be invoked when the debug break signal is raised.
    /// @param[in] callback the debug break callback
    void SetDebugBreakRaisedCallback(debug::CBDebugBreakRaised callback) {
        m_debugBreakMgr.SetDebugBreakRaisedCallback(callback);
    }

    /// @brief Dumps the CD Block DRAM to the specified output stream.
    /// @param[in] out the output stream
    void DumpCDBlockDRAM(std::ostream &out);

private:
    /// @brief Runs the emulator until the end of the current frame.
    /// @tparam debug whether to use debug tracing
    /// @tparam enableSH2Cache whether to emulate SH-2 caches
    /// @tparam cdblockLLE whether to use low-level CD block emulation
    template <bool debug, bool enableSH2Cache, bool cdblockLLE>
    void RunFrameImpl();

    /// @brief Runs the emulator until the next scheduled event.
    /// @tparam debug whether to use debug tracing
    /// @tparam enableSH2Cache whether to emulate SH-2 caches
    /// @tparam cdblockLLE whether to use low-level CD block emulation
    /// @return true if execution should continue, false to suspend
    template <bool debug, bool enableSH2Cache, bool cdblockLLE>
    bool Run();

    /// @brief Runs a single master SH-2 instruction.
    /// @tparam debug whether to use debug tracing
    /// @tparam enableSH2Cache whether to emulate SH-2 caches
    /// @tparam cdblockLLE whether to use low-level CD block emulation
    /// @return the number of cycles executed
    template <bool debug, bool enableSH2Cache, bool cdblockLLE>
    uint64 StepMasterSH2Impl();

    /// @brief Runs a single slave SH-2 instruction if the CPU is enabled.
    /// @tparam debug whether to use debug tracing
    /// @tparam enableSH2Cache whether to emulate SH-2 caches
    /// @tparam cdblockLLE whether to use low-level CD block emulation
    /// @return the number of cycles executed, zero if the slave SH-2 is disabled
    template <bool debug, bool enableSH2Cache, bool cdblockLLE>
    uint64 StepSlaveSH2Impl();

    /// @brief The type of the `RunFrameImpl()` implementation to use from `RunFrame()`.
    using RunFrameFn = void (Saturn::*)();

    /// @brief The current `RunFrameImpl()` implementation in use.
    ///
    /// Depends on debug tracing and SH-2 cache emulation settings.
    RunFrameFn m_runFrameFn;

    /// @brief The type of the `StepMasterSH2Impl()` implementation to use from `StepMasterSH2()`.
    using StepSH2Fn = uint64 (Saturn::*)();

    /// @brief The current `StepMasterSH2Impl()` implementation in use.
    ///
    /// Depends on debug tracing and SH-2 cache emulation settings.
    StepSH2Fn m_stepMSH2Fn;

    /// @brief The current `StepSlaveSH2Impl()` implementation in use.
    ///
    /// Depends on debug tracing and SH-2 cache emulation settings.
    StepSH2Fn m_stepSSH2Fn;

    /// @brief Updates pointers to the execution functions based on the current debug tracing, SH-2 cache emulation and
    /// low-level CD Block emulation settings.
    void UpdateFunctionPointers();

    /// @brief Helper template to convert runtime parameters into compile-time constants for building function pointers.
    template <bool... t_features>
    void UpdateFunctionPointersTemplate(bool feature, auto... features);

    /// @brief Terminal case for helper template.
    template <bool... t_features>
    void UpdateFunctionPointersTemplate();

    // -------------------------------------------------------------------------
    // Cycle counting
    // NOTE: Scheduler must be initialized before other components as they use it to register events

    /// @brief The event scheduler.
    core::Scheduler m_scheduler;

    /// @brief Advances the SH-1 CPU by the specified number of system (SH-2) cycles.
    /// Uses and updates spillover and fractional SH-1 cycle counters.
    /// @param[in] cycles the number of system cycles to advance
    void AdvanceSH1(uint64 cycles);

    /// @brief Configures bus access cycles.
    /// @param[in] fastTimings `true` to use 1 waitstate for every access, `false` to use normal timings
    void ConfigureAccessCycles(bool fastTimings);

    // -------------------------------------------------------------------------
    // Internal configuration

    /// @brief The preferred system region order to be used when auto-configuring the SMPC area code.
    std::vector<media::AreaCode> m_preferredRegionOrder;

    /// @brief Whether to use low-level emulation for the CD block.
    bool m_cdblockLLE = false;

    /// @brief Updates the preferred region order list.
    ///
    /// Registered as an observer of `ymir::core::Configuration::system::preferredRegionOrder`.
    ///
    /// @param[in] regions the new preferred region order
    void UpdatePreferredRegionOrder(std::span<const core::config::sys::Region> regions);

    /// @brief Updates the debug tracing setting and the `RunFrameFn()` pointer.
    /// @param[in] enabled whether to enable debug tracing
    void UpdateDebugTracing(bool enabled);

    /// @brief Updates the SH-2 cache emulation setting and the `RunFrameFn()` pointer.
    /// @param[in] enabled whether to enable SH-2 cache emulation
    void UpdateSH2CacheEmulation(bool enabled);

    /// @brief Updates the SH-2 clock factor and updates system clock ratios.
    /// @param[in] factor the new clock ratio
    void UpdateSH2ClockFactor(Ratio factor);

    /// @brief Updates the video standard to emulate and adjusts clock ratios across the system's components.
    /// @param[in] videoStandard the new video standard
    void UpdateVideoStandard(core::config::sys::VideoStandard videoStandard);

    /// @brief Enables or disables low-level CD block emulation.
    /// Causes a hard reset when changed.
    /// @param[in] enabled whether to enable low-level CD block emulation
    void SetCDBlockLLE(bool enabled);

    /// @brief Whether to force SH-2 cache emulation.
    bool m_forceSH2CacheEmulation = false;

    /// @brief Enables or disables forced SH-2 cache emulation.
    ///
    /// @param[in] enable whether to enable or disable forced SH-2 cache emulation
    void ForceSH2CacheEmulation(bool enable) {
        m_forceSH2CacheEmulation = enable;
        UpdateSH2CacheEmulation(configuration.system.emulateSH2Cache);
    }

    // -------------------------------------------------------------------------
    // Global components and parameters

    /// @brief Global system parameters.
    sys::System m_system;

    /// @brief Whether to use debug tracing.
    bool m_enableDebugTracing = false;

    /// @brief Whether to emulate SH2 caches.
    bool m_emulateSH2Caches = false;

public:
    // -------------------------------------------------------------------------
    // Components

    sys::SystemMemory mem;    ///< IPL ROM, low and high WRAM, internal backup memory
    sys::SH2Bus mainBus;      ///< Primary system bus connecting SH-2s, SCU, IPL ROM and WRAMs
    sh2::SH2 masterSH2;       ///< Master SH-2
    sh2::SH2 slaveSH2;        ///< Slave SH-2
    bool slaveSH2Enabled;     ///< Slave SH-2 enable flag
    scu::SCU SCU;             ///< SCU and its DSP, and the cartridge slot
    vdp::VDP VDP;             ///< VDP1 and VDP2
    smpc::SMPC SMPC;          ///< SMPC and input devices
    scsp::SCSP SCSP;          ///< SCSP and its DSP, and MC68EC000 CPU
    cdblock::CDBlock CDBlock; ///< HLE CD block

    // LLE CD block components
    // TODO: move to cdblock::CDBlockLLE and rename cdblock::CDBlock to CDBlockHLE
    sh1::SH1 SH1;                              ///< CD block SH-1
    sys::SH1Bus SH1Bus;                        ///< CD block SH-1 bus
    cdblock::CDDrive CDDrive;                  ///< CD block drive
    cdblock::YGR YGR;                          ///< CD block YGR LSI
    std::array<uint8, 512 * 1024> CDBlockDRAM; ///< CD block DRAM

private:
    // -------------------------------------------------------------------------
    // Internal state

    // TODO: use an abstraction to support reading from real drives as well as disc images
    media::CDInterface m_cdif;  ///< CD interface containing currently loaded disc
    media::fs::Filesystem m_fs; ///< Filesystem contained in the disc

    uint64 m_msh2SpilloverCycles; ///< Master SH-2 execution cycles spilled over between executions
    uint64 m_ssh2SpilloverCycles; ///< Slave SH-2 execution cycles spilled over between executions
    uint64 m_sh1SpilloverCycles;  ///< SH-1 execution cycles spilled over between executions
    uint64 m_sh1FracCycles;       ///< SH-1 fractional execution cycles spilled over by clock ratio calculation

    /// @brief Invoked when the CD interface detects a change in media.
    void OnMediaChanged();

    // -------------------------------------------------------------------------
    // System operations (SMPC) - smpc::ISMPCOperations implementation

    /// @brief SMPC operations wrapper
    struct SMPCOperations : smpc::ISMPCOperations {
        SMPCOperations(Saturn &saturn);

        bool GetNMI() const final; ///< Retrieves the NMI line state
        void RaiseNMI() final;     ///< Raises the NMI line

        void EnableAndResetSlaveSH2() final; ///< Enables and reset the slave SH-2
        void DisableSlaveSH2() final;        ///< Disables the slave SH-2

        void EnableAndResetM68K() final; ///< Enables and resets the M68K CPU
        void DisableM68K() final;        ///< Disables the M68K CPU

        void SoftResetSystem() final;      ///< Soft resets the entire system
        void ClockChangeSoftReset() final; ///< Soft resets VDP, SCU and SCSP after a clock change

        sys::ClockSpeed GetClockSpeed() const final;          ///< Retrieves the current clock speed
        void SetClockSpeed(sys::ClockSpeed clockSpeed) final; ///< Changes the current clock speed

    private:
        Saturn &m_saturn;
    };

    SMPCOperations smpcOps{*this};

    // -------------------------------------------------------------------------
    // Debugger

    debug::DebugBreakManager m_debugBreakMgr;
};

} // namespace ymir
