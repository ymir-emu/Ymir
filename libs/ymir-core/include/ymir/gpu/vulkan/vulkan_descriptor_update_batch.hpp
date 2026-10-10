#pragma once

/**
@file
@brief Defines `VulkanDescriptorUpdateBatch` for batching descriptor-writes
*/

#include <ymir/gpu/vulkan/vulkan_api.hpp>

#include <ymir/util/result.hpp>

#include <memory>
#include <variant>

namespace ymir::gpu::vulkan {

/// @brief Implements the batching of  descriptor-writes/copies with a fixed amount of space to reduce the overall
/// amount of API calls to `vkUpdateDescriptorSets`
class VulkanDescriptorUpdateBatch {
private:
    const vk::Device m_device;

    const std::size_t m_descriptorWriteMax;
    const std::size_t m_descriptorCopyMax;

    using DescriptorInfoUnion = std::variant<vk::DescriptorImageInfo, vk::DescriptorBufferInfo, vk::BufferView>;

    // Todo: Maybe some kind of hash so that these structures can be re-used
    // among descriptor writes.
    std::unique_ptr<DescriptorInfoUnion[]> m_descriptorInfos;
    std::unique_ptr<vk::WriteDescriptorSet[]> m_descriptorWrites;
    std::unique_ptr<vk::CopyDescriptorSet[]> m_descriptorCopies;

    std::size_t m_descriptorWriteEnd = 0;
    std::size_t m_descriptorCopyEnd = 0;

    VulkanDescriptorUpdateBatch(vk::Device device, std::size_t descriptorWriteMax, std::size_t descriptorCopyMax);

public:
    void Flush();

    void AddImage(vk::DescriptorSet targetDescriptor, uint8 targetBinding, vk::ImageView imageView,
                  vk::ImageLayout imageLayout);
    void AddSampler(vk::DescriptorSet targetDescriptor, uint8 targetBinding, vk::Sampler sampler);

    void AddImageSampler(vk::DescriptorSet targetDescriptor, uint8 targetBinding, vk::ImageView imageView,
                         vk::Sampler sampler, vk::ImageLayout imageLayout);
    void AddBuffer(vk::DescriptorSet targetDescriptor, uint8 targetBinding, vk::Buffer buffer, vk::DeviceSize offset,
                   vk::DeviceSize size = vk::WholeSize);

    void CopyBinding(vk::DescriptorSet sourceDescriptor, vk::DescriptorSet targetDescriptor, uint8 SourceBinding,
                     uint8 targetBinding, uint8 sourceArrayElement = 0, uint8 targetArrayElement = 0,
                     uint8 descriptorCount = 1);

    static util::ValueResult<VulkanDescriptorUpdateBatch>
    Create(vk::Device device, std::size_t descriptorWriteMax = 256, std::size_t descriptorCopyMax = 256);
};

} // namespace ymir::gpu::vulkan