#pragma once

/**
@file
@brief Defines the VDP2 memory write tracing interface.
*/

#include <ymir/core/types.hpp>

namespace ymir::debug {

enum class VDP2MemoryType : uint8 {
    VRAM,
    CRAM,
};

/// Receives committed VDP2 VRAM and CRAM writes from the emulator thread.
/// The callback reports the mapped destination address; it does not identify
/// the CPU, DMA channel, or source address that initiated the write.
struct IVDP2MemoryTracer {
    static constexpr uint64 kUnknownVdp1FrameIndex = ~uint64{0};

    virtual ~IVDP2MemoryTracer() = default;

    virtual bool IsEnabled() const { return true; }
    /// Invoked when the VDP resets, so frame indices from separate runs are not mixed.
    virtual void Reset() {}
    /// @param[in] vdp1Frame the current VDP1 frame index, or zero before its first frame
    virtual void Write(uint64 vdp1Frame, VDP2MemoryType type, uint32 address, uint8 width, uint32 value) {}
};

} // namespace ymir::debug
