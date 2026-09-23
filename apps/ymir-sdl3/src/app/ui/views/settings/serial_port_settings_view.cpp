#include "serial_port_settings_view.hpp"

#include <algorithm>
#include <imgui.h>

namespace app::ui {

SerialPortSettingsView::SerialPortSettingsView(SharedContext &context, services::LinkCableService &linkCableService)
    : SettingsViewBase(context)
    , m_linkCableService(linkCableService) {}

void SerialPortSettingsView::EnableLocalAuto() {
    m_enabled = true;
    m_mode = 0;
    m_linkCableService.AutoConnectLocal(static_cast<uint16>(m_port));
}

void SerialPortSettingsView::Display() {
    ImGui::TextWrapped("Connect two Ymir instances before entering a link battle. "
                       "On one PC, both instances can pair automatically.");
    ImGui::SeparatorText("Connection");

    ImGui::BeginDisabled(m_enabled);
    ImGui::Combo("Mode", &m_mode, "Same PC (automatic)\0LAN host (listen)\0LAN client (connect)\0");
    if (m_mode == 2) {
        ImGui::InputText("Host IPv4 address", m_host, sizeof(m_host));
    }
    ImGui::InputInt("TCP port", &m_port);
    m_port = std::clamp(m_port, 1, 65535);
    ImGui::EndDisabled();

    if (ImGui::Checkbox("Enable Battle (Taisen) Cable", &m_enabled)) {
        if (m_enabled) {
            switch (m_mode) {
            case 0: m_linkCableService.AutoConnectLocal(static_cast<uint16>(m_port)); break;
            case 1: m_linkCableService.Listen(static_cast<uint16>(m_port)); break;
            case 2: m_linkCableService.Connect(m_host, static_cast<uint16>(m_port)); break;
            }
        } else {
            m_linkCableService.Disconnect();
            m_context.DisplayMessage("Battle (Taisen) Cable disconnected");
        }
    }

    const auto state = m_linkCableService.GetState();
    const char *status = "Disconnected";
    switch (state) {
    case services::LinkCableService::State::Disconnected: break;
    case services::LinkCableService::State::Listening: status = "Listening"; break;
    case services::LinkCableService::State::Connecting: status = "Connecting"; break;
    case services::LinkCableService::State::Connected: status = "Connected"; break;
    case services::LinkCableService::State::Error: status = "Error"; break;
    }
    ImGui::Text("Status: %s", status);
    if (state == services::LinkCableService::State::Error) {
        ImGui::TextWrapped("%s", m_linkCableService.GetError().c_str());
    }
    ImGui::TextUnformatted("Serial port: slave SH-2 SCI (Daytona USA Circuit Edition)");
}

} // namespace app::ui
