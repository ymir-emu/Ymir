#pragma once

#include "cmdline_opts.hpp"

#include "settings.hpp"
#include "shared_context.hpp"

#include "services/disc_service.hpp"
#include "services/discord_rpc_service.hpp"
#include "services/display_service.hpp"
#include "services/file_dialog_service.hpp"
#include "services/graphics_service.hpp"
#include "services/input_service.hpp"
#include "services/midi_service.hpp"
#include "services/link_cable_service.hpp"
#include "services/mouse_capture_service.hpp"
#include "services/persistence_service.hpp"
#include "services/rom_service.hpp"
#include "services/save_state_service.hpp"
#include "services/screenshot_service.hpp"
#include "services/update_checker_service.hpp"
#include "services/window_manager_service.hpp"

#include <chrono>
#include <thread>
#include <vector>

namespace app {

class App {
public:
    App();
    ~App();

    int Run(const CommandLineOptions &options);

private:
    CommandLineOptions m_options;

    SharedContext m_context;
    services::GraphicsService m_graphicsService;
    services::SaveStateService m_saveStateService;
    services::MIDIService m_midiService;
    services::LinkCableService m_linkCableService;
    services::ScreenshotService m_screenshotService;
    services::UpdateCheckerService m_updateCheckerService;
    Settings m_settings;
    services::DiscordRPCService m_discordRPCService;
    services::MouseCaptureService m_mouseCaptureService;
    services::ROMService m_romService;
    services::DiscService m_discService;
    services::DisplayService m_displayService;
    services::FileDialogService m_fileDialogService;
    services::WindowManagerService m_windowManagerService;
    services::InputService m_inputService;
    services::PersistenceService m_persistenceService;

    std::thread m_emuThread;
    util::Event m_emuProcessEvent{};

    std::chrono::steady_clock::time_point m_mouseHideTime;

    void RunEmulator();

    void StartEmulatorThread();
    void StopEmulatorThread();
    void EmulatorThread();

    void EnableRewindBuffer(bool enable);
    void ToggleRewindBuffer();

    static void OnMidiInputReceived(double delta, std::vector<unsigned char> *msg, void *userData);

    // Rewind bar
    std::chrono::steady_clock::time_point m_rewindBarFadeTimeBase;
};

} // namespace app
