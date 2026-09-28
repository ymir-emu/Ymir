#pragma once

#include <ymir/hw/vdp/vdp_common_defs.hpp>

#include <ymir/util/callback.hpp>

#include <ymir/core/types.hpp>

// -----------------------------------------------------------------------------
// Forward declarations

struct ID3D12Resource;
struct ID3D12Fence;

// -----------------------------------------------------------------------------

namespace ymir::vdp {

/// @brief Type of callback invoked when the Direct3D 12 VDP renderer is about to begin a new frame, enabling the
/// frontend to react to resolution changes before a frame is drawn.
///
/// @param[in] nativeRes the native VDP2 display resolution
using CBDirect3D12FrameBeginCallback = util::OptionalCallback<void(Dimensions nativeRes)>;

/// @brief Type of callback invoked when the Direct3D 12 VDP renderer requests a frame to copy the finished output frame
/// into. The returned resource (if any) must be a 2D texture with the requested dimensions using the `R8G8B8A8_UNORM`
/// format, in the `COPY_DEST` state and not referenced by any in-flight frames when returned. The frontend may
/// choose to perform a CPU wait for the texture to be free if necessary.
///
/// @param[in] computeFence the compute fence to wait on
/// @param[in] fenceValue the fence value to wait for
/// @param[in] requestedSize the requested texture width and height
/// @param[in] renderArea the size of the area within the requested texture in which the screen will be drawn, with the
/// origin at 0x0 (top-left) corner of the texture
/// @param[in] nativeRes the native VDP2 display resolution
/// @return a pointer to a 2D texture with the requested dimensions, using `R8G8B8A8_UNORM` pixel format, and in the
/// `COPY_DEST` state. Return `nullptr` to omit the copy for this frame.
using CBDirect3D12FrameCopyRequestCallback =
    util::OptionalCallback<ID3D12Resource *(ID3D12Fence *computeFence, uint64 fenceValue, Dimensions requestedSize,
                                            Dimensions renderArea, Dimensions nativeRes)>;

/// @brief Callbacks specific to the Direct3D 12 VDP renderer.
struct Direct3D12RendererCallbacks {
    CBDirect3D12FrameBeginCallback FrameBegin;
    CBDirect3D12FrameCopyRequestCallback FrameCopyRequest;
};

} // namespace ymir::vdp
