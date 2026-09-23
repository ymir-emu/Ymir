#pragma once

#include "settings_view_base.hpp"

#include <app/services/link_cable_service.hpp>

namespace app::ui {

class SerialPortSettingsView : public SettingsViewBase {
public:
    SerialPortSettingsView(SharedContext &context, services::LinkCableService &linkCableService);

    void Display();
    void EnableLocalAuto();

private:
    services::LinkCableService &m_linkCableService;
    bool m_enabled = false;
    int m_mode = 0; // 0: same-PC auto, 1: LAN host, 2: LAN client
    char m_host[128] = "127.0.0.1";
    int m_port = 32456;
};

} // namespace app::ui
