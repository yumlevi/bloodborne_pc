/*
 * Copyright (c) 2026 Q2RTX FSR Vulkan contributors
 * SPDX-License-Identifier: MIT
 */

#include "ffx_vk_fsr3_3_1_5_pipeline.h"

#include <string.h>

static FfxVkPortableResult create_static_sampler(
    FfxVkFsr3_3_1_5Pipeline* pipeline, uint32_t index)
{
    VkSamplerCreateInfo samplerInfo = {0};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = strstr(pipeline->bindings[index].name, "Point") ?
        VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    samplerInfo.minFilter = samplerInfo.magFilter;
    samplerInfo.mipmapMode = samplerInfo.magFilter == VK_FILTER_NEAREST ?
        VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    {
        const VkResult result = vkCreateSampler(pipeline->device, &samplerInfo,
                                                pipeline->allocationCallbacks,
                                                &pipeline->samplers[index]);
        if (result != VK_SUCCESS) {
            return FFX_VK_PORTABLE_ERROR_VULKAN;
        }
    }
    return FFX_VK_PORTABLE_OK;
}

static VkDescriptorType descriptor_type(FfxVkFsr3_3_1_5DescriptorClass descriptorClass)
{
    switch (descriptorClass) {
    case FFX_VK_FSR3_3_1_5_DESCRIPTOR_SRV:
        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case FFX_VK_FSR3_3_1_5_DESCRIPTOR_UAV:
        return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    case FFX_VK_FSR3_3_1_5_DESCRIPTOR_BUFFER_SRV:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case FFX_VK_FSR3_3_1_5_DESCRIPTOR_BUFFER_UAV:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case FFX_VK_FSR3_3_1_5_DESCRIPTOR_SAMPLER:
        return VK_DESCRIPTOR_TYPE_SAMPLER;
    case FFX_VK_FSR3_3_1_5_DESCRIPTOR_CONSTANT_BUFFER:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    default:
        return VK_DESCRIPTOR_TYPE_MAX_ENUM;
    }
}

