#include <ymir/gpu/vulkan/vulkan_api.hpp>

#include <ymir/util/result.hpp>

/**
@file
@brief Defines `VulkanSwapChain`, a wrapper for `vk::SwapchainKHR`
*/

namespace ymir::gpu::vulkan {

/// @brief Manages an `vkSwapchainKHR` and provides synchronization primitives.
class VulkanSwapchain final {
private:
    VulkanSwapchain(vk::Device device, vk::PhysicalDevice physicalDevice, uint32 presentQueueFamily,
                    vk::Queue presentQueue);

    const vk::Device m_device;
    const vk::PhysicalDevice m_physicalDevice;
    const uint32 m_presentQueueFamily;
    const vk::Queue m_presentQueue;

    vk::UniqueSwapchainKHR m_swapchainInstance;

    vk::SurfaceKHR m_surface;
    vk::SurfaceFormatKHR m_surfaceFormat;

    bool m_vsync;

    std::uint8_t m_swapImageCount = 0u;
    vk::Extent2D m_swapImageExtents;

    std::vector<vk::Image> m_swapImages;

    // Semaphores that `vkAcquireNextImageKHR` will signal for then the
    // swapchain image is ready to be rendered into. A new frame should wait on
    // this semaphore.
    std::uint8_t m_curImageAcquireSemaphoreIndex = 0u;
    std::vector<vk::UniqueSemaphore> m_swapSemaphoreImageAcquired;

    // Current swap-image to render into. This is the result of
    // `vkAcquireNextImageKHR`
    std::uint8_t m_nextSwapImageIndex = 0u;

    // Semaphores that render-frames should signal to indicate that they are
    // ready to be presented. Calls to `vkPresentKHR` will wait on this
    // semaphore
    std::vector<vk::UniqueSemaphore> m_swapSemaphorePresentReady;

public:
    VulkanSwapchain(VulkanSwapchain &&) = default;
    ~VulkanSwapchain() = default;

    [[nodiscard]] const vk::SurfaceFormatKHR &GetSurfaceFormat() const {
        return m_surfaceFormat;
    }

    [[nodiscard]] const vk::Format &GetSurfaceImageFormat() const {
        return GetSurfaceFormat().format;
    }

    [[nodiscard]] std::uint8_t GetSwapchainCount() const {
        return m_swapImageCount;
    }

    [[nodiscard]] const vk::Extent2D &GetSwapchainExtents() const {
        return m_swapImageExtents;
    }

    [[nodiscard]] std::uint32_t GetWidth() const {
        return GetSwapchainExtents().width;
    }

    [[nodiscard]] std::uint32_t GetHeight() const {
        return GetSwapchainExtents().height;
    }

    [[nodiscard]] const vk::Image &GetSwapImage(std::uint8_t swapIndex) const {
        return m_swapImages.at(swapIndex);
    }

    [[nodiscard]] const vk::Image &GetNextSwapImage() const {
        return m_swapImages.at(m_nextSwapImageIndex);
    }

    [[nodiscard]] const vk::Semaphore &GetImageAcquiredSemaphore(std::uint8_t swapIndex) const {
        return m_swapSemaphoreImageAcquired.at(swapIndex).get();
    }

    [[nodiscard]] const vk::Semaphore &GetCurrentImageAcquiredSemaphore() const {
        return GetImageAcquiredSemaphore(m_curImageAcquireSemaphoreIndex);
    }

    [[nodiscard]] const vk::Semaphore &GetImagePresentReadySemaphore(std::uint8_t swapIndex) const {
        return m_swapSemaphorePresentReady.at(swapIndex).get();
    }

    [[nodiscard]] const vk::Semaphore &GetNextImagePresentReadySemaphore() const {
        return GetImagePresentReadySemaphore(m_nextSwapImageIndex);
    }

    util::VoidResult<> RecreateSwapchain(std::optional<vk::Extent2D> newExtent = {},
                                         std::optional<vk::SwapchainKHR> oldSwapchain = {});

    // Move on to the next image in the swapchain. Returns the semaphore to wait
    // on for when the image is actually ready to be rendered into. Returns a
    // null-handle if there was an error or if the swapchain needs to be
    // recreated.
    [[nodiscard]] vk::Semaphore AcquireNextImage();

    // Waits on the current "Present-Ready"-semaphore and presents the current
    // swapchain image to the present-queue
    // Returns true on a successful present
    // Returns false otherwise(swapchain was invalidated)
    bool Present();

    static util::ValueResult<VulkanSwapchain> Create(vk::Device device, vk::PhysicalDevice physicalDevice,
                                                     uint32 presentQueueFamily, vk::Queue presentQueue,
                                                     const vk::SurfaceKHR &surface, uint8 swapchainCount, bool vsync,
                                                     const VulkanSwapchain *oldSwapchain = nullptr);
};
} // namespace ymir::gpu::vulkan