#include "general_settings_view.hpp"

#include <app/events/emu_event_factory.hpp>
#include <app/events/gui_event_factory.hpp>

#include <app/ui/i18n.hpp>
#include <app/ui/widgets/common_widgets.hpp>

#include <app/imgui_data.hpp>

#include <misc/cpp/imgui_stdlib.h>

#include <util/math.hpp>
#include <util/sdl_file_dialog.hpp>

#include <fmt/std.h>

#include <SDL3/SDL_misc.h>

namespace app::ui {


GeneralSettingsView::GeneralSettingsView(SharedContext &context)
    : SettingsViewBase(context) {}

void GeneralSettingsView::Display() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    auto &settings = GetSettings().general;
    auto &profile = m_context.profile;

    const float paddingWidth = ImGui::GetStyle().FramePadding.x;
    const float itemSpacingWidth = ImGui::GetStyle().ItemSpacing.x;
    const float fileSelectorButtonWidth = ImGui::CalcTextSize("...").x + paddingWidth * 2;
    const float clearButtonWidth = ImGui::CalcTextSize(_("Clear")).x + paddingWidth * 2;
    const float openButtonWidth = ImGui::CalcTextSize(_("Open")).x + paddingWidth * 2;

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.large);
    ImGui::SeparatorText(_("Performance"));
    ImGui::PopFont();

    if (MakeDirty(ImGui::Checkbox(_("Boost process priority"), &settings.boostProcessPriority))) {
        m_context.EnqueueEvent(events::gui::SetProcessPriority(settings.boostProcessPriority));
    }
    widgets::ExplanationTooltip("Increases the process's priority level, which may help reduce stuttering.");

    if (MakeDirty(ImGui::Checkbox(_("Boost emulator thread priority"), &settings.boostEmuThreadPriority))) {
        m_context.EnqueueEvent(events::emu::SetThreadPriority(settings.boostEmuThreadPriority));
    }
    widgets::ExplanationTooltip("Increases the emulator thread's priority, which may help reduce jitter.");

    MakeDirty(ImGui::Checkbox(_("Preload disc images to RAM"), &settings.preloadDiscImagesToRAM));
    widgets::ExplanationTooltip(
        "Preloads the entire disc image to memory.\n"
        "May help reduce stuttering if you're loading images from a slow disk or from the network.");

    MakeDirty(ImGui::Checkbox(_("Remember last loaded disc image"), &settings.rememberLastLoadedDisc));
    widgets::ExplanationTooltip(
        "When enabled, Ymir will automatically load the most recently loaded game disc on startup.");

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.large);
    ImGui::SeparatorText(_("Behavior"));
    ImGui::PopFont();

    ImGui::TextUnformatted(_("Emulation speed"));
    widgets::ExplanationTooltip(
        "You can adjust and switch between the primary and alternate speeds at any time.\n"
        "The primary speed is meant to be the default speed for normal usage while the alternate speed is used "
        "as a slow motion or speed-limited fast-forward option, but feel free to use them as you wish.\n"
        "The primary and alternate speeds reset/default to 100% and 50% respectively.");

    if (ImGui::BeginTable("emu_speed", 3, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            if (MakeDirty(ImGui::RadioButton(TR_ID("Primary", "emu_speed"), !settings.useAltSpeed))) {
                settings.useAltSpeed = false;
            }
        }
        if (ImGui::TableNextColumn()) {
            double speed = settings.mainSpeedFactor.Get() * 100.0;
            const double kMin = 10.0;
            const double kMax = 500.0;
            ImGui::SetNextItemWidth(300.0f * imguiData->displayScale);
            if (MakeDirty(ImGui::SliderScalar("##main_emu_speed", ImGuiDataType_Double, &speed, &kMin, &kMax, "%.0lf%%",
                                              ImGuiSliderFlags_AlwaysClamp))) {
                settings.mainSpeedFactor = std::clamp(util::RoundToMultiple(speed * 0.01, 0.1), 0.1, 5.0);
            }
        }
        if (ImGui::TableNextColumn()) {
            if (MakeDirty(ImGui::Button(TR_ID("Reset", "main_emu_speed")))) {
                settings.mainSpeedFactor = 1.0;
            }
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            if (MakeDirty(ImGui::RadioButton(TR_ID("Alternate", "emu_speed"), settings.useAltSpeed))) {
                settings.useAltSpeed = true;
            }
        }
        if (ImGui::TableNextColumn()) {
            double speed = settings.altSpeedFactor.Get() * 100.0;
            const double kMin = 10.0;
            const double kMax = 500.0;
            ImGui::SetNextItemWidth(300.0f * imguiData->displayScale);
            if (MakeDirty(ImGui::SliderScalar("##alternate_emu_speed", ImGuiDataType_Double, &speed, &kMin, &kMax,
                                              "%.0lf%%", ImGuiSliderFlags_AlwaysClamp))) {
                settings.altSpeedFactor = std::clamp(util::RoundToMultiple(speed * 0.01, 0.1), 0.1, 5.0);
            }
        }
        if (ImGui::TableNextColumn()) {
            if (MakeDirty(ImGui::Button(TR_ID("Reset", "alt_emu_speed")))) {
                settings.altSpeedFactor = 0.5;
            }
        }

        ImGui::EndTable();
    }

    MakeDirty(ImGui::Checkbox(_("Pause when unfocused"), &settings.pauseWhenUnfocused));
    widgets::ExplanationTooltip(
        "The emulator will pause when the window loses focus and resume when it regains focus.\n"
        "Does not affect the behavior of manual pauses - they persist through focus changes.");

    MakeDirty(ImGui::Checkbox(_("Unpause after loading discs"), &settings.unpauseOnDiscLoad));
    widgets::ExplanationTooltip("The emulator will unpause when a game disc is loaded.");

    MakeDirty(ImGui::Checkbox(_("Start paused upon launch"), &settings.startPaused));
    widgets::ExplanationTooltip("Ymir will launch with emulation paused when starting up.");

    MakeDirty(ImGui::Checkbox(_("Enable Discord Rich Presence"), &settings.enableDiscordPresence));
    widgets::ExplanationTooltip("Displays the current game on your Discord profile while Ymir is running.");

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.large);
    ImGui::SeparatorText(_("Updates"));
    ImGui::PopFont();

