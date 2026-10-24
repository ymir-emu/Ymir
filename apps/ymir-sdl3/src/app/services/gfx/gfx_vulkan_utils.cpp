#include "gfx_vulkan_utils.hpp"

#include <ymir/gpu/vulkan/vulkan_api.hpp>

namespace app::gfx {

static std::vector<VulkanGraphicsAdapter> g_VulkanAdapters;
static bool s_adaptersEnumerated = false;

void EnumerateVulkanGraphicsAdapters() {
    // Don't use VULKAN_HPP_DEFAULT_DISPATCHER.init here, use a local one to avoid overwriting global function pointers.
    vk::detail::DispatchLoaderDynamic dispatcher(VULKAN_HPP_DEFAULT_DISPATCHER);

    const vk::UniqueInstance instance = ymir::gpu::vulkan::CreateInstance({}, dispatcher);

    if (!instance) {
        return;
    }

    // Initialize instance function pointers
    dispatcher.init(instance.get());

    g_VulkanAdapters.clear();

    if (const auto enumerate_result = instance->enumeratePhysicalDevices(dispatcher); enumerate_result.has_value()) {
        for (uint8 device_index = 0; const vk::PhysicalDevice &physical_device : enumerate_result.value) {
            const auto physical_device_propertychain =
                physical_device.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceIDProperties>(
                    dispatcher);

            const vk::PhysicalDeviceProperties &physical_device_properties =
                physical_device_propertychain.get<vk::PhysicalDeviceProperties2>().properties;
            const vk::PhysicalDeviceIDProperties &physical_device_id_properties =
                physical_device_propertychain.get<vk::PhysicalDeviceIDProperties>();

            // TODO: Use VK_EXT_pci_bus_info to get the device's exact pcie bus info

            g_VulkanAdapters.emplace_back(VulkanGraphicsAdapter{
                .id =
                    AdapterID{
                        .bus = device_index,
                        .device = 0,
                        .function = 0,
                    },
                .name = physical_device_properties.deviceName,
                .UUID = physical_device_id_properties.deviceUUID,
            });

            ++device_index;
        }
    }
}

const std::vector<VulkanGraphicsAdapter> &GetVulkanGraphicsAdapters() {
    if (!s_adaptersEnumerated) {
        EnumerateVulkanGraphicsAdapters();
        s_adaptersEnumerated = true;
    }
    return g_VulkanAdapters;
}

std::array<uint8, 16> GetVulkanDeviceUUIDByID(AdapterID id) {
    const auto &adapters = GetVulkanGraphicsAdapters();
    for (const auto &adapter : adapters) {
        if (adapter.id == id) {
            return adapter.UUID;
        }
    }
    return std::array<uint8, 16>{};
}

} // namespace app::gfx