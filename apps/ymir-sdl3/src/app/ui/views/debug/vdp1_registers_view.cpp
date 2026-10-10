#include "vdp1_registers_view.hpp"

#include <ymir/hw/vdp/vdp.hpp>

#include <imgui.h>

using namespace ymir;

namespace app::ui {

VDP1RegistersView::VDP1RegistersView(SharedContext &context, vdp::VDP &vdp)
    : m_context(context)
    , m_vdp(vdp) {}

void VDP1RegistersView::Display() {
    auto &probe = m_vdp.GetProbe();
    auto reso = probe.GetResolution();
    auto interlace = probe.GetInterlaceMode();
    auto &regs1 = probe.GetVDP1Regs();
    auto &regs2 = probe.GetVDP2Regs();

    static constexpr const char *kInterlaceNames[]{"progressive", "(invalid)", "single-density interlace",
                                                   "double-density interlace"};

    auto checkbox = [](const char *name, bool value) { ImGui::Checkbox(name, &value); };

    checkbox(fmt::format("[TVMR.TVM:0] Pixel data: {} bits", regs1.tvmr.pixel8Bits ? 8u : 16u).c_str(),
             regs1.tvmr.pixel8Bits);
    ImGui::Text("VDP2 sprite data readout size: %u bits", (regs2.spriteParams.type >= 8 ? 8u : 16u));
    checkbox("[TVMR.TVM:1] Rotation mode", regs1.tvmr.fbRotEnable);
    checkbox("[TVMR.TVM:2] HDTV mode", regs1.tvmr.hdtvEnable);
    checkbox("[FBCR.DIE] Double interlace enable", regs1.dblInterlaceEnable);
    checkbox("[FBCR.DIL] Double interlace draw even/odd line", regs1.dblInterlaceDrawLine);
    ImGui::Text("Framebuffer size: %ux%u", regs1.tvmr.fbSizeH, regs1.tvmr.fbSizeV);
    ImGui::Text("VDP2 resolution: %ux%u %s", reso.width, reso.height, kInterlaceNames[static_cast<uint8>(interlace)]);

    ImGui::Separator();

    ImGui::Text("Current display framebuffer: %u", probe.GetSpriteDisplayFB());
    checkbox("[TVMR.VBE] VBlank Erase", regs1.tvmr.vblankErase);
    checkbox("[FBCR.FCT] Framebuffer swap trigger", regs1.fbSwapTrigger);
    checkbox("[FBCR.FCM] Framebuffer swap mode", regs1.fbSwapMode);
    ImGui::Indent();
    {
        checkbox("FBCR changed", regs1.fbParamsChanged);
    }
    ImGui::Unindent();
    ImGui::Text("[FBCR.PTM] Plot trigger mode: %u", regs1.plotTrigger);
    ImGui::Text("[EWDR] Erase write value: 0x%04X", regs1.eraseWriteValue);
    ImGui::Text("[EWLR/EWRR] Erase window: %ux%u - %ux%u", regs1.eraseX1, regs1.eraseY1, regs1.eraseX3, regs1.eraseY3);
    ImGui::Indent();
    {
        ImGui::Text("Latched erase write value: 0x%04X", probe.GetLatchedEraseWriteValue());
        ImGui::Text("Latched erase window: %ux%u - %ux%u", probe.GetLatchedEraseX1(), probe.GetLatchedEraseY1(),
                    probe.GetLatchedEraseX3(), probe.GetLatchedEraseY3());
    }
    ImGui::Unindent();
    checkbox("[FBCR.EOS] High-speed shrink even/odd coordinate select", regs1.evenOddCoordSelect);

    ImGui::Separator();

    checkbox("[EDSR.CEF] Current frame ended", regs1.currFrameEnded);
    checkbox("[EDSR.BEF] Previous frame ended", regs1.prevFrameEnded);
    ImGui::Text("[COPR] Current frame command address: 0x%05X", regs1.currCommandAddress);
    ImGui::Text("[LOPR] Previous frame command address: 0x%05X", regs1.prevCommandAddress);
    ImGui::Indent();
    {
        ImGui::Text("Return address: 0x%05X", regs1.returnAddress);
    }
    ImGui::Unindent();
}

} // namespace app::ui
