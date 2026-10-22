#include "update_window.hpp"

#include <app/ui/i18n.hpp>

#include <ymir/version.hpp>

namespace app::ui {


UpdateWindow::UpdateWindow(SharedContext &context)
    : WindowBase(context) {

    m_windowConfig.name = _("Update available");
    m_windowConfig.flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse;
}

void UpdateWindow::PrepareWindow() {
    // Close window if no updates are actually available
    {
        std::unique_lock lock{m_context.locks.targetUpdate};
        if (!m_context.targetUpdate) {
            Open = false;
            return;
        }
    }

    auto *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
}

void UpdateWindow::DrawContents() {
    ImGui::TextUnformatted(_("A new version of Ymir is available."));
    ImGui::Text("%s%s", _("Current version: "), Ymir_VERSION);
    ImGui::TextUnformatted(_("New version: "));
    ImGui::SameLine(0, 0);
    {
        std::unique_lock lock{m_context.locks.targetUpdate};
        if (m_context.targetUpdate) {
            auto &info = m_context.targetUpdate->info;
            ImGui::TextLinkOpenURL(info.version.to_string().c_str(), info.downloadURL.c_str());
            ImGui::TextLinkOpenURL(_("Release notes"), info.releaseNotesURL.c_str());
        } else {
            ImGui::TextUnformatted(_("Retrieving..."));
        }
    }

    ImGui::Separator();
    if (ImGui::Button(_("Close"))) {
        Open = false;
    }
}

} // namespace app::ui
