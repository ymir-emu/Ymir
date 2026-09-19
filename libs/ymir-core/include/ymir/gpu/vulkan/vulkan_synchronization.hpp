#pragma once

/**
@file
@brief Defines vulkan synchronization helpers.
*/

#include <ymir/gpu/vulkan/vulkan_api.hpp>

#include <ymir/util/result.hpp>

#include <chrono>
#include <optional>
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

util::ValueResult<uint64> UpdateTimelineSemaphoreValue(const vk::Device logicalDevice,
                                                       const vk::Semaphore timelineSemaphore,
                                                       std::atomic_uint64_t &hostTickValue);

util::ValueResult<std::chrono::nanoseconds>
WaitUntilSemaphoreValue(const vk::Device logicalDevice, const vk::Semaphore timelineSemaphore,
                        const uint64 timelineValue,
                        const std::chrono::nanoseconds timeOut = std::chrono::nanoseconds(~0ULL));

} // namespace ymir::gpu::vulkan