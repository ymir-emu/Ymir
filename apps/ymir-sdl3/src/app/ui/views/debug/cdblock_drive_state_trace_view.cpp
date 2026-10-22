#include "cdblock_drive_state_trace_view.hpp"

#include <app/imgui_data.hpp>
#include <app/ui/i18n.hpp>
#include <app/settings.hpp>

#include <ymir/util/bit_ops.hpp>

namespace app::ui {


static ImVec4 MakeColorFromU8(uint8 value) {
    ImVec4 color;
    color.w = 1.0f;
    value = bit::reverse(value);
    const float hue = static_cast<float>(value) / static_cast<float>(0xFF);
    ImGui::ColorConvertHSVtoRGB(hue, 0.63f, 1.00f, color.x, color.y, color.z);
    return color;
}

static std::string MakeString(std::span<const uint8, 13> values) {
    fmt::memory_buffer buf{};
    auto out = std::back_inserter(buf);
    bool first = true;
    for (uint8 val : values) {
        if (first) {
            first = false;
        } else {
            fmt::format_to(out, " ");
        }
        fmt::format_to(out, "{:02X}", val);
    }
    return fmt::to_string(buf);
}

CDDriveStateTraceView::CDDriveStateTraceView(SharedContext &context)
    : m_context(context)
    , m_tracer(context.tracers.CDDrive) {}

void CDDriveStateTraceView::Display() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    const auto &settings = m_context.serviceLocator.GetRequired<Settings>();

    const float paddingWidth = ImGui::GetStyle().FramePadding.x;
    ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
    const float hexCharWidth = ImGui::CalcTextSize("F").x;
    ImGui::PopFont();

    ImGui::BeginGroup();

    ImGui::Checkbox(_("Enable"), &m_tracer.traceStateUpdates);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted(_("You must also enable tracing in Debug > Enable tracing (F11)"));
        ImGui::EndTooltip();
    }
    ImGui::SameLine();
    if (ImGui::Button(_("Clear"))) {
        m_tracer.ClearStateUpdates();
    }
    if (!settings.cdblock.useLLE) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        ImGui::TextColored(imguiData->colors.notice, "%s", _("CD Block LLE is disabled. Nothing will be traced here."));
        ImGui::PopTextWrapPos();
    }

    if (ImGui::BeginTable("cdblock_cmd_trace", 3,
                          ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_PreferSortDescending);
        ImGui::TableSetupColumn(_("Command"), ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,
                                paddingWidth * 2 + hexCharWidth * (2 * 13 + 1 * 12));
        ImGui::TableSetupColumn(_("Status"), ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,
                                paddingWidth * 2 + hexCharWidth * (2 * 13 + 1 * 12));
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableHeadersRow();

        const size_t count = m_tracer.stateUpdates.Count();
        for (size_t i = 0; i < count; i++) {
            auto *sort = ImGui::TableGetSortSpecs();
            bool reverse = false;
            if (sort != nullptr && sort->SpecsCount == 1) {
                reverse = sort->Specs[0].SortDirection == ImGuiSortDirection_Descending;
            }

            auto trace = reverse ? m_tracer.stateUpdates.ReadReverse(i) : m_tracer.stateUpdates.Read(i);

            ImGui::TableNextRow();
            if (ImGui::TableNextColumn()) {
                ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
                ImGui::Text("%u", trace.index);
                ImGui::PopFont();
            }
            if (ImGui::TableNextColumn()) {
                const std::string str = MakeString(trace.command);
                const uint8 cmd = trace.command[0];
                ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
                ImGui::TextColored(MakeColorFromU8(cmd), "%s", str.c_str());
                ImGui::PopFont();
            }
            if (ImGui::TableNextColumn()) {
                const std::string str = MakeString(trace.status);
                const uint8 status = trace.status[0];
                ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
                ImGui::TextColored(MakeColorFromU8(status), "%s", str.c_str());
                ImGui::PopFont();
            }
        }

        ImGui::EndTable();
    }

    ImGui::EndGroup();
}

} // namespace app::ui
