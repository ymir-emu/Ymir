#include "gfx_context_impl_vulkan.hpp"
#include "gfx_context_spec_vulkan.hpp"

#include <ymir/gpu/vulkan/vulkan_api.hpp>
#include <ymir/gpu/vulkan/vulkan_debug.hpp>
#include <ymir/gpu/vulkan/vulkan_descriptor_heap.hpp>
#include <ymir/gpu/vulkan/vulkan_descriptor_update_batch.hpp>
#include <ymir/gpu/vulkan/vulkan_memory.hpp>
#include <ymir/gpu/vulkan/vulkan_swap_chain.hpp>
#include <ymir/gpu/vulkan/vulkan_synchronization.hpp>

#include <ymir/gpu/shaders/gpu_shaders.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>

#include <cmrc/cmrc.hpp>
CMRC_DECLARE(ymir_sdl3_shaders);

using namespace ymir::gpu;
using namespace ymir::gpu::vulkan;

namespace app::gfx {

static vk::Format ToVulkanFormat(PixelFormat format) {
    switch (format) {
    case PixelFormat::Unknown: return vk::Format::eUndefined;
    case PixelFormat::R8G8B8A8_UNORM: return vk::Format::eR8G8B8A8Unorm;
    case PixelFormat::R8G8B8X8_UNORM: return vk::Format::eR8G8B8A8Unorm; // Note: A instead of X
    case PixelFormat::B8G8R8A8_UNORM: return vk::Format::eB8G8R8A8Unorm;
    case PixelFormat::B8G8R8X8_UNORM: return vk::Format::eB8G8R8A8Unorm; // Note: A instead of X
    }
    return vk::Format::eUndefined;
}

static const std::array<vk::VertexInputBindingDescription, 2> inputBindingDescs{{
    {.binding = 0, .stride = 0, .inputRate = vk::VertexInputRate::eVertex},  // Position
    {.binding = 1, .stride = 12, .inputRate = vk::VertexInputRate::eVertex}, // TexCoord
}};

static const std::array<vk::VertexInputAttributeDescription, 2> inputAttributeDescs{{
    {.location = 0, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = 0}, // Position
    {.location = 1, .binding = 0, .format = vk::Format::eR32G32Sfloat, .offset = 12},   // TexCoord
}};

// ImGui Descriptor Heap
// ImGui wants VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT enabled and requires some
// VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER descriptors
const auto descriptorLayoutImgui = std::to_array<vk::DescriptorSetLayoutBinding>({
    vk::DescriptorSetLayoutBinding{
        .binding = 0,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment,
    },
});

struct alignas(uint32) Float4 {
    float x, y, z, w;
};

struct alignas(uint32) Float3 {
    float x, y, z;
};

struct alignas(uint32) Float2 {
    float x, y;
};

struct Vertex {
    Float3 position;
    Float2 uv;
};

struct alignas(uint32) DrawTextureConstants {
    Float4 srcRect;
    Float4 dstRect;
    Float2 renderTargetSize;
    Float2 rotPivot;
    float rotAngle;
};

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

    uint32 presentQueueFamilyIndex = 0u;
    uint32 renderQueueFamilyIndex = 0u;
    uint32 transferQueueFamilyIndex = 0u;
    vk::Queue presentQueue;
    vk::Queue renderQueue;
    vk::Queue transferQueue;

    std::unique_ptr<VulkanDescriptorHeap> descriptorHeapImgui;
    std::unique_ptr<VulkanDescriptorUpdateBatch> descriptorUpdateBatch;
    std::unique_ptr<VulkanSwapchain> swapchain;

    struct FrameContext {
        vk::UniqueCommandPool commandPool;
        vk::UniqueCommandBuffer mainCommandBuffer;
        vk::DescriptorSet descriptorSet;

        vk::UniqueFence submitFence;
    };
    std::array<FrameContext, kFrameCount> frames;
    uint8 frameIndex = 0;
    vk::Viewport viewport;
    vk::Rect2D scissorRect;

    VertexShader vertexShader;
    PixelShader pixelShader;

    std::unordered_map<vk::Format, vk::UniquePipeline> renderTargetPipelines;
    std::unordered_map<vk::Format, vk::UniqueRenderPass> renderTargetRenderPasses;

    DrawTextureConstants drawTextureConstants;

    PresentMode presentMode = PresentMode::VSync;

    struct TextureInstance {
        Texture2DSpec spec;
        vk::UniqueImage image;
        vk::UniqueDeviceMemory imageMemory;
    };

    struct TextureToDelete : TextureInstance {
        // Timeline semaphore value used to indicate when it is safe to delete this texture
        uint64 timeStamp;
    };

    std::unordered_map<TextureID, TextureInstance> textures;
    std::deque<TextureToDelete> texturesToDelete;

