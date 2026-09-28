#pragma once

#include <ymir/core/configuration.hpp>

#include <ymir/hw/smpc/peripheral/peripheral_defs.hpp>
#include <ymir/sys/backup_ram_defs.hpp>

#include <ymir/db/ipl_db.hpp>

#include <app/input/input_action.hpp>
#include <app/input/input_bind.hpp>
#include <app/input/input_context.hpp>
#include <app/input/input_events.hpp>

#include <app/services/gfx/gfx_types.hpp>

#include <app/profile.hpp>

#include <app/actions.hpp>
#include <app/display.hpp>

#include <ymir/util/observable.hpp>
#include <ymir/util/ratio.hpp>

#include "settings_defaults.hpp"

#include <fmt/format.h>
#include <fmt/std.h>
#include <toml++/toml.hpp>

#include <chrono>
#include <filesystem>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace app {

inline constexpr std::string_view kSettingsFile = "Ymir.toml";
inline constexpr std::string_view kGameControllerDBFile = "gamecontrollerdb.txt";

struct SettingsLoadResult {
    enum class Type { Success, TOMLParseError, UnsupportedConfigVersion };

    static SettingsLoadResult Success() {
        return {.type = Type::Success};
    }

    static SettingsLoadResult TOMLParseError(toml::parse_error error) {
        return {.type = Type::TOMLParseError, .value = error};
    }

    static SettingsLoadResult UnsupportedConfigVersion(int version) {
        return {.type = Type::UnsupportedConfigVersion, .value = version};
    }

    operator bool() {
        return type == Type::Success;
    }

    std::string string() const {
        switch (type) {
        case Type::Success: return "Success";
        case Type::TOMLParseError: //
        {
            auto &error = std::get<toml::parse_error>(value);
            std::ostringstream ss{};
            ss << error.source();
            return fmt::format("TOML parse error: {} (at {})", error.description(), ss.str());
        }
        case Type::UnsupportedConfigVersion:
            return fmt::format("Unsupported configuration version: {}", std::get<int>(value));
        default: return "Unspecified error";
        }
    }

    Type type;
    std::variant<std::monostate, toml::parse_error, int> value;
};

struct SettingsSaveResult {
    enum class Type { Success, FilesystemError };

    static SettingsSaveResult Success() {
        return {.type = Type::Success};
    }

    static SettingsSaveResult FilesystemError(std::error_code error) {
        return {.type = Type::FilesystemError, .value = error};
    }

    operator bool() {
        return type == Type::Success;
    }

    std::string string() const {
        switch (type) {
        case Type::Success: return "Success";
        case Type::FilesystemError:
            return fmt::format("Filesystem error: {}", std::get<std::error_code>(value).message());
        default: return "Unspecified error";
        }
    }

    Type type;
    std::variant<std::monostate, std::error_code> value;
};

struct SharedContext;

struct Settings {
    Settings(SharedContext &sharedCtx) noexcept;

    void ResetToDefaults();

    void BindConfiguration(ymir::core::Configuration &config);

    SettingsLoadResult Load(const std::filesystem::path &path);
    SettingsSaveResult Save();

    // Auto-saves if the settings have been dirty for a while
    void CheckDirty();

    // Marks settings as dirty
    void MakeDirty();

    // Convenience method that marks settings as dirty when the parameter is true.
    // Can be used to simplify expressions.
    bool MakeDirty(bool dirty) {
        if (dirty) {
            MakeDirty();
        }
        return dirty;
    }

    // ---------------------------------------------------------------------------------------------

    std::filesystem::path path;

    struct General {
        bool preloadDiscImagesToRAM;
        bool rememberLastLoadedDisc;

        bool boostEmuThreadPriority;
        bool boostProcessPriority;

        int screenshotScale;

        bool enableRewindBuffer;
        // TODO: rewind buffer size
        int rewindCompressionLevel;

        util::Observable<double> mainSpeedFactor;
        util::Observable<double> altSpeedFactor;
        util::Observable<bool> useAltSpeed;

        bool pauseWhenUnfocused;
        bool unpauseOnDiscLoad;
        bool startPaused;

        bool checkForUpdates;
        bool includeNightlyBuilds;

        bool enableDiscordPresence;
    } general;

    struct GUI {
        enum class FrameRateOSDPosition { TopLeft, TopRight, BottomLeft, BottomRight };

