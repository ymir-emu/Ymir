#pragma once

#include "vdp1_registers_window.hpp"
#include "vdp1_command_trace_window.hpp"
#include "vdp2_bg_layer_params_window.hpp"
#include "vdp2_color_calc_params_window.hpp"
#include "vdp2_cram_window.hpp"
#include "vdp2_memory_trace_window.hpp"
#include "vdp2_debug_overlay_window.hpp"
#include "vdp2_layer_visibility_window.hpp"
#include "vdp2_registers_window.hpp"
#include "vdp2_sprite_layer_params_window.hpp"
#include "vdp2_vram_access_patterns_window.hpp"
#include "vdp2_window_params_window.hpp"

namespace app::ui {

struct VDPWindowSet {
    VDPWindowSet(SharedContext &context)
        : vdp1Regs(context)
        , vdp1CommandTrace(context)
        , vdp2Regs(context)
        , vdp2LayerVisibility(context)
        , vdp2BGLayerParams(context)
        , vdp2SpriteLayerParams(context)
        , vdp2ColorCalcParams(context)
        , vdp2WindowParams(context)
        , vdp2DebugOverlay(context)
        , vdp2VRAMAccessPatterns(context)
        , vdp2CRAM(context)
        , vdp2MemoryTrace(context) {}

    void DisplayAll() {
        vdp1Regs.Display();
        vdp1CommandTrace.Display();
        vdp2Regs.Display();
        vdp2LayerVisibility.Display();
        vdp2BGLayerParams.Display();
        vdp2SpriteLayerParams.Display();
        vdp2ColorCalcParams.Display();
        vdp2WindowParams.Display();
        vdp2DebugOverlay.Display();
        vdp2VRAMAccessPatterns.Display();
        vdp2CRAM.Display();
        vdp2MemoryTrace.Display();
    }

    VDP1RegistersWindow vdp1Regs;
    VDP1CommandTraceWindow vdp1CommandTrace;
    VDP2RegistersWindow vdp2Regs;
    VDP2LayerVisibilityWindow vdp2LayerVisibility;
    VDP2BGLayerParamsWindow vdp2BGLayerParams;
    VDP2SpriteLayerParamsWindow vdp2SpriteLayerParams;
    VDP2ColorCalcParamsWindow vdp2ColorCalcParams;
    VDP2WindowParamsWindow vdp2WindowParams;
    VDP2DebugOverlayWindow vdp2DebugOverlay;
    VDP2VRAMAccessPatternsWindow vdp2VRAMAccessPatterns;
    VDP2CRAMWindow vdp2CRAM;
    VDP2MemoryTraceWindow vdp2MemoryTrace;
};

} // namespace app::ui
