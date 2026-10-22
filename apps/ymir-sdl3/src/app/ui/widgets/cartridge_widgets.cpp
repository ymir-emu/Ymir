#include "cartridge_widgets.hpp"

#include <app/ui/i18n.hpp>

#include <ymir/hw/cart/cart.hpp>

#include <imgui.h>

namespace app::ui::widgets {


void CartridgeInfo(SharedContext &ctx) {
    using namespace ymir;

    std::unique_lock lock{ctx.locks.cart};
    auto &cart = ctx.saturn.GetCartridge();
    switch (cart.GetType()) {
    case cart::CartType::None: ImGui::TextUnformatted(_("None")); break;
    case cart::CartType::BackupMemory: //
    {
        auto &bupCart = *cart.As<cart::CartType::BackupMemory>();
        ImGui::Text(_("%u Mbit Backup RAM"), bupCart.GetBackupMemory().Size() * 8u / 1024u / 1024u);
        break;
    }
    case cart::CartType::DRAM8Mbit: ImGui::TextUnformatted("8 Mbit DRAM"); break;
    case cart::CartType::DRAM32Mbit: ImGui::TextUnformatted("32 Mbit DRAM"); break;
    case cart::CartType::DRAM48Mbit: ImGui::TextUnformatted("48 Mbit DRAM"); break;
    case cart::CartType::ROM: ImGui::TextUnformatted("16 Mbit ROM"); break;
    }
}

} // namespace app::ui::widgets
