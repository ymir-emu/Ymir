#include <ymir/gpu/vulkan/vulkan_debug.hpp>

#include <ymir/util/dev_assert.hpp>

namespace {

VKAPI_ATTR vk::Bool32 VKAPI_CALL DebugMessengerCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
                                                        vk::DebugUtilsMessageTypeFlagsEXT type,
                                                        const vk::DebugUtilsMessengerCallbackDataEXT *callbackData,
                                                        [[maybe_unused]] void *userData) {
    if (callbackData->pMessage != nullptr) {
        fmt::println("{}({}): {}", vk::to_string(severity), vk::to_string(type), callbackData->pMessage);
    }

    switch (severity) {
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose: break;
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo: break;
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning: break;
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eError: YMIR_DEV_CHECK(); break;
    }

    return vk::False;
}

VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                      VkDebugUtilsMessageTypeFlagsEXT type,
                                                      const VkDebugUtilsMessengerCallbackDataEXT *callbackData,
                                                      void *userData) {
    return DebugMessengerCallback(
        vk::DebugUtilsMessageSeverityFlagBitsEXT(severity), vk::DebugUtilsMessageTypeFlagsEXT(type),
        reinterpret_cast<const vk::DebugUtilsMessengerCallbackDataEXT *>(callbackData), userData);
}

} // namespace

namespace ymir::gpu::vulkan {

vk::UniqueDebugUtilsMessengerEXT CreateDebugMessenger(vk::Instance instance) {
    std::vector<vk::ExtensionProperties> instanceExtensionProperties;

    if (const auto enumerateResult = vk::enumerateInstanceExtensionProperties();
        enumerateResult.result == vk::Result::eSuccess) {
        instanceExtensionProperties = enumerateResult.value;
    } else {
        // Error enumerating instance extensions
        return {};
    }

    const auto it =
        std::find_if(instanceExtensionProperties.begin(), instanceExtensionProperties.end(),
                     [](const vk::ExtensionProperties &properties) {
                         return std::strcmp(VK_EXT_DEBUG_UTILS_EXTENSION_NAME, properties.extensionName) == 0;
                     });

    if (it == instanceExtensionProperties.end()) {
        // Does not support VK_EXT_DEBUG_UTILS
        return {};
    }
    // VK_EXT_DEBUG_UTILS supported

    // Create debug messenger

    const vk::DebugUtilsMessengerCreateInfoEXT debugMessengerInfo = {
        .messageSeverity =
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError |
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose,
        .messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                       vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                       vk::DebugUtilsMessageTypeFlagBitsEXT::eDeviceAddressBinding |
                       vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
        .pfnUserCallback = DebugMessengerCallback,
    };

    if (auto createResult = instance.createDebugUtilsMessengerEXTUnique(debugMessengerInfo);
        createResult.result == vk::Result::eSuccess) {
        return std::move(createResult.value);
    }
    // Error creating debug utils messenger
    return {};
}

void SetObjectName(vk::Device device, vk::ObjectType objectType, const void *objectHandle,
                   std::string_view objectName) {
    const vk::DebugUtilsObjectNameInfoEXT nameInfo = {
        .objectType = objectType,
        .objectHandle = reinterpret_cast<std::uintptr_t>(objectHandle),
        .pObjectName = objectName.data(),
    };

    if (device.setDebugUtilsObjectNameEXT(nameInfo) != vk::Result::eSuccess) {
        // Failed to set object name
    }
}

void BeginDebugLabel(vk::CommandBuffer command_buffer, const std::array<float, 4> &color, std::string_view labelName) {
    const vk::DebugUtilsLabelEXT labelInfo = {
        .pLabelName = labelName.data(),
        .color = color,
    };

    command_buffer.beginDebugUtilsLabelEXT(labelInfo);
}

void InsertDebugLabel(vk::CommandBuffer command_buffer, const std::array<float, 4> &color, std::string_view labelName) {
    const vk::DebugUtilsLabelEXT labelInfo = {
        .pLabelName = labelName.data(),
        .color = color,
    };

    command_buffer.insertDebugUtilsLabelEXT(labelInfo);
}

void EndDebugLabel(vk::CommandBuffer command_buffer) {
    command_buffer.endDebugUtilsLabelEXT();
}

void BeginDebugLabel(vk::Queue queue, const std::array<float, 4> &color, std::string_view labelName) {
    const vk::DebugUtilsLabelEXT labelInfo = {
        .pLabelName = labelName.data(),
        .color = color,
    };

    queue.beginDebugUtilsLabelEXT(labelInfo);
}

void InsertDebugLabel(vk::Queue queue, const std::array<float, 4> &color, std::string_view labelName) {
    const vk::DebugUtilsLabelEXT labelInfo = {
        .pLabelName = labelName.data(),
        .color = color,
    };

    queue.insertDebugUtilsLabelEXT(labelInfo);
}

void EndDebugLabel(vk::Queue queue) {
    queue.endDebugUtilsLabelEXT();
}

} // namespace ymir::gpu::vulkan