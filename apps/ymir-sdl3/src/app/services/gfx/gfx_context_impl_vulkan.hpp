#pragma once

#include "gfx_context.hpp"

namespace app::gfx {

struct VulkanGraphicsContextSpec;

class VulkanGraphicsContext final : public IGraphicsContext {
    struct Impl;

public:
    static constexpr Backend kBackend = Backend::Vulkan;

    VulkanGraphicsContext(const VulkanGraphicsContextSpec &spec);
    ~VulkanGraphicsContext();

    /// @brief Creates a Vulkan graphics context.
    /// @param[in] spec the backend specifications
    /// @return the graphics context instance or an error message
    static util::ObjectResult<VulkanGraphicsContext> Create(const VulkanGraphicsContextSpec &spec);

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
                                          const FPoint2D *anchorPoint = nullptr) override;

    util::VoidResult<> SetPresentMode(PresentMode mode) override;
    util::ValueResult<PresentResult> Present() override;

private:
    std::unique_ptr<Impl> m_impl;

    bool m_imguiInitialized = false;
};

} // namespace app::gfx
