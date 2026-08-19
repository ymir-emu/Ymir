#include "gfx_context_impl_vulkan.hpp"

#include "gfx_context_spec_vulkan.hpp"

namespace app::gfx {

// -----------------------------------------------------------------------------

struct VulkanGraphicsContext::Impl {
    explicit Impl(const VulkanGraphicsContextSpec &spec)
        : spec(spec) {}

    static constexpr UINT kFrameCount = 3;

    VulkanGraphicsContextSpec spec;

    vk::UniqueInstance instance;
    vk::UniqueDevice device;
    vk::PhysicalDevice physical_device;

    util::VoidResult<> Init() {
        if (spec.window == nullptr) {
            return util::ErrorMessage{"No window provided to Vulkan specification"};
        }



    }
};

// -----------------------------------------------------------------------------

VulkanGraphicsContext::VulkanGraphicsContext(const VulkanGraphicsContextSpec &spec)
    : IGraphicsContext(kBackend)
    , m_impl(std::make_unique<Impl>(spec)) {}

VulkanGraphicsContext::~VulkanGraphicsContext() {}

util::ObjectResult<VulkanGraphicsContext> VulkanGraphicsContext::Create(const VulkanGraphicsContextSpec &spec) {
    auto context = std::make_unique<VulkanGraphicsContext>(spec);
    auto result = context->Initialize();
    if (!result) {
        return result.Error();
    }
    return std::move(context);
}

util::VoidResult<> VulkanGraphicsContext::Initialize() {
    return util::ErrorMessage{"Unimplemented"};
}

void VulkanGraphicsContext::Shutdown() {}

bool VulkanGraphicsContext::IsInitialized() const {
    return false;
}

util::VoidResult<> VulkanGraphicsContext::ResizeFramebuffer(uint32 width, uint32 height) {
    // TODO: destroy and recreate swap chain resources
    return util::ErrorMessage{"Unimplemented"};
}

void VulkanGraphicsContext::ClearScreen(gfx::ColorRGBA color) {
    // TODO: enqueue command to clear screen
}

bool VulkanGraphicsContext::ImGuiInit() {
    // TODO: invoke the appropriate ImGui_Impl*_Init* functions
    return false;
}

void VulkanGraphicsContext::ImGuiShutdown() {
    // TODO: invoke the appropriate ImGui_Impl*_Shutdown* functions
}

void VulkanGraphicsContext::ImGuiNewFrame() {
    // TODO: invoke the appropriate ImGui_Impl*_NewFrame functions
}

void VulkanGraphicsContext::ImGuiRenderFrame() {
    // TODO: invoke the appropriate ImGui_Impl*_RenderDrawData function
}

util::ValueResult<TextureID> VulkanGraphicsContext::CreateTexture(const Texture2DSpec &spec) {
    // TODO: create and store texture object in a hash map
    // The texture ID will be the hash map key, not the native object pointer, because resizing the texture requires
    // creating a new object and these IDs must be immutable for the lifetime of the logical texture.
    return util::ErrorMessage{"Unimplemented"};
}

void VulkanGraphicsContext::DestroyTexture(TextureID id) {
    // TODO: delete texture
}

bool VulkanGraphicsContext::IsTextureValid(TextureID id) const {
    // TODO: check if the texture is still live
    return true;
}

ImTextureID VulkanGraphicsContext::GetImGuiTextureID(TextureID id) const {
    // TODO: get and return texture ID
    return 0;
}

util::VoidResult<> VulkanGraphicsContext::ResizeTexture(TextureID id, uint32 width, uint32 height) {
    // TODO: destroy and recreate texture with new dimensions
    return util::ErrorMessage{"Unimplemented"};
}

util::VoidResult<> VulkanGraphicsContext::UpdateTexture(TextureID id, const IRect *rect,
                                                        const std::function<void(void *data, size_t pitch)> &fnUpdate) {
    // TODO: map texture, invoke fnUpdate with contents, unmap texture; handle errors
    return util::ErrorMessage{"Unimplemented"};
}

util::VoidResult<> VulkanGraphicsContext::RenderToTexture(TextureID src, TextureID dst, const FRect &srcRect,
                                                          const FRect &dstRect) {
    // TODO: set render target to dst texture, draw texture, restore render target
    return util::ErrorMessage{"Unimplemented"};
}

util::VoidResult<> VulkanGraphicsContext::DrawTextureRotated(TextureID id, const FRect &srcRect, const FRect &dstRect,
                                                             double rotAngle, const FPoint2D *anchorPoint) {
    // TODO: imitate SDL_RenderTextureRotated:
    // - srcRect specifies the source texture region to copy from (in texels)
    // - dstRect specifies the destination texture region to copy to (in texels)
    // - rotAngle is the clockwise rotation angle (in degrees)
    // - anchorPoint is the rotation anchor point. If null, use the center of the destination rectangle
    return util::ErrorMessage{"Unimplemented"};
}

std::optional<DisplayTextureSpec> VulkanGraphicsContext::AcquireCurrentDisplayOutputTexture() {
    // TODO: find and update index of the latest complete display output texture, emit transition to copy destination
    // barrier and return its texture ID, width and height
    return std::nullopt;
}

void VulkanGraphicsContext::ReleaseCurrentDisplayOutputTexture() {
    // TODO: if the display texture was previously acquired, emit a transition to pixel shading barrier and mark as
    // released
}

void VulkanGraphicsContext::ResetDisplayOutputTextures() {
    // TODO: clear display texture state
}

util::ValueResult<size_t> VulkanGraphicsContext::DownloadDisplayOutputTexture(void *buffer, size_t size) {
    // TODO: if using hardware renderer, copy latest display output texture to buffer and return number of bytes copied.
    // Return 0 if using software renderer.
    return util::ErrorMessage{"Unimplemented"};
}

util::VoidResult<> VulkanGraphicsContext::SetPresentMode(PresentMode mode) {
    // TODO: set presentation mode
    return util::ErrorMessage{"Unimplemented"};
}

util::ValueResult<PresentResult> VulkanGraphicsContext::Present() {
    // TODO: present next frame and wait for vertical retrace if enabled
    return util::ErrorMessage{"Unimplemented"};
}

} // namespace app::gfx
