#pragma once

#include <app/shared_context.hpp>

namespace app::ui::widgets {

namespace settings::system {

    void EmulateSH2Cache(SharedContext &ctx);
    void SH2ClockFactor(SharedContext &ctx);

} // namespace settings::system

namespace settings::video {

    void GraphicsBackendCombo(SharedContext &ctx);
    void GraphicsAdapterCombo(SharedContext &ctx);
    void UseHardwareAcceleration(SharedContext &ctx);

    void DisplayRotation(SharedContext &ctx, bool newLine = false);

    namespace swrenderer {

        void ThreadedVDP(SharedContext &ctx);

    } // namespace swrenderer

    namespace enhancements {

        void Deinterlace(SharedContext &ctx);
        void TransparentMeshes(SharedContext &ctx);
        void ResolutionScaling(SharedContext &ctx);

    } // namespace enhancements

} // namespace settings::video

namespace settings::audio {

    void InterpolationMode(SharedContext &ctx);
    void StepGranularity(SharedContext &ctx);
    void ThreadedSCSP(SharedContext &ctx);

    std::string StepGranularityToString(uint32 stepGranularity);

} // namespace settings::audio

namespace settings::cdblock {

    void CDReadSpeed(SharedContext &ctx);
    void CDBlockLLE(SharedContext &ctx);

} // namespace settings::cdblock

} // namespace app::ui::widgets
