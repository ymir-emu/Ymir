#pragma once

/**
@file
@brief Defines vulkan memory helpers.
*/

#include <ymir/gpu/vulkan/vulkan_api.hpp>

#include <ymir/util/result.hpp>

#include <optional>
#include <span>

namespace ymir::gpu::vulkan {

// Will try to find a memory type that is suitable for the given requirements.
[[nodiscard]] std::optional<uint32>
FindMemoryTypeIndex(vk::PhysicalDevice PhysicalDevice, uint32 MemoryTypeMask, vk::MemoryPropertyFlags MemoryProperties,
                    vk::MemoryPropertyFlags MemoryExcludeProperties = vk::MemoryPropertyFlagBits::eProtected);

// Given an array of valid Vulkan image-handles or buffer-handles, these
// functions will allocate a single block of device-memory for all of them
// and bind them consecutively.
//
// There may be a case that all the buffers or images cannot be allocated
// to the same device memory due to their required memory-type.
[[nodiscard]] util::ValueResult<vk::UniqueDeviceMemory>
CommitImageHeap(vk::Device Device, vk::PhysicalDevice PhysicalDevice, const std::span<const vk::Image> Images,
                vk::MemoryPropertyFlags MemoryProperties = vk::MemoryPropertyFlagBits::eDeviceLocal,
                vk::MemoryPropertyFlags MemoryExcludeProperties = vk::MemoryPropertyFlagBits::eProtected);

[[nodiscard]] util::ValueResult<vk::UniqueDeviceMemory>
CommitBufferHeap(vk::Device Device, vk::PhysicalDevice PhysicalDevice, const std::span<const vk::Buffer> Buffers,
                 vk::MemoryPropertyFlags MemoryProperties = vk::MemoryPropertyFlagBits::eDeviceLocal,
                 vk::MemoryPropertyFlags MemoryExcludeProperties = vk::MemoryPropertyFlagBits::eProtected);

} // namespace ymir::gpu::vulkan