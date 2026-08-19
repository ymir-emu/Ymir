#include <ymir/gpu/vulkan/vulkan_synchronization.hpp>

namespace ymir::gpu::vulkan {

void FindQueueFamilyIndices(vk::PhysicalDevice physicalDevice, vk::SurfaceKHR surface,
                            std::optional<uint32> &presentQueueFamilyIndex,
                            std::optional<uint32> &renderQueueFamilyIndex,
                            std::optional<uint32> &transferQueueFamilyIndex) {
    // Determine which queue families to use
    {
        const std::vector<vk::QueueFamilyProperties> queueFamilyProperties = physicalDevice.getQueueFamilyProperties();

        // Present Queue, check if the surface can be presented to
        if (surface) {
            for (uint32 curQueueFamilyIndex = 0;
                 const vk::QueueFamilyProperties &queueFamilyProperty : queueFamilyProperties) {
                (void)queueFamilyProperty;

                // If the queue-family supports presenting to this particular surface, then we want it!
                if (auto GetResult = physicalDevice.getSurfaceSupportKHR(curQueueFamilyIndex, surface);
                    GetResult.result == vk::Result::eSuccess) {
                    if (GetResult.value == vk::True) {
                        presentQueueFamilyIndex = curQueueFamilyIndex;
                        break;
                    }
                }
                curQueueFamilyIndex++;
            }
        }

        // Render Queue
        // Just get the first queue family that supports rendering, unless there are other requirements that we care for
        // at some such as transfer granularity or timestamp-resolution
        for (uint32 curQueueFamilyIndex = 0;
             const vk::QueueFamilyProperties &queueFamilyProperty : queueFamilyProperties) {
            if (queueFamilyProperty.queueFlags & vk::QueueFlagBits::eGraphics) {
                renderQueueFamilyIndex = curQueueFamilyIndex;
                break;
            }
            curQueueFamilyIndex++;
        }

        // Some queues don't set the transfer flag at all?
        // I've experienced this on my ThinkPad x13s. To mitigate this, default the transfer-queue to be the same as the
        // main graphics queue and then refine  selection
        transferQueueFamilyIndex = renderQueueFamilyIndex;

        // Transfer Queue
        // A queue with the transfer bit set, and the least amount of other bits set, generally maps to dedicated DMA
        // hardware
        for (uint32 curQueueFamilyIndex = 0, minQueueFamilyBitCount = ~0u;
             const vk::QueueFamilyProperties &queueFamilyProperty : queueFamilyProperties) {
            // Keep track of the queue with the least amount of bits set
            const uint32 curQueueFamilyBitCount = std::popcount(static_cast<uint32>(queueFamilyProperty.queueFlags));

            if (queueFamilyProperty.queueFlags & vk::QueueFlagBits::eTransfer &&
                curQueueFamilyBitCount < minQueueFamilyBitCount) {
                minQueueFamilyBitCount = curQueueFamilyBitCount;
                transferQueueFamilyIndex = curQueueFamilyIndex;
            }
            curQueueFamilyIndex++;
        }
    }
}

const std::array<float, 3> QueuePriority = {{1.0f, 1.0f, 1.0f}};

std::vector<vk::DeviceQueueCreateInfo>
DetermineQueueIndexAllocation(const std::optional<uint32> &presentQueueFamilyIndex,
                              const std::optional<uint32> &renderQueueFamilyIndex,
                              const std::optional<uint32> &transferQueueFamilyIndex, uint32 &presentQueueIndex,
                              uint32 &renderQueueIndex, uint32 &transferQueueIndex) {
    std::vector<vk::DeviceQueueCreateInfo> QueueInfo;

    // Create QueueInfos based on the number of unique queue-families that
    // are actually required
    std::unordered_map<uint32, uint32> QueueFamilyHistogram;
    if (renderQueueFamilyIndex.has_value()) {
        renderQueueIndex = QueueFamilyHistogram[renderQueueFamilyIndex.value()]++;
    }
    if (presentQueueFamilyIndex.has_value()) {
        presentQueueIndex = QueueFamilyHistogram[presentQueueFamilyIndex.value()]++;
    }
    if (transferQueueFamilyIndex.has_value()) {
        transferQueueIndex = QueueFamilyHistogram[transferQueueFamilyIndex.value()]++;
    }

    QueueInfo.reserve(QueueFamilyHistogram.size());
    for (const auto &[QueueFamily, QueueFamilyCount] : QueueFamilyHistogram) {
        QueueInfo.emplace_back(vk::DeviceQueueCreateInfo{
            .flags = {},
            .queueFamilyIndex = QueueFamily,
            .queueCount = QueueFamilyCount,
            .pQueuePriorities = QueuePriority.data(),
        });
    }

    return QueueInfo;
}

} // namespace ymir::gpu::vulkan