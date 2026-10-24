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

    /// @brief Device UUID
    std::array<uint8, 16> UUID;
};

/// @brief Enumerates graphics adapters in the system using Vulkan.
void EnumerateVulkanGraphicsAdapters();

/// @brief Gets the graphics adapters present in the system enumerated with `EnumerateVulkanGraphicsAdapters()`.
/// @return a list of graphics adapters. The first device in the list is the default adapter.
const std::vector<VulkanGraphicsAdapter> &GetVulkanGraphicsAdapters();

/// @brief Retrieves a Vulkan physical device UUID by its adapter id .
/// @param[in] id the adapter's unique identifier
/// @return the corresponding graphics adapter device UUID if found, `std::array<uint8, 16>{}` otherwise
std::array<uint8, 16> GetVulkanDeviceUUIDByID(AdapterID id);

} // namespace app::gfx
