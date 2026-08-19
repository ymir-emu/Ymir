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
        m_swapImageCount = std::clamp<std::uint32_t>(m_swapImageCount, surfaceCapabilities.minImageCount,
                                                     surfaceCapabilities.maxImageCount);
    } else {
        m_swapImageCount = std::max<std::uint32_t>(m_swapImageCount, surfaceCapabilities.minImageCount);
    }

    /// Swapchain image extents
    if (surfaceCapabilities.currentExtent.width == 0 || surfaceCapabilities.currentExtent.height == 0) {
        // Window is likely minimized
        m_swapImages.clear();
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
    if (auto getResult = m_device.getSwapchainImagesKHR(m_swapchainInstance.get());
        getResult.result == vk::Result::eSuccess) {
        m_swapImages = std::move(getResult.value);
    } else {
        return util::ErrorMessage{"Error getting swapchain images:" + vk::to_string(getResult.result)};
    }

    return {};
}

vk::Semaphore VulkanSwapchain::AcquireNextImage() {
    if (!m_swapchainInstance) {
        return {};
    }

    // Semaphore to signal when the image has been acquired, generally all image
    // operations that use the swapchain image should wait on this semaphore
    const vk::Semaphore semaphoreImageAcquired = m_swapSemaphoreImageAcquired[m_curImageAcquireSemaphoreIndex].get();

    // Get the next swapchain image to render into
    constexpr std::uint64_t timeout = std::numeric_limits<std::uint64_t>::max();

    // Bypass the default vulkan-hpp implementation which asserts upon results
    // such as `eErrorSurfaceLostKHR` and `eErrorOutOfDateKHR`
    const auto unwrappedAcquireNextImageKHR = [](vk::Device m_device, vk::SwapchainKHR swapchain, uint64_t timeout,
                                                 vk::Semaphore semaphore, vk::Fence fence,
                                                 VULKAN_HPP_DEFAULT_DISPATCHER_TYPE const &d =
                                                     VULKAN_HPP_DEFAULT_DISPATCHER) {
        VULKAN_HPP_ASSERT(d.getVkHeaderVersion() == vk::HeaderVersion);
#if (VULKAN_HPP_DISPATCH_LOADER_DYNAMIC == 1)
        VULKAN_HPP_ASSERT(d.vkAcquireNextImageKHR && "Function <vkAcquireNextImageKHR> requires <VK_KHR_swapchain>");
#endif

        std::uint32_t imageIndex;
        vk::Result result = static_cast<vk::Result>(
            d.vkAcquireNextImageKHR(m_device, static_cast<VkSwapchainKHR>(swapchain), timeout,
                                    static_cast<VkSemaphore>(semaphore), static_cast<VkFence>(fence), &imageIndex));

        return vk::ResultValue<uint32_t>(result, imageIndex);
    };

    // const vk::ResultValue<std::uint32_t> AcquireResult
    //	= m_device.acquireNextImageKHR(
    //		SwapchainInstance.get(), Timeout, SemaphoreImageAcquired,
    //		vk::Fence{}
    //	);
    const vk::ResultValue<std::uint32_t> acquireResult =
        unwrappedAcquireNextImageKHR(m_device, m_swapchainInstance.get(), timeout, semaphoreImageAcquired, vk::Fence{});

    switch (acquireResult.result) {
    case vk::Result::eSuccess: {
        assert(acquireResult.value <= std::numeric_limits<decltype(m_nextSwapImageIndex)>::max());

        // Got the next swapchain image to render into
        m_nextSwapImageIndex = acquireResult.value;
        break;
    }
    case vk::Result::eSuboptimalKHR:
    case vk::Result::eErrorSurfaceLostKHR:
    case vk::Result::eErrorOutOfDateKHR: {
        // TODO: Swapchain needs to be recreated
        if (RecreateSwapchain({}, m_swapchainInstance.get())) {
            return AcquireNextImage();
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
    vk::PresentInfoKHR PresentInfo{};

    const vk::SwapchainKHR &Swapchain = m_swapchainInstance.get();
    PresentInfo.setSwapchains(Swapchain);

    const std::uint32_t NextImageIndex = m_nextSwapImageIndex;
    PresentInfo.setImageIndices(NextImageIndex);

    // Wait for the image to be ready to be presented into
    std::vector<vk::Semaphore> WaitSemaphores;
    WaitSemaphores.emplace_back(GetNextImagePresentReadySemaphore());
    PresentInfo.setWaitSemaphores(WaitSemaphores);

    // Bypass the default vulkan-hpp implementation which asserts upon results
    // such as `eErrorSurfaceLostKHR` and `eErrorOutOfDateKHR`
    const auto UnwrappedPresentKHR = [](vk::Queue m_queue, const vk::PresentInfoKHR &presentInfo,
                                        VULKAN_HPP_DEFAULT_DISPATCHER_TYPE const &d = VULKAN_HPP_DEFAULT_DISPATCHER) {
        VULKAN_HPP_ASSERT(d.getVkHeaderVersion() == vk::HeaderVersion);
#if (VULKAN_HPP_DISPATCH_LOADER_DYNAMIC == 1)
        VULKAN_HPP_ASSERT(d.vkQueuePresentKHR && "Function <vkQueuePresentKHR> requires <VK_KHR_swapchain>");
#endif

        return static_cast<vk::Result>(
            d.vkQueuePresentKHR(m_queue, reinterpret_cast<const VkPresentInfoKHR *>(&presentInfo)));
    };

    // const vk::Result PresentResult
    //	= VulkanContext.PresentQueue.presentKHR(PresentInfo);
    const vk::Result PresentResult = UnwrappedPresentKHR(m_presentQueue, PresentInfo);

    switch (PresentResult) {
    case vk::Result::eSuccess: {
        break;
    }
    case vk::Result::eSuboptimalKHR:
    case vk::Result::eErrorSurfaceLostKHR:
    case vk::Result::eErrorOutOfDateKHR: {
        return false;
    }
    default: {
        // Unhandled result
        return false;
    }
    }

    // Move on to the next semaphore
    m_curImageAcquireSemaphoreIndex = (m_curImageAcquireSemaphoreIndex + 1) % GetSwapchainCount();

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

    /// Swapchain synchronization primitives
    const vk::SemaphoreCreateInfo SemaphoreInfo{};
    for (std::uint8_t swapIndex = 0; swapIndex < newSwapchain.m_swapImageCount; ++swapIndex) {
        SetObjectName(device, newSwapchain.m_swapImages[swapIndex], "Swapchain: Image #{}", swapIndex);

        if (auto createResult = device.createSemaphoreUnique(SemaphoreInfo);
            createResult.result == vk::Result::eSuccess) {
            SetObjectName(device, createResult.value.get(), "Swapchain: Image-Acquired Semaphore #{}", swapIndex);

            newSwapchain.m_swapSemaphoreImageAcquired.emplace_back(std::move(createResult.value));
        } else {
            return util::ErrorMessage{"Error creating swapchain semaphore:" + vk::to_string(createResult.result)};
        }

        if (auto createResult = device.createSemaphoreUnique(SemaphoreInfo);
            createResult.result == vk::Result::eSuccess) {
            SetObjectName(device, createResult.value.get(), "Swapchain: Present-Ready Semaphore #{}", swapIndex);

            newSwapchain.m_swapSemaphorePresentReady.emplace_back(std::move(createResult.value));
        } else {
            return util::ErrorMessage{"Error creating present-ready semaphore:" + vk::to_string(createResult.result)};
        }
    }

    return newSwapchain;
}
} // namespace ymir::gpu::vulkan