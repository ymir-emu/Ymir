#include "sh2_debug_toolbar_view.hpp"

#include <ymir/hw/sh2/sh2.hpp>

#include <app/events/emu_event_factory.hpp>
#include <app/events/gui_event_factory.hpp>

#include <app/ui/fonts/IconsMaterialSymbols.h>

#include <app/ui/model/debug/sh2_debugger_model.hpp>

#include <app/ui/widgets/common_widgets.hpp>
#include <app/ui/widgets/debug_widgets.hpp>

#include <app/imgui_data.hpp>
#include <app/ui/i18n.hpp>

#include <imgui.h>

#include <cstdint>

using namespace ymir;

namespace app::ui {


SH2DebugToolbarView::SH2DebugToolbarView(SharedContext &context, sh2::SH2 &sh2, SH2DebuggerModel &model)
    : m_context(context)
    , m_sh2(sh2)
    , m_model(model)
    , m_disasmDumpView(context, sh2) {}

void SH2DebugToolbarView::Display() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();

    ImGui::BeginGroup();

    ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
    const float hexCharWidth = ImGui::CalcTextSize("F").x;
    ImGui::PopFont();
    const float framePadding = ImGui::GetStyle().FramePadding.x;
    const float regFieldWidth = framePadding * 2 + hexCharWidth * 8;

    widgets::DebugWarning(m_context);

    const bool debugTracing = m_context.saturn.IsDebugTracingEnabled();
    const bool master = m_sh2.IsMaster();
    const bool enabled = master || m_context.saturn.IsSlaveSH2Enabled();
    const bool paused = m_context.paused;
    auto &probe = m_sh2.GetProbe();

    ImGui::BeginDisabled(!enabled);
    {
        if (ImGui::Button(ICON_MS_STEP)) {
            m_context.EnqueueEvent(master ? events::emu::StepMSH2() : events::emu::StepSSH2());
        }
        if (ImGui::BeginItemTooltip()) {
            ImGui::TextUnformatted(_("Step (F7, S)"));
            ImGui::EndTooltip();
        }

        ImGui::SameLine();

        ImGui::BeginDisabled(paused);
        if (ImGui::Button(ICON_MS_PAUSE)) {
            m_context.EnqueueEvent(events::emu::SetPaused(true));
        }
        ImGui::EndDisabled();
        if (ImGui::BeginItemTooltip()) {
            ImGui::TextUnformatted(_("Pause (Space, R)"));
            ImGui::EndTooltip();
        }

        ImGui::SameLine();

        ImGui::BeginDisabled(!paused);
        if (ImGui::Button(ICON_MS_PLAY_ARROW)) {
            m_context.EnqueueEvent(events::emu::SetPaused(false));
        }
        ImGui::EndDisabled();
        if (ImGui::BeginItemTooltip()) {
            ImGui::TextUnformatted(_("Resume (Space, R)"));
            ImGui::EndTooltip();
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();

    if (ImGui::Button(ICON_MS_REPLAY)) {
        m_context.EnqueueEvent(events::emu::HardReset());
    }
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted(_("Hard reset (Ctrl+R)"));
        ImGui::EndTooltip();
    }

    ImGui::SameLine();

    if (ImGui::Button(ICON_MS_MASKED_TRANSITIONS)) {
        m_context.EnqueueEvent(events::gui::OpenSH2BreakpointsWindow(m_sh2.IsMaster()));
    }
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted(_("Breakpoints (Ctrl+F9)"));
        ImGui::EndTooltip();
    }

    ImGui::SameLine();

    if (ImGui::Button(ICON_MS_VISIBILITY)) {
        m_context.EnqueueEvent(events::gui::OpenSH2WatchpointsWindow(m_sh2.IsMaster()));
    }
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted(_("Watchpoints (Ctrl+Shift+F9)"));
        ImGui::EndTooltip();
    }

    ImGui::SameLine();
    if (ImGui::Button(ICON_MS_FILE_DOWNLOAD "##dump_disasm_range")) {
        m_disasmDumpView.OpenPopup();
    }
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted(_("Dump disassembly range (Ctrl+D)"));
        ImGui::EndTooltip();
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_D)) {
        m_disasmDumpView.OpenPopup();
    }
    m_disasmDumpView.Display();

    if (!master) {
        ImGui::SameLine();
        bool slaveSH2Enabled = m_context.saturn.IsSlaveSH2Enabled();
        if (ImGui::Checkbox(_("Enabled"), &slaveSH2Enabled)) {
            m_context.saturn.SetSlaveSH2Enabled(slaveSH2Enabled);
        }
    }

    ImGui::SameLine();
    if (!debugTracing) {
        ImGui::BeginDisabled();
    }
    bool suspended = m_sh2.IsCPUSuspended();
    if (ImGui::Checkbox(_("Suspended"), &suspended)) {
        m_sh2.SetCPUSuspended(suspended);
    }
    widgets::ExplanationTooltip("Disables the CPU while in debug mode.");
    if (!debugTracing) {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    bool asleep = probe.GetSleepState();
    if (ImGui::Checkbox(_("Asleep"), &asleep)) {
        probe.SetSleepState(asleep);
    }
    widgets::ExplanationTooltip("Whether the CPU is in standby or sleep mode due to executing the SLEEP instruction.");

    auto doJump = [&] {
        // Align to even addresses
        m_model.jumpAddress = m_model.jumpAddress & ~1u;
        m_model.JumpTo(m_model.jumpAddress);
    };

    auto doJumpToPC = [&] {
        // Align to even addresses
        m_model.jumpAddress = probe.PC() & ~1u;
        m_model.JumpToPC();
    };

    // Input field to jump to address
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(_("Go to:"));

    ImGui::SameLine();
    if (ImGui::Button("PC##goto")) {
        doJumpToPC();
    }

    ImGui::SameLine();
    if (ImGui::Button("PR##goto")) {
        m_model.jumpAddress = probe.PR();
        doJump();
    }

    ImGui::SameLine();
    ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
    ImGui::SetNextItemWidth(regFieldWidth);
    ImGui::InputScalar("##goto_address", ImGuiDataType_U32, &m_model.jumpAddress, nullptr, nullptr, "%08X",
                       ImGuiInputTextFlags_CharsHexadecimal);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        doJump();
    }
    ImGui::PopFont();

    ImGui::SameLine();
    if (ImGui::Button(_("Jump"))) {
        doJump();
    }

    ImGui::SameLine();
    ImGui::Checkbox(_("Follow PC"), &m_model.followPC);

    ImGui::SameLine();
    ImGui::Checkbox(_("on events"), &m_model.followPCOnEvents);
    widgets::ExplanationTooltip("Causes the cursor to jump to PC when breakpoints and watchpoints are hit.");

    ImGui::EndGroup();
}

} // namespace app::ui
