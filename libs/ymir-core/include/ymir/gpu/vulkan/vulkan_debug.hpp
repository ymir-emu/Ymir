#pragma once

/**
@file
@brief Defines Vulkan debug helpers.
*/

#include <ymir/gpu/vulkan/vulkan_api.hpp>

namespace ymir::gpu::vulkan {

vk::UniqueDebugUtilsMessengerEXT CreateDebugMessenger(vk::Instance instance);

// Command buffer markers
void SetObjectName(vk::Device device, vk::ObjectType objectType, const void *objectHandle, std::string_view objectName);

void BeginDebugLabel(vk::CommandBuffer commandBuffer, const std::array<float, 4> &color, std::string_view labelName);

void InsertDebugLabel(vk::CommandBuffer commandBuffer, const std::array<float, 4> &color, std::string_view labelName);

void EndDebugLabel(vk::CommandBuffer commandBuffer);

// Queue buffer markers
void BeginDebugLabel(vk::Queue queue, const std::array<float, 4> &color, std::string_view labelName);

void InsertDebugLabel(vk::Queue queue, const std::array<float, 4> &color, std::string_view labelName);

void EndDebugLabel(vk::Queue queue);

template <typename T>
concept VulkanHandleType = vk::isVulkanHandleType<T>::value;

// Set Vulkan-Object name (automatically deduce object-type)
template <VulkanHandleType T, typename... ArgsT>
inline void SetObjectName(vk::Device device, const T objectHandle, fmt::format_string<ArgsT...> format,
                          ArgsT &&...args) {
    SetObjectName(device, T::objectType, objectHandle, fmt::format(format, std::forward<ArgsT>(args)...));
}

// Command buffer markers (formatted)
template <typename... ArgsT>
void BeginDebugLabel(vk::CommandBuffer commandBuffer, const std::array<float, 4> &color,
                     fmt::format_string<ArgsT...> format, ArgsT &&...args) {
    BeginDebugLabel(commandBuffer, color, fmt::format(format, std::forward<ArgsT>(args)...));
}

template <typename... ArgsT>
void InsertDebugLabel(vk::CommandBuffer commandBuffer, const std::array<float, 4> &color,
                      fmt::format_string<ArgsT...> format, ArgsT &&...args) {
    InsertDebugLabel(commandBuffer, color, fmt::format(format, std::forward<ArgsT>(args)...));
}

// Command buffer markers (formatted)
template <typename... ArgsT>
void BeginDebugLabel(vk::Queue queue, const std::array<float, 4> &color, fmt::format_string<ArgsT...> format,
                     ArgsT &&...args) {
    BeginDebugLabel(queue, color, fmt::format(format, std::forward<ArgsT>(args)...));
}

template <typename... ArgsT>
void InsertDebugLabel(vk::Queue queue, const std::array<float, 4> &color, fmt::format_string<ArgsT...> format,
                      ArgsT &&...args) {
    InsertDebugLabel(queue, color, fmt::format(format, std::forward<ArgsT>(args)...));
}

// RAII-based utility-object to automatically begin and end label-scopes
// within a command-buffer
class DebugLabelScope {
private:
    const std::variant<vk::CommandBuffer, vk::Queue> target;

public:
    template <typename... ArgsT>
    DebugLabelScope(vk::CommandBuffer targetCommandBuffer, const std::array<float, 4> &color,
                    fmt::format_string<ArgsT...> format, ArgsT &&...args)
        : target(targetCommandBuffer) {
        BeginDebugLabel(targetCommandBuffer, color, fmt::format(format, std::forward<ArgsT>(args)...));
    }

    template <typename... ArgsT>
    DebugLabelScope(vk::Queue target_queue, const std::array<float, 4> &color, fmt::format_string<ArgsT...> format,
                    ArgsT &&...args)
        : target(target_queue) {
        BeginDebugLabel(target_queue, color, fmt::format(format, std::forward<ArgsT>(args)...));
    }

    template <typename... ArgsT>
    void operator()(const std::array<float, 4> &color, fmt::format_string<ArgsT...> format, ArgsT &&...args) const {
        if (target.index() == 0) {
            InsertDebugLabel(std::get<vk::CommandBuffer>(target), color,
                             fmt::format(format, std::forward<ArgsT>(args)...));
        } else if (target.index() == 1) {
            InsertDebugLabel(std::get<vk::Queue>(target), color, fmt::format(format, std::forward<ArgsT>(args)...));
        }
    }

    ~DebugLabelScope() {
        if (target.index() == 0) {
            EndDebugLabel(std::get<vk::CommandBuffer>(target));
        } else if (target.index() == 1) {
            EndDebugLabel(std::get<vk::Queue>(target));
        }
    }
};

} // namespace ymir::gpu::vulkan