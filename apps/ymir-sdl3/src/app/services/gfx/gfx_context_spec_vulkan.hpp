#pragma once

/**
@file
@brief Defines the contents of `app::gfx::VulkanGraphicsContextSpec`.
*/

#include <SDL3/SDL_video.h>

#include <vulkan/vulkan_core.h>

namespace app::gfx {

struct VulkanGraphicsContextSpec {

    static constexpr uint32 MakeApiVersion(uint8 variant, uint8 major, uint8 minor, uint8 patch) {
        return ((((uint32_t)(variant)) << 29U) | (((uint32_t)(major)) << 22U) | (((uint32_t)(minor)) << 12U) |
                ((uint32_t)(patch)));
    }

    /// @brief (Required) Target feature level.
    uint32 api_level = MakeApiVersion(1, 1, 0, 0);

    /// @brief (Required) Pointer to SDL3 window
    SDL_Window *window = nullptr;

    /// @brief (Optional) Target Vulkan physical device handle(vk::PhysicalDevice/VkPhysicalDevice).
    /// Defaults to the system default first device if nullptr.
    void *device = nullptr;
};

} // namespace app::gfx
