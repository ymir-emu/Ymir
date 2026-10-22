#include "cartridge_settings_view.hpp"

#include <ymir/hw/cart/cart.hpp>
#include <ymir/hw/cdblock/cdblock.hpp>
#include <ymir/sys/backup_ram.hpp>

#include <ymir/db/game_db.hpp>

#include <app/ui/i18n.hpp>
#include <app/ui/widgets/cartridge_widgets.hpp>
#include <app/ui/widgets/common_widgets.hpp>

#include <app/events/emu_event_factory.hpp>
#include <app/events/gui_event_factory.hpp>

#include <app/imgui_data.hpp>

#include <util/file_loader.hpp>
#include <util/sdl_file_dialog.hpp>

#include <misc/cpp/imgui_stdlib.h>

#include <fmt/std.h>

#include <SDL3/SDL_misc.h>

using namespace ymir;

namespace app::ui {


static const char *GetCartTypeName(Settings::Cartridge::Type type) {
    switch (type) {
    case Settings::Cartridge::Type::None: return "None";
    case Settings::Cartridge::Type::BackupRAM: return "Backup RAM";
    case Settings::Cartridge::Type::DRAM: return "DRAM";
    case Settings::Cartridge::Type::ROM: return "ROM";
    default: return "Unknown";
    }
}

// ---------------------------------------------------------------------------------------------------------------------

CartridgeSettingsView::CartridgeSettingsView(SharedContext &context)
    : SettingsViewBase(context) {}

void CartridgeSettingsView::Display() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    auto &settings = GetSettings().cartridge;

    static constexpr Settings::Cartridge::Type kCartTypes[] = {
        Settings::Cartridge::Type::None,
        Settings::Cartridge::Type::BackupRAM,
        Settings::Cartridge::Type::DRAM,
        Settings::Cartridge::Type::ROM,
    };

    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);

    ImGui::TextUnformatted(_("Current cartridge: "));
    ImGui::SameLine(0, 0);
    widgets::CartridgeInfo(m_context);
    {
        std::unique_lock lock{m_context.locks.cart};
        auto &cart = m_context.saturn.GetCartridge();
        if (cart.GetType() == cart::CartType::BackupMemory) {
            auto &bupCart = *cart.As<cart::CartType::BackupMemory>();
            ImGui::Text("Image path: %s", fmt::format("{}", bupCart.GetBackupMemory().GetPath()).c_str());
        }
    }

    MakeDirty(ImGui::Checkbox(_("Automatically switch to recommended cartridges"), &settings.autoLoadGameCarts));
    widgets::ExplanationTooltip(
        fmt::format("Certain games require specific cartridges to work:\n"
                    "- The King of Fighters '95 and Ultraman need their respective ROM cartridges\n"
                    "- Some other games need a DRAM cartridge to start up\n"
                    "\n"
                    "With this option enabled, the correct cartridge is automatically inserted when a game with these "
                    "requirements is loaded.\n"
                    "\n"
                    "For ROM cartridges, make sure you add the required files to {}.",
                    m_context.profile.GetPath(ProfilePath::ROMCartImages))
            .c_str());

    if (ImGui::Button(_("Open ROM cartridge images directory"))) {
        SDL_OpenURL(fmt::format("file:///{}", m_context.profile.GetPath(ProfilePath::ROMCartImages)).c_str());
    }

    ImGui::Separator();

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(_("Cartridge type:"));
    ImGui::SameLine();
    if (ImGui::BeginCombo("##cart_type", GetCartTypeName(settings.type), ImGuiComboFlags_WidthFitPreview)) {
        for (auto type : kCartTypes) {
            if (MakeDirty(ImGui::Selectable(GetCartTypeName(type), type == settings.type))) {
                settings.type = type;
            }
        }

        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button(_("Insert"))) {
        m_context.EnqueueEvent(events::emu::InsertCartridgeFromSettings());
    }

    const db::GameInfo *gameInfo = nullptr;
    {
        std::unique_lock lock{m_context.locks.disc};
        const auto &discHeader = m_context.saturn.GetDiscHeader();
        if (discHeader.IsValid()) {
            gameInfo = db::GetGameInfo(discHeader.productNumber, m_context.saturn.GetDiscHash());
        }
    }
    if (gameInfo != nullptr) {
        cart::CartType wantedCartType;
        switch (gameInfo->GetCartridge()) {
        case db::Cartridge::DRAM8Mbit: wantedCartType = cart::CartType::DRAM8Mbit; break;
        case db::Cartridge::DRAM32Mbit: wantedCartType = cart::CartType::DRAM32Mbit; break;
        case db::Cartridge::DRAM48Mbit: wantedCartType = cart::CartType::DRAM48Mbit; break;
        case db::Cartridge::ROM_KOF95: wantedCartType = cart::CartType::ROM; break;
        case db::Cartridge::ROM_Ultraman: wantedCartType = cart::CartType::ROM; break;
        case db::Cartridge::BackupRAM: wantedCartType = cart::CartType::BackupMemory; break;
        default: wantedCartType = cart::CartType::None; break;
        }

        cart::CartType currCartType;
        {
            std::unique_lock lock{m_context.locks.cart};
            currCartType = m_context.saturn.GetCartridge().GetType();
        }

        if (wantedCartType != cart::CartType::None && currCartType != wantedCartType) {
            ImGui::AlignTextToFramePadding();

            const auto color = imguiData->colors.notice;

            switch (gameInfo->GetCartridge()) {
            case db::Cartridge::DRAM8Mbit:
                ImGui::TextColored(color, "The currently loaded game requires an 8 Mbit DRAM cartridge.");
                break;
            case db::Cartridge::DRAM32Mbit:
                ImGui::TextColored(color, "The currently loaded game requires a 32 Mbit DRAM cartridge.");
                break;
            case db::Cartridge::DRAM48Mbit:
                ImGui::TextColored(color, "The currently loaded game requires a 48 Mbit DRAM dev cartridge.");
                break;
            case db::Cartridge::ROM_KOF95:
                ImGui::TextColored(color, "The currently loaded game requires the King of Fighters '95 ROM cartridge.");
                break;
            case db::Cartridge::ROM_Ultraman:
                ImGui::TextColored(color, "The currently loaded game requires the Ultraman ROM cartridge.");
                break;
            case db::Cartridge::BackupRAM:
                ImGui::TextColored(color, "A Backup RAM cartridge is recommended for this game.");
                break;
            default: break;
            }
            ImGui::SameLine();

            if (MakeDirty(ImGui::Button("Insert##recommended_cart"))) {
                m_context.EnqueueEvent(events::gui::LoadRecommendedGameCartridge());
            }

            if (gameInfo->cartReason) {
                ImGui::TextColored(color, "Reason: %s", gameInfo->cartReason);
            }
        }
    }

    switch (settings.type) {
    case Settings::Cartridge::Type::None: break;
    case Settings::Cartridge::Type::BackupRAM: DrawBackupRAMSettings(); break;
    case Settings::Cartridge::Type::DRAM: DrawDRAMSettings(); break;
    case Settings::Cartridge::Type::ROM: DrawROMSettings(); break;
    }

    ImGui::PopTextWrapPos();
}

