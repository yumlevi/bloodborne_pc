/*
 * Copyright (c) 2026 Q2RTX FSR Vulkan contributors
 * SPDX-License-Identifier: MIT
 */

#include "ffx_vk_portable.h"

#include <stdio.h>
#include <stdlib.h>

static const char* yes_no(VkBool32 value)
{
    return value ? "yes" : "no";
}

int main(void)
{
    const VkApplicationInfo applicationInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "ffx_vk_capability_probe",
        .applicationVersion = 1,
        .pEngineName = "ffx-vulkan",
        .engineVersion = 1,
        .apiVersion = VK_API_VERSION_1_2
    };
    const VkInstanceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &applicationInfo
    };
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice* devices = NULL;
    uint32_t deviceCount = 0;
    int exitCode = EXIT_FAILURE;

    VkResult result = vkCreateInstance(&createInfo, NULL, &instance);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "vkCreateInstance failed: %d\n", result);
        return EXIT_FAILURE;
    }

    result = vkEnumeratePhysicalDevices(instance, &deviceCount, NULL);
    if (result != VK_SUCCESS || deviceCount == 0) {
        fprintf(stderr, "no Vulkan physical devices found\n");
        goto cleanup;
    }

    devices = (VkPhysicalDevice*)calloc(deviceCount, sizeof(*devices));
    if (devices == NULL)
        goto cleanup;
    result = vkEnumeratePhysicalDevices(instance, &deviceCount, devices);
    if (result != VK_SUCCESS)
        goto cleanup;

    for (uint32_t index = 0; index < deviceCount; ++index) {
        VkPhysicalDeviceProperties properties;
        FfxVkPortableDeviceCapabilities capabilities = {
            .structSize = sizeof(capabilities)
        };
        vkGetPhysicalDeviceProperties(devices[index], &properties);
        if (ffxVkPortableQueryDeviceCapabilities(devices[index], &capabilities) != FFX_VK_PORTABLE_OK) {
            fprintf(stderr, "capability query failed for device %u\n", index);
            goto cleanup;
        }

        printf("%s (%04x:%04x)\n", properties.deviceName, capabilities.vendorId, capabilities.deviceId);
        printf("  Vulkan %u.%u.%u, subgroup %u [%u, %u]\n",
            VK_API_VERSION_MAJOR(capabilities.apiVersion),
            VK_API_VERSION_MINOR(capabilities.apiVersion),
            VK_API_VERSION_PATCH(capabilities.apiVersion),
            capabilities.subgroupSize,
            capabilities.minSubgroupSize,
            capabilities.maxSubgroupSize);
        printf("  FSR3 compute prerequisites:          %s\n", yes_no(capabilities.fsr3ComputePrerequisites));
        printf("  FSR3 frame-generation prerequisites: %s\n", yes_no(capabilities.fsr3FrameGenerationPrerequisites));
        printf("  FSR4 INT8 prerequisites:             %s\n", yes_no(capabilities.fsr4Int8Prerequisites));
        printf("  fp16=%s int8=%s signed-int8-dot=%s timeline=%s sync2=%s\n",
            yes_no(capabilities.shaderFloat16),
            yes_no(capabilities.shaderInt8),
            yes_no(capabilities.acceleratedSignedInt8DotProduct),
            yes_no(capabilities.timelineSemaphore),
            yes_no(capabilities.synchronization2));
    }

    exitCode = EXIT_SUCCESS;

cleanup:
    free(devices);
    if (instance != VK_NULL_HANDLE)
        vkDestroyInstance(instance, NULL);
    return exitCode;
}

