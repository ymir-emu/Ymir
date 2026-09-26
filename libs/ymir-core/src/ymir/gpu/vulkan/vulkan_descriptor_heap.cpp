#include <ymir/gpu/vulkan/vulkan_descriptor_heap.hpp>

#include <algorithm>
#include <unordered_map>

namespace ymir::gpu::vulkan {

VulkanDescriptorHeap::VulkanDescriptorHeap(vk::Device device)
    : m_device(device) {}

util::ValueResult<vk::DescriptorSet> VulkanDescriptorHeap::AllocateDescriptorSet() {
    // Find a free slot
    const auto freeSlot = std::find(m_allocationMap.begin(), m_allocationMap.end(), false);

    // If there is no free slot
    if (freeSlot == m_allocationMap.end()) {
        return util::ErrorMessage{"No free descriptor heap is full"};
    }

    // Mark the slot as allocated
    *freeSlot = true;

    const uint16 index = static_cast<uint16>(std::distance(m_allocationMap.begin(), freeSlot));

    vk::UniqueDescriptorSet &newDescriptorSet = m_descriptorSets[index];

    if (!newDescriptorSet) {
        // Descriptor set doesn't exist yet. Allocate a new one
        const vk::DescriptorSetAllocateInfo allocateInfo = {
            .descriptorPool = m_descriptorPool.get(),
            .descriptorSetCount = 1,
            .pSetLayouts = &m_descriptorSetLayout.get(),
        };

        if (auto allocateResult = m_device.allocateDescriptorSetsUnique(allocateInfo);
            allocateResult.result == vk::Result::eSuccess) {
            newDescriptorSet = std::move(allocateResult.value[0]);
        } else {
            // Error allocating descriptor set
            return util::ErrorMessage{"Error allocating descriptor set: " + vk::to_string(allocateResult.result)};
        }
    }

    return newDescriptorSet.get();
}

bool VulkanDescriptorHeap::FreeDescriptorSet(vk::DescriptorSet Set) {
    // Find the descriptor set
    const auto Found = std::find_if(m_descriptorSets.begin(), m_descriptorSets.end(),
                                    [&Set](const auto &CurSet) -> bool { return CurSet.get() == Set; });

    // If the descriptor set is not found, return
    if (Found == m_descriptorSets.end()) {
        return false;
    }

    // Mark the slot as free
    const uint16 index = static_cast<uint16>(std::distance(m_descriptorSets.begin(), Found));

    m_allocationMap[index] = false;

    return true;
}

util::ValueResult<VulkanDescriptorHeap>
VulkanDescriptorHeap::Create(vk::Device device, std::span<const vk::DescriptorSetLayoutBinding> Bindings,
                             uint16 DescriptorHeapCount) {
    VulkanDescriptorHeap newDescriptorHeap(device);

    // Create a histogram of each of the descriptor types and how many of each
    // the pool should have
    // TODO: maybe keep this around as a hash table to do more dynamic
    // allocations of descriptor sets rather than allocating them all up-front
    std::vector<vk::DescriptorPoolSize> poolSizes;
    {
        std::unordered_map<vk::DescriptorType, uint16> descriptorTypeCounts;

        for (const vk::DescriptorSetLayoutBinding &CurBinding : Bindings) {
            descriptorTypeCounts[CurBinding.descriptorType] += CurBinding.descriptorCount;
        }
        for (const auto &[CurDescriptorType, CurDescriptorTypeCount] : descriptorTypeCounts) {
            poolSizes.push_back(vk::DescriptorPoolSize{
                .type = CurDescriptorType,
                .descriptorCount = static_cast<uint32>(CurDescriptorTypeCount * DescriptorHeapCount)});
        }
    }

    // Create descriptor pool
    {
        const vk::DescriptorPoolCreateInfo poolInfo = {
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets = DescriptorHeapCount,
            .poolSizeCount = static_cast<uint32>(poolSizes.size()),
            .pPoolSizes = poolSizes.data(),
        };

        if (auto createResult = device.createDescriptorPoolUnique(poolInfo);
            createResult.result == vk::Result::eSuccess) {
            newDescriptorHeap.m_descriptorPool = std::move(createResult.value);
        } else {
            return util::ErrorMessage{"Error creating descriptor pool: " + vk::to_string(createResult.result)};
        }
    }

    // Create descriptor set layout
    {
        const vk::DescriptorSetLayoutCreateInfo layoutInfo = {
            .bindingCount = static_cast<uint32>(Bindings.size()),
            .pBindings = Bindings.data(),
        };

        if (auto createResult = device.createDescriptorSetLayoutUnique(layoutInfo);
            createResult.result == vk::Result::eSuccess) {
            newDescriptorHeap.m_descriptorSetLayout = std::move(createResult.value);
        } else {
            // Error creating descriptor set layout
            return util::ErrorMessage{"Error creating descriptor set layout: " + vk::to_string(createResult.result)};
        }
    }

    newDescriptorHeap.m_descriptorSets.resize(DescriptorHeapCount);
    newDescriptorHeap.m_allocationMap.resize(DescriptorHeapCount);

    newDescriptorHeap.m_bindings.assign(Bindings.begin(), Bindings.end());

    return newDescriptorHeap;
}

} // namespace ymir::gpu::vulkan