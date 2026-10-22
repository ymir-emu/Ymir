#include "update_onboarding_window.hpp"

#include <app/settings.hpp>

#include <app/ui/i18n.hpp>

#include <app/ui/widgets/common_widgets.hpp>

#include <app/events/gui_event_factory.hpp>

#include <app/imgui_data.hpp>

#include <util/os_features.hpp>

#include <ymir/version.hpp>

#include <fstream>

namespace app::ui {


UpdateOnboardingWindow::UpdateOnboardingWindow(SharedContext &context)
    : WindowBase(context) {

    m_windowConfig.name = _("Automatic update checks");
    m_windowConfig.flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse;
}

void UpdateOnboardingWindow::PrepareWindow() {
    auto *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
}

void UpdateOnboardingWindow::DrawContents() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    ImGui::PushTextWrapPos(450.0f * imguiData->displayScale);

    ImGui::TextUnformatted(_("Ymir can check for new versions automatically on startup."));
    ImGui::TextUnformatted(_("This requires an Internet connection and will reach github.com to check for new versions."));
    ImGui::TextUnformatted(_("Please make your choices below:"));
    ImGui::Checkbox(_("Check for updates on startup"), &m_checkForUpdates);
    widgets::ExplanationTooltip(
        _("Ymir will check for updates whenever it is launched, and notify you if a new version is available.\n"
           "Upon accepting, Ymir will immediately check for updates if this option is enabled."));
    ImGui::Checkbox(_("Update to nightly builds"), &m_includeNightlyBuilds);
    widgets::ExplanationTooltip(
        _("Whenever Ymir checks for updates, it will also consider nightly builds.\n"
           "Nightly builds include the latest features and bug fixes, but are work-in-progress and may contain bugs"));

    ImGui::NewLine();
    ImGui::TextUnformatted(_("Choose Accept to apply these settings or Decide later to close this window now.\n"
                              "If you choose to decide later, this popup will appear again on next startup."));

    ImGui::Separator();
    if (ImGui::Button(_("Accept"))) {
        const auto updatesPath = m_context.profile.GetPath(ProfilePath::PersistentState) / "updates";
        const auto onboardedPath = updatesPath / ".onboarded";
        std::filesystem::create_directories(updatesPath);
        std::ofstream{onboardedPath};

        util::os::SetFileHidden(onboardedPath, true);

        auto &settings = m_context.serviceLocator.GetRequired<Settings>();
        settings.general.checkForUpdates = m_checkForUpdates;
        settings.general.includeNightlyBuilds = m_includeNightlyBuilds;
        if (m_checkForUpdates) {
            m_context.EnqueueEvent(events::gui::CheckForUpdates());
        }
        Open = false;
    }
    ImGui::SameLine();
    if (ImGui::Button(_("Decide later"))) {
        Open = false;
    }

    ImGui::PopTextWrapPos();
}

} // namespace app::ui
