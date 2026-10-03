#include <ymir/gpu/vulkan/vulkan_descriptor_update_batch.hpp>

#include <memory>
#include <span>

namespace ymir::gpu::vulkan {

VulkanDescriptorUpdateBatch::VulkanDescriptorUpdateBatch(vk::Device device, std::size_t descriptorWriteMax,
                                                         std::size_t descriptorCopyMax)
    : m_device(device)
    , m_descriptorWriteMax(descriptorWriteMax)
    , m_descriptorCopyMax(descriptorCopyMax) {}

void VulkanDescriptorUpdateBatch::Flush() {

    m_device.updateDescriptorSets({std::span(m_descriptorWrites.get(), m_descriptorWriteEnd)},
                                  {std::span(m_descriptorCopies.get(), m_descriptorCopyEnd)});

    m_descriptorWriteEnd = 0;
    m_descriptorCopyEnd = 0;
}

void VulkanDescriptorUpdateBatch::AddImage(vk::DescriptorSet targetDescriptor, uint8 targetBinding,
                                           vk::ImageView imageView, vk::ImageLayout imageLayout) {

    if (m_descriptorWriteEnd >= m_descriptorWriteMax) {
        Flush();
    }

    const auto &imageInfo =
        m_descriptorInfos[m_descriptorWriteEnd].emplace<vk::DescriptorImageInfo>(vk::DescriptorImageInfo{
            .sampler = vk::Sampler{},
            .imageView = imageView,
            .imageLayout = imageLayout,
        });

    m_descriptorWrites[m_descriptorWriteEnd] = vk::WriteDescriptorSet{
        .dstSet = targetDescriptor,
        .dstBinding = targetBinding,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eSampledImage,
        .pImageInfo = &imageInfo,
    };

    ++m_descriptorWriteEnd;
}

void VulkanDescriptorUpdateBatch::AddSampler(vk::DescriptorSet targetDescriptor, uint8 targetBinding,
                                             vk::Sampler sampler) {

    if (m_descriptorWriteEnd >= m_descriptorWriteMax) {
        Flush();
    }

    const auto &imageInfo =
        m_descriptorInfos[m_descriptorWriteEnd].emplace<vk::DescriptorImageInfo>(vk::DescriptorImageInfo{
            .sampler = sampler,
            .imageView = vk::ImageView{},
            .imageLayout = vk::ImageLayout{},
        });

    m_descriptorWrites[m_descriptorWriteEnd] = vk::WriteDescriptorSet{
        .dstSet = targetDescriptor,
        .dstBinding = targetBinding,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eSampler,
        .pImageInfo = &imageInfo,
    };

    ++m_descriptorWriteEnd;
}

void VulkanDescriptorUpdateBatch::AddImageSampler(vk::DescriptorSet targetDescriptor, uint8 targetBinding,
                                                  vk::ImageView imageView, vk::Sampler sampler,
                                                  vk::ImageLayout imageLayout) {

    if (m_descriptorWriteEnd >= m_descriptorWriteMax) {
        Flush();
    }

    const auto &imageInfo = m_descriptorInfos[m_descriptorWriteEnd].emplace<vk::DescriptorImageInfo>(
        vk::DescriptorImageInfo{sampler, imageView, imageLayout});

    m_descriptorWrites[m_descriptorWriteEnd] = vk::WriteDescriptorSet{
        .dstSet = targetDescriptor,
        .dstBinding = targetBinding,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .pImageInfo = &imageInfo,
    };

    ++m_descriptorWriteEnd;
}

void VulkanDescriptorUpdateBatch::AddBuffer(vk::DescriptorSet targetDescriptor, uint8 targetBinding, vk::Buffer buffer,
                                            vk::DeviceSize offset, vk::DeviceSize size) {

    if (m_descriptorWriteEnd >= m_descriptorWriteMax) {
        Flush();
    }

    const auto &bufferInfo =
        m_descriptorInfos[m_descriptorWriteEnd].emplace<vk::DescriptorBufferInfo>(vk::DescriptorBufferInfo{
            .buffer = buffer,
            .offset = offset,
            .range = size,
        });

    m_descriptorWrites[m_descriptorWriteEnd] = vk::WriteDescriptorSet{
        .dstSet = targetDescriptor,
        .dstBinding = targetBinding,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eStorageBuffer,
        .pBufferInfo = &bufferInfo,
    };

    ++m_descriptorWriteEnd;
}

void VulkanDescriptorUpdateBatch::CopyBinding(vk::DescriptorSet sourceDescriptor, vk::DescriptorSet targetDescriptor,
                                              uint8 SourceBinding, uint8 targetBinding, uint8 sourceArrayElement,
                                              uint8 targetArrayElement, uint8 descriptorCount) {

    if (m_descriptorCopyEnd >= m_descriptorCopyMax) {
        Flush();
    }

    m_descriptorCopies[m_descriptorCopyEnd] = vk::CopyDescriptorSet{
        .srcSet = sourceDescriptor,
        .srcBinding = SourceBinding,
        .srcArrayElement = sourceArrayElement,
        .dstSet = targetDescriptor,
        .dstBinding = targetBinding,
        .dstArrayElement = targetArrayElement,
        .descriptorCount = descriptorCount,
    };

    ++m_descriptorCopyEnd;
}

util::ValueResult<VulkanDescriptorUpdateBatch>
VulkanDescriptorUpdateBatch::Create(vk::Device device, std::size_t descriptorWriteMax, std::size_t descriptorCopyMax)

{
    VulkanDescriptorUpdateBatch newDescriptorUpdateBatch(device, descriptorWriteMax, descriptorCopyMax);

    newDescriptorUpdateBatch.m_descriptorInfos = std::make_unique<DescriptorInfoUnion[]>(descriptorWriteMax);
    newDescriptorUpdateBatch.m_descriptorWrites = std::make_unique<vk::WriteDescriptorSet[]>(descriptorWriteMax);
    newDescriptorUpdateBatch.m_descriptorCopies = std::make_unique<vk::CopyDescriptorSet[]>(descriptorCopyMax);

    return newDescriptorUpdateBatch;
}

} // namespace ymir::gpu::vulkan