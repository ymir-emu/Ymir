#include <ymir/gpu/vulkan/vulkan_debug.hpp>

namespace {

VKAPI_ATTR vk::Bool32 VKAPI_CALL DebugMessengerCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
                                                        vk::DebugUtilsMessageTypeFlagsEXT type,
                                                        const vk::DebugUtilsMessengerCallbackDataEXT *callback_data,
                                                        [[maybe_unused]] void *user_data) {
    if (callback_data->pMessage != nullptr) {
        fmt::println("{}({}): {}", vk::to_string(severity), vk::to_string(type), callback_data->pMessage);
    }

    switch (severity) {
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose: break;
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo: break;
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning: break;
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eError: break;
    }

    return false;
}

VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                      VkDebugUtilsMessageTypeFlagsEXT type,
                                                      const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
                                                      void *user_data) {
    return DebugMessengerCallback(
        vk::DebugUtilsMessageSeverityFlagBitsEXT(severity), vk::DebugUtilsMessageTypeFlagsEXT(type),
        reinterpret_cast<const vk::DebugUtilsMessengerCallbackDataEXT *>(callback_data), user_data);
}

} // namespace

namespace ymir::gpu::vulkan {

vk::UniqueDebugUtilsMessengerEXT CreateDebugMessenger(vk::Instance instance) {
    std::vector<vk::ExtensionProperties> instance_extension_properties;

    if (const auto enumerate_result = vk::enumerateInstanceExtensionProperties();
        enumerate_result.result == vk::Result::eSuccess) {
        instance_extension_properties = enumerate_result.value;
    } else {
        // Error enumerating instance extensions
        return {};
    }

    const auto it =
        std::find_if(instance_extension_properties.begin(), instance_extension_properties.end(),
                     [](const vk::ExtensionProperties &properties) {
                         return std::strcmp(VK_EXT_DEBUG_UTILS_EXTENSION_NAME, properties.extensionName) == 0;
                     });

    if (it == instance_extension_properties.end()) {
        // Does not support VK_EXT_DEBUG_UTILS
        return {};
    }
    // VK_EXT_DEBUG_UTILS supported

    // Create debug messenger

    const vk::DebugUtilsMessengerCreateInfoEXT debug_messenger_info = {
        .messageSeverity =
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError |
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose,
        .messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                       vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                       vk::DebugUtilsMessageTypeFlagBitsEXT::eDeviceAddressBinding |
                       vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
        .pfnUserCallback = DebugMessengerCallback,
    };

    if (auto create_result = instance.createDebugUtilsMessengerEXTUnique(debug_messenger_info);
        create_result.result == vk::Result::eSuccess) {
        return std::move(create_result.value);
    }
    // Error creating debug utils messenger
    return {};
}

void SetObjectName(vk::Device device, vk::ObjectType object_type, const void *object_handle,
                   std::string_view object_name) {
    const vk::DebugUtilsObjectNameInfoEXT name_info = {
        .objectType = object_type,
        .objectHandle = reinterpret_cast<std::uintptr_t>(object_handle),
        .pObjectName = object_name.data(),
    };

    if (device.setDebugUtilsObjectNameEXT(name_info) != vk::Result::eSuccess) {
        // Failed to set object name
    }
}

void BeginDebugLabel(vk::CommandBuffer command_buffer, const std::array<float, 4> &color, std::string_view label_name) {
    const vk::DebugUtilsLabelEXT label_info = {
        .pLabelName = label_name.data(),
        .color = color,
    };

    command_buffer.beginDebugUtilsLabelEXT(label_info);
}

void InsertDebugLabel(vk::CommandBuffer command_buffer, const std::array<float, 4> &color,
                      std::string_view label_name) {
    const vk::DebugUtilsLabelEXT label_info = {
        .pLabelName = label_name.data(),
        .color = color,
    };

    command_buffer.insertDebugUtilsLabelEXT(label_info);
}

void EndDebugLabel(vk::CommandBuffer command_buffer) {
    command_buffer.endDebugUtilsLabelEXT();
}

void BeginDebugLabel(vk::Queue queue, const std::array<float, 4> &color, std::string_view label_name) {
    const vk::DebugUtilsLabelEXT label_info = {
        .pLabelName = label_name.data(),
        .color = color,
    };

    queue.beginDebugUtilsLabelEXT(label_info);
}

void InsertDebugLabel(vk::Queue queue, const std::array<float, 4> &color, std::string_view label_name) {
    const vk::DebugUtilsLabelEXT label_info = {
        .pLabelName = label_name.data(),
        .color = color,
    };

    queue.insertDebugUtilsLabelEXT(label_info);
}

void EndDebugLabel(vk::Queue queue) {
    queue.endDebugUtilsLabelEXT();
}

} // namespace ymir::gpu::vulkan