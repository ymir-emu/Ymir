// Ymir SDL3 frontend
//
// Foreword
// --------
// I find frontend development to be extremely tedious and unrewarding for the most part. Whenever I start working on
// it, my desire to code vanishes. I'd rather spend two weeks troubleshooting a stupid off-by-one bug in the emulator
// core, decompiling SH2 assembly and comparing gigabytes of execution traces against other emulators than write yet
// another goddamn window for a single hour.
//
// This abomination here is the result of my half-assed attempt to provide a usable frontend for the emulator.
// If you wish to rewrite it from scratch, be my guest. Use whatever tech you want, come up with whatever design you
// wish, or just fix this mess and send a PR.
//
// Just make sure it's awesome, and follow the instructions in mainpage.hpp to use the emulator core.
//
// - StrikerX3
//
// ---------------------------------------------------------------------------------------------------------------------
//
// Listed below are some high-level implementation details on how this frontend uses the emulator core library.
//
// General usage
// -------------
// This frontend implements a simple audio sync that locks up the emulator thread while the audio ring buffer is full.
// Fast-forward simply disables audio sync, which allows the core to run as fast as possible as the audio callback
// overruns the audio buffer. The buffer size requested from the audio device is slightly smaller than 1/60 of the
// sample rate which results in minor video jitter but no frame skipping.
//
//
// Loading IPL ROMs, discs, backup memory and cartridges
// -----------------------------------------------------
// This frontend uses the standard pathways to load IPL ROMs, discs, backup memories and cartridges, with extra care
// taken to ensure thread-safety. Global mutexes for each component (peripherals, the disc and the cartridge slot) are
// carefully used throughout the frontend codebase.
//
//
// Sending input
// -------------
// This frontend allows attaching and detaching peripherals to both ports with fully customizable input binds,
// supporting both keyboard and gamepad binds simultaneously. Multiple axis inputs are combined into one and clamped to
// valid ranges.
//
//
// Receiving video frames and audio samples
// ----------------------------------------
// This frontend implements all video and audio callbacks.
//
// The VDP2 frame complete callback copies the framebuffer into an intermediate buffer and sets a signal, letting the
// GUI thread know that there is a new frame available to be rendered. It also increments the total frame counter to
// display the frame rate.
//
// The VDP1 frame complete callback simply updates the VDP1 frame counter for the FPS counter.
//
// The audio callback writes the sample to a ring buffer. It blocks the emulator thread if the ring buffer is full,
// which serves as a synchronization/pacing method to make the emulator run in real time at 100% speed.
//
//
// Persistent state
// ----------------
// This frontend persists SMPC state into the state folder of the user profile. Other state, including internal and
// external backup memories, are also persisted in the user profile directory by default, but they may be located
// anywhere on the file system.
//
//
// Debugging
// ---------
// This frontend enqueues debugger writes to be executed on the emulator thread when it is convenient and implements all
// tracers, storing their data into bounded ring buffers to be used by the debug views.
//
//
// Thread safety
// -------------
// This frontend runs the emulator core in a dedicated thread while the GUI runs on the main thread. Synchronization
// between threads is accomplished by using a blocking concurrent queue to send events to the emulator thread, which
// processes the events between frames. The debugger performs dirty reads and enqueues writes to be executed in the
// emulator thread. Video and audio callbacks use minimal synchronization.

#include "app.hpp"

#include "actions.hpp"

#include <ymir/ymir.hpp>

#include <ymir/sys/saturn.hpp>

#include <ymir/media/host_cd.hpp>

#include <ymir/util/lsn_denormals.hpp>
#include <ymir/util/process.hpp>
#include <ymir/util/scope_guard.hpp>
#include <ymir/util/string.hpp>
#include <ymir/util/thread_name.hpp>

#include <app/events/emu_event_factory.hpp>
#include <app/events/gui_event_factory.hpp>

#ifdef _WIN32
    #include <app/services/gfx/gfx_d3d_utils.hpp>
#endif
#include <app/services/disc_service.hpp>
#include <app/services/gfx/gfx_adapters.hpp>

#include <app/input/input_backend_sdl3.hpp>
#include <app/input/input_utils.hpp>

#include <app/ui/fonts/IconsMaterialSymbols.h>

#include <app/ui/widgets/cartridge_widgets.hpp>
#include <app/ui/widgets/savestate_widgets.hpp>
#include <app/ui/widgets/settings_widgets.hpp>
#include <app/ui/widgets/system_widgets.hpp>

#include <util/os_exception_handler.hpp>
#include <util/os_features.hpp>
#include <util/std_lib.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_messagebox.h>
#include <SDL3/SDL_misc.h>

#include <backends/imgui_impl_sdl3.h>

#include <imgui.h>

#include <cmrc/cmrc.hpp>

#include <stb_image.h>

#include <rtmidi/RtMidi.h>

#include <clocale>
#include <mutex>
#include <numbers>
#include <span>
#include <thread>
#include <unordered_map>
#include <vector>

CMRC_DECLARE(Ymir_sdl3_rc);

using clk = std::chrono::steady_clock;
using MidiPortType = app::Settings::Audio::MidiPort::Type;

namespace app {

template <typename... TArgs>
static void ShowStartupFailure(fmt::format_string<TArgs...> fmt, TArgs &&...args) {
    std::string message = fmt::format(fmt, static_cast<TArgs &&>(args)...);
    devlog::error<grp::base>("{}", message);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Ymir startup error", message.c_str(), nullptr);
}

App::App()
    : m_graphicsService(m_settings)
    , m_saveStateService(m_context, m_settings)
    , m_midiService(m_context.serviceLocator)
    , m_settings(m_context)
    , m_discordRPCService(m_context, m_settings)
    , m_mouseCaptureService(m_context, m_settings)
    , m_romService(m_context, m_settings,
                   [this](std::string title, std::function<void()> fnContents) {
                       m_windowManagerService.OpenGenericModal(std::move(title), std::move(fnContents));
                   })
    , m_discService(m_context, m_settings,
                    [this](std::string title, std::function<void()> fnContents) {
                        m_windowManagerService.OpenGenericModal(std::move(title), std::move(fnContents));
                    })
    , m_displayService(m_context, m_settings)
    , m_fileDialogService(m_context, m_settings)
    , m_windowManagerService(m_context, m_settings)
    , m_inputService(m_context, m_settings,
                     {.openSettings =
                          [this]() {
                              m_windowManagerService.SettingsWindow().Open = true;
                              m_windowManagerService.SettingsWindow().RequestFocus();
                          },
                      .showMessageHistory = [this]() { m_windowManagerService.MessageHistoryWindow().Open = true; },
                      .selectSaveStateSlot = [this](size_t slot) { m_saveStateService.SelectSaveStateSlot(slot); },
                      .loadSaveStateSlot = [this](size_t slot) { m_saveStateService.LoadSaveStateSlot(slot); },
                      .saveSaveStateSlot = [this](size_t slot) { m_saveStateService.SaveSaveStateSlot(slot); },
                      .toggleRewindBuffer = [this]() { ToggleRewindBuffer(); }}) {

    // Register services
    m_context.serviceLocator.Register(m_graphicsService);
    m_context.serviceLocator.Register(m_saveStateService);
    m_context.serviceLocator.Register(m_midiService);
    m_context.serviceLocator.Register(m_settings);
    m_context.serviceLocator.Register(m_screenshotService);
    m_context.serviceLocator.Register(m_updateCheckerService);
    m_context.serviceLocator.Register(m_mouseCaptureService);
    m_context.serviceLocator.Register(m_romService);
    m_context.serviceLocator.Register(m_discService);
    m_context.serviceLocator.Register(m_displayService);
    m_context.serviceLocator.Register(m_fileDialogService);
    m_context.serviceLocator.Register(m_windowManagerService);
    m_context.serviceLocator.Register(m_inputService);
    m_context.serviceLocator.Register(m_persistenceService);
    m_settings.BindConfiguration(m_context.saturn.instance->configuration);
}

App::~App() {
    m_context.saturn.instance->SMPC.ClearPersistDataCallback();
}

int App::Run(const CommandLineOptions &options) {
    devlog::info<grp::base>("{} {}", Ymir_APP_NAME, ymir::version::string);

    // Use UTF-8 locale by default on all C runtime functions
    // TODO: adjust this to the user's preferred locale (with ".UTF8" suffix) when i18n is implemented
    setlocale(LC_ALL, "en_us.UTF8");

#ifdef _WIN32
    gfx::EnumerateDXGIGraphicsAdapters();
#endif

    m_options = options;

    auto &settings = m_settings;

    {
        auto &generalSettings = settings.general;
        auto &emuSpeed = m_context.emuSpeed;

        generalSettings.mainSpeedFactor.ObserveAndNotify([&](double value) {
            emuSpeed.speedFactors[0] = value;
            m_context.audioSystem.SetSync(emuSpeed.ShouldSyncToAudio());
        });
        generalSettings.altSpeedFactor.ObserveAndNotify([&](double value) {
            emuSpeed.speedFactors[1] = value;
            m_context.audioSystem.SetSync(emuSpeed.ShouldSyncToAudio());
        });
        generalSettings.useAltSpeed.ObserveAndNotify([&](bool value) {
            emuSpeed.altSpeed = value;
            m_context.audioSystem.SetSync(emuSpeed.ShouldSyncToAudio());
        });
    }

    {
        auto &inputSettings = settings.input;
        auto &inputContext = m_context.inputContext;

        for (uint32 port = 0; port < 2; ++port) {
            inputSettings.ports[port].type.Observe([&, port = port](ymir::peripheral::PeripheralType type) {
                m_context.EnqueueEvent(events::emu::InsertPeripheral(port, type));
                bool changed = m_mouseCaptureService.SetPeripheralType(port, type);
                if (changed) {
                    m_mouseCaptureService.ReleaseAllMice();
                }
            });
        }
        inputSettings.gamepad.lsDeadzone.Observe(inputContext.GamepadLSDeadzone);
        inputSettings.gamepad.rsDeadzone.Observe(inputContext.GamepadRSDeadzone);
        inputSettings.gamepad.analogToDigitalSensitivity.Observe(inputContext.GamepadAnalogToDigitalSens);
    }

    {
        auto &audioSettings = settings.audio;

        audioSettings.midiInputPort.Observe([&](app::Settings::Audio::MidiPort value) {
            auto *input = m_midiService.GetInput();
            input->closePort();

            switch (value.type) {
            case MidiPortType::Normal: {
                int portNumber = m_midiService.FindInputPortByName(value.id);
                if (portNumber == -1) {
                    devlog::error<grp::base>("Failed opening MIDI input port: no port named {}", value.id);
                } else {
                    try {
                        input->openPort(portNumber);
                        devlog::debug<grp::base>("Opened MIDI input port {}", value.id);
                    } catch (RtMidiError &error) {
                        devlog::error<grp::base>("Failed opening MIDI input port {}: {}", portNumber,
                                                 error.getMessage());
                    };
                }
                break;
            }
            case MidiPortType::Virtual:
                try {
                    input->openVirtualPort(m_midiService.GetMidiVirtualInputPortName());
                    devlog::debug<grp::base>("Opened virtual MIDI input port");
                } catch (RtMidiError &error) {
                    devlog::error<grp::base>("Failed opening virtual MIDI input port: {}", error.getMessage());
                }
                break;
            default: break;
            }
        });

        audioSettings.midiOutputPort.Observe([&](app::Settings::Audio::MidiPort value) {
            auto *output = m_midiService.GetOutput();
            output->closePort();

            switch (value.type) {
            case MidiPortType::Normal: {
                int portNumber = m_midiService.FindOutputPortByName(value.id);
                if (portNumber == -1) {
                    devlog::error<grp::base>("Failed opening MIDI output port: no port named {}", value.id);
                } else {
                    try {
                        output->openPort(portNumber);
                        devlog::debug<grp::base>("Opened MIDI output port {}", value.id);
                    } catch (RtMidiError &error) {
                        devlog::error<grp::base>("Failed opening MIDI output port {}: {}", portNumber,
                                                 error.getMessage());
                    };
                }
                break;
            }
            case MidiPortType::Virtual:
                try {
                    output->openVirtualPort(m_midiService.GetMidiVirtualOutputPortName());
                    devlog::debug<grp::base>("Opened virtual MIDI output port");
                } catch (RtMidiError &error) {
                    devlog::error<grp::base>("Failed opening virtual MIDI output port: {}", error.getMessage());
                }
                break;
            default: break;
            }
        });
    }

    {
        auto &videoSettings = settings.video;

        videoSettings.enhancements.deinterlace.Observe(
            [&](bool value) { m_context.EnqueueEvent(events::emu::SetDeinterlace(value)); });
        videoSettings.enhancements.transparentMeshes.Observe(
            [&](bool value) { m_context.EnqueueEvent(events::emu::SetTransparentMeshes(value)); });
    }

    // Profile priority:
    // 1. -p option: force custom profile
    // 2. -u option: force user profile, e.g. ${HOME}/.local/share/StrikerX3/Ymir on Unix
    // 3. portable profile from current dir, if it contains the settings file
    // 4. portable profile from executable dir, if it contains the settings file
    // 5. user profile, if it contains the settings file
    // 6. show dialog to choose "installed" or "portable" mode
    // NOTE: Under Flatpak, portable profile isn't allowed; installed profile is forced
    if (!options.profilePath.empty()) {
        // -p option
        m_context.profile.UseProfilePath(options.profilePath);
    } else if (options.forceUserProfile) {
        // -u option
        m_context.profile.UseUserProfilePath();
    } else {
        bool hasSettingsFile = false;

#ifdef __linux__
        // Force "installed" mode under Flatpak
        if (getenv("FLATPAK_ID") != nullptr) {
            m_context.profile.UseUserProfilePath();
            hasSettingsFile = true; // skips all checks below
        }
#endif

        // portable profile from current dir
        if (!hasSettingsFile) {
            m_context.profile.UsePortableProfilePath();
            hasSettingsFile =
                std::filesystem::is_regular_file(m_context.profile.GetPath(ProfilePath::Root) / kSettingsFile);
        }

        if (!hasSettingsFile) {
            // portable profile from executable dir
            m_context.profile.UseExecutableProfilePath();
            hasSettingsFile =
                std::filesystem::is_regular_file(m_context.profile.GetPath(ProfilePath::Root) / kSettingsFile);
        }

        if (!hasSettingsFile) {
            // "installed" mode profile
            m_context.profile.UseUserProfilePath();
            hasSettingsFile =
                std::filesystem::is_regular_file(m_context.profile.GetPath(ProfilePath::Root) / kSettingsFile);
        }

        if (!hasSettingsFile) {
            // ask user between "installed" and "portable" modes
            auto userPath = Profile::GetUserProfilePath();
            auto portablePath = Profile::GetPortableProfilePath();

            std::string message = fmt::format("No existing profile found.\n"
                                              "Looks like this is the first time you're launching Ymir.\n"
                                              "Choose where to place settings and data:\n"
                                              "\n"
                                              "Installed: User's home directory - {}\n"
                                              "Portable: Current working directory - {}",
                                              userPath, portablePath);

            constexpr int bIDInstalled = 0;
            constexpr int bIDPortable = 1;
            constexpr int bIDCancel = 2;

            SDL_MessageBoxButtonData buttons[] = {
                {.flags = 0, .buttonID = bIDInstalled, .text = "Installed"},
                {.flags = 0, .buttonID = bIDPortable, .text = "Portable"},
                {.flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, .buttonID = bIDCancel, .text = "Cancel"},
            };
#ifdef WIN32
            std::reverse(std::begin(buttons), std::end(buttons));
#endif

            SDL_MessageBoxData messageboxdata = {.flags = SDL_MESSAGEBOX_INFORMATION,
                                                 .window = nullptr,
                                                 .title = "Ymir first time run - profile mode selection",
                                                 .message = message.c_str(),
                                                 .numbuttons = std::size(buttons),
                                                 .buttons = &buttons[0],
                                                 .colorScheme = nullptr};

            int buttonid;

            SDL_ShowMessageBox(&messageboxdata, &buttonid);

            if (buttonid == bIDCancel) {
                devlog::info<grp::base>("User cancelled profile path selection. Quitting.");
                return 0;
            } else if (buttonid == bIDInstalled) {
                m_context.profile.UseUserProfilePath();
            } else {
                m_context.profile.UsePortableProfilePath();
            }
        }
    }

    // TODO: allow overriding configuration from CommandLineOptions without modifying the underlying values

    if (auto result = settings.Load(m_context.profile.GetPath(ProfilePath::Root) / kSettingsFile); !result) {
        devlog::warn<grp::base>("Failed to load settings: {}", result.string());
    }
    util::ScopeGuard sgSaveSettings{[&] {
        if (auto result = settings.Save(); !result) {
            devlog::warn<grp::base>("Failed to save settings: {}", result.string());
        }
    }};

    if (!m_context.profile.CheckFolders()) {
        std::error_code error{};
        if (!m_context.profile.CreateFolders(error)) {
            devlog::error<grp::base>("Could not create profile folders: {}", error.message());
            return -1;
        }
    }

    devlog::debug<grp::base>("Profile directory: {}", m_context.profile.GetPath(ProfilePath::Root));

    // Apply settings
    m_context.saturn.instance->UsePreferredRegion();
    m_context.saturn.instance->configuration.cdblock.useLLE = settings.cdblock.useLLE;
    m_context.EnqueueEvent(events::emu::LoadInternalBackupMemory());
    EnableRewindBuffer(settings.general.enableRewindBuffer);
    util::BoostCurrentProcessPriority(settings.general.boostProcessPriority);
    if (settings.video.useHardwareAcceleration) {
        m_context.EnqueueEvent(events::emu::SwitchVDPRenderer(false));
    }

#if Ymir_FF_HOST_CD_DRIVES
    ymir::media::host::EnumerateHostCDDrives();
#endif

    // Load recent discs list.
    // Must be done before LoadDiscImage because it saves the recent list to the file.
    m_discService.LoadRecentDiscs();

    // Load disc image if provided
    if (!options.gameDiscPath.empty()) {
        // This also inserts the game-specific cartridges or the one configured by the user in Settings > Cartridge
        m_discService.LoadDiscImage(options.gameDiscPath, false);
    } else if (settings.general.rememberLastLoadedDisc && !m_context.state.recentDiscs.empty()) {
        m_discService.LoadDiscImage(m_context.state.recentDiscs[0], false);
    } else {
        m_context.EnqueueEvent(events::emu::InsertCartridgeFromSettings());
    }

    // Register SMPC data persistence callback
    m_context.saturn.instance->SMPC.SetPersistDataCallback(
        {&m_context, [](const ymir::smpc::PersistentSMPCData &data, void *ctx) {
             auto &sharedCtx = *static_cast<SharedContext *>(ctx);
             auto &svc = sharedCtx.serviceLocator.GetRequired<services::PersistenceService>();

             const std::filesystem::path path = sharedCtx.GetPersistentSMPCDataPath();
             svc.SavePersistentSMPCData(data, path, [&](std::string_view message) {
                 devlog::warn<grp::base>("Failed to save SMPC settings to {}: {}", path, message);
             });
         }});

    // Load IPL ROM
    // Should be done after loading disc image so that the auto-detected region is used to select the appropriate ROM.
    // This will also reload persistent SMPC data.
    m_romService.ScanIPLROMs();
    auto iplLoadResult = m_romService.LoadIPLROM();
    if (!iplLoadResult.succeeded) {
        if (m_context.romManager.GetIPLROMs().empty()) {
            // Could not load IPL ROM because there are none -- likely to be a fresh install, so show the Welcome screen
            m_windowManagerService.OpenWelcomeModal(true);
        } else {
            m_windowManagerService.OpenSimpleErrorModal(
                fmt::format("Could not load IPL ROM: {}", iplLoadResult.errorMessage));
        }
    }

    // Load CD Block ROM
    m_romService.ScanCDBlockROMs();
    auto cdbLoadResult = m_romService.LoadCDBlockROM();
    if (!cdbLoadResult.succeeded && settings.cdblock.useLLE) {
        m_windowManagerService.OpenSimpleErrorModal(
            fmt::format("Could not load CD Block ROM: {}", cdbLoadResult.errorMessage));
    }

    m_saveStateService.LoadSaveStates();

    m_context.debuggers.dirty = false;
    m_context.debuggers.dirtyTimestamp = clk::now();
    m_saveStateService.LoadDebuggerState();

    m_inputService.RebindInputs();

    RunEmulator();

    return 0;
}

void App::RunEmulator() {
    using namespace std::chrono_literals;
    using namespace ymir;
    using namespace util;

    lsn::CScopedNoSubnormals snsNoSubnormals{};

    auto &settings = m_settings;

    {
        const auto updatesPath = m_context.profile.GetPath(ProfilePath::PersistentState) / "updates";

#if Ymir_ENABLE_UPDATE_CHECKS
        // Check if user has opted in or out of automatic updates
        const auto onboardedPath = updatesPath / ".onboarded";
        const bool onboarded = std::filesystem::is_regular_file(onboardedPath);
        if (!onboarded) {
            m_windowManagerService.UpdateOnboardingWindow().Open = true;
        }
#endif

        // Start update checker thread and fire update check immediately if configured to do so
        if (Ymir_ENABLE_UPDATE_CHECKS && settings.general.checkForUpdates) {
            m_updateCheckerService.CheckForUpdates(false);
        } else {
            // Load cached results if available
            if (auto result =
                    m_context.updateChecker.Check(ReleaseChannel::Stable, updatesPath, UpdateCheckMode::Offline)) {
                m_context.updates.latestStable = result.updateInfo;
            }
            if (auto result =
                    m_context.updateChecker.Check(ReleaseChannel::Nightly, updatesPath, UpdateCheckMode::Offline)) {
                m_context.updates.latestNightly = result.updateInfo;
            }
        }
    }

    m_updateCheckerService.Start(m_context, m_settings, [&] { m_windowManagerService.UpdateWindow().Open = true; });
    ScopeGuard sgStopUpdateCheckerThread{[&] { m_updateCheckerService.Stop(); }};

    ScopeGuard sgStopDiscordRPC{[&] { m_discordRPCService.Stop(); }};

    // Get embedded file system
    auto embedfs = cmrc::Ymir_sdl3_rc::get_filesystem();

    // Screen parameters
    auto &screen = m_context.screen;

    screen.videoSync =
        settings.video.fullScreen ? settings.video.syncInFullscreenMode : settings.video.syncInWindowedMode;

    settings.system.videoStandard.ObserveAndNotify([&](core::config::sys::VideoStandard standard) {
        if (standard == core::config::sys::VideoStandard::PAL) {
            screen.frameInterval =
                std::chrono::duration_cast<clk::duration>(std::chrono::duration<double>(sys::kPALFrameInterval));
        } else {
            screen.frameInterval =
                std::chrono::duration_cast<clk::duration>(std::chrono::duration<double>(sys::kNTSCFrameInterval));
        }
    });

    // ---------------------------------
    // Initialize SDL subsystems

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) {
        ShowStartupFailure("Failed to initialize SDL: {}", SDL_GetError());
        return;
    }
    ScopeGuard sgQuit{[&] { SDL_Quit(); }};

