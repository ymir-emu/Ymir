#pragma once

#include "vdp_window_base.hpp"

namespace app::ui {

class VDP2MemoryTraceWindow final : public VDPWindowBase {
public:
    explicit VDP2MemoryTraceWindow(SharedContext &context);

protected:
    void DrawContents() override;
};

} // namespace app::ui
