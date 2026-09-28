#include "settings_widgets.hpp"

#include "common_widgets.hpp"

#include <app/imgui_data.hpp>
#include <app/settings.hpp>

#include <app/services/gfx/gfx_adapters.hpp>

#include <app/events/emu_event_factory.hpp>
#include <app/events/gui_event_factory.hpp>

#include <ymir/db/game_db.hpp>

#include <ymir/hw/cdblock/cdblock.hpp>

#include <fmt/format.h>

using namespace ymir;

namespace app::ui::widgets {

namespace settings::system {

    void EmulateSH2Cache(SharedContext &ctx) {
        const db::GameInfo *gameInfo = nullptr;
        {
            std::unique_lock lock{ctx.locks.disc};
            const auto &discHeader = ctx.saturn.GetDiscHeader();
            if (discHeader.IsValid()) {
                gameInfo = db::GetGameInfo(discHeader.productNumber, ctx.saturn.GetDiscHash());
            }
        }
        const bool forced =
            gameInfo != nullptr && BitmaskEnum(gameInfo->flags).AnyOf(db::GameInfo::Flags::ForceSH2Cache);

        auto &settings = ctx.serviceLocator.GetRequired<Settings>();
        bool emulateSH2Cache = settings.system.emulateSH2Cache || forced;
        if (forced) {
            ImGui::BeginDisabled();
        }
        if (settings.MakeDirty(ImGui::Checkbox("Emulate SH-2 cache", &emulateSH2Cache))) {
            ctx.EnqueueEvent(events::emu::SetEmulateSH2Cache(emulateSH2Cache));
            settings.system.emulateSH2Cache = emulateSH2Cache;
        }
        widgets::ExplanationTooltip("Enables emulation of the SH-2 cache.\n"
                                    "A few games require this to work properly.\n"
                                    "Reduces emulation performance by about 10%.\n\n"
                                    "Upon enabling this option, both SH-2 CPUs' caches will be flushed.");
        if (forced) {
            const YmirImGuiData *imguiData = GetYmirImGuiData();
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextColored(imguiData->colors.notice, "Forced by the currently loaded game");
        }
    }

    void SH2ClockFactor(SharedContext &ctx) {
        const float paddingWidth = ImGui::GetStyle().FramePadding.x;
        const float itemSpacingWidth = ImGui::GetStyle().ItemSpacing.x;
        const float resetButtonWidth = ImGui::CalcTextSize("Reset").x + paddingWidth * 2;

        auto &settings = ctx.serviceLocator.GetRequired<Settings>();
        int factor = settings.system.sh2ClockFactor.Get();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("SH-2 clock factor");
        widgets::ExplanationTooltip("WARNING: May break games, desync audio, or cause crashes.\n"
                                    "Use with caution!\n"
                                    "\n"
                                    "Adjusts the cycle rate of the SH-2 CPUs. Also affects SCU DSP and VDP1.\n"
                                    "\n"
                                    "Values over 100% can reduce slowdowns in CPU-intensive games.\n"
                                    "Values below 100% can improve performance on slower host CPUs.");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-(resetButtonWidth + itemSpacingWidth));
        if (settings.MakeDirty(ImGui::SliderInt(
                "##sh2_clock_factor", &factor, app::config_defaults::system::kMinSH2ClockFactor,
                app::config_defaults::system::kMaxSH2ClockFactor, "%d%%", ImGuiSliderFlags_AlwaysClamp))) {
            settings.system.sh2ClockFactor = factor;
        }
        ImGui::SameLine();
        if (settings.MakeDirty(ImGui::Button("Reset##sh2_clock_factor"))) {
            settings.system.sh2ClockFactor = app::config_defaults::system::kDefaultSH2ClockFactor;
        }
    }

} // namespace settings::system

namespace settings::video {