    // ---------------------------------
    // Setup Dear ImGui context

    std::filesystem::path imguiIniLocation = m_context.profile.GetPath(ProfilePath::PersistentState) / "imgui.ini";
    auto imguiIniLocationStr = fmt::format("{}", imguiIniLocation);
    ScopeGuard sgSaveImguiIni{[&] { ImGui::SaveIniSettingsToDisk(imguiIniLocationStr.c_str()); }};

    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    ImGui::LoadIniSettingsFromDisk(imguiIniLocationStr.c_str());
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // Enable Docking
    io.KeyRepeatDelay = 0.350f;
    io.KeyRepeatRate = 0.030f;

    m_displayService.LoadFonts();

    // RescaleUI also loads the style and fonts
    bool rescaleUIPending = false;
    m_displayService.RescaleUI();
    {
        auto &guiSettings = settings.gui;

        // Observe changes to the UI scale options at this point to avoid "destroying"
        guiSettings.overrideUIScale.Observe([&](bool) { rescaleUIPending = true; });
        guiSettings.uiScale.Observe([&](double) { rescaleUIPending = true; });
    }

    const ImGuiStyle &style = ImGui::GetStyle();

    // ---------------------------------
    // Enumerate displays

    {
        int count = 0;
        SDL_DisplayID *displays = SDL_GetDisplays(&count);
        if (displays != nullptr) {
            util::ScopeGuard sgFreeDisplays{[&] { SDL_free(displays); }};
            for (SDL_DisplayID *currDisplay = displays; *currDisplay != 0; ++currDisplay) {
                m_displayService.OnDisplayAdded(*currDisplay);
            }
        }

        // Apply full screen display configuration
        const auto &dispSettings = settings.video.fullScreenDisplay;
        m_context.display.UseDisplay(dispSettings.name, dispSettings.bounds.x, dispSettings.bounds.y);

        // Find best matching display mode and sanitize setting
        auto &mode = settings.video.fullScreenMode;
        const bool borderless = settings.video.borderlessFullScreen;
        if (!borderless && mode.IsValid()) {
            SDL_DisplayID displayID = m_context.GetSelectedDisplay();
            SDL_DisplayMode closest;
            if (SDL_GetClosestFullscreenDisplayMode(displayID, mode.width, mode.height, mode.refreshRate, true,
                                                    &closest)) {
                mode.width = closest.w;
                mode.height = closest.h;
                mode.pixelFormat = closest.format;
                mode.refreshRate = closest.refresh_rate;
                mode.pixelDensity = closest.pixel_density;
                settings.MakeDirty();
            } else {
                mode = {};
            }
        }
    }

    // ---------------------------------
    // Create window

    // Apply command line override
    if (m_options.fullScreen) {
        settings.video.fullScreen = true;
    }

    SDL_PropertiesID windowProps = SDL_CreateProperties();
    if (windowProps == 0) {
        ShowStartupFailure("Failed to create window properties: {}", SDL_GetError());
        return;
    }
    ScopeGuard sgDestroyWindowProps{[&] { SDL_DestroyProperties(windowProps); }};