void CartridgeSettingsView::DrawBackupRAMSettings() {
    auto &settings = GetSettings().cartridge.backupRAM;

    const float paddingWidth = ImGui::GetStyle().FramePadding.x;
    const float itemSpacingWidth = ImGui::GetStyle().ItemSpacing.x;
    const float fileSelectorButtonWidth = ImGui::CalcTextSize("...").x + paddingWidth * 2;

    using BUPCap = Settings::Cartridge::BackupRAM::Capacity;

    uint32 currSize = 0;
    std::filesystem::path currPath = "";
    {
        std::unique_lock lock{m_context.locks.cart};
        auto *cart = m_context.saturn.GetCartridge().As<cart::CartType::BackupMemory>();
        if (cart != nullptr) {
            auto &bupMem = cart->GetBackupMemory();
            currSize = bupMem.Size();
            currPath = bupMem.GetPath();
        }
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Capacity:");
    widgets::ExplanationTooltip(
        "This will automatically adjust if you load an existing image from the file selector below.");
    ImGui::SameLine();
    if (ImGui::BeginCombo("##bup_capacity", BupCapacityLongName(settings.capacity), ImGuiComboFlags_WidthFitPreview)) {
        for (auto cap : {BUPCap::_4Mbit, BUPCap::_8Mbit, BUPCap::_16Mbit, BUPCap::_32Mbit}) {
            if (MakeDirty(ImGui::Selectable(BupCapacityLongName(cap), settings.capacity == cap))) {
                settings.capacity = cap;
            }
        }
        ImGui::EndCombo();
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Image path:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-(fileSelectorButtonWidth + itemSpacingWidth * 2));
    std::string imagePath = fmt::format("{}", settings.imagePath);
    std::string defaultPath =
        fmt::format("{}", m_context.profile.GetPath(ProfilePath::PersistentState) /
                              fmt::format("bup-ext-{}M.bin", CapacityToSize(settings.capacity) * 8 / 1024 / 1024));
    if (MakeDirty(ImGui::InputTextWithHint("##bup_image_path", defaultPath.c_str(), &imagePath,
                                           ImGuiInputTextFlags_ElideLeft))) {
        settings.imagePath = std::u8string{imagePath.begin(), imagePath.end()};
    }
    ImGui::SameLine();
    if (ImGui::Button("...##bup_image_path")) {
        m_context.EnqueueEvent(events::gui::SaveFile({
            .dialogTitle = "Load backup memory image",
            .defaultPath = settings.imagePath.empty()
                               ? m_context.profile.GetPath(ProfilePath::PersistentState) / "bup-ext.bin"
                               : settings.imagePath,
            .filters = {{"Backup memory image files (*.bin, *.sav)", "bin;sav"}, {"All files (*.*)", "*"}},
            .userdata = this,
            .callback = util::WrapSingleSelectionCallback<&CartridgeSettingsView::ProcessLoadBackupImage,
                                                          &util::NoopCancelFileDialogCallback,
                                                          &CartridgeSettingsView::ProcessLoadBackupImageError>,
        }));
    }

    if (ImGui::Button("Open backup memory manager")) {
        m_context.EnqueueEvent(events::gui::OpenBackupMemoryManager());
    }

    if (currSize != 0 && CapacityToSize(settings.capacity) != currSize && !currPath.empty() &&
        !settings.imagePath.empty() && currPath == settings.imagePath) {
        ImGui::TextUnformatted("WARNING: Changing the size of the backup memory image will format it!");
    }
}

void CartridgeSettingsView::DrawDRAMSettings() {
    auto &settings = GetSettings().cartridge.dram;

    using DRAMCap = Settings::Cartridge::DRAM::Capacity;

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Capacity:");
    ImGui::SameLine();
    if (MakeDirty(ImGui::RadioButton("48 Mbit (6 MiB) (dev)", settings.capacity == DRAMCap::_48Mbit))) {
        settings.capacity = DRAMCap::_48Mbit;
    }
    ImGui::SameLine();
    if (MakeDirty(ImGui::RadioButton("32 Mbit (4 MiB)", settings.capacity == DRAMCap::_32Mbit))) {
        settings.capacity = DRAMCap::_32Mbit;
    }
    ImGui::SameLine();
    if (MakeDirty(ImGui::RadioButton("8 Mbit (1 MiB)", settings.capacity == DRAMCap::_8Mbit))) {
        settings.capacity = DRAMCap::_8Mbit;
    }
}

void CartridgeSettingsView::DrawROMSettings() {
    auto &settings = GetSettings().cartridge.rom;

    const float paddingWidth = ImGui::GetStyle().FramePadding.x;
    const float itemSpacingWidth = ImGui::GetStyle().ItemSpacing.x;
    const float fileSelectorButtonWidth = ImGui::CalcTextSize("...").x + paddingWidth * 2;

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Image path:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-(fileSelectorButtonWidth + itemSpacingWidth * 2));
    std::string imagePath = fmt::format("{}", settings.imagePath);
    if (MakeDirty(ImGui::InputText("##rom_image_path", &imagePath, ImGuiInputTextFlags_ElideLeft))) {
        settings.imagePath = std::u8string{imagePath.begin(), imagePath.end()};
    }
    ImGui::SameLine();
    if (ImGui::Button("...##rom_image_path")) {
        m_context.EnqueueEvent(events::gui::OpenFile({
            .dialogTitle = "Load ROM cartridge image",
            .defaultPath =
                settings.imagePath.empty() ? m_context.profile.GetPath(ProfilePath::ROMCartImages) : settings.imagePath,
            .filters = {{"ROM cartridge image files (*.bin, *.ic1)", "bin;ic1"}, {"All files (*.*)", "*"}},
            .userdata = this,
            .callback = util::WrapSingleSelectionCallback<&CartridgeSettingsView::ProcessLoadROMImage,
                                                          &util::NoopCancelFileDialogCallback,
                                                          &CartridgeSettingsView::ProcessLoadROMImageError>,
        }));
    }
}

