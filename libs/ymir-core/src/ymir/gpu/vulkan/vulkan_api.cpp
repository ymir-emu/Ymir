#include <ymir/gpu/vulkan/vulkan_api.hpp>

#include <ymir/gpu/vulkan/vulkan_debug.hpp>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE;

// Initialize vulkan's function-pointers as early as possible
static const bool vulkan_loader = []() -> bool {
    static vk::detail::DynamicLoader DynamicLoader;

    VULKAN_HPP_DEFAULT_DISPATCHER.init(
        DynamicLoader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));
    return true;
}();

namespace ymir::gpu::vulkan {

vk::UniqueInstance CreateInstance(std::span<const char *const> instance_extensions) {
    vk::UniqueInstance result;
    // Create a minimal instance to enumerate physical devices
    static const vk::ApplicationInfo application_info = {
        .pApplicationName = "ymir",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "ymir",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_1,
    };

    const vk::InstanceCreateInfo instance_info = {
        .pApplicationInfo = &application_info,
        .enabledExtensionCount = static_cast<uint32_t>(instance_extensions.size()),
        .ppEnabledExtensionNames = instance_extensions.data(),
    };

    if (auto CreateResult = vk::createInstanceUnique(instance_info); CreateResult.result == vk::Result::eSuccess) {
        return std::move(CreateResult.value);
    };
    return {};
}
} // namespace ymir::gpu::vulkan