    {
        bool initGeometry = true;
        int windowX, windowY;
        int windowWidth, windowHeight;

        // Try loading persistent window geometry if available
        if (settings.gui.rememberWindowGeometry) {
            std::ifstream in{m_context.profile.GetPath(ProfilePath::PersistentState) / "window.txt"};
            in >> windowX >> windowY >> windowWidth >> windowHeight;
            if (in) {
                initGeometry = false;

                int numDisplays = 0;
                SDL_DisplayID *displayIDs = SDL_GetDisplays(&numDisplays);
                if (displayIDs != nullptr) {
                    util::ScopeGuard sgFreeDisplayIDs{[&] { SDL_free(displayIDs); }};

                    // If the window geometry happens to exactly match a display's bounds, reset it
                    SDL_Rect displayRect{};
                    for (int i = 0; i < numDisplays; ++i) {
                        if (SDL_GetDisplayBounds(displayIDs[i], &displayRect)) {
                            if (windowX == displayRect.x && windowY == displayRect.y && windowWidth == displayRect.w &&
                                windowHeight == displayRect.h) {
                                const char *displayName = SDL_GetDisplayName(displayIDs[i]);
                                devlog::info<grp::base>(
                                    "Window geometry matches the bounds of display {} and will be reset", displayName);
                                devlog::info<grp::base>("{} bounds: {}x{} - {}x{}", displayName, displayRect.x,
                                                        displayRect.y, displayRect.w, displayRect.h);
                                initGeometry = true;
                                break;
                            }
                        }
                    }
                }
            }
        }

        // Compute initial window size if not loaded from persistent state
        if (initGeometry) {
            // This is equivalent to ImGui::GetFrameHeight() without requiring a window
            const float menuBarHeight = (16.0f + style.FramePadding.y * 2.0f) * m_context.displayScale;

            const auto &videoSettings = settings.video;
            const bool forceAspectRatio = videoSettings.forceAspectRatio;
            const double forcedAspect = videoSettings.forcedAspect;
            const bool horzDisplay = videoSettings.rotation == Settings::Video::DisplayRotation::Normal ||
                                     videoSettings.rotation == Settings::Video::DisplayRotation::_180;

            // Find reasonable default scale based on the primary display resolution
            SDL_Rect displayRect{};
            auto displayID = SDL_GetPrimaryDisplay();
            if (!SDL_GetDisplayBounds(displayID, &displayRect)) {
                devlog::error<grp::base>("Could not get primary display resolution: {}", SDL_GetError());

                // This will set the window scale to 1.0 without assuming any resolution
                displayRect.w = 0;
                displayRect.h = 0;
            }

            devlog::info<grp::base>("Primary display resolution: {}x{}", displayRect.w, displayRect.h);

            const double screenW = horzDisplay ? screen.width : screen.height;
            const double screenH = horzDisplay ? screen.height : screen.width;

            // Take 85% of the available display area
            const double maxScaleX = (double)displayRect.w / screenW * 0.85;
            const double maxScaleY = (double)displayRect.h / screenH * 0.85;
            double scale = std::min(maxScaleX, maxScaleY);
            if (videoSettings.forceIntegerScaling) {
                scale = std::floor(scale);
            }

            double baseWidth = forceAspectRatio ? std::ceil(screen.height * screen.scaleY * forcedAspect)
                                                : screen.width * screen.scaleX;
            double baseHeight = screen.height * screen.scaleY;
            if (!horzDisplay) {
                std::swap(baseWidth, baseHeight);
            }
            const int scaledWidth = baseWidth * scale;
            const int scaledHeight = baseHeight * scale;

            windowX = SDL_WINDOWPOS_CENTERED;
            windowY = SDL_WINDOWPOS_CENTERED;
            windowWidth = scaledWidth;
            windowHeight = scaledHeight + menuBarHeight;
        }

        // Assume the following calls succeed
        SDL_SetStringProperty(windowProps, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "Ymir " Ymir_VERSION);
        SDL_SetBooleanProperty(windowProps, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
        SDL_SetBooleanProperty(windowProps, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, true);
        SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, windowWidth);
        SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, windowHeight);
        SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_X_NUMBER, windowX);
        SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_Y_NUMBER, windowY);
        SDL_SetBooleanProperty(windowProps, SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN, true);
        SDL_SetBooleanProperty(windowProps, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, settings.video.fullScreen.Get());
    }

    screen.window = SDL_CreateWindowWithProperties(windowProps);
    if (screen.window == nullptr) {
        ShowStartupFailure("Failed to create window: {}", SDL_GetError());
        return;
    }
    ScopeGuard sgDestroyWindow{[&] {
        m_displayService.PersistWindowGeometry();
        SDL_DestroyWindow(screen.window);
        m_saveStateService.SaveDebuggerState();
    }};
    util::os::ConfigureWindowDecorations(screen.window);
    m_displayService.RescaleUI();

    settings.video.fullScreen.Observe([&](bool fullScreen) {
        devlog::info<grp::base>("{} full screen mode", (fullScreen ? "Entering" : "Leaving"));
        SDL_SetWindowFullscreen(screen.window, fullScreen);
        SDL_SyncWindow(screen.window);
    });

    // ---------------------------------
    // Create graphics backend

    gfx::PresentMode presentMode = gfx::PresentMode::VSync;
    {
        const gfx::Backend backend = settings.video.graphicsBackend;
        const std::optional<gfx::AdapterID> adapter = settings.video.graphicsAdapter;
        std::vector<std::string> failures{};

        services::GraphicsContextSpec spec{
            .backend = backend,
            .adapter = adapter,
            .window = screen.window,
        };
        auto result = m_graphicsService.InitGraphicsContext(spec, presentMode);
        if (!result) {
            std::string &failureMsg = failures.emplace_back();
            failureMsg = fmt::format("Could not create {} graphics context: {}", gfx::GraphicsBackendName(backend),
                                     result.Error().message);
            m_context.DisplayMessage(failureMsg);

            auto fallback = [&](gfx::Backend fallbackBackend) {
                if (backend == fallbackBackend) {
                    return false;
                }

                spec.backend = fallbackBackend;
                auto result = m_graphicsService.InitGraphicsContext(spec, presentMode);
                if (result) {
                    m_context.DisplayMessage(fmt::format("Reverted to {}", gfx::GraphicsBackendName(fallbackBackend)));
                    settings.video.graphicsBackend = fallbackBackend;
                    settings.MakeDirty();
                    return true;
                }

                std::string &failureMsg = failures.emplace_back();
                failureMsg = fmt::format("Fallback to {} failed: {}", gfx::GraphicsBackendName(fallbackBackend),
                                         result.Error().message);
                m_context.DisplayMessage(failureMsg);
                return false;
            };

            // Try fallback options
            if (!fallback(gfx::kDefaultBackend) && !fallback(gfx::Backend::SDLRenderer)) {
                // Nothing worked; bail out
                fmt::memory_buffer buf{};
                auto out = std::back_inserter(buf);
                fmt::format_to(out, "Failed to initialize graphics.");
                for (auto &failureMsg : failures) {
                    fmt::format_to(out, "\n{}", failureMsg);
                }
                ShowStartupFailure("{}", fmt::to_string(buf));
                return;
            }
        }
    }

    {
        const gfx::Backend backend = m_graphicsService.GetGraphicsContextBackend();
        const char *backendName = gfx::GraphicsBackendName(backend);
        devlog::info<grp::base>("{} graphics context initialized successfully", backendName);
    }

    settings.video.fullScreen.ObserveAndNotify([&](bool fullScreen) {
        devlog::info<grp::base>("{} full screen mode", (fullScreen ? "Entering" : "Leaving"));
        SDL_SetWindowFullscreen(screen.window, fullScreen);
        m_displayService.ApplyFullscreenMode();
    });

    auto &vdp = m_context.saturn.instance->VDP;

    ScopeGuard sgReleaseVDPRenderer{[&] { vdp.UseNullRenderer(); }};

    // ---------------------------------
    // Create textures to render on

    // We use two textures to render the Saturn display:
    // - The framebuffer texture containing the Saturn framebuffer, updated on every frame
    // - The display texture, rendered to the screen
    // The scaling technique used here is a combination of nearest and linear interpolations to make the uninterpolated
    // pixels look sharp at any scale. It consists of rendering the framebuffer texture into the display texture using
    // nearest interpolation with an integer scale, then rendering the display texture onto the screen with linear
    // interpolation.

    // Software framebuffer texture
    auto swFbTextureResult = m_graphicsService.CreateTexture(
        {
            .width = vdp::kMaxResH,
            .height = vdp::kMaxResV,
            .format = gfx::PixelFormat::R8G8B8X8_UNORM,
            .access = gfx::TextureAccess::Streaming,
            .filterMode = gfx::TextureFilterMode::Nearest,
            .name = "[Ymir] Software framebuffer",
        },
        [&](gfx::GUITextureHandle handle, bool recreated, void *data, size_t pitch) {
            if (recreated) {
                screen.CopyFramebufferToTexture(data, pitch);
            }
        });

    if (!swFbTextureResult) {
        ShowStartupFailure(
            "Failed to create software framebuffer texture: {}.\nThe graphics backend will reset on next launch.",
            swFbTextureResult.Error().message);
        m_graphicsService.RevertGraphicsBackend();
        return;
    };
    const gfx::GUITextureHandle swFbTexture = swFbTextureResult.Value();

    // Display texture, containing the scaled framebuffer to be displayed on the screen
    auto dispTextureResult = m_graphicsService.CreateTexture({
        .width = vdp::kMaxResH * screen.fbScale,
        .height = vdp::kMaxResV * screen.fbScale,
        .format = gfx::PixelFormat::R8G8B8X8_UNORM,
        .access = gfx::TextureAccess::RenderTarget,
        .filterMode = gfx::TextureFilterMode::Linear,
        .name = "[Ymir] Scaled display",
    });
    if (!dispTextureResult) {
        ShowStartupFailure("Failed to create display texture: {}.\nThe graphics backend will reset on next launch.",
                           dispTextureResult.Error().message);
        m_graphicsService.RevertGraphicsBackend();
        return;
    }
    const gfx::GUITextureHandle dispTexture = dispTextureResult.Value();

    auto renderDispTexture = [&](double targetWidth, double targetHeight) {
        auto &videoSettings = settings.video;
        const bool forceAspectRatio = videoSettings.forceAspectRatio;
        const double forcedAspect = videoSettings.forcedAspect;
        const double dispWidth = (forceAspectRatio ? screen.height * forcedAspect : screen.width) / screen.scaleY;
        const double dispHeight = (double)screen.height / screen.scaleX;
        const double dispScaleX = (double)targetWidth / dispWidth;
        const double dispScaleY = (double)targetHeight / dispHeight;
        const double dispScale = std::min(dispScaleX, dispScaleY);
        const uint32 scale = std::max(1.0, ceil(dispScale));

        assert(m_graphicsService.IsTextureHandleValid(dispTexture));
        assert(m_graphicsService.IsTextureHandleValid(swFbTexture));

        // Recreate render target texture if scale changed
        if (scale != screen.fbScale) {
            screen.fbScale = scale;
            auto result = m_graphicsService.ResizeTexture(dispTexture, vdp::kMaxResH * screen.fbScale,
                                                          vdp::kMaxResV * screen.fbScale);
            if (!result) {
                devlog::warn<grp::base>("Failed to resize framebuffer texture: {}", result.Error().message);
            }
        }

        // Render scaled framebuffer into display texture
        gfx::FRect srcRect{.x = 0.0f, .y = 0.0f, .w = (float)screen.width, .h = (float)screen.height};
        gfx::FRect dstRect{.x = 0.0f,
                           .y = 0.0f,
                           .w = (float)screen.width * screen.fbScale,
                           .h = (float)screen.height * screen.fbScale};

        m_graphicsService.RenderToTexture(swFbTexture, dispTexture, srcRect, dstRect);
    };

    // Logo texture
    stbi_uc *ymirLogoImgData;
    {
        // Read PNG from embedded filesystem
        cmrc::file ymirLogoFile = embedfs.open("images/ymir.png");
        int imgW, imgH, chans;
        ymirLogoImgData =
            stbi_load_from_memory((const stbi_uc *)ymirLogoFile.begin(), ymirLogoFile.size(), &imgW, &imgH, &chans, 4);
        if (ymirLogoImgData == nullptr) {
            ShowStartupFailure("Could not read logo image");
            return;
        }
        ScopeGuard sgFreeImageData{[&] { stbi_image_free(ymirLogoImgData); }};
        if (chans != 4) {
            ShowStartupFailure("Unexpected logo image format");
            return;
        }

        // Create texture with the logo image
        auto logoImageResult = m_graphicsService.CreateTexture(
            {
                .width = static_cast<uint32>(imgW),
                .height = static_cast<uint32>(imgH),
                .format = gfx::PixelFormat::R8G8B8A8_UNORM,
                .access = gfx::TextureAccess::Static,
                .name = "[Ymir] Logo image",
            },
            [=, this](gfx::GUITextureHandle texture, bool, void *data, size_t pitch) {
                auto byteData = static_cast<char *>(data);
                for (size_t y = 0; y < imgH; ++y) {
                    memcpy(byteData + y * pitch, ymirLogoImgData + y * imgW * sizeof(uint32), imgW * sizeof(uint32));
                }
            });
        if (!logoImageResult) {
            ShowStartupFailure("Failed to create logo texture: {}.\nThe graphics backend will reset on next launch.",
                               logoImageResult.Error().message);
            m_graphicsService.RevertGraphicsBackend();
            return;
        }
        m_context.images.ymirLogo.texture = logoImageResult.Value();

        m_context.images.ymirLogo.size.x = imgW;
        m_context.images.ymirLogo.size.y = imgH;
        sgFreeImageData.Cancel();
    }
    ScopeGuard sgFreeImageData{[&] { stbi_image_free(ymirLogoImgData); }};

    // ---------------------------------
    // Setup Dear ImGui Platform/Renderer backends

    m_graphicsService.ImGuiInit();

    gfx::ColorRGBA clearColor{0.0f, 0.0f, 0.0f, 1.0f};

    // ---------------------------------
    // Setup framebuffer and render callbacks

    {
        auto &renderer = vdp.GetRenderer();
        auto &callbacks = renderer.Callbacks;

        callbacks.VDP1DrawFinished.Bind(&m_context, [](void *ctx) {
            auto &sharedCtx = *static_cast<SharedContext *>(ctx);
            auto &screen = sharedCtx.screen;
            ++screen.VDP1DrawCalls;
        });

        callbacks.VDP1FramebufferSwap.Bind(&m_context, [](void *ctx) {
            auto &sharedCtx = *static_cast<SharedContext *>(ctx);
            auto &screen = sharedCtx.screen;
            ++screen.VDP1Frames;
        });

        callbacks.VDP2ResolutionChanged.Bind(&m_context, [](uint32 width, uint32 height, void *ctx) {
            auto &sharedCtx = *static_cast<SharedContext *>(ctx);
            auto &screen = sharedCtx.screen;
            if (width != screen.width || height != screen.height) {
                screen.SetResolution(width, height);
            }
        });

        callbacks.VDP2DrawFinished.Bind(&m_context, [](void *ctx) {
            auto &sharedCtx = *static_cast<SharedContext *>(ctx);
            auto &screen = sharedCtx.screen;
            ++screen.VDP2Frames;

            // Limit emulation speed if requested and not using video sync.
            // When video sync is enabled, frame pacing is done by the GUI thread.
            if (sharedCtx.emuSpeed.limitSpeed && !screen.videoSync &&
                sharedCtx.emuSpeed.GetCurrentSpeedFactor() != 1.0) {

                const auto frameInterval = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    screen.frameInterval / sharedCtx.emuSpeed.GetCurrentSpeedFactor());

                // Sleep until 1ms before the next frame presentation time, then spin wait for the deadline.
                // Skip waiting if the frame target is too far into the future.
                auto now = clk::now();
                if (now < screen.nextEmuFrameTarget + frameInterval) {
                    if (now < screen.nextEmuFrameTarget - 1ms) {
                        std::this_thread::sleep_until(screen.nextEmuFrameTarget - 1ms);
                    }
                    while (clk::now() < screen.nextEmuFrameTarget) {
                    }
                }

                now = clk::now();
                if (now > screen.nextEmuFrameTarget + frameInterval) {
                    // The delay was too long for some reason; set next frame target time relative to now
                    screen.nextEmuFrameTarget = now + frameInterval;
                } else {
                    // The delay was on time; increment by the interval
                    screen.nextEmuFrameTarget += frameInterval;
                }
            }
        });

        vdp.SetSoftwareRenderCallback({
            this,
            [](uint32 *fb, uint32 width, uint32 height, void *ctx) {
                auto &app = *static_cast<App *>(ctx);
                auto &sharedCtx = app.m_context;
                auto &screen = sharedCtx.screen;
                auto &settings = app.m_settings;
                if (width != screen.width || height != screen.height) {
                    screen.SetResolution(width, height);
                }

                if (sharedCtx.emuSpeed.limitSpeed && screen.videoSync) {
                    screen.frameRequestEvent.Wait();
                    screen.frameRequestEvent.Reset();
                }
                if (settings.video.reduceLatency || !screen.updated || screen.videoSync) {
                    std::unique_lock lock{screen.mtxFramebuffer};
                    std::copy_n(fb, width * height, screen.framebuffers[0].data());
                    screen.updated = true;
                    if (screen.videoSync) {
                        screen.frameReadyEvent.Set();
                    }
                }
            },
        });

        m_graphicsService.RegisterHardwareRendererCallbacks(vdp);
    }

    // ---------------------------------
    // Initialize audio system

    static constexpr int kSampleRate = 44100;
    static constexpr SDL_AudioFormat kSampleFormat = SDL_AUDIO_S16;
    static constexpr int kChannels = 2;
    static constexpr uint32 kBufferSize = 512; // TODO: make this configurable

    if (!m_context.audioSystem.Init(kSampleRate, kSampleFormat, kChannels, kBufferSize)) {
        ShowStartupFailure("Failed to create audio stream: {}", SDL_GetError());
        return;
    }
    ScopeGuard sgDeinitAudio{[&] { m_context.audioSystem.Deinit(); }};

    // Connect gain and mute to settings
    settings.audio.volume.ObserveAndNotify([&](float volume) { m_context.audioSystem.SetGain(volume); });
    settings.audio.mute.ObserveAndNotify([&](bool mute) { m_context.audioSystem.SetMute(mute); });

    settings.audio.stepGranularity.ObserveAndNotify(
        [&](uint32 granularity) { m_context.EnqueueEvent(events::emu::SetSCSPStepGranularity(granularity)); });

    if (m_context.audioSystem.Start()) {
        int sampleRate;
        SDL_AudioFormat audioFormat;
        int channels;
        if (!m_context.audioSystem.GetAudioStreamFormat(&sampleRate, &audioFormat, &channels)) {
            ShowStartupFailure("Failed to get audio stream format: {}", SDL_GetError());
            return;
        }
        auto formatName = [&] {
            switch (audioFormat) {
            case SDL_AUDIO_U8: return "unsigned 8-bit PCM";
            case SDL_AUDIO_S8: return "signed 8-bit PCM";
            case SDL_AUDIO_S16LE: return "signed 16-bit little-endian integer PCM";
            case SDL_AUDIO_S16BE: return "signed 16-bit big-endian integer PCM";
            case SDL_AUDIO_S32LE: return "signed 32-bit little-endian integer PCM";
            case SDL_AUDIO_S32BE: return "signed 32-bit big-endian integer PCM";
            case SDL_AUDIO_F32LE: return "32-bit little-endian floating point PCM";
            case SDL_AUDIO_F32BE: return "32-bit big-endian floating point PCM";
            default: return "unknown";
            }
        };

        devlog::info<grp::base>("Audio stream opened: {} Hz, {} channel{}, {} format", sampleRate, channels,
                                (channels == 1 ? "" : "s"), formatName());
        if (sampleRate != kSampleRate || channels != kChannels || audioFormat != kSampleFormat) {
            // Hopefully this never happens
            ShowStartupFailure("Audio format mismatch");
            return;
        }
    } else {
        ShowStartupFailure("Failed to start audio stream: {}", SDL_GetError());
        return;
    }

    m_context.saturn.instance->SCSP.SetSampleCallback(
        {&m_context.audioSystem,
         [](sint16 left, sint16 right, void *ctx) { static_cast<AudioSystem *>(ctx)->ReceiveSample(left, right); }});

    m_context.saturn.instance->SCSP.SetSendMidiOutputCallback(
        {&m_midiService, [](std::span<uint8> payload, void *ctx) {
             try {
                 auto &svc = *static_cast<services::MIDIService *>(ctx);
                 svc.GetOutput()->sendMessage(payload.data(), payload.size());
             } catch (RtMidiError &error) {
                 devlog::error<grp::base>("Failed to send MIDI output message: {}", error.getMessage());
             }
         }});

    // ---------------------------------
    // MIDI setup

    {
        auto *input = m_midiService.GetInput();
        input->setCallback(OnMidiInputReceived, this);

        const std::string api = input->getApiName(input->getCurrentApi());
        devlog::info<grp::base>("Using MIDI backend: {}", api);
    }

    // ---------------------------------
    // File dialogs

    m_fileDialogService.Initialize(screen.window);

    // ---------------------------------
    // Input action handlers

    m_context.saturn.instance->SMPC.GetPeripheralPort1().SetPeripheralReportCallback(
        util::MakeClassMemberOptionalCallback<&services::InputService::ReadPeripheral<1>>(&m_inputService));
    m_context.saturn.instance->SMPC.GetPeripheralPort2().SetPeripheralReportCallback(
        util::MakeClassMemberOptionalCallback<&services::InputService::ReadPeripheral<2>>(&m_inputService));

    auto &inputContext = m_context.inputContext;

    m_context.paused = m_options.startPaused || settings.general.startPaused;
    bool pausedByLostFocus = false;

    if (m_options.enableDebugTracing) {
        m_context.EnqueueEvent(events::emu::SetDebugTrace(true));
    }

    // ---------------------------------
    // Debugger

    m_context.saturn.instance->SetDebugBreakRaisedCallback(
        {&m_context, [](const debug::DebugBreakInfo &info, void *ctx) {
             auto &sharedCtx = *static_cast<SharedContext *>(ctx);
             sharedCtx.EnqueueEvent(events::emu::SetPaused(true));
             switch (info.event) {
                 using enum debug::DebugBreakInfo::Event;
             case SH2Breakpoint: //
                 sharedCtx.DisplayMessage(fmt::format("{}SH2 breakpoint hit at {:08X}",
                                                      (info.details.sh2Breakpoint.master ? 'M' : 'S'),
                                                      info.details.sh2Breakpoint.pc));
                 sharedCtx.EnqueueEvent(events::gui::OpenSH2DebuggerWindow(info.details.sh2Breakpoint.master, true));
                 break;
             case SH2Watchpoint: //
             {
                 const auto &wtptInfo = info.details.sh2Watchpoint;

                 fmt::memory_buffer msgBuf{};
                 auto writer = std::back_inserter(msgBuf);

                 fmt::format_to(writer, "{}SH2 ", (wtptInfo.master ? 'M' : 'S'));

                 if (std::popcount(wtptInfo.mask) == 1) {
                     fmt::format_to(writer, "watchpoint");
                 } else {
                     fmt::format_to(writer, "watchpoints");
                 }
                 fmt::format_to(writer, " on ");
                 uint8 mask = wtptInfo.mask;
                 uint8 offset = 0;
                 bool sep = false;
                 while (mask != 0) {
                     const uint8 zeros = std::countr_zero(mask);
                     const uint32 address = wtptInfo.address + zeros + offset;
                     offset += zeros + 1;
                     mask >>= zeros + 1;
                     if (sep) {
                         fmt::format_to(writer, ", ");
                     } else {
                         sep = true;
                     }
                     fmt::format_to(writer, "{:08X}", address);
                 }

                 fmt::format_to(writer, " hit at {:08X} by {}-bit {} {:08X}", wtptInfo.pc, wtptInfo.size * 8,
                                (wtptInfo.write ? "write to" : "read from"), wtptInfo.address);

                 sharedCtx.DisplayMessage(fmt::to_string(msgBuf));
                 sharedCtx.EnqueueEvent(events::gui::OpenSH2DebuggerWindow(wtptInfo.master, true));
                 break;
             }
             default: sharedCtx.DisplayMessage("Paused due to a debug break event"); break;
             }
         }});

    m_context.saturn.instance->SCU.SetDebugPortWriteCallback(
        util::MakeClassMemberOptionalCallback<&SCUTracer::DebugPortWrite>(&m_context.tracers.SCU));

    // ---------------------------------
    // Main emulator loop

    m_context.emuSpeed.limitSpeed = !m_options.startFastForward;
    m_context.audioSystem.SetSync(m_context.emuSpeed.ShouldSyncToAudio());

    m_context.saturn.instance->Reset(true);

    auto t = clk::now();
    m_mouseHideTime = t;

    // Start emulator thread
    StartEmulatorThread();
    ScopeGuard sgStopEmuThread{[this] { StopEmulatorThread(); }};

    // Start screenshot processor thread
    m_screenshotService.Start(m_context);
    ScopeGuard sgStopScreenshotThread{[&] { m_screenshotService.Stop(); }};

    SDL_ShowWindow(screen.window);

    m_romService.ReloadSDLGameControllerDatabases(false);

    // Maps device IDs to player indices
    struct PlayerIndexMap {
        int GetPlayerIndex(uint32 id) const {
            if (m_playerIndices.contains(id)) {
                return m_playerIndices.at(id);
            } else {
                return -1;
            }
        }

        int GetOrAssignPlayerIndex(uint32 id) {
            if (m_playerIndices.contains(id)) {
                return m_playerIndices.at(id);
            }
            return AssignPlayerIndex(id);
        }

        int AssignPlayerIndex(uint32 id) {
            int index = -1;
            if (m_freePlayerIndices.empty()) {
                index = m_playerIndices.size();
            } else {
                auto first = m_freePlayerIndices.begin();
                index = *first;
                m_freePlayerIndices.erase(first);
            }
            m_playerIndices[id] = index;
            return index;
        }

        int ReleasePlayerIndex(uint32 id) {
            int index = GetPlayerIndex(id);
            m_playerIndices.erase(id);
            m_freePlayerIndices.insert(index);
            return index;
        }

    private:
        std::unordered_map<uint32, int> m_playerIndices{};
        std::set<int> m_freePlayerIndices;
    };

    // Track connected gamepads and mice and assigned player indices
    // NOTE: SDL3 has a bug with Windows raw input where new controllers are always assigned to player index 0.
    // We'll manage player indices manually instead.
    std::unordered_map<SDL_JoystickID, SDL_Gamepad *> gamepads{};
    PlayerIndexMap gamepadPlayerIndices;

    std::array<GUIEvent, 64> evts{};

#if Ymir_ENABLE_IMGUI_DEMO
    bool showImGuiDemoWindow = false;
