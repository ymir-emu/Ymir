#pragma once

#include "gfx_context.hpp"

// -----------------------------------------------------------------------------
// Forward declarations

struct ID3D12Device;
struct ID3D12Resource;
struct ID3D12Fence;

// -----------------------------------------------------------------------------
// Implementation

namespace app::gfx {

struct Direct3D12GraphicsContextSpec;

class Direct3D12GraphicsContext final : public IGraphicsContext {
    struct Impl;

public:
    static constexpr Backend kBackend = Backend::Direct3D12;

    Direct3D12GraphicsContext(const Direct3D12GraphicsContextSpec &spec);
    ~Direct3D12GraphicsContext();

    /// @brief Creates a Direct3D 12 graphics context.
    /// @param[in] spec the backend specifications
    /// @return the graphics context instance or an error message
    static util::ObjectResult<Direct3D12GraphicsContext> Create(const Direct3D12GraphicsContextSpec &spec);

    util::VoidResult<> Initialize() override;
    void Shutdown() override;
    bool IsInitialized() const override;

    util::VoidResult<> ResizeFramebuffer(uint32 width, uint32 height) override;

    void ClearScreen(gfx::ColorRGBA color) override;

    bool ImGuiInit() override;
    void ImGuiShutdown() override;
    void ImGuiNewFrame() override;
    void ImGuiRenderFrame() override;

    util::ValueResult<TextureID> CreateTexture(const Texture2DSpec &spec) override;
    void DestroyTexture(TextureID id) override;
    bool IsTextureValid(TextureID id) const override;
    ImTextureID GetImGuiTextureID(TextureID id) const override;
    util::VoidResult<> ResizeTexture(TextureID id, uint32 width, uint32 height) override;
    util::VoidResult<> UpdateTexture(TextureID id, const IRect *rect,
                                     const std::function<void(void *data, size_t pitch)> &fnUpdate) override;
    util::VoidResult<> RenderToTexture(TextureID src, TextureID dst, const FRect &srcRect,
                                       const FRect &dstRect) override;
    util::VoidResult<> DrawTextureRotated(TextureID id, const FRect &srcRect, const FRect &dstRect, double rotAngle,
                                          const FPoint2D *pivot = nullptr) override;

    std::optional<DisplayTextureSpec> AcquireCurrentDisplayOutputTexture() override;
    void ReleaseCurrentDisplayOutputTexture() override;
    void ResetDisplayOutputTextures() override;
    util::ValueResult<size_t> DownloadDisplayOutputTexture(void *buffer, size_t size) override;

    util::VoidResult<> SetPresentMode(PresentMode mode) override;
    util::ValueResult<PresentResult> Present() override;

    /// @brief Retrieves a pointer to the `ID3D12Device` managed by this graphics context.
    /// @return a pointer to the context's Direct3D 12 device instance
    ID3D12Device *GetDevice() const;

    /// @brief Retrieves a pointer to the next free display output texture, or `nullptr` if no slots are available.
    /// The frame also stores three sizes:
    /// - The internal display texture size, tracking the maximum supported resolution at the given scale:
    ///   - 704x512 when not scaling
    ///   - a multiple of 352x256 when scaling by a factor
    ///   - the target resolution when scaling to a fixed resolution
    /// - The rendering area size, which drawn to the top-left corner of the internal display texture
    /// - The native VDP2 resolution, for aspect ratio calculations
    ///
    /// @param[in] fence the compute fence to wait for
    /// @param[in] fenceValue the fence value to wait for
    /// @param[in] textureWidth the requested display texture width
    /// @param[in] textureHeight the requested display texture height
    /// @param[in] renderWidth the render area width
    /// @param[in] renderHeight the render area height
    /// @param[in] nativeWidth the native VDP2 resolution width
    /// @param[in] nativeHeight the native VDP2 resolution height
    /// @return a pointer to the next free display output frame, or `nullptr` if no frame is available
    ID3D12Resource *GetNextDisplayOutputTexture(ID3D12Fence *fence, uint64 fenceValue, uint32 textureWidth,
                                                uint32 textureHeight, uint32 renderWidth, uint32 renderHeight,
                                                uint32 nativeWidth, uint32 nativeHeight);

private:
    std::unique_ptr<Impl> m_impl;

    bool m_imguiInitialized = false;
};

} // namespace app::gfx
