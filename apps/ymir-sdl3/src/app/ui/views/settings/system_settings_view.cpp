#include "system_settings_view.hpp"

#include <ymir/hw/smpc/smpc.hpp>

#include <app/events/emu_event_factory.hpp>
#include <app/events/gui_event_factory.hpp>

#include <app/ui/i18n.hpp>
#include <app/ui/widgets/common_widgets.hpp>
#include <app/ui/widgets/datetime_widgets.hpp>
#include <app/ui/widgets/settings_widgets.hpp>
#include <app/ui/widgets/system_widgets.hpp>

#include <app/imgui_data.hpp>

#include <util/regions.hpp>
#include <util/sdl_file_dialog.hpp>

#include <ymir/util/size_ops.hpp>

#include <misc/cpp/imgui_stdlib.h>

#include <fmt/format.h>
#include <fmt/std.h>

#include <SDL3/SDL_misc.h>

#include <set>

using namespace ymir;

namespace app::ui {


SystemSettingsView::SystemSettingsView(SharedContext &context)
    : SettingsViewBase(context) {}

void SystemSettingsView::Display() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    auto &settings = GetSettings().system;

    const float paddingWidth = ImGui::GetStyle().FramePadding.x;
    const float itemSpacingWidth = ImGui::GetStyle().ItemSpacing.x;
    const float fileSelectorButtonWidth = ImGui::CalcTextSize("...").x + paddingWidth * 2;

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.large);
    ImGui::SeparatorText(_("Region"));
    ImGui::PopFont();

    if (ImGui::BeginTable("sys_region", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, 0);
        ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(_("Video standard"));
        }
        if (ImGui::TableNextColumn()) {
            MakeDirty(ui::widgets::VideoStandardSelector(m_context));
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(_("Region"));
            widgets::ExplanationTooltip(_("Changing this option will cause a hard reset"));
        }
        if (ImGui::TableNextColumn()) {
            ui::widgets::RegionSelector(m_context);
        }

        ImGui::EndTable();
    }

    bool autodetectRegion = settings.autodetectRegion.Get();
    if (MakeDirty(ImGui::Checkbox(_("Autodetect region from loaded discs"), &autodetectRegion))) {
        settings.autodetectRegion = autodetectRegion;
    }
    widgets::ExplanationTooltip(_("Whenever a game disc is loaded, the emulator will automatically switch the system region to match one of the game's supported regions. The list below allows you to choose the preferred region order. If none of the preferred regions is supported by the game, the emulator will pick the first region listed on the disc."));

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(_("Preferred region order:"));
    widgets::ExplanationTooltip(_("Drag items to reorder"));

    std::vector<core::config::sys::Region> prefRgnOrder{};
    {
        // Set of all valid regions
        std::set<core::config::sys::Region> validRegions{
            core::config::sys::Region::Japan, core::config::sys::Region::NorthAmerica,
            core::config::sys::Region::AsiaNTSC, core::config::sys::Region::EuropePAL};

        // Build list of regions from setting using only valid options
        for (auto &region : settings.preferredRegionOrder.Get()) {
            if (validRegions.erase(region)) {
                prefRgnOrder.push_back(region);
            }
        }

        // Add any missing regions to the end of the list
        prefRgnOrder.insert(prefRgnOrder.end(), validRegions.begin(), validRegions.end());
    }

    if (ImGui::BeginListBox("##pref_rgn_order", ImVec2(150 * imguiData->displayScale, ImGui::GetFrameHeight() * 4))) {
        ImGui::PushItemFlag(ImGuiItemFlags_AllowDuplicateId, true);
        bool changed = false;
        for (int n = 0; n < prefRgnOrder.size(); n++) {
            core::config::sys::Region item = prefRgnOrder[n];
            ImGui::Selectable(util::RegionToString(item).c_str());

            if (ImGui::IsItemActive() && !ImGui::IsItemHovered()) {
                int n_next = n + (ImGui::GetMouseDragDelta(0).y < 0.f ? -1 : 1);
                if (n_next >= 0 && n_next < prefRgnOrder.size()) {
                    prefRgnOrder[n] = prefRgnOrder[n_next];
                    prefRgnOrder[n_next] = item;
                    ImGui::ResetMouseDragDelta();
                    changed = true;
                }
            }
        }
        ImGui::PopItemFlag();

        if (changed) {
            settings.preferredRegionOrder = prefRgnOrder;
            MakeDirty();
        }

        ImGui::EndListBox();
    }

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.large);
    ImGui::SeparatorText(_("SH-2"));
    ImGui::PopFont();

    widgets::settings::system::EmulateSH2Cache(m_context);
    widgets::settings::system::SH2ClockFactor(m_context);

    // -----------------------------------------------------------------------------------------------------------------

    auto &smpc = m_context.saturn.GetSMPC();
    auto &rtc = smpc.GetRTC();

    ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.large);
    ImGui::SeparatorText(_("Real-Time Clock"));
    ImGui::PopFont();

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(_("Mode:"));
    widgets::ExplanationTooltip("- Host: Syncs the emulated RTC to your system's clock.\n"
                                "- Virtual: Runs a virtual RTC synced to emulation speed.\n\n"
                                "For deterministic behavior, use a virtual RTC synced to a fixed time point on reset.");
    ImGui::SameLine();
    if (MakeDirty(ImGui::RadioButton(TR_ID("Host", "rtc"), settings.rtc.mode == core::config::rtc::Mode::Host))) {
        settings.rtc.mode = core::config::rtc::Mode::Host;
    }
    ImGui::SameLine();
    if (MakeDirty(ImGui::RadioButton(TR_ID("Virtual", "rtc"), settings.rtc.mode == core::config::rtc::Mode::Virtual))) {
        settings.rtc.mode = core::config::rtc::Mode::Virtual;
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(_("Current date/time:"));
    ImGui::SameLine();
    auto dateTime = rtc.GetDateTime();
    if (widgets::DateTimeSelector("rtc_curr", dateTime)) {
        rtc.SetDateTime(dateTime);
        smpc.PersistData();
    }

    if (settings.rtc.mode == core::config::rtc::Mode::Host) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(_("Host time offset:"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.0f * imguiData->displayScale);
        if (ImGui::DragScalar("##rtc_host_offset", ImGuiDataType_S64, &rtc.HostTimeOffset())) {
            smpc.PersistData();
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(_("seconds"));
        ImGui::SameLine();
        if (ImGui::Button(_("Reset"))) {
            rtc.HostTimeOffset() = 0;
            smpc.PersistData();
        }
    } else if (settings.rtc.mode == core::config::rtc::Mode::Virtual) {
        // TODO: request emulator to update date/time so that it is updated in real time
        widgets::ExplanationTooltip(_("This may occasionally stop updating because the virtual RTC is only updated when the game reads from it."));

        if (ImGui::Button(TR_ID("Set to host time", "curr_time"))) {
            rtc.SetDateTime(util::datetime::host());
            smpc.PersistData();
        }
        ImGui::SameLine();
        if (ImGui::Button(TR_ID("Set to starting point", "curr_time"))) {
            rtc.SetDateTime(util::datetime::from_timestamp(settings.rtc.virtHardResetTimestamp));
            smpc.PersistData();
        }

        using HardResetStrategy = core::config::rtc::HardResetStrategy;

        auto hardResetOption = [&](const char *name, HardResetStrategy strategy, const char *explanation) {
            if (MakeDirty(ImGui::RadioButton(fmt::format("{}##virt_rtc_reset", name).c_str(),
                                             settings.rtc.virtHardResetStrategy == strategy))) {
                settings.rtc.virtHardResetStrategy = strategy;
            }
            widgets::ExplanationTooltip(explanation);
        };

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(_("Hard reset behavior:"));
        widgets::ExplanationTooltip(_("Specifies how the virtual RTC behaves on a hard reset."));

        hardResetOption(_("Preserve current time"), HardResetStrategy::Preserve,
                        "The virtual RTC will continue counting from the time point prior to the reset.\n"
                        "The date/time persists between executions of the emulator.");

        hardResetOption(_("Sync to host time"), HardResetStrategy::SyncToHost,
                        "The virtual RTC will reset to the current host RTC time.");

        hardResetOption(_("Reset to starting point"), HardResetStrategy::ResetToFixedTime,
                        "The virtual RTC will reset to the specified starting point.");

        ImGui::Indent();
        {
            auto dateTime = util::datetime::from_timestamp(settings.rtc.virtHardResetTimestamp);
            if (MakeDirty(widgets::DateTimeSelector("virt_base_time", dateTime))) {
                settings.rtc.virtHardResetTimestamp = util::datetime::to_timestamp(dateTime);
            }
            if (MakeDirty(ImGui::Button(TR_ID("Set to host time", "virt_base_time")))) {
                settings.rtc.virtHardResetTimestamp = util::datetime::to_timestamp(util::datetime::host());
            }
        }
        ImGui::Unindent();
    }

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.large);
    ImGui::SeparatorText(_("Internal backup memory"));
    ImGui::PopFont();

    if (MakeDirty(
            ImGui::Checkbox(_("Create internal backup memory images per game"), &settings.internalBackupRAMPerGame))) {
        m_context.EnqueueEvent(events::emu::LoadInternalBackupMemory());
    }
    widgets::ExplanationTooltip(
        fmt::format("When enabled, separate internal backup memory images will be created for each game under {}",
                    m_context.profile.GetPath(ProfilePath::BackupMemory) / "games")
            .c_str());

    if (settings.internalBackupRAMPerGame) {
        ImGui::BeginDisabled();
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(_("Image path"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-(fileSelectorButtonWidth + itemSpacingWidth));
    std::string imagePath = fmt::format("{}", settings.internalBackupRAMImagePath);
    if (MakeDirty(ImGui::InputText("##bup_image_path", &imagePath, ImGuiInputTextFlags_ElideLeft))) {
        settings.internalBackupRAMImagePath = std::u8string{imagePath.begin(), imagePath.end()};
        m_bupSettingsDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("...##bup_image_path")) {
        m_context.EnqueueEvent(events::gui::OpenFile({
            .dialogTitle = _("Load backup memory image"),
            .defaultPath = settings.internalBackupRAMImagePath.empty()
                               ? m_context.profile.GetPath(ProfilePath::PersistentState) / "bup-int.bin"
                               : settings.internalBackupRAMImagePath,
            .filters = {{_("Backup memory image files (*.bin, *.sav)"), "bin;sav"}, {_("All files (*.*)"), "*"}},
            .userdata = this,
            .callback = util::WrapSingleSelectionCallback<&SystemSettingsView::ProcessLoadBackupImage,
                                                          &util::NoopCancelFileDialogCallback,
                                                          &SystemSettingsView::ProcessLoadBackupImageError>,
        }));
    }
    if (settings.internalBackupRAMPerGame) {
        ImGui::EndDisabled();
    }

    const bool dirty = m_bupSettingsDirty;
    if (!dirty) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button(_("Load"))) {
        m_context.EnqueueEvent(events::emu::LoadInternalBackupMemory());
        m_bupSettingsDirty = false;
    }
    if (!dirty) {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button(_("Open backup memory manager"))) {
        m_context.EnqueueEvent(events::gui::OpenBackupMemoryManager());
    }

    if (settings.internalBackupRAMPerGame) {
        const std::filesystem::path intBupPath = m_context.GetInternalBackupRAMPath();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        ImGui::Text(_("Currently using internal backup memory image from %s"), fmt::format("{}", intBupPath).c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::Button(TR_ID("Open containing directory", "int_bup"))) {
            SDL_OpenURL(fmt::format("file:///{}", intBupPath.parent_path()).c_str());
        }
    }
}

void SystemSettingsView::ProcessLoadBackupImage(void *userdata, std::filesystem::path file, int filter) {
    static_cast<SystemSettingsView *>(userdata)->LoadBackupImage(file);
}

void SystemSettingsView::ProcessLoadBackupImageError(void *userdata, const char *message, int filter) {
    static_cast<SystemSettingsView *>(userdata)->ShowLoadBackupImageError(message);
}

void SystemSettingsView::LoadBackupImage(std::filesystem::path file) {
    auto &settings = GetSettings().system;

    if (std::filesystem::is_regular_file(file)) {
        // The user selected an existing image. Make sure it's a proper internal backup image.
        std::error_code error{};
        bup::BackupMemory bupMem{};
        const auto result = bupMem.LoadFrom(file, false, error);
        switch (result) {
        case bup::BackupMemoryImageLoadResult::Success:
            if (bupMem.Size() == 32_KiB) {
                settings.internalBackupRAMImagePath = file;
                m_bupSettingsDirty = false;
                m_context.EnqueueEvent(events::emu::LoadInternalBackupMemory());
                MakeDirty();
            } else {
                m_context.EnqueueEvent(
                    events::gui::ShowError(fmt::format("Could not load backup memory image: Invalid image size")));
            }
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
        settings.internalBackupRAMImagePath = file;
        m_bupSettingsDirty = false;
        m_context.EnqueueEvent(events::emu::LoadInternalBackupMemory());
        MakeDirty();
    }
}

void SystemSettingsView::ShowLoadBackupImageError(const char *message) {
    m_context.EnqueueEvent(events::gui::ShowError(fmt::format("Could not load backup memory image: {}", message)));
}

} // namespace app::ui