#endif

    screen.nextFrameTarget = clk::now();
    double avgFrameDelay = 0.0;

    bool imguiWantedKeyboardInput = false;
    bool imguiWantedMouseInput = false;

    auto prevTime = clk::now();

    while (true) {
        bool fitWindowToScreenNow = false;
        bool forceScreenScale = false;
        int forcedScreenScale = 1;

        // Configure video sync
        const bool fullScreen = settings.video.fullScreen;
        const bool videoSync = fullScreen ? settings.video.syncInFullscreenMode : settings.video.syncInWindowedMode;
        screen.videoSync = videoSync && !m_context.paused && m_context.emuSpeed.limitSpeed;

        const double frameIntervalAdjustFactor = 0.2; // how much adjustment is applied to the frame interval

        if (m_context.emuSpeed.limitSpeed) {
            if (m_context.emuSpeed.GetCurrentSpeedFactor() == 1.0) {
                // Deliver frame early if audio buffer is emptying (video sync is slowing down emulation too much).
                // Attempt to maintain the audio buffer between 30% and 70%.
                // Smoothly adjust frame interval up or down if audio buffer exceeds either threshold.
                const uint32 audioBufferSize = m_context.audioSystem.GetBufferCount();
                const uint32 audioBufferCap = m_context.audioSystem.GetBufferCapacity();
                const double audioBufferMinLevel = 0.3;
                const double audioBufferMaxLevel = 0.7;
                const double frameIntervalAdjustWeight =
                    0.8; // how much weight the current value has over the moving avg
                const double audioBufferPct = (double)audioBufferSize / audioBufferCap;
                if (audioBufferPct < audioBufferMinLevel) {
                    // Audio buffer is too low; lower frame interval
                    const double adjustPct =
                        std::clamp((audioBufferMinLevel - audioBufferPct) / audioBufferMinLevel, 0.0, 1.0);
                    avgFrameDelay = avgFrameDelay + (adjustPct - avgFrameDelay) * frameIntervalAdjustWeight;

                } else if (audioBufferPct > audioBufferMaxLevel) {
                    // Audio buffer is too high; increase frame interval
                    const double adjustPct =
                        std::clamp((audioBufferPct - audioBufferMaxLevel) / (1.0 - audioBufferMaxLevel), 0.0, 1.0);
                    avgFrameDelay = avgFrameDelay - (adjustPct + avgFrameDelay) * frameIntervalAdjustWeight;

                } else {
                    // Audio buffer is within range; restore frame interval to normal amount
                    avgFrameDelay *= 1.0 - frameIntervalAdjustWeight;
                }
            } else {
                // Don't bother syncing to audio if not running at 100% speed
                avgFrameDelay = 0.0;
            }

            auto baseFrameInterval = screen.frameInterval / m_context.emuSpeed.GetCurrentSpeedFactor();
            const double baseFrameRate = 1000000000.0 / baseFrameInterval.count();

            double maxFrameRate = baseFrameRate;

            if (settings.video.useFullRefreshRateWithVideoSync) {
                // Use display refresh rate if requested
                const SDL_DisplayMode *dispMode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(screen.window));
                if (dispMode != nullptr && dispMode->refresh_rate != 0.0f) {
                    maxFrameRate = dispMode->refresh_rate;
                }
            } else {
                // Never go below 48 fps
                maxFrameRate = std::max(maxFrameRate, 48.0);
            }

            // Compute GUI frame duplication
            if (baseFrameRate <= maxFrameRate) {
                // Duplicate frames displayed by the GUI to maintain the minimum requested GUI frame rate
                screen.dupGUIFrames = std::max(1.0, std::floor(maxFrameRate / baseFrameRate));
                baseFrameInterval /= screen.dupGUIFrames;
            } else {
                // Base frame rate is higher than refresh rate; don't duplicate any frames
                screen.dupGUIFrames = 1;
            }

            // Update VSync setting
            gfx::PresentMode newPresentMode;
            if (videoSync) {
                newPresentMode = baseFrameRate <= maxFrameRate ? gfx::PresentMode::VSync : gfx::PresentMode::Adaptive;
            } else {
                newPresentMode = gfx::PresentMode::VSync;
            }
            if (presentMode != newPresentMode) {
                auto result = m_graphicsService.SetPresentMode(newPresentMode);
                if (result) {
                    devlog::info<grp::base>("VSync {}",
                                            (newPresentMode == gfx::PresentMode::VSync ? "enabled" : "disabled"));
                    presentMode = newPresentMode;
                } else {
                    devlog::warn<grp::base>("Could not change VSync mode: {}", result.Error().message);
                }
            }

            const auto frameInterval = std::chrono::duration_cast<std::chrono::nanoseconds>(baseFrameInterval);

            // Adjust frame presentation time
            if (m_context.paused) {
                screen.nextFrameTarget = clk::now();
            } else {
                screen.nextFrameTarget -= std::chrono::duration_cast<std::chrono::nanoseconds>(
                    frameInterval * avgFrameDelay * frameIntervalAdjustFactor);
            }

            if (screen.videoSync) {
                // Sleep until 1ms before the next frame presentation time, then spin wait for the deadline
                bool skipDelay = false;
                auto now = clk::now();
                if (now < screen.nextFrameTarget - 1ms) {
                    // Failsafe: Don't wait for longer than two frame intervals
                    auto sleepTime = screen.nextFrameTarget - 1ms - now;
                    if (sleepTime > frameInterval * 2) {
                        sleepTime = frameInterval * 2;
                        skipDelay = true;
                    }
                    std::this_thread::sleep_for(sleepTime);
                }
                if (!skipDelay) {
                    while (clk::now() < screen.nextFrameTarget) {
                    }
                }
            }

            // Update next frame target
            if (videoSync) {
                auto now = clk::now();
                if (now > screen.nextFrameTarget + frameInterval) {
                    // The frame was presented too late; set next frame target time relative to now
                    screen.nextFrameTarget = now + frameInterval;
                } else {
                    // The frame was presented on time; increment by the interval
                    screen.nextFrameTarget += frameInterval;
                }
            }
            if (++screen.dupGUIFrameCounter >= screen.dupGUIFrames) {
                screen.frameRequestEvent.Set();
                screen.dupGUIFrameCounter = 0;
                screen.expectFrame = true;
            }
        } else {
            screen.frameRequestEvent.Set();
        }

        // Process SDL events
        SDL_Event evt{};
        while (SDL_PollEvent(&evt)) {
            ImGui_ImplSDL3_ProcessEvent(&evt);
            if (io.WantCaptureKeyboard != imguiWantedKeyboardInput) {
                imguiWantedKeyboardInput = io.WantCaptureKeyboard;
                if (io.WantCaptureKeyboard) {
                    inputContext.ResetAllKeyboardInputs();
                }
            }
            if (io.WantCaptureMouse != imguiWantedMouseInput) {
                imguiWantedMouseInput = io.WantCaptureMouse;
                if (io.WantCaptureMouse) {
                    inputContext.ResetAllMouseInputs();
                }
            }

            switch (evt.type) {
            case SDL_EVENT_KEYBOARD_ADDED: [[fallthrough]];
            case SDL_EVENT_KEYBOARD_REMOVED:
                // TODO: handle these
                // evt.kdevice.type;
                // evt.kdevice.which;
                break;
            case SDL_EVENT_KEY_DOWN: [[fallthrough]];
            case SDL_EVENT_KEY_UP:
                if (!io.WantCaptureKeyboard || inputContext.IsCapturing()) {
                    // TODO: consider supporting multiple keyboards (evt.key.which)
                    inputContext.ProcessPrimitive(input::SDL3ScancodeToKeyboardKey(evt.key.scancode),
                                                  input::SDL3ToKeyModifier(evt.key.mod), evt.key.down);
                }

                // Handle ESC key press actions
                if (evt.key.scancode == SDL_SCANCODE_ESCAPE && evt.key.down) {
                    // Leave full screen while not focused on ImGui windows
                    if (!io.WantCaptureKeyboard && settings.video.fullScreen) {
                        settings.video.fullScreen = false;
                        settings.MakeDirty();
                    }

                    // Restore system mouse cursor and release captured mice
                    m_mouseCaptureService.ReleaseAllMice();
                }
                break;

            case SDL_EVENT_MOUSE_ADDED:
                if (evt.button.which != SDL_PEN_MOUSEID && evt.button.which != SDL_TOUCH_MOUSEID) {
                    inputContext.ConnectMouse(evt.mdevice.which);
                    devlog::debug<grp::base>("Mouse {} added", evt.mdevice.which);
                }
                break;
            case SDL_EVENT_MOUSE_REMOVED:
                if (evt.button.which != SDL_PEN_MOUSEID && evt.button.which != SDL_TOUCH_MOUSEID) {
                    inputContext.DisconnectMouse(evt.mdevice.which);
                    devlog::debug<grp::base>("Mouse {} removed", evt.mdevice.which);
                    m_mouseCaptureService.ReleaseMouse(evt.mdevice.which);
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN: [[fallthrough]];
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (!io.WantCaptureMouse) {
                    if (!m_mouseCaptureService.IsMouseCaptured() &&
                        !m_mouseCaptureService.HasValidPeripheralsForMouseCapture() &&
                        settings.video.doubleClickToFullScreen && evt.button.clicks % 2 == 0 && evt.button.down &&
                        evt.button.button == SDL_BUTTON_LEFT) {
                        settings.video.fullScreen = !settings.video.fullScreen;
                        settings.MakeDirty();
                    }
                }
                if (!io.WantCaptureMouse || inputContext.IsCapturing()) {
                    if (evt.button.which != SDL_PEN_MOUSEID && evt.button.which != SDL_TOUCH_MOUSEID) {
                        // TODO: evt.button.x, evt.button.y
                        // TODO: maybe evt.button.clicks?
                        inputContext.ProcessPrimitive(evt.button.which, input::SDL3ToMouseButton(evt.button.button),
                                                      evt.button.down);

                        // Try capturing the mouse cursor
                        if (evt.button.down && evt.button.button == SDL_BUTTON_LEFT) {
                            m_mouseCaptureService.ConnectMouseToPeripheral(evt.button.which);
                        }
                    }
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (evt.button.which != SDL_PEN_MOUSEID && evt.button.which != SDL_TOUCH_MOUSEID) {
                    if (!io.WantCaptureMouse || inputContext.IsCapturing()) {
                        inputContext.ProcessPrimitive(evt.button.which, input::MouseAxis2D::MouseRelative,
                                                      evt.motion.xrel, evt.motion.yrel);
                        inputContext.ProcessPrimitive(evt.button.which, input::MouseAxis2D::MouseAbsolute, evt.motion.x,
                                                      evt.motion.y);
                    }
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                if (evt.button.which != SDL_PEN_MOUSEID && evt.button.which != SDL_TOUCH_MOUSEID) {
                    if (!io.WantCaptureMouse || inputContext.IsCapturing()) {
                        const float flippedFactor = evt.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
                        inputContext.ProcessPrimitive(evt.button.which, input::MouseAxis1D::WheelHorizontal,
                                                      evt.wheel.x * flippedFactor);
                        inputContext.ProcessPrimitive(evt.button.which, input::MouseAxis1D::WheelVertical,
                                                      evt.wheel.y * flippedFactor);
                    }
                }
                break;

            case SDL_EVENT_GAMEPAD_ADDED: //
            {
                SDL_Gamepad *gamepad = SDL_OpenGamepad(evt.gdevice.which);
                if (gamepad != nullptr) {
                    // const int playerIndex = SDL_GetGamepadPlayerIndex(gamepad);
                    const int playerIndex = gamepadPlayerIndices.AssignPlayerIndex(evt.gdevice.which);
                    gamepads[evt.gdevice.which] = gamepad;
                    inputContext.ConnectGamepad(playerIndex);
                    devlog::debug<grp::base>("Gamepad {} added -> player index {}", evt.gdevice.which, playerIndex);
                }
                break;
            }
            case SDL_EVENT_GAMEPAD_REMOVED: //
            {
                if (gamepads.contains(evt.gdevice.which)) {
                    const int playerIndex = gamepadPlayerIndices.ReleasePlayerIndex(evt.gdevice.which);
                    SDL_CloseGamepad(gamepads.at(evt.gdevice.which));
                    inputContext.DisconnectGamepad(playerIndex);
                    gamepads.erase(evt.gdevice.which);
                    devlog::debug<grp::base>("Gamepad {} removed -> player index {}", evt.gdevice.which, playerIndex);
                } else {
                    devlog::warn<grp::base>("Gamepad {} removed, but it was not open!", evt.gdevice.which);
                }
                break;
            }
            case SDL_EVENT_GAMEPAD_REMAPPED: [[fallthrough]];
            case SDL_EVENT_GAMEPAD_UPDATE_COMPLETE: [[fallthrough]];
            case SDL_EVENT_GAMEPAD_STEAM_HANDLE_UPDATED:
                // TODO: handle these
                // evt.gdevice.type;
                // evt.gdevice.which;
                break;
            case SDL_EVENT_GAMEPAD_AXIS_MOTION: //
            {
                const int playerIndex = gamepadPlayerIndices.GetPlayerIndex(evt.gaxis.which);
                const float value = evt.gaxis.value < 0 ? evt.gaxis.value / 32768.0f : evt.gaxis.value / 32767.0f;
                inputContext.ProcessPrimitive(playerIndex, input::SDL3ToGamepadAxis1D((SDL_GamepadAxis)evt.gaxis.axis),
                                              value);
                break;
            }
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN: [[fallthrough]];
            case SDL_EVENT_GAMEPAD_BUTTON_UP: //
            {
                const int playerIndex = gamepadPlayerIndices.GetPlayerIndex(evt.gbutton.which);
                inputContext.ProcessPrimitive(
                    playerIndex, input::SDL3ToGamepadButton((SDL_GamepadButton)evt.gbutton.button), evt.gbutton.down);
                break;
            }

            case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN: [[fallthrough]];
            case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION: [[fallthrough]];
            case SDL_EVENT_GAMEPAD_TOUCHPAD_UP:
                // TODO: handle these
                // evt.gtouchpad.type;
                // evt.gtouchpad.which;
                // evt.gtouchpad.touchpad;
                // evt.gtouchpad.finger;
                // evt.gtouchpad.x;
                // evt.gtouchpad.y;
                // evt.gtouchpad.pressure;
                break;
            case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
                // TODO: handle these
                // evt.gsensor.which;
                // evt.gsensor.sensor;
                // evt.gsensor.data;
                break;

            case SDL_EVENT_QUIT: goto end_loop; break;

            case SDL_EVENT_DISPLAY_ADDED: m_displayService.OnDisplayAdded(evt.display.displayID); break;
            case SDL_EVENT_DISPLAY_REMOVED: m_displayService.OnDisplayRemoved(evt.display.displayID); break;

            case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
                if (m_context.display.id == 0 && !fullScreen) {
                    settings.video.fullScreenMode = {};
                }
                break;

            case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN: util::os::ConfigureWindowDecorations(screen.window); break;

            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
                if (!settings.gui.overrideUIScale) {
                    m_displayService.RescaleUI();
                    m_displayService.PersistWindowGeometry();
                }
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                m_graphicsService.ResizeFramebuffer(evt.window.data1, evt.window.data2);
                [[fallthrough]];
            case SDL_EVENT_WINDOW_MOVED: m_displayService.PersistWindowGeometry(); break;

            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (evt.window.windowID == SDL_GetWindowID(screen.window)) {
                    goto end_loop;
                }
                break;

            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                if (settings.general.pauseWhenUnfocused) {
                    if (m_context.paused && pausedByLostFocus) {
                        m_context.EnqueueEvent(events::emu::SetPaused(false));
                    }
                    pausedByLostFocus = false;
                }
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                if (settings.general.pauseWhenUnfocused) {
                    if (!m_context.paused) {
                        pausedByLostFocus = true;
                        m_context.EnqueueEvent(events::emu::SetPaused(true));
                    }
                }
                m_mouseCaptureService.ReleaseAllMice();
                break;

            case SDL_EVENT_DROP_FILE: //
            {
                std::string fileStr = evt.drop.data;
                const std::u8string u8File{fileStr.begin(), fileStr.end()};
                m_context.EnqueueEvent(events::emu::LoadDisc(u8File));
                break;
            }
            }
        }
        if (rescaleUIPending) {
            rescaleUIPending = false;
            m_displayService.RescaleUI();
        }

        // Process all axis changes
        m_context.inputContext.ProcessAxes();

        // Make emulator thread process next frame
        m_emuProcessEvent.Set();

        // Process GUI events
        const size_t evtCount = m_context.eventQueues.gui.try_dequeue_bulk(evts.begin(), evts.size());
        for (size_t i = 0; i < evtCount; i++) {
            const GUIEvent &evt = evts[i];
            using EvtType = GUIEvent::Type;
            switch (evt.type) {
            case EvtType::LoadDisc: m_discService.OpenLoadDiscDialog(); break;
            case EvtType::LoadRecommendedGameCartridge: m_romService.LoadRecommendedCartridge(); break;
            case EvtType::OpenBackupMemoryCartFileDialog: m_romService.OpenBackupMemoryCartFileDialog(); break;
            case EvtType::OpenROMCartFileDialog: m_romService.OpenROMCartFileDialog(); break;
            case EvtType::OpenPeripheralBindsEditor: {
                const auto &params = std::get<PeripheralBindsParams>(evt.value);
                m_windowManagerService.OpenPeripheralBindsEditor(params.portIndex, params.slotIndex);
                break;
            }

            case EvtType::OpenFile:
                m_fileDialogService.InvokeOpenFileDialog(std::get<FileDialogParams>(evt.value));
                break;
            case EvtType::OpenManyFiles:
                m_fileDialogService.InvokeOpenManyFilesDialog(std::get<FileDialogParams>(evt.value));
                break;
            case EvtType::SaveFile:
                m_fileDialogService.InvokeSaveFileDialog(std::get<FileDialogParams>(evt.value));
                break;
            case EvtType::SelectFolder:
                m_fileDialogService.InvokeSelectFolderDialog(std::get<FolderDialogParams>(evt.value));
                break;

            case EvtType::OpenBackupMemoryManager:
                m_windowManagerService.BackupMemoryManagerWindow().Open = true;
                break;
            case EvtType::OpenSettings:
                m_windowManagerService.SettingsWindow().OpenTab(std::get<ui::SettingsTab>(evt.value));
                break;
            case EvtType::OpenSH2DebuggerWindow: //
            {
                const auto &params = std::get<OpenSH2DebuggerWindowParams>(evt.value);
                auto &windowSet = params.master ? m_windowManagerService.MasterSH2WindowSet()
                                                : m_windowManagerService.SlaveSH2WindowSet();
                windowSet.debugger.RequestOpen(params.triggeredByEvent, true);
                break;
            }
            case EvtType::OpenSH2BreakpointsWindow: //
            {
                auto &windowSet = std::get<bool>(evt.value) ? m_windowManagerService.MasterSH2WindowSet()
                                                            : m_windowManagerService.SlaveSH2WindowSet();
                windowSet.breakpoints.Open = true;
                windowSet.breakpoints.RequestFocus();
                break;
            }
            case EvtType::OpenSH2WatchpointsWindow: //
            {
                auto &windowSet = std::get<bool>(evt.value) ? m_windowManagerService.MasterSH2WindowSet()
                                                            : m_windowManagerService.SlaveSH2WindowSet();
                windowSet.watchpoints.Open = true;
                windowSet.watchpoints.RequestFocus();
                break;
            }
            case EvtType::SetProcessPriority: util::BoostCurrentProcessPriority(std::get<bool>(evt.value)); break;
            case EvtType::SwitchGraphicsBackend: //
            {
                auto params = std::get<GraphicsBackendParams>(evt.value);
                if (params.backend != m_graphicsService.GetGraphicsContextBackend() ||
                    params.adapter != settings.video.graphicsAdapter) {

                    // Shut down hardware renderer to free up resources, otherwise the device instance held by it
                    // prevents us from reusing the window
                    if (vdp.GetRenderer().IsHardwareRenderer()) {
                        util::Event evtDone{false};
                        m_context.EnqueueEvent(events::emu::UseNullVDPRenderer(evtDone));
                        evtDone.Wait();
                    }

                    const services::GraphicsContextSpec spec{
                        .backend = params.backend,
                        .adapter = params.adapter,
                        .window = screen.window,
                    };
                    auto result = m_graphicsService.InitGraphicsContext(spec, presentMode);
                    if (result) {
                        settings.video.graphicsBackend = params.backend;
                        settings.video.graphicsAdapter = params.adapter;
                        settings.MakeDirty();
                        m_context.DisplayMessage(
                            fmt::format("{} initialized successfully", gfx::GraphicsBackendName(params.backend)));
                        m_context.EnqueueEvent(events::emu::SwitchVDPRenderer());
                    } else {
                        m_context.DisplayMessage(fmt::format("Could not initialize {} backend: {}",
                                                             gfx::GraphicsBackendName(params.backend),
                                                             result.Error().message));
                    }
                }

                break;
            }

            case EvtType::FitWindowToScreen: fitWindowToScreenNow = true; break;
            case EvtType::ApplyFullscreenMode: m_displayService.ApplyFullscreenMode(); break;

            case EvtType::RebindInputs: m_inputService.RebindInputs(); break;
            case EvtType::ReloadGameControllerDatabase: m_romService.ReloadSDLGameControllerDatabases(true); break;

            case EvtType::ShowErrorMessage:
                m_windowManagerService.OpenSimpleErrorModal(std::get<std::string>(evt.value));
                break;

            case EvtType::EnableRewindBuffer: EnableRewindBuffer(std::get<bool>(evt.value)); break;

            case EvtType::TryLoadIPLROM: //
            {
                auto path = std::get<std::filesystem::path>(evt.value);
                auto result = util::LoadIPLROM(path, *m_context.saturn.instance);
                if (result.succeeded) {
                    if (settings.system.ipl.path != path) {
                        settings.system.ipl.path = path;
                        settings.MakeDirty();
                        m_context.EnqueueEvent(events::emu::HardReset());
                    }
                } else {
                    m_windowManagerService.OpenSimpleErrorModal(
                        fmt::format("Failed to load IPL ROM from \"{}\": {}", path, result.errorMessage));
                }
                break;
            }
            case EvtType::ReloadIPLROM: //
            {
                util::ROMLoadResult result = m_romService.LoadIPLROM();
                if (result.succeeded) {
                    m_context.EnqueueEvent(events::emu::HardReset());
                } else {
                    m_windowManagerService.OpenSimpleErrorModal(fmt::format("Failed to reload IPL ROM from \"{}\": {}",
                                                                            m_context.iplRomPath, result.errorMessage));
                }
                break;
            }
            case EvtType::IPLROMLoaded: //
            {
                const std::filesystem::path path = m_context.GetPersistentSMPCDataPath();
                const std::filesystem::path oldSMPCFile =
                    m_context.profile.GetPath(ProfilePath::PersistentState) / "smpc.bin";

                // Migrate old state to new path to preseve current behavior
                if (!std::filesystem::is_regular_file(path) && std::filesystem::is_regular_file(oldSMPCFile)) {
                    std::filesystem::copy_file(oldSMPCFile, path);
                }

                ymir::smpc::PersistentSMPCData smpcData{};
                std::error_code error{};
                if (m_persistenceService.LoadPersistentSMPCData(smpcData, path, error, [&](std::string_view message) {
                        devlog::warn<grp::base>("Failed to load SMPC settings from {}: {}", path, message);
                    })) {
                    m_context.saturn.instance->SMPC.LoadPersistentData(smpcData);
                    devlog::info<grp::base>("Loaded SMPC settings from {}", path);
                } else if (error) {
                    // If it failed to load because the file doesn't exist, create the file now and reset SMPC state
                    if (!std::filesystem::is_regular_file(path)) {
                        m_context.saturn.instance->SMPC.LoadPersistentData(smpcData);
                        m_context.saturn.instance->SMPC.PersistData();
                        devlog::info<grp::base>("SMPC settings created at {}", path);
                    } else {
                        devlog::warn<grp::base>("Failed to load SMPC settings from {}: {}", path, error.message());
                    }
                }
                break;
            }
            case EvtType::TryLoadCDBlockROM: //
            {
                auto path = std::get<std::filesystem::path>(evt.value);
                auto result = util::LoadCDBlockROM(path, *m_context.saturn.instance);
                if (result.succeeded) {
                    if (settings.cdblock.romPath != path) {
                        settings.cdblock.romPath = path;
                        settings.MakeDirty();
                        if (settings.cdblock.useLLE) {
                            m_context.EnqueueEvent(events::emu::HardReset());
                        }
                    }
                } else if (settings.cdblock.useLLE) {
                    m_windowManagerService.OpenSimpleErrorModal(
                        fmt::format("Failed to load CD block ROM from \"{}\": {}", path, result.errorMessage));
                }
                break;
            }
            case EvtType::ReloadCDBlockROM: //
            {
                util::ROMLoadResult result = m_romService.LoadCDBlockROM();
                if (settings.cdblock.useLLE) {
                    if (result.succeeded) {
                        m_context.EnqueueEvent(events::emu::HardReset());
                    } else {
                        m_windowManagerService.OpenSimpleErrorModal(
                            fmt::format("Failed to reload CD block ROM from \"{}\": {}", m_context.cdbRomPath,
                                        result.errorMessage));
                    }
                }
                break;
            }

            case EvtType::TakeScreenshot: //
            {
                screenshot::Screenshot ss{};
                ss.fbWidth = screen.width;
                ss.fbHeight = screen.height;
                ss.fb.resize(screen.width * screen.height);
                std::copy_n(screen.framebuffers[1].begin(), ss.fb.size(), ss.fb.begin());
                ss.fbScaleX = screen.scaleX;
                ss.fbScaleY = screen.scaleY;
                ss.ssScale = settings.general.screenshotScale;
                ss.rotation = settings.video.rotation;
                ss.timestamp = std::chrono::system_clock::now();
                m_screenshotService.Enqueue(std::move(ss));
                break;
            }

            case EvtType::CheckForUpdates: m_updateCheckerService.CheckForUpdates(true); break;

            case EvtType::StateLoaded:
                m_context.DisplayMessage(fmt::format("State {} loaded", std::get<uint32>(evt.value) + 1));
                break;
            case EvtType::StateSaved: m_saveStateService.PersistSaveState(std::get<uint32>(evt.value)); break;
            }
        }

        // Update display
        if (screen.updated || screen.videoSync) {
            if (screen.videoSync && screen.expectFrame && !m_context.paused) {
                screen.frameReadyEvent.Wait();
                screen.frameReadyEvent.Reset();
                screen.expectFrame = false;
            }
            screen.updated = false;
            {
                std::unique_lock lock{screen.mtxFramebuffer};
                screen.framebuffers[1] = screen.framebuffers[0];
            }
            const gfx::IRect area{.x = 0, .y = 0, .w = screen.width, .h = screen.height};
            m_graphicsService.UpdateTexture(
                swFbTexture, &area, [&](void *data, size_t pitch) { screen.CopyFramebufferToTexture(data, pitch); });
        }

        auto now = clk::now();
        const auto timeDelta = now - prevTime;
        prevTime = now;

        // Calculate performance and update title bar
        {
            fmt::memory_buffer buf{};
            auto bufWriter = std::back_inserter(buf);
            fmt::format_to(bufWriter, "Ymir " Ymir_VERSION);

            if (settings.gui.showGameNameOnTitleBar) {
                fmt::format_to(bufWriter, " - ");
                std::unique_lock lock{m_context.locks.disc};
                const media::CDInterface &cdif = m_context.saturn.GetCDInterface();
                if (cdif.HasDisc()) {
                    const media::SaturnHeader &header = cdif.GetDiscHeader();
                    if (!header.productNumber.empty()) {
                        fmt::format_to(bufWriter, "[{}] ", header.productNumber);
                    }

                    if (header.gameTitle.empty()) {
                        fmt::format_to(bufWriter, "Unnamed game");
                    } else {
                        fmt::format_to(bufWriter, "{}", util::TranslateSaturnString(header.gameTitle));
                    }
                } else {
                    fmt::format_to(bufWriter, "No disc inserted");
                }
            }

            if (settings.gui.showPerformanceOnTitleBar) {
                fmt::format_to(bufWriter, " | Speed: ");
                if (m_context.paused) {
                    fmt::format_to(bufWriter, "paused");
                } else {
                    const double frameInterval = screen.frameInterval.count() * 0.000000001;
                    const double currSpeed = screen.lastVDP2Frames * frameInterval * 100.0;
                    fmt::format_to(bufWriter, "{:.0f}% / ", currSpeed);
                    if (m_context.emuSpeed.limitSpeed) {
                        fmt::format_to(bufWriter, "{:.0f}%", m_context.emuSpeed.GetCurrentSpeedFactor() * 100.0);
                        if (m_context.emuSpeed.altSpeed) {
                            fmt::format_to(bufWriter, " (alt)");
                        }
                    } else {
                        fmt::format_to(bufWriter, "\u221E%");
                    }
                }

                fmt::format_to(bufWriter, " | VDP2: ");
                if (m_context.paused) {
                    fmt::format_to(bufWriter, "paused");
                } else {
                    fmt::format_to(bufWriter, "{} fps", screen.lastVDP2Frames);
                }

                fmt::format_to(bufWriter, " | VDP1: ");
                if (m_context.paused) {
                    fmt::format_to(bufWriter, "paused");
                } else {
                    fmt::format_to(bufWriter, "{} fps, {} draws", screen.lastVDP1Frames, screen.lastVDP1DrawCalls);
                }

                fmt::format_to(bufWriter, " | GUI: {:.0f} fps", io.Framerate);
            } else if (m_context.paused) {
                fmt::format_to(bufWriter, " (paused)");
            }

            std::string title = fmt::to_string(buf);
            SDL_SetWindowTitle(screen.window, title.c_str());

            if (now - t >= 1s) {
                screen.lastVDP2Frames = screen.VDP2Frames;
                screen.lastVDP1Frames = screen.VDP1Frames;
                screen.lastVDP1DrawCalls = screen.VDP1DrawCalls;
                screen.VDP2Frames = 0;
                screen.VDP1Frames = 0;
                screen.VDP1DrawCalls = 0;
                t = now;
            }
        }

        m_inputService.UpdateInputs(std::chrono::duration<double>(timeDelta).count());

        const bool prevForceAspectRatio = settings.video.forceAspectRatio;
        const double prevForcedAspect = settings.video.forcedAspect;

        // Hide mouse cursor if no interactions were made recently or if the mouse is captured
        const bool mouseMoved = io.MouseDelta.x != 0.0f && io.MouseDelta.y != 0.0f;
        const bool mouseDown =
            io.MouseDown[0] || io.MouseDown[1] || io.MouseDown[2] || io.MouseDown[3] || io.MouseDown[4];
        if (mouseMoved || mouseDown || io.WantCaptureMouse) {
            m_mouseHideTime = clk::now();
        }
        const bool hideMouse =
            m_mouseCaptureService.ShouldHideMouse(io.WantCaptureMouse) || clk::now() >= m_mouseHideTime + 2s;
        if (hideMouse) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_None);
        }

        // Selectively enable gamepad navigation if ImGui navigation is active
        if (io.NavActive) {
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        } else {
            io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
        }

        // ---------------------------------------------------------------------
        // Draw ImGui widgets

        // Start the Dear ImGui frame
        m_graphicsService.ImGuiNewFrame();
        ImGui::NewFrame();

        // In PhysicalMouse mode, automatically release all mice if any ImGui window gains focus
        // NOTE: ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow) cannot be used becaused the application always
        // starts with main menu bar focused
        if (settings.input.mouse.captureMode == Settings::Input::Mouse::CaptureMode::PhysicalMouse &&
            io.WantCaptureKeyboard && m_mouseCaptureService.IsPhysicalMouseCapturedOrActive()) {
            m_mouseCaptureService.ReleaseAllMice();
        }

        auto *viewport = ImGui::GetMainViewport();

        const bool drawMainMenu = [&] {
            // Always draw main menu bar in windowed mode
            if (!fullScreen) {
                return true;
            }

            // -- Full screen mode --

            // Always show main menu bar if some ImGui element is focused
            if (io.NavActive || ImGui::IsAnyItemFocused()) {
                return true;
            }

            // Hide main menu bar if mouse is hidden
            if (hideMouse) {
                return false;
            }

            const float mousePosY = io.MousePos.y;
            const float vpTopQuarter =
                viewport->Pos.y + std::min(viewport->Size.y * 0.25f, 120.0f * m_context.displayScale);

            // Show menu bar if mouse is in the top quarter of the screen (minimum of 120 scaled pixels) and visible
            return mousePosY <= vpTopQuarter;
        }();

        if (drawMainMenu) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            if (ImGui::BeginMainMenuBar()) {
                ImGui::PopStyleVar();
                if (ImGui::BeginMenu("File")) {
                    // CD drive
                    if (ImGui::MenuItem("Load disc image",
                                        input::ToShortcut(inputContext, actions::cd_drive::LoadDisc).c_str())) {
                        m_discService.OpenLoadDiscDialog();
                    }
                    if (ImGui::BeginMenu("Recent disc images")) {
                        if (m_context.state.recentDiscs.empty()) {
                            ImGui::TextDisabled("(empty)");
                        } else {
                            for (int i = 0; auto &path : m_context.state.recentDiscs) {
                                std::string fullPathStr = fmt::format("{}", path);
                                std::string pathStr = fullPathStr;
                                bool shorten = pathStr.length() > 60;
                                if (shorten) {
                                    pathStr =
                                        fmt::format("[...]{}{}##{}", (char)std::filesystem::path::preferred_separator,
                                                    path.filename(), i);
                                }
                                if (ImGui::MenuItem(pathStr.c_str())) {
                                    m_context.EnqueueEvent(events::emu::LoadDisc(path));
                                }
                                if (shorten) {
                                    if (ImGui::BeginItemTooltip()) {
                                        ImGui::PushTextWrapPos(450.0f * m_context.displayScale);
                                        ImGui::Text("%s", fullPathStr.c_str());
                                        ImGui::PopTextWrapPos();
                                        ImGui::EndTooltip();
                                    }
                                }
                                ++i;
                            }
                            ImGui::Separator();
                            if (ImGui::MenuItem("Clear")) {
                                m_context.state.recentDiscs.clear();
                                m_discService.SaveRecentDiscs();
                            }
                        }
                        ImGui::EndMenu();
                    }
#if Ymir_FF_HOST_CD_DRIVES
                    if (ImGui::BeginMenu("Load from drive")) {
                        for (const media::host::HostDriveInfo &info : media::host::GetEnumeratedHostCDDrives()) {
                            const std::string drivePath = info.GetDisplayPath();
                            const char *stateStr;
                            switch (info.driveState) {
                            default: [[fallthrough]];
                            case media::DriveState::Unknown: stateStr = "Unknown"; break;
                            case media::DriveState::TrayOpen: stateStr = "Tray open"; break;
                            case media::DriveState::NoDisc: stateStr = "No disc"; break;
                            case media::DriveState::MediaPresent: stateStr = "<disc info goes here>"; break;
                            }
                            if (ImGui::MenuItem(fmt::format("[{}] {}", drivePath, stateStr).c_str())) {
                                m_context.EnqueueEvent(events::emu::OpenHostDevice(drivePath));
                            }
                        }
                        ImGui::Separator();
                        ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
                        if (ImGui::MenuItem("Reenumerate devices")) {
                            media::host::EnumerateHostCDDrives();
                        }
                        ImGui::PopItemFlag();
                        ImGui::EndMenu();
                    }
#endif
                    if (ImGui::MenuItem("Open/close tray",
                                        input::ToShortcut(inputContext, actions::cd_drive::OpenCloseTray).c_str())) {
                        m_context.EnqueueEvent(events::emu::OpenCloseTray());
                    }
                    if (ImGui::MenuItem("Eject disc",
                                        input::ToShortcut(inputContext, actions::cd_drive::EjectDisc).c_str())) {
                        m_context.EnqueueEvent(events::emu::EjectDisc());
                    }

                    ImGui::Separator();

                    // TODO: save state manager window to copy/move/swap/delete states

                    auto drawSaveStatesList = [&](bool save) {
                        const auto currentSlot = m_saveStateService.CurrentSlot();

                        for (const auto &slotMeta : m_saveStateService.List()) {
                            const auto slotIndex = slotMeta.index;
                            const auto shortcut = input::ToShortcut(
                                inputContext, save ? actions::save_states::GetSaveStateAction(slotIndex)
                                                   : actions::save_states::GetLoadStateAction(slotIndex));

                            const bool present = slotMeta.present;
                            const size_t backups = slotMeta.backupCount;
                            const bool isSelected = currentSlot == slotIndex;
                            const auto label = [&]() -> std::string {
                                if (!present) {
                                    return fmt::format("{}: (empty)", slotIndex + 1);
                                }
                                const auto localTime = util::to_local_time(slotMeta.ts);
                                if (backups == 0) {
                                    return fmt::format("{}: {}", slotIndex + 1, localTime);
                                }
                                const char *undoText = backups > 1 ? "undos" : "undo";
                                return fmt::format("{}: {} ({} {})", slotIndex + 1, localTime, backups, undoText);
                            }();

                            if (ImGui::MenuItem(label.c_str(), shortcut.c_str(), isSelected, present || save)) {
                                if (save) {
                                    m_saveStateService.SaveSaveStateSlot(slotIndex);
                                } else if (present) {
                                    m_saveStateService.LoadSaveStateSlot(slotIndex);
                                } else {
                                    m_saveStateService.SelectSaveStateSlot(slotIndex);
                                }
                            }
                        }
                    };

                    auto drawCommonSaveStatesSection = [&] {
                        if (ImGui::MenuItem("Clear all")) {
                            m_windowManagerService.OpenGenericModal(
                                "Clear all save states",
                                [&] {
                                    ImGui::TextUnformatted(
                                        "Are you sure you wish to clear all save states for this game?");
                                    if (ImGui::Button(
                                            "Yes", ImVec2(80 * m_context.displayScale, 0 * m_context.displayScale))) {
                                        m_saveStateService.ClearSaveStates();
                                        m_windowManagerService.CloseGenericModal();
                                    }
                                    ImGui::SameLine();
                                    if (ImGui::Button(
                                            "No", ImVec2(80 * m_context.displayScale, 0 * m_context.displayScale))) {
                                        m_windowManagerService.CloseGenericModal();
                                    }
                                },
                                false);
                        }
                    };

                    if (ImGui::BeginMenu("Load state")) {
                        drawSaveStatesList(false);
                        ImGui::Separator();
                        drawCommonSaveStatesSection();
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Save state")) {
                        drawSaveStatesList(true);
                        ImGui::Separator();
                        drawCommonSaveStatesSection();
                        ImGui::EndMenu();
                    }
                    {
                        auto &saves = m_context.serviceLocator.GetRequired<services::SaveStateService>();
                        if (ImGui::MenuItem(
                                "Undo save state",
                                input::ToShortcut(inputContext, actions::save_states::UndoSaveState).c_str(), false,
                                saves.GetCurrentSlotBackupStatesCount() > 0)) {
                            m_context.EnqueueEvent(events::emu::UndoSaveState());
                        }
                        if (ImGui::MenuItem(
                                "Undo load state",
                                input::ToShortcut(inputContext, actions::save_states::UndoLoadState).c_str(), false,
                                saves.CanUndoLoadState())) {
                            m_context.EnqueueEvent(events::emu::UndoLoadState());
                        }
                    }
                    if (ImGui::MenuItem("Open save states directory")) {
                        auto path = m_context.profile.GetPath(ProfilePath::SaveStates) /
                                    ToString(m_context.saturn.instance->GetDiscHash());

                        SDL_OpenURL(fmt::format("file:///{}", path).c_str());
                    }
                    if (ImGui::MenuItem("Reload save states from disk")) {
                        m_saveStateService.LoadSaveStates();
                    }

                    ImGui::Separator();

                    if (ImGui::MenuItem("Take screenshot",
                                        input::ToShortcut(inputContext, actions::general::TakeScreenshot).c_str())) {
                        m_context.EnqueueEvent(events::gui::TakeScreenshot());
                    }

                    ImGui::Separator();

                    if (ImGui::MenuItem("Open profile directory")) {
                        SDL_OpenURL(fmt::format("file:///{}", m_context.profile.GetPath(ProfilePath::Root)).c_str());
                    }

                    ImGui::Separator();

                    if (ImGui::MenuItem("Exit", "Alt+F4")) {
                        SDL_Event quitEvent{.type = SDL_EVENT_QUIT};
                        SDL_PushEvent(&quitEvent);
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("View")) {
                    auto &videoSettings = settings.video;
                    ImGui::MenuItem("Force integer scaling", nullptr, &videoSettings.forceIntegerScaling);
                    ImGui::MenuItem("Force aspect ratio", nullptr, &videoSettings.forceAspectRatio);
                    if (ImGui::SmallButton("4:3")) {
                        videoSettings.forcedAspect = 4.0 / 3.0;
                        settings.MakeDirty();
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("3:2")) {
                        videoSettings.forcedAspect = 3.0 / 2.0;
                        settings.MakeDirty();
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("16:10")) {
                        videoSettings.forcedAspect = 16.0 / 10.0;
                        settings.MakeDirty();
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("16:9")) {
                        videoSettings.forcedAspect = 16.0 / 9.0;
                        settings.MakeDirty();
                    }

                    ui::widgets::settings::video::DisplayRotation(m_context, true);

                    bool fullScreen = settings.video.fullScreen.Get();
                    if (ImGui::MenuItem("Full screen",
                                        input::ToShortcut(inputContext, actions::general::ToggleFullScreen).c_str(),
                                        &fullScreen)) {
                        videoSettings.fullScreen = fullScreen;
                        settings.MakeDirty();
                    }

                    ImGui::Separator();

                    if (ImGui::MenuItem("Auto-fit window to screen", nullptr, &videoSettings.autoResizeWindow)) {
                        if (videoSettings.autoResizeWindow) {
                            fitWindowToScreenNow = true;
                        }
                    }
                    if (ImGui::MenuItem("Fit window to screen", nullptr, nullptr,
                                        !videoSettings.displayVideoOutputInWindow)) {
                        fitWindowToScreenNow = true;
                    }
                    ImGui::MenuItem("Remember window geometry", nullptr, &settings.gui.rememberWindowGeometry);
                    if (fullScreen) {
                        ImGui::BeginDisabled();
                    }
                    ImGui::TextUnformatted("Set view scale to");
                    ImGui::SameLine();
                    if (ImGui::SmallButton("1x")) {
                        forceScreenScale = true;
                        forcedScreenScale = 1;
                        fitWindowToScreenNow = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("2x")) {
                        forceScreenScale = true;
                        forcedScreenScale = 2;
                        fitWindowToScreenNow = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("3x")) {
                        forceScreenScale = true;
                        forcedScreenScale = 3;
                        fitWindowToScreenNow = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("4x")) {
                        forceScreenScale = true;
                        forcedScreenScale = 4;
                        fitWindowToScreenNow = true;
                    }
                    if (fullScreen) {
                        ImGui::EndDisabled();
                    }

                    ImGui::Separator();

                    if (ImGui::MenuItem(
                            "Windowed video output",
                            input::ToShortcut(inputContext, actions::general::ToggleWindowedVideoOutput).c_str(),
                            &videoSettings.displayVideoOutputInWindow)) {
                        fitWindowToScreenNow = true;
                        settings.MakeDirty();
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("System")) {
                    ImGui::MenuItem("System state", nullptr, &m_windowManagerService.SystemStateWindow().Open);
                    if (ImGui::MenuItem("Copy disc hash")) {
                        std::unique_lock lock{m_context.locks.disc};
                        std::string hash = ToString(m_context.saturn.instance->GetDiscHash());
                        SDL_SetClipboardText(hash.c_str());
                    }
                    ImGui::MenuItem("Backup memory manager", nullptr,
                                    &m_windowManagerService.BackupMemoryManagerWindow().Open);

                    ImGui::Separator();

                    // Resets
                    {
                        if (ImGui::MenuItem("Soft reset",
                                            input::ToShortcut(inputContext, actions::sys::SoftReset).c_str())) {
                            m_context.EnqueueEvent(events::emu::SoftReset());
                        }
                        if (ImGui::MenuItem("Hard reset",
                                            input::ToShortcut(inputContext, actions::sys::HardReset).c_str())) {
                            m_context.EnqueueEvent(events::emu::HardReset());
                        }
                        // TODO: Let's not make it that easy to accidentally wipe system settings
                        /*if (ImGui::MenuItem("Factory reset", "Ctrl+Shift+R")) {
                            m_context.EnqueueEvent(events::emu::FactoryReset());
                        }*/
                    }

                    ImGui::Separator();

                    // Video standard and region
                    {
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextUnformatted("Video standard:");
                        ImGui::SameLine();
                        ui::widgets::VideoStandardSelector(m_context);

                        ImGui::AlignTextToFramePadding();
                        ImGui::TextUnformatted("Region");
                        ImGui::SameLine();
                        ImGui::TextDisabled("(?)");
                        if (ImGui::BeginItemTooltip()) {
                            ImGui::TextUnformatted("Changing this option will cause a hard reset");
                            ImGui::EndTooltip();
                        }
                        ImGui::SameLine();
                        ui::widgets::RegionSelector(m_context);
                    }

                    ImGui::Separator();

                    // Cartridge slot
                    {
                        ImGui::BeginDisabled();
                        ImGui::TextUnformatted("Cartridge port: ");
                        ImGui::SameLine(0, 0);
                        ui::widgets::CartridgeInfo(m_context);
                        ImGui::EndDisabled();

                        if (ImGui::MenuItem("Insert backup RAM...")) {
                            m_romService.OpenBackupMemoryCartFileDialog();
                        }
                        if (ImGui::MenuItem("Insert 8 Mbit DRAM")) {
                            m_context.EnqueueEvent(events::emu::Insert8MbitDRAMCartridge());
                        }
                        if (ImGui::MenuItem("Insert 32 Mbit DRAM")) {
                            m_context.EnqueueEvent(events::emu::Insert32MbitDRAMCartridge());
                        }
                        if (ImGui::MenuItem("Insert 48 Mbit DRAM (dev)")) {
                            m_context.EnqueueEvent(events::emu::Insert48MbitDRAMCartridge());
                        }
                        if (ImGui::MenuItem("Insert 16 Mbit ROM...")) {
                            m_romService.OpenROMCartFileDialog();
                        }

                        if (ImGui::MenuItem("Remove cartridge")) {
                            m_context.EnqueueEvent(events::emu::RemoveCartridge());
                        }
                    }

                    // ImGui::Separator();

                    // Peripherals
                    {
                        // TODO
                    }

                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Emulation")) {
                    bool rewindEnabled = settings.general.enableRewindBuffer;

                    if (ImGui::MenuItem("Pause/resume",
                                        input::ToShortcut(inputContext, actions::emu::PauseResume).c_str())) {
                        m_context.EnqueueEvent(events::emu::SetPaused(!m_context.paused));
                    }
                    if (ImGui::MenuItem("Forward frame step",
                                        input::ToShortcut(inputContext, actions::emu::ForwardFrameStep).c_str())) {
                        m_context.EnqueueEvent(events::emu::ForwardFrameStep());
                    }
                    if (ImGui::MenuItem("Reverse frame step",
                                        input::ToShortcut(inputContext, actions::emu::ReverseFrameStep).c_str(),
                                        nullptr, rewindEnabled)) {
                        if (rewindEnabled) {
                            m_context.EnqueueEvent(events::emu::ReverseFrameStep());
                        }
                    }
                    if (ImGui::MenuItem("Rewind buffer",
                                        input::ToShortcut(inputContext, actions::emu::ToggleRewindBuffer).c_str(),
                                        &rewindEnabled)) {
                        ToggleRewindBuffer();
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Settings")) {
                    auto &settingsWindow = m_windowManagerService.SettingsWindow();
                    if (ImGui::MenuItem("Settings",
                                        input::ToShortcut(inputContext, actions::general::OpenSettings).c_str(),
                                        &settingsWindow.Open)) {
                        if (settingsWindow.Open) {
                            settingsWindow.RequestFocus();
                        }
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("General")) {
                        settingsWindow.OpenTab(ui::SettingsTab::General);
                    }
                    if (ImGui::MenuItem("GUI")) {
                        settingsWindow.OpenTab(ui::SettingsTab::GUI);
                    }
                    if (ImGui::MenuItem("Hotkeys")) {
                        settingsWindow.OpenTab(ui::SettingsTab::Hotkeys);
                    }
                    if (ImGui::MenuItem("System")) {
                        settingsWindow.OpenTab(ui::SettingsTab::System);
                    }
                    if (ImGui::MenuItem("IPL (BIOS)")) {
                        settingsWindow.OpenTab(ui::SettingsTab::IPL);
                    }
                    if (ImGui::MenuItem("Input")) {
                        settingsWindow.OpenTab(ui::SettingsTab::Input);
                    }
                    if (ImGui::MenuItem("Video")) {
                        settingsWindow.OpenTab(ui::SettingsTab::Video);
                    }
                    if (ImGui::MenuItem("Audio")) {
                        settingsWindow.OpenTab(ui::SettingsTab::Audio);
                    }
                    if (ImGui::MenuItem("Cartridge")) {
                        settingsWindow.OpenTab(ui::SettingsTab::Cartridge);
                    }
                    if (ImGui::MenuItem("CD Block")) {
                        settingsWindow.OpenTab(ui::SettingsTab::CDBlock);
                    }
                    if (ImGui::MenuItem("Tweaks")) {
                        settingsWindow.OpenTab(ui::SettingsTab::Tweaks);
                    }

                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Debug")) {
                    bool debugTrace = m_context.saturn.instance->IsDebugTracingEnabled();
                    if (ImGui::MenuItem("Enable tracing",
                                        input::ToShortcut(inputContext, actions::dbg::ToggleDebugTrace).c_str(),
                                        &debugTrace)) {
                        m_context.EnqueueEvent(events::emu::SetDebugTrace(debugTrace));
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Open memory viewer", nullptr)) {
                        m_windowManagerService.OpenMemoryViewer();
                    }
                    if (ImGui::BeginMenu("Memory viewers")) {
                        for (auto &memView : m_windowManagerService.MemoryViewerWindows()) {
                            ImGui::MenuItem(fmt::format("Memory viewer #{}", memView.Index() + 1).c_str(), nullptr,
                                            &memView.Open);
                        }
                        ImGui::EndMenu();
                    }
                    if (ImGui::MenuItem("Dump all memory",
                                        input::ToShortcut(inputContext, actions::dbg::DumpMemory).c_str())) {
                        m_context.EnqueueEvent(events::emu::DumpMemory());
                    }
                    ImGui::Separator();

                    auto sh2Menu = [&](const char *name, ui::SH2WindowSet &set) {
                        if (ImGui::BeginMenu(name)) {
                            ImGui::MenuItem("Debugger", nullptr, &set.debugger.Open);
                            ImGui::Indent();
                            {
                                ImGui::MenuItem("Breakpoints", nullptr, &set.breakpoints.Open);
                                ImGui::MenuItem("Watchpoints", nullptr, &set.watchpoints.Open);
                            }
                            ImGui::Unindent();
                            ImGui::MenuItem("Interrupts", nullptr, &set.interrupts.Open);
                            ImGui::MenuItem("Interrupt trace", nullptr, &set.interruptTrace.Open);
                            ImGui::MenuItem("Exception vectors", nullptr, &set.exceptionVectors.Open);
                            ImGui::MenuItem("Cache", nullptr, &set.cache.Open);
                            ImGui::MenuItem("Division unit (DIVU)", nullptr, &set.divisionUnit.Open);
                            ImGui::MenuItem("Timers (FRT and WDT)", nullptr, &set.timers.Open);
                            ImGui::MenuItem("Power module", nullptr, &set.power.Open);
                            ImGui::MenuItem("DMA Controller (DMAC)", nullptr, &set.dmaController.Open);
                            ImGui::MenuItem("DMA Controller trace", nullptr, &set.dmaControllerTrace.Open);
                            ImGui::EndMenu();
                        }
                    };
                    sh2Menu("Master SH2", m_windowManagerService.MasterSH2WindowSet());
                    sh2Menu("Slave SH2", m_windowManagerService.SlaveSH2WindowSet());

                    if (ImGui::BeginMenu("SCU")) {
                        auto &windowSet = m_windowManagerService.SCUWindowSet();
                        ImGui::MenuItem("Registers", nullptr, windowSet.regs.Open);
                        ImGui::MenuItem("DSP", nullptr, windowSet.dsp.Open);
                        ImGui::MenuItem("DMA", nullptr, windowSet.dma.Open);
                        ImGui::MenuItem("DMA trace", nullptr, windowSet.dmaTrace.Open);
                        ImGui::MenuItem("Interrupt trace", nullptr, windowSet.intrTrace.Open);
                        ImGui::EndMenu();
                    }

                    if (ImGui::BeginMenu("SCSP")) {
                        auto &windowSet = m_windowManagerService.SCSPWindowSet();
                        ImGui::MenuItem("Output", nullptr, &windowSet.output.Open);
                        ImGui::MenuItem("Slots", nullptr, &windowSet.slots.Open);
                        ImGui::MenuItem("KYONEX trace", nullptr, &windowSet.kyonexTrace.Open);

                        ImGui::EndMenu();
                    }

                    if (ImGui::BeginMenu("VDP")) {
                        auto &windowSet = m_windowManagerService.VDPWindowSet();
                        auto layerMenuItem = [&](const char *name, vdp::Layer layer) {
                            const bool enabled = vdp.IsLayerEnabled(layer);
                            ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
                            if (ImGui::MenuItem(name, nullptr, enabled)) {
                                m_context.EnqueueEvent(events::emu::debug::SetLayerEnabled(layer, !enabled));
                            }
                            ImGui::PopItemFlag();
                        };

                        ImGui::MenuItem("Layer visibility", nullptr, &windowSet.vdp2LayerVisibility.Open);
                        ImGui::Indent();
                        layerMenuItem("Sprite", vdp::Layer::Sprite);
                        layerMenuItem("RBG0", vdp::Layer::RBG0);
                        layerMenuItem("NBG0/RBG1", vdp::Layer::NBG0_RBG1);
                        layerMenuItem("NBG1/EXBG", vdp::Layer::NBG1_EXBG);
                        layerMenuItem("NBG2", vdp::Layer::NBG2);
                        layerMenuItem("NBG3", vdp::Layer::NBG3);
                        ImGui::Unindent();

                        ImGui::Separator();
                        ImGui::BeginDisabled();
                        ImGui::TextUnformatted("VDP1");
                        ImGui::EndDisabled();
                        ImGui::MenuItem("Registers##vdp1", nullptr, &windowSet.vdp1Regs.Open);

                        ImGui::Separator();
                        ImGui::BeginDisabled();
                        ImGui::TextUnformatted("VDP2");
                        ImGui::EndDisabled();
                        ImGui::MenuItem("Registers##vdp2", nullptr, &windowSet.vdp2Regs.Open);
                        ImGui::MenuItem("Background layer parameters", nullptr, &windowSet.vdp2BGLayerParams.Open);
                        ImGui::MenuItem("Sprite layer parameters", nullptr, &windowSet.vdp2SpriteLayerParams.Open);
                        ImGui::MenuItem("Window parameters", nullptr, &windowSet.vdp2WindowParams.Open);
                        ImGui::MenuItem("Color calculation parameters", nullptr, &windowSet.vdp2ColorCalcParams.Open);
                        ImGui::MenuItem("Debug overlay", nullptr, &windowSet.vdp2DebugOverlay.Open);
                        ImGui::MenuItem("VRAM access patterns", nullptr, &windowSet.vdp2VRAMAccessPatterns.Open);
                        ImGui::MenuItem("Color RAM palette", nullptr, &windowSet.vdp2CRAM.Open);

                        ImGui::EndMenu();
                    }

                    if (ImGui::BeginMenu("CD Block")) {
                        auto &windowSet = m_windowManagerService.CDBlockWindowSet();
                        ImGui::BeginDisabled();
                        ImGui::TextUnformatted("HLE");
                        ImGui::EndDisabled();
                        ImGui::MenuItem("Command trace", nullptr, &windowSet.cmdTrace.Open);
                        ImGui::MenuItem("Filters", nullptr, &windowSet.filters.Open);
                        ImGui::MenuItem("Partitions", nullptr, &windowSet.partitions.Open);
                        ImGui::Separator();
                        ImGui::BeginDisabled();
                        ImGui::TextUnformatted("LLE");
                        ImGui::EndDisabled();
                        ImGui::MenuItem("CD drive state trace", nullptr, &windowSet.driveStateTrace.Open);
                        ImGui::MenuItem("YGR command trace", nullptr, &windowSet.ygrCmdTrace.Open);
                        ImGui::EndMenu();
                    }

                    ImGui::MenuItem("Debug output", nullptr, &m_windowManagerService.DebugOutputWindow().Open);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Help")) {
                    if (ImGui::MenuItem("Open welcome window", nullptr)) {
                        m_windowManagerService.OpenWelcomeModal(false);
                    }
                    if (ImGui::MenuItem("Check for updates", nullptr)) {
                        m_updateCheckerService.CheckForUpdates(true);
                    }

                    ImGui::Separator();
                    ImGui::MenuItem("Show message history",
                                    input::ToShortcut(inputContext, actions::general::ShowMessageHistory).c_str(),
                                    &m_windowManagerService.MessageHistoryWindow().Open);
                    ImGui::Separator();
#if Ymir_ENABLE_IMGUI_DEMO
                    ImGui::MenuItem("ImGui demo window", nullptr, &showImGuiDemoWindow);
                    ImGui::Separator();
#endif
                    ImGui::MenuItem("About", nullptr, &m_windowManagerService.AboutWindow().Open);
                    ImGui::EndMenu();
                }
                ImGui::EndMainMenuBar();
            } else {
                ImGui::PopStyleVar();
            }
        }

        {
            ImGuiViewport *viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::Begin("##dockspace_window", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoDocking |
                             ImGuiWindowFlags_NoBackground);
            ImGui::PopStyleVar(3);
        }

        ImGui::DockSpace(ImGui::GetID("##main_dockspace"), ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
        {
#if Ymir_ENABLE_IMGUI_DEMO
            // Show the big ImGui demo window if enabled
            if (showImGuiDemoWindow) {
                ImGui::ShowDemoWindow(&showImGuiDemoWindow);
            }
#endif

            /*if (ImGui::Begin("Audio buffer")) {
                ImGui::SetNextItemWidth(-1);
                ImGui::ProgressBar((float)m_context.audioSystem.GetBufferCount() /
            m_context.audioSystem.GetBufferCapacity());
            }
            ImGui::End();*/

            auto &videoSettings = settings.video;

            // Draw video output as a window
            if (videoSettings.displayVideoOutputInWindow) {
                std::string title = fmt::format("Video Output - {}x{}###Display", screen.width, screen.height);

                const bool horzDisplay = videoSettings.rotation == Settings::Video::DisplayRotation::Normal ||
                                         videoSettings.rotation == Settings::Video::DisplayRotation::_180;

                double aspectRatio = videoSettings.forceAspectRatio
                                         ? 1.0 / videoSettings.forcedAspect
                                         : (double)screen.height / screen.width * screen.scaleY / screen.scaleX;
                if (!horzDisplay) {
                    aspectRatio = 1.0 / aspectRatio;
                }

                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                ImGui::SetNextWindowSizeConstraints(
                    (horzDisplay ? ImVec2(vdp::kMinResH, vdp::kMinResV) : ImVec2(vdp::kMinResV, vdp::kMinResH)),
                    ImVec2(FLT_MAX, FLT_MAX),
                    [](ImGuiSizeCallbackData *data) {
                        double aspectRatio = *(double *)data->UserData;
                        data->DesiredSize.y =
                            (float)(int)(data->DesiredSize.x * aspectRatio) + ImGui::GetFrameHeightWithSpacing();
                    },
                    (void *)&aspectRatio);

                ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoNavInputs;
                if (m_mouseCaptureService.ShouldDisableImGuiMouseInputs()) {
                    windowFlags |= ImGuiWindowFlags_NoMouseInputs;
                }
                if (ImGui::Begin(title.c_str(), &videoSettings.displayVideoOutputInWindow, windowFlags)) {
                    const ImVec2 avail = ImGui::GetContentRegionAvail();
                    renderDispTexture(avail.x, avail.y);

                    const ImVec2 pos = ImGui::GetCursorScreenPos();
                    const auto tl = pos;
                    const auto tr = ImVec2(pos.x + avail.x, pos.y);
                    const auto br = ImVec2(pos.x + avail.x, pos.y + avail.y);
                    const auto bl = ImVec2(pos.x, pos.y + avail.y);
                    const auto uv1 = ImVec2(0, 0);
                    const auto uv2 = ImVec2((float)screen.width / vdp::kMaxResH, 0);
                    const auto uv3 = ImVec2((float)screen.width / vdp::kMaxResH, (float)screen.height / vdp::kMaxResV);
                    const auto uv4 = ImVec2(0, (float)screen.height / vdp::kMaxResV);

                    screen.scale =
                        std::min((double)avail.x / (double)screen.width, (double)avail.y / (double)screen.height);
                    screen.dCenterX = tl.x + avail.x * 0.5f;
                    screen.dCenterY = tl.y + avail.y * 0.5f;
                    screen.dSizeX = horzDisplay ? avail.x : avail.y;
                    screen.dSizeY = horzDisplay ? avail.y : avail.x;

                    // TODO: for SDL Renderer, return SDL_Texture * cast to ImTextureID
                    const ImTextureID dispTextureID = m_graphicsService.GetImGuiTextureID(dispTexture);
                    auto *drawList = ImGui::GetWindowDrawList();
                    switch (videoSettings.rotation) {
                    default: [[fallthrough]];
                    case Settings::Video::DisplayRotation::Normal:
                        drawList->AddImageQuad(dispTextureID, tl, tr, br, bl, uv1, uv2, uv3, uv4);
                        break;
                    case Settings::Video::DisplayRotation::_90CW:
                        drawList->AddImageQuad(dispTextureID, tl, tr, br, bl, uv4, uv1, uv2, uv3);
                        break;
                    case Settings::Video::DisplayRotation::_180:
                        drawList->AddImageQuad(dispTextureID, tl, tr, br, bl, uv3, uv4, uv1, uv2);
                        break;
                    case Settings::Video::DisplayRotation::_90CCW:
                        drawList->AddImageQuad(dispTextureID, tl, tr, br, bl, uv2, uv3, uv4, uv1);
                        break;
                    }

                    m_inputService.DrawInputs(drawList);

                    ImGui::Dummy(avail);

                    m_mouseCaptureService.SetMouseRect(tl.x, tl.y, br.x, br.y);
                }
                ImGui::End();
                ImGui::PopStyleVar();
            }

            // Draw input cursors on background if not displaying screen in a window
            if (!settings.video.displayVideoOutputInWindow) {
                auto *drawList = ImGui::GetBackgroundDrawList();
                m_inputService.DrawInputs(drawList);
            }

            // Draw windows and modals
            m_windowManagerService.DrawWindows();
            m_windowManagerService.DrawGenericModal();

            auto *viewport = ImGui::GetMainViewport();

            // Draw rewind buffer bar widget
            if (m_context.rewindBuffer.IsRunning()) {
                const auto now = clk::now();

                const float mousePosY = io.MousePos.y;
                const float vpBottomQuarter =
                    viewport->Pos.y +
                    std::min(viewport->Size.y * 0.75f, viewport->Size.y - 120.0f * m_context.displayScale);
                if ((mouseMoved && mousePosY >= vpBottomQuarter) || m_context.rewinding || m_context.paused) {
                    m_rewindBarFadeTimeBase = now;
                }

                // Delta time since last fade in event
                const auto delta = now - m_rewindBarFadeTimeBase;

                // TODO: make these configurable
                static constexpr auto kOpaqueTime = 2.0s; // how long to keep the bar opaque since last event
                static constexpr auto kFadeTime = 0.75s;  // how long to fade from opaque to transparent

                const double t = (delta - kOpaqueTime) / kFadeTime;
                const double alpha = std::clamp(1.0 - t, 0.0, 1.0);

                ui::widgets::RewindBar(m_context, alpha);

                // TODO: add mouse interactions
            }

            // Draw speed and mute indicators on top-right of viewport
            {
                static constexpr float kBaseSize = 50.0f;
                static constexpr float kBasePadding = 30.0f;
                static constexpr float kBaseShadowOffset = 3.0f;
                static constexpr float kBaseTextShadowOffset = 1.0f;
                static constexpr sint64 kBlinkInterval = 700;
                const float size = kBaseSize * m_context.displayScale;
                const float fontSizeMedium = m_context.fontSizes.medium * m_context.displayScale;
                const float padding = kBasePadding * m_context.displayScale;
                const float shadowOffset = kBaseShadowOffset * m_context.displayScale;
                const float textShadowOffset = kBaseTextShadowOffset * m_context.displayScale;
                ImFont *font = m_context.fonts.sansSerif.regular;
                ImGui::PushFont(font, kBaseSize);
                const ImVec2 charSize = ImGui::CalcTextSize(ICON_MS_PLAY_ARROW);
                ImGui::PopFont();

                const ImVec2 tl{viewport->WorkPos.x + viewport->WorkSize.x - padding - charSize.x,
                                viewport->WorkPos.y + padding};
                const ImVec2 br{tl.x + charSize.x, tl.y + charSize.y};

                const sint64 currMillis =
                    std::chrono::duration_cast<std::chrono::milliseconds>(clk::now().time_since_epoch()).count();
                const double phase =
                    (double)(currMillis % kBlinkInterval) / (double)kBlinkInterval * std::numbers::pi * 2.0;
                const double alpha = std::sin(phase) * 0.2 + 0.7;

                auto *drawList = ImGui::GetBackgroundDrawList();
                auto drawIndicator = [&](ImVec2 pos, double alpha, float size, const char *text) {
                    const uint32 alphaU32 = std::clamp((uint32)(alpha * 255.0), 0u, 255u);
                    const uint32 shadowAlphaU32 = std::clamp((uint32)(alpha * 0.65 * 255.0), 0u, 255u);
                    const uint32 color = 0xFFFFFF | (alphaU32 << 24u);
                    const uint32 shadowColor = 0x000000 | (shadowAlphaU32 << 24u);
                    drawList->AddText(font, size, ImVec2(pos.x + shadowOffset, pos.y + shadowOffset), shadowColor,
                                      text);
                    drawList->AddText(font, size, pos, color, text);
                };

                // Draw bounding box
                // drawList->AddRect(tl, br, 0xFFFF00FF);

                if (m_context.paused) {
                    drawIndicator(tl, alpha, size, ICON_MS_PAUSE);
                } else {
                    // Determine icon based on speed factor and direction
                    const bool rev = m_context.rewindBuffer.IsRunning() && m_context.rewinding;
                    const float speedFactor = m_context.emuSpeed.GetCurrentSpeedFactor();
                    const bool slomo = m_context.emuSpeed.limitSpeed && speedFactor < 1.0;
                    if (!m_context.emuSpeed.limitSpeed ||
                        (speedFactor != 1.0 && settings.gui.showSpeedIndicatorForAllSpeeds)) {

                        const std::string speed =
                            m_context.emuSpeed.limitSpeed
                                ? fmt::format("{:.02f}x{}", speedFactor, m_context.emuSpeed.altSpeed ? "\n(alt)" : "")
                                : "(unlimited)";

                        drawIndicator(tl, alpha, size,
                                      slomo ? (rev ? ICON_MS_ARROW_BACK_2 : ICON_MS_PLAY_ARROW)
                                            : (rev ? ICON_MS_FAST_REWIND : ICON_MS_FAST_FORWARD));

                        ImGui::PushFont(m_context.fonts.sansSerif.regular, m_context.fontSizes.medium);
                        const auto textSize = ImGui::CalcTextSize(speed.c_str());
                        ImGui::PopFont();

                        const ImVec2 textPadding = style.FramePadding;

                        const ImVec2 rectPos(tl.x + (charSize.x - textSize.x - textPadding.x * 2.0f) * 0.5f,
                                             br.y + textPadding.y);
                        const ImVec2 textPos(rectPos.x + textPadding.x, rectPos.y + textPadding.y);

                        drawList->AddText(m_context.fonts.sansSerif.regular, fontSizeMedium,
                                          ImVec2(textPos.x + textShadowOffset, textPos.y + textShadowOffset),
                                          ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.85f)), speed.c_str());
                        drawList->AddText(m_context.fonts.sansSerif.regular, fontSizeMedium, textPos,
                                          ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.00f)), speed.c_str());
                    } else if (rev) {
                        drawIndicator(tl, alpha, size, ICON_MS_ARROW_BACK_2);
                    }
                }

                // Draw sound volume/mute indicator
                {
                    static constexpr auto kDisplayDuration = 1.5s; // how long to display indicator at full alpha
                    static constexpr auto kFadeOutDuration = 0.5s; // how long to fade out
                    static constexpr auto kTotalDisplayDuration =
                        kDisplayDuration + kFadeOutDuration; // total time displaying the indicator
                    static constexpr float kIconBaseSize = 30.0f;
                    static constexpr float kIconBasePadding = 10.0f;
                    static constexpr float kVolumeBarBaseWidth = 100.0f;
                    static constexpr float kVolumeBarBaseHeight = 15.0f;
                    static constexpr float kVolumeBarYFudge = 3.0f; // compensate for Material Symbols char height
                    const float iconSize = kIconBaseSize * m_context.displayScale;
                    const float iconPadding = kIconBasePadding * m_context.displayScale;
                    const float volumeBarWidth = kVolumeBarBaseWidth * m_context.displayScale;
                    const float volumeBarHeight = kVolumeBarBaseHeight * m_context.displayScale;
                    const float volumeBarYFudge = kVolumeBarYFudge * m_context.displayScale;
                    const float gain = m_context.audioSystem.GetGain();
                    const bool mute = m_context.audioSystem.IsMute();
                    const bool forceDisplay = mute || gain == 0.0f;
                    const char *icon = mute           ? ICON_MS_VOLUME_OFF
                                       : gain == 0.0f ? ICON_MS_VOLUME_MUTE
                                       : gain < 0.6f  ? ICON_MS_VOLUME_DOWN
                                                      : ICON_MS_VOLUME_UP;
                    const auto dt = clk::now() - m_context.lastVolumeChangeTime;
                    const float baseAlpha =
                        dt <= kDisplayDuration
                            ? 1.0f
                            : std::clamp(1.0f - std::chrono::duration<float>(dt - kDisplayDuration).count() /
                                                    std::chrono::duration<float>(kFadeOutDuration).count(),
                                         0.0f, 1.0f);

                    if (forceDisplay || dt <= kTotalDisplayDuration) {
                        const float iconAlpha = (forceDisplay ? 1.0f : baseAlpha) * 0.9f;

                        // Top-right anchor point
                        const ImVec2 anchor{viewport->WorkPos.x + viewport->WorkSize.x - iconPadding,
                                            viewport->WorkPos.y + iconPadding};

                        ImGui::PushFont(font, kIconBaseSize);
                        const ImVec2 charSize = ImGui::CalcTextSize(icon);
                        ImGui::PopFont();

                        const ImVec2 tlIcon{anchor.x - charSize.x, anchor.y};
                        drawIndicator(tlIcon, iconAlpha, iconSize, icon);

                        if (dt <= kTotalDisplayDuration) {
                            const float volumeAlpha = baseAlpha * 0.9f;

                            // Volume bar
                            const float rightEdgeX = volumeBarWidth * gain;
                            const float rightEdgeY = volumeBarHeight * gain;
                            const ImVec2 p1{anchor.x - charSize.x - iconPadding - volumeBarWidth,
                                            anchor.y + (volumeBarHeight + charSize.y) * 0.5f + volumeBarYFudge};
                            const ImVec2 p2{p1.x + rightEdgeX, p1.y};
                            const ImVec2 p3{p1.x + rightEdgeX, p1.y - rightEdgeY};
                            const ImVec2 sp1{p1.x + shadowOffset, p1.y + shadowOffset};
                            const ImVec2 sp2{p2.x + shadowOffset, p2.y + shadowOffset};
                            const ImVec2 sp3{p3.x + shadowOffset, p3.y + shadowOffset};
                            drawList->AddTriangleFilled(
                                sp1, sp3, sp2, ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, volumeAlpha * 0.65f)));
                            drawList->AddTriangleFilled(p1, p3, p2,
                                                        ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, volumeAlpha)));

                            // Volume text
                            std::string volumeText = fmt::format("{}%", std::round(gain * 100.0f));
                            if (mute) {
                                volumeText = fmt::format("(Mute) {}", volumeText);
                            }
                            ImGui::PushFont(font, m_context.fontSizes.medium);
                            const ImVec2 volumeTextSize = ImGui::CalcTextSize(volumeText.c_str());
                            ImGui::PopFont();
                            const ImVec2 volumeTextPos{p1.x - iconPadding - volumeTextSize.x, p1.y - volumeTextSize.y};
                            drawIndicator(volumeTextPos, volumeAlpha, fontSizeMedium, volumeText.c_str());
                        }
                    }
                }
            }

            // Draw frame rate counters
            if (settings.gui.showFrameRateOSD) {
                std::string speedStr =
                    m_context.paused ? "paused"
                    : m_context.emuSpeed.limitSpeed
                        ? fmt::format("{:.0f}%{}", m_context.emuSpeed.GetCurrentSpeedFactor() * 100.0,
                                      m_context.emuSpeed.altSpeed ? " (alt)" : "")
                        : "unlimited";
                std::string fpsText{};
                if (m_context.paused) {
                    fpsText = fmt::format("VDP2: paused\nVDP1: paused\nVDP1: paused\nGUI: {:.0f} fps\nSpeed: {}",
                                          io.Framerate, speedStr);
                } else {
                    const double frameInterval = screen.frameInterval.count() * 0.000000001;
                    const double currSpeed = screen.lastVDP2Frames * frameInterval * 100.0;
                    fpsText =
                        fmt::format("VDP2: {} fps\nVDP1: {} fps\nVDP1: {} draws\nGUI: {:.0f} fps\nSpeed: {:.0f}% / {}",
                                    screen.lastVDP2Frames, screen.lastVDP1Frames, screen.lastVDP1DrawCalls,
                                    io.Framerate, currSpeed, speedStr);
                }

                auto *drawList = ImGui::GetBackgroundDrawList();
                bool top, left;
                switch (settings.gui.frameRateOSDPosition) {
                case Settings::GUI::FrameRateOSDPosition::TopLeft:
                    top = true;
                    left = true;
                    break;
                default: [[fallthrough]];
                case Settings::GUI::FrameRateOSDPosition::TopRight:
                    top = true;
                    left = false;
                    break;
                case Settings::GUI::FrameRateOSDPosition::BottomLeft:
                    top = false;
                    left = true;
                    break;
                case Settings::GUI::FrameRateOSDPosition::BottomRight:
                    top = false;
                    left = false;
                    break;
                }

                const ImVec2 padding = style.FramePadding;
                const ImVec2 spacing = style.ItemSpacing;

                const float anchorX =
                    left ? viewport->WorkPos.x + padding.x : viewport->WorkPos.x + viewport->WorkSize.x - padding.x;
                const float anchorY =
                    top ? viewport->WorkPos.y + padding.x : viewport->WorkPos.y + viewport->WorkSize.y - padding.x;

                const float textWrapWidth = viewport->WorkSize.x - padding.x * 4.0f;

                ImGui::PushFont(m_context.fonts.sansSerif.regular, m_context.fontSizes.small);
                const auto textSize = ImGui::CalcTextSize(fpsText.c_str(), nullptr, false, textWrapWidth);
                ImGui::PopFont();

                const ImVec2 rectPos(left ? anchorX + padding.x : anchorX - padding.x - spacing.x - textSize.x,
                                     top ? anchorY + padding.x : anchorY - padding.x - spacing.y - textSize.y);

                const ImVec2 textPos(rectPos.x + padding.x, rectPos.y + padding.y);

                drawList->AddRectFilled(
                    rectPos,
                    ImVec2(rectPos.x + textSize.x + padding.x * 2.0f, rectPos.y + textSize.y + padding.y * 2.0f),
                    ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.5f)));
                drawList->AddText(m_context.fonts.sansSerif.regular, m_context.fontSizes.small * m_context.displayScale,
                                  textPos, ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)), fpsText.c_str(), nullptr,
                                  textWrapWidth);
            }

            // Draw messages
            if (settings.gui.showMessages) {
                auto *drawList = ImGui::GetForegroundDrawList();
                float messageX = viewport->WorkPos.x + style.FramePadding.x + style.ItemSpacing.x;
                float messageY = viewport->WorkPos.y + style.FramePadding.y + style.ItemSpacing.y;

                std::unique_lock lock{m_context.locks.messages};
                const size_t count = m_context.messages.Count();
                const size_t start = count > 10 ? count - 10 : 0;
                for (size_t i = start; i < count; ++i) {
                    const Message *message = m_context.messages.Get(i);
                    assert(message != nullptr);
                    if (now >= message->timestamp + kMessageDisplayDuration + kMessageFadeOutDuration) {
                        // Message is too old; don't display
                        continue;
                    }

                    float alpha;
                    if (now >= message->timestamp + kMessageDisplayDuration) {
                        // Message is in fade-out phase
                        const auto delta = static_cast<float>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                                  now - message->timestamp - kMessageDisplayDuration)
                                                                  .count());
                        static constexpr auto length = static_cast<float>(
                            std::chrono::duration_cast<std::chrono::nanoseconds>(kMessageFadeOutDuration).count());
                        alpha = 1.0f - delta / length;
                    } else {
                        alpha = 1.0f;
                    }

                    const ImVec2 padding = style.FramePadding;
                    const ImVec2 spacing = style.ItemSpacing;

                    const float textWrapWidth = viewport->WorkSize.x - padding.x * 4.0f - spacing.x * 2.0f;

                    ImGui::PushFont(m_context.fonts.sansSerif.regular, m_context.fontSizes.large);
                    const auto textSize = ImGui::CalcTextSize(message->message.c_str(), nullptr, false, textWrapWidth);
                    ImGui::PopFont();
                    const ImVec2 textPos(messageX + padding.x, messageY + padding.y);

                    drawList->AddRectFilled(
                        ImVec2(messageX, messageY),
                        ImVec2(messageX + textSize.x + padding.x * 2.0f, messageY + textSize.y + padding.y * 2.0f),
                        ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, alpha * 0.5f)));
                    drawList->AddText(m_context.fonts.sansSerif.regular,
                                      m_context.fontSizes.large * m_context.displayScale, textPos,
                                      ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, alpha)), message->message.c_str(),
                                      nullptr, textWrapWidth);

                    messageY += textSize.y + padding.y * 2.0f;
                }
            }
        }
        ImGui::End();

        // ---------------------------------------------------------------------
        // Render window

        ImGui::Render();

        // Clear screen
        m_graphicsService.ClearScreen(clearColor);

        // Draw Saturn screen
        if (!settings.video.displayVideoOutputInWindow) {
            const auto &videoSettings = settings.video;
            const bool forceAspectRatio = videoSettings.forceAspectRatio;
            const double forcedAspect = videoSettings.forcedAspect;
            const bool aspectRatioChanged = forceAspectRatio && forcedAspect != prevForcedAspect;
            const bool forceAspectRatioChanged = prevForceAspectRatio != forceAspectRatio;
            const bool screenSizeChanged = aspectRatioChanged || forceAspectRatioChanged || screen.resolutionChanged;
            const bool fitWindowToScreen =
                (videoSettings.autoResizeWindow && screenSizeChanged) || fitWindowToScreenNow;
            const bool horzDisplay = videoSettings.rotation == Settings::Video::DisplayRotation::Normal ||
                                     videoSettings.rotation == Settings::Video::DisplayRotation::_180;

            float menuBarHeight = drawMainMenu ? ImGui::GetFrameHeight() : 0.0f;

            // Get window size
            int ww, wh;
            SDL_GetWindowSize(screen.window, &ww, &wh);

#if defined(__APPLE__)
            // Logical->Physical window-coordinate fix primarily for MacOS Retina displays
            const float pixelDensity = SDL_GetWindowPixelDensity(screen.window);
            ww *= pixelDensity;
            wh *= pixelDensity;

            menuBarHeight *= pixelDensity;
#endif

            wh -= menuBarHeight;

            double baseWidth = forceAspectRatio ? std::ceil(screen.height * screen.scaleY * forcedAspect)
                                                : screen.width * screen.scaleX;
            double baseHeight = screen.height * screen.scaleY;
            if (!horzDisplay) {
                std::swap(baseWidth, baseHeight);
            }

            double scale;
            if (forceScreenScale) {
                const bool doubleRes = screen.width >= 640 || screen.height >= 400;
                scale = doubleRes ? forcedScreenScale : forcedScreenScale * 2;
            } else {
                // Compute maximum scale to fit the display given the constraints above
                double scaleFactor = 1.0;

                const double scaleX = (double)ww / baseWidth;
                const double scaleY = (double)wh / baseHeight;
                scale = std::max(1.0, std::min(scaleX, scaleY));

                // Preserve the previous scale if the aspect ratio changed or the force option was just enabled/disabled
                // when fitting the window to the screen
                if (fitWindowToScreen) {
                    int screenWidth = screen.width;
                    int screenHeight = screen.height;
                    int screenScaleX = screen.scaleX;
                    int screenScaleY = screen.scaleY;
                    if (screen.resolutionChanged) {
                        // Handle double resolution scaling
                        const bool currDoubleRes = screen.prevWidth >= 640 || screen.prevHeight >= 400;
                        const bool nextDoubleRes = screen.width >= 640 || screen.height >= 400;
                        if (currDoubleRes != nextDoubleRes) {
                            scaleFactor = nextDoubleRes ? 0.5 : 2.0;
                        }
                        screenWidth = screen.prevWidth;
                        screenHeight = screen.prevHeight;
                        screenScaleX = screen.prevScaleX;
                        screenScaleY = screen.prevScaleY;
                    }
                    if (screenSizeChanged) {
                        double baseWidth = forceAspectRatio ? std::ceil(screenHeight * screenScaleY * prevForcedAspect)
                                                            : screenWidth * screenScaleX;
                        double baseHeight = screenHeight * screenScaleY;
                        if (!horzDisplay) {
                            std::swap(baseWidth, baseHeight);
                        }
                        const double scaleX = (double)ww / baseWidth;
                        const double scaleY = (double)wh / baseHeight;
                        scale = std::max(1.0, std::min(scaleX, scaleY));
                    }
                }
                scale *= scaleFactor;
                if (videoSettings.forceIntegerScaling) {
                    scale = floor(scale);
                }
            }
            int scaledWidth = baseWidth * scale;
            int scaledHeight = baseHeight * scale;

            // Resize window without moving the display position relative to the screen
            if (fitWindowToScreen && (ww != scaledWidth || wh != scaledHeight)) {
                int wx, wy;
                SDL_GetWindowPosition(screen.window, &wx, &wy);

                // Get window decoration borders in order to prevent moving it off the screen
                int wbt = 0;
                int wbl = 0;
                SDL_GetWindowBordersSize(screen.window, &wbt, &wbl, nullptr, nullptr);

                int dx = scaledWidth - ww;
                int dy = scaledHeight - wh;
                SDL_SetWindowSize(screen.window, scaledWidth, scaledHeight + menuBarHeight);

                int nwx = std::max(wx - dx / 2, wbt);
                int nwy = std::max(wy - dy / 2, wbl);
                SDL_SetWindowPosition(screen.window, nwx, nwy);
            }
            if (!horzDisplay) {
                std::swap(scaledWidth, scaledHeight);
            }

            // Render framebuffer to display texture
            renderDispTexture(scaledWidth, scaledHeight);

            // Determine how much slack there is on each axis in order to center the image on the window
            const int slackX = ww - scaledWidth;
            const int slackY = wh - scaledHeight;

            double rotAngle;
            switch (videoSettings.rotation) {
            default: [[fallthrough]];
            case Settings::Video::DisplayRotation::Normal: rotAngle = 0.0; break;
            case Settings::Video::DisplayRotation::_90CW: rotAngle = 90.0; break;
            case Settings::Video::DisplayRotation::_180: rotAngle = 180.0; break;
            case Settings::Video::DisplayRotation::_90CCW: rotAngle = 270.0; break;
            }

            // Draw the texture
            gfx::FRect srcRect{.x = 0.0f,
                               .y = 0.0f,
                               .w = (float)(screen.width * screen.fbScale),
                               .h = (float)(screen.height * screen.fbScale)};
            gfx::FRect dstRect{.x = floorf(slackX * 0.5f),
                               .y = floorf(slackY * 0.5f + menuBarHeight),
                               .w = (float)scaledWidth,
                               .h = (float)scaledHeight};
            m_graphicsService.DrawTextureRotated(dispTexture, srcRect, dstRect, rotAngle);
            // SDL_FRect srcRect{.x = 0.0f,
            //                   .y = 0.0f,
            //                   .w = (float)(screen.width * screen.fbScale),
            //                   .h = (float)(screen.height * screen.fbScale)};
            // SDL_FRect dstRect{.x = floorf(slackX * 0.5f),
            //                   .y = floorf(slackY * 0.5f + menuBarHeight),
            //                   .w = (float)scaledWidth,
            //                   .h = (float)scaledHeight};
            // SDL_Texture *dispTexturePtr = m_graphicsService.GetSDLTexture(dispTexture);
            // SDL_RenderTextureRotated(renderer, dispTexturePtr, &srcRect, &dstRect, rotAngle, nullptr, SDL_FLIP_NONE);

            screen.scale = scale;
            screen.dCenterX = dstRect.x + dstRect.w * 0.5f;
            screen.dCenterY = dstRect.y + dstRect.h * 0.5f;
            screen.dSizeX = dstRect.w;
            screen.dSizeY = dstRect.h;

            m_mouseCaptureService.SetMouseRect(dstRect.x, dstRect.y, dstRect.w, dstRect.h);
        }

        screen.resolutionChanged = false;

        // Render ImGui widgets
        m_graphicsService.ImGuiRenderFrame();

        {
            auto presentResult = m_graphicsService.Present();
            if (!presentResult) {
                // Revert to sane settings right now, just to be safe
                settings.video.graphicsBackend = gfx::Backend::SDLRenderer;
                settings.video.graphicsAdapter = std::nullopt;
                settings.video.useHardwareAcceleration = false;
                settings.Save();
                util::ShowFatalErrorDialog("The graphics context crashed. Cannot continue execution.\n"
                                           "Your graphics settings were reset.");
                goto end_loop;
            }
        }

        // Process ImGui INI file write requests
        // TODO: compress and include in state blob
        if (io.WantSaveIniSettings) {
            ImGui::SaveIniSettingsToDisk(imguiIniLocationStr.c_str());
            io.WantSaveIniSettings = false;
        }

        settings.CheckDirty();
        m_saveStateService.CheckDebuggerStateDirty();
        m_persistenceService.DoPendingPersistences();
        m_discordRPCService.Poll();
    }

