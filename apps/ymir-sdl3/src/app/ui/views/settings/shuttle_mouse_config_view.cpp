#include "shuttle_mouse_config_view.hpp"

#include <app/imgui_data.hpp>
#include <app/ui/i18n.hpp>

namespace app::ui {


ShuttleMouseConfigView::ShuttleMouseConfigView(SharedContext &context)
    : SettingsViewBase(context)
    , m_inputCaptureWidget(context, m_unboundActionsWidget)
    , m_unboundActionsWidget(context) {}

void ShuttleMouseConfigView::Display(Settings::Input::Port::ShuttleMouse &controllerSettings, uint32 portIndex) {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    auto &settings = GetSettings();
    auto &binds = controllerSettings.binds;

    using namespace app::config_defaults::input::shuttle_mouse;

    // -------------------------------------------------------------------------

    ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.large);
    ImGui::SeparatorText(_("Behavior"));
    ImGui::PopFont();

    if (ImGui::Button(TR_ID("Restore defaults", "speed"))) {
        controllerSettings.speed = kDefaultSpeed;
        controllerSettings.speedBoostFactor = kDefaultSpeedBoostFactor;
        controllerSettings.sensitivity = kDefaultSensitivity;
        MakeDirty();
    }

    float speed = controllerSettings.speed.Get();
    if (MakeDirty(ImGui::SliderFloat(_("Speed"), &speed, kMinSpeed, kMaxSpeed, "%.0f", ImGuiSliderFlags_AlwaysClamp))) {
        controllerSettings.speed = speed;
    }
    float speedBoostFactor = controllerSettings.speedBoostFactor.Get() * 100.0f;
    if (MakeDirty(ImGui::SliderFloat(_("Speed boost factor"), &speedBoostFactor, kMinSpeedBoostFactor * 100.0f,
                                     kMaxSpeedBoostFactor * 100.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp))) {
        controllerSettings.speedBoostFactor = speedBoostFactor / 100.0f;
    }
    float sensitivity = controllerSettings.sensitivity.Get();
    if (MakeDirty(ImGui::SliderFloat(_("Mouse sensitivity"), &sensitivity, kMinSensitivity, kMaxSensitivity, "%.2fx",
                                     ImGuiSliderFlags_AlwaysClamp))) {
        controllerSettings.sensitivity = sensitivity;
    }

    // -------------------------------------------------------------------------

    ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.large);
    ImGui::SeparatorText(_("Binds"));
    ImGui::PopFont();

    if (ImGui::Button(TR_ID("Restore defaults", "binds"))) {
        m_unboundActionsWidget.Capture(settings.ResetBinds(binds, true));
        MakeDirty();
    }
    ImGui::SameLine();
    if (ImGui::Button(_("Clear all"))) {
        m_unboundActionsWidget.Capture(settings.ResetBinds(binds, false));
        MakeDirty();
    }

    ImGui::TextUnformatted(_("Left, middle and right mouse buttons are mapped normally."));
    ImGui::TextUnformatted(_("Start is bound to mouse buttons 4 and 5."));
    ImGui::TextUnformatted(_("Left-click a button to assign a hotkey. Right-click to clear."));
    m_unboundActionsWidget.Display();
    if (ImGui::BeginTable("hotkeys", 1 + input::kNumBindsPerInput, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(_("Button"), ImGuiTableColumnFlags_WidthFixed, 90.0f * imguiData->displayScale);
        for (size_t i = 0; i < input::kNumBindsPerInput; i++) {
            ImGui::TableSetupColumn(fmt::format(fmt::runtime(_("Hotkey {}")), i + 1).c_str(), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        }
        ImGui::TableHeadersRow();

        auto drawRow = [&](input::InputBind &bind) {
            ImGui::TableNextRow();
            if (ImGui::TableNextColumn()) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(bind.action.name);
            }
            for (uint32 i = 0; i < input::kNumBindsPerInput; i++) {
                if (ImGui::TableNextColumn()) {
                    m_inputCaptureWidget.DrawInputBindButton(bind, i, &m_context.shuttleMouseInputs[portIndex]);
                }
            }
        };

        drawRow(binds.start);
        drawRow(binds.left);
        drawRow(binds.middle);
        drawRow(binds.right);
        drawRow(binds.moveUp);
        drawRow(binds.moveDown);
        drawRow(binds.moveLeft);
        drawRow(binds.moveRight);
        drawRow(binds.move);
        drawRow(binds.speedBoost);
        drawRow(binds.speedToggle);

        m_inputCaptureWidget.DrawCapturePopup();

        ImGui::EndTable();
    }
}

} // namespace app::ui
