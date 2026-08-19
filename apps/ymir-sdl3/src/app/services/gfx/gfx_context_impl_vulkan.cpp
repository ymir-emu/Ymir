#include "gfx_context_impl_vulkan.hpp"
#include "gfx_context_spec_vulkan.hpp"

#include <ymir/gpu/vulkan/vulkan_api.hpp>
#include <ymir/gpu/vulkan/vulkan_debug.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>

namespace app::gfx {

// -----------------------------------------------------------------------------

struct VulkanGraphicsContext::Impl {
    explicit Impl(const VulkanGraphicsContextSpec &spec)
        : spec(spec) {}

    static constexpr uint8 kFrameCount = 3;
    static constexpr uint32 kDescriptorCount = 256;

    VulkanGraphicsContextSpec spec;

    vk::UniqueInstance instance;
    vk::UniqueDebugUtilsMessengerEXT debug_messenger;
    vk::UniqueDevice device;
    vk::PhysicalDevice physical_device;

    vk::SurfaceKHR surface;
    vk::Queue queue;

    vk::UniqueDescriptorPool descriptor_pool;

    util::VoidResult<> Init() {
        if (spec.window == nullptr) {
            return util::ErrorMessage{"No window provided to Vulkan specification"};
        }

        // Create instance
        uint32_t instance_extension_count;
        const char *const *instance_extensions = SDL_Vulkan_GetInstanceExtensions(&instance_extension_count);

        instance = ymir::gpu::vulkan::CreateInstance(
            std::span<const char *const>(instance_extensions, instance_extension_count));

        // Register debug messenger
        debug_messenger = ymir::gpu::vulkan::CreateDebugMessenger(instance.get());

        // Determine physical device

        if (spec.device) {
            physical_device = (VkPhysicalDevice)spec.device;
        } else {
            if (const auto enumerate_result = instance->enumeratePhysicalDevices(); enumerate_result.has_value()) {
                // No device specified, select the first device
                physical_device = enumerate_result.value[0];
            }
        }

        SDL_Vulkan_CreateSurface(spec.window, instance.get(), nullptr, (VkSurfaceKHR *)&surface);

        // Create Device
        vk::DeviceCreateInfo device_info = {};

        static const char *device_extensions[] = {
#if defined(__APPLE__)
            "VK_KHR_portability_subset",
#endif
            VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
        };
        device_info.ppEnabledExtensionNames = device_extensions;
        device_info.enabledExtensionCount = std::size(device_extensions);

        vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceTimelineSemaphoreFeatures>
            device_feature_chain = {};

        auto &device_features = device_feature_chain.get<vk::PhysicalDeviceFeatures2>().features;

        // Enable timeline semaphores
        auto &device_timeline_features = device_feature_chain.get<vk::PhysicalDeviceTimelineSemaphoreFeatures>();
        device_timeline_features.timelineSemaphore = VK_TRUE;

        device_info.pNext = &device_feature_chain.get();

        static const float queue_priority = 1.0f;

        static const vk::DeviceQueueCreateInfo queue_info = {
            .queueFamilyIndex = 0,
            .queueCount = 1,
            .pQueuePriorities = &queue_priority,
        };

        device_info.queueCreateInfoCount = 1;
        device_info.pQueueCreateInfos = &queue_info;

        if (auto create_result = physical_device.createDeviceUnique(device_info);
            create_result.result == vk::Result::eSuccess) {
            device = std::move(create_result.value);
        } else {
            return util::ErrorMessage{"Error creating logical device:" + vk::to_string(create_result.result)};
        }

        queue = device->getQueue(0, 0);

        // Descriptor Pool
        // ImGui wants VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT enabled and requires some
        // VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER descriptors
        std::vector<vk::DescriptorPoolSize> PoolSizes;
        PoolSizes.emplace_back(vk::DescriptorPoolSize{
            .type = vk::DescriptorType::eCombinedImageSampler,
            .descriptorCount = kDescriptorCount,
        });

        const vk::DescriptorPoolCreateInfo descriptor_pool_info{
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets = kDescriptorCount,
            .poolSizeCount = static_cast<std::uint32_t>(PoolSizes.size()),
            .pPoolSizes = PoolSizes.data(),
        };

        if (auto create_result = device->createDescriptorPoolUnique(descriptor_pool_info);
            create_result.result == vk::Result::eSuccess) {
            descriptor_pool = std::move(create_result.value);
        } else {
            return util::ErrorMessage{"Error creating descriptor pool:" + vk::to_string(create_result.result)};
        }

        return {};
    }

    bool IsInitialized() const {
        return device.get() != nullptr;
    }

    util::VoidResult<> ResizeFramebuffer(uint32 width, uint32 height) {}

