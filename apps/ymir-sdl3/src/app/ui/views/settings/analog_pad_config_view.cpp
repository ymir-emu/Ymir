#include "analog_pad_config_view.hpp"

#include <app/imgui_data.hpp>
#include <app/ui/i18n.hpp>

namespace app::ui {


AnalogPadConfigView::AnalogPadConfigView(SharedContext &context)
    : SettingsViewBase(context)
    , m_inputCaptureWidget(context, m_unboundActionsWidget)
    , m_unboundActionsWidget(context) {}

void AnalogPadConfigView::Display(Settings::Input::Port::AnalogPad &controllerSettings, uint32 portIndex) {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    auto &settings = GetSettings();
    auto &binds = controllerSettings.binds;

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(_("Mode:"));
    ImGui::SameLine();
    if (ImGui::RadioButton(_("Analog"), m_context.analogPadInputs[portIndex].analogMode)) {
        m_context.analogPadInputs[portIndex].analogMode = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton(_("Digital"), !m_context.analogPadInputs[portIndex].analogMode)) {
        m_context.analogPadInputs[portIndex].analogMode = false;
    }

    if (ImGui::Button(_("Restore defaults"))) {
        m_unboundActionsWidget.Capture(settings.ResetBinds(binds, true));
        MakeDirty();
    }
    ImGui::SameLine();
    if (ImGui::Button(_("Clear all"))) {
        m_unboundActionsWidget.Capture(settings.ResetBinds(binds, false));
        MakeDirty();
    }

    ImGui::TextUnformatted(_("Left-click a button to assign a hotkey. Right-click to clear."));
    m_unboundActionsWidget.Display();
    if (ImGui::BeginTable("hotkeys", 1 + input::kNumBindsPerInput,
                          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn(_("Button"), ImGuiTableColumnFlags_WidthFixed, 85.0f * imguiData->displayScale);
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
                    m_inputCaptureWidget.DrawInputBindButton(bind, i, &m_context.analogPadInputs[portIndex]);
                }
            }
        };

        drawRow(binds.a);
        drawRow(binds.b);
        drawRow(binds.c);
        drawRow(binds.x);
        drawRow(binds.y);
        drawRow(binds.z);
        drawRow(binds.l);
        drawRow(binds.r);
        drawRow(binds.start);
        drawRow(binds.up);
        drawRow(binds.down);
        drawRow(binds.left);
        drawRow(binds.right);
        drawRow(binds.dpad);
        drawRow(binds.analogStick);
        drawRow(binds.analogL);
        drawRow(binds.analogR);
        drawRow(binds.switchMode);

        m_inputCaptureWidget.DrawCapturePopup();

        ImGui::EndTable();
    }
}

} // namespace app::ui
