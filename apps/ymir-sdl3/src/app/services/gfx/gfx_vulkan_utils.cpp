#include "gfx_vulkan_utils.hpp"

#define VULKAN_HPP_NO_EXCEPTIONS
// Used to allow aggregate initialization for structs
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS

#include <vulkan/vulkan.hpp>

namespace app::gfx {

static std::vector<VulkanGraphicsAdapter> g_VulkanAdapters;

void EnumerateVulkanGraphicsAdapters() {

    // Create a minimal instance to enumerate physical devices
    static const vk::ApplicationInfo application_info = {
        .pApplicationName = "ymir",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "ymir",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_1,
    };

    static const std::array instance_extensions = std::to_array({
#if defined(__APPLE__)
        VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
#endif
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME});

    const vk::InstanceCreateInfo instance_info = {
#if defined(__APPLE__)
        .flags = vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR,
#endif
        .pApplicationInfo = &application_info,
        .enabledExtensionCount = instance_extensions.size(),
        .ppEnabledExtensionNames = instance_extensions.data(),
    };

    vk::UniqueInstance instance = {};

    if (auto CreateResult = vk::createInstanceUnique(instance_info); CreateResult.result == vk::Result::eSuccess) {
        instance = std::move(CreateResult.value);
    } else {
        return;
    }

    if (const auto enumerate_result = instance->enumeratePhysicalDevices(); enumerate_result.has_value()) {
        for (uint8 device_index = 0; const vk::PhysicalDevice &physical_device : enumerate_result.value) {
            const vk::PhysicalDeviceProperties physical_device_properties = physical_device.getProperties();

            // TODO: Use VK_EXT_pci_bus_info to get the device's exact pcie bus info

            g_VulkanAdapters.emplace_back(VulkanGraphicsAdapter{
                .id =
                    AdapterID{
                        .bus = device_index,
                        .device = 0,
                        .function = 0,
                    },
                .name = physical_device_properties.deviceName,
                .device = physical_device,
            });

            ++device_index;
        }
    }

    g_VulkanAdapters.clear();
}

const std::vector<VulkanGraphicsAdapter> &GetVulkanGraphicsAdapters() {
    return g_VulkanAdapters;
}

void *GetVulkanDeviceByID(AdapterID id) {
    const auto &adapters = GetVulkanGraphicsAdapters();
    for (const auto &adapter : adapters) {
        if (adapter.id == id) {
            return adapter.device;
        }
    }
    return nullptr;
}

} // namespace app::gfx