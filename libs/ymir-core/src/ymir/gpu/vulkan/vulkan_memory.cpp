#include <ymir/gpu/vulkan/vulkan_memory.hpp>

#include <ymir/util/bit_ops.hpp>

namespace {
// Given a speculative heap-allocation, defined by its current size and
// memory-type bits, appends a memory requirements structure to it, updating
// both the size and the required memory-type-bits. Returns the offset within
// the heap for the current MemoryRequirements
// TODO: Rather than using a running-size of the heap, look at all of the memory
// requests and optimally create a packing for all of the offset and alignment
// requirements. Such as by satisfying all of the largest alignments first, and
// then the smallest, to reduce padding
vk::DeviceSize CommitMemoryRequestToHeap(const vk::MemoryRequirements &curMemoryRequirements,
                                         vk::DeviceSize &curHeapEnd, std::uint32_t &curMemoryTypeBits,
                                         vk::DeviceSize sizeAlignment) {
    // Accumulate a mask of all the memory types that satisfies each of the
    // handles
    curMemoryTypeBits &= curMemoryRequirements.memoryTypeBits;

    // Pad up the memory sizes so they are not considered aliasing
    const vk::DeviceSize curMemoryOffset = bit::align(curHeapEnd, curMemoryRequirements.alignment);
    // Pad the size by the required size-alignment.
    // Intended for bufferImageGranularity
    const vk::DeviceSize curMemorySize = bit::align(curMemoryRequirements.size, sizeAlignment);

    curHeapEnd = (curMemoryOffset + curMemorySize);
    return curMemoryOffset;
}
} // namespace

