#include "backup_ram_manager_window.hpp"

#include <ymir/hw/cart/cart.hpp>
#include <ymir/sys/memory.hpp>

#include <app/imgui_data.hpp>
#include <app/ui/i18n.hpp>

#include <imgui.h>

using namespace ymir;

namespace app::ui {


BackupMemoryManagerWindow::BackupMemoryManagerWindow(SharedContext &context)
    : WindowBase(context)
    , m_sysBupView(context, "System memory", false)
    , m_cartBupView(context, "Cartridge memory", true) {

    m_sysBupView.SetBackupMemory(&m_context.saturn.GetSystemMemory().GetInternalBackupRAM());

    m_windowConfig.name = _("Backup memory manager");
}

void BackupMemoryManagerWindow::PrepareWindow() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    ImGui::SetNextWindowSizeConstraints(ImVec2(1175 * imguiData->displayScale, 340 * imguiData->displayScale),
                                        ImVec2(1175 * imguiData->displayScale, 960 * imguiData->displayScale));
}

void BackupMemoryManagerWindow::DrawContents() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();

    if (ImGui::BeginTable("bup_mgr", 3,
                          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("##sys_bup", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("##btns", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("##cart_bup", ImGuiTableColumnFlags_WidthStretch, 1.0f);

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::SeparatorText(_("System memory"));
            ImGui::PushID("sys_bup");
            m_sysBupView.Display();
            ImGui::PopID();
        }
        if (ImGui::TableNextColumn()) {
            const auto avail = ImGui::GetContentRegionAvail();
            const float textHeight = ImGui::GetTextLineHeightWithSpacing();
            const float buttonHeight = ImGui::GetFrameHeightWithSpacing();
            const float totalHeight = textHeight + buttonHeight * 4;

            const bool hasCartBup = m_cartBupView.HasBackupMemory();
            const bool hasCartBupSelection = m_cartBupView.HasSelection();
            const bool hasSysBupSelection = m_sysBupView.HasSelection();

            // Center vertically
            ImGui::Dummy(ImVec2(0, (avail.y - totalHeight) * 0.5f));
            ImGui::TextUnformatted(_("Copy"));

            if (!hasCartBup) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("<<", ImVec2(35 * imguiData->displayScale, 0 * imguiData->displayScale))) {
                std::unique_lock lock{m_context.locks.cart};
                auto files = m_cartBupView.ExportAll();
                m_sysBupView.ImportAll(files);
            }
            if (!hasCartBup) {
                ImGui::EndDisabled();
            }

            if (!hasCartBup || !hasCartBupSelection) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("<", ImVec2(35 * imguiData->displayScale, 0 * imguiData->displayScale))) {
                std::unique_lock lock{m_context.locks.cart};
                auto files = m_cartBupView.ExportSelected();
                m_sysBupView.ImportAll(files);
            }
            if (!hasCartBup || !hasCartBupSelection) {
                ImGui::EndDisabled();
            }

            if (!hasCartBup || !hasSysBupSelection) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button(">", ImVec2(35 * imguiData->displayScale, 0 * imguiData->displayScale))) {
                std::unique_lock lock{m_context.locks.cart};
                auto files = m_sysBupView.ExportSelected();
                m_cartBupView.ImportAll(files);
            }
            if (!hasCartBup || !hasSysBupSelection) {
                ImGui::EndDisabled();
            }

            if (!hasCartBup) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button(">>", ImVec2(35 * imguiData->displayScale, 0 * imguiData->displayScale))) {
                std::unique_lock lock{m_context.locks.cart};
                auto files = m_sysBupView.ExportAll();
                m_cartBupView.ImportAll(files);
            }
            if (!hasCartBup) {
                ImGui::EndDisabled();
            }
        }
        if (ImGui::TableNextColumn()) {
            ImGui::SeparatorText(_("Cartridge memory"));

            ImGui::PushID("cart_bup");
            std::unique_lock lock{m_context.locks.cart};
            if (auto *bupCart = m_context.saturn.GetCartridge().As<cart::CartType::BackupMemory>()) {
                m_cartBupView.SetBackupMemory(&bupCart->GetBackupMemory());
            } else {
                m_cartBupView.SetBackupMemory(nullptr);
            }
            m_cartBupView.Display();
            ImGui::PopID();
        }

        ImGui::EndTable();
    }
}

} // namespace app::ui