        util::Observable<bool> overrideUIScale;
        util::Observable<double> uiScale;

        bool rememberWindowGeometry;
        bool showMessages;
        bool showGameNameOnTitleBar;
        bool showPerformanceOnTitleBar;
        bool showFrameRateOSD;
        FrameRateOSDPosition frameRateOSDPosition;
        bool showSpeedIndicatorForAllSpeeds;
    } gui;

    struct System {
        util::Observable<bool> autodetectRegion;
        util::Observable<std::vector<ymir::core::config::sys::Region>> preferredRegionOrder;

        util::Observable<ymir::core::config::sys::VideoStandard> videoStandard;

        bool emulateSH2Cache;
        util::Observable<uint32> sh2ClockFactor;

        std::filesystem::path internalBackupRAMImagePath;
        bool internalBackupRAMPerGame;

        struct IPL {
            bool overrideImage;
            std::filesystem::path path;
            ymir::db::SystemVariant variant;
        } ipl;

        struct RTC {
            util::Observable<ymir::core::config::rtc::Mode> mode;

            util::Observable<ymir::core::config::rtc::HardResetStrategy> virtHardResetStrategy;
            util::Observable<sint64> virtHardResetTimestamp;
        } rtc;
    } system;

    struct Hotkeys {
        input::InputBind openSettings{actions::general::OpenSettings};
        input::InputBind toggleWindowedVideoOutput{actions::general::ToggleWindowedVideoOutput};
        input::InputBind toggleFullScreen{actions::general::ToggleFullScreen};
        input::InputBind showMessageHistory{actions::general::ShowMessageHistory};
        input::InputBind takeScreenshot{actions::general::TakeScreenshot};
        input::InputBind exitApp{actions::general::ExitApp};

        input::InputBind toggleFrameRateOSD{actions::view::ToggleFrameRateOSD};
        input::InputBind nextFrameRateOSDPos{actions::view::NextFrameRateOSDPos};
        input::InputBind prevFrameRateOSDPos{actions::view::PrevFrameRateOSDPos};
        input::InputBind rotateScreenCW{actions::view::RotateScreenCW};
        input::InputBind rotateScreenCCW{actions::view::RotateScreenCCW};

        input::InputBind toggleMute{actions::audio::ToggleMute};
        input::InputBind increaseVolume{actions::audio::IncreaseVolume};
        input::InputBind decreaseVolume{actions::audio::DecreaseVolume};

        input::InputBind loadDisc{actions::cd_drive::LoadDisc};
        input::InputBind ejectDisc{actions::cd_drive::EjectDisc};
        input::InputBind openCloseTray{actions::cd_drive::OpenCloseTray};

        input::InputBind hardReset{actions::sys::HardReset};
        input::InputBind softReset{actions::sys::SoftReset};
        input::InputBind resetButton{actions::sys::ResetButton};

        input::InputBind turboSpeed{actions::emu::TurboSpeed};
        input::InputBind turboSpeedHold{actions::emu::TurboSpeedHold};
        input::InputBind toggleAlternateSpeed{actions::emu::ToggleAlternateSpeed};
        input::InputBind increaseSpeed{actions::emu::IncreaseSpeed};
        input::InputBind decreaseSpeed{actions::emu::DecreaseSpeed};
        input::InputBind increaseSpeedLarge{actions::emu::IncreaseSpeedLarge};
        input::InputBind decreaseSpeedLarge{actions::emu::DecreaseSpeedLarge};
        input::InputBind resetSpeed{actions::emu::ResetSpeed};
        input::InputBind pauseResume{actions::emu::PauseResume};
        input::InputBind fwdFrameStep{actions::emu::ForwardFrameStep};
        input::InputBind revFrameStep{actions::emu::ReverseFrameStep};
        input::InputBind rewind{actions::emu::Rewind};
        input::InputBind toggleRewindBuffer{actions::emu::ToggleRewindBuffer};

        input::InputBind toggleDebugTrace{actions::dbg::ToggleDebugTrace};
        input::InputBind dumpMemory{actions::dbg::DumpMemory};

        struct SaveStates {
            input::InputBind quickLoad{actions::save_states::QuickLoadState};
            input::InputBind quickSave{actions::save_states::QuickSaveState};

