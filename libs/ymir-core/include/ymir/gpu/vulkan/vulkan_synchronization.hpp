#pragma once

/**
@file
@brief Defines vulkan synchronization helpers.
*/

#include <ymir/gpu/vulkan/vulkan_api.hpp>

#include <optional>
#include <unordered_map>
#include <vector>

namespace ymir::gpu::vulkan {

void FindQueueFamilyIndices(vk::PhysicalDevice physicalDevice, vk::SurfaceKHR surface,
                            std::optional<uint32> &presentQueueFamilyIndex,
                            std::optional<uint32> &renderQueueFamilyIndex,
                            std::optional<uint32> &transferQueueFamilyIndex);

std::vector<vk::DeviceQueueCreateInfo>
DetermineQueueIndexAllocation(const std::optional<uint32> &presentQueueFamilyIndex,
                              const std::optional<uint32> &renderQueueFamilyIndex,
                              const std::optional<uint32> &transferQueueFamilyIndex, uint32 &presentQueueIndex,
                              uint32 &renderQueueIndex, uint32 &transferQueueIndex);

} // namespace ymir::gpu::vulkan