#include "gui_settings_view.hpp"

#include <app/ui/i18n.hpp>
#include <app/ui/widgets/common_widgets.hpp>

#include <app/imgui_data.hpp>

#include <ymir/sys/clocks.hpp>

namespace app::ui {


GUISettingsView::GUISettingsView(SharedContext &context)
    : SettingsViewBase(context) {}

void GUISettingsView::Display() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    auto &settings = GetSettings();
    auto &guiSettings = settings.gui;

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.large);
    ImGui::SeparatorText(_("General"));
    ImGui::PopFont();

    MakeDirty(ImGui::Checkbox(_("Show game name on window title bar"), &guiSettings.showGameNameOnTitleBar));
    MakeDirty(
        ImGui::Checkbox(_("Show performance indicator on window title bar"), &guiSettings.showPerformanceOnTitleBar));
    widgets::ExplanationTooltip("Display emulation speed, VDP2, VDP1 and GUI frame rates on title bar");

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.large);
    ImGui::SeparatorText(_("UI scaling"));
    ImGui::PopFont();

    // Round scale to steps of 25% and clamp to 100%-200% range
    bool overrideUIScale = guiSettings.overrideUIScale;
    double uiScale = overrideUIScale ? guiSettings.uiScale.Get() : imguiData->displayScale;
    uiScale = std::round(uiScale / 0.25) * 0.25;
    uiScale = std::clamp(uiScale, 1.00, 2.00);

    if (MakeDirty(ImGui::Checkbox(fmt::format(fmt::runtime(_("Override UI scale (current: {:.0f}%)")), uiScale * 100.0).c_str(),
                                  &overrideUIScale))) {
        guiSettings.overrideUIScale = overrideUIScale;
        // Use current DPI setting when enabling the override
        if (overrideUIScale) {
            guiSettings.uiScale = uiScale;
        }
    }

    ImGui::Indent();
    if (!overrideUIScale) {
        ImGui::BeginDisabled();
    }
    if (MakeDirty(ImGui::RadioButton("100%##ui_scale", uiScale == 1.0))) {
        guiSettings.uiScale = 1.00;
    }
    ImGui::SameLine();
    if (MakeDirty(ImGui::RadioButton("125%##ui_scale", uiScale == 1.25))) {
        guiSettings.uiScale = 1.25;
    }
    ImGui::SameLine();
    if (MakeDirty(ImGui::RadioButton("150%##ui_scale", uiScale == 1.50))) {
        guiSettings.uiScale = 1.50;
    }
    ImGui::SameLine();
    if (MakeDirty(ImGui::RadioButton("175%##ui_scale", uiScale == 1.75))) {
        guiSettings.uiScale = 1.75;
    }
    ImGui::SameLine();
    if (MakeDirty(ImGui::RadioButton("200%##ui_scale", uiScale == 2.00))) {
        guiSettings.uiScale = 2.00;
    }
    if (!overrideUIScale) {
        ImGui::EndDisabled();
    }
    ImGui::Unindent();

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.large);
    ImGui::SeparatorText(_("Behavior"));
    ImGui::PopFont();

    MakeDirty(ImGui::Checkbox(_("Remember window geometry"), &guiSettings.rememberWindowGeometry));
    widgets::ExplanationTooltip("When enabled, the current window position and size will be restored the next time the "
                                "application is started.");

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.large);
    ImGui::SeparatorText(_("On-screen display"));
    ImGui::PopFont();

    MakeDirty(ImGui::Checkbox(_("Show messages"), &guiSettings.showMessages));
    widgets::ExplanationTooltip(
        "When enabled, notification messages are displayed on the top-left corner of the window.");

    const bool isPAL = settings.system.videoStandard.Get() == ymir::core::config::sys::VideoStandard::PAL;

    MakeDirty(ImGui::Checkbox(_("Show frame rate"), &guiSettings.showFrameRateOSD));
    widgets::ExplanationTooltip(
        fmt::format(
            "Displays a small overlay with the VDP2, VDP1 and GUI frame rates, and the target emulation speed.\n"
            "\n"
            "- VDP2 frame rate indicates the emulator's overall speed. If it is below 60 or 50 fps (for NTSC or PAL "
            "respectively) while emulating at 100% speed, your system isn't keeping up. (The current video standard "
            "setting is {0}, so the target frame rate is {1:.0f}.)\n"
            "- VDP1 frame rate may vary depending on the game - a half or a third of the VDP2 frame rate are common "
            "ratios. It may be zero or even go higher than {1:.0f} fps.\n"
            "- GUI frame rate indicates how fast the user interface is refreshing. It should match your monitor's "
            "refresh rate or the VDP2 target frame rate. If video sync is enabled, GUI updates are paced to ensure a "
            "smooth experience on capable machines with variable refresh rate displays.\n"
            "- Speed indicates the (adjustable) target emulation speed. 100% is realtime speed.",
            (isPAL ? "PAL" : "NTSC"), (isPAL ? ymir::sys::kPALFrameRate : ymir::sys::kNTSCFrameRate))
            .c_str());
    ImGui::Indent();
    auto frameRateOSDOption = [&](const char *name, Settings::GUI::FrameRateOSDPosition value) {
        if (MakeDirty(ImGui::RadioButton(name, guiSettings.frameRateOSDPosition == value))) {
            guiSettings.frameRateOSDPosition = value;
        }
    };
    frameRateOSDOption(TR_ID("Top left", "fps_osd"), Settings::GUI::FrameRateOSDPosition::TopLeft);
    ImGui::SameLine();
    frameRateOSDOption(TR_ID("Top right", "fps_osd"), Settings::GUI::FrameRateOSDPosition::TopRight);
    ImGui::SameLine();
    frameRateOSDOption(TR_ID("Bottom left", "fps_osd"), Settings::GUI::FrameRateOSDPosition::BottomLeft);
    ImGui::SameLine();
    frameRateOSDOption(TR_ID("Bottom right", "fps_osd"), Settings::GUI::FrameRateOSDPosition::BottomRight);
    ImGui::Unindent();

    MakeDirty(
        ImGui::Checkbox(_("Show speed indicators for modified speeds"), &guiSettings.showSpeedIndicatorForAllSpeeds));
    widgets::ExplanationTooltip(
        "When enabled, the speed indicator will be displayed for any emulation speed other than 100%.\n"
        "When disabled, the speed indicator is only displayed while running in turbo speed.\n"
        "The speed indicator is always shown while paused or rewinding.");
}

} // namespace app::ui
