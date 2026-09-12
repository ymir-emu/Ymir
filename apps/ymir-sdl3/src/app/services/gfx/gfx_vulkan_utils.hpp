#pragma once

#include "gfx_types.hpp"

#include <vector>

// -----------------------------------------------------------------------------
// Implementation

namespace app::gfx {

/// @brief Describes a graphics adapter in the system, enumerated with Vulkan.
struct VulkanGraphicsAdapter {
    /// @brief Unique identifier for this adapter.
    AdapterID id;

    /// @brief Device description, typically the GPU's name.
    std::string name;

    /// @brief Opaque handle to the Vulkan physical device(vk::PhysicalDevice/VkPhysicalDevice)
    void *device = nullptr;
};

/// @brief Enumerates graphics adapters in the system using Vulkan.
void EnumerateVulkanGraphicsAdapters();

/// @brief Gets the graphics adapters present in the system enumerated with `EnumerateVulkanGraphicsAdapters()`.
/// @return a list of graphics adapters. The first device in the list is the default adapter.
const std::vector<VulkanGraphicsAdapter> &GetVulkanGraphicsAdapters();

/// @brief Retrieves a Vulkan graphics adapter device pointer by its unique identifier.
/// @param[in] id the adapter's unique identifier
/// @return a pointer to the corresponding graphics adapter device (id<MTLDevice>) if found, `nullptr` otherwise
void *GetVulkanDeviceByID(AdapterID id);

} // namespace app::gfx
