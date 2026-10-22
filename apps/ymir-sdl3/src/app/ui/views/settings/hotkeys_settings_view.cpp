#include "hotkeys_settings_view.hpp"

#include <app/imgui_data.hpp>
#include <app/ui/i18n.hpp>

namespace app::ui {


HotkeysSettingsView::HotkeysSettingsView(SharedContext &context)
    : SettingsViewBase(context)
    , m_inputCaptureWidget(context, m_unboundActionsWidget)
    , m_unboundActionsWidget(context) {}

void HotkeysSettingsView::Display() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    auto &settings = GetSettings();

    if (ImGui::Button(_("Restore defaults"))) {
        m_unboundActionsWidget.Capture(settings.ResetHotkeys());
        MakeDirty();
    }

    ImGui::TextUnformatted(_("Left-click a button to assign a hotkey. Right-click to clear."));
    m_unboundActionsWidget.Display();
    if (ImGui::BeginTable("hotkeys", 2 + input::kNumBindsPerInput,
                          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn(_("Type"), ImGuiTableColumnFlags_WidthFixed, 80.0f * imguiData->displayScale);
        ImGui::TableSetupColumn(_("Command"), ImGuiTableColumnFlags_WidthFixed, 200.0f * imguiData->displayScale);
        for (size_t i = 0; i < input::kNumBindsPerInput; i++) {
            ImGui::TableSetupColumn(fmt::format(fmt::runtime(_("Hotkey {}")), i + 1).c_str(), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        }
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        auto drawRow = [&](input::InputBind &bind) {
            ImGui::TableNextRow();
            if (ImGui::TableNextColumn()) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(bind.action.group);
            }
            if (ImGui::TableNextColumn()) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(bind.action.name);
            }
            for (uint32 i = 0; i < input::kNumBindsPerInput; i++) {
                if (ImGui::TableNextColumn()) {
                    m_inputCaptureWidget.DrawInputBindButton(bind, i, nullptr);
                }
            }
        };

        auto &hotkeys = settings.hotkeys;

        drawRow(hotkeys.openSettings);
        drawRow(hotkeys.toggleWindowedVideoOutput);
        drawRow(hotkeys.toggleFullScreen);
        drawRow(hotkeys.showMessageHistory);
        drawRow(hotkeys.takeScreenshot);
        drawRow(hotkeys.exitApp);

        drawRow(hotkeys.toggleFrameRateOSD);
        drawRow(hotkeys.nextFrameRateOSDPos);
        drawRow(hotkeys.prevFrameRateOSDPos);
        drawRow(hotkeys.rotateScreenCW);
        drawRow(hotkeys.rotateScreenCCW);

        drawRow(hotkeys.toggleMute);
        drawRow(hotkeys.increaseVolume);
        drawRow(hotkeys.decreaseVolume);

        drawRow(hotkeys.loadDisc);
        drawRow(hotkeys.ejectDisc);
        drawRow(hotkeys.openCloseTray);

        drawRow(hotkeys.hardReset);
        drawRow(hotkeys.softReset);
        drawRow(hotkeys.resetButton);

        drawRow(hotkeys.turboSpeed);
        drawRow(hotkeys.turboSpeedHold);
        drawRow(hotkeys.toggleAlternateSpeed);
        drawRow(hotkeys.increaseSpeed);
        drawRow(hotkeys.decreaseSpeed);
        drawRow(hotkeys.increaseSpeedLarge);
        drawRow(hotkeys.decreaseSpeedLarge);
        drawRow(hotkeys.resetSpeed);
        drawRow(hotkeys.pauseResume);
        drawRow(hotkeys.fwdFrameStep);
        drawRow(hotkeys.revFrameStep);
        drawRow(hotkeys.toggleRewindBuffer);
        drawRow(hotkeys.rewind);

        drawRow(hotkeys.toggleDebugTrace);
        drawRow(hotkeys.dumpMemory);

        drawRow(hotkeys.saveStates.quickLoad);
        drawRow(hotkeys.saveStates.quickSave);

        drawRow(hotkeys.saveStates.select1);
        drawRow(hotkeys.saveStates.select2);
        drawRow(hotkeys.saveStates.select3);
        drawRow(hotkeys.saveStates.select4);
        drawRow(hotkeys.saveStates.select5);
        drawRow(hotkeys.saveStates.select6);
        drawRow(hotkeys.saveStates.select7);
        drawRow(hotkeys.saveStates.select8);
        drawRow(hotkeys.saveStates.select9);
        drawRow(hotkeys.saveStates.select10);

        drawRow(hotkeys.saveStates.load1);
        drawRow(hotkeys.saveStates.load2);
        drawRow(hotkeys.saveStates.load3);
        drawRow(hotkeys.saveStates.load4);
        drawRow(hotkeys.saveStates.load5);
        drawRow(hotkeys.saveStates.load6);
        drawRow(hotkeys.saveStates.load7);
        drawRow(hotkeys.saveStates.load8);
        drawRow(hotkeys.saveStates.load9);
        drawRow(hotkeys.saveStates.load10);

        drawRow(hotkeys.saveStates.save1);
        drawRow(hotkeys.saveStates.save2);
        drawRow(hotkeys.saveStates.save3);
        drawRow(hotkeys.saveStates.save4);
        drawRow(hotkeys.saveStates.save5);
        drawRow(hotkeys.saveStates.save6);
        drawRow(hotkeys.saveStates.save7);
        drawRow(hotkeys.saveStates.save8);
        drawRow(hotkeys.saveStates.save9);
        drawRow(hotkeys.saveStates.save10);

        drawRow(hotkeys.saveStates.undoSave);
        drawRow(hotkeys.saveStates.undoLoad);

        m_inputCaptureWidget.DrawCapturePopup();

        ImGui::EndTable();
    }
}

} // namespace app::ui