            input::InputBind select1{actions::save_states::SelectState1};
            input::InputBind select2{actions::save_states::SelectState2};
            input::InputBind select3{actions::save_states::SelectState3};
            input::InputBind select4{actions::save_states::SelectState4};
            input::InputBind select5{actions::save_states::SelectState5};
            input::InputBind select6{actions::save_states::SelectState6};
            input::InputBind select7{actions::save_states::SelectState7};
            input::InputBind select8{actions::save_states::SelectState8};
            input::InputBind select9{actions::save_states::SelectState9};
            input::InputBind select10{actions::save_states::SelectState10};

            input::InputBind load1{actions::save_states::LoadState1};
            input::InputBind load2{actions::save_states::LoadState2};
            input::InputBind load3{actions::save_states::LoadState3};
            input::InputBind load4{actions::save_states::LoadState4};
            input::InputBind load5{actions::save_states::LoadState5};
            input::InputBind load6{actions::save_states::LoadState6};
            input::InputBind load7{actions::save_states::LoadState7};
            input::InputBind load8{actions::save_states::LoadState8};
            input::InputBind load9{actions::save_states::LoadState9};
            input::InputBind load10{actions::save_states::LoadState10};

            input::InputBind save1{actions::save_states::SaveState1};
            input::InputBind save2{actions::save_states::SaveState2};
            input::InputBind save3{actions::save_states::SaveState3};
            input::InputBind save4{actions::save_states::SaveState4};
            input::InputBind save5{actions::save_states::SaveState5};
            input::InputBind save6{actions::save_states::SaveState6};
            input::InputBind save7{actions::save_states::SaveState7};
            input::InputBind save8{actions::save_states::SaveState8};
            input::InputBind save9{actions::save_states::SaveState9};
            input::InputBind save10{actions::save_states::SaveState10};

            input::InputBind undoLoad{actions::save_states::UndoLoadState};
            input::InputBind undoSave{actions::save_states::UndoSaveState};
        } saveStates;
    } hotkeys;

    struct Input {
        struct Port {
            util::Observable<ymir::peripheral::PeripheralType> type;

            struct ControlPad {
                struct Binds {
                    input::InputBind a{actions::control_pad::A};
                    input::InputBind b{actions::control_pad::B};
                    input::InputBind c{actions::control_pad::C};
                    input::InputBind x{actions::control_pad::X};
                    input::InputBind y{actions::control_pad::Y};
                    input::InputBind z{actions::control_pad::Z};
                    input::InputBind l{actions::control_pad::L};
                    input::InputBind r{actions::control_pad::R};
                    input::InputBind start{actions::control_pad::Start};
                    input::InputBind up{actions::control_pad::Up};
                    input::InputBind down{actions::control_pad::Down};
                    input::InputBind left{actions::control_pad::Left};
                    input::InputBind right{actions::control_pad::Right};
                    input::InputBind dpad{actions::control_pad::DPad};
                } binds;
            } controlPad;

            struct AnalogPad {
                struct Binds {
                    input::InputBind a{actions::analog_pad::A};
                    input::InputBind b{actions::analog_pad::B};
                    input::InputBind c{actions::analog_pad::C};
                    input::InputBind x{actions::analog_pad::X};
                    input::InputBind y{actions::analog_pad::Y};
                    input::InputBind z{actions::analog_pad::Z};
                    input::InputBind l{actions::analog_pad::L};
                    input::InputBind r{actions::analog_pad::R};
                    input::InputBind start{actions::analog_pad::Start};
                    input::InputBind up{actions::analog_pad::Up};
                    input::InputBind down{actions::analog_pad::Down};
                    input::InputBind left{actions::analog_pad::Left};
                    input::InputBind right{actions::analog_pad::Right};
                    input::InputBind dpad{actions::analog_pad::DPad};
                    input::InputBind analogStick{actions::analog_pad::AnalogStick};
                    input::InputBind analogL{actions::analog_pad::AnalogL};
                    input::InputBind analogR{actions::analog_pad::AnalogR};
                    input::InputBind switchMode{actions::analog_pad::SwitchMode};
                } binds;
            } analogPad;

