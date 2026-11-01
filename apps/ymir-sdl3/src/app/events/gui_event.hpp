#pragma once

#include <app/input/input_action.hpp>
#include <app/input/input_events.hpp>
#include <app/services/gfx/gfx_types.hpp>
#include <app/ui/defs/settings_defs.hpp>

#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace app {

// A filter for the file dialog.
// Follows SDL3 rules:
// - filters must be specified
// - filters are a list of file extensions, separated by semicolons (e.g. "bmp;jpg;png")
// - use "*" to match all files
struct FileDialogFilter {
    const char *name;
    const char *filters;
};

// Parameters for open/save files dialogs.
struct FileDialogParams {
    std::string dialogTitle;
    std::filesystem::path defaultPath;
    std::vector<FileDialogFilter> filters;
    void *userdata;
    void (*callback)(void *userdata, const char *const *filelist, int filter);
};

// Parameters for select folder dialogs.
struct FolderDialogParams {
    std::string dialogTitle;
    std::filesystem::path defaultPath;
    void *userdata;
    void (*callback)(void *userdata, const char *const *filelist, int filter);
};

// Parameters for opening peripheral binds configuration windows.
struct PeripheralBindsParams {
    uint32 portIndex;
    uint32 slotIndex;
};

// Parameters for opening an SH-2 debugger window.
struct OpenSH2DebuggerWindowParams {
    bool master; // true=MSH2, false=SSH2
    bool triggeredByEvent;
};

struct GraphicsBackendParams {
    gfx::Backend backend;
    std::optional<gfx::AdapterID> adapter;
};

struct GUIEvent {
    enum class Type {
        LoadDisc,
        LoadRecommendedGameCartridge,
        OpenBackupMemoryCartFileDialog,
        OpenROMCartFileDialog,
        OpenPeripheralBindsEditor,

        OpenFile,      // Invoke generic open single file dialog; uses FileDialogParams
        OpenManyFiles, // Invoke generic open multiple files dialog; uses FileDialogParams
        SaveFile,      // Invoke generic save file dialog; uses FileDialogParams
        SelectFolder,  // Invoke generic select folder dialog; uses FolderDialogParams

        OpenBackupMemoryManager,
        OpenSettings,             // Opens a specific Settings tab; uses ui::SettingsTab
        OpenSH2DebuggerWindow,    // Opens an SH-2 debugger window; uses OpenSH2DebuggerWindowParams
        OpenSH2BreakpointsWindow, // Opens an SH-2 breakpoints window; uses bool (true=MSH2, false=SSH2)
        OpenSH2WatchpointsWindow, // Opens an SH-2 watchpoints window; uses bool (true=MSH2, false=SSH2)

        SetProcessPriority,    // Uses bool
        SwitchGraphicsBackend, // Uses GraphicsBackendParams

        FitWindowToScreen,
        ApplyFullscreenMode,

        RebindInputs,
        ReloadGameControllerDatabase,

        ShowErrorMessage,

        EnableRewindBuffer,

        TryLoadIPLROM,
        ReloadIPLROM,
        IPLROMLoaded,
        TryLoadCDBlockROM,
        ReloadCDBlockROM,

        CheckForUpdates,

        TakeScreenshot,

        // Emulator notifications

        StateLoaded, // A save state slot was just loaded
        StateSaved,  // A save state slot was just saved
    };

    Type type;
    std::variant<std::monostate, bool, uint32, std::string, std::filesystem::path, PeripheralBindsParams,
                 FileDialogParams, FolderDialogParams, OpenSH2DebuggerWindowParams, ui::SettingsTab,
                 GraphicsBackendParams>
        value;
};

constexpr std::string_view GUIEventToString(GUIEvent::Type type) {
    switch (type) {
    case GUIEvent::Type::LoadDisc: return "LoadDisc";
    case GUIEvent::Type::LoadRecommendedGameCartridge: return "LoadRecommendedGameCartridge";
    case GUIEvent::Type::OpenBackupMemoryCartFileDialog: return "OpenBackupMemoryCartFileDialog";
    case GUIEvent::Type::OpenROMCartFileDialog: return "OpenROMCartFileDialog";
    case GUIEvent::Type::OpenPeripheralBindsEditor: return "OpenPeripheralBindsEditor";
    case GUIEvent::Type::OpenFile: return "OpenFile";
    case GUIEvent::Type::OpenManyFiles: return "OpenManyFiles";
    case GUIEvent::Type::SaveFile: return "SaveFile";
    case GUIEvent::Type::SelectFolder: return "SelectFolder";
    case GUIEvent::Type::OpenBackupMemoryManager: return "OpenBackupMemoryManager";
    case GUIEvent::Type::OpenSettings: return "OpenSettings";
    case GUIEvent::Type::OpenSH2DebuggerWindow: return "OpenSH2DebuggerWindow";
    case GUIEvent::Type::OpenSH2BreakpointsWindow: return "OpenSH2BreakpointsWindow";
    case GUIEvent::Type::OpenSH2WatchpointsWindow: return "OpenSH2WatchpointsWindow";
    case GUIEvent::Type::SetProcessPriority: return "SetProcessPriority";
    case GUIEvent::Type::SwitchGraphicsBackend: return "SwitchGraphicsBackend";
    case GUIEvent::Type::FitWindowToScreen: return "FitWindowToScreen";
    case GUIEvent::Type::ApplyFullscreenMode: return "ApplyFullscreenMode";
    case GUIEvent::Type::RebindInputs: return "RebindInputs";
    case GUIEvent::Type::ReloadGameControllerDatabase: return "ReloadGameControllerDatabase";
    case GUIEvent::Type::ShowErrorMessage: return "ShowErrorMessage";
    case GUIEvent::Type::EnableRewindBuffer: return "EnableRewindBuffer";
    case GUIEvent::Type::TryLoadIPLROM: return "TryLoadIPLROM";
    case GUIEvent::Type::ReloadIPLROM: return "ReloadIPLROM";
    case GUIEvent::Type::IPLROMLoaded: return "IPLROMLoaded";
    case GUIEvent::Type::TryLoadCDBlockROM: return "TryLoadCDBlockROM";
    case GUIEvent::Type::ReloadCDBlockROM: return "ReloadCDBlockROM";
    case GUIEvent::Type::CheckForUpdates: return "CheckForUpdates";
    case GUIEvent::Type::TakeScreenshot: return "TakeScreenshot";
    case GUIEvent::Type::StateLoaded: return "StateLoaded";
    case GUIEvent::Type::StateSaved: return "StateSaved";
    default: return "Unknown";
    }
}

inline std::ostream &operator<<(std::ostream &os, GUIEvent::Type type) {
    return os << GUIEventToString(type);
}

inline std::ostream &operator<<(std::ostream &os, const GUIEvent &event) {
    return os << "GUIEvent{ type: " << event.type << " }";
}

} // namespace app
