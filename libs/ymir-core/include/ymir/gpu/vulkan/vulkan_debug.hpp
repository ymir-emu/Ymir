#include <ymir/gpu/vulkan/vulkan_api.hpp>

/**
@file
@brief Defines Vulkan debug helpers.
*/

namespace ymir::gpu::vulkan {

vk::UniqueDebugUtilsMessengerEXT CreateDebugMessenger(vk::Instance instance);

// Command buffer markers
void SetObjectName(vk::Device device, vk::ObjectType object_type, const void *object_handle,
                   std::string_view object_name);

void BeginDebugLabel(vk::CommandBuffer command_buffer, const std::array<float, 4> &color, std::string_view label_name);

void InsertDebugLabel(vk::CommandBuffer command_buffer, const std::array<float, 4> &color, std::string_view label_name);

void EndDebugLabel(vk::CommandBuffer command_buffer);

// Queue buffer markers
void BeginDebugLabel(vk::Queue queue, const std::array<float, 4> &color, std::string_view label_name);

void InsertDebugLabel(vk::Queue queue, const std::array<float, 4> &color, std::string_view label_name);

void EndDebugLabel(vk::Queue queue);

template <typename T>
concept VulkanHandleType = vk::isVulkanHandleType<T>::value;

// Set Vulkan-Object name (automatically deduce object-type)
template <VulkanHandleType T, typename... ArgsT>
inline void SetObjectName(vk::Device device, const T object_handle, fmt::format_string<ArgsT...> format,
                          ArgsT &&...args) {
    SetObjectName(device, T::objectType, object_handle, fmt::format(format, std::forward<ArgsT>(args)...));
}

// Command buffer markers (formatted)
template <typename... ArgsT>
void BeginDebugLabel(vk::CommandBuffer command_buffer, const std::array<float, 4> &color,
                     fmt::format_string<ArgsT...> format, ArgsT &&...args) {
    BeginDebugLabel(command_buffer, color, fmt::format(format, std::forward<ArgsT>(args)...));
}

template <typename... ArgsT>
void InsertDebugLabel(vk::CommandBuffer command_buffer, const std::array<float, 4> &color,
                      fmt::format_string<ArgsT...> format, ArgsT &&...args) {
    InsertDebugLabel(command_buffer, color, fmt::format(format, std::forward<ArgsT>(args)...));
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
    DebugLabelScope(vk::CommandBuffer target_command_buffer, const std::array<float, 4> &color,
                    fmt::format_string<ArgsT...> format, ArgsT &&...args)
        : target(target_command_buffer) {
        BeginDebugLabel(target_command_buffer, color, fmt::format(format, std::forward<ArgsT>(args)...));
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