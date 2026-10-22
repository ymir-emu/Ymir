#include "settings_window.hpp"

#include <app/imgui_data.hpp>

#include <app/ui/i18n.hpp>

using namespace ymir;

namespace app::ui {


SettingsWindow::SettingsWindow(SharedContext &context)
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
    , m_tweaksSettingsView(context) {

    m_windowConfig.name = _("Settings");
}

void SettingsWindow::OpenTab(SettingsTab tab) {
    Open = true;
    m_selectedTab = tab;
    RequestFocus();
}

void SettingsWindow::PrepareWindow() {
    auto *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));

    const YmirImGuiData *imguiData = GetYmirImGuiData();
    const ImVec2 minSize(500 * imguiData->displayScale, 300 * imguiData->displayScale);
    const ImVec2 maxSize(1000 * imguiData->displayScale, 900 * imguiData->displayScale);
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
        if (ImGui::BeginTabItem(_("General"), nullptr, tabFlag(SettingsTab::General))) {
            m_generalSettingsView.Display();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(_("GUI"), nullptr, tabFlag(SettingsTab::GUI))) {
            m_guiSettingsView.Display();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(_("Hotkeys"), nullptr, tabFlag(SettingsTab::Hotkeys))) {
            m_hotkeysSettingsView.Display();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(_("System"), nullptr, tabFlag(SettingsTab::System))) {
            m_systemSettingsView.Display();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(_("IPL (BIOS)"), nullptr, tabFlag(SettingsTab::IPL))) {
            m_iplSettingsView.Display();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(_("Input"), nullptr, tabFlag(SettingsTab::Input))) {
            // m_windowConfig.allowClosingWithGamepad = false;
            m_inputSettingsView.Display();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(_("Video"), nullptr, tabFlag(SettingsTab::Video))) {
            m_videoSettingsView.Display();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(_("Audio"), nullptr, tabFlag(SettingsTab::Audio))) {
            m_audioSettingsView.Display();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(_("Cartridge"), nullptr, tabFlag(SettingsTab::Cartridge))) {
            m_cartSettingsView.Display();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(_("CD Block"), nullptr, tabFlag(SettingsTab::CDBlock))) {
            m_cdblockSettingsView.Display();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(_("Tweaks"), nullptr, tabFlag(SettingsTab::Tweaks))) {
            m_tweaksSettingsView.Display();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    m_selectedTab = SettingsTab::None;
}

} // namespace app::ui
