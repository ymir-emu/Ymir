#include "vdp2_color_calc_params_view.hpp"

#include <app/ui/utils/debug/vdp_window_set_printer.hpp>

#include <ymir/hw/vdp/vdp.hpp>

#include <imgui.h>

#include <app/ui/i18n.hpp>

using namespace ymir;

namespace app::ui {


VDP2ColorCalcParamsView::VDP2ColorCalcParamsView(SharedContext &context, vdp::VDP &vdp)
    : m_context(context)
    , m_vdp(vdp) {}

void VDP2ColorCalcParamsView::Display() {
    auto &probe = m_vdp.GetProbe();
    const auto &regs2 = probe.GetVDP2Regs();

    if (regs2.colorCalcParams.colorGradEnable) {
        ImGui::TextUnformatted(_("Color gradation: enabled on "));
        ImGui::SameLine(0, 0);
        switch (regs2.colorCalcParams.colorGradScreen) {
        case vdp::ColorGradScreen::Sprite: ImGui::TextUnformatted("Sprite"); break;
        case vdp::ColorGradScreen::NBG0_RBG1:
            if (regs2.bgEnabled[5]) {
                ImGui::TextUnformatted("RBG1");
            } else {
                ImGui::TextUnformatted("NBG0");
            }
            break;
        case vdp::ColorGradScreen::NBG1_EXBG:
            if (regs2.EXTEN.EXBGEN) {
                ImGui::TextUnformatted("EXBG");
            } else {
                ImGui::TextUnformatted("NBG1");
            }
            break;
        case vdp::ColorGradScreen::NBG2: ImGui::TextUnformatted("NBG2"); break;
        case vdp::ColorGradScreen::NBG3: ImGui::TextUnformatted("NBG3"); break;
        case vdp::ColorGradScreen::RBG0: ImGui::TextUnformatted("RBG0"); break;
        case vdp::ColorGradScreen::Invalid3: ImGui::TextUnformatted("(invalid) [3]"); break;
        case vdp::ColorGradScreen::Invalid7: ImGui::TextUnformatted("(invalid) [7]"); break;
        }
    } else {
        ImGui::TextUnformatted(_("Color gradation: disabled"));
    }

    if (regs2.colorCalcParams.extendedColorCalcEnable) {
        ImGui::TextUnformatted(_("Using extended color calculations"));
        if (regs2.colorCalcParams.colorGradEnable) {
            ImGui::SameLine();
            ImGui::TextUnformatted(_("(ignored; color gradation takes precedence)"));
        }
    } else {
        ImGui::TextUnformatted(_("Using standard color calculations"));
    }

    if (regs2.colorCalcParams.useSecondScreenRatio) {
        ImGui::TextUnformatted(_("Using ratio from second screen"));
    } else {
        ImGui::TextUnformatted(_("Using ratio from top screen"));
    }

    if (regs2.colorCalcParams.useAdditiveBlend) {
        ImGui::TextUnformatted(_("Blend mode: Additive"));
    } else {
        ImGui::TextUnformatted(_("Blend mode: Alpha (color ratio)"));
    }

    auto printWindowSet = [&]<bool hasSpriteWindow>(const vdp::WindowSet<hasSpriteWindow> &windowSet) {
        WindowSetPrinter printer{windowSet.logic};
        printer.AppendWindow("0", windowSet.enabled[0], windowSet.inverted[0]);
        printer.AppendWindow("1", windowSet.enabled[1], windowSet.inverted[1]);
        if constexpr (hasSpriteWindow) {
            printer.AppendWindow("S", windowSet.enabled[2], windowSet.inverted[2]);
        }
        ImGui::Text("%s", printer.ToString().c_str());
    };

    ImGui::TextUnformatted(_("Windows: "));
    ImGui::SameLine(0, 0);
    printWindowSet(regs2.colorCalcParams.windowSet);
}

} // namespace app::ui
