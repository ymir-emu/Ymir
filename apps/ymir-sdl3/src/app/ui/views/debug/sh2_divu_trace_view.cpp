#include "sh2_divu_trace_view.hpp"

#include <app/imgui_data.hpp>
#include <app/ui/i18n.hpp>

#include <cinttypes>

using namespace ymir;

namespace app::ui {


SH2DivisionUnitTraceView::SH2DivisionUnitTraceView(SH2Tracer &tracer)
    : m_tracer(tracer) {}

void SH2DivisionUnitTraceView::Display() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();

    ImGui::BeginGroup();

    ImGui::Checkbox(_("Enable"), &m_tracer.traceDivisions);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted(_("You must also enable tracing in Debug > Enable tracing (F11)"));
        ImGui::EndTooltip();
    }
    ImGui::SameLine();
    ImGui::Checkbox(_("Display numbers in hexadecimal"), &m_showHex);
    ImGui::SameLine();
    if (ImGui::Button(_("Clear"))) {
        m_tracer.divisions.Clear();
        m_tracer.divStats.Clear();
        m_tracer.ResetDivisionCounter();
    }

    const float paddingWidth = ImGui::GetStyle().FramePadding.x;
    ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
    const float hexCharWidth = ImGui::CalcTextSize("F").x;
    ImGui::PopFont();

    if (ImGui::BeginTable("divu_trace", 7,
                          ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_PreferSortDescending);
        ImGui::TableSetupColumn(_("Size"), ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort);
        ImGui::TableSetupColumn(_("Dividend"), ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,
                                paddingWidth * 2 + hexCharWidth * 16);
        ImGui::TableSetupColumn(_("Divisor"), ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,
                                paddingWidth * 2 + hexCharWidth * 8);
        ImGui::TableSetupColumn(_("Quotient"), ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,
                                paddingWidth * 2 + hexCharWidth * 8);
        ImGui::TableSetupColumn(_("Remainder"), ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,
                                paddingWidth * 2 + hexCharWidth * 8);
        ImGui::TableSetupColumn(_("Overflow"), ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoSort);
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableHeadersRow();

        const size_t count = m_tracer.divisions.Count();
        for (size_t i = 0; i < count; i++) {
            auto *sort = ImGui::TableGetSortSpecs();
            bool reverse = false;
            if (sort != nullptr && sort->SpecsCount == 1) {
                reverse = sort->Specs[0].SortDirection == ImGuiSortDirection_Descending;
            }

            auto trace = reverse ? m_tracer.divisions.ReadReverse(i) : m_tracer.divisions.Read(i);

            ImGui::TableNextRow();
            if (ImGui::TableNextColumn()) {
                ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
                ImGui::Text("%u", trace.counter);
                ImGui::PopFont();
            }
            if (ImGui::TableNextColumn()) {
                ImGui::Text("%dx32", trace.div64 ? 64 : 32);
            }
            if (ImGui::TableNextColumn()) {
                if (m_showHex) {
                    ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
                    if (trace.div64) {
                        ImGui::Text("%016" PRIX64, trace.dividend);
                    } else {
                        ImGui::Text("%08X", (sint32)trace.dividend);
                    }
                    ImGui::PopFont();
                } else {
                    ImGui::Text("%" PRId64, trace.dividend);
                }
            }
            if (ImGui::TableNextColumn()) {
                if (m_showHex) {
                    ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
                    ImGui::Text("%08X", trace.divisor);
                    ImGui::PopFont();
                } else {
                    ImGui::Text("%d", trace.divisor);
                }
            }
            if (ImGui::TableNextColumn()) {
                if (trace.finished) {
                    if (m_showHex) {
                        ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
                        ImGui::Text("%08X", trace.quotient);
                        ImGui::PopFont();
                    } else {
                        ImGui::Text("%d", trace.quotient);
                    }
                }
            }
            if (ImGui::TableNextColumn()) {
                if (trace.finished) {
                    if (m_showHex) {
                        ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
                        ImGui::Text("%08X", trace.remainder);
                        ImGui::PopFont();
                    } else {
                        ImGui::Text("%d", trace.remainder);
                    }
                }
            }
            if (ImGui::TableNextColumn()) {
                if (trace.overflow) {
                    if (trace.overflowIntrEnable) {
                        ImGui::TextUnformatted(_("yes+IRQ"));
                    } else {
                        ImGui::TextUnformatted(_("yes"));
                    }
                }
            }
        }

        ImGui::EndTable();
    }

    ImGui::EndGroup();
}

} // namespace app::ui
