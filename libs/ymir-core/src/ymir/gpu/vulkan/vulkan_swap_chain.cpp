#include <ymir/gpu/vulkan/vulkan_swap_chain.hpp>

#include <ymir/gpu/vulkan/vulkan_debug.hpp>

#include <algorithm>
#include <vulkan/vulkan_format_traits.hpp>

namespace {

vk::SurfaceFormatKHR FindSurfaceFormat(const vk::PhysicalDevice &physicalDevice, const vk::SurfaceKHR &surface) {
    // Determine surface format and color-space
    std::vector<vk::SurfaceFormatKHR> surfaceFormats;
    if (auto enumerateResult = physicalDevice.getSurfaceFormatsKHR(surface);
        enumerateResult.result == vk::Result::eSuccess) {
        surfaceFormats = std::move(enumerateResult.value);
    }

    // Prefer an sRGB presentation color-space
    std::ranges::stable_partition(surfaceFormats, [](const vk::SurfaceFormatKHR &surfaceFormat) -> bool {
        return surfaceFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
    });

    // After the stable partitions, the top of the list is the best candidate
    // surface format/color-space
    return surfaceFormats[0];
}

vk::PresentModeKHR FindPresentMode(const vk::PhysicalDevice &physicalDevice, const vk::SurfaceKHR &surface,
                                   bool vsync) {
    std::vector<vk::PresentModeKHR> presentModes;
    if (auto enumerateResult = physicalDevice.getSurfacePresentModesKHR(surface);
        enumerateResult.result == vk::Result::eSuccess) {
        presentModes = std::move(enumerateResult.value);
    } else {
        return vk::PresentModeKHR::eFifo;
    }

    const auto SupportsPresentMode = [&presentModes](vk::PresentModeKHR requestedPresentMode) -> bool {
        return std::ranges::find_if(presentModes,
                                    [&requestedPresentMode](const vk::PresentModeKHR &curPresentMode) -> bool {
                                        return curPresentMode == requestedPresentMode;
                                    }) != presentModes.cend();
    };

    const bool hasImmediate = SupportsPresentMode(vk::PresentModeKHR::eImmediate);

    const bool hasMailbox = SupportsPresentMode(vk::PresentModeKHR::eMailbox);

    // Vulkan mandates support for FIFO present mode as a baseline
    // Hard-sync with the monitor's refresh rate(vsync) with a FIFO queue
    // No tearing, most latency
    vk::PresentModeKHR result = vk::PresentModeKHR::eFifo;

    // Double/Triple/etc-Buffering with adaptive sync, similar to FIFO but
    // new images are allowed to bypass the queue when full and be presented
    // immediately as it comes in.
    // No tearing, less latency
    result = hasMailbox ? vk::PresentModeKHR::eMailbox : result;

    // No VSync requested
    if (!vsync) {
        // Immediately present frame, no synchronization
        // Tearing, minimal latency
        result = hasImmediate ? vk::PresentModeKHR::eImmediate : result;
    }

    return result;
}
} // namespace

