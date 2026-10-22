#include "arcade_racer_config_view.hpp"

#include <app/ui/widgets/common_widgets.hpp>

#include <app/imgui_data.hpp>
#include <app/ui/i18n.hpp>

#include <app/input/input_utils.hpp>

namespace app::ui {


ArcadeRacerConfigView::ArcadeRacerConfigView(SharedContext &context)
    : SettingsViewBase(context)
    , m_inputCaptureWidget(context, m_unboundActionsWidget)
    , m_unboundActionsWidget(context) {}

void ArcadeRacerConfigView::Display(Settings::Input::Port::ArcadeRacer &controllerSettings, uint32 portIndex) {
    using namespace app::config_defaults::input::arcade_racer;

    const YmirImGuiData *imguiData = GetYmirImGuiData();
    auto &settings = GetSettings();
    auto &binds = controllerSettings.binds;
    float sensitivity = controllerSettings.sensitivity;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(_("Wheel sensitivity"));
    widgets::ExplanationTooltip(
        "Adjusts the exponent of the value mapping curve.\n"
        "The graph below displays how the current sensitivity affects values.\n"
        "Lower sensitivity pushes values closer to zero leading to stiffer controls while higher sensitivity pushes "
        "values away from zero causing the slightest touch to be detected.\n"
        "In the meter below, green represents the raw input value and orange is the mapped value sent to the "
        "controller.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    if (MakeDirty(ImGui::SliderFloat("##wheel_sens", &sensitivity, kMinSensitivity, kMaxSensitivity, "%.02f",
                                     ImGuiSliderFlags_AlwaysClamp))) {
        controllerSettings.sensitivity = sensitivity;
    }

    // Draw curve graph and value meter
    {
        auto *drawList = ImGui::GetWindowDrawList();
        auto pos = ImGui::GetCursorScreenPos();
        auto avail = ImGui::GetContentRegionAvail();

        const auto &input = m_context.arcadeRacerInputs[portIndex];
        const float currRawValue = input.rawWheel;
        const float currValue = input.wheel;

        static constexpr float kGraphHeight = 100.0f;
        static constexpr float kMeterHeight = 25.0f;
        static constexpr ImU32 kBorderColor = 0xE0F5D4C6;
        static constexpr ImU32 kBackgroundColor = 0xAA401A0A;
        static constexpr ImU32 kZeroLineColor = 0x9AA89992;
        static constexpr ImU32 kGraphLineColor = 0xE0BAD1DB;
        static constexpr ImU32 kValueColor = 0xF05FF58F;
        static constexpr ImU32 kAdjustedValueColor = 0xF05F8FF5;

        const float borderThickness = 1.5f * imguiData->displayScale;
        const float zeroLineThickness = 1.0f * imguiData->displayScale;
        const float graphLineThickness = 1.7f * imguiData->displayScale;
        const float valueLineThickness = 2.0f * imguiData->displayScale;
        const float valuePointRadius = 2.5f * imguiData->displayScale;

        // ---------------------------------------------------------------------
        // Draw value mapping graph

        const auto graphSize = ImVec2(avail.x, kGraphHeight * imguiData->displayScale);

        // Graph background
        drawList->AddRectFilled(pos, ImVec2(pos.x + graphSize.x, pos.y + graphSize.y), kBackgroundColor);

        // Zero crossings (horizontal and vertical)
        drawList->AddLine(ImVec2(pos.x, pos.y + graphSize.y * 0.5f),
                          ImVec2(pos.x + graphSize.x, pos.y + graphSize.y * 0.5f), kZeroLineColor, zeroLineThickness);
        drawList->AddLine(ImVec2(pos.x + graphSize.x * 0.5f, pos.y),
                          ImVec2(pos.x + graphSize.x * 0.5f, pos.y + graphSize.y), kZeroLineColor, zeroLineThickness);

        // Map [-1.0, +1.0] to [1.0, 0.0].
        // The value is flipped so that -1.0 is at the bottom of the graph.
        auto mapToGraph = [&](float value) { return (-value + 1.0f) * 0.5f; };

        // Graph values
        std::vector<ImVec2> graph{};
        const int maxValue = graphSize.x;
        for (int x = 0; x < maxValue; x += 2) {
            // Convert to -1.0 to +1.0 range and apply sensitivity
            const float value = input::ApplySensitivity(static_cast<float>(x) / graphSize.x * 2.0f - 1.0f, sensitivity);
            const float offset = mapToGraph(value);
            graph.push_back(ImVec2(pos.x + x, pos.y + offset * graphSize.y));
        }
        drawList->AddPolyline(graph.data(), graph.size(), kGraphLineColor, graphLineThickness);

        // Current input value mapped onto the graph, vertical
        const ImVec2 valuePos(pos.x + (currRawValue + 1.0f) * 0.5f * graphSize.x,
                              pos.y + graphSize.y * mapToGraph(currValue));
        drawList->AddLine(ImVec2(pos.x + (currRawValue + 1.0f) * 0.5f * graphSize.x, pos.y + graphSize.y * 0.5f),
                          valuePos, kValueColor, valueLineThickness);
        drawList->AddCircleFilled(valuePos, valuePointRadius, kValueColor);

        // Graph border
        drawList->AddRect(pos, ImVec2(pos.x + graphSize.x, pos.y + graphSize.y), kBorderColor, 0.0f, borderThickness);

        ImGui::Dummy(graphSize);

        // ---------------------------------------------------------------------
        // Draw meter with the current and mapped values

        pos = ImGui::GetCursorScreenPos();
        avail = ImGui::GetContentRegionAvail();

        const auto meterSize = ImVec2(avail.x, kMeterHeight * imguiData->displayScale);

        // Meter background
        drawList->AddRectFilled(pos, ImVec2(pos.x + meterSize.x, pos.y + meterSize.y), kBackgroundColor);

        // Zero crossing
        drawList->AddLine(ImVec2(pos.x + meterSize.x * 0.5f, pos.y),
                          ImVec2(pos.x + meterSize.x * 0.5f, pos.y + meterSize.y), kZeroLineColor, zeroLineThickness);

        // Raw value
        if (m_showRawValueInMeter) {
            drawList->AddLine(ImVec2(pos.x + meterSize.x * (currRawValue + 1.0f) * 0.5f, pos.y),
                              ImVec2(pos.x + meterSize.x * (currRawValue + 1.0f) * 0.5f, pos.y + meterSize.y),
                              kValueColor, valueLineThickness);
        }

        // Adjusted value
        drawList->AddLine(ImVec2(pos.x + meterSize.x * (currValue + 1.0f) * 0.5f, pos.y),
                          ImVec2(pos.x + meterSize.x * (currValue + 1.0f) * 0.5f, pos.y + meterSize.y),
                          kAdjustedValueColor, valueLineThickness);

        // Meter border
        drawList->AddRect(pos, ImVec2(pos.x + meterSize.x, pos.y + meterSize.y), kBorderColor, 0.0f, borderThickness);

        ImGui::Dummy(meterSize);
    }
    ImGui::Checkbox(_("Display raw value in meter"), &m_showRawValueInMeter);

    ImGui::Separator();

    if (ImGui::Button(_("Restore default binds"))) {
        m_unboundActionsWidget.Capture(settings.ResetBinds(binds, true));
        MakeDirty();
    }
    ImGui::SameLine();
    if (ImGui::Button(_("Clear all binds"))) {
        m_unboundActionsWidget.Capture(settings.ResetBinds(binds, false));
        MakeDirty();
    }

    ImGui::TextUnformatted(_("Left-click a button to assign a hotkey. Right-click to clear."));
    m_unboundActionsWidget.Display();
    if (ImGui::BeginTable("hotkeys", 1 + input::kNumBindsPerInput,
                          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn(_("Button"), ImGuiTableColumnFlags_WidthFixed, 95.0f * imguiData->displayScale);
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
        drawRow(binds.start);
        drawRow(binds.gearUp);
        drawRow(binds.gearDown);
        drawRow(binds.wheelLeft);
        drawRow(binds.wheelRight);
        drawRow(binds.wheel);

        m_inputCaptureWidget.DrawCapturePopup();

        ImGui::EndTable();
    }
}

} // namespace app::ui