void CartridgeSettingsView::ProcessLoadBackupImage(void *userdata, std::filesystem::path file, int filter) {
    static_cast<CartridgeSettingsView *>(userdata)->LoadBackupImage(file);
}

void CartridgeSettingsView::ProcessLoadBackupImageError(void *userdata, const char *message, int filter) {
    static_cast<CartridgeSettingsView *>(userdata)->ShowLoadBackupImageError(message);
}

void CartridgeSettingsView::LoadBackupImage(std::filesystem::path file) {
    auto &settings = GetSettings().cartridge.backupRAM;

    // TODO: rework this entire process
    // - everything here should be done by the emulator event
    // - it should also reuse the current backup cartridge and ask the bup::BackupMemory to LoadFrom/CreateFrom
    //   - CreateFrom should temporarily disconnect the file in order to modify its size if it's trying to change
    //   the
    //     file it has already loaded

    std::error_code error{};
    bup::BackupMemory bupMem{};
    if (std::filesystem::is_regular_file(file)) {
        // The user selected an existing image. Make sure it's a proper backup image.
        const auto result = bupMem.LoadFrom(file, false, error);
        switch (result) {
        case bup::BackupMemoryImageLoadResult::Success:
            settings.imagePath = file;
            settings.capacity = SizeToCapacity(bupMem.Size());
            m_context.EnqueueEvent(events::emu::InsertCartridgeFromSettings());
            MakeDirty();
            break;
        case bup::BackupMemoryImageLoadResult::FilesystemError:
            if (error) {
                m_context.EnqueueEvent(
                    events::gui::ShowError(fmt::format("Could not load backup memory image: {}", error.message())));
            } else {
                m_context.EnqueueEvent(events::gui::ShowError(
                    fmt::format("Could not load backup memory image: Unspecified file system error")));
            }
            break;
        case bup::BackupMemoryImageLoadResult::InvalidSize:
            m_context.EnqueueEvent(
                events::gui::ShowError(fmt::format("Could not load backup memory image: Invalid image size")));
            break;
        default:
            m_context.EnqueueEvent(
                events::gui::ShowError(fmt::format("Could not load backup memory image: Unexpected error")));
            break;
        }
    } else {
        // The user wants to create a new image file
        if (file.empty()) {
            file = fmt::format("{}",
                               m_context.profile.GetPath(ProfilePath::PersistentState) /
                                   fmt::format("bup-ext-{}M.bin", CapacityToSize(settings.capacity) * 8 / 1024 / 1024));
        }
        bupMem.CreateFrom(file, false, error, CapacityToBupSize(settings.capacity));
        if (error) {
            m_context.EnqueueEvent(
                events::gui::ShowError(fmt::format("Could not load backup memory image: {}", error.message())));
        } else {
            settings.imagePath = file;
            m_context.EnqueueEvent(events::emu::InsertCartridgeFromSettings());
            MakeDirty();
        }
    }
}

