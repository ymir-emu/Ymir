#pragma once

/**
@file
@brief VDP1 and VDP2 renderer implementation using Direct3D 12.

Requires Shader Model 6.0.
*/

#include <ymir/hw/vdp/renderer/vdp_renderer_hw_base.hpp>

#include "vdp_renderer_hw_d3d12_callbacks.hpp"

#include <ymir/util/result.hpp>

#include <memory>

// -----------------------------------------------------------------------------
// Forward declarations

struct ID3D12Device;

// -----------------------------------------------------------------------------

namespace ymir::vdp {

/// @brief VDP renderer implementation using Direct3D 12.
/// Requires a valid `ID3D12Device *` instance, which has its reference count increments to ensure all managed objects
/// kept alive for the lifetime of the renderer.
class Direct3D12VDPRenderer : public HardwareVDPRendererBase {
    Direct3D12VDPRenderer(VDPState &state, const config::VDP2DebugRender &vdp2DebugRenderOptions,
                          const config::VDP2AccessPatternsConfig &vdp2AccessPatternsConfig);

    util::VoidResult<> Initialize(ID3D12Device *device);

public:
    ~Direct3D12VDPRenderer();

    static util::ObjectResult<Direct3D12VDPRenderer>
    Create(VDPState &state, const config::VDP2DebugRender &vdp2DebugRenderOptions,
           const config::VDP2AccessPatternsConfig &vdp2AccessPatternsConfig, ID3D12Device *device);

    // -------------------------------------------------------------------------
    // Configuration

    Direct3D12RendererCallbacks HwCallbacks;

    // -------------------------------------------------------------------------
    // Basics

    bool IsValid() const override;

    void Reset(bool hard) override;

    // -------------------------------------------------------------------------
    // Save states

    void PreSaveStateSync() override;
    void PostLoadStateSync() override;

    void SaveState(savestate::VDPSaveState::VDPRendererSaveState &state) override;
    bool ValidateState(const savestate::VDPSaveState::VDPRendererSaveState &state) const override;
    void LoadState(const savestate::VDPSaveState::VDPRendererSaveState &state) override;

    // -------------------------------------------------------------------------
    // VDP1 memory and register writes

    void VDP1WriteVRAM(uint32 address, uint8 value) override;
    void VDP1WriteVRAM(uint32 address, uint16 value) override;
    void VDP1SyncFB() override;
    void VDP1DebugSyncFB() override;
    void VDP1WriteFB(uint32 address, uint8 value) override;
    void VDP1WriteFB(uint32 address, uint16 value) override;
    void VDP1WriteReg(uint32 address, uint16 value) override;

    // -------------------------------------------------------------------------
    // VDP2 memory and register writes

    void VDP2WriteVRAM(uint32 address, uint8 value) override;
    void VDP2WriteVRAM(uint32 address, uint16 value) override;
    void VDP2WriteCRAM(uint32 address, uint8 value) override;
    void VDP2WriteCRAM(uint32 address, uint16 value) override;
    void VDP2WriteReg(uint32 address, uint16 value) override;

    // -------------------------------------------------------------------------
    // Debugger

    void UpdateEnabledLayers() override;

    // -------------------------------------------------------------------------
    // Utilities

    void DumpExtraVDP1Framebuffers(std::ostream &out) const override;

    // -------------------------------------------------------------------------
    // Rendering process

    void VDP1EraseFramebuffer(uint64 cycles) override;
    void VDP1SwapFramebuffer() override;
    void VDP1BeginFrame() override;
    void VDP1ExecuteCommand(uint32 cmdAddress, VDP1Command::Control control) override;
    void VDP1EndFrame() override;

    void VDP2SetResolution(uint32 h, uint32 v, bool exclusive) override;
    void VDP2SetField(bool odd) override;
    void VDP2LatchTVMD() override;
    void VDP2BeginFrame() override;
    void VDP2RenderLine(uint32 y) override;
    void VDP2EndFrame() override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace ymir::vdp
