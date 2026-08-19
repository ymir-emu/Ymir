#pragma once

/**
@file
@brief Includes the Vulkan API with project-wide preprocessor configurations
*/

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#define VULKAN_HPP_NO_EXCEPTIONS
// Used to allow aggregate initialization for structs
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_format_traits.hpp>
#include <vulkan/vulkan_hash.hpp>

#include <span>

namespace ymir::gpu::vulkan {

vk::UniqueInstance CreateInstance(std::span<const char *const> instance_extensions);

}