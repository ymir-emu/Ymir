#pragma once

#include <app/settings.hpp>
#include <app/shared_context.hpp>
#include <functional>
#include <string>
#include <vector>

#include <app/ui/windows/about_window.hpp>
#include <app/ui/windows/backup_ram_manager_window.hpp>
#include <app/ui/windows/message_history_window.hpp>
#include <app/ui/windows/peripheral_config_window.hpp>
#include <app/ui/windows/settings_window.hpp>
#include <app/ui/windows/system_state_window.hpp>
#include <app/ui/windows/update_onboarding_window.hpp>
#include <app/ui/windows/update_window.hpp>

#include <app/ui/windows/debug/cdblock_window_set.hpp>
#include <app/ui/windows/debug/debug_output_window.hpp>
#include <app/ui/windows/debug/memory_viewer_window.hpp>
#include <app/ui/windows/debug/scsp_window_set.hpp>
#include <app/ui/windows/debug/scu_window_set.hpp>
#include <app/ui/windows/debug/sh2_window_set.hpp>
#include <app/ui/windows/debug/vdp_window_set.hpp>

namespace app::services {

class LinkCableService;

/// @brief Manages settings, debugger windows, and modal popups.
class WindowManagerService {
public:
    explicit WindowManagerService(SharedContext &context, Settings &settings, LinkCableService &linkCableService);
    ~WindowManagerService() = default;

    WindowManagerService(const WindowManagerService &) = delete;
    WindowManagerService &operator=(const WindowManagerService &) = delete;

    /// @brief Renders all active application windows.
    void DrawWindows();

    /// @brief Renders the generic modal dialog if active.
    void DrawGenericModal();

    /// @brief Opens a simple modal showing an error message.
    /// @param[in] message Error message to display.
    void OpenSimpleErrorModal(std::string message);

    /// @brief Opens a customizable modal dialog.
    /// @param[in] title Modal title.
    /// @param[in] fnContents Lambda containing the ImGui drawing code for the modal content.
    /// @param[in] showOKButton True to show an OK button at the bottom of the modal.
    void OpenGenericModal(std::string title, std::function<void()> fnContents, bool showOKButton = true);

    /// @brief Opens the welcome onboarding modal.
    /// @param[in] scanIPLROMs True if the app should scan for IPL ROMs when closed.
    void OpenWelcomeModal(bool scanIPLROMs);

    /// @brief Opens a new memory viewer window.
    void OpenMemoryViewer();

    /// @brief Opens the key rebinding editor modal for a controller port/slot.
    /// @param[in] portIndex Port index.
    /// @param[in] slotIndex Peripheral slot index.
    void OpenPeripheralBindsEditor(uint32 portIndex, uint32 slotIndex);

    /// @brief Closes the currently active generic modal.
    void CloseGenericModal();

    // Windows (used in menu bar bindings / callbacks)
    ui::SystemStateWindow &SystemStateWindow() {
        return m_systemStateWindow;
    }
    ui::BackupMemoryManagerWindow &BackupMemoryManagerWindow() {
        return m_bupMgrWindow;
    }
    ui::SH2WindowSet &MasterSH2WindowSet() {
        return m_masterSH2WindowSet;
    }
    ui::SH2WindowSet &SlaveSH2WindowSet() {
        return m_slaveSH2WindowSet;
    }
    ui::SCUWindowSet &SCUWindowSet() {
        return m_scuWindowSet;
    }
    ui::SCSPWindowSet &SCSPWindowSet() {
        return m_scspWindowSet;
    }
    ui::VDPWindowSet &VDPWindowSet() {
        return m_vdpWindowSet;
    }
    ui::CDBlockWindowSet &CDBlockWindowSet() {
        return m_cdblockWindowSet;
    }
    ui::DebugOutputWindow &DebugOutputWindow() {
        return m_debugOutputWindow;
    }
    std::vector<ui::MemoryViewerWindow> &MemoryViewerWindows() {
        return m_memoryViewerWindows;
    }
    ui::SettingsWindow &SettingsWindow() {
        return m_settingsWindow;
    }
    ui::PeripheralConfigWindow &PeripheralConfigWindow() {
        return m_periphConfigWindow;
    }
    ui::MessageHistoryWindow &MessageHistoryWindow() {
        return m_messageHistoryWindow;
    }
    ui::AboutWindow &AboutWindow() {
        return m_aboutWindow;
    }
    ui::UpdateOnboardingWindow &UpdateOnboardingWindow() {
        return m_updateOnboardingWindow;
    }
    ui::UpdateWindow &UpdateWindow() {
        return m_updateWindow;
    }

    bool &OpenGenericModalFlag() {
        return m_openGenericModal;
    }
    bool &CloseGenericModalFlag() {
        return m_closeGenericModal;
    }
    bool &ShowOkButtonInGenericModal() {
        return m_showOkButtonInGenericModal;
    }
    std::string &GenericModalTitle() {
        return m_genericModalTitle;
    }
    std::function<void()> &GenericModalContents() {
        return m_genericModalContents;
    }

private:
    SharedContext &m_context;
    Settings &m_settings;

    ui::SystemStateWindow m_systemStateWindow;
    ui::BackupMemoryManagerWindow m_bupMgrWindow;

    ui::SH2WindowSet m_masterSH2WindowSet;
    ui::SH2WindowSet m_slaveSH2WindowSet;
    ui::SCUWindowSet m_scuWindowSet;
    ui::SCSPWindowSet m_scspWindowSet;
    ui::VDPWindowSet m_vdpWindowSet;
    ui::CDBlockWindowSet m_cdblockWindowSet;

    ui::DebugOutputWindow m_debugOutputWindow;

    std::vector<ui::MemoryViewerWindow> m_memoryViewerWindows;

    ui::SettingsWindow m_settingsWindow;
    ui::PeripheralConfigWindow m_periphConfigWindow;
    ui::MessageHistoryWindow m_messageHistoryWindow;
    ui::AboutWindow m_aboutWindow;
    ui::UpdateOnboardingWindow m_updateOnboardingWindow;
    ui::UpdateWindow m_updateWindow;

    bool m_openGenericModal = false;
    bool m_closeGenericModal = false;
    bool m_showOkButtonInGenericModal = true;
    std::string m_genericModalTitle = "Message";
    std::function<void()> m_genericModalContents;
};

} // namespace app::services
