#pragma once

#include <array>

namespace app::config_defaults {

namespace system {
    inline constexpr uint32 kMinSH2ClockFactor = 25u;
    inline constexpr uint32 kMaxSH2ClockFactor = 500u;
    inline constexpr uint32 kDefaultSH2ClockFactor = 100u;
} // namespace system

namespace input {

    namespace arcade_racer {
        inline constexpr float kMinSensitivity = 0.2f;
        inline constexpr float kMaxSensitivity = 2.0f;
        inline constexpr float kDefaultSensitivity = 0.5f;
    } // namespace arcade_racer

    namespace virtua_gun {
        inline constexpr float kMinSpeed = 50.0f;
        inline constexpr float kMaxSpeed = 500.0f;
        inline constexpr float kDefaultSpeed = 200.0f;

        inline constexpr float kMinSpeedBoostFactor = 1.5f;
        inline constexpr float kMaxSpeedBoostFactor = 4.0f;
        inline constexpr float kDefaultSpeedBoostFactor = 2.0f;

        namespace crosshair {
            inline constexpr std::array<std::array<float, 4>, 2> kDefaultColor = {
                {{1.0f, 0.5f, 0.0f, 0.88f}, {0.0f, 0.5f, 1.0f, 0.88f}}};

            inline constexpr float kMinRadius = 2.0f;
            inline constexpr float kMaxRadius = 50.0f;
            inline constexpr std::array<float, 2> kDefaultRadius = {15.0f, 15.0f};

            inline constexpr float kMinThickness = 0.00f;
            inline constexpr float kMaxThickness = 0.75f;
            inline constexpr std::array<float, 2> kDefaultThickness = {0.33f, 0.33f};

            inline constexpr std::array<float, 2> kDefaultRotation = {0.0f, 45.0f};

            inline constexpr std::array<std::array<float, 4>, 2> kDefaultStrokeColor = {
                {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}}};

            inline constexpr float kMinStrokeThickness = 0.0f;
            inline constexpr float kMaxStrokeThickness = 0.5f;
            inline constexpr std::array<float, 2> kDefaultStrokeThickness = {0.25f, 0.25f};
        } // namespace crosshair

    } // namespace virtua_gun

    namespace shuttle_mouse {
        inline constexpr float kMinSpeed = 5.0f;
        inline constexpr float kMaxSpeed = 150.0f;
        inline constexpr float kDefaultSpeed = 30.0f;

        inline constexpr float kMinSpeedBoostFactor = 1.5f;
        inline constexpr float kMaxSpeedBoostFactor = 4.0f;
        inline constexpr float kDefaultSpeedBoostFactor = 2.0f;

        inline constexpr float kMinSensitivity = 0.5f;
        inline constexpr float kMaxSensitivity = 10.0f;
        inline constexpr float kDefaultSensitivity = 4.0f;
    } // namespace shuttle_mouse

} // namespace input

namespace video {
    inline constexpr uint32 kDefaultResolutionScalingFactor = 2u;
    // NOTE: minimum and maximum are defined in vdp2_defs.hpp
} // namespace video

} // namespace app::config_defaults
