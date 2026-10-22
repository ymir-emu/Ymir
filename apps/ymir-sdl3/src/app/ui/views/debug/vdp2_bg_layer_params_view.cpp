#include "vdp2_bg_layer_params_view.hpp"

#include <app/ui/utils/debug/vdp_window_set_printer.hpp>

#include <ymir/hw/vdp/vdp.hpp>

#include <app/imgui_data.hpp>
#include <app/ui/i18n.hpp>

#include <imgui.h>

using namespace ymir;

namespace app::ui {


VDP2BGLayerParamsView::VDP2BGLayerParamsView(vdp::VDP &vdp)
    : m_vdp(vdp) {}

void VDP2BGLayerParamsView::Display() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    auto &probe = m_vdp.GetProbe();
    const auto &regs2 = probe.GetVDP2Regs();
    const auto &state2 = probe.GetVDP2State();

    auto printYesNo = [&](bool value) {
        if (value) {
            ImGui::TextUnformatted(_("yes"));
        } else {
            ImGui::TextUnformatted(_("no"));
        }
    };

    if (ImGui::BeginTable("layers", 7, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("");
        ImGui::TableSetupColumn("NBG0", ImGuiTableColumnFlags_WidthFixed, 70.0f * imguiData->displayScale);
        ImGui::TableSetupColumn("NBG1", ImGuiTableColumnFlags_WidthFixed, 70.0f * imguiData->displayScale);
        ImGui::TableSetupColumn("NBG2", ImGuiTableColumnFlags_WidthFixed, 70.0f * imguiData->displayScale);
        ImGui::TableSetupColumn("NBG3", ImGuiTableColumnFlags_WidthFixed, 70.0f * imguiData->displayScale);
        ImGui::TableSetupColumn("RBG0", ImGuiTableColumnFlags_WidthFixed, 70.0f * imguiData->displayScale);
        ImGui::TableSetupColumn("RBG1", ImGuiTableColumnFlags_WidthFixed, 70.0f * imguiData->displayScale);
        ImGui::TableHeadersRow();

        // -------------------------------------------------------------------------------------------------------------

        auto printType = [&](bool bitmap) {
            if (bitmap) {
                ImGui::TextUnformatted(_("Bitmap"));
            } else {
                ImGui::TextUnformatted(_("Scroll"));
            }
        };

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(_("Type"));
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                printType(regs2.bgParams[i + 1].bitmap);
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                printType(regs2.bgParams[i].bitmap);
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        auto printPlaneSize = [&](const vdp::BGParams &params) {
            if (params.bitmap) {
                ImGui::Text("%ux%u", params.bitmapSizeH, params.bitmapSizeV);
            } else {
                ImGui::Text("%ux%u", 1u << params.pageShiftH, 1u << params.pageShiftV);
            }
        };

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(_("Plane/bitmap size"));
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                printPlaneSize(regs2.bgParams[i + 1]);
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                printPlaneSize(regs2.bgParams[i]);
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        auto printCharPatSize = [&](const vdp::BGParams &params) {
            if (params.bitmap) {
                ImGui::TextUnformatted("-");
            } else {
                const uint8 size = 1u << params.cellSizeShift;
                ImGui::Text("%ux%u", size, size);
            }
        };

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(_("Char. pattern size"));
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                printCharPatSize(regs2.bgParams[i + 1]);
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                printCharPatSize(regs2.bgParams[i]);
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(_("Reduction"));
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                if (i == 0) {
                    ImGui::TextUnformatted(regs2.ZMCTL.N0ZMQT ? "1/4x" : regs2.ZMCTL.N0ZMHF ? "1/2x" : "1x");
                } else if (i == 1) {
                    ImGui::TextUnformatted(regs2.ZMCTL.N1ZMQT ? "1/4x" : regs2.ZMCTL.N1ZMHF ? "1/2x" : "1x");
                } else {
                    ImGui::TextUnformatted("1x");
                }
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                ImGui::TextUnformatted("-");
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        auto printColorFormat = [&](vdp::ColorFormat colorFormat) {
            switch (colorFormat) {
            case vdp::ColorFormat::Palette16: ImGui::TextUnformatted("Pal 16"); break;
            case vdp::ColorFormat::Palette256: ImGui::TextUnformatted("Pal 256"); break;
            case vdp::ColorFormat::Palette2048: ImGui::TextUnformatted("Pal 2048"); break;
            case vdp::ColorFormat::RGB555: ImGui::TextUnformatted("RGB 5:5:5"); break;
            case vdp::ColorFormat::RGB888: ImGui::TextUnformatted("RGB 8:8:8"); break;
            }
        };

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(_("Color format"));
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                printColorFormat(regs2.bgParams[i + 1].colorFormat);
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                printColorFormat(regs2.bgParams[i].colorFormat);
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(_("Transparency"));
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                printYesNo(regs2.bgParams[i + 1].enableTransparency);
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                printYesNo(regs2.bgParams[i].enableTransparency);
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(_("Shadow"));
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                printYesNo(regs2.bgParams[i + 1].shadowEnable);
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                printYesNo(regs2.bgParams[i].shadowEnable);
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(_("Gradation"));
        static constexpr vdp::ColorGradScreen kColorGradScreens[] = {
            vdp::ColorGradScreen::NBG0_RBG1, vdp::ColorGradScreen::NBG1_EXBG, vdp::ColorGradScreen::NBG2,
            vdp::ColorGradScreen::NBG3,      vdp::ColorGradScreen::RBG0,      vdp::ColorGradScreen::NBG0_RBG1,
        };
        for (uint32 i = 0; i < 6; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                if (regs2.colorCalcParams.colorGradEnable) {
                    printYesNo(regs2.colorCalcParams.colorGradScreen == kColorGradScreens[i]);
                } else {
                    ImGui::TextUnformatted("-");
                }
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        auto printMosaic = [&](const vdp::BGParams &params, bool rot) {
            if (params.mosaicEnable) {
                if (rot) {
                    ImGui::Text("%ux1", regs2.mosaicH);
                } else {
                    ImGui::Text("%ux%u", regs2.mosaicH, regs2.mosaicV);
                }
            } else {
                ImGui::TextUnformatted("-");
            }
        };

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(_("Mosaic"));
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                printMosaic(regs2.bgParams[i + 1], false);
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                printMosaic(regs2.bgParams[i], true);
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        auto printPriorityNum = [&](uint8 priorityNumber) { ImGui::Text("%u", priorityNumber); };

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(_("Priority number"));
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                printPriorityNum(regs2.bgParams[i + 1].priorityNumber);
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                printPriorityNum(regs2.bgParams[i].priorityNumber);
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        auto printPriorityMode = [&](vdp::PriorityMode priorityMode) {
            switch (priorityMode) {
            case vdp::PriorityMode::PerScreen: ImGui::TextUnformatted(_("Screen")); break;
            case vdp::PriorityMode::PerCharacter: ImGui::TextUnformatted(_("Character")); break;
            case vdp::PriorityMode::PerDot: ImGui::TextUnformatted(_("Dot")); break;
            default: ImGui::TextUnformatted(_("Illegal")); break;
            }
        };

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(_("Priority mode"));
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                printPriorityMode(regs2.bgParams[i + 1].priorityMode);
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                printPriorityMode(regs2.bgParams[i].priorityMode);
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        auto printColorCalcRatio = [&](const vdp::BGParams &params) {
            if (params.colorCalcEnable) {
                ImGui::Text("%u:%u", params.colorCalcRatio, 31u - params.colorCalcRatio);
            } else {
                ImGui::TextUnformatted("-");
            }
        };

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(_("Color calc. ratio"));
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                printColorCalcRatio(regs2.bgParams[i + 1]);
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                printColorCalcRatio(regs2.bgParams[i]);
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        auto printColorCalcMode = [&](const vdp::BGParams &params) {
            if (params.colorCalcEnable) {
                switch (params.specialColorCalcMode) {
                case vdp::SpecialColorCalcMode::PerScreen: ImGui::TextUnformatted(_("Screen")); break;
                case vdp::SpecialColorCalcMode::PerCharacter: ImGui::TextUnformatted(_("Character")); break;
                case vdp::SpecialColorCalcMode::PerDot: ImGui::TextUnformatted(_("Dot")); break;
                case vdp::SpecialColorCalcMode::ColorDataMSB: ImGui::TextUnformatted(_("Color MSB")); break;
                }
            } else {
                ImGui::TextUnformatted("-");
            }
        };

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(_("Color calc. mode"));
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                printColorCalcMode(regs2.bgParams[i + 1]);
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                printColorCalcMode(regs2.bgParams[i]);
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(_("LNCL insertion"));
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                printYesNo(regs2.bgParams[i + 1].lineColorScreenEnable);
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                printYesNo(regs2.bgParams[i].lineColorScreenEnable);
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        auto printSpecialFunctionSelect = [&](bool specialFunctionSelect) {
            if (specialFunctionSelect) {
                ImGui::TextUnformatted("B");
            } else {
                ImGui::TextUnformatted("A");
            }
        };

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(_("Special function"));
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                printSpecialFunctionSelect(regs2.bgParams[i + 1].specialFunctionSelect);
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                printSpecialFunctionSelect(regs2.bgParams[i].specialFunctionSelect);
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(_("Vert. cell scroll"));
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                if (i < 2) {
                    printYesNo(regs2.bgParams[i + 1].vcellScrollEnable);
                } else {
                    ImGui::TextUnformatted("-");
                }
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                ImGui::TextUnformatted("-");
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(_("X line scroll"));
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                if (i < 2) {
                    printYesNo(regs2.bgParams[i + 1].lineScrollXEnable);
                } else {
                    ImGui::TextUnformatted("-");
                }
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                ImGui::TextUnformatted("-");
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(_("Y line scroll"));
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                if (i < 2) {
                    printYesNo(regs2.bgParams[i + 1].lineScrollYEnable);
                } else {
                    ImGui::TextUnformatted("-");
                }
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                ImGui::TextUnformatted("-");
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(_("Line zoom"));
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                if (i < 2) {
                    printYesNo(regs2.bgParams[i + 1].lineZoomEnable);
                } else {
                    ImGui::TextUnformatted("-");
                }
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                ImGui::TextUnformatted("-");
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(_("Scroll X"));
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                ImGui::Text("%.3f",
                            (state2.nbgLayerStates[i].fracScrollX + regs2.bgParams[i + 1].scrollAmountH) / 256.0f);
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                ImGui::TextUnformatted("-");
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(_("Scroll Y"));
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                ImGui::Text("%.3f", regs2.bgParams[i + 1].scrollAmountV / 256.0f);
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                ImGui::TextUnformatted("-");
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(_("Scroll X inc."));
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                ImGui::Text("%.3f", state2.nbgLayerStates[i].scrollIncH / 256.0f);
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                ImGui::TextUnformatted("-");
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(_("Scroll Y inc."));
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                ImGui::Text("%.3f", regs2.bgParams[i + 1].scrollIncV / 256.0f);
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                ImGui::TextUnformatted("-");
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        auto printWindowSet = [&]<bool hasSpriteWindow>(const vdp::WindowSet<hasSpriteWindow> &windowSet) {
            WindowSetPrinter printer{windowSet.logic};
            printer.AppendWindow("0", windowSet.enabled[0], windowSet.inverted[0]);
            printer.AppendWindow("1", windowSet.enabled[1], windowSet.inverted[1]);
            if constexpr (hasSpriteWindow) {
                printer.AppendWindow("S", windowSet.enabled[2], windowSet.inverted[2]);
            }
            ImGui::Text("%s", printer.ToString().c_str());
        };

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(_("Windows"));
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                printWindowSet(regs2.bgParams[i + 1].windowSet);
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                printWindowSet(regs2.bgParams[i].windowSet);
            }
        }

        ImGui::EndTable();
    }
}

} // namespace app::ui
