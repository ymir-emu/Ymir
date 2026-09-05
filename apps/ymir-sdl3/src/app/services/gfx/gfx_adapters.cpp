#include "gfx_adapters.hpp"

#if YMIR_PLATFORM_HAS_DIRECT3D
    #include "gfx_d3d_utils.hpp"
#endif
#if YMIR_PLATFORM_HAS_VULKAN
    #include "gfx_vulkan_utils.hpp"
#endif
#if YMIR_PLATFORM_HAS_METAL
    #include "gfx_metal_utils.hpp"
#endif

#include <ymir/util/string.hpp>

namespace app::gfx {

std::vector<Adapter> GetGraphicsAdapters(Backend backend) {
    std::vector<Adapter> adapters{};
    switch (backend) {
#if YMIR_PLATFORM_HAS_DIRECT3D
    case Backend::Direct3D11: [[fallthrough]];
    case Backend::Direct3D12:
        for (const DXGIGraphicsAdapter &dxgiAdapter : GetDXGIGraphicsAdapters()) {
            adapters.push_back(Adapter{
                .id = dxgiAdapter.id,
                .name = util::WStringToString(dxgiAdapter.description),
            });
        }
        break;

#endif
#if YMIR_PLATFORM_HAS_VULKAN
    case Backend::Vulkan:
        for (const VulkanGraphicsAdapter &vulkanAdapter : GetVulkanGraphicsAdapters()) {
            adapters.push_back(Adapter{
                .id = vulkanAdapter.id,
                .name = vulkanAdapter.name,
            });
        }
        break;
#endif
#if YMIR_PLATFORM_HAS_METAL
    case Backend::Metal:
        for (const MetalGraphicsAdapter &metalAdapter : GetMetalGraphicsAdapters()) {
            adapters.push_back(Adapter{
                .id = metalAdapter.id,
                .name = metalAdapter.name,
            });
        }
        break;
#endif
    default: break;
    }

    return adapters;
}

void RefreshGraphicsAdapters(Backend backend) {
    switch (backend) {
#if YMIR_PLATFORM_HAS_DIRECT3D
    case Backend::Direct3D11: [[fallthrough]];
    case Backend::Direct3D12: EnumerateDXGIGraphicsAdapters(); break;

#endif
#if YMIR_PLATFORM_HAS_VULKAN
    case Backend::Vulkan: EnumerateVulkanGraphicsAdapters(); break;
#endif
#if YMIR_PLATFORM_HAS_METAL
    case Backend::Metal: EnumerateMetalGraphicsAdapters(); break;
#endif
    default: break;
    }
}

} // namespace app::gfx
