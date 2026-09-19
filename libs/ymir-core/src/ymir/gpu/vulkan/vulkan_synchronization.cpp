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

util::ValueResult<uint64> UpdateTimelineSemaphoreValue(const vk::Device logicalDevice,
                                                       const vk::Semaphore timelineSemaphore,
                                                       std::atomic_uint64_t &hostTickValue) {
    uint64 expectedCpuTickValue;
    uint64 incomingGpuTickValue;
    do {
        // For the duration of the loop iteration, this value should stay still
        expectedCpuTickValue = hostTickValue.load(std::memory_order_acquire);

        // Get the current timeline semaphore value from the GPU
        if (auto getResult = logicalDevice.getSemaphoreCounterValue(timelineSemaphore);
            getResult.result == vk::Result::eSuccess) {
            incomingGpuTickValue = getResult.value;
        } else {
            // Error getting timeline semaphore value
            return util::ErrorMessage{
                fmt::format("Error waiting getting timeline semaphore value: {}", vk::to_string(getResult.result))};
        }

    } while (!hostTickValue.compare_exchange_weak(expectedCpuTickValue, incomingGpuTickValue, std::memory_order_release,
                                                  std::memory_order_relaxed));

    return expectedCpuTickValue;
}

util::ValueResult<std::chrono::nanoseconds> WaitUntilSemaphoreValue(const vk::Device device,
                                                                    const vk::Semaphore timelineSemaphore,
                                                                    const uint64_t timelineValue,
                                                                    const std::chrono::nanoseconds timeOut) {
    const vk::SemaphoreWaitInfo waitInfo{
        .semaphoreCount = 1,
        .pSemaphores = &timelineSemaphore,
        .pValues = &timelineValue,
    };

    const auto startTime = std::chrono::high_resolution_clock::now();
    vk::Result waitResult = device.waitSemaphores(waitInfo, timeOut.count());
    while (waitResult == vk::Result::eTimeout) {
        // Spin
        waitResult = device.waitSemaphores(waitInfo, timeOut.count());
    }
    const auto stopTime = std::chrono::high_resolution_clock::now();

    if (waitResult == vk::Result::eSuccess) {
        return stopTime - startTime;
    }

    return util::ErrorMessage{fmt::format("Error waiting on timeline semaphore: {}", vk::to_string(waitResult))};
}

} // namespace ymir::gpu::vulkan