    void GraphicsBackendCombo(SharedContext &ctx) {
        auto &settings = ctx.serviceLocator.GetRequired<Settings>();
        auto &videoSettings = settings.video;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Graphics backend:");
        widgets::ExplanationTooltip(
            "Select the graphics API used to render the GUI.\n"
            "Affects availability of additional features such as GPU rendering and shaders.\n"
            "\n"
            "Changes are applied immediately. If the new graphics backend fails to initialize, this option "
            "automatically reverts to the last working backend option.\n"
            "\n"
            "SDL Renderer is offered as a fallback option and will not support any advanced graphics features.");
        ImGui::SameLine();
        if (ImGui::BeginCombo("##graphics_backend", gfx::GraphicsBackendName(videoSettings.graphicsBackend),
                              ImGuiComboFlags_HeightLarge | ImGuiComboFlags_WidthFitPreview)) {
            auto item = [&](gfx::Backend backend) {
                if (settings.MakeDirty(ImGui::Selectable(gfx::GraphicsBackendName(backend),
                                                         videoSettings.graphicsBackend == backend))) {
                    ctx.EnqueueEvent(events::gui::SwitchGraphicsBackend(backend, videoSettings.graphicsAdapter));
                }
            };
            for (gfx::Backend backend : gfx::kGraphicsBackends) {
                if (backend == gfx::Backend::Null) {
                    continue;
                }
                item(backend);
            }
            ImGui::EndCombo();
        }
    }