namespace ymir::gpu::vulkan {

std::optional<std::uint32_t> FindMemoryTypeIndex(vk::PhysicalDevice physicalDevice, std::uint32_t memoryTypeMask,
                                                 vk::MemoryPropertyFlags memoryProperties,
                                                 vk::MemoryPropertyFlags memoryExcludeProperties) {
    const vk::PhysicalDeviceMemoryProperties deviceMemoryProperties = physicalDevice.getMemoryProperties();
    // Iterate the physical device's memory types until we find a match
    for (std::uint32_t memoryTypeIndex = 0; memoryTypeIndex < deviceMemoryProperties.memoryTypeCount;
         memoryTypeIndex++) {
        if (
            // Is within memory type mask
            (((memoryTypeMask >> memoryTypeIndex) & 0b1) == 0b1) &&
            // Has property flags
            (deviceMemoryProperties.memoryTypes[memoryTypeIndex].propertyFlags & memoryProperties) ==
                memoryProperties &&
            // None of the excluded properties are enabled
            !(deviceMemoryProperties.memoryTypes[memoryTypeIndex].propertyFlags & memoryExcludeProperties)) {
            return memoryTypeIndex;
        }
    }

    return std::nullopt;
}

util::ValueResult<vk::UniqueDeviceMemory> CommitImageHeap(vk::Device device, vk::PhysicalDevice physicalDevice,
                                                          const std::span<const vk::Image> images,
                                                          vk::MemoryPropertyFlags memoryProperties,
                                                          vk::MemoryPropertyFlags memoryExcludeProperties) {
    vk::MemoryAllocateInfo imageHeapAllocInfo = {};
    std::uint32_t imageHeapMemoryTypeBits = 0xFFFFFFFF;
    std::vector<vk::BindImageMemoryInfo> imageHeapBinds;

    const vk::DeviceSize bufferImageGranularity = physicalDevice.getProperties().limits.bufferImageGranularity;

    for (const vk::Image &curImage : images) {
        const vk::DeviceSize curBindOffset =
            CommitMemoryRequestToHeap(device.getImageMemoryRequirements(curImage), imageHeapAllocInfo.allocationSize,
                                      imageHeapMemoryTypeBits, bufferImageGranularity);

        if (imageHeapMemoryTypeBits == 0) {
            return util::ErrorMessage{"Unable to satisfy memory heap for all images to share"};
        }

        // Put nullptr for the device memory for now
        imageHeapBinds.emplace_back(vk::BindImageMemoryInfo{
            .image = curImage,
            .memory = nullptr,
            .memoryOffset = curBindOffset,
        });
    }

    std::uint32_t memoryTypeIndex = 0;

    if (auto findResult =
            FindMemoryTypeIndex(physicalDevice, imageHeapMemoryTypeBits, memoryProperties, memoryExcludeProperties);
        findResult) {
        memoryTypeIndex = *findResult;
    } else {
        return util::ErrorMessage{"Unable to satisfy memory heap for all images to share"};
    }

    imageHeapAllocInfo.memoryTypeIndex = memoryTypeIndex;

    vk::UniqueDeviceMemory imageHeapMemory = {};

    if (auto allocResult = device.allocateMemoryUnique(imageHeapAllocInfo);
        allocResult.result == vk::Result::eSuccess) {
        imageHeapMemory = std::move(allocResult.value);
    } else {
        return util::ErrorMessage{fmt::format("Could not create device memory:{}", vk::to_string(allocResult.result))};
    }

    // Assign the device memory to the bindings
    for (vk::BindImageMemoryInfo &curBind : imageHeapBinds) {
        curBind.memory = imageHeapMemory.get();
    }

    // Now bind them all in one call
    if (const vk::Result bindResult = device.bindImageMemory2(imageHeapBinds); bindResult == vk::Result::eSuccess) {
        // Binding memory succeeded
    } else {
        return util::ErrorMessage{fmt::format("Could not bind image device-memory:{}", vk::to_string(bindResult))};
    }

    return imageHeapMemory;
}

util::ValueResult<vk::UniqueDeviceMemory> CommitBufferHeap(vk::Device device, vk::PhysicalDevice physicalDevice,
                                                           const std::span<const vk::Buffer> buffers,
                                                           vk::MemoryPropertyFlags memoryProperties,
                                                           vk::MemoryPropertyFlags memoryExcludeProperties) {
    vk::MemoryAllocateInfo bufferHeapAllocInfo = {};
    std::uint32_t bufferHeapMemoryTypeBits = 0xFFFFFFFF;
    std::vector<vk::BindBufferMemoryInfo> bufferHeapBinds;

    const vk::DeviceSize bufferImageGranularity = physicalDevice.getProperties().limits.bufferImageGranularity;

    for (const vk::Buffer &curBuffer : buffers) {
        const vk::DeviceSize curBindOffset =
            CommitMemoryRequestToHeap(device.getBufferMemoryRequirements(curBuffer), bufferHeapAllocInfo.allocationSize,
                                      bufferHeapMemoryTypeBits, bufferImageGranularity);

        if (bufferHeapMemoryTypeBits == 0) {
            return util::ErrorMessage{"Unable to satisfy memory heap for all buffers to share"};
        }

        // Put nullptr for the device memory for now
        bufferHeapBinds.emplace_back(vk::BindBufferMemoryInfo{
            .buffer = curBuffer,
            .memory = nullptr,
            .memoryOffset = curBindOffset,
        });
    }

    // Attempt to find a valid memory type index
    const std::optional<std::uint32_t> searchedMemoryTypeIndex =
        FindMemoryTypeIndex(physicalDevice, bufferHeapMemoryTypeBits, memoryProperties, memoryExcludeProperties);

    if (!searchedMemoryTypeIndex.has_value()) {
        return util::ErrorMessage{"Unable to satisfy memory heap for all buffers to share"};
    }

    bufferHeapAllocInfo.memoryTypeIndex = *searchedMemoryTypeIndex;

    vk::UniqueDeviceMemory bufferHeapMemory = {};

    if (auto allocResult = device.allocateMemoryUnique(bufferHeapAllocInfo);
        allocResult.result == vk::Result::eSuccess) {
        bufferHeapMemory = std::move(allocResult.value);
    } else {
        return util::ErrorMessage{fmt::format("Could not create device memory:{}", vk::to_string(allocResult.result))};
    }

    // Assign the device memory to the bindings
    for (vk::BindBufferMemoryInfo &curBind : bufferHeapBinds) {
        curBind.memory = bufferHeapMemory.get();
    }

    // Now bind them all in one call
    if (const vk::Result bindResult = device.bindBufferMemory2(bufferHeapBinds); bindResult == vk::Result::eSuccess) {
        // Binding memory succeeded
    } else {
        return util::ErrorMessage{fmt::format("Could not bind buffer device-memory:{}", vk::to_string(bindResult))};
    }

    return bufferHeapMemory;
}

} // namespace ymir::gpu::vulkan