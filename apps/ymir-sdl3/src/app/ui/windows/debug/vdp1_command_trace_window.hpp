#pragma once

#include "vdp_window_base.hpp"

namespace app::ui {

class VDP1CommandTraceWindow final : public VDPWindowBase {
public:
    explicit VDP1CommandTraceWindow(SharedContext &context);

protected:
    void DrawContents() override;
};

} // namespace app::ui
