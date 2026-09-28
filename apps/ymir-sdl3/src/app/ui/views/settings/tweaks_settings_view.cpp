#include "tweaks_settings_view.hpp"

#include <app/ui/widgets/settings_widgets.hpp>

#include <app/events/emu_event_factory.hpp>

#include <app/imgui_data.hpp>

#include <misc/cpp/imgui_stdlib.h>

#include <SDL3/SDL_clipboard.h>

namespace app::ui {

TweaksSettingsView::TweaksSettingsView(SharedContext &context)
    : SettingsViewBase(context) {}

void TweaksSettingsView::Display() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    const float availWidth = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;

    ImGui::PushTextWrapPos(availWidth);
    ImGui::TextUnformatted("The options listed in this tab affect emulation accuracy.\n"
                           "If you encounter an issue running some games, try using the recommended or maximum "
                           "quality/accuracy/compatibility presets below.\n"
                           "The performance presets may cause issues with some games.\n"
                           "When reporting issues, make sure to include this list:");
    ImGui::PopTextWrapPos();

    std::string tweaksList{};
    {
        auto checkbox = [](const char *name, bool value) { return fmt::format("[{}] {}", (value ? 'x' : ' '), name); };

        auto &settings = GetSettings();

        auto &enhancements = settings.video.enhancements;
        auto &swRenderer = settings.video.swRenderer;

        fmt::memory_buffer buf{};
        auto inserter = std::back_inserter(buf);

        // =============================================================================================================

        fmt::format_to(inserter, "## General settings\n");

        // -------------------------------------------------------------------------------------------------------------
        // Video

        fmt::format_to(inserter, "### Video\n");
        fmt::format_to(inserter, "- Graphics backend: {}\n", gfx::GraphicsBackendName(settings.video.graphicsBackend));

        // =============================================================================================================

        fmt::format_to(inserter, "## Enhancements\n");

        // -------------------------------------------------------------------------------------------------------------
        // Video

        fmt::format_to(inserter, "### Video\n");
        fmt::format_to(inserter, "- {}\n", checkbox("Deinterlace", enhancements.deinterlace));
        fmt::format_to(inserter, "- {}\n", checkbox("Transparent meshes", enhancements.transparentMeshes));
        if (enhancements.resolutionScaling) {
            if (enhancements.resScaleToDisplaySize) {
                fmt::format_to(inserter, "- {}\n", checkbox("Internal resolution scaling: fit to display", true));
            } else {
                fmt::format_to(
                    inserter, "- {}\n",
                    checkbox(fmt::format("Internal resolution scaling: {}x", enhancements.resScaleFactor.Get()).c_str(),
                             true));
            }
        } else {
            fmt::format_to(inserter, "- {}\n", checkbox("Internal resolution scaling", false));
        }

        // =============================================================================================================

        fmt::format_to(inserter, "## Accuracy settings\n");

        // -------------------------------------------------------------------------------------------------------------
        // SH-2

        fmt::format_to(inserter, "### SH-2\n");
        fmt::format_to(inserter, "- {}\n", checkbox("Emulate SH-2 cache", settings.system.emulateSH2Cache));
        fmt::format_to(inserter, "- SH-2 clock factor: {}%\n", settings.system.sh2ClockFactor.Get());

        // -------------------------------------------------------------------------------------------------------------
        // Audio

        auto interpMode = [](ymir::core::config::audio::SampleInterpolationMode mode) {
            using enum ymir::core::config::audio::SampleInterpolationMode;
            switch (mode) {
            case NearestNeighbor: return "Nearest neighbor";
            case Linear: return "Linear";
            default: return "(invalid setting)";
            }
        };

        fmt::format_to(inserter, "### Audio\n");
        fmt::format_to(inserter, "- Interpolation mode: {}\n", interpMode(settings.audio.interpolation.Get()));
        fmt::format_to(inserter, "- Emulation step granularity: {}\n",
                       widgets::settings::audio::StepGranularityToString(settings.audio.stepGranularity.Get()));

        // -------------------------------------------------------------------------------------------------------------
        // CD Block

        fmt::format_to(inserter, "### CD Block\n");
        fmt::format_to(inserter, "- {}\n", checkbox("Use low level CD Block emulation", settings.cdblock.useLLE));
        fmt::format_to(inserter, "- CD read speed: {}x\n", settings.cdblock.readSpeedFactor.Get());

        // =============================================================================================================

        fmt::format_to(inserter, "## Performance settings\n");

        // -------------------------------------------------------------------------------------------------------------
        // Video

        fmt::format_to(inserter, "### Video\n");
        fmt::format_to(inserter, "- {}\n", checkbox("Threaded VDP1 rendering", swRenderer.threadedVDP1.Get()));
        fmt::format_to(inserter, "- {}\n", checkbox("Threaded VDP2 rendering", swRenderer.threadedVDP2.Get()));
        fmt::format_to(
            inserter, "  - {}\n",
            checkbox("Use dedicated thread for deinterlaced rendering", swRenderer.threadedDeinterlacer.Get()));
        fmt::format_to(inserter, "- {}\n",
                       checkbox("Hardware VDP rendering", settings.video.useHardwareAcceleration.Get()));

        // -------------------------------------------------------------------------------------------------------------
        // Audio

        fmt::format_to(inserter, "### Audio\n");
        fmt::format_to(inserter, "- {}\n", checkbox("Threaded SCSP and sound CPU", settings.audio.threadedSCSP.Get()));

        // =============================================================================================================

        tweaksList = fmt::to_string(buf);
    }

    ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
    ImGui::InputTextMultiline("##tweaks_list", &tweaksList, ImVec2(availWidth, 0),
                              ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_AutoSelectAll);
    ImGui::PopFont();
    if (ImGui::Button("Copy to clipboard")) {
        SDL_SetClipboardText(tweaksList.c_str());
    }

