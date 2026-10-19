/*
 * Copyright (c) 2026 Q2RTX FSR Vulkan contributors
 * SPDX-License-Identifier: MIT
 */

#include "ffx_vk_fsr4_v07.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "check failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
        result = 1; \
        goto cleanup; \
    } \
} while (0)

static int choose_compute_device(VkInstance instance, VkPhysicalDevice *physical,
                                 uint32_t *queue_family)
{
    uint32_t count = 0;
    VkPhysicalDevice *devices = NULL;
    int found = 0;
    if (vkEnumeratePhysicalDevices(instance, &count, NULL) != VK_SUCCESS || !count)
        return 0;
    devices = calloc(count, sizeof(*devices));
    if (!devices || vkEnumeratePhysicalDevices(instance, &count, devices) != VK_SUCCESS)
        goto done;
    for (uint32_t d = 0; d < count && !found; ++d) {
        uint32_t queue_count = 0;
        VkQueueFamilyProperties *queues = NULL;
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(devices[d], &properties);
        if (VK_API_VERSION_MAJOR(properties.apiVersion) < 1 ||
            (VK_API_VERSION_MAJOR(properties.apiVersion) == 1 &&
             VK_API_VERSION_MINOR(properties.apiVersion) < 3))
            continue;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &queue_count, NULL);
        queues = calloc(queue_count, sizeof(*queues));
        if (!queues)
            continue;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &queue_count, queues);
        for (uint32_t q = 0; q < queue_count; ++q) {
            if (queues[q].queueCount &&
                (queues[q].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
                *physical = devices[d];
                *queue_family = q;
                found = 1;
                break;
            }
        }
        free(queues);
    }
done:
    free(devices);
    return found;
}

