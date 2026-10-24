#pragma once

/**
@file
@brief Defines the contents of `app::gfx::VulkanGraphicsContextSpec`.
*/

#include <SDL3/SDL_video.h>

#include <ymir/gpu/vulkan/vulkan_api.hpp>

namespace app::gfx {

struct VulkanGraphicsContextSpec {

    static constexpr uint32 MakeApiVersion(uint8 variant, uint8 major, uint8 minor, uint8 patch) {
        return ((((uint32_t)(variant)) << 29U) | (((uint32_t)(major)) << 22U) | (((uint32_t)(minor)) << 12U) |
                ((uint32_t)(patch)));
    }

    /// @brief (Required) Target feature level.
    uint32 apiLevel = MakeApiVersion(1, 1, 0, 0);

    /// @brief (Required) Pointer to SDL3 window
    SDL_Window *window = nullptr;

    /// @brief (Optional) Target Vulkan physical device UUID.
    /// Defaults to the first device otherwise.
    std::array<uint8, 16> deviceUUID = {};
};

} // namespace app::gfx
