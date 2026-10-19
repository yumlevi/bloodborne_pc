/* Copyright (c) 2026 Q2RTX FSR Vulkan contributors SPDX-License-Identifier: MIT */
#include "ffx_vk_fsr3_3_1_5_descriptor.h"

#include <string.h>

static int find_resource(const FfxVkFsr3_3_1_5DescriptorResource* resources,
                         uint32_t resourceCount, const char* name)
{
    int result = -1;
    for (uint32_t index = 0; index < resourceCount; ++index) {
        if (resources[index].name && strcmp(resources[index].name, name) == 0) {
            if (result >= 0)
                return -2;
            result = (int)index;
        }
    }
    return result;
}

void ffxVkFsr3_3_1_5DestroyDescriptorSet(FfxVkFsr3_3_1_5DescriptorSet* descriptorSet)
{
    if (!descriptorSet)
        return;
    if (descriptorSet->device != VK_NULL_HANDLE && descriptorSet->descriptorPool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(descriptorSet->device, descriptorSet->descriptorPool,
                                descriptorSet->allocationCallbacks);
    memset(descriptorSet, 0, sizeof(*descriptorSet));
}

FfxVkPortableResult ffxVkFsr3_3_1_5CreateDescriptorSet(
    const FfxVkFsr3_3_1_5Pipeline* pipeline,
    const FfxVkFsr3_3_1_5DescriptorResource* resources,
    uint32_t resourceCount,
    FfxVkFsr3_3_1_5DescriptorSet* outDescriptorSet)
{
    VkDescriptorPoolSize poolSizes[5] = {0};
    VkDescriptorImageInfo imageInfos[FFX_VK_FSR3_3_1_5_MAX_PIPELINE_BINDINGS] = {0};
    VkDescriptorBufferInfo bufferInfos[FFX_VK_FSR3_3_1_5_MAX_PIPELINE_BINDINGS] = {0};
    VkWriteDescriptorSet writes[FFX_VK_FSR3_3_1_5_MAX_PIPELINE_BINDINGS] = {0};
    VkDescriptorPoolCreateInfo poolInfo = {0};
    VkDescriptorSetAllocateInfo setInfo = {0};
    uint8_t consumed[FFX_VK_FSR3_3_1_5_MAX_PIPELINE_BINDINGS] = {0};
    uint32_t poolCounts[5] = {0};
    uint32_t writeCount = 0;
    uint32_t poolCount = 0;

    if (!pipeline || !resources || !outDescriptorSet)
        return FFX_VK_PORTABLE_ERROR_INVALID_POINTER;
    if (pipeline->device == VK_NULL_HANDLE || pipeline->descriptorSetLayout == VK_NULL_HANDLE ||
        pipeline->bindingCount == 0u || pipeline->bindingCount > FFX_VK_FSR3_3_1_5_MAX_PIPELINE_BINDINGS ||
        resourceCount > FFX_VK_FSR3_3_1_5_MAX_PIPELINE_BINDINGS)
        return FFX_VK_PORTABLE_ERROR_INVALID_ARGUMENT;
    memset(outDescriptorSet, 0, sizeof(*outDescriptorSet));

    for (uint32_t index = 0; index < pipeline->bindingCount; ++index) {
        const FfxVkFsr3_3_1_5DescriptorBinding* binding = &pipeline->bindings[index];
        const int resourceIndex = binding->descriptorClass == FFX_VK_FSR3_3_1_5_DESCRIPTOR_SAMPLER ?
            -1 : find_resource(resources, resourceCount, binding->name);
        if (binding->descriptorClass != FFX_VK_FSR3_3_1_5_DESCRIPTOR_SAMPLER && resourceIndex < 0)
            return FFX_VK_PORTABLE_ERROR_INVALID_ARGUMENT;
        if (resourceIndex >= 0)
            consumed[resourceIndex] = 1u;

        switch (binding->descriptorClass) {
        case FFX_VK_FSR3_3_1_5_DESCRIPTOR_SRV:
            if (resources[resourceIndex].imageView == VK_NULL_HANDLE ||
                resources[resourceIndex].imageLayout == VK_IMAGE_LAYOUT_UNDEFINED)
                return FFX_VK_PORTABLE_ERROR_INVALID_ARGUMENT;
            ++poolCounts[0];
            writes[writeCount].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[writeCount].dstBinding = binding->binding;
            writes[writeCount].descriptorCount = 1u;
            writes[writeCount].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            imageInfos[writeCount].imageView = resources[resourceIndex].imageView;
            imageInfos[writeCount].imageLayout = resources[resourceIndex].imageLayout;
            writes[writeCount].pImageInfo = &imageInfos[writeCount];
            ++writeCount;
            break;
        case FFX_VK_FSR3_3_1_5_DESCRIPTOR_UAV:
            if (resources[resourceIndex].imageView == VK_NULL_HANDLE ||
                resources[resourceIndex].imageLayout == VK_IMAGE_LAYOUT_UNDEFINED)
                return FFX_VK_PORTABLE_ERROR_INVALID_ARGUMENT;
            ++poolCounts[1];
            writes[writeCount].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[writeCount].dstBinding = binding->binding;
            writes[writeCount].descriptorCount = 1u;
            writes[writeCount].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            imageInfos[writeCount].imageView = resources[resourceIndex].imageView;
            imageInfos[writeCount].imageLayout = resources[resourceIndex].imageLayout;
            writes[writeCount].pImageInfo = &imageInfos[writeCount];
            ++writeCount;
            break;
        case FFX_VK_FSR3_3_1_5_DESCRIPTOR_BUFFER_SRV:
        case FFX_VK_FSR3_3_1_5_DESCRIPTOR_BUFFER_UAV:
            if (resources[resourceIndex].buffer == VK_NULL_HANDLE ||
                resources[resourceIndex].bufferRange == 0u)
                return FFX_VK_PORTABLE_ERROR_INVALID_ARGUMENT;
            ++poolCounts[2];
            writes[writeCount].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[writeCount].dstBinding = binding->binding;
            writes[writeCount].descriptorCount = 1u;
            writes[writeCount].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bufferInfos[writeCount].buffer = resources[resourceIndex].buffer;
            bufferInfos[writeCount].offset = resources[resourceIndex].bufferOffset;
            bufferInfos[writeCount].range = resources[resourceIndex].bufferRange;
            writes[writeCount].pBufferInfo = &bufferInfos[writeCount];
            ++writeCount;
            break;
        case FFX_VK_FSR3_3_1_5_DESCRIPTOR_CONSTANT_BUFFER:
            if (resources[resourceIndex].buffer == VK_NULL_HANDLE || resources[resourceIndex].bufferRange == 0u)
                return FFX_VK_PORTABLE_ERROR_INVALID_ARGUMENT;
            ++poolCounts[3];
            writes[writeCount].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[writeCount].dstBinding = binding->binding;
            writes[writeCount].descriptorCount = 1u;
            writes[writeCount].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            bufferInfos[writeCount].buffer = resources[resourceIndex].buffer;
            bufferInfos[writeCount].offset = resources[resourceIndex].bufferOffset;
            bufferInfos[writeCount].range = resources[resourceIndex].bufferRange;
            writes[writeCount].pBufferInfo = &bufferInfos[writeCount];
            ++writeCount;
            break;
        case FFX_VK_FSR3_3_1_5_DESCRIPTOR_SAMPLER:
            ++poolCounts[4];
            break;
        default:
            return FFX_VK_PORTABLE_ERROR_UNSUPPORTED;
        }
    }
    for (uint32_t index = 0; index < resourceCount; ++index) {
        if (!consumed[index])
            return FFX_VK_PORTABLE_ERROR_INVALID_ARGUMENT;
    }
    for (uint32_t index = 0; index < 5u; ++index) {
        static const VkDescriptorType types[] = {
            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            VK_DESCRIPTOR_TYPE_SAMPLER};
        if (poolCounts[index]) {
            poolSizes[poolCount].type = types[index];
            poolSizes[poolCount].descriptorCount = poolCounts[index];
            ++poolCount;
        }
    }

    outDescriptorSet->device = pipeline->device;
    outDescriptorSet->allocationCallbacks = pipeline->allocationCallbacks;
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1u;
    poolInfo.poolSizeCount = poolCount;
    poolInfo.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(pipeline->device, &poolInfo, pipeline->allocationCallbacks,
                               &outDescriptorSet->descriptorPool) != VK_SUCCESS)
        goto fail;
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setInfo.descriptorPool = outDescriptorSet->descriptorPool;
    setInfo.descriptorSetCount = 1u;
    setInfo.pSetLayouts = &pipeline->descriptorSetLayout;
    if (vkAllocateDescriptorSets(pipeline->device, &setInfo, &outDescriptorSet->descriptorSet) != VK_SUCCESS)
        goto fail;
    for (uint32_t index = 0; index < writeCount; ++index)
        writes[index].dstSet = outDescriptorSet->descriptorSet;
    vkUpdateDescriptorSets(pipeline->device, writeCount, writes, 0u, NULL);
    return FFX_VK_PORTABLE_OK;

fail:
    ffxVkFsr3_3_1_5DestroyDescriptorSet(outDescriptorSet);
    return FFX_VK_PORTABLE_ERROR_VULKAN;
}