#if Ymir_ENABLE_UPDATE_CHECKS
    MakeDirty(ImGui::Checkbox(_("Check for updates on startup"), &settings.checkForUpdates));
#endif
    MakeDirty(ImGui::Checkbox(_("Update to nightly builds"), &settings.includeNightlyBuilds));
    widgets::ExplanationTooltip("When enabled, Ymir will also notify you when new nightly builds are available.");
    if (ImGui::Button(_("Check now"))) {
        m_context.EnqueueEvent(events::gui::CheckForUpdates());
    }
    ImGui::TextUnformatted(_("Latest versions:"));
    if (ImGui::BeginTable("updates", 3, ImGuiTableFlags_SizingFixedFit)) {
        std::unique_lock lock{m_context.locks.updates};
        auto version = [&](const char *name, const std::optional<UpdateInfo> &ver) {
            ImGui::PushID(name);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s", name);

            ImGui::TableNextColumn();
            if (ver) {
                if (m_context.updates.inProgress) {
                    ImGui::TextUnformatted(_("Checking..."));
                } else {
                    ImGui::Text("%s", ver->version.to_string().c_str());
                }
                ImGui::TableNextColumn();
                if (!ver->releaseNotesURL.empty()) {
                    if (ImGui::SmallButton(_("Release notes"))) {
                        SDL_OpenURL(ver->releaseNotesURL.c_str());
                    }
                }
                if (!ver->downloadURL.empty()) {
                    if (!ver->releaseNotesURL.empty()) {
                        ImGui::SameLine();
                    }
                    if (ImGui::SmallButton(_("Download"))) {
                        SDL_OpenURL(ver->downloadURL.c_str());
                    }
                }
            } else {
                ImGui::TextUnformatted(_("Not checked"));
            }

            ImGui::PopID();
        };
        version("Stable", m_context.updates.latestStable);
        version("Nightly", m_context.updates.latestNightly);

        ImGui::EndTable();
    }
    {
        std::unique_lock lock{m_context.locks.targetUpdate};
        if (m_context.targetUpdate) {
            ImGui::Text("Update to v%s (%s channel) available.",
                        m_context.targetUpdate->info.version.to_string().c_str(),
                        (m_context.targetUpdate->channel == ReleaseChannel::Stable ? "stable" : "nightly"));
        } else {
            ImGui::TextUnformatted("You are already using the latest version.");
        }
    }

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.large);
    ImGui::SeparatorText(_("Screenshots"));
    ImGui::PopFont();

    MakeDirty(
        ImGui::SliderInt(_("Screenshot scale"), &settings.screenshotScale, 1, 4, "%u", ImGuiSliderFlags_AlwaysClamp));
    widgets::ExplanationTooltip("Adjusts the scale at which screenshots are saved.\n"
                                "Screenshots taken by the emulator have no aspect ratio distortion and are scaled with "
                                "nearest neighbor interpolation to preserve the raw framebuffer data.");

    const std::filesystem::path screenshotsPath = m_context.profile.GetPath(ProfilePath::Screenshots);
    ImGui::TextUnformatted(_("Screenshots are saved to "));
    ImGui::SameLine(0, 0);
    if (ImGui::TextLink(fmt::format("{}", screenshotsPath).c_str())) {
        SDL_OpenURL(fmt::format("file:///{}", screenshotsPath).c_str());
    }

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.large);
    ImGui::SeparatorText(_("Rewind buffer"));
    ImGui::PopFont();

    if (MakeDirty(ImGui::Checkbox(_("Enable rewind buffer"), &settings.enableRewindBuffer))) {
        m_context.EnqueueEvent(events::gui::EnableRewindBuffer(settings.enableRewindBuffer));
    }
    widgets::ExplanationTooltip("Allows you to step back in time.\n"
                                "Increases memory usage and slightly reduces performance.");

    // TODO: rewind buffer size

    if (MakeDirty(ImGui::SliderInt(_("Compression level"), &settings.rewindCompressionLevel, 0, 16, "%d",
                                   ImGuiSliderFlags_AlwaysClamp))) {
        m_context.rewindBuffer.LZ4Accel = 1 << (16 - settings.rewindCompressionLevel);
    }
    widgets::ExplanationTooltip("Adjust compression ratio vs. speed.\n"
                                "Higher values improve compression ratio, reducing memory usage.\n"
                                "Lower values increase compression speed and improve emulation performance.");

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.large);
    ImGui::SeparatorText(_("Profile paths"));
    ImGui::PopFont();

    const std::filesystem::path profileRoot = m_context.profile.GetPath(ProfilePath::Root);
    ImGui::TextUnformatted("Current profile located at ");
    ImGui::SameLine(0, 0);
    if (ImGui::TextLink(fmt::format("{}", profileRoot).c_str())) {
        SDL_OpenURL(fmt::format("file:///{}", profileRoot).c_str());
    }

    ImGui::TextUnformatted("Override profile paths");

    if (ImGui::BeginTable("path_overrides", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        auto drawRow = [&](const char *name, ProfilePath profPath) {
            ImGui::TableNextRow();
            if (ImGui::TableNextColumn()) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(name);
            }
            if (ImGui::TableNextColumn()) {
                std::string label = fmt::format("##prof_path_override_{}", static_cast<uint32>(profPath));
                std::string imagePath = fmt::format("{}", profile.GetPathOverride(profPath));
                std::string currPath = fmt::format("{}", profile.GetPath(profPath));

                ImGui::SetNextItemWidth(
                    -(fileSelectorButtonWidth + clearButtonWidth + openButtonWidth + itemSpacingWidth * 3));
                if (MakeDirty(ImGui::InputTextWithHint(label.c_str(), currPath.c_str(), &imagePath,
                                                       ImGuiInputTextFlags_ElideLeft))) {
                    profile.SetPathOverride(profPath, std::u8string{imagePath.begin(), imagePath.end()});
                }
                ImGui::SameLine();
                if (ImGui::Button(fmt::format("...{}", label).c_str())) {
                    m_selectedProfPath = profPath;
                    m_context.EnqueueEvent(events::gui::SelectFolder({
                        .dialogTitle = fmt::format("Select {} directory", name),
                        .defaultPath = m_context.profile.GetPath(profPath),
                        .userdata = this,
                        .callback =
                            util::WrapSingleSelectionCallback<&GeneralSettingsView::ProcessPathOverrideSelection,
                                                              &util::NoopCancelFileDialogCallback,
                                                              &GeneralSettingsView::ProcessPathOverrideSelectionError>,
                    }));
                }
                ImGui::SameLine();
                if (ImGui::Button(fmt::format("Clear{}", label).c_str())) {
                    profile.ClearOverridePath(profPath);
                }
                ImGui::SameLine();
                if (ImGui::Button(fmt::format("Open{}", label).c_str())) {
                    auto path = m_context.profile.GetPath(profPath);
                    SDL_OpenURL(fmt::format("file:///{}", path).c_str());
                }
            }
        };

        drawRow("IPL ROM images", ProfilePath::IPLROMImages);
        drawRow("CD block ROM images", ProfilePath::CDBlockROMImages);
        drawRow("Cartridge ROM images", ProfilePath::ROMCartImages);
        drawRow("Backup memory", ProfilePath::BackupMemory);
        drawRow("Exported backup files", ProfilePath::ExportedBackups);
        drawRow("Persistent state", ProfilePath::PersistentState);
        drawRow("Save states", ProfilePath::SaveStates);
        drawRow("Dumps", ProfilePath::Dumps);
        drawRow("Screenshots", ProfilePath::Screenshots);

        ImGui::EndTable();
    }
}

void GeneralSettingsView::ProcessPathOverrideSelection(void *userdata, std::filesystem::path file, int filter) {
    static_cast<GeneralSettingsView *>(userdata)->SelectPathOverride(file);
}

void GeneralSettingsView::ProcessPathOverrideSelectionError(void *userdata, const char *message, int filter) {
    static_cast<GeneralSettingsView *>(userdata)->ShowPathOverrideSelectionError(message);
}

void GeneralSettingsView::SelectPathOverride(std::filesystem::path file) {
    if (std::filesystem::is_directory(file)) {
        m_context.profile.SetPathOverride(m_selectedProfPath, file);
        MakeDirty();
    }
}

void GeneralSettingsView::ShowPathOverrideSelectionError(const char *message) {
    m_context.EnqueueEvent(events::gui::ShowError(fmt::format("Could not open directory: {}", message)));
}

} // namespace app::ui
