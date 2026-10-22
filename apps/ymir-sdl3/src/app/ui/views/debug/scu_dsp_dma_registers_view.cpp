#include "scu_dsp_dma_registers_view.hpp"

#include <ymir/hw/scu/scu.hpp>

#include <app/imgui_data.hpp>
#include <app/ui/i18n.hpp>

namespace app::ui {


SCUDSPDMARegistersView::SCUDSPDMARegistersView(ymir::scu::SCU &scu)
    : m_scu(scu) {}

void SCUDSPDMARegistersView::Display() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();

    ImGui::BeginGroup();

    ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
    const float hexCharWidth = ImGui::CalcTextSize("F").x;
    ImGui::PopFont();

    auto &dsp = m_scu.GetDSP();

    auto fmtRAMOp = [&](uint8 value, bool allowProgramRAM) {
        switch (value) {
        case 0: return "Data RAM 0";
        case 1: return "Data RAM 1";
        case 2: return "Data RAM 2";
        case 3: return "Data RAM 3";
        case 4: return allowProgramRAM ? "Program RAM" : "Invalid (4)";
        case 5: return "Invalid (5)";
        case 6: return "Invalid (6)";
        case 7: return "Invalid (7)";
        default: return "Invalid";
        }
    };

    {
        if (dsp.dmaToD0) {
            ImGui::BeginGroup();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(_("From"));
            ImGui::SameLine();
            if (ImGui::BeginCombo("##src", fmtRAMOp(dsp.dmaSrc, false), ImGuiComboFlags_WidthFitPreview)) {
                for (uint32 i = 0; i < 4; i++) {
                    if (ImGui::Selectable(fmtRAMOp(i, false), dsp.dmaSrc == i)) {
                        dsp.dmaSrc = i;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::EndGroup();

            ImGui::SameLine();

            ImGui::BeginGroup();
            ImGui::TextUnformatted(_("to"));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 7);
            ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
            if (ImGui::InputScalar("##dst", ImGuiDataType_U32, &dsp.dmaWriteAddr, nullptr, nullptr, "%07X",
                                   ImGuiInputTextFlags_CharsHexadecimal)) {
                dsp.dmaWriteAddr &= 0x7FF'FFFC;
            }
            ImGui::PopFont();
            ImGui::EndGroup();
        } else {
            ImGui::BeginGroup();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(_("From"));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 7);
            ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
            if (ImGui::InputScalar("##src", ImGuiDataType_U32, &dsp.dmaReadAddr, nullptr, nullptr, "%07X",
                                   ImGuiInputTextFlags_CharsHexadecimal)) {
                dsp.dmaReadAddr &= 0x7FF'FFFC;
            }
            ImGui::PopFont();
            ImGui::EndGroup();

            ImGui::SameLine();

            ImGui::BeginGroup();
            ImGui::TextUnformatted(_("to"));
            ImGui::SameLine();
            if (ImGui::BeginCombo("##dst", fmtRAMOp(dsp.dmaDst, true), ImGuiComboFlags_WidthFitPreview)) {
                for (uint32 i = 0; i < 5; i++) {
                    if (ImGui::Selectable(fmtRAMOp(i, true), dsp.dmaDst == i)) {
                        dsp.dmaDst = i;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::EndGroup();
        }
        ImGui::SameLine();
        if (ImGui::Button(_("Swap"))) {
            dsp.dmaToD0 = !dsp.dmaToD0;
            if (!dsp.dmaToD0) {
                dsp.dmaAddrInc = std::min(dsp.dmaAddrInc, 4u);
            }
        }
    }

    {
        ImGui::BeginGroup();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(_("Increment address by"));
        ImGui::SameLine();
        if (ImGui::BeginCombo("##addr_inc", fmt::format("{}", dsp.dmaAddrInc).c_str(),
                              ImGuiComboFlags_WidthFitPreview)) {
            if (dsp.dmaToD0) {
                for (uint32 i = 0; i < 8; i++) {
                    const uint32 inc = (1u << i) >> 1u;
                    if (ImGui::Selectable(fmt::format("{}", inc).c_str(), dsp.dmaAddrInc == inc)) {
                        dsp.dmaAddrInc = inc;
                    }
                }
            } else {
                for (uint32 i = 0; i < 2; i++) {
                    const uint32 inc = (1u << (i * 2u)) & ~1u;
                    if (ImGui::Selectable(fmt::format("{}", inc).c_str(), dsp.dmaAddrInc == inc)) {
                        dsp.dmaAddrInc = inc;
                    }
                }
            }
            ImGui::EndCombo();
        }
        ImGui::EndGroup();
        ImGui::SameLine();
        ImGui::Checkbox(_("Hold"), &dsp.dmaHold);
    }

    {
        ImGui::BeginGroup();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(_("Count:"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 2);
        ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
        ImGui::InputScalar("##count", ImGuiDataType_U8, &dsp.dmaCount, nullptr, nullptr, "%02X",
                           ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::TextUnformatted(_("longwords"));
        ImGui::EndGroup();
        ImGui::SameLine();
        if (ImGui::Button(_("Run transfer"))) {
            dsp.dmaRun = true;
        }
    }

    ImGui::EndGroup();
}

} // namespace app::ui
