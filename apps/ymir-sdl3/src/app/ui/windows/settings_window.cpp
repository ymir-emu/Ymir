#include "settings_window.hpp"

using namespace ymir;

namespace app::ui {

SettingsWindow::SettingsWindow(SharedContext &context, services::LinkCableService &linkCableService)
    : WindowBase(context)
    , m_generalSettingsView(context)
    , m_guiSettingsView(context)
    , m_hotkeysSettingsView(context)
    , m_systemSettingsView(context)
    , m_iplSettingsView(context)
    , m_inputSettingsView(context)
    , m_videoSettingsView(context)
    , m_audioSettingsView(context)
    , m_cartSettingsView(context)
    , m_cdblockSettingsView(context)
    , m_serialPortSettingsView(context, linkCableService)
    , m_tweaksSettingsView(context) {

    m_windowConfig.name = "Settings";
}

void SettingsWindow::OpenTab(SettingsTab tab) {
    Open = true;
    m_selectedTab = tab;
    RequestFocus();
}

void SettingsWindow::EnableLocalAutoLink() {
    m_serialPortSettingsView.EnableLocalAuto();
}

void SettingsWindow::PrepareWindow() {
    auto *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));

    const ImVec2 minSize(500 * m_context.displayScale, 300 * m_context.displayScale);
    const ImVec2 maxSize(1000 * m_context.displayScale, 900 * m_context.displayScale);
    ImGui::SetNextWindowSizeConstraints(minSize, maxSize);
}

void SettingsWindow::DrawContents() {
    auto tabFlag = [&](SettingsTab tab) {
        if (m_selectedTab == tab) {
            return ImGuiTabItemFlags_SetSelected;
        } else {
            return ImGuiTabItemFlags_None;
        }
    };

    m_windowConfig.allowClosingWithGamepad = true;
    if (ImGui::BeginTabBar("settings_tabs")) {
        if (ImGui::BeginTabItem("General", nullptr, tabFlag(SettingsTab::General))) {
            m_generalSettingsView.Display();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("GUI", nullptr, tabFlag(SettingsTab::GUI))) {
            m_guiSettingsView.Display();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Hotkeys", nullptr, tabFlag(SettingsTab::Hotkeys))) {
            m_hotkeysSettingsView.Display();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("System", nullptr, tabFlag(SettingsTab::System))) {
            m_systemSettingsView.Display();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("IPL (BIOS)", nullptr, tabFlag(SettingsTab::IPL))) {
            m_iplSettingsView.Display();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Input", nullptr, tabFlag(SettingsTab::Input))) {
            // m_windowConfig.allowClosingWithGamepad = false;
            m_inputSettingsView.Display();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Video", nullptr, tabFlag(SettingsTab::Video))) {
            m_videoSettingsView.Display();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Audio", nullptr, tabFlag(SettingsTab::Audio))) {
            m_audioSettingsView.Display();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Cartridge", nullptr, tabFlag(SettingsTab::Cartridge))) {
            m_cartSettingsView.Display();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("CD Block", nullptr, tabFlag(SettingsTab::CDBlock))) {
            m_cdblockSettingsView.Display();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Serial Port", nullptr, tabFlag(SettingsTab::SerialPort))) {
            m_serialPortSettingsView.Display();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Tweaks", nullptr, tabFlag(SettingsTab::Tweaks))) {
            m_tweaksSettingsView.Display();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    m_selectedTab = SettingsTab::None;
}

} // namespace app::ui
