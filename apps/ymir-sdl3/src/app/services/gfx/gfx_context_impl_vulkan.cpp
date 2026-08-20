#include "gfx_context_impl_vulkan.hpp"
#include "gfx_context_spec_vulkan.hpp"

#include <ymir/gpu/vulkan/vulkan_api.hpp>
#include <ymir/gpu/vulkan/vulkan_debug.hpp>
#include <ymir/gpu/vulkan/vulkan_descriptor_heap.hpp>
#include <ymir/gpu/vulkan/vulkan_swap_chain.hpp>
#include <ymir/gpu/vulkan/vulkan_synchronization.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>

using namespace ymir::gpu;
using namespace ymir::gpu::vulkan;

namespace app::gfx {

// -----------------------------------------------------------------------------

struct VulkanGraphicsContext::Impl {
    explicit Impl(const VulkanGraphicsContextSpec &spec)
        : spec(spec) {}

    static constexpr uint8 kFrameCount = 3;
    static constexpr uint32 kImGuiDescriptorCount = 256;

    VulkanGraphicsContextSpec spec;

    vk::UniqueInstance instance;
    vk::UniqueDebugUtilsMessengerEXT debugMessenger;
    vk::UniqueDevice device;
    vk::PhysicalDevice physicalDevice;

    vk::SurfaceKHR surface;
    vk::Queue presentQueue;
    vk::Queue renderQueue;
    vk::Queue transferQueue;

    std::unique_ptr<VulkanDescriptorHeap> descriptorHeapImgui;
    std::unique_ptr<VulkanSwapchain> swapchain;

    PresentMode presentMode = PresentMode::VSync;

    struct TextureInstance {
        Texture2DSpec spec;
        vk::UniqueImage image;
    };

    struct TextureToDelete {
        vk::UniqueImage texture;
    };

    std::unordered_map<TextureID, TextureInstance> textures;
    std::deque<TextureToDelete> texturesToDelete;

