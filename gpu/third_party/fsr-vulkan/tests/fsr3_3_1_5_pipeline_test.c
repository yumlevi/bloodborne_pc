/*
 * Copyright (c) 2026 Q2RTX FSR Vulkan contributors
 * SPDX-License-Identifier: MIT
 */

#include "ffx_vk_fsr3_3_1_5_bundle.h"
#include "ffx_vk_fsr3_3_1_5_descriptor.h"
#include "ffx_vk_fsr3_3_1_5_pipeline.h"
#include "ffx_vk_vulkan_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int find_compute_queue(VkPhysicalDevice physicalDevice, uint32_t* outFamily)
{
    uint32_t count = 0;
    VkQueueFamilyProperties* properties;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, NULL);
    properties = calloc(count, sizeof(*properties));
    if (!properties)
        return 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, properties);
    for (uint32_t index = 0; index < count; ++index) {
        if (properties[index].queueCount > 0u && (properties[index].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
            *outFamily = index;
            free(properties);
            return 1;
        }
    }
    free(properties);
    return 0;
}

static int choose_device(VkInstance instance, VkPhysicalDevice* outDevice, uint32_t* outFamily)
{
    uint32_t count = 0;
    VkPhysicalDevice* devices;
    if (vkEnumeratePhysicalDevices(instance, &count, NULL) != VK_SUCCESS || count == 0u)
        return 0;
    devices = calloc(count, sizeof(*devices));
    if (!devices)
        return 0;
    if (vkEnumeratePhysicalDevices(instance, &count, devices) != VK_SUCCESS) {
        free(devices);
        return 0;
    }
    for (uint32_t index = 0; index < count; ++index) {
        VkPhysicalDeviceProperties properties;
        uint32_t queueFamily;
        vkGetPhysicalDeviceProperties(devices[index], &properties);
        if (VK_API_VERSION_MAJOR(properties.apiVersion) > 1u ||
            (VK_API_VERSION_MAJOR(properties.apiVersion) == 1u &&
             VK_API_VERSION_MINOR(properties.apiVersion) >= 2u)) {
            if (find_compute_queue(devices[index], &queueFamily)) {
                *outDevice = devices[index];
                *outFamily = queueFamily;
                free(devices);
                return 1;
            }
        }
    }
    free(devices);
    return 0;
}

static int has_device_extension(VkPhysicalDevice physicalDevice, const char* required)
{
    uint32_t count = 0;
    VkExtensionProperties* properties;
    if (vkEnumerateDeviceExtensionProperties(physicalDevice, NULL, &count, NULL) != VK_SUCCESS)
        return 0;
    properties = calloc(count, sizeof(*properties));
    if (!properties)
        return 0;
    if (vkEnumerateDeviceExtensionProperties(physicalDevice, NULL, &count, properties) != VK_SUCCESS) {
        free(properties);
        return 0;
    }
    for (uint32_t index = 0; index < count; ++index) {
        if (strcmp(properties[index].extensionName, required) == 0) {
            free(properties);
            return 1;
        }
    }
    free(properties);
    return 0;
}

static uint32_t find_memory_type(VkPhysicalDevice physicalDevice, uint32_t typeBits,
                                 VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memoryProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
    for (uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index) {
        if ((typeBits & (1u << index)) &&
            (memoryProperties.memoryTypes[index].propertyFlags & properties) == properties)
            return index;
    }
    return UINT32_MAX;
}

static int create_image(VkPhysicalDevice physicalDevice, VkDevice device,
                        VkImage* outImage, VkDeviceMemory* outMemory, VkImageView* outView)
{
    VkImageCreateInfo imageInfo = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    VkMemoryAllocateInfo allocationInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    VkImageViewCreateInfo viewInfo = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    VkMemoryRequirements requirements;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    imageInfo.extent.width = 8u;
    imageInfo.extent.height = 8u;
    imageInfo.extent.depth = 1u;
    imageInfo.mipLevels = 1u;
    imageInfo.arrayLayers = 1u;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &imageInfo, NULL, outImage) != VK_SUCCESS)
        return 0;
    vkGetImageMemoryRequirements(device, *outImage, &requirements);
    allocationInfo.allocationSize = requirements.size;
    allocationInfo.memoryTypeIndex = find_memory_type(physicalDevice, requirements.memoryTypeBits,
                                                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (allocationInfo.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(device, &allocationInfo, NULL, outMemory) != VK_SUCCESS ||
        vkBindImageMemory(device, *outImage, *outMemory, 0u) != VK_SUCCESS)
        return 0;
    viewInfo.image = *outImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = imageInfo.format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1u;
    viewInfo.subresourceRange.layerCount = 1u;
    return vkCreateImageView(device, &viewInfo, NULL, outView) == VK_SUCCESS;
}

