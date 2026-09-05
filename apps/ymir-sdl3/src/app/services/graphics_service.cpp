#include "graphics_service.hpp"

#include "gfx/gfx_context_impls.hpp"
#include "gfx/gfx_context_specs.hpp"

#if YMIR_PLATFORM_HAS_DIRECT3D
    #include "gfx/gfx_d3d_utils.hpp"
#endif
#if YMIR_PLATFORM_HAS_VULKAN
    #include "gfx/gfx_vulkan_utils.hpp"
#endif
#if YMIR_PLATFORM_HAS_METAL
    #include "gfx/gfx_metal_utils.hpp"
#endif

#include <ymir/hw/vdp/vdp.hpp>

#include <SDL3/SDL_video.h>

#include <imgui.h>

#include <cassert>
#include <functional>
#include <memory>
#include <utility>

using namespace app::gfx;

namespace app::services {

GraphicsService::GraphicsService(Settings &settings)
    : m_settings(settings)
    , m_gfxContext(std::make_unique<NullGraphicsContext>()) {}

GraphicsService::~GraphicsService() {}

void GraphicsService::RegisterHardwareRendererCallbacks(ymir::vdp::VDP &vdp) {
#if YMIR_PLATFORM_HAS_DIRECT3D
    vdp.SetDirect3D12FrameCopyRequestCallback(
        {this, [](ID3D12Fence *fence, uint64 fenceValue, void *ctx) -> ID3D12Resource * {
             // TODO: this might need a mutex
             auto &graphicsService = *static_cast<GraphicsService *>(ctx);
             auto *graphicsContext = graphicsService.GetGraphicsContext().As<Direct3D12GraphicsContext>();
             if (graphicsContext == nullptr) {
                 return nullptr;
             }
             return graphicsContext->GetNextDisplayOutputFrame(fence, fenceValue);
         }});
#endif
}

util::VoidResult<> GraphicsService::InitGraphicsContext(const GraphicsContextSpec &spec, PresentMode presentMode) {
    m_gfxContext->Shutdown();
    auto result = CreateGraphicsContext(spec);
    util::VoidResult<> output;
    if (result) {
        m_gfxContext = result.Value();
        output = {};
    } else {
        m_gfxContext->Initialize();
        output = result.Error();
    }
    assert(m_gfxContext->IsInitialized());
    m_gfxContext->SetPresentMode(presentMode);
    RecreateTextures();
    if (m_imguiInitialized) {
        m_gfxContext->ImGuiInit();
    }
    return output;
}

template <typename T>
static util::ObjectResult<IGraphicsContext> ConvertResult(util::ObjectResult<T> &&result) {
    if (!result) {
        return result.Error();
    }
    return std::unique_ptr<IGraphicsContext>{result.Value()};
}

util::ObjectResult<IGraphicsContext> GraphicsService::CreateGraphicsContext(const GraphicsContextSpec &spec) {
    switch (spec.backend) {
    case Backend::Null:
        // Use DestroyGraphicsContext instead
        return util::ErrorMessage{"Cannot initialize the null backend"};
#if YMIR_PLATFORM_HAS_DIRECT3D
    case Backend::Direct3D11: return ConvertResult(Direct3D11GraphicsContext::Create({/*TODO*/}));
    case Backend::Direct3D12:
        return ConvertResult(Direct3D12GraphicsContext::Create({
            .featureLevel = D3D_FEATURE_LEVEL_11_0,
            .window = spec.window,
            .adapter = spec.adapter ? gfx::GetDXGIGraphicsAdapterByID(*spec.adapter) : nullptr,
        }));
#endif
#if YMIR_PLATFORM_HAS_VULKAN
    case Backend::Vulkan:
        return ConvertResult(VulkanGraphicsContext::Create({
            .window = spec.window,
            .device = spec.adapter ? gfx::GetVulkanDeviceByID(*spec.adapter) : nullptr
        }));
#endif
#if YMIR_PLATFORM_HAS_METAL
    case Backend::Metal:
        return ConvertResult(MetalGraphicsContext::Create({
            .featureLevel = MetalGPUFamily::Common1,
            .window = spec.window,
            .device = spec.adapter ? gfx::GetMetalDeviceByID(*spec.adapter) : nullptr,
        }));
#endif
    case Backend::SDLRenderer: return ConvertResult(SDLRendererGraphicsContext::Create({.window = spec.window}));
    }
    return util::ErrorMessage{"Invalid backend"};
}

void GraphicsService::DestroyGraphicsContext() {
    m_gfxContext = std::make_unique<NullGraphicsContext>();
}

void GraphicsService::RevertGraphicsBackend() {
    if (m_settings.video.graphicsBackend != kDefaultBackend) {
        m_settings.video.graphicsBackend = kDefaultBackend;
        m_settings.Save();
    } else if (m_settings.video.graphicsBackend != Backend::SDLRenderer) {
        m_settings.video.graphicsBackend = Backend::SDLRenderer;
        m_settings.Save();
    }
}

util::VoidResult<> GraphicsService::ResizeFramebuffer(uint32 width, uint32 height) {
    return m_gfxContext->ResizeFramebuffer(width, height);
}

void GraphicsService::ClearScreen(ColorRGBA color) {
    m_gfxContext->ClearScreen(color);
}

bool GraphicsService::ImGuiInit() {
    m_imguiInitialized = true;
    return m_gfxContext->ImGuiInit();
}

void GraphicsService::ImGuiShutdown() {
    m_gfxContext->ImGuiShutdown();
    m_imguiInitialized = false;
}

void GraphicsService::ImGuiNewFrame() {
    if (m_imguiInitialized) {
        m_gfxContext->ImGuiNewFrame();
    }
}

void GraphicsService::ImGuiRenderFrame() {
    if (m_imguiInitialized) {
        m_gfxContext->ImGuiRenderFrame();
    }
}

util::ValueResult<GUITextureHandle> GraphicsService::CreateTexture(const Texture2DSpec &spec,
                                                                   FnTextureSetup &&fnSetup) {
    auto createResult = m_gfxContext->CreateTexture(spec);
    if (!createResult) {
        return util::ErrorMessage{fmt::format("Failed to create texture: {}", createResult.Error().message)};
    }

    const GUITextureHandle handle = GetNextTextureHandle();
    Texture2DInstance &texture = m_textures[handle];
    texture.id = createResult.Value();
    texture.spec = spec;
    texture.fnSetup = std::move(fnSetup);
    auto updateResult = m_gfxContext->UpdateTexture(
        texture.id, nullptr, [&](void *data, size_t pitch) { texture.fnSetup(handle, false, data, pitch); });
    if (!updateResult) {
        return util::ErrorMessage{fmt::format("Failed to upload texture: {}", updateResult.Error().message)};
    }
    return handle;
}

bool GraphicsService::IsTextureHandleValid(GUITextureHandle handle) const {
    const Texture2DInstance *texture = GetTexture(handle);
    return texture != nullptr && m_gfxContext->IsTextureValid(texture->id);
}

util::VoidResult<> GraphicsService::ResizeTexture(GUITextureHandle handle, uint32 width, uint32 height) {
    Texture2DInstance *texture = GetTexture(handle);
    if (texture == nullptr) {
        return util::ErrorMessage{"Invalid texture handle"};
    }
    auto result = m_gfxContext->ResizeTexture(texture->id, width, height);
    if (result) {
        texture->spec.width = width;
        texture->spec.height = height;
    }
    return result;
}

util::VoidResult<> GraphicsService::UpdateTexture(GUITextureHandle handle, const IRect *rect,
                                                  const std::function<void(void *data, size_t pitch)> &fnUpdate) {
    const Texture2DInstance *texture = GetTexture(handle);
    if (texture == nullptr) {
        return util::ErrorMessage{"Invalid texture handle"};
    }
    return m_gfxContext->UpdateTexture(texture->id, rect, fnUpdate);
}

util::VoidResult<> GraphicsService::RenderToTexture(GUITextureHandle src, GUITextureHandle dst, const FRect &srcRect,
                                                    const FRect &dstRect) {
    const Texture2DInstance *srcTexture = GetTexture(src);
    if (srcTexture == nullptr) {
        return util::ErrorMessage{"Invalid source texture handle"};
    }
    const Texture2DInstance *dstTexture = GetTexture(dst);
    if (dstTexture == nullptr) {
        return util::ErrorMessage{"Invalid destination texture handle"};
    }
    return m_gfxContext->RenderToTexture(srcTexture->id, dstTexture->id, srcRect, dstRect);
}

util::VoidResult<> GraphicsService::DrawTextureRotated(GUITextureHandle handle, const FRect &srcRect,
                                                       const FRect &dstRect, double rotAngle,
                                                       const FPoint2D *anchorPoint) {
    const Texture2DInstance *texture = GetTexture(handle);
    if (texture == nullptr) {
        return util::ErrorMessage{"Invalid source texture handle"};
    }
    return m_gfxContext->DrawTextureRotated(texture->id, srcRect, dstRect, rotAngle, anchorPoint);
}

ImTextureID GraphicsService::GetImGuiTextureID(GUITextureHandle handle) const {
    const Texture2DInstance *texture = GetTexture(handle);
    if (texture == nullptr) {
        return 0;
    }
    return m_gfxContext->GetImGuiTextureID(texture->id);
}

bool GraphicsService::DestroyTexture(GUITextureHandle handle) {
    const Texture2DInstance *texture = GetTexture(handle);
    if (texture == nullptr) {
        return false;
    }
    m_gfxContext->DestroyTexture(texture->id);
    m_textures.erase(handle);
    m_freeTexHandles.push_back(handle);
    return true;
}

util::VoidResult<> GraphicsService::SetPresentMode(PresentMode mode) {
    return m_gfxContext->SetPresentMode(mode);
}

util::ValueResult<PresentResult> GraphicsService::Present() {
    return m_gfxContext->Present();
}

GUITextureHandle GraphicsService::GetNextTextureHandle() {
    if (!m_freeTexHandles.empty()) {
        const GUITextureHandle handle = m_freeTexHandles.back();
        m_freeTexHandles.pop_back();
        return handle;
    }

    const GUITextureHandle handle = m_nextHandle++;

    // This really should not happen unless we somehow managed to create over 4 billion textures.
    assert(m_nextHandle != kInvalidGUITextureHandle);
    assert(!m_textures.contains(handle));

    return handle;
}

auto GraphicsService::GetTexture(GUITextureHandle handle) -> Texture2DInstance * {
    if (handle == kInvalidGUITextureHandle) {
        return nullptr;
    }
    if (auto it = m_textures.find(handle); it != m_textures.end()) {
        return &it->second;
    }
    return nullptr;
}

auto GraphicsService::GetTexture(GUITextureHandle handle) const -> const Texture2DInstance * {
    return const_cast<GraphicsService *>(this)->GetTexture(handle);
}

util::VoidResult<> GraphicsService::RecreateTextures() {
    for (auto &[handle, texture] : m_textures) {
        auto createResult = m_gfxContext->CreateTexture(texture.spec);
        if (!createResult) {
            return util::ErrorMessage{fmt::format("Failed to create texture: {}", createResult.Error().message)};
        }

        texture.id = createResult.Value();
        auto updateResult = m_gfxContext->UpdateTexture(
            texture.id, nullptr, [&](void *data, size_t pitch) { texture.fnSetup(handle, true, data, pitch); });
        if (!updateResult) {
            return util::ErrorMessage{fmt::format("Failed to upload texture: {}", updateResult.Error().message)};
        }
    }

    return {};
}

} // namespace app::services