            struct ArcadeRacer {
                struct Binds {
                    input::InputBind a{actions::arcade_racer::A};
                    input::InputBind b{actions::arcade_racer::B};
                    input::InputBind c{actions::arcade_racer::C};
                    input::InputBind x{actions::arcade_racer::X};
                    input::InputBind y{actions::arcade_racer::Y};
                    input::InputBind z{actions::arcade_racer::Z};
                    input::InputBind start{actions::arcade_racer::Start};
                    input::InputBind gearUp{actions::arcade_racer::GearUp};
                    input::InputBind gearDown{actions::arcade_racer::GearDown};
                    input::InputBind wheelLeft{actions::arcade_racer::WheelLeft};
                    input::InputBind wheelRight{actions::arcade_racer::WheelRight};
                    input::InputBind wheel{actions::arcade_racer::AnalogWheel};
                } binds;

                util::Observable<float> sensitivity;
            } arcadeRacer;

            struct MissionStick {
                struct Binds {
                    input::InputBind a{actions::mission_stick::A};
                    input::InputBind b{actions::mission_stick::B};
                    input::InputBind c{actions::mission_stick::C};
                    input::InputBind x{actions::mission_stick::X};
                    input::InputBind y{actions::mission_stick::Y};
                    input::InputBind z{actions::mission_stick::Z};
                    input::InputBind l{actions::mission_stick::L};
                    input::InputBind r{actions::mission_stick::R};
                    input::InputBind start{actions::mission_stick::Start};
                    input::InputBind mainUp{actions::mission_stick::MainUp};
                    input::InputBind mainDown{actions::mission_stick::MainDown};
                    input::InputBind mainLeft{actions::mission_stick::MainLeft};
                    input::InputBind mainRight{actions::mission_stick::MainRight};
                    input::InputBind mainStick{actions::mission_stick::MainStick};
                    input::InputBind mainThrottle{actions::mission_stick::MainThrottle};
                    input::InputBind mainThrottleUp{actions::mission_stick::MainThrottleUp};
                    input::InputBind mainThrottleDown{actions::mission_stick::MainThrottleDown};
                    input::InputBind mainThrottleMax{actions::mission_stick::MainThrottleMax};
                    input::InputBind mainThrottleMin{actions::mission_stick::MainThrottleMin};
                    input::InputBind subUp{actions::mission_stick::SubUp};
                    input::InputBind subDown{actions::mission_stick::SubDown};
                    input::InputBind subLeft{actions::mission_stick::SubLeft};
                    input::InputBind subRight{actions::mission_stick::SubRight};
                    input::InputBind subStick{actions::mission_stick::SubStick};
                    input::InputBind subThrottle{actions::mission_stick::SubThrottle};
                    input::InputBind subThrottleUp{actions::mission_stick::SubThrottleUp};
                    input::InputBind subThrottleDown{actions::mission_stick::SubThrottleDown};
                    input::InputBind subThrottleMax{actions::mission_stick::SubThrottleMax};
                    input::InputBind subThrottleMin{actions::mission_stick::SubThrottleMin};
                    input::InputBind switchMode{actions::mission_stick::SwitchMode};
                } binds;
            } missionStick;

            struct VirtuaGun {
                struct Binds {
                    input::InputBind start{actions::virtua_gun::Start};
                    input::InputBind trigger{actions::virtua_gun::Trigger};
                    input::InputBind reload{actions::virtua_gun::Reload};
                    input::InputBind up{actions::virtua_gun::Up};
                    input::InputBind down{actions::virtua_gun::Down};
                    input::InputBind left{actions::virtua_gun::Left};
                    input::InputBind right{actions::virtua_gun::Right};
                    input::InputBind move{actions::virtua_gun::Move};
                    input::InputBind recenter{actions::virtua_gun::Recenter};
                    input::InputBind speedBoost{actions::virtua_gun::SpeedBoost};
                    input::InputBind speedToggle{actions::virtua_gun::SpeedToggle};
                } binds;

                util::Observable<float> speed;
                util::Observable<float> speedBoostFactor;

                struct Crosshair {
                    std::array<float, 4> color; // R,G,B,A
                    float radius;
                    float thickness;
                    float rotation;

                    std::array<float, 4> strokeColor; // R,G,B,A
                    float strokeThickness;
                } crosshair;
            } virtuaGun;

