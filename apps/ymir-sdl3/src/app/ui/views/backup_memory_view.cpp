// This absolutely disgusting hacky mess somehow manages to work.
//
// It seems like the current design of the application makes it extremely hard to do anything more complex that just
// displaying state or doing very simple interactions. Multi-step operations take a *ton* of effort to build and
// requires storing a bunch of state in a hacky manner, on top of having to be thread-aware -- most of this code runs on
// the main thread, but file dialog operations run on an unspecified SDL thread (which could be the main thread or not).
// Anything that needs access to the cartridge also needs to lock the cartridges mutex (which is done by the window, but
// not by the SDL thread, so you'll find locks in both places).
//
// I *really* hate frontend development... *sigh*
//
// - StrikerX3

#include "backup_memory_view.hpp"

#include <app/events/emu_event_factory.hpp>
#include <app/events/gui_event_factory.hpp>

#include <app/imgui_data.hpp>
#include <app/ui/i18n.hpp>

#include <util/sdl_file_dialog.hpp>

#include <ymir/util/backup_datetime.hpp>
#include <ymir/util/bit_ops.hpp>
#include <ymir/util/size_ops.hpp>
#include <ymir/util/string.hpp>
#include <ymir/util/unreachable.hpp>

#include <fmt/std.h>

#include <cassert>
#include <fstream>
#include <iostream>
#include <optional>
#include <string_view>

using namespace ymir;

namespace app::ui {


namespace static_config {

    // true: Export backup files using Ymir's own format.
    // false: Export backup files using the standard BUP format ("Vmem" magic).
    static constexpr bool export_ymbup = false;