    util::VoidResult<> Init() {
        if (spec.window == nullptr) {
            return util::ErrorMessage{"No window provided to Vulkan specification"};
        }

        // Create instance
        std::vector<const char *> instanceExtensions;

#ifndef NDEBUG
        instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

        // Get the extensions that SDL wants
        {
            uint32 sdlInstanceExtensionCount;
            const char *const *sdlInstanceExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlInstanceExtensionCount);
            for (uint32 i = 0; i < sdlInstanceExtensionCount; ++i) {
                instanceExtensions.emplace_back(sdlInstanceExtensions[i]);
            }
        }

        instance = ymir::gpu::vulkan::CreateInstance(instanceExtensions);

        // Initialize instance function pointers
        VULKAN_HPP_DEFAULT_DISPATCHER.init(instance.get());

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
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
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
        std::optional<uint32> bestPresentQueueFamilyIndex;
        std::optional<uint32> bestRenderQueueFamilyIndex;
        std::optional<uint32> bestTransferQueueFamilyIndex;
        FindQueueFamilyIndices(physicalDevice, surface, bestPresentQueueFamilyIndex, bestRenderQueueFamilyIndex,
                               bestTransferQueueFamilyIndex);

        // In the case that queues families are re-used, these are indices for the
        // particular queue to be allocated
        const std::vector<vk::DeviceQueueCreateInfo> queueInfo = DetermineQueueIndexAllocation(
            bestPresentQueueFamilyIndex, bestRenderQueueFamilyIndex, bestTransferQueueFamilyIndex,
            presentQueueFamilyIndex, renderQueueFamilyIndex, transferQueueFamilyIndex);

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
        presentQueue = bestPresentQueueFamilyIndex.has_value()
                           ? device->getQueue(bestPresentQueueFamilyIndex.value(), presentQueueFamilyIndex)
                           : vk::Queue{};
        renderQueue = bestRenderQueueFamilyIndex.has_value()
                          ? device->getQueue(bestRenderQueueFamilyIndex.value(), renderQueueFamilyIndex)
                          : vk::Queue{};
        transferQueue = bestTransferQueueFamilyIndex.has_value()
                            ? device->getQueue(bestTransferQueueFamilyIndex.value(), transferQueueFamilyIndex)
                            : vk::Queue{};

        if (auto createResult =
                VulkanDescriptorHeap::Create(device.get(), descriptorLayoutImgui, kImGuiDescriptorCount);
            createResult.HasValue()) {
            descriptorHeapImgui = std::make_unique<VulkanDescriptorHeap>(std::move(createResult.Value()));
        } else {
            return createResult.Error();
        }
        SetObjectName(device.get(), descriptorHeapImgui->GetDescriptorPool(), "[Ymir-GCtx] ImGui Descriptor Pool");
        SetObjectName(device.get(), descriptorHeapImgui->GetDescriptorSetLayout(),
                      "[Ymir-GCtx] ImGui Descriptor Set Layout");

        // Descriptor update batching
        if (auto createResult = VulkanDescriptorUpdateBatch::Create(device.get()); createResult.HasValue()) {
            descriptorUpdateBatch = std::make_unique<VulkanDescriptorUpdateBatch>(std::move(createResult.Value()));
        } else {
            return createResult.Error();
        }

        // Swapchain
        if (auto createResult = VulkanSwapchain::Create(device.get(), physicalDevice, 0, presentQueue, surface,
                                                        kFrameCount, presentMode == PresentMode::VSync);
            createResult.HasValue()) {
            swapchain = std::make_unique<VulkanSwapchain>(std::move(createResult.Value()));
        } else {
            return createResult.Error();
        }