            struct ShuttleMouse {
                struct Binds {
                    input::InputBind start{actions::shuttle_mouse::Start};
                    input::InputBind left{actions::shuttle_mouse::Left};
                    input::InputBind middle{actions::shuttle_mouse::Middle};
                    input::InputBind right{actions::shuttle_mouse::Right};
                    input::InputBind moveUp{actions::shuttle_mouse::MoveUp};
                    input::InputBind moveDown{actions::shuttle_mouse::MoveDown};
                    input::InputBind moveLeft{actions::shuttle_mouse::MoveLeft};
                    input::InputBind moveRight{actions::shuttle_mouse::MoveRight};
                    input::InputBind move{actions::shuttle_mouse::Move};
                    input::InputBind speedBoost{actions::shuttle_mouse::SpeedBoost};
                    input::InputBind speedToggle{actions::shuttle_mouse::SpeedToggle};
                } binds;

                util::Observable<float> speed;
                util::Observable<float> speedBoostFactor;
                util::Observable<float> sensitivity;
            } shuttleMouse;
        };
        std::array<Port, 2> ports;

        struct Mouse {
            enum class CaptureMode { SystemCursor, PhysicalMouse };

            util::Observable<CaptureMode> captureMode;
            bool lockToDisplay;
        } mouse;

        struct Gamepad {
            util::Observable<float> lsDeadzone;
            util::Observable<float> rsDeadzone;
            util::Observable<float> analogToDigitalSensitivity;
        } gamepad;

    } input;

    struct Video {
        enum class DisplayRotation { Normal, _90CW, _180, _90CCW };

        gfx::Backend graphicsBackend;
        std::optional<gfx::AdapterID> graphicsAdapter;

        bool forceIntegerScaling;
        bool forceAspectRatio;
        Ratio forcedAspect;
        DisplayRotation rotation;

        bool autoResizeWindow;
        bool displayVideoOutputInWindow;

        bool syncInWindowedMode;
        bool syncInFullscreenMode;
        bool useFullRefreshRateWithVideoSync;
        bool reduceLatency;

        util::Observable<bool> fullScreen;
        bool doubleClickToFullScreen;

        struct FullScreenDisplay {
            std::string name; // empty = default display

            struct Bounds {
                int x, y;
            } bounds;
        } fullScreenDisplay;

        display::DisplayMode fullScreenMode;
        bool borderlessFullScreen;

        util::Observable<bool> useHardwareAcceleration;

        struct SoftwareRenderer {
            util::Observable<bool> threadedVDP1;
            util::Observable<bool> threadedVDP2;
            util::Observable<bool> threadedDeinterlacer;
        } swRenderer;

        struct Enhancements {
            util::Observable<bool> deinterlace;
            util::Observable<bool> transparentMeshes;
            util::Observable<bool> resolutionScaling;
            util::Observable<bool> resScaleToDisplaySize;
            util::Observable<uint32> resScaleFactor;
        } enhancements;
    } video;

    struct Audio {
        struct MidiPort {
            enum Type { None, Normal, Virtual };
            std::string id;
            Type type;
        };

        util::Observable<float> volume;
        util::Observable<bool> mute;

        util::Observable<ymir::core::config::audio::SampleInterpolationMode> interpolation;
        util::Observable<bool> threadedSCSP;

        util::Observable<uint32> stepGranularity;

        util::Observable<MidiPort> midiInputPort;
        util::Observable<MidiPort> midiOutputPort;
    } audio;

    struct Cartridge {
        enum Type { None, BackupRAM, DRAM, ROM };
        Type type;

        struct BackupRAM {
            std::filesystem::path imagePath;

            enum Capacity { _4Mbit, _8Mbit, _16Mbit, _32Mbit };
            Capacity capacity;
        } backupRAM;

        struct DRAM {
            enum Capacity { _48Mbit, _32Mbit, _8Mbit };
            Capacity capacity;
        } dram;

        struct ROM {
            std::filesystem::path imagePath;
        } rom;

        bool autoLoadGameCarts;
    } cartridge;

    struct CDBlock {
        util::Observable<uint8> readSpeedFactor;
        bool useLLE;

        bool overrideROM;
        std::filesystem::path romPath;
    } cdblock;

    // ---------------------------------------------------------------------------------------------

    // Clears and rebinds all configured inputs
    void RebindInputs();

    // Clears an existing bind of the specified input element.
    // Returns the previously bound action, if any.
    [[nodiscard]] std::optional<input::MappedAction> UnbindInput(const input::InputElement &element);

    // Synchronizes input settings with those from the input context
    void SyncInputSettings();