    // The exported backup file suffix.
    // Derived from export_ymbup.
    static constexpr const char *bup_file_suffix = export_ymbup ? "ymbup" : "bup";

} // namespace static_config

static const char *kConfirmDeletionTitle = _("Confirm deletion");
static const char *kConfirmFormatTitle = _("Confirm format");

BackupMemoryView::BackupMemoryView(SharedContext &context, std::string_view name, bool external)
    : m_context(context)
    , m_name(name)
    , m_external(external) {}

void BackupMemoryView::SetBackupMemory(ymir::bup::IBackupMemory *bup) {
    if (m_bup != bup) {
        m_bup = bup;
        m_selected.clear();
    }
}

void BackupMemoryView::Display() {
    const bool hasBup = m_bup != nullptr;

    std::vector<bup::BackupFileInfo> files{};

    if (hasBup) {
        std::unique_lock lock{m_context.locks.backupRAM};
        ImGui::Text(_("%u KiB capacity, %u of %u blocks used"), m_bup->Size() / 1024u, m_bup->GetUsedBlocks(),
                    m_bup->GetTotalBlocks());
        files = m_bup->List();
    } else {
        ImGui::BeginDisabled();
        ImGui::TextUnformatted(_("Unavailable - insert a backup memory cartridge to manage it"));
    }

    // Make room for buttons below the table
    auto avail = ImGui::GetContentRegionAvail();
    avail.y -= ImGui::GetTextLineHeightWithSpacing(); // selection stats
    avail.y -= ImGui::GetFrameHeightWithSpacing();    // actions

    if (ImGui::BeginChild("##bup_files_table", avail)) {
        // TODO: support drag and drop
        if (ImGui::BeginTable("bup_files_list", 6,
                              ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable |
                                  ImGuiTableFlags_SortMulti | ImGuiTableFlags_SortTristate)) {
            DrawFileTableHeader();

            if (const ImGuiTableSortSpecs *sortSpecs = ImGui::TableGetSortSpecs();
                sortSpecs->SpecsDirty && files.size() > 1) {
                for (int specIndex = sortSpecs->SpecsCount - 1; specIndex >= 0; --specIndex) {
                    const ImGuiTableColumnSortSpecs &sortSpec = sortSpecs->Specs[specIndex];
                    const auto sortColumns = [&sortSpec](auto sortStart, auto sortEnd) -> void {
                        switch (sortSpec.ColumnIndex) {
                        case 0: // File name
                            std::stable_sort(sortStart, sortEnd,
                                             [](const bup::BackupFileInfo &lhs, const bup::BackupFileInfo &rhs) {
                                                 return lhs.header.filename < rhs.header.filename;
                                             });
                            break;
                        case 1: // Comment
                            std::stable_sort(sortStart, sortEnd,
                                             [](const bup::BackupFileInfo &lhs, const bup::BackupFileInfo &rhs) {
                                                 return lhs.header.comment < rhs.header.comment;
                                             });
                            break;
                        case 2: // Language
                            std::stable_sort(sortStart, sortEnd,
                                             [](const bup::BackupFileInfo &lhs, const bup::BackupFileInfo &rhs) {
                                                 return lhs.header.language < rhs.header.language;
                                             });
                            break;
                        case 3: // Size
                            std::stable_sort(sortStart, sortEnd,
                                             [](const bup::BackupFileInfo &lhs, const bup::BackupFileInfo &rhs) {
                                                 return lhs.size < rhs.size;
                                             });
                            break;
                        case 4: // Blocks
                            std::stable_sort(sortStart, sortEnd,
                                             [](const bup::BackupFileInfo &lhs, const bup::BackupFileInfo &rhs) {
                                                 return lhs.numRawBlocks < rhs.numRawBlocks;
                                             });
                            break;
                        case 5: // Date/time
                            std::stable_sort(sortStart, sortEnd,
                                             [](const bup::BackupFileInfo &lhs, const bup::BackupFileInfo &rhs) {
                                                 return lhs.header.date < rhs.header.date;
                                             });
                            break;
                        default: util::unreachable();
                        }
                    };

                    switch (sortSpec.SortDirection) {
                    case ImGuiSortDirection_None: break;
                    case ImGuiSortDirection_Ascending: sortColumns(files.begin(), files.end()); break;
                    case ImGuiSortDirection_Descending: sortColumns(files.rbegin(), files.rend()); break;
                    }
                }
            }

            if (hasBup) {
                ImGuiMultiSelectIO *msio =
                    ImGui::BeginMultiSelect(ImGuiMultiSelectFlags_ClearOnEscape |
                                            ImGuiMultiSelectFlags_ClearOnClickVoid | ImGuiMultiSelectFlags_BoxSelect1d);
                ApplyRequests(msio, files);

                for (uint32 i = 0; i < files.size(); i++) {
                    auto &file = files[i];
                    DrawFileTableRow(file, i, true);
                }

                msio = ImGui::EndMultiSelect();
                ApplyRequests(msio, files);
            }

            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    // Show selection statistics
    {
        uint32 selCount = m_selected.size();
        uint32 selBlocks = 0;
        uint32 selSize = 0;
        for (const std::string &item : m_selected) {
            auto it = std::find_if(files.begin(), files.end(),
                                   [&](const bup::BackupFileInfo &bupFile) { return bupFile.header.filename == item; });
            if (it == files.end()) {
                // This shouldn't happen
                continue;
            }
            auto &file = *it;
            selBlocks += file.numBlocks;
            selSize += file.size;
        }

        auto plural = [](uint32 count, const char *singular, const char *plural) {
            return count == 1 ? singular : plural;
        };

        if (selCount == 0) {
            ImGui::TextUnformatted(_("No files selected"));
        } else {
            ImGui::Text("%u %s selected - %u %s, %u %s", selCount, plural(selCount, "file", "files"), selBlocks,
                        plural(selBlocks, "block", "blocks"), selSize, plural(selSize, "byte", "bytes"));
        }
    }

    if (ImGui::Button(_("Import"))) {
        // Open file dialog to select backup files to load
        FileDialogParams params{};
        params.dialogTitle = fmt::format("Import backup files to {}", m_name);
        params.defaultPath = m_context.profile.GetPath(ProfilePath::ExportedBackups);
        params.filters.push_back({"Backup files (*.bup, *.ymbup)", "bup;ymbup"});
        params.filters.push_back({"All files (*.*)", "*"});
        params.userdata = this;
        params.callback = util::WrapMultiSelectionCallback<&BackupMemoryView::ProcessFileImport,
                                                           &BackupMemoryView::ProcessCancelFileImport,
                                                           &BackupMemoryView::ProcessFileImportError>;

        m_context.EnqueueEvent(events::gui::OpenManyFiles(std::move(params)));
    }
    ImGui::SameLine();
    if (m_selected.empty()) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button(_("Export"))) {
        assert(m_bup != nullptr);

        std::unique_lock lock{m_context.locks.backupRAM};

        // Export files from backup memory into a list
        m_filesToExport.clear();
        for (const std::string &item : m_selected) {
            auto it = std::find_if(files.begin(), files.end(),
                                   [&](const bup::BackupFileInfo &bupFile) { return bupFile.header.filename == item; });
            if (it == files.end()) {
                // This shouldn't happen
                continue;
            }
            auto &fileInfo = *it;
            auto optFile = m_bup->Export(fileInfo.header.filename);
            if (optFile) {
                m_filesToExport.push_back({*optFile, fileInfo});
            }
        }

        // Open file dialog to export selected backup files
        if (m_filesToExport.size() == 1) {
            // Single file -> allow user to pick location and file name

            auto &filename = m_filesToExport[0].file.header.filename;
            util::BackupDateTime bupDate{m_filesToExport[0].file.header.date};

            FileDialogParams params{};
            params.dialogTitle = fmt::format("Export {} from {}", filename, m_name);
            params.defaultPath =
                m_context.profile.GetPath(ProfilePath::ExportedBackups) /
                fmt::format("{}_{:04d}{:02d}{:02d}_{:02d}{:02d}.{}", filename, bupDate.year, bupDate.month, bupDate.day,
                            bupDate.hour, bupDate.minute, static_config::bup_file_suffix);
            params.filters.push_back({"Backup files (*.bup, *.ymbup)", "bup;ymbup"});
            params.filters.push_back({"All files (*.*)", "*"});
            params.userdata = this;
            params.callback = util::WrapSingleSelectionCallback<&BackupMemoryView::ProcessSingleFileExport,
                                                                &BackupMemoryView::ProcessCancelFileExport,
                                                                &BackupMemoryView::ProcessFileExportError>;

            m_context.EnqueueEvent(events::gui::SaveFile(std::move(params)));
        } else if (!m_filesToExport.empty()) {
            // Multiple files -> allow user to pick location only

            FolderDialogParams params{};
            params.dialogTitle = fmt::format("Export {} files from {}", m_filesToExport.size(), m_name);
            params.defaultPath = m_context.profile.GetPath(ProfilePath::ExportedBackups);
            params.userdata = this;
            params.callback = util::WrapSingleSelectionCallback<&BackupMemoryView::ProcessMultiFileExport,
                                                                &BackupMemoryView::ProcessCancelFileExport,
                                                                &BackupMemoryView::ProcessFileExportError>;

            m_context.EnqueueEvent(events::gui::SelectFolder(std::move(params)));
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(_("Delete"))) {
        ImGui::OpenPopup(kConfirmDeletionTitle);
    }
    if (m_selected.empty()) {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button(_("Format"))) {
        ImGui::OpenPopup(kConfirmFormatTitle);
    }

    // Align to the right
    const float loadImageWidth = ImGui::CalcTextSize(_("Load image...")).x + ImGui::GetStyle().FramePadding.x * 2;
    const float sameLineSpacing = ImGui::GetStyle().ItemSpacing.x;
    const float saveImageWidth = ImGui::CalcTextSize(_("Save image...")).x + ImGui::GetStyle().FramePadding.x * 2;
    std::string bupMemFilename;
    if (m_external) {
        if (m_bup != nullptr) {
            std::unique_lock lock{m_context.locks.backupRAM};
            bupMemFilename = fmt::format("bup-ext-{}M.bin", m_bup->Size() * 8 / 1024 / 1024);
        } else {
            bupMemFilename = "bup-ext.bin";
        }
    } else {
        bupMemFilename = "bup-int.bin";
    }
    ImGui::SameLine(avail.x - loadImageWidth - sameLineSpacing - saveImageWidth);
    if (ImGui::Button(_("Load image..."))) {
        // Open file dialog to select backup memory image to load
        FileDialogParams params{};
        params.dialogTitle = fmt::format("Load {} image", m_name);
        params.defaultPath = m_context.profile.GetPath(ProfilePath::BackupMemory) / bupMemFilename;
        params.filters.push_back({"Backup memory image files (*.bin, *.sav)", "bin;sav"});
        params.filters.push_back({"All files (*.*)", "*"});
        params.userdata = this;
        params.callback = util::WrapSingleSelectionCallback<&BackupMemoryView::ProcessImageImport,
                                                            &BackupMemoryView::ProcessCancelImageImport,
                                                            &BackupMemoryView::ProcessImageImportError>;

        m_context.EnqueueEvent(events::gui::OpenFile(std::move(params)));
    }
    ImGui::SameLine();
    if (ImGui::Button(_("Save image..."))) {
        {
            std::unique_lock lock{m_context.locks.backupRAM};
            m_imageToSave = m_bup->ReadAll();
        }

        // Open file dialog to select backup memory image to save
        FileDialogParams params{};
        params.dialogTitle = fmt::format("Save {} image", m_name);
        params.defaultPath = m_context.profile.GetPath(ProfilePath::BackupMemory) / bupMemFilename;
        params.filters.push_back({"Backup memory image files (*.bin, *.sav)", "bin;sav"});
        params.filters.push_back({"All files (*.*)", "*"});
        params.userdata = this;
        params.callback = util::WrapSingleSelectionCallback<&BackupMemoryView::ProcessImageExport,
                                                            &BackupMemoryView::ProcessCancelImageExport,
                                                            &BackupMemoryView::ProcessImageExportError>;

        m_context.EnqueueEvent(events::gui::SaveFile(std::move(params)));
    }

    if (!hasBup) {
        ImGui::EndDisabled();
    }

    DisplayConfirmDeleteModal(files);
    DisplayConfirmFormatModal();
    DisplayFileImportOverwriteModal(files);
    DisplayFileImportResultModal();
    DisplayFilesExportSuccessfulModal();
    DisplayImageImportSuccessfulModal();
    DisplayImageExportSuccessfulModal();
    DisplayErrorModal();
}

bool BackupMemoryView::HasBackupMemory() const {
    return m_bup != nullptr;
}

bool BackupMemoryView::HasSelection() const {
    return !m_selected.empty();
}

std::vector<bup::BackupFile> BackupMemoryView::ExportAll() const {
    if (m_bup != nullptr) {
        std::unique_lock lock{m_context.locks.backupRAM};
        return m_bup->ExportAll();
    } else {
        return {};
    }
}

std::vector<bup::BackupFile> BackupMemoryView::ExportSelected() const {
    if (m_bup != nullptr) {
        std::unique_lock lock{m_context.locks.backupRAM};
        std::vector<bup::BackupFile> files{};
        auto bupFiles = m_bup->List();
        for (const std::string &item : m_selected) {
            auto it = std::find_if(bupFiles.begin(), bupFiles.end(),
                                   [&](const bup::BackupFileInfo &bupFile) { return bupFile.header.filename == item; });
            if (it == bupFiles.end()) {
                // This shouldn't happen
                continue;
            }
            auto &fileInfo = *it;
            if (auto file = m_bup->Export(fileInfo.header.filename)) {
                files.push_back(*file);
            }
        }
        return files;
    } else {
        return {};
    }
}

void BackupMemoryView::ImportAll(std::span<const bup::BackupFile> files) {
    m_importBad.clear();
    m_importFailed.clear();
    m_importOverwrite.clear();
    {
        std::unique_lock lock{m_context.locks.backupRAM};
        for (auto &file : files) {
            switch (m_bup->Import(file, true)) {
            case bup::BackupFileImportResult::Imported: break;
            case bup::BackupFileImportResult::Overwritten: break;
            case bup::BackupFileImportResult::NoSpace:
                m_importFailed.push_back({file.header, _("Not enough space in memory")});
                break;
            default: m_importFailed.push_back({file.header, _("Unspecified error")}); break;
            }
        }
    }
    if (!m_importFailed.empty()) {
        OpenFileImportResultModal();
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// File table drawing

void BackupMemoryView::ApplyRequests(ImGuiMultiSelectIO *msio, std::vector<ymir::bup::BackupFileInfo> &files) {
    for (ImGuiSelectionRequest &req : msio->Requests) {
        switch (req.Type) {
        case ImGuiSelectionRequestType_None: break;
        case ImGuiSelectionRequestType_SetAll:
            if (req.Selected) {
                for (const auto &file : files) {
                    m_selected.insert(file.header.filename);
                }
            } else {
                m_selected.clear();
            }
            break;
        case ImGuiSelectionRequestType_SetRange:
            for (uint32 i = req.RangeFirstItem; i <= req.RangeLastItem; i++) {
                if (i >= files.size()) {
                    break;
                }
                const auto &file = files[i];
                if (req.Selected) {
                    m_selected.insert(file.header.filename);
                } else {
                    m_selected.erase(file.header.filename);
                }
            }
            break;
        }
    }
}

void BackupMemoryView::DrawFileTableHeader() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
    const float monoCharWidth = ImGui::CalcTextSize("F").x;
    const float jpCharWidth = ImGui::CalcTextSize("ア").x;
    ImGui::PopFont();

    ImGui::TableSetupColumn(_("File name"), ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort,
                            monoCharWidth * 12.5f);
    ImGui::TableSetupColumn(_("Comment"), ImGuiTableColumnFlags_WidthFixed, jpCharWidth * 9.5f);
    ImGui::TableSetupColumn(_("Language"), ImGuiTableColumnFlags_WidthFixed, monoCharWidth * 9);
    ImGui::TableSetupColumn(_("Size"), ImGuiTableColumnFlags_WidthFixed, monoCharWidth * 6.5f);
    ImGui::TableSetupColumn(_("Blocks"), ImGuiTableColumnFlags_WidthFixed, monoCharWidth * 7);
    ImGui::TableSetupColumn(_("Date/time"), ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupScrollFreeze(0, 1);

    ImGui::TableNextRow();
    for (uint32 i = 0; i < 6; ++i) {
        if (!ImGui::TableSetColumnIndex(i)) {
            continue;
        }
        ImGui::TableHeader(ImGui::TableGetColumnName(i));
        if (i == 4) {
            if (ImGui::BeginItemTooltip()) {
                ImGui::TextUnformatted(_("Used blocks + header blocks"));
                ImGui::EndTooltip();
            }
        }
    }
}

void BackupMemoryView::DrawFileTableRow(const bup::BackupFileInfo &file, uint32 index, bool selectable) {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    ImGui::TableNextRow();
    if (ImGui::TableNextColumn()) {
        ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
        std::string filename = util::TranslateSaturnString(file.header.filename);
        if (selectable) {
            bool selected = m_selected.contains(file.header.filename);
            ImGui::SetNextItemSelectionUserData(index);
            ImGui::Selectable(filename.c_str(), selected,
                              ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_SpanAllColumns);
        } else {
            ImGui::Text("%s", filename.c_str());
        }
        ImGui::PopFont();
    }
    if (ImGui::TableNextColumn()) {
        std::string comment = util::TranslateSaturnString(file.header.comment);
        ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
        ImGui::Text("%s", comment.c_str());
        ImGui::PopFont();
    }
    if (ImGui::TableNextColumn()) {
        static constexpr const char *kLanguages[] = {"Japanese", "English", "French", "German", "Spanish", "Italian"};
        const auto langIndex = static_cast<uint8>(file.header.language);
        if (langIndex < std::size(kLanguages)) {
            ImGui::Text("%s", kLanguages[langIndex]);
        } else {
            ImGui::Text("<%X>", langIndex);
        }
    }
    if (ImGui::TableNextColumn()) {
        ImGui::Text("%u", file.size);
    }
    if (ImGui::TableNextColumn()) {
        ImGui::Text("%u+%u", file.numBlocks, file.numRawBlocks - file.numBlocks);
    }
    if (ImGui::TableNextColumn()) {
        util::BackupDateTime bupDate{file.header.date};
        ImGui::Text("%04u/%02u/%02u %02u:%02u", bupDate.year, bupDate.month, bupDate.day, bupDate.hour, bupDate.minute);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Popups and modals

void BackupMemoryView::OpenFileImportOverwriteModal() {
    m_openFileImportOverwriteModal = true;
}

void BackupMemoryView::OpenFileImportResultModal() {
    m_openFileImportResultModal = true;
}

void BackupMemoryView::OpenFilesExportSuccessfulModal(uint32 exportCount) {
    m_openFilesExportSuccessfulModal = true;
    m_filesExportCount = exportCount;
}

void BackupMemoryView::OpenImageImportSuccessfulModal() {
    m_openImageImportSuccessfulModal = true;
}

void BackupMemoryView::OpenImageExportSuccessfulModal() {
    m_openImageExportSuccessfulModal = true;
}

void BackupMemoryView::OpenErrorModal(std::string errorMessage) {
    m_openErrorModal = true;
    m_errorModalMessage = errorMessage;
}

void BackupMemoryView::DisplayConfirmDeleteModal(std::span<bup::BackupFileInfo> files) {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    if (ImGui::BeginPopupModal(kConfirmDeletionTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text(_("The following files will be deleted from %s:"), m_name.c_str());

        const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
        if (ImGui::BeginChild("##files_to_delete", ImVec2(550, lineHeight * 10))) {
            if (ImGui::BeginTable("bup_files_list", 6, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY)) {
                DrawFileTableHeader();

                for (const std::string &item : m_selected) {
                    auto it = std::find_if(files.begin(), files.end(), [&](const bup::BackupFileInfo &bupFile) {
                        return bupFile.header.filename == item;
                    });
                    if (it == files.end()) {
                        // This shouldn't happen
                        continue;
                    }
                    auto &file = *it;
                    DrawFileTableRow(file);
                }

                ImGui::EndTable();
            }
        }
        ImGui::EndChild();

        ImGui::TextUnformatted(_("This operation cannot be undone!"));

        /*
        static bool dont_ask_me_next_time = false;
        ImGui::Separator();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        ImGui::Checkbox("Don't ask me next time", &dont_ask_me_next_time);
        ImGui::PopStyleVar();*/

        if (ImGui::Button(_("OK"), ImVec2(80 * imguiData->displayScale, 0))) {
            for (const std::string &item : m_selected) {
                auto it = std::find_if(files.begin(), files.end(), [&](const bup::BackupFileInfo &bupFile) {
                    return bupFile.header.filename == item;
                });
                if (it == files.end()) {
                    // This shouldn't happen
                    continue;
                }
                auto &file = *it;
                m_context.EnqueueEvent(events::emu::DeleteBackupFile(file.header.filename, m_external));
            }
            m_selected.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button(_("Cancel"), ImVec2(80 * imguiData->displayScale, 0))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void BackupMemoryView::DisplayConfirmFormatModal() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    if (ImGui::BeginPopupModal(kConfirmFormatTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text(_("%s will be formatted. All files will be erased."), m_name.c_str());
        ImGui::TextUnformatted(_("This operation cannot be undone!"));
        ImGui::NewLine();
        ImGui::Text(_("Are you sure you want to format %s?"), m_name.c_str());

        if (ImGui::Button(_("Yes"), ImVec2(80 * imguiData->displayScale, 0))) {
            m_context.EnqueueEvent(events::emu::FormatBackupMemory(m_external));
            m_selected.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button(_("No"), ImVec2(80 * imguiData->displayScale, 0))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void BackupMemoryView::DisplayFileImportOverwriteModal(std::span<bup::BackupFileInfo> files) {
    const char *kTitle = _("Resolve imported file conflicts");

    const YmirImGuiData *imguiData = GetYmirImGuiData();

    if (m_openFileImportOverwriteModal) {
        ImGui::OpenPopup(kTitle);
        m_openFileImportOverwriteModal = false;
    }

    if (ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text(_("The following files already exist in %s:"), m_name.c_str());

        ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
        const float monoCharWidth = ImGui::CalcTextSize("F").x;
        ImGui::PopFont();

        const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
        if (ImGui::BeginChild("##bup_files_table", ImVec2(550, lineHeight * 20))) {
            if (ImGui::BeginTable("bup_files_overwrite_list", 6,
                                  ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY)) {
                ImGui::TableSetupColumn(_("File name"), ImGuiTableColumnFlags_WidthFixed, monoCharWidth * 12.5f);
                ImGui::TableSetupColumn(_("Original\nSize"), ImGuiTableColumnFlags_WidthFixed, monoCharWidth * 7);
                ImGui::TableSetupColumn(_("Original\nDate/time"), ImGuiTableColumnFlags_WidthFixed, monoCharWidth * 14);
                ImGui::TableSetupColumn(_("Imported\nSize"), ImGuiTableColumnFlags_WidthFixed, monoCharWidth * 7);
                ImGui::TableSetupColumn(_("Imported\nDate/time"), ImGuiTableColumnFlags_WidthFixed, monoCharWidth * 14);
                ImGui::TableSetupColumn(_("Overwrite"), ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableHeadersRow();

                auto findOgInfo = [&](std::string_view filename) -> std::optional<bup::BackupFileInfo> {
                    for (auto &file : files) {
                        if (file.header.filename == filename) {
                            return file;
                        }
                    }
                    return std::nullopt;
                };

                int index = 0;
                for (auto &ovFile : m_importOverwrite) {
                    auto ogInfo = findOgInfo(ovFile.file.header.filename);

                    if (!ogInfo.has_value()) {
                        // Might've been erased by the emulated system in the meantime; "overwrite" and move on
                        ovFile.overwrite = true;
                        continue;
                    }

                    ImGui::TableNextRow();
                    // filename
                    if (ImGui::TableNextColumn()) {
                        ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
                        ImGui::Text("%s", ovFile.file.header.filename.c_str());
                        ImGui::PopFont();
                    }

                    if (ovFile.overwrite) {
                        ImGui::BeginDisabled();
                    }
                    // original size
                    if (ImGui::TableNextColumn()) {
                        ImGui::Text("%u", ogInfo->size);
                    }
                    // original date/time
                    if (ImGui::TableNextColumn()) {
                        util::BackupDateTime bupDate{ogInfo->header.date};
                        ImGui::Text("%04u/%02u/%02u %02u:%02u", bupDate.year, bupDate.month, bupDate.day, bupDate.hour,
                                    bupDate.minute);
                    }
                    if (ovFile.overwrite) {
                        ImGui::EndDisabled();
                    }

                    if (!ovFile.overwrite) {
                        ImGui::BeginDisabled();
                    }
                    // imported size
                    if (ImGui::TableNextColumn()) {
                        ImGui::Text("%u", (uint32)ovFile.file.data.size());
                    }
                    // imported date/time
                    if (ImGui::TableNextColumn()) {
                        util::BackupDateTime bupDate{ovFile.file.header.date};
                        ImGui::Text("%04u/%02u/%02u %02u:%02u", bupDate.year, bupDate.month, bupDate.day, bupDate.hour,
                                    bupDate.minute);
                    }
                    if (!ovFile.overwrite) {
                        ImGui::EndDisabled();
                    }

                    // overwrite checkbox
                    if (ImGui::TableNextColumn()) {
                        ImGui::Checkbox(fmt::format("##overwrite_{}_{}", ovFile.file.header.filename, index++).c_str(),
                                        &ovFile.overwrite);
                    }
                }

                ImGui::EndTable();
            }
        }
        ImGui::EndChild();

        bool execute = false;

        if (ImGui::Button(_("Import"), ImVec2(100 * imguiData->displayScale, 0))) {
            execute = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(_("Overwrite all"), ImVec2(100 * imguiData->displayScale, 0))) {
            for (auto &ovFile : m_importOverwrite) {
                ovFile.overwrite = true;
                break;
            }
            execute = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(_("Ignore all"), ImVec2(100 * imguiData->displayScale, 0))) {
            execute = false;
            ImGui::CloseCurrentPopup();
            OpenFileImportResultModal();
        }

        if (execute) {
            {
                std::unique_lock lock{m_context.locks.backupRAM};
                for (auto &ovFile : m_importOverwrite) {
                    // Skip files not selected to be overwritten
                    if (!ovFile.overwrite) {
                        continue;
                    }

                    // TODO: should do this in the emulator thread; but needs two-way communication
                    // - std::future/std::promise?
                    // Attempt to overwrite files
                    switch (m_bup->Import(ovFile.file, true)) {
                    case bup::BackupFileImportResult::Imported: [[fallthrough]];
                    case bup::BackupFileImportResult::Overwritten: m_importSuccess.push_back(ovFile.file.header); break;
                    case bup::BackupFileImportResult::NoSpace:
                        m_importFailed.push_back({ovFile.file.header, _("Not enough space in memory")});
                        break;
                    default: m_importFailed.push_back({ovFile.file.header, _("Unspecified error")}); break;
                    }
                }
            }
            m_importOverwrite.clear();

            ImGui::CloseCurrentPopup();
            OpenFileImportResultModal();
        }

        ImGui::EndPopup();
    }
}

void BackupMemoryView::DisplayFileImportResultModal() {
    const char *kTitle = _("Backup file import summary");

    const YmirImGuiData *imguiData = GetYmirImGuiData();

    if (m_openFileImportResultModal) {
        ImGui::OpenPopup(kTitle);
        m_openFileImportResultModal = false;
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(250 * imguiData->displayScale, 0),
                                        ImVec2(600 * imguiData->displayScale, 900 * imguiData->displayScale));
    if (ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
        const float monoCharWidth = ImGui::CalcTextSize("F").x;
        ImGui::PopFont();

        if (!m_importSuccess.empty()) {
            ImGui::Text("%zu file%s imported successfully.", m_importSuccess.size(),
                        (m_importSuccess.size() == 1 ? "" : "s"));
        } else {
            ImGui::TextUnformatted(_("No files were imported."));
        }

        if (!m_importFailed.empty()) {
            ImGui::Text("The following file%s could not be imported:", (m_importFailed.size() == 1 ? "" : "s"));

            const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
            if (ImGui::BeginChild("##bup_failed_table", ImVec2(550 * imguiData->displayScale, lineHeight * 10))) {
                if (ImGui::BeginTable("bup_files_failed_list", 2,
                                      ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY)) {
                    ImGui::TableSetupColumn(_("File name"), ImGuiTableColumnFlags_WidthFixed, monoCharWidth * 12.5f);
                    ImGui::TableSetupColumn(_("Reason"), ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableHeadersRow();

                    for (auto &file : m_importFailed) {
                        ImGui::TableNextRow();
                        // filename
                        if (ImGui::TableNextColumn()) {
                            ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
                            ImGui::Text("%s", file.file.filename.c_str());
                            ImGui::PopFont();
                        }
                        // reason
                        if (ImGui::TableNextColumn()) {
                            ImGui::Text("%s", file.errorMessage.c_str());
                        }
                    }

                    ImGui::EndTable();
                }
            }
            ImGui::EndChild();
        }

        if (!m_importBad.empty()) {
            ImGui::Text("The following file%s could not be loaded:", (m_importBad.size() == 1 ? "" : "s"));

            const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
            if (ImGui::BeginChild("##bup_bad_table", ImVec2(550 * imguiData->displayScale, lineHeight * 10))) {
                if (ImGui::BeginTable("bup_files_bad_list", 2,
                                      ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY)) {
                    ImGui::TableSetupColumn(_("Path"));
                    ImGui::TableSetupColumn(_("Reason"));
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableHeadersRow();

                    for (auto &file : m_importBad) {
                        ImGui::TableNextRow();
                        // path
                        if (ImGui::TableNextColumn()) {
                            ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
                            ImGui::Text("%s", fmt::format("{}", file.file).c_str());
                            ImGui::PopFont();
                        }
                        // reason
                        if (ImGui::TableNextColumn()) {
                            ImGui::Text("%s", file.errorMessage.c_str());
                        }
                    }

                    ImGui::EndTable();
                }
            }
            ImGui::EndChild();
        }

        if (ImGui::Button(_("OK"), ImVec2(80 * imguiData->displayScale, 0))) {
            m_importBad.clear();
            m_importFailed.clear();
            m_importOverwrite.clear();
            m_importSuccess.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void BackupMemoryView::DisplayFilesExportSuccessfulModal() {
    const char *kTitle = _("Files export successful");

    const YmirImGuiData *imguiData = GetYmirImGuiData();

    if (m_openFilesExportSuccessfulModal) {
        ImGui::OpenPopup(kTitle);
        m_openFilesExportSuccessfulModal = false;
    }

    if (ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%u file%s exported successfully.", m_filesExportCount, (m_filesExportCount == 1 ? "" : "s"));

        if (ImGui::Button(_("OK"), ImVec2(80 * imguiData->displayScale, 0))) {
            m_filesExportCount = 0;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void BackupMemoryView::DisplayImageImportSuccessfulModal() {
    const char *kTitle = _("Image import successful");

    const YmirImGuiData *imguiData = GetYmirImGuiData();

    if (m_openImageImportSuccessfulModal) {
        ImGui::OpenPopup(kTitle);
        m_openImageImportSuccessfulModal = false;
    }

    if (ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s image imported successfully.", m_name.c_str());

        if (ImGui::Button(_("OK"), ImVec2(80 * imguiData->displayScale, 0))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void BackupMemoryView::DisplayImageExportSuccessfulModal() {
    const char *kTitle = _("Image export successful");

    const YmirImGuiData *imguiData = GetYmirImGuiData();

    if (m_openImageExportSuccessfulModal) {
        ImGui::OpenPopup(kTitle);
        m_openImageExportSuccessfulModal = false;
    }

    if (ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s image exported successfully.", m_name.c_str());

        if (ImGui::Button(_("OK"), ImVec2(80 * imguiData->displayScale, 0))) {
            m_filesExportCount = 0;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void BackupMemoryView::DisplayErrorModal() {
    const char *kTitle = _("Error");

    const YmirImGuiData *imguiData = GetYmirImGuiData();

    if (m_openErrorModal) {
        ImGui::OpenPopup(kTitle);
        m_openErrorModal = false;
    }

    if (ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(450.0f);
        ImGui::Text("%s", m_errorModalMessage.c_str());
        ImGui::PopTextWrapPos();

        if (ImGui::Button(_("OK"), ImVec2(80 * imguiData->displayScale, 0))) {
            m_filesExportCount = 0;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// File import action

void BackupMemoryView::ProcessFileImport(void *userdata, std::span<std::filesystem::path> files, int filter) {
    static_cast<BackupMemoryView *>(userdata)->ImportFiles(files);
}

void BackupMemoryView::ProcessCancelFileImport(void *userdata, int filter) {
    static_cast<BackupMemoryView *>(userdata)->CancelFileImport();
}

void BackupMemoryView::ProcessFileImportError(void *userdata, const char *errorMessage, int filter) {
    static_cast<BackupMemoryView *>(userdata)->FileImportError(errorMessage);
}

void BackupMemoryView::ImportFiles(std::span<std::filesystem::path> files) {
    bup::BackupFile bupFile{};
    std::error_code error{};
    {
        std::unique_lock lock{m_context.locks.backupRAM};
        for (auto &file : files) {
            switch (ImportFile(file, bupFile, error)) {
            case ImportFileResult::Success:
                // TODO: should do this in the emulator thread; but needs two-way communication
                // - std::future/std::promise?
                // Attempt to import file without overwriting
                switch (m_bup->Import(bupFile, false)) {
                case bup::BackupFileImportResult::Imported: m_importSuccess.push_back(bupFile.header); break;
                case bup::BackupFileImportResult::NoSpace:
                    m_importFailed.push_back({bupFile.header, _("Not enough space in memory")});
                    break;
                case bup::BackupFileImportResult::FileExists: m_importOverwrite.push_back({bupFile}); break;
                default: m_importFailed.push_back({bupFile.header, _("Unspecified error")}); break;
                }
                break;
            case ImportFileResult::FilesystemError: m_importBad.push_back({file, error.message()}); break;
            case ImportFileResult::FileTruncated: m_importBad.push_back({file, _("Backup file truncated")}); break;
            case ImportFileResult::BadMagic: m_importBad.push_back({file, _("Not a valid backup file")}); break;
            default: m_importBad.push_back({file, _("Unspecified error")}); break;
            }
        }
    }

    if (m_importOverwrite.empty()) {
        // Show final import result
        OpenFileImportResultModal();
    } else {
        // Show list of files that would be overwritten
        OpenFileImportOverwriteModal();
    }
}

void BackupMemoryView::CancelFileImport() {
    // nothing to do
}

void BackupMemoryView::FileImportError(const char *errorMessage) {
    OpenErrorModal(fmt::format(fmt::runtime(_("File import failed: {}")), errorMessage));
}

BackupMemoryView::ImportFileResult BackupMemoryView::ImportFile(std::filesystem::path path, bup::BackupFile &out,
                                                                std::error_code &error) {

    error.clear();

#define CHECK_INPUT_ERROR                                 \
    do {                                                  \
        if (!in) {                                        \
            error.assign(errno, std::generic_category()); \
            return ImportFileResult::FilesystemError;     \
        }                                                 \
    } while (0)

    std::ifstream in{path, std::ios::binary};
    std::array<char, 11> buf{};

    CHECK_INPUT_ERROR;

    const size_t fileSize = std::filesystem::file_size(path);
    if (fileSize < 0x22) {
        return ImportFileResult::FileTruncated;
    }

    static constexpr std::string_view kYmirMagic = "YmBP";
    static constexpr std::string_view kVmemMagic = "Vmem";

    // Determine format from magic
    in.read(buf.data(), 4);
    CHECK_INPUT_ERROR;
    if (std::string_view(buf.data(), 4) == kYmirMagic) {
        // Ymir backup file format:
        //
        // 00..03  char[4]   magic: "YmBP"
        // 04..0E  char[11]  filename
        //     0F  uint8     language
        // 10..19  char[10]  comment
        // 1A..1D  uint32le  date/time (minutes since 01/01/1980)
        // 1E..21  uint32le  data size (in bytes)
        // 22....  uint8...  data

        // filename
        in.read(buf.data(), 11);
        CHECK_INPUT_ERROR;
        out.header.filename.assign(buf.begin(), buf.end());

        // language
        out.header.language = static_cast<bup::Language>(in.get());
        CHECK_INPUT_ERROR;

        // comment
        in.read(buf.data(), 10);
        CHECK_INPUT_ERROR;
        out.header.comment.assign(buf.begin(), buf.end() - 1);

        // date/time
        in.read((char *)&out.header.date, sizeof(out.header.date));
        CHECK_INPUT_ERROR;

        // data size
        uint32 size{};
        in.read((char *)&size, sizeof(size));
        CHECK_INPUT_ERROR;
        if ((size_t)size + 0x22 > fileSize) {
            return ImportFileResult::FileTruncated;
        }

        // data
        out.data.resize(size);
        in.read((char *)out.data.data(), out.data.size());
        CHECK_INPUT_ERROR;
    } else if (std::string_view(buf.data(), 4) == kVmemMagic) {
        // BUP file format:
        // (adapted from https://github.com/slinga-homebrew/Save-Game-Extractor/blob/master/bup_header.h)
        //
        // 00..03  char[4]   magic: "Vmem"
        // 04..07  uint32be  save ID
        //     08  uint8     number of times BUP_Dir function is called
        //     09  uint8     number of times BUP_Read function is called
        //     0A  uint8     number of times BUP_Write function is called
        //     0B  uint8     number of times BUP_Verify function is called
        // 0C..0F  -         padding/zeros
        // 10..1B  char[12]  filename (null-terminated)
        // 1C..26  char[11]  comment (null-terminated)
        //     27  uint8     language
        // 28..2B  uint32be  date/time (minutes since 01/01/1980)
        // 2C..2F  uint32be  data size (in bytes)
        // 30..31  uint16be  block size (in bytes)
        // 32..33  -         padding/zeros
        // 34..37  uint32be  date/time (minutes since 01/01/1980); used by external tools
        // 38..3F  -         padding/zeros
        // 40....  uint8...  data

        in.seekg(0x10, std::ios::beg);

        // filename
        in.read(buf.data(), 11);
        CHECK_INPUT_ERROR;
        out.header.filename.assign(buf.begin(), buf.end());

        // comment
        in.seekg(0x1C, std::ios::beg); // skip extra byte
        in.read(buf.data(), 10);
        CHECK_INPUT_ERROR;
        out.header.comment.assign(buf.begin(), buf.end() - 1);

        // language
        in.seekg(0x27, std::ios::beg); // skip extra byte
        out.header.language = static_cast<bup::Language>(in.get());
        CHECK_INPUT_ERROR;

        // date/time
        in.read((char *)&out.header.date, sizeof(out.header.date));
        CHECK_INPUT_ERROR;
        out.header.date = bit::big_endian_swap<uint32>(out.header.date);

        // data size
        uint32 size{};
        in.read((char *)&size, sizeof(size));
        CHECK_INPUT_ERROR;
        size = bit::big_endian_swap<uint32>(size);
        if ((size_t)size + 0x40 > fileSize) {
            return ImportFileResult::FileTruncated;
        }

        // data
        out.data.resize(size);
        in.seekg(0x40, std::ios::beg);
        in.read((char *)out.data.data(), out.data.size());
        CHECK_INPUT_ERROR;
    } else {
        return ImportFileResult::BadMagic;
    }

    return ImportFileResult::Success;
}

// ---------------------------------------------------------------------------------------------------------------------
// File export action

void BackupMemoryView::ProcessSingleFileExport(void *userdata, std::filesystem::path file, int filter) {
    static_cast<BackupMemoryView *>(userdata)->ExportSingleFile(file);
}

void BackupMemoryView::ProcessMultiFileExport(void *userdata, std::filesystem::path dir, int filter) {
    static_cast<BackupMemoryView *>(userdata)->ExportMultiFile(dir);
}

void BackupMemoryView::ProcessCancelFileExport(void *userdata, int filter) {
    static_cast<BackupMemoryView *>(userdata)->CancelFileExport();
}

void BackupMemoryView::ProcessFileExportError(void *userdata, const char *errorMessage, int filter) {
    static_cast<BackupMemoryView *>(userdata)->FileExportError(errorMessage);
}

void BackupMemoryView::ExportSingleFile(std::filesystem::path file) {
    assert(m_filesToExport.size() == 1);

    std::filesystem::create_directories(file.parent_path());

    ExportFile(file, m_filesToExport[0]);
    OpenFilesExportSuccessfulModal(m_filesToExport.size());
    m_filesToExport.clear();
}

void BackupMemoryView::ExportMultiFile(std::filesystem::path dir) {
    std::filesystem::create_directories(dir);

    for (auto &exportedFile : m_filesToExport) {
        util::BackupDateTime bupDate{exportedFile.file.header.date};
        std::string filename =
            fmt::format("{}_{:04d}{:02d}{:02d}_{:02d}{:02d}.{}", exportedFile.file.header.filename, bupDate.year,
                        bupDate.month, bupDate.day, bupDate.hour, bupDate.minute, static_config::bup_file_suffix);
        ExportFile(dir / filename, exportedFile);
    }
    OpenFilesExportSuccessfulModal(m_filesToExport.size());
    m_filesToExport.clear();
}

void BackupMemoryView::CancelFileExport() {
    m_filesToExport.clear();
}

void BackupMemoryView::FileExportError(const char *errorMessage) {
    OpenErrorModal(fmt::format(fmt::runtime(_("File export failed: {}")), errorMessage));
    m_filesToExport.clear();
}

void BackupMemoryView::ExportFile(std::filesystem::path path, const ExportedFile &exportedFile) {
    std::ofstream out{path, std::ios::binary};
    std::array<char, 11> buf{};

    if constexpr (static_config::export_ymbup) {
        // Ymir backup file format:
        //
        // 00..03  char[4]   magic: "YmBP"
        // 04..0E  char[11]  filename
        //     0F  uint8     language
        // 10..19  char[10]  comment
        // 1A..1D  uint32le  date/time (minutes since 01/01/1980)
        // 1E..21  uint32le  data size (in bytes)
        // 22....  uint8...  data

        // magic
        static constexpr std::string_view kMagic = "YmBP";
        out.write(kMagic.data(), kMagic.size());

        // filename
        buf.fill(0);
        std::copy(exportedFile.file.header.filename.begin(), exportedFile.file.header.filename.end(), buf.begin());
        out.write(&buf[0], 11);

        // language
        out.put(static_cast<char>(exportedFile.file.header.language));

        // comment
        buf.fill(0);
        std::copy(exportedFile.file.header.comment.begin(), exportedFile.file.header.comment.end(), buf.begin());
        out.write(&buf[0], 10);

        // date/time
        const uint32 date = exportedFile.file.header.date;
        out.write((const char *)&date, sizeof(date));

        // data size
        const uint32 size = static_cast<uint32>(exportedFile.file.data.size());
        out.write((const char *)&size, sizeof(size));

        // data
        out.write((const char *)exportedFile.file.data.data(), exportedFile.file.data.size());
    } else {
        // BUP file format:
        // (adapted from https://github.com/slinga-homebrew/Save-Game-Extractor/blob/master/bup_header.h)
        //
        // 00..03  char[4]   magic: "Vmem"
        // 04..07  uint32be  save ID
        //     08  uint8     number of times BUP_Dir function is called
        //     09  uint8     number of times BUP_Read function is called
        //     0A  uint8     number of times BUP_Write function is called
        //     0B  uint8     number of times BUP_Verify function is called
        // 0C..0F  -         padding/zeros
        // 10..1B  char[12]  filename (null-terminated)
        // 1C..26  char[11]  comment (null-terminated)
        //     27  uint8     language
        // 28..2B  uint32be  date/time (minutes since 01/01/1980)
        // 2C..2F  uint32be  data size (in bytes)
        // 30..31  uint16be  block size (in bytes)
        // 32..33  -         padding/zeros
        // 34..37  uint32be  date/time (minutes since 01/01/1980); used by external tools
        // 38..3F  -         padding/zeros
        // 40....  uint8...  data

        // magic
        static constexpr std::string_view kMagic = "Vmem";
        out.write(kMagic.data(), kMagic.size());

        // save ID (=0), function call counts (=0,0,0,0), padding
        for (uint32 i = 0x04; i <= 0x0F; ++i) {
            out.put(0);
        }

        // filename
        buf.fill(0);
        std::copy(exportedFile.file.header.filename.begin(), exportedFile.file.header.filename.end(), buf.begin());
        out.write(&buf[0], 11);
        out.put(0);

        // comment
        buf.fill(0);
        std::copy(exportedFile.file.header.comment.begin(), exportedFile.file.header.comment.end(), buf.begin());
        out.write(&buf[0], 10);
        out.put(0);

        // language
        out.put(static_cast<char>(exportedFile.file.header.language));

        // date/time
        const uint32 date = bit::big_endian_swap(exportedFile.file.header.date);
        out.write((const char *)&date, sizeof(date));

        // data size
        const uint32 size = bit::big_endian_swap(static_cast<uint32>(exportedFile.file.data.size()));
        out.write((const char *)&size, sizeof(size));

        // block size
        {
            const uint16 blockSize = bit::big_endian_swap<uint16>(exportedFile.info.numRawBlocks);
            out.write((const char *)&blockSize, sizeof(blockSize));
        }

        // padding
        for (uint32 i = 0x32; i <= 0x33; ++i) {
            out.put(0);
        }

        // date/time (again)
        out.write((const char *)&date, sizeof(date));

        // padding
        for (uint32 i = 0x38; i <= 0x3F; ++i) {
            out.put(0);
        }

        // data
        out.write((const char *)exportedFile.file.data.data(), exportedFile.file.data.size());
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Load image action

void BackupMemoryView::ProcessImageImport(void *userdata, std::filesystem::path file, int filter) {
    static_cast<BackupMemoryView *>(userdata)->ImportImage(file);
}

void BackupMemoryView::ProcessCancelImageImport(void *userdata, int filter) {
    static_cast<BackupMemoryView *>(userdata)->CancelImageImport();
}

void BackupMemoryView::ProcessImageImportError(void *userdata, const char *errorMessage, int filter) {
    static_cast<BackupMemoryView *>(userdata)->ImageImportError(errorMessage);
}

void BackupMemoryView::ImportImage(std::filesystem::path file) {
    // Try to load image
    bup::BackupMemory bupMem{};
    std::error_code error{};
    auto result = bupMem.LoadFrom(file, true, error);
    switch (result) {
    case bup::BackupMemoryImageLoadResult::Success: break;
    case bup::BackupMemoryImageLoadResult::FilesystemError:
        OpenErrorModal(fmt::format(fmt::runtime(_("Could not import {} as {}: {}")), file, m_name, error.message()));
        return;
    case bup::BackupMemoryImageLoadResult::InvalidSize:
        OpenErrorModal(fmt::format(fmt::runtime(_("Could not import {} as {}: invalid image size")), file, m_name));
        return;
    case bup::BackupMemoryImageLoadResult::OutOfMemoryError:
        OpenErrorModal(fmt::format(fmt::runtime(_("Could not import {} as {}: out of memory")), file, m_name));
        return;
    }

    // Check file size - must match expected size for the device
    // - for system memory, file must be 32 KiB
    // - for cartridge memory, file must be 512 KiB, 1 MiB, 2 MiB or 4 MiB
    auto size = bupMem.Size();
    if ((!m_external && size != 32_KiB) || (m_external && (size < 512_KiB || size > 4_MiB))) {
        OpenErrorModal(fmt::format(fmt::runtime(_("Could not import {} as {}: invalid image size")), file, m_name));
        return;
    }

    // Replace backup memory instances
    if (m_external) {
        m_context.EnqueueEvent(events::emu::ReplaceExternalBackupMemory(std::move(bupMem)));
    } else {
        m_context.EnqueueEvent(events::emu::ReplaceInternalBackupMemory(std::move(bupMem)));
    }
    m_selected.clear();

    OpenImageImportSuccessfulModal();
}

void BackupMemoryView::CancelImageImport() {
    // nothing to do
}

void BackupMemoryView::ImageImportError(const char *errorMessage) {
    OpenErrorModal(fmt::format("{} image import failed: {}", m_name, errorMessage));
}

// ---------------------------------------------------------------------------------------------------------------------
// Save image action

void BackupMemoryView::ProcessImageExport(void *userdata, std::filesystem::path file, int filter) {
    static_cast<BackupMemoryView *>(userdata)->ExportImage(file);
}

void BackupMemoryView::ProcessCancelImageExport(void *userdata, int filter) {
    static_cast<BackupMemoryView *>(userdata)->CancelImageExport();
}

void BackupMemoryView::ProcessImageExportError(void *userdata, const char *errorMessage, int filter) {
    static_cast<BackupMemoryView *>(userdata)->ImageExportError(errorMessage);
}

void BackupMemoryView::ExportImage(std::filesystem::path file) {
    std::ofstream out{file, std::ios::binary};
    out.write((const char *)m_imageToSave.data(), m_imageToSave.size());
    m_imageToSave.clear();
    OpenImageExportSuccessfulModal();
}

void BackupMemoryView::CancelImageExport() {
    m_imageToSave.clear();
}

void BackupMemoryView::ImageExportError(const char *errorMessage) {
    OpenErrorModal(fmt::format("{} image export failed: {}", m_name, errorMessage));
    m_imageToSave.clear();
}

} // namespace app::ui