    DisplayEnhancements();
    DisplayAccuracyOptions();
    DisplayPerformanceOptions();
}

void TweaksSettingsView::DisplayEnhancements() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    auto &settings = GetSettings();

    ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.xlarge);
    ImGui::SeparatorText("Enhancements");
    ImGui::PopFont();

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Presets:");
    ImGui::SameLine();
    if (MakeDirty(ImGui::Button("Recommended##enhancements"))) {
        settings.video.enhancements.deinterlace = false;
        settings.video.enhancements.transparentMeshes = true;
        settings.video.enhancements.resolutionScaling = false;
    }
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted(
            "Strikes a good balance between quality and performance without compromising compatibility.");
        ImGui::EndTooltip();
    }

    ImGui::SameLine();
    if (MakeDirty(ImGui::Button("Best quality##enhancements"))) {
        settings.video.enhancements.deinterlace = true;
        settings.video.enhancements.transparentMeshes = true;
        settings.video.enhancements.resScaleToDisplaySize = true;
        settings.video.enhancements.resolutionScaling = true;
    }
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted("Maximizes quality with no regard for performance.");
        ImGui::EndTooltip();
    }

    ImGui::SameLine();
    if (MakeDirty(ImGui::Button("Best performance##enhancements"))) {
        settings.video.enhancements.deinterlace = false;
        settings.video.enhancements.transparentMeshes = false;
        settings.video.enhancements.resolutionScaling = false;
    }
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted("Maximizes performance with no regard for quality.");
        ImGui::EndTooltip();
    }

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.large);
    ImGui::SeparatorText("Video");
    ImGui::PopFont();

    widgets::settings::video::enhancements::Deinterlace(m_context);
    widgets::settings::video::enhancements::TransparentMeshes(m_context);
    widgets::settings::video::enhancements::ResolutionScaling(m_context);
}