    util::VoidResult<> Init() {
        if (spec.window == nullptr) {
            return util::ErrorMessage{"No window provided to Vulkan specification"};
        }

        // Create instance
        uint32_t instanceExtensionCount;
        const char *const *instanceExtensions = SDL_Vulkan_GetInstanceExtensions(&instanceExtensionCount);

        instance =
            ymir::gpu::vulkan::CreateInstance(std::span<const char *const>(instanceExtensions, instanceExtensionCount));

        // Register debug messenger
        debugMessenger = ymir::gpu::vulkan::CreateDebugMessenger(instance.get());

        // Determine physical device

        if (spec.device) {
            physicalDevice = (VkPhysicalDevice)spec.device;
        } else {
            if (const auto enumerateResult = instance->enumeratePhysicalDevices(); enumerateResult.has_value()) {
                // No device specified, select the first device
                physicalDevice = enumerateResult.value[0];
            }
        }

        SDL_Vulkan_CreateSurface(spec.window, instance.get(), nullptr, (VkSurfaceKHR *)&surface);

        // Create Device
        vk::DeviceCreateInfo deviceInfo = {};

        static const char *deviceExtensions[] = {
#if defined(__APPLE__)
            "VK_KHR_portability_subset",
#endif
            VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
        };
        deviceInfo.ppEnabledExtensionNames = deviceExtensions;
        deviceInfo.enabledExtensionCount = std::size(deviceExtensions);

        vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceTimelineSemaphoreFeatures>
            deviceFeatureChain = {};

        auto &deviceFeatures = deviceFeatureChain.get<vk::PhysicalDeviceFeatures2>().features;

        // Enable timeline semaphores
        auto &deviceTimelineFeatures = deviceFeatureChain.get<vk::PhysicalDeviceTimelineSemaphoreFeatures>();
        deviceTimelineFeatures.timelineSemaphore = VK_TRUE;

        deviceInfo.pNext = &deviceFeatureChain.get();

        // Determine which queue families to use
        std::optional<uint32> presentQueueFamilyIndex;
        std::optional<uint32> renderQueueFamilyIndex;
        std::optional<uint32> transferQueueFamilyIndex;
        FindQueueFamilyIndices(physicalDevice, surface, presentQueueFamilyIndex, renderQueueFamilyIndex,
                               transferQueueFamilyIndex);

        // In the case that queues families are re-used, these are indices for the
        // particular queue to be allocated
        uint32 presentQueueIndex = 0u;
        uint32 renderQueueIndex = 0u;
        uint32 transferQueueIndex = 0u;
        const std::vector<vk::DeviceQueueCreateInfo> queueInfo =
            DetermineQueueIndexAllocation(presentQueueFamilyIndex, renderQueueFamilyIndex, transferQueueFamilyIndex,
                                          presentQueueIndex, renderQueueIndex, transferQueueIndex);

        deviceInfo.queueCreateInfoCount = static_cast<uint32>(queueInfo.size());
        deviceInfo.pQueueCreateInfos = queueInfo.data();

        if (auto createResult = physicalDevice.createDeviceUnique(deviceInfo);
            createResult.result == vk::Result::eSuccess) {
            device = std::move(createResult.value);
        } else {
            return util::ErrorMessage{"Error creating logical device:" + vk::to_string(createResult.result)};
        }
        SetObjectName(device.get(), device.get(), "[Ymir-GCtx] Vulkan device");

        // Initialize device function pointers
        VULKAN_HPP_DEFAULT_DISPATCHER.init(device.get());

        // Main queues
        presentQueue = presentQueueFamilyIndex.has_value()
                           ? device->getQueue(presentQueueFamilyIndex.value(), presentQueueIndex)
                           : vk::Queue{};
        renderQueue = renderQueueFamilyIndex.has_value()
                          ? device->getQueue(renderQueueFamilyIndex.value(), renderQueueIndex)
                          : vk::Queue{};
        transferQueue = transferQueueFamilyIndex.has_value()
                            ? device->getQueue(transferQueueFamilyIndex.value(), transferQueueIndex)
                            : vk::Queue{};

        // ImGui Descriptor Heap
        // ImGui wants VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT enabled and requires some
        // VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER descriptors
        const auto ImGuiBindingLayout = std::to_array<vk::DescriptorSetLayoutBinding>({
            vk::DescriptorSetLayoutBinding{
                .binding = 1,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .descriptorCount = 1,
                .stageFlags = vk::ShaderStageFlagBits::eFragment,
            },
        });
        if (auto createResult = VulkanDescriptorHeap::Create(device.get(), ImGuiBindingLayout, kImGuiDescriptorCount);
            createResult.HasValue()) {
            descriptorHeapImgui = std::make_unique<VulkanDescriptorHeap>(std::move(createResult.Value()));
        } else {
            return createResult.Error();
        }
        SetObjectName(device.get(), descriptorHeapImgui->GetDescriptorPool(), "[Ymir-GCtx] ImGui Descriptor Pool");
        SetObjectName(device.get(), descriptorHeapImgui->GetDescriptorSetLayout(),
                      "[Ymir-GCtx] ImGui Descriptor Set Layout");

        // Swapchain
        if (auto createResult = VulkanSwapchain::Create(device.get(), physicalDevice, 0, presentQueue, surface,
                                                        kFrameCount, presentMode == PresentMode::VSync);
            createResult.HasValue()) {
            swapchain = std::make_unique<VulkanSwapchain>(std::move(createResult.Value()));
        } else {
            return createResult.Error();
        }

        return {};
    }

    bool IsInitialized() const {
        return device.get() != nullptr;
    }

    util::VoidResult<> ResizeFramebuffer(uint32 width, uint32 height) {
        swapchain->RecreateSwapchain(vk::Extent2D{
            .width = width,
            .height = height,
        });
    }

    util::VoidResult<> BeginFrame() {
        return {};
    }

    util::VoidResult<> EndFrame() {
        return {};
    }

    util::ValueResult<PresentResult> Present() {
        if (auto result = EndFrame(); !result) {
            return util::ErrorMessage{fmt::format("Could not end frame: {}", result.Error().message)};
        }

        if (swapchain->Present()) {
            return PresentResult::Ok;
        }

        return util::ErrorMessage{"Error presenting frame"};
    }

    util::ValueResult<TextureInstance> CreateTexture(const Texture2DSpec &spec) {
        const bool isRenderTarget = spec.access == TextureAccess::RenderTarget;

        return TextureInstance{};
    }

