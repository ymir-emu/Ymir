#pragma once

/**
@file
@brief Defines `VulkanSwapChain`, a wrapper for `vk::SwapchainKHR`
*/

#include <ymir/gpu/vulkan/vulkan_api.hpp>

#include <ymir/util/result.hpp>

#include <mutex>
#include <span>
#include <vector>

namespace ymir::gpu::vulkan {

/// @brief Manages an `vk::DescriptorPool` and its `vk::DescriptorSets`. Implements a basic heap of descriptor sets
/// given a layout of bindings. Create a descriptor set by providing a list of bindings and it will automatically create
/// both the pool, layout, and maintain a heap of descriptor-sets. Descriptor sets will be reused and recycled. Assume
/// that newly allocated descriptor sets are in an undefined state.
class VulkanDescriptorHeap final {
private:
    explicit VulkanDescriptorHeap(vk::Device device);

    const vk::Device m_device;

    vk::UniqueDescriptorPool m_descriptorPool;
    vk::UniqueDescriptorSetLayout m_descriptorSetLayout;
    std::vector<vk::UniqueDescriptorSet> m_descriptorSets;

    std::vector<vk::DescriptorSetLayoutBinding> m_bindings;

    std::vector<bool> m_allocationMap;

public:
    VulkanDescriptorHeap(VulkanDescriptorHeap &&) = default;
    ~VulkanDescriptorHeap() = default;

    [[nodiscard]] const vk::DescriptorPool &GetDescriptorPool() const {
        return m_descriptorPool.get();
    };

    [[nodiscard]] const vk::DescriptorSetLayout &GetDescriptorSetLayout() const {
        return m_descriptorSetLayout.get();
    };

    [[nodiscard]] const std::span<const vk::UniqueDescriptorSet> GetDescriptorSets() const {
        return m_descriptorSets;
    };

    std::span<const vk::DescriptorSetLayoutBinding> GetBindings() const {
        return m_bindings;
    };

    util::ValueResult<vk::DescriptorSet> AllocateDescriptorSet();
    bool FreeDescriptorSet(vk::DescriptorSet Set);

    static util::ValueResult<VulkanDescriptorHeap> Create(vk::Device device,
                                                          std::span<const vk::DescriptorSetLayoutBinding> bindings,
                                                          uint16 descriptorHeapCount = 1024);
};
} // namespace ymir::gpu::vulkan