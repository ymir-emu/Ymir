#pragma once

/**
@file
@brief Defines the VDP1 command tracing interface.
*/

#include <ymir/core/types.hpp>

#include <array>

namespace ymir::debug {

/// Receives VDP1 command-table events. Attach with `VDP::UseTracer`.
struct IVDPTracer {
    virtual ~IVDPTracer() = default;

    /// Whether command words should be copied for this frame.
    virtual bool IsEnabled() const { return true; }
    virtual void BeginFrame(uint64) {}
    /// `words` contains the 16 command words read from VDP1 VRAM in command order.
    virtual void Command(uint64, uint32, uint32, uint32, sint32, sint32, const std::array<uint16, 16> &) {}
};

} // namespace ymir::debug