static int create_uniform_buffer(VkPhysicalDevice physicalDevice, VkDevice device,
                                 VkBuffer* outBuffer, VkDeviceMemory* outMemory)
{
    VkBufferCreateInfo bufferInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    VkMemoryAllocateInfo allocationInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    VkMemoryRequirements requirements;
    bufferInfo.size = 1024u;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (vkCreateBuffer(device, &bufferInfo, NULL, outBuffer) != VK_SUCCESS)
        return 0;
    vkGetBufferMemoryRequirements(device, *outBuffer, &requirements);
    allocationInfo.allocationSize = requirements.size;
    allocationInfo.memoryTypeIndex = find_memory_type(physicalDevice, requirements.memoryTypeBits,
                                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    return allocationInfo.memoryTypeIndex != UINT32_MAX &&
           vkAllocateMemory(device, &allocationInfo, NULL, outMemory) == VK_SUCCESS &&
           vkBindBufferMemory(device, *outBuffer, *outMemory, 0u) == VK_SUCCESS;
}

int main(void)
{
    VkApplicationInfo applicationInfo = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    VkInstanceCreateInfo instanceInfo = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkDeviceMemory imageMemory = VK_NULL_HANDLE;
    VkBuffer uniformBuffer = VK_NULL_HANDLE;
    VkDeviceMemory uniformMemory = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    VkDeviceCreateInfo deviceInfo = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR derivativeFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR};
    VkPhysicalDeviceFeatures2 enabledFeatures = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    const char* enabledExtensions[1];
    int result = 1;

    applicationInfo.apiVersion = VK_API_VERSION_1_2;
    instanceInfo.pApplicationInfo = &applicationInfo;
    if (vkCreateInstance(&instanceInfo, NULL, &instance) != VK_SUCCESS) {
        puts("SKIP: cannot create a Vulkan 1.2 instance");
        return 77;
    }
    if (!choose_device(instance, &physicalDevice, &queueFamily)) {
        puts("SKIP: no Vulkan 1.2 compute device");
        result = 77;
        goto cleanup;
    }
    queueInfo.queueFamilyIndex = queueFamily;
    queueInfo.queueCount = 1u;
    queueInfo.pQueuePriorities = &priority;
    vkGetPhysicalDeviceFeatures2(physicalDevice, &enabledFeatures);
    if (has_device_extension(physicalDevice, VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME)) {
        VkPhysicalDeviceFeatures2 supportedFeatures = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR supportedDerivatives = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR};
        supportedFeatures.pNext = &supportedDerivatives;
        vkGetPhysicalDeviceFeatures2(physicalDevice, &supportedFeatures);
        if (supportedDerivatives.computeDerivativeGroupLinear) {
            derivativeFeatures.computeDerivativeGroupLinear = VK_TRUE;
            enabledFeatures.pNext = &derivativeFeatures;
            enabledExtensions[deviceInfo.enabledExtensionCount++] =
                VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME;
        }
    }
    deviceInfo.queueCreateInfoCount = 1u;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.pNext = &enabledFeatures;
    deviceInfo.enabledExtensionCount = deviceInfo.enabledExtensionCount;
    deviceInfo.ppEnabledExtensionNames = enabledExtensions;
    if (vkCreateDevice(physicalDevice, &deviceInfo, NULL, &device) != VK_SUCCESS) {
        puts("SKIP: cannot create a Vulkan compute device");
        result = 77;
        goto cleanup;
    }
    if (!create_image(physicalDevice, device, &image, &imageMemory, &imageView) ||
        !create_uniform_buffer(physicalDevice, device, &uniformBuffer, &uniformMemory))
        goto cleanup;

    for (uint32_t pass = 0; pass <= 10u; ++pass) {
        FfxVkFsr3_3_1_5Pipeline pipeline;
        FfxVkFsr3_3_1_5DescriptorSet descriptorSet;
        FfxVkFsr3_3_1_5DescriptorResource resources[FFX_VK_FSR3_3_1_5_MAX_PIPELINE_BINDINGS];
        uint32_t resourceCount = 0u;
        FfxVkFsr3_3_1_5PipelineCreateInfo pipelineInfo;
        size_t wordCount = 0;
        const uint32_t* words;
        const uint32_t permutation = pass == 7u ? 39u : 7u;
        if (ffxVkFsr3_3_1_5GetEmbeddedModule(pass, permutation, &words, &wordCount) != FFX_VK_PORTABLE_OK) {
            goto cleanup;
        }
        memset(&pipelineInfo, 0, sizeof(pipelineInfo));
        pipelineInfo.structSize = sizeof(pipelineInfo);
        pipelineInfo.device = device;
        pipelineInfo.spirvWords = words;
        pipelineInfo.spirvWordCount = wordCount;
        memset(&pipeline, 0, sizeof(pipeline));
        if (ffxVkFsr3_3_1_5CreatePipeline(&pipelineInfo, &pipeline) != FFX_VK_PORTABLE_OK ||
            pipeline.pipeline == VK_NULL_HANDLE || pipeline.bindingCount == 0u) {
            ffxVkFsr3_3_1_5DestroyPipeline(&pipeline);
            goto cleanup;
        }
        for (uint32_t binding = 0; binding < pipeline.bindingCount; ++binding) {
            if (pipeline.bindings[binding].descriptorClass == FFX_VK_FSR3_3_1_5_DESCRIPTOR_SAMPLER)
                continue;
            resources[resourceCount].name = pipeline.bindings[binding].name;
            if (pipeline.bindings[binding].descriptorClass == FFX_VK_FSR3_3_1_5_DESCRIPTOR_CONSTANT_BUFFER) {
                resources[resourceCount].buffer = uniformBuffer;
                resources[resourceCount].bufferRange = 1024u;
            } else {
                resources[resourceCount].imageView = imageView;
                resources[resourceCount].imageLayout =
                    pipeline.bindings[binding].descriptorClass == FFX_VK_FSR3_3_1_5_DESCRIPTOR_UAV ?
                        VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
            ++resourceCount;
        }
        if (resourceCount == 0u ||
            ffxVkFsr3_3_1_5CreateDescriptorSet(&pipeline, resources, resourceCount - 1u,
                                                &descriptorSet) != FFX_VK_PORTABLE_ERROR_INVALID_ARGUMENT) {
            ffxVkFsr3_3_1_5DestroyPipeline(&pipeline);
            goto cleanup;
        }
        resources[resourceCount] = resources[0];
        if (ffxVkFsr3_3_1_5CreateDescriptorSet(&pipeline, resources, resourceCount + 1u,
                                                &descriptorSet) != FFX_VK_PORTABLE_ERROR_INVALID_ARGUMENT) {
            ffxVkFsr3_3_1_5DestroyPipeline(&pipeline);
            goto cleanup;
        }
        if (ffxVkFsr3_3_1_5CreateDescriptorSet(&pipeline, resources, resourceCount,
                                                &descriptorSet) != FFX_VK_PORTABLE_OK ||
            descriptorSet.descriptorSet == VK_NULL_HANDLE) {
            ffxVkFsr3_3_1_5DestroyDescriptorSet(&descriptorSet);
            ffxVkFsr3_3_1_5DestroyPipeline(&pipeline);
            goto cleanup;
        }
        ffxVkFsr3_3_1_5DestroyDescriptorSet(&descriptorSet);
        ffxVkFsr3_3_1_5DestroyPipeline(&pipeline);
    }
    result = 0;
    puts("FSR3.1.5 Vulkan pipeline test passed (11/11 modules)");

cleanup:
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
        if (uniformBuffer != VK_NULL_HANDLE)
            vkDestroyBuffer(device, uniformBuffer, NULL);
        if (uniformMemory != VK_NULL_HANDLE)
            vkFreeMemory(device, uniformMemory, NULL);
        if (imageView != VK_NULL_HANDLE)
            vkDestroyImageView(device, imageView, NULL);
        if (image != VK_NULL_HANDLE)
            vkDestroyImage(device, image, NULL);
        if (imageMemory != VK_NULL_HANDLE)
            vkFreeMemory(device, imageMemory, NULL);
        vkDestroyDevice(device, NULL);
    }
    vkDestroyInstance(instance, NULL);
    return result;
}