void TweaksSettingsView::DisplayAccuracyOptions() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    auto &settings = GetSettings();

    ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.xlarge);
    ImGui::SeparatorText("Accuracy");
    ImGui::PopFont();

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Presets:");
    ImGui::SameLine();
    if (MakeDirty(ImGui::Button("Recommended##accuracy"))) {
        m_context.EnqueueEvent(events::emu::SetEmulateSH2Cache(false));

        settings.system.emulateSH2Cache = false;
        settings.system.sh2ClockFactor = config_defaults::system::kDefaultSH2ClockFactor;

        settings.audio.interpolation = ymir::core::config::audio::SampleInterpolationMode::Linear;
        settings.audio.stepGranularity = 0;

        settings.cdblock.readSpeedFactor = 2;
        settings.cdblock.useLLE = false;
        m_context.EnqueueEvent(events::emu::SetCDBlockLLE(false));
    }
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted(
            "Strikes a good balance between accuracy and performance without compromising compatibility.");
        ImGui::EndTooltip();
    }

    ImGui::SameLine();
    if (MakeDirty(ImGui::Button("Best accuracy##accuracy"))) {
        m_context.EnqueueEvent(events::emu::SetEmulateSH2Cache(true));

        settings.system.emulateSH2Cache = true;
        settings.system.sh2ClockFactor = config_defaults::system::kDefaultSH2ClockFactor;

        settings.audio.interpolation = ymir::core::config::audio::SampleInterpolationMode::Linear;
        settings.audio.stepGranularity = 5;

        const bool hasCDBlockROMs = [&] {
            std::unique_lock lock{m_context.locks.romManager};
            return !m_context.romManager.GetCDBlockROMs().empty();
        }();
        settings.cdblock.readSpeedFactor = 2;
        settings.cdblock.useLLE = hasCDBlockROMs;
        m_context.EnqueueEvent(events::emu::SetCDBlockLLE(hasCDBlockROMs));
    }
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted("Maximizes accuracy with no regard for performance.");
        ImGui::EndTooltip();
    }

    ImGui::SameLine();
    if (MakeDirty(ImGui::Button("Best performance##accuracy"))) {
        m_context.EnqueueEvent(events::emu::SetEmulateSH2Cache(false));

        settings.system.emulateSH2Cache = false;
        settings.system.sh2ClockFactor = config_defaults::system::kDefaultSH2ClockFactor;

        settings.audio.interpolation = ymir::core::config::audio::SampleInterpolationMode::Linear;
        settings.audio.stepGranularity = 0;

        settings.cdblock.readSpeedFactor = 200;
        settings.cdblock.useLLE = false;
        m_context.EnqueueEvent(events::emu::SetCDBlockLLE(false));
    }
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted("Maximizes performance with no regard for accuracy.\n"
                               "Reduces compatibility with some games.");
        ImGui::EndTooltip();
    }

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.large);
    ImGui::SeparatorText("SH-2");
    ImGui::PopFont();

    widgets::settings::system::EmulateSH2Cache(m_context);
    widgets::settings::system::SH2ClockFactor(m_context);

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.large);
    ImGui::SeparatorText("Audio");
    ImGui::PopFont();

    widgets::settings::audio::InterpolationMode(m_context);
    widgets::settings::audio::StepGranularity(m_context);

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.large);
    ImGui::SeparatorText("CD Block");
    ImGui::PopFont();

    widgets::settings::cdblock::CDBlockLLE(m_context);
    widgets::settings::cdblock::CDReadSpeed(m_context);
}

void TweaksSettingsView::DisplayPerformanceOptions() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    auto &settings = GetSettings();

    ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.xlarge);
    ImGui::SeparatorText("Performance");
    ImGui::PopFont();

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Presets:");
    ImGui::SameLine();
    if (MakeDirty(ImGui::Button("Recommended##performance"))) {
        m_context.EnqueueEvent(events::emu::EnableThreadedVDP1(true));
        m_context.EnqueueEvent(events::emu::EnableThreadedVDP2(true));
        m_context.EnqueueEvent(events::emu::EnableThreadedDeinterlacer(true));
        m_context.EnqueueEvent(events::emu::EnableThreadedSCSP(true));
    }
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted("Strikes a good balance between compatibility and performance.");
        ImGui::EndTooltip();
    }

    ImGui::SameLine();
    if (MakeDirty(ImGui::Button("Best compatibility##performance"))) {
        m_context.EnqueueEvent(events::emu::EnableThreadedVDP1(false));
        m_context.EnqueueEvent(events::emu::EnableThreadedVDP2(true));
        m_context.EnqueueEvent(events::emu::EnableThreadedDeinterlacer(true));
        m_context.EnqueueEvent(events::emu::EnableThreadedSCSP(false));
    }
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted("Maximizes compatibility with no regard for performance.");
        ImGui::EndTooltip();
    }

    ImGui::SameLine();
    if (MakeDirty(ImGui::Button("Best performance##performance"))) {
        m_context.EnqueueEvent(events::emu::EnableThreadedVDP1(true));
        m_context.EnqueueEvent(events::emu::EnableThreadedVDP2(true));
        m_context.EnqueueEvent(events::emu::EnableThreadedDeinterlacer(true));
        m_context.EnqueueEvent(events::emu::EnableThreadedSCSP(true));
    }
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted("Maximizes performance with no regard for accuracy.\n"
                               "Reduces compatibility with some games.");
        ImGui::EndTooltip();
    }

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.large);
    ImGui::SeparatorText("Video");
    ImGui::PopFont();

    ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.medium);
    ImGui::SeparatorText("Software renderer");
    ImGui::PopFont();

    widgets::settings::video::swrenderer::ThreadedVDP(m_context);

    ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.medium);
    ImGui::SeparatorText("Hardware renderer");
    ImGui::PopFont();

    widgets::settings::video::UseHardwareAcceleration(m_context);

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.large);
    ImGui::SeparatorText("Audio");
    ImGui::PopFont();

    widgets::settings::audio::ThreadedSCSP(m_context);
}

} // namespace app::ui
