#include "cdblock_settings_view.hpp"

#include <app/events/gui_event_factory.hpp>

#include <app/ui/i18n.hpp>
#include <app/ui/widgets/settings_widgets.hpp>

#include <app/imgui_data.hpp>

#include <util/sdl_file_dialog.hpp>

#include <misc/cpp/imgui_stdlib.h>

#include <SDL3/SDL_misc.h>

using namespace ymir;

namespace app::ui {


CDBlockSettingsView::CDBlockSettingsView(SharedContext &context)
    : SettingsViewBase(context) {}

void CDBlockSettingsView::Display() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    const float paddingWidth = ImGui::GetStyle().FramePadding.x;
    const float itemSpacingWidth = ImGui::GetStyle().ItemSpacing.x;
    const float fileSelectorButtonWidth = ImGui::CalcTextSize("...").x + paddingWidth * 2;
    const float reloadButtonWidth = ImGui::CalcTextSize("Reload").x + paddingWidth * 2;
    const float useButtonWidth = ImGui::CalcTextSize("Use").x + paddingWidth * 2;

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.large);
    ImGui::SeparatorText(_("Accuracy"));
    ImGui::PopFont();

    ImGui::TextUnformatted("NOTE: Changing any of these options will cause a hard reset");

    widgets::settings::cdblock::CDBlockLLE(m_context);

    ImGui::Separator();

    std::filesystem::path cdbRomsPaths = m_context.profile.GetPath(ProfilePath::CDBlockROMImages);

    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    ImGui::Text("CD block ROMs in %s", fmt::format("{}", cdbRomsPaths).c_str());
    ImGui::PopTextWrapPos();

    if (ImGui::Button(_("Open directory"))) {
        SDL_OpenURL(fmt::format("file:///{}", cdbRomsPaths).c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button(_("Rescan"))) {
        {
            std::unique_lock lock{m_context.locks.romManager};
            m_context.romManager.ScanCDBlockROMs(cdbRomsPaths);
        }
        if (m_context.cdbRomPath.empty() && !m_context.romManager.GetCDBlockROMs().empty()) {
            m_context.EnqueueEvent(events::gui::ReloadCDBlockROM());
        }
    }

    auto &settings = GetSettings().cdblock;

    int index = 0;
    if (ImGui::BeginTable("cdb_roms", 3,
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti |
                              ImGuiTableFlags_SortTristate,
                          ImVec2(0, 150 * imguiData->displayScale))) {
        ImGui::TableSetupColumn(_("Path"), ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort, 0.0f);
        ImGui::TableSetupColumn(_("Version"), ImGuiTableColumnFlags_WidthFixed, 60 * imguiData->displayScale);
        ImGui::TableSetupColumn("##use", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,
                                useButtonWidth);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        std::vector<CDBlockROMEntry> sortedROMs;

        for (const auto &[path, info] : m_context.romManager.GetCDBlockROMs()) {
            sortedROMs.emplace_back(info);
        }

        if (const ImGuiTableSortSpecs *sortSpecs = ImGui::TableGetSortSpecs();
            sortSpecs->SpecsDirty && sortedROMs.size() > 1) {

            for (int specIndex = sortSpecs->SpecsCount - 1; specIndex >= 0; --specIndex) {
                const ImGuiTableColumnSortSpecs &sortSpec = sortSpecs->Specs[specIndex];

                const auto sortColumns = [&sortSpec](auto sortStart, auto sortEnd) -> void {
                    switch (sortSpec.ColumnIndex) {
                    case 0: // Path
                        std::stable_sort(
                            sortStart, sortEnd,
                            [](const CDBlockROMEntry &lhs, const CDBlockROMEntry &rhs) { return lhs.path < rhs.path; });
                        break;
                    case 1: // Version
                        std::stable_sort(sortStart, sortEnd,
                                         [](const CDBlockROMEntry &lhs, const CDBlockROMEntry &rhs) {
                                             if (lhs.info == nullptr || rhs.info == nullptr) {
                                                 return false;
                                             }
                                             return lhs.info->version < rhs.info->version;
                                         });
                        break;
                    case 3: // ##Use
                        break;
                    default: util::unreachable();
                    }
                };

                switch (sortSpec.SortDirection) {
                case ImGuiSortDirection_None: break;
                case ImGuiSortDirection_Ascending: sortColumns(sortedROMs.begin(), sortedROMs.end()); break;
                case ImGuiSortDirection_Descending: sortColumns(sortedROMs.rbegin(), sortedROMs.rend()); break;
                }
            }
        }

        for (const auto &cdbROM : sortedROMs) {
            ImGui::TableNextRow();

            if (ImGui::TableNextColumn()) {
                std::filesystem::path relativePath = std::filesystem::relative(cdbROM.path, cdbRomsPaths);
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%s", fmt::format("{}", relativePath).c_str());
            }
            if (ImGui::TableNextColumn()) {
                ImGui::AlignTextToFramePadding();
                if (cdbROM.info != nullptr) {
                    ImGui::Text("%s", cdbROM.info->version.data());
                } else {
                    ImGui::TextUnformatted("-");
                }
            }
            if (ImGui::TableNextColumn()) {
                if (ImGui::Button(fmt::format("{}##{}", _("Use"), index).c_str())) {
                    settings.overrideROM = true;
                    settings.romPath = cdbROM.path;
                    if (!settings.romPath.empty()) {
                        m_context.EnqueueEvent(events::gui::ReloadCDBlockROM());
                        MakeDirty();
                    }
                }
            }
            ++index;
        }

        ImGui::EndTable();
    }

    ImGui::Separator();

    if (MakeDirty(ImGui::Checkbox(_("Override CD block ROM"), &settings.overrideROM))) {
        if (settings.overrideROM && !settings.romPath.empty()) {
            m_context.EnqueueEvent(events::gui::ReloadCDBlockROM());
            MakeDirty();
        }
    }

    if (!settings.overrideROM) {
        ImGui::BeginDisabled();
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(_("CD block ROM path"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-(fileSelectorButtonWidth + reloadButtonWidth + itemSpacingWidth * 2));
    std::string cdbPath = fmt::format("{}", settings.romPath);
    if (MakeDirty(ImGui::InputText("##cdb_path", &cdbPath, ImGuiInputTextFlags_ElideLeft))) {
        settings.romPath = std::u8string{cdbPath.begin(), cdbPath.end()};
    }
    ImGui::SameLine();
    if (ImGui::Button("...##cdb_path")) {
        m_context.EnqueueEvent(events::gui::OpenFile({
            .dialogTitle = "Load CD block ROM",
            .filters = {{"ROM files (*.bin, *.rom)", "bin;rom"}, {"All files (*.*)", "*"}},
            .userdata = this,
            .callback = util::WrapSingleSelectionCallback<&CDBlockSettingsView::ProcessLoadCDBlockROM,
                                                          &util::NoopCancelFileDialogCallback,
                                                          &CDBlockSettingsView::ProcessLoadCDBlockROMError>,
        }));
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload")) {
        if (!settings.romPath.empty()) {
            m_context.EnqueueEvent(events::gui::ReloadCDBlockROM());
            MakeDirty();
        }
    }
    if (!settings.overrideROM) {
        ImGui::EndDisabled();
    }

    ImGui::Separator();

    if (m_context.cdbRomPath.empty()) {
        ImGui::TextUnformatted("No CD block ROM loaded");
    } else {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        ImGui::Text("Currently using CD block ROM at %s", fmt::format("{}", m_context.cdbRomPath).c_str());
        ImGui::PopTextWrapPos();
    }
    const db::CDBlockROMInfo *info = db::GetCDBlockROMInfo(m_context.saturn.GetCDBlockROMHash());
    if (info != nullptr) {
        ImGui::Text("Version: %s", info->version.data());
    } else {
        ImGui::TextUnformatted("Unknown CD block ROM");
    }

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.large);
    ImGui::SeparatorText("Tweaks");
    ImGui::PopFont();

    widgets::settings::cdblock::CDReadSpeed(m_context);
}

void CDBlockSettingsView::ProcessLoadCDBlockROM(void *userdata, std::filesystem::path file, int filter) {
    static_cast<CDBlockSettingsView *>(userdata)->LoadCDBlockROM(file);
}

void CDBlockSettingsView::ProcessLoadCDBlockROMError(void *userdata, const char *message, int filter) {
    static_cast<CDBlockSettingsView *>(userdata)->ShowCDBlockROMLoadError(message);
}

void CDBlockSettingsView::LoadCDBlockROM(std::filesystem::path file) {
    m_context.EnqueueEvent(events::gui::TryLoadCDBlockROM(file));
}

void CDBlockSettingsView::ShowCDBlockROMLoadError(const char *message) {
    m_context.EnqueueEvent(events::gui::ShowError(fmt::format("Could not load CD block ROM: {}", message)));
}

} // namespace app::ui