    util::VoidResult<> ResizeTexture(TextureID id, uint32 width, uint32 height) {
        auto it = textures.find(id);
        if (it == textures.end()) {
            return util::ErrorMessage{"Texture does not exist"};
        }
        TextureInstance &texture = it->second;

        // First, try creating new texture using the existing texture's specifications
        Texture2DSpec newSpec = texture.spec;
        newSpec.width = width;
        newSpec.height = height;
        auto createResult = CreateTexture(newSpec);
        if (!createResult) {
            return createResult.Error();
        }

        // Now that we've succeeded, mark the previous texture for deletion and replace it
        SubmitTextureForDeletion(texture);
        texture = createResult.Value();

        return {};
    }

    void DestroyTexture(TextureID id) {}

    void SubmitTextureForDeletion(TextureInstance &texture) {}

    void DeletePendingTextures(bool force) {}

    bool IsTextureValid(TextureID id) {}

    TextureInstance *GetTexture(TextureID id) {}

    util::VoidResult<> UpdateTexture(TextureID id, const IRect *rect,
                                     const std::function<void(void *data, size_t pitch)> &fnUpdate) {
        return {};
    }

    util::VoidResult<> RenderToTexture(TextureID src, TextureID dst, const FRect &srcRect, const FRect &dstRect) {}

    util::VoidResult<> DrawTextureRotated(TextureID id, const FRect &srcRect, const FRect &dstRect, double rotAngle,
                                          const FPoint2D *rotPivot) {}
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

    const ImGui_ImplVulkan_PipelineInfo pipelineInfoMain{
        .RenderPass = nullptr,
        .Subpass = 0,
    };

    ImGui_ImplVulkan_InitInfo initInfo = {
        .ApiVersion = m_impl->spec.apiLevel,
        .Instance = m_impl->instance.get(),
        .PhysicalDevice = m_impl->physicalDevice,
        .Device = m_impl->device.get(),
        .QueueFamily = 0,
        .Queue = m_impl->renderQueue,
        .DescriptorPool = m_impl->descriptorHeapImgui->GetDescriptorPool(),
        .DescriptorPoolSize = 0,
        .MinImageCount = VulkanGraphicsContext::Impl::kFrameCount,
        .PipelineCache = nullptr,
        .PipelineInfoMain = pipelineInfoMain,
    };

    m_imguiInitialized =                                     //
        ImGui_ImplSDL3_InitForVulkan(m_impl->spec.window) && //
        ImGui_ImplVulkan_Init(&initInfo);

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
    auto result = m_impl->CreateTexture(spec);
    if (!result) {
        return result.Error();
    }

    const TextureID id = GetNextTextureID();
    m_impl->textures[id] = std::move(result.Value());

    return id;
}

void VulkanGraphicsContext::DestroyTexture(TextureID id) {
    m_impl->DestroyTexture(id);
}

bool VulkanGraphicsContext::IsTextureValid(TextureID id) const {
    return m_impl->IsTextureValid(id);
}

ImTextureID VulkanGraphicsContext::GetImGuiTextureID(TextureID id) const {
    // ImTextureIDs for Vulkan are the VkImage handles
    Impl::TextureInstance *instance = m_impl->GetTexture(id);
    return (instance != nullptr) ? reinterpret_cast<ImTextureID>((VkImage)instance->image.get()) : 0;
}

util::VoidResult<> VulkanGraphicsContext::ResizeTexture(TextureID id, uint32 width, uint32 height) {
    return m_impl->ResizeTexture(id, width, height);
}

util::VoidResult<> VulkanGraphicsContext::UpdateTexture(TextureID id, const IRect *rect,
                                                        const std::function<void(void *data, size_t pitch)> &fnUpdate) {
    return m_impl->UpdateTexture(id, rect, fnUpdate);
}

util::VoidResult<> VulkanGraphicsContext::RenderToTexture(TextureID src, TextureID dst, const FRect &srcRect,
                                                          const FRect &dstRect) {
    return m_impl->RenderToTexture(src, dst, srcRect, dstRect);
}

util::VoidResult<> VulkanGraphicsContext::DrawTextureRotated(TextureID id, const FRect &srcRect, const FRect &dstRect,
                                                             double rotAngle, const FPoint2D *anchorPoint) {
    return m_impl->DrawTextureRotated(id, srcRect, dstRect, rotAngle, anchorPoint);
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
    m_impl->presentMode = mode;
    return {};
}

util::ValueResult<PresentResult> VulkanGraphicsContext::Present() {
    return m_impl->Present();
}

} // namespace app::gfx
