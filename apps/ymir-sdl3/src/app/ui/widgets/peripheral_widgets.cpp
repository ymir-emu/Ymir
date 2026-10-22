#include "peripheral_widgets.hpp"

#include <app/events/gui_event_factory.hpp>

#include <app/ui/i18n.hpp>

#include <app/settings.hpp>

#include <ymir/hw/smpc/smpc.hpp>

using namespace ymir;

namespace app::ui::widgets {


bool PeripheralSelector(SharedContext &ctx, uint32 portIndex) {
    std::unique_lock lock{ctx.locks.peripherals};

    const bool isPort1 = portIndex == 1;
    auto &smpc = ctx.saturn.GetSMPC();
    auto &port = isPort1 ? smpc.GetPeripheralPort1() : smpc.GetPeripheralPort2();
    auto &periph = port.GetPeripheral();
    auto &settings = ctx.serviceLocator.GetRequired<Settings>().input.ports[portIndex - 1];
    const bool isNone = periph.GetType() == peripheral::PeripheralType::None;

    bool changed = false;

    if (ImGui::BeginTable(fmt::format("periph_table_{}", portIndex).c_str(), 3, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("##input", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##config", ImGuiTableColumnFlags_WidthFixed);

        for (uint32 slotIndex = 0; slotIndex < 1; slotIndex++) {
            ImGui::TableNextRow();
            if (ImGui::TableNextColumn()) {
                ImGui::AlignTextToFramePadding();
                // TODO: multitap -- ImGui::Text("Slot %u:", slotIndex);
                ImGui::TextUnformatted(_("Peripheral:"));
            }
            if (ImGui::TableNextColumn()) {
                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo(fmt::format("##periph_{}_{}", portIndex, slotIndex).c_str(),
                                      periph.GetName().data())) {
                    for (auto type : peripheral::kTypes) {
                        if (ImGui::Selectable(peripheral::GetPeripheralName(type).data(), periph.GetType() == type)) {
                            settings.type = type;
                            changed = true;
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            if (ImGui::TableNextColumn()) {
                if (isNone) {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button(fmt::format("{}##{}_{}", _("Configure"), portIndex, slotIndex).c_str())) {
                    ctx.EnqueueEvent(events::gui::OpenPeripheralBindsEditor(portIndex - 1, slotIndex));
                }
                if (isNone) {
                    ImGui::EndDisabled();
                }
            }
        }
        ImGui::EndTable();
    }

    return changed;
}

bool Port1PeripheralSelector(SharedContext &ctx) {
    return PeripheralSelector(ctx, 1);
}

bool Port2PeripheralSelector(SharedContext &ctx) {
    return PeripheralSelector(ctx, 2);
}

} // namespace app::ui::widgets