        const vk::Extent2D swapExtents = swapchain->GetSwapchainExtents();
        viewport = vk::Viewport{
            .x = 0.0f,
            .y = 0.0f,
            .width = static_cast<float>(swapExtents.width),
            .height = static_cast<float>(swapExtents.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        scissorRect.offset = {};
        scissorRect.extent = swapExtents;

        // Create frame resources
        {
            for (uint8 i = 0; i < kFrameCount; i++) {
                const vk::CommandPoolCreateInfo commandPoolInfo{
                    .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
                    .queueFamilyIndex = renderQueueFamilyIndex,
                };

                if (auto createResult = device->createCommandPoolUnique(commandPoolInfo);
                    createResult.result == vk::Result::eSuccess) {
                    frames[i].commandPool = std::move(createResult.value);
                } else {
                    return util::ErrorMessage{"Error creating command pool:" + vk::to_string(createResult.result)};
                }
                SetObjectName(device.get(), frames[i].commandPool.get(), "Swapchain Command Pool #{}", i);

                const vk::CommandBufferAllocateInfo commandBufferInfo{
                    .commandPool = frames[i].commandPool.get(),
                    .level = vk::CommandBufferLevel::ePrimary,
                    .commandBufferCount = 1,
                };

                if (auto allocateResult = device->allocateCommandBuffersUnique(commandBufferInfo);
                    allocateResult.result == vk::Result::eSuccess) {
                    frames[i].mainCommandBuffer = std::move(allocateResult.value[0]);
                } else {
                    return util::ErrorMessage{"Error allocating command buffer:" +
                                              vk::to_string(allocateResult.result)};
                }
                SetObjectName(device.get(), frames[i].mainCommandBuffer.get(), "Swapchain Main Command Buffer #{}", i);

                const vk::FenceCreateInfo fenceInfo{
                    .flags = vk::FenceCreateFlagBits::eSignaled,
                };
                if (auto createResult = device->createFenceUnique(fenceInfo);
                    createResult.result == vk::Result::eSuccess) {
                    frames[i].submitFence = std::move(createResult.value);
                } else {
                    return util::ErrorMessage{"Error creating fence:" + vk::to_string(createResult.result)};
                }
                SetObjectName(device.get(), frames[i].submitFence.get(), "Swapchain submit fence #{}", i);
            }
        }

        // Load shaders
        {
            auto fs = cmrc::ymir_sdl3_shaders::get_filesystem();
            auto loadShader = [&](const char *path) -> util::ValueResult<std::vector<char>> {
                assert(fs.is_file(path));
                auto shaderFile = fs.open(path);
                return std::vector<char>{shaderFile.begin(), shaderFile.end()};
            };

            // Load vertex shader
            auto vertexShaderBytecodeResult = loadShader("gctx/quad_vs.spv");
            if (!vertexShaderBytecodeResult) {
                return util::ErrorMessage{
                    fmt::format("Could not load vertex shader: {}", vertexShaderBytecodeResult.Error().message)};
            }
            vertexShader.format = ShaderBytecodeFormat::SPIRV;
            vertexShader.bytecode = vertexShaderBytecodeResult.Value();
            vertexShader.entrypoint = "VSMain";
            if (auto result = ValidateShader(vertexShader); !result) {
                return util::ErrorMessage{fmt::format("Vertex shader validation failed: {}", result.Error().message)};
            }

            // Load pixel shader
            auto pixelShaderBytecodeResult = loadShader("gctx/quad_ps.spv");
            if (!pixelShaderBytecodeResult) {
                return util::ErrorMessage{
                    fmt::format("Could not load pixel shader: {}", pixelShaderBytecodeResult.Error().message)};
            }
            pixelShader.format = ShaderBytecodeFormat::SPIRV;
            pixelShader.bytecode = pixelShaderBytecodeResult.Value();
            pixelShader.entrypoint = "PSMain";
            if (auto result = ValidateShader(pixelShader); !result) {
                return util::ErrorMessage{fmt::format("Pixel shader validation failed: {}", result.Error().message)};
            }
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

        const vk::Extent2D swapExtents = swapchain->GetSwapchainExtents();
        viewport = vk::Viewport{
            .x = 0.0f,
            .y = 0.0f,
            .width = static_cast<float>(swapExtents.width),
            .height = static_cast<float>(swapExtents.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        scissorRect.offset = {};
        scissorRect.extent = swapExtents;

        return {};
    }

    util::VoidResult<> BeginFrame() {

        const FrameContext &currFrame = GetCurrentFrameContext();

        if (const auto resetResult = currFrame.mainCommandBuffer->reset(); resetResult != vk::Result::eSuccess) {
            return util::ErrorMessage{
                fmt::format("Error resetting swapchain command buffer: {}", vk::to_string(resetResult))};
        }

        const vk::CommandBufferBeginInfo beginInfo{
            .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
        };
        if (const auto beginResult = currFrame.mainCommandBuffer->begin(beginInfo);
            beginResult != vk::Result::eSuccess) {
            return util::ErrorMessage{
                fmt::format("Error beginning swapchain command buffer: {}", vk::to_string(beginResult))};
        }

        // Transition present-image to be render-ready
        const vk::ImageMemoryBarrier presentBarrier{
            .srcAccessMask = {},
            .dstAccessMask = {},
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
            .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
            .image = swapchain->GetNextSwapImage(),
            .subresourceRange =
                vk::ImageSubresourceRange{
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = vk::RemainingArrayLayers,
                },
        };
        currFrame.mainCommandBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eBottomOfPipe,
                                                     vk::PipelineStageFlagBits::eColorAttachmentOutput,
                                                     vk::DependencyFlagBits::eByRegion, {}, {}, {presentBarrier});

        vk::RenderPass renderTargetRenderPass;
        if (auto getResult = GetRenderTargetRenderPass(swapchain->GetSurfaceImageFormat()); getResult.HasValue()) {
            renderTargetRenderPass = getResult.Value();
        } else {
            return getResult.Error();
        }

        const vk::Rect2D renderArea{
            .offset = {},
            .extent = swapchain->GetSwapchainExtents(),
        };
        static const std::array<float, 4> clearColor{0.0f, 0.0f, 0.0f, 0.0f};
        static const vk::ClearValue clearColorValue{vk::ClearColorValue(clearColor)};
        const vk::RenderPassBeginInfo renderPassInfo{
            .renderPass = renderTargetRenderPass,
            .framebuffer = swapchain->GetNextSwapFramebuffer(),
            .renderArea = renderArea,
            .clearValueCount = 1,
            .pClearValues = &clearColorValue,
        };
        currFrame.mainCommandBuffer->beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
        currFrame.mainCommandBuffer->setViewport(0, {viewport});
        currFrame.mainCommandBuffer->setScissor(0, {scissorRect});

        return {};
    }

    util::VoidResult<> EndFrame() {

        const FrameContext &currFrame = GetCurrentFrameContext();
        currFrame.mainCommandBuffer->endRenderPass();

        // Transition present-image to be present-ready
        const vk::ImageMemoryBarrier presentBarrier{
            .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
            .dstAccessMask = {},
            .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .newLayout = vk::ImageLayout::ePresentSrcKHR,
            .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
            .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
            .image = swapchain->GetNextSwapImage(),
            .subresourceRange =
                vk::ImageSubresourceRange{
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = vk::RemainingArrayLayers,
                },
        };

        currFrame.mainCommandBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                                                     vk::PipelineStageFlagBits::eBottomOfPipe,
                                                     vk::DependencyFlagBits::eByRegion, {}, {}, {presentBarrier});

        if (const auto endResult = currFrame.mainCommandBuffer->end(); endResult != vk::Result::eSuccess) {
            return util::ErrorMessage{fmt::format("Could not end frame command buffer: {}", vk::to_string(endResult))};
        }

        descriptorUpdateBatch->Flush();
        return {};
    }

    util::ValueResult<PresentResult> Present() {
        if (auto result = EndFrame(); !result) {
            return util::ErrorMessage{fmt::format("Could not end frame: {}", result.Error().message)};
        }

        const vk::Semaphore acquiredImageReady = swapchain->GetCurrentImageAcquiredSemaphore();

        const FrameContext &currFrame = GetCurrentFrameContext();

        // Submit work
        std::vector<vk::Semaphore> waitSemaphores;
        std::vector<vk::PipelineStageFlags> waitStages;
        std::vector<std::uint64_t> waitTimelineValues;
        std::vector<vk::Semaphore> signalSemaphores;

        // Wait for transfers
        // waitStages.emplace_back(vk::PipelineStageFlagBits::eTransfer);
        // waitTimelineValues.emplace_back(UploadTick);

        // Swapchain synchronization
        {
            // Wait for the swapchain image to be acquired
            waitSemaphores.emplace_back(acquiredImageReady);
            waitStages.emplace_back(vk::PipelineStageFlagBits::eColorAttachmentOutput);
            // This is a binary semaphore, push a dummy value
            waitTimelineValues.emplace_back(0);

            // Signal that the image is ready to be presented
            signalSemaphores.emplace_back(swapchain->GetNextImagePresentReadySemaphore());
        }

        const vk::StructureChain<vk::SubmitInfo, vk::TimelineSemaphoreSubmitInfo> SubmitInfoChain{
            vk::SubmitInfo{
                .waitSemaphoreCount = static_cast<std::uint32_t>(waitSemaphores.size()),
                .pWaitSemaphores = waitSemaphores.data(),
                .pWaitDstStageMask = waitStages.data(),
                .commandBufferCount = 1,
                .pCommandBuffers = &currFrame.mainCommandBuffer.get(),
                .signalSemaphoreCount = static_cast<std::uint32_t>(signalSemaphores.size()),
                .pSignalSemaphores = signalSemaphores.data(),
            },
            vk::TimelineSemaphoreSubmitInfo{
                .waitSemaphoreValueCount = static_cast<std::uint32_t>(waitTimelineValues.size()),
                .pWaitSemaphoreValues = waitTimelineValues.data(),
            },
        };

        if (const auto resetResult = device->resetFences({currFrame.submitFence.get()});
            resetResult != vk::Result::eSuccess) {
            return util::ErrorMessage{fmt::format("Error resetting swapchain fence: {}", vk::to_string(resetResult))};
        }

        if (auto submitResult = renderQueue.submit(SubmitInfoChain.get(), currFrame.submitFence.get());
            submitResult != vk::Result::eSuccess) {
            return util::ErrorMessage{
                fmt::format("Error submitting command buffer to render queue: {}", vk::to_string(submitResult))};
        }

        if (!swapchain->Present()) {
            return util::ErrorMessage{"Error presenting frame"};
        }

        if (auto result = MoveToNextFrame(); !result) {
            return util::ErrorMessage{fmt::format("Could not advance frame: {}", result.Error().message)};
        }
        if (auto result = BeginFrame(); !result) {
            return util::ErrorMessage{fmt::format("Could not begin frame: {}", result.Error().message)};
        }

        return PresentResult::Ok;
    }

    util::VoidResult<> WaitForGPU() {

        // Wait for current frame
        const FrameContext &currFrame = GetCurrentFrameContext();

        if (const auto waitResult = device->waitForFences({currFrame.submitFence.get()}, vk::True, ~0ULL);
            waitResult != vk::Result::eSuccess) {
            return util::ErrorMessage{fmt::format("Error waiting on swapchain fence: {}", vk::to_string(waitResult))};
        }

        // Wait for the device to idle to ensure the entire swapchain has been flushed
        // TODO: Replace this with waiting on actual timeline semaphore values
        if (const auto waitResult = device->waitIdle(); waitResult != vk::Result::eSuccess) {
            return util::ErrorMessage{fmt::format("Error waiting for device to idle: {}", vk::to_string(waitResult))};
        }

        return {};
    }

    util::VoidResult<> MoveToNextFrame() {

        // Update the frame index
        swapchain->AcquireNextImage(&frameIndex);

        const FrameContext &currFrame = GetCurrentFrameContext();

        // If the next frame has not finished rendering yet, wait for it.
        if (const auto waitResult = device->waitForFences({currFrame.submitFence.get()}, vk::True, ~0ULL);
            waitResult != vk::Result::eSuccess) {
            return util::ErrorMessage{fmt::format("Error waiting on swapchain fence: {}", vk::to_string(waitResult))};
        }

        if (const auto resetResult = device->resetCommandPool(currFrame.commandPool.get(), {});
            resetResult != vk::Result::eSuccess) {
            return util::ErrorMessage{
                fmt::format("Error resetting swapchain command pool: {}", vk::to_string(resetResult))};
        }

        return {};
    }

    util::ValueResult<TextureInstance> CreateTexture(const Texture2DSpec &spec) {
        TextureInstance newTexture{
            .spec = spec,
        };

        vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc |
                                    vk::ImageUsageFlagBits::eTransferDst;

        if (spec.access == TextureAccess::RenderTarget) {
            usage |= vk::ImageUsageFlagBits::eColorAttachment;
        }

        const vk::ImageCreateInfo imageInfo{
            .flags = {},
            .imageType = vk::ImageType::e2D,
            .format = ToVulkanFormat(spec.format),
            .extent =
                vk::Extent3D{
                    .width = spec.width,
                    .height = spec.height,
                    .depth = 1,
                },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = usage,
            .sharingMode = vk::SharingMode::eExclusive,
            .queueFamilyIndexCount = 1,
            .pQueueFamilyIndices = &renderQueueFamilyIndex,
            .initialLayout = vk::ImageLayout::eUndefined,
        };

        if (auto createResult = device->createImageUnique(imageInfo); createResult.result == vk::Result::eSuccess) {
            newTexture.image = std::move(createResult.value);
        } else {
            return util::ErrorMessage{fmt::format("Could not create texture:{}", vk::to_string(createResult.result))};
        }

        if (auto commitResult = CommitImageHeap(device.get(), physicalDevice, std::to_array({newTexture.image.get()}));
            commitResult.HasValue()) {
            newTexture.imageMemory = std::move(commitResult.Value());
        } else {
            return commitResult.Error();
        }

        // Transition image into

        return newTexture;
    }

    util::VoidResult<> ResizeTexture(TextureID id, uint32 width, uint32 height) {
        auto it = textures.find(id);
        if (it == textures.end()) {
            return util::ErrorMessage{"Texture does not exist"};
        }
        TextureInstance &texture = it->second;

        // First, try creating the new texture using the existing texture's specifications
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

    void DestroyTexture(TextureID id) {
        auto it = textures.find(id);
        if (it == textures.end()) {
            return;
        }
        auto &texture = it->second;
        SubmitTextureForDeletion(texture);
        textures.erase(it);
    }

    void SubmitTextureForDeletion(TextureInstance &texture) {
        TextureToDelete &texToDelete = texturesToDelete.emplace_back(std::move(texture));

        // TODO: Timeline semaphore value used to indicate when it is safe to delete this texture
        texToDelete.timeStamp = 1;
    }

    void DeletePendingTextures(bool force) {
        if (texturesToDelete.empty()) {
            return;
        }

        // TODO: Timeline semaphore value used to indicate when it is safe to delete this texture
        const uint64 currentTick = 0;

        while (!texturesToDelete.empty() && (force || texturesToDelete.front().timeStamp <= currentTick)) {
            texturesToDelete.front().~TextureToDelete();
            texturesToDelete.pop_front();
        }
    }

    bool IsTextureValid(TextureID id) {
        auto it = textures.find(id);
        if (it == textures.end()) {
            return false;
        }
        auto &texture = it->second;
        return texture.image.get();
    }

    TextureInstance *GetTexture(TextureID id) {
        auto it = textures.find(id);
        if (it == textures.end()) {
            return nullptr;
        }
        return &it->second;
    }

    util::VoidResult<> UpdateTexture(TextureID id, const IRect *rect,
                                     const std::function<void(void *data, size_t pitch)> &fnUpdate) {
        return {};
    }

    util::ValueResult<vk::RenderPass> GetRenderTargetRenderPass(vk::Format vulkanFormat) {
        auto it = renderTargetRenderPasses.find(vulkanFormat);
        if (it != renderTargetRenderPasses.end()) {
            return it->second.get();
        }

        // Create trivial render-pass for the rendertarget
        vk::UniqueRenderPass &renderTargetRenderPass = renderTargetRenderPasses[vulkanFormat];

        vk::RenderPassCreateInfo renderPassInfo{};

        const std::array<vk::AttachmentDescription, 1> attachments{{
            // Color Attachment
            vk::AttachmentDescription{
                .flags = vk::AttachmentDescriptionFlags(),
                .format = vulkanFormat,
                .samples = vk::SampleCountFlagBits::e1,
                .loadOp = vk::AttachmentLoadOp::eClear,
                .storeOp = vk::AttachmentStoreOp::eStore,
                .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
                .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
                .initialLayout = vk::ImageLayout::eColorAttachmentOptimal,
                .finalLayout = vk::ImageLayout::eColorAttachmentOptimal,
            },
        }};

        const std::array<vk::AttachmentReference, 1> attachmentRefs{{
            vk::AttachmentReference{.attachment = 0, .layout = vk::ImageLayout::eColorAttachmentOptimal},
        }};
        renderPassInfo.setAttachments(attachments);

        const std::array<vk::SubpassDescription, 1> subPasses{
            vk::SubpassDescription{
                .colorAttachmentCount = 1,
                .pColorAttachments = &attachmentRefs[0],
            },
        };
        renderPassInfo.setSubpasses(subPasses);

        const std::array<vk::SubpassDependency, 1> subpassDependencies{{
            vk::SubpassDependency{
                .srcSubpass = vk::SubpassExternal,
                .dstSubpass = 0,
                .srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe,
                .dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput |
                                vk::PipelineStageFlagBits::eEarlyFragmentTests |
                                vk::PipelineStageFlagBits::eLateFragmentTests,
                .srcAccessMask = vk::AccessFlagBits::eNone,
                .dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite |
                                 vk::AccessFlagBits::eDepthStencilAttachmentRead |
                                 vk::AccessFlagBits::eDepthStencilAttachmentWrite,
                .dependencyFlags = vk::DependencyFlagBits::eByRegion,
            },
        }};

        renderPassInfo.setDependencies(subpassDependencies);

        if (auto createResult = device->createRenderPassUnique(renderPassInfo);
            createResult.result == vk::Result::eSuccess) {
            renderTargetRenderPass = std::move(createResult.value);
        } else {
            return util::ErrorMessage{
                fmt::format("Error creating render-target render-pass: {}", vk::to_string(createResult.result))};
        }
        SetObjectName(device.get(), renderTargetRenderPass.get(), "RenderTarget RenderPass {}",
                      vk::to_string(vulkanFormat));

        return renderTargetRenderPass.get();
    }

    util::ValueResult<vk::Pipeline> GetRenderTargetPipeline(const TextureInstance &texture) {
        const vk::Format vulkanFormat = ToVulkanFormat(texture.spec.format);
        return GetRenderTargetPipeline(vulkanFormat);
    }

    util::ValueResult<vk::Pipeline> GetRenderTargetPipeline(vk::Format vulkanFormat) {
        auto it = renderTargetPipelines.find(vulkanFormat);
        if (it != renderTargetPipelines.end()) {
            return it->second.get();
        }

        // Create trivial render-pass for the rendertarget
        vk::RenderPass renderTargetRenderPass;
        if (auto getResult = GetRenderTargetRenderPass(vulkanFormat); getResult.HasValue()) {
            renderTargetRenderPass = getResult.Value();
        } else {
            return getResult.Error();
        }

        vk::UniquePipeline &renderTargetPipeline = renderTargetPipelines[vulkanFormat];

        // Create Pipeline Layout
        vk::PipelineLayoutCreateInfo graphicsPipelineLayoutInfo{};

        // ImGui expects 4 floats of push-constant data
        const vk::PushConstantRange pushConstantInfo{
            .stageFlags = vk::ShaderStageFlagBits::eVertex,
            .offset = 0,
            .size = sizeof(DrawTextureConstants),
        };
        graphicsPipelineLayoutInfo.setPushConstantRanges({pushConstantInfo});

        graphicsPipelineLayoutInfo.setSetLayouts({descriptorHeapImgui->GetDescriptorSetLayout()});

        vk::UniquePipelineLayout graphicsPipelineLayout;
        if (auto [result, newPipelineLayout] = device->createPipelineLayoutUnique(graphicsPipelineLayoutInfo);
            result == vk::Result::eSuccess) {
            graphicsPipelineLayout = std::move(newPipelineLayout);
        } else {
            return util::ErrorMessage{"Error creating pipeline layout"};
        }

        vk::UniqueShaderModule vertModule;
        vk::UniqueShaderModule fragModule;
        {
            const vk::ShaderModuleCreateInfo vertexShaderModuleInfo = {
                .codeSize = vertexShader.bytecode.size(),
                .pCode = reinterpret_cast<const std::uint32_t *>(vertexShader.bytecode.data()),
            };
            if (auto createResult = device->createShaderModuleUnique(vertexShaderModuleInfo);
                createResult.result == vk::Result::eSuccess) {
                vertModule = std::move(createResult.value);
            } else {
                return util::ErrorMessage{
                    fmt::format("Error creating imgui vertex shader module: {}", vk::to_string(createResult.result))};
            }
            const vk::ShaderModuleCreateInfo pixelShaderModuleInfo = {
                .codeSize = pixelShader.bytecode.size(),
                .pCode = reinterpret_cast<const std::uint32_t *>(pixelShader.bytecode.data()),
            };
            if (auto createResult = device->createShaderModuleUnique(pixelShaderModuleInfo);
                createResult.result == vk::Result::eSuccess) {
                fragModule = std::move(createResult.value);
            } else {
                return util::ErrorMessage{
                    fmt::format("Error creating imgui pixel shader module: {}", vk::to_string(createResult.result))};
            }
        }
        SetObjectName(device.get(), vertModule.get(), "Imgui vertex shader module");
        SetObjectName(device.get(), fragModule.get(), "Imgui pixel shader module");

        const std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStagesInfo{{
            vk::PipelineShaderStageCreateInfo{
                .stage = vk::ShaderStageFlagBits::eVertex,
                .module = vertModule.get(),
                .pName = vertexShader.entrypoint.data(),
            },
            vk::PipelineShaderStageCreateInfo{
                .stage = vk::ShaderStageFlagBits::eFragment,
                .module = fragModule.get(),
                .pName = pixelShader.entrypoint.data(),
            },
        }};

        vk::PipelineVertexInputStateCreateInfo vertexInputState = {};

        vertexInputState.setVertexBindingDescriptions(inputBindingDescs);
        vertexInputState.setVertexAttributeDescriptions(inputAttributeDescs);

        const vk::PipelineInputAssemblyStateCreateInfo InputAssemblyState{
            .topology = vk::PrimitiveTopology::eTriangleList,
            .primitiveRestartEnable = false,
        };

        vk::PipelineViewportStateCreateInfo ViewportState = {};
        static const vk::Viewport DefaultViewport = {0, 0, 16, 16, 0.0f, 1.0f};
        static const vk::Rect2D DefaultScissor = {{0, 0}, {16, 16}};
        ViewportState.viewportCount = 1;
        ViewportState.pViewports = &DefaultViewport;
        ViewportState.scissorCount = 1;
        ViewportState.pScissors = &DefaultScissor;

        const vk::PipelineRasterizationStateCreateInfo RasterizationState{
            .depthClampEnable = false,
            .rasterizerDiscardEnable = false,
            .polygonMode = vk::PolygonMode::eFill,
            .cullMode = vk::CullModeFlagBits::eBack,
            .frontFace = vk::FrontFace::eClockwise,
            .depthBiasEnable = false,
            .depthBiasConstantFactor = 0.0f,
            .depthBiasClamp = 0.0f,
            .depthBiasSlopeFactor = 0.0,
            .lineWidth = 1.0f,
        };

        vk::PipelineMultisampleStateCreateInfo MultisampleState{
            .rasterizationSamples = vk::SampleCountFlagBits::e1,
            .sampleShadingEnable = false,
            .minSampleShading = 1.0f,
            .pSampleMask = nullptr,
            .alphaToCoverageEnable = false,
            .alphaToOneEnable = false,
        };

        const vk::PipelineDepthStencilStateCreateInfo DepthStencilState = {
            .depthTestEnable = false,
            .depthWriteEnable = false,
            .depthCompareOp = vk::CompareOp::eLess,
            .depthBoundsTestEnable = false,
            .stencilTestEnable = false,
            .front = vk::StencilOpState{},
            .back = vk::StencilOpState{},
            .minDepthBounds = 0.0f,
            .maxDepthBounds = 1.0f,
        };

        const vk::PipelineColorBlendAttachmentState blendAttachmentState{
            .blendEnable = false,
            .srcColorBlendFactor = vk::BlendFactor::eOne,
            .dstColorBlendFactor = vk::BlendFactor::eZero,
            .colorBlendOp = vk::BlendOp::eAdd,
            .srcAlphaBlendFactor = vk::BlendFactor::eOne,
            .dstAlphaBlendFactor = vk::BlendFactor::eZero,
            .alphaBlendOp = vk::BlendOp::eAdd,
            .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                              vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
        };

        const vk::PipelineColorBlendStateCreateInfo colorBlendState{
            .logicOpEnable = false,
            .logicOp = vk::LogicOp::eClear,
            .attachmentCount = 1,
            .pAttachments = &blendAttachmentState,
        };

        vk::PipelineDynamicStateCreateInfo dynamicState = {};
        const std::array<vk::DynamicState, 2> dynamicStates{{
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor,
        }};
        dynamicState.setDynamicStates(dynamicStates);

        vk::GraphicsPipelineCreateInfo renderPipelineInfo = {};
        renderPipelineInfo.setStages(shaderStagesInfo);
        renderPipelineInfo.pVertexInputState = &vertexInputState;
        renderPipelineInfo.pInputAssemblyState = &InputAssemblyState;
        renderPipelineInfo.pViewportState = &ViewportState;
        renderPipelineInfo.pRasterizationState = &RasterizationState;
        renderPipelineInfo.pMultisampleState = &MultisampleState;
        renderPipelineInfo.pDepthStencilState = &DepthStencilState;
        renderPipelineInfo.pColorBlendState = &colorBlendState;
        renderPipelineInfo.pDynamicState = &dynamicState;
        renderPipelineInfo.subpass = 0;
        renderPipelineInfo.renderPass = renderTargetRenderPass;
        renderPipelineInfo.layout = graphicsPipelineLayout.get();

        // Create Pipeline
        if (auto createResult = device->createGraphicsPipelineUnique({}, renderPipelineInfo);
            createResult.result == vk::Result::eSuccess) {
            renderTargetPipeline = std::move(createResult.value);
        } else {
            return util::ErrorMessage{fmt::format("Error creating pipeline: {}", vk::to_string(createResult.result))};
        }

        SetObjectName(device.get(), renderTargetPipeline.get(), "RenderTarget Pipeline {}",
                      vk::to_string(vulkanFormat));

        return renderTargetPipeline.get();
    }

    util::VoidResult<> RenderToTexture(TextureID src, TextureID dst, const FRect &srcRect, const FRect &dstRect) {
        return {};
    }

    util::VoidResult<> DrawTextureRotated(TextureID id, const FRect &srcRect, const FRect &dstRect, double rotAngle,
                                          const FPoint2D *rotPivot) {
        return {};
    }

    FrameContext &GetCurrentFrameContext() {
        return frames[frameIndex];
    }

    const FrameContext &GetCurrentFrameContext() const {
        return frames[frameIndex];
    }
};

// -----------------------------------------------------------------------------

VulkanGraphicsContext::VulkanGraphicsContext(const VulkanGraphicsContextSpec &spec)
    : IGraphicsContext(kBackend)
    , m_impl(std::make_unique<Impl>(spec)) {}

VulkanGraphicsContext::~VulkanGraphicsContext() {
    if (m_impl->IsInitialized()) {
        m_impl->WaitForGPU();
        ImGuiShutdown();
    }
}

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
    return m_impl->ResizeFramebuffer(width, height);
}

void VulkanGraphicsContext::ClearScreen(gfx::ColorRGBA color) {
    // TODO: enqueue command to clear screen
}

bool VulkanGraphicsContext::ImGuiInit() {

    const ImGui_ImplVulkan_PipelineInfo pipelineInfoMain{
        .RenderPass = m_impl->swapchain->GetTrivialRenderPass(),
        .Subpass = 0,
    };

    ImGui_ImplVulkan_InitInfo initInfo = {
        .ApiVersion = m_impl->spec.apiLevel,
        .Instance = m_impl->instance.get(),
        .PhysicalDevice = m_impl->physicalDevice,
        .Device = m_impl->device.get(),
        .QueueFamily = m_impl->renderQueueFamilyIndex,
        .Queue = m_impl->renderQueue,
        .DescriptorPool = {},
        .DescriptorPoolSize = VulkanGraphicsContext::Impl::kImGuiDescriptorCount,
        .MinImageCount = VulkanGraphicsContext::Impl::kFrameCount,
        .ImageCount = m_impl->swapchain->GetSwapchainCount(),
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
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL3_Shutdown();
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
        const vk::CommandBuffer commandBuffer = m_impl->GetCurrentFrameContext().mainCommandBuffer.get();

        const DebugLabelScope debugScope(commandBuffer, {0.25f, 0.25f, 0.5f, 1.0f}, "ImGuiRenderFrame");
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
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