void ffxVkFsr3_3_1_5DestroyPipeline(FfxVkFsr3_3_1_5Pipeline* pipeline)
{
    if (!pipeline)
        return;
    if (pipeline->device != VK_NULL_HANDLE) {
        if (pipeline->pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(pipeline->device, pipeline->pipeline, pipeline->allocationCallbacks);
        if (pipeline->pipelineLayout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(pipeline->device, pipeline->pipelineLayout, pipeline->allocationCallbacks);
        if (pipeline->descriptorSetLayout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(pipeline->device, pipeline->descriptorSetLayout,
                                         pipeline->allocationCallbacks);
        if (pipeline->shaderModule != VK_NULL_HANDLE)
            vkDestroyShaderModule(pipeline->device, pipeline->shaderModule, pipeline->allocationCallbacks);
        for (uint32_t index = 0; index < pipeline->bindingCount; ++index) {
            if (pipeline->samplers[index] != VK_NULL_HANDLE)
                vkDestroySampler(pipeline->device, pipeline->samplers[index], pipeline->allocationCallbacks);
        }
    }
    memset(pipeline, 0, sizeof(*pipeline));
}

FfxVkPortableResult ffxVkFsr3_3_1_5CreatePipeline(
    const FfxVkFsr3_3_1_5PipelineCreateInfo* createInfo,
    FfxVkFsr3_3_1_5Pipeline* outPipeline)
{
    VkDescriptorSetLayoutBinding layoutBindings[FFX_VK_FSR3_3_1_5_MAX_PIPELINE_BINDINGS];
    VkShaderModuleCreateInfo shaderModuleInfo = {0};
    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo = {0};
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {0};
    VkComputePipelineCreateInfo computePipelineInfo = {0};
    char entryPoint[64];
    uint32_t bindingCount = FFX_VK_FSR3_3_1_5_MAX_PIPELINE_BINDINGS;

    if (!createInfo || !outPipeline || !createInfo->spirvWords)
        return FFX_VK_PORTABLE_ERROR_INVALID_POINTER;
    if (createInfo->structSize < sizeof(*createInfo))
        return FFX_VK_PORTABLE_ERROR_INVALID_STRUCT_SIZE;
    if (createInfo->device == VK_NULL_HANDLE || createInfo->spirvWordCount == 0u)
        return FFX_VK_PORTABLE_ERROR_INVALID_ARGUMENT;

    memset(outPipeline, 0, sizeof(*outPipeline));
    if (ffxVkFsr3_3_1_5ReflectSpirv(createInfo->spirvWords, createInfo->spirvWordCount,
                                     outPipeline->bindings, &bindingCount) != FFX_VK_PORTABLE_OK)
        return FFX_VK_PORTABLE_ERROR_INVALID_ARGUMENT;
    if (bindingCount == 0u || bindingCount > FFX_VK_FSR3_3_1_5_MAX_PIPELINE_BINDINGS)
        return FFX_VK_PORTABLE_ERROR_UNSUPPORTED;
    if (ffxVkFsr3_3_1_5ReflectComputeEntryPoint(createInfo->spirvWords,
                                                  createInfo->spirvWordCount,
                                                  entryPoint) != FFX_VK_PORTABLE_OK)
        return FFX_VK_PORTABLE_ERROR_INVALID_ARGUMENT;

    for (uint32_t index = 0; index < bindingCount; ++index) {
        const VkDescriptorType type = descriptor_type(outPipeline->bindings[index].descriptorClass);
        if (type == VK_DESCRIPTOR_TYPE_MAX_ENUM) {
            memset(outPipeline, 0, sizeof(*outPipeline));
            return FFX_VK_PORTABLE_ERROR_UNSUPPORTED;
        }
        layoutBindings[index].binding = outPipeline->bindings[index].binding;
        layoutBindings[index].descriptorType = type;
        layoutBindings[index].descriptorCount = 1u;
        layoutBindings[index].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    outPipeline->device = createInfo->device;
    outPipeline->allocationCallbacks = createInfo->allocationCallbacks;
    outPipeline->bindingCount = bindingCount;

    for (uint32_t index = 0; index < bindingCount; ++index) {
        if (outPipeline->bindings[index].descriptorClass == FFX_VK_FSR3_3_1_5_DESCRIPTOR_SAMPLER &&
            create_static_sampler(outPipeline, index) != FFX_VK_PORTABLE_OK)
            goto fail;
        if (outPipeline->samplers[index] != VK_NULL_HANDLE)
            layoutBindings[index].pImmutableSamplers = &outPipeline->samplers[index];
    }

    shaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderModuleInfo.codeSize = createInfo->spirvWordCount * sizeof(*createInfo->spirvWords);
    shaderModuleInfo.pCode = createInfo->spirvWords;
    {
        const VkResult result = vkCreateShaderModule(outPipeline->device, &shaderModuleInfo,
                                                     outPipeline->allocationCallbacks,
                                                     &outPipeline->shaderModule);
        if (result != VK_SUCCESS) {
            goto fail;
        }
    }

    descriptorSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutInfo.bindingCount = bindingCount;
    descriptorSetLayoutInfo.pBindings = layoutBindings;
    {
        const VkResult result = vkCreateDescriptorSetLayout(outPipeline->device,
                                                             &descriptorSetLayoutInfo,
                                                             outPipeline->allocationCallbacks,
                                                             &outPipeline->descriptorSetLayout);
        if (result != VK_SUCCESS) {
            goto fail;
        }
    }

    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1u;
    pipelineLayoutInfo.pSetLayouts = &outPipeline->descriptorSetLayout;
    {
        const VkResult result = vkCreatePipelineLayout(outPipeline->device, &pipelineLayoutInfo,
                                                        outPipeline->allocationCallbacks,
                                                        &outPipeline->pipelineLayout);
        if (result != VK_SUCCESS) {
            goto fail;
        }
    }

    computePipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    computePipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    computePipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    computePipelineInfo.stage.module = outPipeline->shaderModule;
    computePipelineInfo.stage.pName = entryPoint;
    computePipelineInfo.layout = outPipeline->pipelineLayout;
    {
        const VkResult pipelineResult = vkCreateComputePipelines(
            outPipeline->device, VK_NULL_HANDLE, 1u, &computePipelineInfo,
            outPipeline->allocationCallbacks, &outPipeline->pipeline);
        if (pipelineResult != VK_SUCCESS) {
            goto fail;
        }
    }

    return FFX_VK_PORTABLE_OK;

fail:
    ffxVkFsr3_3_1_5DestroyPipeline(outPipeline);
    return FFX_VK_PORTABLE_ERROR_VULKAN;
}