void CartridgeSettingsView::ShowLoadBackupImageError(const char *message) {
    m_context.EnqueueEvent(events::gui::ShowError(fmt::format("Could not load backup memory image: {}", message)));
}

void CartridgeSettingsView::ProcessLoadROMImage(void *userdata, std::filesystem::path file, int filter) {
    static_cast<CartridgeSettingsView *>(userdata)->LoadROMImage(file);
}

void CartridgeSettingsView::ProcessLoadROMImageError(void *userdata, const char *message, int filter) {
    static_cast<CartridgeSettingsView *>(userdata)->ShowLoadROMImageError(message);
}

void CartridgeSettingsView::LoadROMImage(std::filesystem::path file) {
    auto &settings = GetSettings().cartridge.rom;

    // TODO: rework this entire process
    // - everything here should be done by the emulator event

    // Check that the file exists
    if (!std::filesystem::is_regular_file(file)) {
        m_context.EnqueueEvent(
            events::gui::ShowError(fmt::format("Could not load ROM cartridge image: {} is not a file", file)));
        return;
    }

    // Check that the file has contents
    const auto size = std::filesystem::file_size(file);
    if (size == 0) {
        m_context.EnqueueEvent(
            events::gui::ShowError("Could not load ROM cartridge image: file is empty or could not be read."));
        return;
    }

    // Check that the image is not larger than the ROM cartridge capacity
    if (size > cart::kROMCartSize) {
        m_context.EnqueueEvent(events::gui::ShowError(fmt::format(
            "Could not load ROM cartridge image: file is too large ({} > {} bytes)", size, cart::kROMCartSize)));
        return;
    }

    // TODO: Check that the image is a proper Sega Saturn cartridge (headers)

    // Update settings and insert cartridge
    settings.imagePath = file;
    m_context.EnqueueEvent(events::emu::InsertCartridgeFromSettings());
    MakeDirty();
}

void CartridgeSettingsView::ShowLoadROMImageError(const char *message) {
    m_context.EnqueueEvent(events::gui::ShowError(fmt::format("Could not load ROM cartridge image: {}", message)));
}

} // namespace app::ui