    void GraphicsAdapterCombo(SharedContext &ctx) {
        auto &settings = ctx.serviceLocator.GetRequired<Settings>();
        auto &videoSettings = settings.video;
        const gfx::Backend backend = videoSettings.graphicsBackend;

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Graphics adapter:");
        ImGui::SameLine();
        if (backend == gfx::Backend::SDLRenderer) {
            ImGui::TextDisabled("(unavailable for SDL Renderer)");
        } else {
            std::vector<gfx::Adapter> adapters = gfx::GetGraphicsAdapters(backend);
            std::string currAdapter;
            if (adapters.empty()) {
                currAdapter = "(No graphics adapters detected)";
            } else {
                currAdapter = adapters.front().ToString();
            }
            for (const gfx::Adapter &adapter : adapters) {
                if (adapter.id == videoSettings.graphicsAdapter) {
                    currAdapter = adapter.ToString();
                }
            }

            if (ImGui::BeginCombo("##graphics_adapter", currAdapter.c_str(),
                                  ImGuiComboFlags_HeightLarge | ImGuiComboFlags_WidthFitPreview)) {
                for (const gfx::Adapter &adapter : adapters) {
                    if (settings.MakeDirty(ImGui::Selectable(adapter.ToString().c_str(),
                                                             adapter.id == videoSettings.graphicsAdapter))) {
                        ctx.EnqueueEvent(events::gui::SwitchGraphicsBackend(backend, adapter.id));
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (ImGui::Button("Refresh##graphics_adapters")) {
                gfx::RefreshGraphicsAdapters(backend);
            }
        }
    }

    void UseHardwareAcceleration(SharedContext &ctx) {
        auto &settings = ctx.serviceLocator.GetRequired<Settings>();
        auto &videoSettings = settings.video;
        const bool isSDLRenderer = videoSettings.graphicsBackend == gfx::Backend::SDLRenderer;
        bool hwAccel = videoSettings.useHardwareAcceleration;
        if (isSDLRenderer) {
            ImGui::BeginDisabled();
        }
        if (settings.MakeDirty(ImGui::Checkbox("Use hardware-accelerated VDP1/VDP2 rendering", &hwAccel))) {
            videoSettings.useHardwareAcceleration = hwAccel;
            ctx.EnqueueEvent(events::emu::SwitchVDPRenderer());
        }
        widgets::ExplanationTooltip("Enables use of GPU compute shaders to accelerate VDP1 and VDP2 rendering.\n"
                                    "Greatly improves performance and enables additional enhancements.\n"
                                    "\n"
                                    "NOTE: Support for hardware acceleration is currently EXPERIMENTAL. You may "
                                    "encounter bugs, stability and performance issues.");
        if (isSDLRenderer) {
            widgets::ExplanationTooltip("Not supported with SDL Renderer");
            ImGui::EndDisabled();
        }
    }

    void DisplayRotation(SharedContext &ctx, bool newLine) {
        auto &settings = ctx.serviceLocator.GetRequired<Settings>();
        auto &videoSettings = settings.video;
        ImGui::PushID("##disp_rot");
        using Rot = Settings::Video::DisplayRotation;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Display rotation:");
        auto option = [&](const char *name, Rot value, bool sameLine = true) {
            if (sameLine) {
                ImGui::SameLine();
            }
            if (settings.MakeDirty(ImGui::RadioButton(name, videoSettings.rotation == value))) {
                videoSettings.rotation = value;
            }
        };
        option("Normal", Rot::Normal, !newLine);
        option("90\u00B0 CW", Rot::_90CW);
        option("180\u00B0", Rot::_180);
        option("90\u00B0 CCW", Rot::_90CCW);
        ImGui::PopID();
    }

    namespace swrenderer {

        void ThreadedVDP(SharedContext &ctx) {
            auto &settings = ctx.serviceLocator.GetRequired<Settings>();
            bool threadedVDP1 = settings.video.swRenderer.threadedVDP1;
            if (settings.MakeDirty(ImGui::Checkbox("Threaded VDP1 renderer", &threadedVDP1))) {
                ctx.EnqueueEvent(events::emu::EnableThreadedVDP1(threadedVDP1));
            }
            widgets::ExplanationTooltip("Runs the software VDP1 renderer in a dedicated thread.\n"
                                        "Slightly improves performance.\n"
                                        "When disabled, VDP1 rendering is done on the emulator thread.");

            bool threadedVDP2 = settings.video.swRenderer.threadedVDP2;
            if (settings.MakeDirty(ImGui::Checkbox("Threaded VDP2 renderer", &threadedVDP2))) {
                ctx.EnqueueEvent(events::emu::EnableThreadedVDP2(threadedVDP2));
            }
            widgets::ExplanationTooltip(
                "Runs the software VDP2 renderer in a dedicated thread.\n"
                "Greatly improves performance and seems to cause no issues to games.\n"
                "When disabled, VDP2 rendering is done on the emulator thread.\n"
                "\n"
                "It is HIGHLY recommended to leave this option enabled as there are no known drawbacks.");

            ImGui::Indent();
            {
                if (!threadedVDP2) {
                    ImGui::BeginDisabled();
                }

                bool threadedDeinterlacer = settings.video.swRenderer.threadedDeinterlacer;
                if (settings.MakeDirty(
                        ImGui::Checkbox("Use dedicated thread for deinterlaced rendering", &threadedDeinterlacer))) {
                    ctx.EnqueueEvent(events::emu::EnableThreadedDeinterlacer(threadedDeinterlacer));
                }
                widgets::ExplanationTooltip(
                    "If threaded VDP2 rendering and the deinterlace enhancement are both enabled, runs the "
                    "deinterlacer on a dedicated thread.\n"
                    "Significantly improves performance of the enhancement on CPUs with enough spare cores.\n"
                    "Requires a quad-core CPU or better for best results.\n"
                    "\n"
                    "It is HIGHLY recommended to leave this option enabled if your CPU meets the requirements.");

                if (!threadedVDP2) {
                    ImGui::EndDisabled();
                }
            }
            ImGui::Unindent();
        }

    } // namespace swrenderer

    namespace enhancements {

        void Deinterlace(SharedContext &ctx) {
            auto &settings = ctx.serviceLocator.GetRequired<Settings>();
            auto &videoSettings = settings.video;
            bool deinterlace = videoSettings.enhancements.deinterlace.Get();
            if (settings.MakeDirty(ImGui::Checkbox("Deinterlace video", &deinterlace))) {
                videoSettings.enhancements.deinterlace = deinterlace;
            }
            widgets::ExplanationTooltip(
                "When enabled, interlaced high-resolution modes will be rendered in progressive mode.\n"
                "Noticeably impacts performance in those modes when enabled.\n"
                "It is highly recommended to enable the \"Threaded VDP2 renderer\" and \"Use dedicated thread for "
                "deinterlaced rendering\" options alongside this to lessen the performance impact.\n"
                "A quad-core CPU or better is recommended to use this option.\n"
                "\n"
                "Very few games may exhibit graphics artifacts when this option is enabled. These are the known cases "
                "so "
                "far:\n"
                "- True Pinball displays the bottom half of the board interleaved with the top half at the top of the "
                "screen\n"
                "- Shienryuu and Pro-Pinball: The Web's graphics jitter");
        }

        void TransparentMeshes(SharedContext &ctx) {
            auto &settings = ctx.serviceLocator.GetRequired<Settings>();
            auto &videoSettings = settings.video;
            bool transparentMeshes = videoSettings.enhancements.transparentMeshes.Get();
            if (settings.MakeDirty(ImGui::Checkbox("Transparent meshes", &transparentMeshes))) {
                videoSettings.enhancements.transparentMeshes = transparentMeshes;
            }
            widgets::ExplanationTooltip(
                "When enabled, meshes (checkerboard patterns) will be rendered as transparent polygons instead.");
        }

        void ResolutionScaling(SharedContext &ctx) {
            using namespace app::config_defaults::video;
            auto &settings = ctx.serviceLocator.GetRequired<Settings>();
            auto &videoSettings = settings.video;

            bool resolutionScaling = videoSettings.enhancements.resolutionScaling.Get();
            if (settings.MakeDirty(ImGui::Checkbox("Internal resolution scaling", &resolutionScaling))) {
                videoSettings.enhancements.resolutionScaling = resolutionScaling;
            }
            widgets::ExplanationTooltip(
                "When enabled, the emulator will render VDP1 and VDP2 graphics at an increased resolution.");
            if (!videoSettings.useHardwareAcceleration) {
                widgets::WarningTooltip("Requires hardware-accelerated VDP1/VDP2 rendering to work.");
            }

            ImGui::Indent();
            {
                bool nativeRes = videoSettings.enhancements.resScaleToDisplaySize.Get();
                if (settings.MakeDirty(ImGui::Checkbox("Scale to native resolution", &nativeRes))) {
                    videoSettings.enhancements.resScaleToDisplaySize = nativeRes;
                }
                widgets::ExplanationTooltip("When enabled, the VDP2 framebuffer will be scaled to match the display "
                                            "size in the user interface.\n"
                                            "Overrides the resolution factor option below if enabled.");

                uint32 scaleFactor = videoSettings.enhancements.resScaleFactor;
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Resolution factor:");
                if (nativeRes) {
                    widgets::WarningTooltip("Overridden by the \"Scale to native resolution\" option above.");
                }
                ImGui::SameLine();
                if (ImGui::SliderScalar("##res_factor", ImGuiDataType_U32, &scaleFactor, &vdp::kMinResScaleFactor,
                                        &vdp::kMaxResScaleFactor, "%ux", ImGuiSliderFlags_AlwaysClamp)) {
                    videoSettings.enhancements.resScaleFactor = scaleFactor;
                }
            }
            ImGui::Unindent();
        }

    } // namespace enhancements

} // namespace settings::video

namespace settings::audio {

    void InterpolationMode(SharedContext &ctx) {
        auto &settings = ctx.serviceLocator.GetRequired<Settings>();
        auto &audioSettings = settings.audio;

        using InterpMode = ymir::core::config::audio::SampleInterpolationMode;

        auto interpOption = [&](const char *name, InterpMode mode) {
            const std::string label = fmt::format("{}##sample_interp", name);
            ImGui::SameLine();
            if (settings.MakeDirty(ImGui::RadioButton(label.c_str(), audioSettings.interpolation == mode))) {
                audioSettings.interpolation = mode;
            }
        };

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Interpolation:");
        widgets::ExplanationTooltip("- Nearest neighbor: Cheapest option with grittier sounds.\n"
                                    "- Linear: Hardware accurate option with softer sounds. (default)");
        interpOption("Nearest neighbor", InterpMode::NearestNeighbor);
        interpOption("Linear", InterpMode::Linear);
    }

    std::string StepGranularityToString(uint32 stepGranularity) {
        const uint32 numSteps = 32u >> stepGranularity;
        return fmt::format("{} {}{}", numSteps, (numSteps != 1 ? "slots" : "slot"),
                           (numSteps == 32 ? " (1 sample)" : ""));
    }

    void StepGranularity(SharedContext &ctx) {
        const YmirImGuiData *imguiData = GetYmirImGuiData();
        auto &settings = ctx.serviceLocator.GetRequired<Settings>();
        auto &audioSettings = settings.audio;
        int stepGranularity = audioSettings.stepGranularity;

        if (ImGui::BeginTable("scsp_step_granularity", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 200.0f * imguiData->displayScale);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableNextRow();
            if (ImGui::TableNextColumn()) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Emulation step granularity");
                widgets::ExplanationTooltip(
                    "WARNING: This setting is very performance-hungry!\n"
                    "\n"
                    "Increasing this setting causes the SCSP to be emulated in smaller timeslices (up to 32 times as "
                    "often as sample-level processing), significantly lowering performance in exchange for a higher "
                    "level of accuracy that doesn't benefit the vast majority of commercial games.\n"
                    "\n"
                    "Very few games require this setting to be tweaked. It is recommended to leave it at 0 in most "
                    "cases.\n"
                    "\n"
                    "This option might be of interest to homebrew developers who need extra accuracy in some way.");
            }
            if (ImGui::TableNextColumn()) {
                ImGui::SetNextItemWidth(-1.0f);
                if (settings.MakeDirty(ImGui::SliderInt("##scsp_step_granularity", &stepGranularity, 0, 5, "%d",
                                                        ImGuiSliderFlags_AlwaysClamp))) {
                    audioSettings.stepGranularity = stepGranularity;
                }
            }
            ImGui::TableNextRow();
            if (ImGui::TableNextColumn()) {
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Step size: %s", StepGranularityToString(stepGranularity).c_str());
                widgets::ExplanationTooltip(
                    "The entire bar encompasses one sample. The SCSP processes 32 slots per sample, represented by the "
                    "subdivisions in the bar.\n\n"
                    "The different colored regions indicate which portions of the sample are emulated on each step "
                    "with the current granularity setting. Higher granularity results in tighter synchronization "
                    "between the SCSP and other components (more accuracy) but lower performance due to additional "
                    "context switching.");
            }
            if (ImGui::TableNextColumn()) {
                static constexpr ImU32 kGraphBackgroundColor = 0xAA253840;
                static constexpr ImU32 kGraphSliceFillColor = 0xE04AC3F7;
                static constexpr ImU32 kGraphSliceFillColorAlt = 0xE02193C4;
                static constexpr ImU32 kGraphSlotSeparatorColor = 0xE02A6F8C;

                const auto initBasePos = ImGui::GetCursorScreenPos();
                const auto totalAvail = ImGui::GetContentRegionAvail();
                const auto cellPadding = ImGui::GetStyle().CellPadding;
                const auto basePos = ImVec2(initBasePos.x, initBasePos.y + cellPadding.y);
                const auto avail = ImVec2(totalAvail.x, totalAvail.y - cellPadding.y * 2.0f);
                const float graphWidth = avail.x;
                const float graphHeight = ImGui::GetFrameHeight();
                const float sliceWidth = graphWidth / (1 << stepGranularity);
                const float slotWidth = graphWidth / 32.0f;
                const float sepThickness = 1.5f * imguiData->displayScale;

                auto *drawList = ImGui::GetWindowDrawList();

                drawList->AddRectFilled(basePos, ImVec2(basePos.x + graphWidth, basePos.y + graphHeight),
                                        kGraphBackgroundColor);
                for (uint32 i = 0; i < (1 << stepGranularity); ++i) {
                    const float xStart = basePos.x + i * sliceWidth;
                    const float xEnd = xStart + sliceWidth;
                    drawList->AddRectFilled(ImVec2(xStart, basePos.y), ImVec2(xEnd, basePos.y + graphHeight),
                                            (i & 1) ? kGraphSliceFillColorAlt : kGraphSliceFillColor);
                }
                for (uint32 i = 1; i < 32; ++i) {
                    const float x = basePos.x + i * slotWidth;
                    drawList->AddLine(ImVec2(x, basePos.y), ImVec2(x, basePos.y + graphHeight),
                                      kGraphSlotSeparatorColor, sepThickness);
                }

                ImGui::Dummy(ImVec2(graphWidth, graphHeight));
            }

            ImGui::EndTable();
        }
    }

    void ThreadedSCSP(SharedContext &ctx) {
        auto &settings = ctx.serviceLocator.GetRequired<Settings>();
        bool threadedSCSP = settings.audio.threadedSCSP;
        if (settings.MakeDirty(ImGui::Checkbox("Threaded SCSP and sound CPU", &threadedSCSP))) {
            ctx.EnqueueEvent(events::emu::EnableThreadedSCSP(threadedSCSP));
        }
        widgets::ExplanationTooltip("Runs the SCSP and MC68EC000 in a dedicated thread.\n"
                                    "Improves performance at the cost of accuracy.\n"
                                    "A few select games may break when this option is enabled.");
    }

} // namespace settings::audio

namespace settings::cdblock {

    void CDReadSpeed(SharedContext &ctx) {
        auto &settings = ctx.serviceLocator.GetRequired<Settings>();
        auto &cdblockSettings = settings.cdblock;

        if (settings.cdblock.useLLE) {
            ImGui::BeginDisabled();
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("CD read speed");
        widgets::ExplanationTooltip("Changes the maximum read speed of the emulated CD drive.\n"
                                    "The default value is 2x, matching the real Saturn's CD drive speed.\n"
                                    "Higher speeds decrease load times but may reduce compatibility.\n"
                                    "\n"
                                    "This option is unavailable when using low level CD block emulation.");

        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        static constexpr uint8 kMinReadSpeed = 2u;
        static constexpr uint8 kMaxReadSpeed = 200u;
        uint8 readSpeed = cdblockSettings.readSpeedFactor;
        if (settings.MakeDirty(ImGui::SliderScalar("##read_speed", ImGuiDataType_U8, &readSpeed, &kMinReadSpeed,
                                                   &kMaxReadSpeed, "%ux", ImGuiSliderFlags_AlwaysClamp))) {
            cdblockSettings.readSpeedFactor = readSpeed;
        }
        if (settings.cdblock.useLLE) {
            ImGui::EndDisabled();
        }
    }

    void CDBlockLLE(SharedContext &ctx) {
        auto &settings = ctx.serviceLocator.GetRequired<Settings>();
        auto &cdblockSettings = settings.cdblock;

        bool hasROMs;
        {
            std::unique_lock lock{ctx.locks.romManager};
            hasROMs = !ctx.romManager.GetCDBlockROMs().empty();
        }
        if (!hasROMs) {
            ImGui::BeginDisabled();
        }
        if (settings.MakeDirty(ImGui::Checkbox("Use low level CD Block emulation", &cdblockSettings.useLLE))) {
            ctx.EnqueueEvent(events::emu::SetCDBlockLLE(cdblockSettings.useLLE));
        }
        widgets::ExplanationTooltip("Choose between high or low level CD Block emulation.\n"
                                    "High level emulation is faster, but has lower compatibility.\n"
                                    "Low level emulation is much more accurate, but also more demanding and requires a "
                                    "valid CD block ROM image.\n"
                                    "\n"
                                    "Changing this option causes a hard reset.");
        if (!hasROMs) {
            const YmirImGuiData *imguiData = GetYmirImGuiData();
            ImGui::EndDisabled();
            ImGui::TextColored(imguiData->colors.warn,
                               "No CD Block ROMs found. Low level emulation cannot be enabled.");
        }
    }

} // namespace settings::cdblock

} // namespace app::ui::widgets