    util::VoidResult<> BeginFrame() {}
};

// -----------------------------------------------------------------------------

VulkanGraphicsContext::VulkanGraphicsContext(const VulkanGraphicsContextSpec &spec)
    : IGraphicsContext(kBackend)
    , m_impl(std::make_unique<Impl>(spec)) {}

VulkanGraphicsContext::~VulkanGraphicsContext() {}

util::ObjectResult<VulkanGraphicsContext> VulkanGraphicsContext::Create(const VulkanGraphicsContextSpec &spec) {
    auto context = std::make_unique<VulkanGraphicsContext>(spec);
    auto result = context->Initialize();
    if (!result) {
        return result.Error();
    }
    return std::move(context);
}

util::VoidResult<> VulkanGraphicsContext::Initialize() {
    return m_impl->Init();
}

void VulkanGraphicsContext::Shutdown() {}

bool VulkanGraphicsContext::IsInitialized() const {
    return m_impl->IsInitialized();
}

util::VoidResult<> VulkanGraphicsContext::ResizeFramebuffer(uint32 width, uint32 height) {
    // TODO: destroy and recreate swap chain resources
    return {};
    return util::ErrorMessage{"Unimplemented"};
}

void VulkanGraphicsContext::ClearScreen(gfx::ColorRGBA color) {
    // TODO: enqueue command to clear screen
}

bool VulkanGraphicsContext::ImGuiInit() {

    const ImGui_ImplVulkan_PipelineInfo pipeline_info_main{
        .RenderPass = nullptr,
        .Subpass = 0,
    };

    ImGui_ImplVulkan_InitInfo init_info = {
        .ApiVersion = m_impl->spec.api_level,
        .Instance = m_impl->instance.get(),
        .PhysicalDevice = m_impl->physical_device,
        .Device = m_impl->device.get(),
        .QueueFamily = 0,
        .Queue = m_impl->queue,
        .DescriptorPool = m_impl->descriptor_pool.get(),
        .DescriptorPoolSize = 0,
        .MinImageCount = VulkanGraphicsContext::Impl::kFrameCount,
        .PipelineCache = nullptr,
        .PipelineInfoMain = pipeline_info_main,
    };

    m_imguiInitialized =                                     //
        ImGui_ImplSDL3_InitForVulkan(m_impl->spec.window) && //
        ImGui_ImplVulkan_Init(&init_info);

    return m_imguiInitialized;
}

void VulkanGraphicsContext::ImGuiShutdown() {
    if (m_imguiInitialized) {
        ImGui_ImplSDL3_Shutdown();
        ImGui_ImplVulkan_Shutdown();
        m_imguiInitialized = false;
    }
}

void VulkanGraphicsContext::ImGuiNewFrame() {
    if (m_imguiInitialized) {
        m_impl->BeginFrame();
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
    }
}

void VulkanGraphicsContext::ImGuiRenderFrame() {
    if (m_imguiInitialized) {
        ImDrawData *drawData = ImGui::GetDrawData();
        ImGui_ImplVulkan_RenderDrawData(drawData, (VkCommandBuffer) nullptr, nullptr);
    }
}

util::ValueResult<TextureID> VulkanGraphicsContext::CreateTexture(const Texture2DSpec &spec) {
    // TODO: create and store texture object in a hash map
    // The texture ID will be the hash map key, not the native object pointer, because resizing the texture requires
    // creating a new object and these IDs must be immutable for the lifetime of the logical texture.
    return {0};
    return util::ErrorMessage{"Unimplemented"};
}

void VulkanGraphicsContext::DestroyTexture(TextureID id) {
    // TODO: delete texture
}

bool VulkanGraphicsContext::IsTextureValid(TextureID id) const {
    // TODO: check if the texture is still live
    return true;
}

ImTextureID VulkanGraphicsContext::GetImGuiTextureID(TextureID id) const {
    // TODO: get and return texture ID
    return 0;
}

util::VoidResult<> VulkanGraphicsContext::ResizeTexture(TextureID id, uint32 width, uint32 height) {
    // TODO: destroy and recreate texture with new dimensions
    return {};
    return util::ErrorMessage{"Unimplemented"};
}

util::VoidResult<> VulkanGraphicsContext::UpdateTexture(TextureID id, const IRect *rect,
                                                        const std::function<void(void *data, size_t pitch)> &fnUpdate) {
    // TODO: map texture, invoke fnUpdate with contents, unmap texture; handle errors
    return {};
    return util::ErrorMessage{"Unimplemented"};
}

util::VoidResult<> VulkanGraphicsContext::RenderToTexture(TextureID src, TextureID dst, const FRect &srcRect,
                                                          const FRect &dstRect) {
    // TODO: set render target to dst texture, draw texture, restore render target
    return {};
    return util::ErrorMessage{"Unimplemented"};
}

util::VoidResult<> VulkanGraphicsContext::DrawTextureRotated(TextureID id, const FRect &srcRect, const FRect &dstRect,
                                                             double rotAngle, const FPoint2D *anchorPoint) {
    // TODO: imitate SDL_RenderTextureRotated:
    // - srcRect specifies the source texture region to copy from (in texels)
    // - dstRect specifies the destination texture region to copy to (in texels)
    // - rotAngle is the clockwise rotation angle (in degrees)
    // - anchorPoint is the rotation anchor point. If null, use the center of the destination rectangle
    return {};
    return util::ErrorMessage{"Unimplemented"};
}

std::optional<DisplayTextureSpec> VulkanGraphicsContext::AcquireCurrentDisplayOutputTexture() {
    // TODO: find and update index of the latest complete display output texture, emit transition to copy destination
    // barrier and return its texture ID, width and height
    return std::nullopt;
}

void VulkanGraphicsContext::ReleaseCurrentDisplayOutputTexture() {
    // TODO: if the display texture was previously acquired, emit a transition to pixel shading barrier and mark as
    // released
}

void VulkanGraphicsContext::ResetDisplayOutputTextures() {
    // TODO: clear display texture state
}

util::ValueResult<size_t> VulkanGraphicsContext::DownloadDisplayOutputTexture(void *buffer, size_t size) {
    // TODO: if using hardware renderer, copy latest display output texture to buffer and return number of bytes copied.
    // Return 0 if using software renderer.
    return util::ErrorMessage{"Unimplemented"};
}

util::VoidResult<> VulkanGraphicsContext::SetPresentMode(PresentMode mode) {
    // TODO: set presentation mode
    return {};
    return util::ErrorMessage{"Unimplemented"};
}

util::ValueResult<PresentResult> VulkanGraphicsContext::Present() {
    // TODO: present next frame and wait for vertical retrace if enabled
    return {PresentResult::Ok};
    return util::ErrorMessage{"Unimplemented"};
}

} // namespace app::gfx
