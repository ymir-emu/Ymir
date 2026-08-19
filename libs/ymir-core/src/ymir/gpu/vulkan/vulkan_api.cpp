#include <ymir/gpu/vulkan/vulkan_api.hpp>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE;

// Initialize vulkan's function-pointers as early as possible
static const bool vulkan_loader = []() -> bool {
    static vk::detail::DynamicLoader DynamicLoader;

    VULKAN_HPP_DEFAULT_DISPATCHER.init(
        DynamicLoader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));
    return true;
}();

namespace ymir::gpu::vulkan {}