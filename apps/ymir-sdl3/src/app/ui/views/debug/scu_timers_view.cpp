#include "scu_timers_view.hpp"

#include <ymir/hw/scu/scu.hpp>

#include <app/imgui_data.hpp>
#include <app/ui/i18n.hpp>

namespace app::ui {


SCUTimersView::SCUTimersView(ymir::scu::SCU &scu)
    : m_scu(scu) {}

void SCUTimersView::Display() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    auto &probe = m_scu.GetProbe();

    ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
    const float hexCharWidth = ImGui::CalcTextSize("F").x;
    ImGui::PopFont();

    bool t1mode = probe.GetTimer1Mode();

    bool enable = probe.IsTimerEnabled();
    if (ImGui::Checkbox(TR_ID("Enabled", "timer"), &enable)) {
        probe.SetTimerEnabled(enable);
    }

    if (ImGui::BeginTable("timer", 3, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.medium);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(_("Timer 0"));
            ImGui::PopFont();
        }
        if (ImGui::TableNextColumn()) {
            uint16 counter = probe.GetTimer0Counter();
            ImGui::BeginGroup();
            ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 3);
            ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
            if (ImGui::InputScalar("##t0cnt", ImGuiDataType_U16, &counter, nullptr, nullptr, "%03X",
                                   ImGuiInputTextFlags_CharsHexadecimal)) {
                probe.SetTimer0Counter(counter);
            }
            ImGui::PopFont();
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(_("Counter"));
            ImGui::EndGroup();
        }
        if (ImGui::TableNextColumn()) {
            uint16 compare = probe.GetTimer0Compare();
            ImGui::BeginGroup();
            ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 3);
            ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
            ImGui::InputScalar("##t0cmp", ImGuiDataType_U16, &compare, nullptr, nullptr, "%03X",
                               ImGuiInputTextFlags_CharsHexadecimal);
            ImGui::PopFont();
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(_("Compare"));
            ImGui::EndGroup();
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.medium);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(_("Timer 1"));
            ImGui::PopFont();
        }
        if (ImGui::TableNextColumn()) {
            uint16 reload = probe.GetTimer1Reload();
            ImGui::BeginGroup();
            ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 3);
            ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
            ImGui::InputScalar("##t1rld", ImGuiDataType_U16, &reload, nullptr, nullptr, "%03X",
                               ImGuiInputTextFlags_CharsHexadecimal);
            ImGui::PopFont();
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(_("Reload"));
            ImGui::EndGroup();
        }
        if (ImGui::TableNextColumn()) {
            if (ImGui::RadioButton(_("Every line"), !t1mode)) {
                probe.SetTimer1Mode(false);
            }
            ImGui::SameLine();
            if (ImGui::RadioButton(_("Timer 0 match"), t1mode)) {
                probe.SetTimer1Mode(true);
            }
        }

        ImGui::EndTable();
    }

    // TODO: draw display diagram showing where Timer 1 will match on the screen
}

} // namespace app::ui