end_loop:; // the semicolon is not a typo!

    // Everything is cleaned up automatically by ScopeGuards
}

void App::StartEmulatorThread() {
    m_emuThread = std::thread([this] { EmulatorThread(); });
}

void App::StopEmulatorThread() {
    if (!m_emuThread.joinable()) {
        return;
    }

    m_emuProcessEvent.Set();
    m_context.screen.frameRequestEvent.Set();
    m_context.audioSystem.SetSilent(true);
    m_context.EnqueueEvent(events::emu::Shutdown());

    m_emuThread.join();
}

void App::EmulatorThread() {
    auto &settings = m_settings;

    util::SetCurrentThreadName("Emulator thread");
    util::BoostCurrentThreadPriority(settings.general.boostEmuThreadPriority);

    lsn::CScopedNoSubnormals snsNoSubnormals{};

    enum class StepAction { Noop, RunFrame, FrameStep, StepMSH2, StepSSH2 };

    std::array<EmuEvent, 64> evts{};

    while (true) {
        const bool paused = m_context.paused;
        StepAction stepAction = paused ? StepAction::Noop : StepAction::RunFrame;

        // Process all pending events
        const size_t evtCount = paused ? m_context.eventQueues.emulator.wait_dequeue_bulk(evts.begin(), evts.size())
                                       : m_context.eventQueues.emulator.try_dequeue_bulk(evts.begin(), evts.size());
        for (size_t i = 0; i < evtCount; i++) {
            EmuEvent &evt = evts[i];
            using enum EmuEvent::Type;

            switch (evt.type) {
            case FactoryReset:
                m_context.saturn.instance->FactoryReset();
                m_context.DisplayMessage("Factory reset triggered");
                break;
            case HardReset:
                m_context.saturn.instance->Reset(true);
                m_context.rewindBuffer.Reset();
                m_context.DisplayMessage("Hard reset triggered");
                break;
            case SoftReset:
                m_context.saturn.instance->Reset(false);
                m_context.DisplayMessage("Soft reset triggered");
                break;
            case SetResetButton:
                m_context.saturn.instance->SMPC.SetResetButtonState(std::get<bool>(evt.value));
                if (std::get<bool>(evt.value)) {
                    m_context.DisplayMessage("Soft reset triggered");
                }
                break;

            case SetPaused: //
            {
                const bool newPaused = std::get<bool>(evt.value);
                stepAction = newPaused ? StepAction::Noop : StepAction::RunFrame;
                m_context.paused = newPaused;
                m_context.audioSystem.SetSilent(newPaused);
                if (m_context.screen.videoSync) {
                    // Avoid locking the GUI thread
                    m_context.screen.frameReadyEvent.Set();
                }
                break;
            }
            case ForwardFrameStep:
                stepAction = StepAction::FrameStep;
                m_context.paused = true;
                m_context.audioSystem.SetSilent(false);
                break;
            case ReverseFrameStep:
                stepAction = StepAction::FrameStep;
                m_context.paused = true;
                m_context.rewinding = true;
                m_context.audioSystem.SetSilent(false);
                break;
            case StepMSH2:
                stepAction = StepAction::StepMSH2;
                if (!m_context.paused) {
                    m_context.paused = true;
                    m_context.DisplayMessage("Paused due to single-stepping master SH-2");
                }
                m_context.audioSystem.SetSilent(true);
                break;
            case StepSSH2:
                stepAction = StepAction::StepSSH2;
                if (!m_context.paused) {
                    m_context.paused = true;
                    m_context.DisplayMessage("Paused due to single-stepping slave SH-2");
                }
                m_context.audioSystem.SetSilent(true);
                break;

            case OpenCloseTray:
                if (m_context.saturn.instance->IsTrayOpen()) {
                    m_context.saturn.instance->CloseTray();
                    m_context.DisplayMessage("Disc tray closed");
                } else {
                    m_context.saturn.instance->OpenTray();
                    m_context.DisplayMessage("Disc tray opened");
                }
                break;
            case LoadDisc: //
            {
                auto &path = std::get<std::filesystem::path>(evt.value);
                m_discService.LoadDiscImageAsync(path, true);
                break;
            }
            case ApplyDisc: {
                app::services::DiscService::AsyncLoadState &loadState =
                    std::get<app::services::DiscService::AsyncLoadState>(evt.value);
                if (loadState.disc) {
                    // UpdateSettingsAndContext locks the disc mutex
                    m_discService.UpdateSettingsAndContext(loadState.disc.value(), loadState.path);
                } else {
                    m_saveStateService.LoadSaveStates();
                    m_saveStateService.LoadDebuggerState();
                    auto iplLoadResult = m_romService.LoadIPLROM();
                    if (!iplLoadResult.succeeded) {
                        m_windowManagerService.OpenSimpleErrorModal(
                            fmt::format("Could not load IPL ROM: {}", iplLoadResult.errorMessage));
                    }
                }
                break;
            }
            case OpenHostDevice: //
            {
                auto &path = std::get<std::string>(evt.value);
                // LoadDiscImage locks the disc mutex
                if (m_context.saturn.instance->OpenHostCDDrive(path)) {
                    m_saveStateService.LoadSaveStates();
                    m_saveStateService.LoadDebuggerState();
                    m_context.state.loadedDiscImagePath.clear();
                    m_context.state.loadedDiscDrivePath = path;
                    auto iplLoadResult = m_romService.LoadIPLROM();
                    if (!iplLoadResult.succeeded) {
                        m_windowManagerService.OpenSimpleErrorModal(
                            fmt::format("Could not load IPL ROM: {}", iplLoadResult.errorMessage));
                    }
                    m_context.DisplayMessage(fmt::format("Drive {} opened", path));
                }
                break;
            }
            case EjectDisc: //
            {
                std::unique_lock lock{m_context.locks.disc};
                m_context.saturn.instance->EjectDisc();
                m_context.state.loadedDiscImagePath.clear();
                m_context.state.loadedDiscDrivePath.clear();
                if (settings.system.internalBackupRAMPerGame) {
                    m_context.EnqueueEvent(events::emu::LoadInternalBackupMemory());
                }
                m_context.DisplayMessage("Disc ejected");
                break;
            }
            case RemoveCartridge: //
            {
                std::unique_lock lock{m_context.locks.cart};
                m_context.saturn.instance->RemoveCartridge();
                break;
            }

            case ReplaceInternalBackupMemory: //
            {
                std::unique_lock lock{m_context.locks.backupRAM};
                m_context.saturn.instance->mem.GetInternalBackupRAM().CopyFrom(
                    std::get<ymir::bup::BackupMemory>(evt.value));
                break;
            }
            case ReplaceExternalBackupMemory:
                if (auto *cart = m_context.saturn.instance->GetCartridge().As<ymir::cart::CartType::BackupMemory>()) {
                    std::unique_lock lock{m_context.locks.backupRAM};
                    cart->CopyBackupMemoryFrom(std::get<ymir::bup::BackupMemory>(evt.value));
                }
                break;

            case RunFunction: std::get<std::function<void(SharedContext &)>>(evt.value)(m_context); break;

            case ReceiveMidiInput:
                m_context.saturn.instance->SCSP.ReceiveMidiInput(std::get<ymir::scsp::MidiMessage>(evt.value));
                break;

            case SetThreadPriority: util::BoostCurrentThreadPriority(std::get<bool>(evt.value)); break;

            case Shutdown: m_context.saturn.instance->VDP.UseNullRenderer(); return;
            }
        }

        // Emulate one frame
        switch (stepAction) {
        case StepAction::Noop: break;
        case StepAction::RunFrame: [[fallthrough]];
        case StepAction::FrameStep: //
        {
            // Synchronize with GUI thread
            if (m_context.emuSpeed.limitSpeed && m_context.screen.videoSync) {
                m_emuProcessEvent.Wait();
                m_emuProcessEvent.Reset();
            }

            const bool rewindEnabled = m_context.rewindBuffer.IsRunning();
            bool doRunFrame = true;
            if (rewindEnabled && m_context.rewinding) {
                if (m_context.rewindBuffer.PopState()) {
                    if (!m_context.saturn.instance->LoadState(m_context.rewindBuffer.NextState)) {
                        doRunFrame = false;
                    }
                } else {
                    doRunFrame = false;
                }
            }

            if (doRunFrame) [[likely]] {
                m_context.saturn.instance->RunFrame();
            }

            if (rewindEnabled && !m_context.rewinding) {
                m_context.saturn.instance->SaveState(m_context.rewindBuffer.NextState);
                m_context.rewindBuffer.ProcessState();
            }

            if (stepAction == StepAction::FrameStep) {
                m_context.rewinding = false;
                m_context.audioSystem.SetSilent(true);
            }
            break;
        }
        case StepAction::StepMSH2: //
        {
            const uint64 cycles = m_context.saturn.instance->StepMasterSH2();
            devlog::debug<grp::base>("SH2-M stepped for {} cycles", cycles);
            break;
        }
        case StepAction::StepSSH2: //
        {
            const uint64 cycles = m_context.saturn.instance->StepSlaveSH2();
            devlog::debug<grp::base>("SH2-S stepped for {} cycles", cycles);
            break;
        }
        }
    }
}

void App::EnableRewindBuffer(bool enable) {
    bool wasEnabled = m_context.rewindBuffer.IsRunning();
    if (enable != wasEnabled) {
        if (enable) {
            m_context.rewindBuffer.Start();
            m_rewindBarFadeTimeBase = clk::now();
        } else {
            m_context.rewindBuffer.Stop();
            m_context.rewindBuffer.Reset();
        }
        m_context.DisplayMessage(fmt::format("Rewind buffer {}", (enable ? "enabled" : "disabled")));
    }
}

void App::ToggleRewindBuffer() {
    auto &settings = m_settings;
    settings.general.enableRewindBuffer ^= true;
    settings.MakeDirty();
    EnableRewindBuffer(settings.general.enableRewindBuffer);
}

void App::OnMidiInputReceived(double delta, std::vector<unsigned char> *msg, void *userData) {
    App *app = static_cast<App *>(userData);
    app->m_context.EnqueueEvent(events::emu::ReceiveMidiInput(delta, std::move(*msg)));
}

} // namespace app
