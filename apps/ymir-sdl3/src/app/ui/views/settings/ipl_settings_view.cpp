#include "ipl_settings_view.hpp"

#include <app/events/gui_event_factory.hpp>

#include <app/ui/i18n.hpp>

#include <app/imgui_data.hpp>

#include <util/sdl_file_dialog.hpp>

#include <misc/cpp/imgui_stdlib.h>

#include <SDL3/SDL_misc.h>

#include <fmt/std.h>

using namespace ymir;

namespace app::ui {


static const char *GetVariantName(db::SystemVariant variant) {
    switch (variant) {
    case db::SystemVariant::None: return "None";
    case db::SystemVariant::Saturn: return "Saturn";
    case db::SystemVariant::HiSaturn: return "HiSaturn";
    case db::SystemVariant::VSaturn: return "V-Saturn";
    case db::SystemVariant::DevKit: return "Dev kit";
    default: return "Unknown";
    }
}

static const char *GetRegionName(db::SystemRegion region) {
    switch (region) {
    case db::SystemRegion::None: return "None";
    case db::SystemRegion::US_EU: return "US/EU";
    case db::SystemRegion::JP: return "Japan";
    case db::SystemRegion::KR: return "South Korea";
    default: return "Unknown";
    }
}

IPLSettingsView::IPLSettingsView(SharedContext &context)
    : SettingsViewBase(context) {}

void IPLSettingsView::Display() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    auto &settings = GetSettings().system.ipl;

    ImGui::TextUnformatted("NOTE: Changing any of these options will cause a hard reset.");
    ImGui::TextUnformatted("IPL ROMs contain the Saturn BIOS program.");

    ImGui::Separator();

    const float paddingWidth = ImGui::GetStyle().FramePadding.x;
    const float itemSpacingWidth = ImGui::GetStyle().ItemSpacing.x;
    const float fileSelectorButtonWidth = ImGui::CalcTextSize("...").x + paddingWidth * 2;
    const float reloadButtonWidth = ImGui::CalcTextSize("Reload").x + paddingWidth * 2;
    const float useButtonWidth = ImGui::CalcTextSize("Use").x + paddingWidth * 2;

    std::filesystem::path iplRomsPath = m_context.profile.GetPath(ProfilePath::IPLROMImages);

    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    ImGui::Text("IPL ROMs in %s", fmt::format("{}", iplRomsPath).c_str());
    ImGui::PopTextWrapPos();