    // Restores all default hotkeys.
    // Returns all unbound actions.
    [[nodiscard]] std::unordered_set<input::MappedAction> ResetHotkeys();

    // Restores all default input binds for the specified Control Pad.
    // Returns all unbound actions.
    // If useDefaults is true, restores the default binds, otherwise all binds are cleared.
    [[nodiscard]] std::unordered_set<input::MappedAction> ResetBinds(Input::Port::ControlPad::Binds &binds,
                                                                     bool useDefaults);

    // Restores all default input binds for the specified 3D Control Pad.
    // Returns all unbound actions.
    // If useDefaults is true, restores the default binds, otherwise all binds are cleared.
    [[nodiscard]] std::unordered_set<input::MappedAction> ResetBinds(Input::Port::AnalogPad::Binds &binds,
                                                                     bool useDefaults);

    // Restores all default input binds for the specified Arcade Racer controller.
    // Returns all unbound actions.
    // If useDefaults is true, restores the default binds, otherwise all binds are cleared.
    [[nodiscard]] std::unordered_set<input::MappedAction> ResetBinds(Input::Port::ArcadeRacer::Binds &binds,
                                                                     bool useDefaults);

    // Restores all default input binds for the specified Mission Stick controller.
    // Returns all unbound actions.
    // If useDefaults is true, restores the default binds, otherwise all binds are cleared.
    [[nodiscard]] std::unordered_set<input::MappedAction> ResetBinds(Input::Port::MissionStick::Binds &binds,
                                                                     bool useDefaults);

    // Restores all default input binds for the specified Virtua Gun controller.
    // Returns all unbound actions.
    // If useDefaults is true, restores the default binds, otherwise all binds are cleared.
    [[nodiscard]] std::unordered_set<input::MappedAction> ResetBinds(Input::Port::VirtuaGun::Binds &binds,
                                                                     bool useDefaults);

    // Restores all default input binds for the specified Shuttle Mouse controller.
    // Returns all unbound actions.
    // If useDefaults is true, restores the default binds, otherwise all binds are cleared.
    [[nodiscard]] std::unordered_set<input::MappedAction> ResetBinds(Input::Port::ShuttleMouse::Binds &binds,
                                                                     bool useDefaults);

private:
    SharedContext &m_context;

    bool m_dirty = false;
    std::chrono::steady_clock::time_point m_dirtyTimestamp;

    struct InputMap {
        std::unordered_map<input::Action, std::unordered_set<input::InputBind *>> map;
        void *context = nullptr;
    };

    InputMap m_actionInputs;
    std::array<InputMap, 2> m_controlPadInputs;
    std::array<InputMap, 2> m_analogPadInputs;
    std::array<InputMap, 2> m_arcadeRacerInputs;
    std::array<InputMap, 2> m_missionStickInputs;
    std::array<InputMap, 2> m_virtuaGunInputs;
    std::array<InputMap, 2> m_shuttleMouseInputs;

    InputMap &GetInputMapForContext(void *context);

    struct RebindContext {
        RebindContext(Settings &settings)
            : m_sharedCtx(settings.m_context)
            , m_settings(settings) {}

        void Rebind(input::InputBind &bind, const std::array<input::InputElement, input::kNumBindsPerInput> &defaults);

        std::unordered_set<input::MappedAction> GetReplacedActions() const {
            return m_replacedActions;
        }

    private:
        SharedContext &m_sharedCtx;
        Settings &m_settings;

        std::unordered_set<input::MappedAction> m_previousActions{};
        std::unordered_set<input::MappedAction> m_replacedActions{};
    };

    std::filesystem::path Proximate(ProfilePath base, std::filesystem::path path) const;
    std::filesystem::path Absolute(ProfilePath base, std::filesystem::path path) const;
};

const char *BupCapacityShortName(Settings::Cartridge::BackupRAM::Capacity capacity);
const char *BupCapacityLongName(Settings::Cartridge::BackupRAM::Capacity capacity);
Settings::Cartridge::BackupRAM::Capacity SizeToCapacity(uint32 size);
ymir::bup::BackupMemorySize CapacityToBupSize(Settings::Cartridge::BackupRAM::Capacity capacity);
uint32 CapacityToSize(Settings::Cartridge::BackupRAM::Capacity capacity);
uint32 BupSizeToSize(ymir::bup::BackupMemorySize bupSize);

} // namespace app