namespace ymir::gpu::vulkan {

VulkanSwapchain::VulkanSwapchain(vk::Device device, vk::PhysicalDevice physicalDevice, uint32 presentQueueFamily,
                                 vk::Queue presentQueue)
    : m_device(device)
    , m_physicalDevice(physicalDevice)
    , m_presentQueueFamily(presentQueueFamily)
    , m_presentQueue(presentQueue) {}

util::VoidResult<> VulkanSwapchain::RecreateSwapchain(std::optional<vk::Extent2D> newExtent,
                                                      std::optional<vk::SwapchainKHR> oldSwapchain) {
    // Unfortunately this is the best way to ensure that any currently in-flight
    // frames are done.
    // TODO: VK_{KHR,EXT}_swapchain_maintenance1 has better swapchain
    // waiting/cleanup mechanisms that should be used here
    if (const vk::Result waitResult = m_device.waitIdle(); waitResult != vk::Result::eSuccess) {
        return util::ErrorMessage{"Error waiting on device to idle:" + vk::to_string(waitResult)};
    }

    /// Swapchain surface format
    m_surfaceFormat = FindSurfaceFormat(m_physicalDevice, m_surface);

    // Get present mode
    const vk::PresentModeKHR presetMode = FindPresentMode(m_physicalDevice, m_surface, m_vsync);

    vk::SurfaceCapabilitiesKHR surfaceCapabilities{};
    if (auto getResult = m_physicalDevice.getSurfaceCapabilitiesKHR(m_surface);
        getResult.result == vk::Result::eSuccess) {
        surfaceCapabilities = getResult.value;
    } else {
        // Error getting surface capabilities
        return util::ErrorMessage{"Error getting surface capabilities:" + vk::to_string(getResult.result)};
    }

    /// Swapchain image count

    // Clamp the requested swapchain size between the supported min/max
    // `maxImageCount` may be `0`, indicating there is no limit
    if (surfaceCapabilities.maxImageCount != 0u) {
        m_swapImageCount =
            std::clamp<uint32>(m_swapImageCount, surfaceCapabilities.minImageCount, surfaceCapabilities.maxImageCount);
    } else {
        m_swapImageCount = std::max<uint32>(m_swapImageCount, surfaceCapabilities.minImageCount);
    }

    /// Swapchain image extents
    if (surfaceCapabilities.currentExtent.width == 0 || surfaceCapabilities.currentExtent.height == 0) {
        // Window is likely minimized
        m_swapImages.clear();
        m_swapImageViews.clear();
        m_swapFramebuffers.clear();
        m_swapSemaphoreImageAcquired.clear();
        m_swapSemaphorePresentReady.clear();
        m_swapchainInstance.reset();
        return {};
    }

    // Set new size, or preserve the older one
    m_swapImageExtents = newExtent.value_or(surfaceCapabilities.currentExtent);

    /// Swapchain image usage
    // Color-attachment is mandated by the vulkan spec
    vk::ImageUsageFlags swapchainImageFlags = vk::ImageUsageFlagBits::eColorAttachment;

    // Transfer Src, possibly for screenshots
    if (surfaceCapabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eTransferSrc) {
        swapchainImageFlags |= vk::ImageUsageFlagBits::eTransferSrc;
    }

    // Transfer Dst, for blits, resolves, writes, etc
    if (surfaceCapabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eTransferDst) {
        swapchainImageFlags |= vk::ImageUsageFlagBits::eTransferDst;
    }

    /// Swapchain transform
    vk::SurfaceTransformFlagBitsKHR surfaceTransform;
    surfaceTransform = surfaceCapabilities.currentTransform;

    // Prefer Identity, if supported
    if (surfaceCapabilities.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity) {
        surfaceTransform = vk::SurfaceTransformFlagBitsKHR::eIdentity;
    }

    // Use old(current) swapchain if available, or the user-provided one
    // This seems to help allow previous image handles to be recycled, such as
    // how resizing a window to be smaller means you can just use a smaller
    // subset of the larger image. Or when the window is minified.
    const vk::SwapchainKHR oldSwapchainInstance = oldSwapchain.value_or(m_swapchainInstance.get());

    const vk::SwapchainCreateInfoKHR swapchainInfo{
        .flags = {},
        .surface = m_surface,
        .minImageCount = m_swapImageCount,
        .imageFormat = m_surfaceFormat.format,
        .imageColorSpace = m_surfaceFormat.colorSpace,
        .imageExtent = m_swapImageExtents,
        .imageArrayLayers = 1u,
        .imageUsage = swapchainImageFlags,
        .imageSharingMode = vk::SharingMode::eExclusive,
        .queueFamilyIndexCount = 1u,
        .pQueueFamilyIndices = &m_presentQueueFamily,
        .preTransform = surfaceTransform,
        .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
        .presentMode = presetMode,
        .clipped = vk::True,
        .oldSwapchain = oldSwapchainInstance,
    };

    if (auto createResult = m_device.createSwapchainKHRUnique(swapchainInfo);
        createResult.result == vk::Result::eSuccess) {
        SetObjectName(m_device, createResult.value.get(), "Swapchain");

        m_swapchainInstance = std::move(createResult.value);
    } else {
        return util::ErrorMessage{"Error creating swapchain:" + vk::to_string(createResult.result)};
    }

    /// Get swapchain images
    m_nextSwapImageIndex = 0;
    if (auto getResult = m_device.getSwapchainImagesKHR(m_swapchainInstance.get());
        getResult.result == vk::Result::eSuccess) {
        m_swapImages = std::move(getResult.value);
    } else {
        return util::ErrorMessage{"Error getting swapchain images:" + vk::to_string(getResult.result)};
    }

    /// Create swapchain image-views
    m_swapImageViews.resize(m_swapImageCount);
    for (uint8 swapIndex = 0; swapIndex < m_swapImageCount; ++swapIndex) {
        const vk::ImageViewCreateInfo imageViewInfo{
            .flags = {},
            .image = m_swapImages[swapIndex],
            .viewType = vk::ImageViewType::e2D,
            .format = m_surfaceFormat.format,
            .components = vk::ComponentMapping{},
            .subresourceRange =
                vk::ImageSubresourceRange{
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };
        if (auto createResult = m_device.createImageViewUnique(imageViewInfo);
            createResult.result == vk::Result::eSuccess) {
            m_swapImageViews[swapIndex] = std::move(createResult.value);
            SetObjectName(m_device, m_swapImageViews[swapIndex].get(), "Swapchain: ImageView #{}", swapIndex);
        } else {
            return util::ErrorMessage{"Error creating swapchain image view:" + vk::to_string(createResult.result)};
        }
    }

    /// Create trivial renderpass for baseline framebuffer compatibility
    {
        // Create trivial render-pass for the rendertarget
        vk::RenderPassCreateInfo renderPassInfo{};

        const std::array<vk::AttachmentDescription, 1> attachments{{
            // Color Attachment
            vk::AttachmentDescription{
                .flags = vk::AttachmentDescriptionFlags(),
                .format = m_surfaceFormat.format,
                .samples = vk::SampleCountFlagBits::e1,
                .loadOp = vk::AttachmentLoadOp::eLoad,
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

        if (auto createResult = m_device.createRenderPassUnique(renderPassInfo);
            createResult.result == vk::Result::eSuccess) {
            trivialRenderPass = std::move(createResult.value);
        } else {
            return util::ErrorMessage{
                fmt::format("Error creating trivial swapchain render-pass: {}", vk::to_string(createResult.result))};
        }
        SetObjectName(m_device, trivialRenderPass.get(), "Swapchain: Trivial render pass {}",
                      vk::to_string(m_surfaceFormat.format));
    }

    /// Create trivial swapchain image framebuffers
    m_swapFramebuffers.resize(m_swapImageCount);
    for (uint8 swapIndex = 0; swapIndex < m_swapImageCount; ++swapIndex) {
        const vk::ImageView swapImageColorTarget = m_swapImageViews[swapIndex].get();
        const vk::FramebufferCreateInfo framebufferInfo{
            .flags = {},
            .renderPass = trivialRenderPass.get(),
            .attachmentCount = 1,
            .pAttachments = &swapImageColorTarget,
            .width = m_swapImageExtents.width,
            .height = m_swapImageExtents.height,
            .layers = 1,
        };
        if (auto createResult = m_device.createFramebufferUnique(framebufferInfo);
            createResult.result == vk::Result::eSuccess) {
            m_swapFramebuffers[swapIndex] = std::move(createResult.value);
            SetObjectName(m_device, m_swapFramebuffers[swapIndex].get(), "Swapchain: Framebuffer #{}", swapIndex);
        } else {
            return util::ErrorMessage{"Error creating swapchain framebuffer:" + vk::to_string(createResult.result)};
        }
    }

    /// Swapchain synchronization primitives
    const vk::SemaphoreCreateInfo semaphoreInfo{};
    m_semaphoreCount = m_swapImageCount + 1;
    m_swapSemaphoreImageAcquired.resize(m_semaphoreCount);
    m_swapSemaphorePresentReady.resize(m_semaphoreCount);
    m_curImageAcquireSemaphoreIndex = 0;
    for (uint8 semaphoreIndex = 0; semaphoreIndex < m_semaphoreCount; ++semaphoreIndex) {
        if (auto createResult = m_device.createSemaphoreUnique(semaphoreInfo);
            createResult.result == vk::Result::eSuccess) {
            SetObjectName(m_device, createResult.value.get(), "Swapchain: Image-Acquired Semaphore #{}",
                          semaphoreIndex);

            m_swapSemaphoreImageAcquired[semaphoreIndex] = std::move(createResult.value);
        } else {
            return util::ErrorMessage{"Error creating swapchain semaphore:" + vk::to_string(createResult.result)};
        }

        if (auto createResult = m_device.createSemaphoreUnique(semaphoreInfo);
            createResult.result == vk::Result::eSuccess) {
            SetObjectName(m_device, createResult.value.get(), "Swapchain: Present-Ready Semaphore #{}", semaphoreIndex);

            m_swapSemaphorePresentReady[semaphoreIndex] = std::move(createResult.value);
        } else {
            return util::ErrorMessage{"Error creating present-ready semaphore:" + vk::to_string(createResult.result)};
        }
    }

    AcquireNextImage();

    return {};
}

vk::Semaphore VulkanSwapchain::AcquireNextImage(uint8_t *nextSwapIndex, vk::Fence signalFence) {
    if (!m_swapchainInstance) {
        return {};
    }

    // Semaphore to signal when the image has been acquired, generally all image
    // operations that use the swapchain image should wait on this semaphore
    const vk::Semaphore semaphoreImageAcquired = m_swapSemaphoreImageAcquired[m_curImageAcquireSemaphoreIndex].get();

    // Get the next swapchain image to render into

    const vk::ResultValue<uint32> acquireResult = m_device.acquireNextImageKHR(
        m_swapchainInstance.get(), std::numeric_limits<uint64>::max(), semaphoreImageAcquired, signalFence);

    switch (acquireResult.result) {
    case vk::Result::eSuccess: {
        assert(acquireResult.value <= std::numeric_limits<decltype(m_nextSwapImageIndex)>::max());

        // Got the next swapchain image to render into
        m_nextSwapImageIndex = acquireResult.value;
        if (nextSwapIndex) {
            *nextSwapIndex = m_nextSwapImageIndex;
        }
        break;
    }
    case vk::Result::eSuboptimalKHR:
    case vk::Result::eErrorSurfaceLostKHR:
    case vk::Result::eErrorOutOfDateKHR: {
        // Swapchain needs to be recreated
        if (RecreateSwapchain({}, m_swapchainInstance.get())) {
            return AcquireNextImage(nextSwapIndex, signalFence);
        } else {
            return {};
        }
        break;
    }
    default: return {};
    }

    return semaphoreImageAcquired;
}

bool VulkanSwapchain::Present() {
    vk::PresentInfoKHR presentInfo{};

    const vk::SwapchainKHR &swapchain = m_swapchainInstance.get();
    presentInfo.setSwapchains(swapchain);

    const uint32 nextImageIndex = m_nextSwapImageIndex;
    presentInfo.setImageIndices(nextImageIndex);

    // Wait for the image to be ready to be presented into
    std::vector<vk::Semaphore> waitSemaphores;
    waitSemaphores.emplace_back(GetNextImagePresentReadySemaphore());
    presentInfo.setWaitSemaphores(waitSemaphores);

    const vk::Result presentResult = m_presentQueue.presentKHR(presentInfo);

    switch (presentResult) {
    case vk::Result::eSuboptimalKHR:
    case vk::Result::eErrorOutOfDateKHR:
    case vk::Result::eSuccess: {
        break;
    }
    case vk::Result::eErrorSurfaceLostKHR:
    default: {
        // Unhandled result
        return false;
    }
    }

    // Move on to the next semaphore
    m_curImageAcquireSemaphoreIndex = (m_curImageAcquireSemaphoreIndex + 1) % m_semaphoreCount;

    return true;
}

util::ValueResult<VulkanSwapchain> VulkanSwapchain::Create(vk::Device device, vk::PhysicalDevice physicalDevice,
                                                           uint32 presentQueueFamily, vk::Queue presentQueue,
                                                           const vk::SurfaceKHR &surface, uint8 swapchainCount,
                                                           bool vsync, const VulkanSwapchain *oldSwapchain) {
    VulkanSwapchain newSwapchain(device, physicalDevice, presentQueueFamily, presentQueue);

    newSwapchain.m_surface = surface;
    newSwapchain.m_vsync = vsync;

    // Let RecreateSwapchain "fix" these assignments
    newSwapchain.m_swapImageCount = swapchainCount;

    const vk::SwapchainKHR OldSwapchainHandle =
        oldSwapchain != nullptr ? oldSwapchain->m_swapchainInstance.get() : vk::SwapchainKHR();

    newSwapchain.RecreateSwapchain({}, OldSwapchainHandle);

    return newSwapchain;
}
} // namespace ymir::gpu::vulkan