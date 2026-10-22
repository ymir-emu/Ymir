#include "input_widgets.hpp"

#include <app/events/gui_event_factory.hpp>

#include <app/ui/i18n.hpp>

#include <app/imgui_data.hpp>
#include <app/settings.hpp>
#include <app/settings_defaults.hpp>

#include <algorithm>
#include <numbers>

namespace app::ui::widgets {


InputCaptureWidget::InputCaptureWidget(SharedContext &context, UnboundActionsWidget &unboundActionsWidget)
    : m_context(context)
    , m_unboundActionsWidget(unboundActionsWidget) {}

void InputCaptureWidget::DrawInputBindButton(input::InputBind &bind, size_t elementIndex, void *context) {
    const std::string bindStr = input::ToHumanString(bind.elements[elementIndex]);
    const std::string label = fmt::format("{}##bind_{}_{}", bindStr, elementIndex, bind.action.id);
    const float availWidth = ImGui::GetContentRegionAvail().x;
    auto &settings = m_context.serviceLocator.GetRequired<Settings>();

    // Left-click engages bind mode
    if (ImGui::Button(label.c_str(), ImVec2(availWidth, 0))) {
        ImGui::OpenPopup("input_capture");
        m_capturing = true;

        using enum input::Action::Kind;
        switch (bind.action.kind) {
        case Trigger: [[fallthrough]];
        case RepeatableTrigger: CaptureTrigger(bind, elementIndex, context); break;
        case ComboTrigger: CaptureComboTrigger(bind, elementIndex, context); break;
        case Button: CaptureButton(bind, elementIndex, context); break;
        case AbsoluteMonopolarAxis1D: CaptureAxis1D(bind, elementIndex, context, false); break;
        case AbsoluteBipolarAxis1D: [[fallthrough]];
        case RelativeBipolarAxis1D: CaptureAxis1D(bind, elementIndex, context, true); break;
        case AbsoluteBipolarAxis2D: [[fallthrough]];
        case RelativeBipolarAxis2D: CaptureAxis2D(bind, elementIndex, context); break;
        }
    }

    // Right-click erases a bind
    if (settings.MakeDirty(ImGui::IsItemClicked(ImGuiMouseButton_Right))) {
        m_context.inputContext.CancelCapture();
        m_capturing = false;
        bind.elements[elementIndex] = {};
        m_context.EnqueueEvent(events::gui::RebindInputs());
    }
}

void InputCaptureWidget::DrawCapturePopup() {
    if (ImGui::BeginPopup("input_capture")) {
        if (m_closePopup) {
            m_closePopup = false;
            ImGui::CloseCurrentPopup();
        }

        using enum input::Action::Kind;
        switch (m_kind) {
        case Trigger: [[fallthrough]];
        case RepeatableTrigger: [[fallthrough]];
        case Button:
            ImGui::TextUnformatted(_("Press any key or gamepad button to map it.\n\n"
                                      "Press Escape or click outside of this popup to cancel."));
            break;
        case ComboTrigger:
            ImGui::TextUnformatted(_("Press any key combo with at least one modifier key to map it.\n\n"
                                      "Press Escape or click outside of this popup to cancel."));
            break;
        case AbsoluteMonopolarAxis1D:
            ImGui::TextUnformatted(_("Move any one-dimensional monopolar axis such as analog triggers to map it.\n\n"
                                      "Press Escape or click outside of this popup to cancel."));
            break;
        case AbsoluteBipolarAxis1D: [[fallthrough]];
        case RelativeBipolarAxis1D:
            ImGui::TextUnformatted(_("Move any one-dimensional bipolar axis such as analog wheels or one direction of an "
                                      "analog stick to map it.\n\n"
                                      "Press Escape or click outside of this popup to cancel."));
            break;
        case AbsoluteBipolarAxis2D: [[fallthrough]];
        case RelativeBipolarAxis2D:
            ImGui::TextUnformatted(_(
                "Move any two-dimensional bipolar axis such as analog sticks or D-Pads to map it.\n\n"
                "Press Escape or click outside of this popup to cancel."));
            break;
        }

        ImGui::EndPopup();
    } else if (m_capturing) {
        m_context.inputContext.CancelCapture();
        m_capturing = false;
    }
}

void InputCaptureWidget::CaptureButton(input::InputBind &bind, size_t elementIndex, void *context) {
    m_kind = input::Action::Kind::Button;
    m_context.inputContext.Capture([=, this, &bind](const input::InputEvent &event) -> bool {
        if (!event.element.IsButton()) {
            return false;
        }

        // Ignore released presses
        if (!event.buttonPressed) {
            return false;
        }

        // Ignore mouse inputs
        if (event.element.IsMouse()) {
            return false;
        }

        // Don't map multiple modifier keys at once
        if (event.element.type == input::InputElement::Type::KeyCombo &&
            event.element.keyCombo.key == input::KeyboardKey::None) {
            auto bmKeyMods = BitmaskEnum(event.element.keyCombo.modifiers);
            if (!bmKeyMods.NoneExcept(input::KeyModifier::Control) && !bmKeyMods.NoneExcept(input::KeyModifier::Alt) &&
                !bmKeyMods.NoneExcept(input::KeyModifier::Shift) && !bmKeyMods.NoneExcept(input::KeyModifier::Super)) {
                return false;
            }
        }

        if (bind.elements[elementIndex] == event.element) {
            // User bound the same input element as before; do nothing
            m_closePopup = true;
            return true;
        }

        auto &settings = m_context.serviceLocator.GetRequired<Settings>();
        bind.elements[elementIndex] = event.element;
        settings.MakeDirty();
        m_unboundActionsWidget.Capture(settings.UnbindInput(event.element));
        m_context.EnqueueEvent(events::gui::RebindInputs());
        m_closePopup = true;
        return true;
    });
}

void InputCaptureWidget::CaptureTrigger(input::InputBind &bind, size_t elementIndex, void *context) {
    m_kind = input::Action::Kind::Trigger;
    m_context.inputContext.Capture([=, this, &bind](const input::InputEvent &event) -> bool {
        if (!event.element.IsButton()) {
            return false;
        }

        // Ignore mouse inputs
        if (event.element.IsMouse()) {
            return false;
        }

        if (event.element.type == input::InputElement::Type::KeyCombo) {
            if (event.element.keyCombo.key == input::KeyboardKey::None) {
                // Map key modifier combos without a key press when releasing the keys
                if (event.buttonPressed) {
                    return false;
                }
            } else {
                // Map other key combos when pressed
                if (!event.buttonPressed) {
                    return false;
                }
            }
        }

        BindInput(bind, elementIndex, context, event);
        return true;
    });
}

void InputCaptureWidget::CaptureComboTrigger(input::InputBind &bind, size_t elementIndex, void *context) {
    m_kind = input::Action::Kind::ComboTrigger;
    m_context.inputContext.Capture([=, this, &bind](const input::InputEvent &event) -> bool {
        // Only accept keyboard combos with at least one modifier pressed.
        // Disallow modifier-only key combos.
        if (event.element.type != input::InputElement::Type::KeyCombo) {
            return false;
        }
        if (event.element.keyCombo.modifiers == input::KeyModifier::None) {
            return false;
        }
        if (event.element.keyCombo.key == input::KeyboardKey::None) {
            return false;
        }
        if (!event.buttonPressed) {
            return false;
        }

        BindInput(bind, elementIndex, context, event);
        return true;
    });
}

void InputCaptureWidget::CaptureAxis1D(input::InputBind &bind, size_t elementIndex, void *context, bool bipolar) {
    m_kind = bipolar ? input::Action::Kind::AbsoluteBipolarAxis1D : input::Action::Kind::AbsoluteMonopolarAxis1D;
    m_context.inputContext.Capture([=, this, &bind](const input::InputEvent &event) -> bool {
        if (!event.element.IsAxis1D()) {
            return false;
        }
        if (event.element.IsBipolarAxis() != bipolar) {
            return false;
        }

        // Ignore mouse inputs
        if (event.element.IsMouse()) {
            return false;
        }

        if (std::abs(event.axis1DValue) < 0.5f) {
            return false;
        }

        BindInput(bind, elementIndex, context, event);
        return true;
    });
}

void InputCaptureWidget::CaptureAxis2D(input::InputBind &bind, size_t elementIndex, void *context) {
    m_kind = input::Action::Kind::AbsoluteBipolarAxis2D;
    m_context.inputContext.Capture([=, this, &bind](const input::InputEvent &event) -> bool {
        if (!event.element.IsAxis2D()) {
            return false;
        }
        if (!event.element.IsBipolarAxis()) {
            return false;
        }

        // Ignore mouse inputs
        if (event.element.IsMouse()) {
            return false;
        }

        const float d = event.axis2D.x * event.axis2D.x + event.axis2D.y * event.axis2D.y;
        if (d < 0.5f * 0.5f) {
            return false;
        }

        BindInput(bind, elementIndex, context, event);
        return true;
    });
}

void InputCaptureWidget::BindInput(input::InputBind &bind, size_t elementIndex, void *context,
                                   const input::InputEvent &event) {
    if (bind.elements[elementIndex] == event.element) {
        // User bound the same input element as before; do nothing
        m_closePopup = true;
        return;
    }

    auto &settings = m_context.serviceLocator.GetRequired<Settings>();
    bind.elements[elementIndex] = event.element;
    settings.MakeDirty();
    m_unboundActionsWidget.Capture(settings.UnbindInput(event.element));
    m_context.EnqueueEvent(events::gui::RebindInputs());
    m_closePopup = true;
}

void Crosshair(ImDrawList *drawList, const CrosshairParams &params, ImVec2 pos) {
    const YmirImGuiData *imguiData = GetYmirImGuiData();

    const float x = (int)pos.x + 0.5f;
    const float y = (int)pos.y + 0.5f;

    using namespace app::config_defaults::input::virtua_gun::crosshair;

    const float baseRadius = std::clamp<float>(params.radius, kMinRadius, kMaxRadius);
    const float baseThickness = std::clamp<float>(params.thickness, kMinThickness, kMaxThickness) * baseRadius;
    const float baseStrokeThickness =
        std::clamp<float>(params.strokeThickness, kMinStrokeThickness, kMaxStrokeThickness) * baseThickness;
    const float scale = imguiData->displayScale;

    const ImU32 color = ImGui::ColorConvertFloat4ToU32(params.color);
    const float radius = baseRadius * scale;
    const float thickness = std::max<float>(baseThickness, 2.0f) * 0.5f * scale;

    const ImU32 strokeColor = ImGui::ColorConvertFloat4ToU32(params.strokeColor);
    const float strokeThickness = std::max<float>(baseStrokeThickness, 1.0f) * scale;

    ImVec2 points[] = {
        {-thickness, -thickness}, {-radius, -thickness},    {-radius, +thickness},    {-thickness, +thickness},
        {-thickness, +radius},    {+thickness, +radius},    {+thickness, +thickness}, {+radius, +thickness},
        {+radius, -thickness},    {+thickness, -thickness}, {+thickness, -radius},    {-thickness, -radius},
    };

    // Rotate around center
    if (params.rotation != 0.0f) {
        static constexpr auto kToRadians = std::numbers::pi / 180.0;
        const float s = sin(params.rotation * kToRadians);
        const float c = cos(params.rotation * kToRadians);
        for (int i = 0; i < std::size(points); ++i) {
            const float rx = c * points[i].x - s * points[i].y;
            const float ry = s * points[i].x + c * points[i].y;
            points[i].x = rx;
            points[i].y = ry;
        }
    }

    // Translate
    for (int i = 0; i < std::size(points); ++i) {
        points[i].x += x;
        points[i].y += y;
    }

    drawList->AddConvexPolyFilled(points, std::size(points), color);
    drawList->AddPolyline(points, std::size(points), strokeColor, strokeThickness, ImDrawFlags_Closed);
}

} // namespace app::ui::widgets