static uint32_t find_memory_type(VkPhysicalDevice physical, uint32_t type_bits,
                                 VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memory;
    vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    for (uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
        if ((type_bits & (1u << i)) &&
            (memory.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }
    return UINT32_MAX;
}

int main(void)
{
    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkImage external_image = VK_NULL_HANDLE;
    VkImageView external_view = VK_NULL_HANDLE;
    VkDeviceMemory external_memory = VK_NULL_HANDLE;
    uint32_t queue_family = UINT32_MAX;
    FfxInterface backend;
    void *scratch = NULL;
    int result = 0;

    memset(&backend, 0, sizeof(backend));
    VkApplicationInfo application = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .apiVersion = VK_API_VERSION_1_3,
    };
    VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &application,
    };
    if (vkCreateInstance(&instance_info, NULL, &instance) != VK_SUCCESS ||
        !choose_compute_device(instance, &physical, &queue_family)) {
        result = 77;
        goto cleanup;
    }

    const float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = queue_family,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority,
    };
    VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
    };
    if (vkCreateDevice(physical, &device_info, NULL, &device) != VK_SUCCESS) {
        result = 77;
        goto cleanup;
    }
    vkGetDeviceQueue(device, queue_family, 0u, &queue);

    scratch = calloc(1, ffxFsr4VkGetScratchMemorySize());
    CHECK(scratch);
    FfxFsr4VkCreateInfo create_info = {
        .device = device,
        .physicalDevice = physical,
        .scratchBuffer = scratch,
        .scratchBufferSize = ffxFsr4VkGetScratchMemorySize(),
    };
    const VkResult context_result = ffxFsr4VkCreateContext(&create_info, &backend);
    if (context_result == VK_ERROR_FEATURE_NOT_PRESENT ||
        context_result == VK_ERROR_FORMAT_NOT_SUPPORTED) {
        result = 77;
        goto cleanup;
    }
    CHECK(context_result == VK_SUCCESS);

    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queue_family,
    };
    CHECK(vkCreateCommandPool(device, &pool_info, NULL, &command_pool) == VK_SUCCESS);
    VkCommandBufferAllocateInfo command_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    CHECK(vkAllocateCommandBuffers(device, &command_info, &command_buffer) == VK_SUCCESS);

    /* Imported FSR4 images are no longer implicitly assumed to be GENERAL.
     * Give the backend a real shader-read image, let it transition through its
     * compute ABI, then restore shader-read layout in the same command buffer. */
    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R16G16B16A16_SFLOAT,
        .extent = {1u, 1u, 1u},
        .mipLevels = 1u,
        .arrayLayers = 1u,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    CHECK(vkCreateImage(device, &image_info, NULL, &external_image) == VK_SUCCESS);
    VkMemoryRequirements image_requirements;
    vkGetImageMemoryRequirements(device, external_image, &image_requirements);
    uint32_t memory_type = find_memory_type(physical,
        image_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    CHECK(memory_type != UINT32_MAX);
    VkMemoryAllocateInfo allocation_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = image_requirements.size,
        .memoryTypeIndex = memory_type,
    };
    CHECK(vkAllocateMemory(device, &allocation_info, NULL, &external_memory) == VK_SUCCESS);
    CHECK(vkBindImageMemory(device, external_image, external_memory, 0u) == VK_SUCCESS);
    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = external_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = image_info.format,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u},
    };
    CHECK(vkCreateImageView(device, &view_info, NULL, &external_view) == VK_SUCCESS);

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    CHECK(vkBeginCommandBuffer(command_buffer, &begin_info) == VK_SUCCESS);
    VkImageMemoryBarrier initialize_barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = external_image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u},
    };
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u, 0u, NULL, 0u, NULL, 1u,
        &initialize_barrier);
    CHECK(vkEndCommandBuffer(command_buffer) == VK_SUCCESS);
    VkSubmitInfo initialize_submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1u,
        .pCommandBuffers = &command_buffer,
    };
    CHECK(vkQueueSubmit(queue, 1u, &initialize_submit, VK_NULL_HANDLE) == VK_SUCCESS);
    CHECK(vkQueueWaitIdle(queue) == VK_SUCCESS);
    CHECK(vkResetCommandBuffer(command_buffer, 0u) == VK_SUCCESS);

    FfxFsr4VkExternalImageState external_state = {
        .structSize = sizeof(external_state),
        .image = external_image,
        .view = external_view,
        .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .stageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        .accessMask = VK_ACCESS_SHADER_READ_BIT,
        .restoreLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .restoreStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        .restoreAccessMask = VK_ACCESS_SHADER_READ_BIT,
    };
    FfxFsr4VkExternalImageState bad_state = external_state;
    bad_state.structSize = 0u;
    CHECK(ffxFsr4VkSetExternalImageState(&backend, &bad_state) ==
          VK_ERROR_VALIDATION_FAILED_EXT);
    CHECK(ffxFsr4VkSetExternalImageState(&backend, &external_state) == VK_SUCCESS);
    FfxApiResource external_resource = {
        .resource = (void*)external_view,
        .description = {
            .type = FFX_RESOURCE_TYPE_TEXTURE2D,
            .format = FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT,
            .width = 1u,
            .height = 1u,
            .depth = 1u,
            .mipCount = 1u,
        },
        .state = FFX_API_RESOURCE_STATE_COMPUTE_READ,
    };
    FfxResourceInternal external_handle = {UINT32_MAX};
    CHECK(backend.fpRegisterResource(&backend, &external_resource, 0u,
                                     &external_handle) == FFX_OK);
    CHECK(vkBeginCommandBuffer(command_buffer, &begin_info) == VK_SUCCESS);
    CHECK(ffxFsr4VkBeginFrame(&backend, 1u) == VK_SUCCESS);
    CHECK(backend.fpExecuteGpuJobs(&backend, (FfxCommandList)command_buffer, 0u) == FFX_OK);
    CHECK(backend.fpUnregisterResources(&backend, (FfxCommandList)command_buffer, 0u) == FFX_OK);
    CHECK(vkEndCommandBuffer(command_buffer) == VK_SUCCESS);
    CHECK(vkQueueSubmit(queue, 1u, &initialize_submit, VK_NULL_HANDLE) == VK_SUCCESS);
    CHECK(vkQueueWaitIdle(queue) == VK_SUCCESS);
    CHECK(ffxFsr4VkRetireFrame(&backend, 1u) == VK_SUCCESS);
    CHECK(vkResetCommandBuffer(command_buffer, 0u) == VK_SUCCESS);

    /* Three unretired records are supported. A fourth must not recycle a
     * descriptor/constant partition without the host's completion signal. */
    for (uint64_t frame_id = 1; frame_id <= 3; ++frame_id) {
        VkCommandBufferBeginInfo begin_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        };
        CHECK(vkBeginCommandBuffer(command_buffer, &begin_info) == VK_SUCCESS);
        CHECK(ffxFsr4VkBeginFrame(&backend, frame_id) == VK_SUCCESS);
        CHECK(backend.fpExecuteGpuJobs(&backend, (FfxCommandList)command_buffer, 0) == FFX_OK);
        CHECK(vkEndCommandBuffer(command_buffer) == VK_SUCCESS);
        CHECK(vkResetCommandBuffer(command_buffer, 0) == VK_SUCCESS);
    }
    CHECK(ffxFsr4VkBeginFrame(&backend, 4) == VK_NOT_READY);
    CHECK(ffxFsr4VkRetireFrame(&backend, 1) == VK_SUCCESS);
    CHECK(ffxFsr4VkBeginFrame(&backend, 4) == VK_SUCCESS);
    CHECK(ffxFsr4VkRetireFrame(&backend, 4) == VK_SUCCESS);

cleanup:
    if (backend.device)
        ffxFsr4VkDestroyContext((FfxFsr4VkContext *)backend.device);
    if (external_view)
        vkDestroyImageView(device, external_view, NULL);
    if (external_image)
        vkDestroyImage(device, external_image, NULL);
    if (external_memory)
        vkFreeMemory(device, external_memory, NULL);
    if (command_pool)
        vkDestroyCommandPool(device, command_pool, NULL);
    if (device)
        vkDestroyDevice(device, NULL);
    if (instance)
        vkDestroyInstance(instance, NULL);
    free(scratch);
    return result;
}
