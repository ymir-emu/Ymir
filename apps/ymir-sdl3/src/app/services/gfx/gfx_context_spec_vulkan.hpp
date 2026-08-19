#pragma once

/**
@file
@brief Defines the contents of `app::gfx::VulkanGraphicsContextSpec`.
*/

#include <SDL3/SDL_video.h>

#include <vulkan/vulkan.hpp>

namespace app::gfx {

struct VulkanGraphicsContextSpec {
    /// @brief (Required) Target feature level.
    uint32 api_level = vk::ApiVersion11;

    /// @brief (Required) Pointer to SDL3 window
    SDL_Window *window = nullptr;

    /// @brief (Optional) Target Vulkan physical device handle(vk::PhysicalDevice/VkPhysicalDevice).
    /// Defaults to the system default first device if nullptr.
    void *device = nullptr;
};

} // namespace app::gfx
