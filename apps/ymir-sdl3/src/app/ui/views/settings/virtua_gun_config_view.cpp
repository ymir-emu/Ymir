#include "virtua_gun_config_view.hpp"

#include <app/imgui_data.hpp>
#include <app/ui/i18n.hpp>

namespace app::ui {


VirtuaGunConfigView::VirtuaGunConfigView(SharedContext &context)
    : SettingsViewBase(context)
    , m_inputCaptureWidget(context, m_unboundActionsWidget)
    , m_unboundActionsWidget(context) {}

void VirtuaGunConfigView::Display(Settings::Input::Port::VirtuaGun &controllerSettings, uint32 portIndex) {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    auto &settings = GetSettings();
    auto &binds = controllerSettings.binds;
    auto &xhair = controllerSettings.crosshair;

    using namespace app::config_defaults::input::virtua_gun;

    // -------------------------------------------------------------------------

    ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.large);
    ImGui::SeparatorText(_("Behavior"));
    ImGui::PopFont();

    if (ImGui::Button(TR_ID("Restore defaults", "speed"))) {
        controllerSettings.speed = kDefaultSpeed;
        controllerSettings.speedBoostFactor = kDefaultSpeedBoostFactor;
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

    // -------------------------------------------------------------------------

    ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.large);
    ImGui::SeparatorText(_("Crosshair"));
    ImGui::PopFont();

    ImGui::BeginGroup();
    {
        const float scale = imguiData->displayScale;
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 size{150.0f * scale, 150.0f * scale};
        const ImVec2 end{pos.x + size.x, pos.y + size.y};
        static constexpr ImU32 kBorderColor = 0xE0F5D4C6;
        const ImU32 bgColor = ImGui::ColorConvertFloat4ToU32(
            ImVec4{m_crosshairPreviewBGColor[0], m_crosshairPreviewBGColor[1], m_crosshairPreviewBGColor[2], 1.0f});

        ImDrawList *drawList = ImGui::GetWindowDrawList();

        const widgets::CrosshairParams params{
            .color = {xhair.color[0], xhair.color[1], xhair.color[2], xhair.color[3]},
            .radius = xhair.radius,
            .thickness = xhair.thickness,
            .rotation = xhair.rotation,

            .strokeColor = {xhair.strokeColor[0], xhair.strokeColor[1], xhair.strokeColor[2], xhair.strokeColor[3]},
            .strokeThickness = xhair.strokeThickness,
        };

        drawList->AddRectFilled(pos, end, bgColor);

        drawList->PushClipRect(pos, end, true);
        widgets::Crosshair(drawList, params, {pos.x + size.x * 0.5f, pos.y + size.y * 0.5f});
        drawList->PopClipRect();

        drawList->AddRect(pos, end, kBorderColor, 0.0f, 1.0f * scale);

        ImGui::Dummy(size);

        ImGui::ColorEdit3(_("Background"), m_crosshairPreviewBGColor.data(), ImGuiColorEditFlags_NoInputs);
    }
    ImGui::EndGroup();

    ImGui::SameLine();