    if (ImGui::Button(_("Open directory"))) {
        SDL_OpenURL(fmt::format("file:///{}", iplRomsPath).c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button(_("Rescan"))) {
        {
            std::unique_lock lock{m_context.locks.romManager};
            m_context.romManager.ScanIPLROMs(iplRomsPath);
        }
        if (m_context.iplRomPath.empty() && !m_context.romManager.GetIPLROMs().empty()) {
            m_context.EnqueueEvent(events::gui::ReloadIPLROM());
        }
    }

    int index = 0;
    if (ImGui::BeginTable("sys_ipl_roms", 6,
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti |
                              ImGuiTableFlags_SortTristate,
                          ImVec2(0, 250 * imguiData->displayScale))) {
        ImGui::TableSetupColumn(_("Path"), ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort, 0.0f);
        ImGui::TableSetupColumn(_("Version"), ImGuiTableColumnFlags_WidthFixed, 50 * imguiData->displayScale);
        ImGui::TableSetupColumn(_("Date"), ImGuiTableColumnFlags_WidthFixed, 75 * imguiData->displayScale);
        ImGui::TableSetupColumn(_("Variant"), ImGuiTableColumnFlags_WidthFixed, 60 * imguiData->displayScale);
        ImGui::TableSetupColumn(_("Region"), ImGuiTableColumnFlags_WidthFixed, 105 * imguiData->displayScale);
        ImGui::TableSetupColumn("##use", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,
                                useButtonWidth);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        std::vector<IPLROMEntry> sortedIpl;

        for (const auto &[path, info] : m_context.romManager.GetIPLROMs()) {
            sortedIpl.emplace_back(info);
        }

        if (const ImGuiTableSortSpecs *sortSpecs = ImGui::TableGetSortSpecs();
            sortSpecs->SpecsDirty && sortedIpl.size() > 1) {

            for (int specIndex = sortSpecs->SpecsCount - 1; specIndex >= 0; --specIndex) {
                const ImGuiTableColumnSortSpecs &sortSpec = sortSpecs->Specs[specIndex];

                const auto sortColumns = [&sortSpec](auto sortStart, auto sortEnd) -> void {
                    switch (sortSpec.ColumnIndex) {
                    case 0: // Path
                        std::stable_sort(sortStart, sortEnd, [](const IPLROMEntry &lhs, const IPLROMEntry &rhs) {
                            return lhs.path < rhs.path;
                        });
                        break;
                    case 1: // Version
                        std::stable_sort(sortStart, sortEnd, [](const IPLROMEntry &lhs, const IPLROMEntry &rhs) {
                            return lhs.versionString < rhs.versionString;
                        });
                        break;
                    case 2: // Date
                        std::stable_sort(sortStart, sortEnd, [](const IPLROMEntry &lhs, const IPLROMEntry &rhs) {
                            if (lhs.info && rhs.info) {
                                return std::tie(lhs.info->year, lhs.info->month, lhs.info->day) <
                                       std::tie(rhs.info->year, rhs.info->month, rhs.info->day);
                            } else {
                                return (lhs.info != nullptr) < (rhs.info != nullptr);
                            }
                        });
                        break;
                    case 3: // Variant
                        std::stable_sort(sortStart, sortEnd, [](const IPLROMEntry &lhs, const IPLROMEntry &rhs) {
                            if (lhs.info && rhs.info) {
                                return (lhs.info->variant < rhs.info->variant);
                            } else {
                                return (lhs.info != nullptr) < (rhs.info != nullptr);
                            }
                        });
                        break;
                    case 4: // Region
                        std::stable_sort(sortStart, sortEnd, [](const IPLROMEntry &lhs, const IPLROMEntry &rhs) {
                            if (lhs.info && rhs.info) {
                                return std::tie(lhs.info->regionFree, lhs.info->region) <
                                       std::tie(rhs.info->regionFree, rhs.info->region);
                            } else {
                                return (lhs.info != nullptr) < (rhs.info != nullptr);
                            }
                        });
                        break;
                    case 5: // ##Use
                        break;
                    default: util::unreachable();
                    }
                };

                switch (sortSpec.SortDirection) {
                case ImGuiSortDirection_None: break;
                case ImGuiSortDirection_Ascending: sortColumns(sortedIpl.begin(), sortedIpl.end()); break;
                case ImGuiSortDirection_Descending: sortColumns(sortedIpl.rbegin(), sortedIpl.rend()); break;
                }
            }
        }

        for (const auto &iplRom : sortedIpl) {
            ImGui::TableNextRow();

            if (ImGui::TableNextColumn()) {
                std::filesystem::path relativePath = std::filesystem::relative(iplRom.path, iplRomsPath);
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%s", fmt::format("{}", relativePath).c_str());
            }
            if (ImGui::TableNextColumn()) {
                ImGui::AlignTextToFramePadding();
                if (iplRom.info != nullptr) {
                    ImGui::Text("%s", iplRom.info->version);
                } else {
                    ImGui::TextUnformatted("-");
                }
            }
            if (ImGui::TableNextColumn()) {
                ImGui::AlignTextToFramePadding();
                if (iplRom.info != nullptr) {
                    ImGui::Text("%04u/%02u/%02u", iplRom.info->year, iplRom.info->month, iplRom.info->day);
                } else {
                    ImGui::TextUnformatted("-");
                }
            }
            if (ImGui::TableNextColumn()) {
                ImGui::AlignTextToFramePadding();
                if (iplRom.info != nullptr) {
                    ImGui::Text("%s", GetVariantName(iplRom.info->variant));
                } else {
                    ImGui::TextUnformatted("Unknown");
                }
            }
            if (ImGui::TableNextColumn()) {
                ImGui::AlignTextToFramePadding();
                if (iplRom.info != nullptr) {
                    if (iplRom.info->regionFree) {
                        ImGui::Text("%s (RF)", GetRegionName(iplRom.info->region));
                    } else {
                        ImGui::Text("%s", GetRegionName(iplRom.info->region));
                    }
                } else {
                    ImGui::TextUnformatted("Unknown");
                }
            }
            if (ImGui::TableNextColumn()) {
                if (ImGui::Button(fmt::format("Use##{}", index).c_str())) {
                    settings.overrideImage = true;
                    settings.path = iplRom.path;
                    if (!settings.path.empty()) {
                        m_context.EnqueueEvent(events::gui::ReloadIPLROM());
                        MakeDirty();
                    }
                }
            }
            ++index;
        }

        ImGui::EndTable();
    }

    ImGui::Separator();

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Preferred system variant:");
    ImGui::SameLine();
    if (ImGui::BeginCombo("##variant", GetVariantName(settings.variant), ImGuiComboFlags_WidthFitPreview)) {
        for (int i = 0; i <= 4; ++i) {
            const auto variant = static_cast<db::SystemVariant>(i);
            if (MakeDirty(ImGui::Selectable(GetVariantName(variant), variant == settings.variant))) {
                settings.variant = variant;
                m_context.EnqueueEvent(events::gui::ReloadIPLROM());
            }
        }
        ImGui::EndCombo();
    }

    ImGui::Separator();

    if (MakeDirty(ImGui::Checkbox("Override IPL ROM", &settings.overrideImage))) {
        if (settings.overrideImage && !settings.path.empty()) {
            m_context.EnqueueEvent(events::gui::ReloadIPLROM());
            MakeDirty();
        }
    }

    if (!settings.overrideImage) {
        ImGui::BeginDisabled();
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("IPL ROM path");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-(fileSelectorButtonWidth + reloadButtonWidth + itemSpacingWidth * 2));
    std::string iplPath = fmt::format("{}", settings.path);
    if (MakeDirty(ImGui::InputText("##ipl_path", &iplPath, ImGuiInputTextFlags_ElideLeft))) {
        settings.path = std::u8string{iplPath.begin(), iplPath.end()};
    }
    ImGui::SameLine();
    if (ImGui::Button("...##ipl_path")) {
        m_context.EnqueueEvent(events::gui::OpenFile({
            .dialogTitle = "Load IPL ROM",
            .filters = {{"ROM files (*.bin, *.rom)", "bin;rom"}, {"All files (*.*)", "*"}},
            .userdata = this,
            .callback = util::WrapSingleSelectionCallback<&IPLSettingsView::ProcessLoadIPLROM,
                                                          &util::NoopCancelFileDialogCallback,
                                                          &IPLSettingsView::ProcessLoadIPLROMError>,
        }));
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload")) {
        if (!settings.path.empty()) {
            m_context.EnqueueEvent(events::gui::ReloadIPLROM());
            MakeDirty();
        }
    }
    if (!settings.overrideImage) {
        ImGui::EndDisabled();
    }

    ImGui::Separator();

    if (m_context.iplRomPath.empty()) {
        ImGui::TextUnformatted("No IPL ROM loaded");
    } else {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        ImGui::Text("Currently using IPL ROM at %s", fmt::format("{}", m_context.iplRomPath).c_str());
        ImGui::PopTextWrapPos();
    }
    const db::IPLROMInfo *info = db::GetIPLROMInfo(m_context.saturn.GetIPLHash());
    if (info != nullptr) {
        ImGui::Text("Version: %s", info->version);
        ImGui::Text("Release date: %04u/%02u/%02u", info->year, info->month, info->day);
        ImGui::Text("Variant: %s", GetVariantName(info->variant));
        ImGui::Text("Region: %s", GetRegionName(info->region));
    } else {
        ImGui::TextUnformatted("Unknown IPL ROM");
    }
}

void IPLSettingsView::ProcessLoadIPLROM(void *userdata, std::filesystem::path file, int filter) {
    static_cast<IPLSettingsView *>(userdata)->LoadIPLROM(file);
}

void IPLSettingsView::ProcessLoadIPLROMError(void *userdata, const char *message, int filter) {
    static_cast<IPLSettingsView *>(userdata)->ShowIPLROMLoadError(message);
}

void IPLSettingsView::LoadIPLROM(std::filesystem::path file) {
    m_context.EnqueueEvent(events::gui::TryLoadIPLROM(file));
}

void IPLSettingsView::ShowIPLROMLoadError(const char *message) {
    m_context.EnqueueEvent(events::gui::ShowError(fmt::format("Could not load IPL ROM: {}", message)));
}

} // namespace app::ui