    ImGui::BeginGroup();
    {
        using namespace crosshair;
        if (ImGui::BeginTable("crosshair_params", 2, ImGuiTableColumnFlags_WidthStretch)) {
            ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(_("Color"));
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            MakeDirty(ImGui::ColorEdit4("##color", xhair.color.data(), ImGuiColorEditFlags_AlphaBar));

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(_("Radius"));
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            MakeDirty(ImGui::SliderFloat("##radius", &xhair.radius, kMinRadius, kMaxRadius, "%.1f",
                                         ImGuiSliderFlags_AlwaysClamp));

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(_("Thickness"));
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            float thickness = xhair.thickness * 100.0f;
            if (MakeDirty(ImGui::SliderFloat("##thickness", &thickness, kMinThickness * 100.0f, kMaxThickness * 100.0f,
                                             "%.1f%%", ImGuiSliderFlags_AlwaysClamp))) {
                xhair.thickness = thickness / 100.0f;
            }

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(_("Rotation"));
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            MakeDirty(ImGui::SliderFloat("##rotation", &xhair.rotation, 0.0f, 90.0f, "%.1f\u00B0",
                                         ImGuiSliderFlags_AlwaysClamp));

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(_("Stroke color"));
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            MakeDirty(ImGui::ColorEdit4("##stroke_color", xhair.strokeColor.data(), ImGuiColorEditFlags_AlphaBar));

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(_("Stroke thickness"));
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            float strokeThickness = xhair.strokeThickness * 100.0f;
            if (MakeDirty(ImGui::SliderFloat("##stroke_thickness", &strokeThickness, kMinStrokeThickness * 100.0f,
                                             kMaxStrokeThickness * 100.0f, "%.1f%%", ImGuiSliderFlags_AlwaysClamp))) {
                xhair.strokeThickness = strokeThickness / 100.0f;
            }

            ImGui::EndTable();
        }

        if (ImGui::Button(TR_ID("Restore defaults", "crosshair"))) {
            xhair.color = kDefaultColor[portIndex];
            xhair.radius = kDefaultRadius[portIndex];
            xhair.thickness = kDefaultThickness[portIndex];
            xhair.rotation = kDefaultRotation[portIndex];
            xhair.strokeColor = kDefaultStrokeColor[portIndex];
            xhair.strokeThickness = kDefaultStrokeThickness[portIndex];
            MakeDirty();
        }
        ImGui::SameLine();
        if (ImGui::Button(TR_ID("Randomize", "crosshair"))) {

            xhair.color = {m_randomDist(m_randomEngine), m_randomDist(m_randomEngine), m_randomDist(m_randomEngine),
                           m_randomDist(m_randomEngine) * 0.4f + 0.6f};
            xhair.radius = m_randomDist(m_randomEngine) * (kMaxRadius - kMinRadius) + kMinRadius;
            xhair.thickness = m_randomDist(m_randomEngine) * (kMaxThickness - kMinThickness) + kMinThickness;
            xhair.rotation = m_randomDist(m_randomEngine) * 90.0f;
            xhair.strokeColor = {m_randomDist(m_randomEngine), m_randomDist(m_randomEngine),
                                 m_randomDist(m_randomEngine), m_randomDist(m_randomEngine) * 0.4f + 0.6f};
            xhair.strokeThickness =
                m_randomDist(m_randomEngine) * (kMaxStrokeThickness - kMinStrokeThickness) + kMinStrokeThickness;
            MakeDirty();
        }
    }
    ImGui::EndGroup();

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
                    m_inputCaptureWidget.DrawInputBindButton(bind, i, &m_context.virtuaGunInputs[portIndex]);
                }
            }
        };

        drawRow(binds.start);
        drawRow(binds.trigger);
        drawRow(binds.reload);
        drawRow(binds.up);
        drawRow(binds.down);
        drawRow(binds.left);
        drawRow(binds.right);
        drawRow(binds.move);
        drawRow(binds.recenter);
        drawRow(binds.speedBoost);
        drawRow(binds.speedToggle);

        m_inputCaptureWidget.DrawCapturePopup();

        ImGui::EndTable();
    }

    // -------------------------------------------------------------------------

    ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.large);
    ImGui::SeparatorText(_("Mouse binds"));
    ImGui::PopFont();

    // TODO: configurable mouse inputs
    ImGui::TextUnformatted(_("Mouse inputs are bound as follows:"));

    if (ImGui::BeginTable("mouse_hotkeys", 2, ImGuiTableFlags_SizingFixedFit)) {
        auto drawRow = [&](const char *name, const char *button) {
            ImGui::TableNextRow();
            if (ImGui::TableNextColumn()) {
                ImGui::TextUnformatted(name);
            }
            if (ImGui::TableNextColumn()) {
                ImGui::TextUnformatted(button);
            }
        };

        drawRow(_("Trigger"), _("Left button"));
        drawRow(_("Reload"), _("Right button"));
        drawRow(_("Start"), _("Middle button"));

        ImGui::EndTable();
    }
}

} // namespace app::ui
