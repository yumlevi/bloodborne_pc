/* Copyright (c) 2026 Q2RTX FSR Vulkan contributors. SPDX-License-Identifier: MIT */

#include "ffx_vk_fsr3_3_1_5_pipeline.h"
#include "ffx_vk_fsr3_3_1_6_framegen_embedded_spirv.h"

#include <cstdio>
#include <vector>

int main()
{
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo = &app;
    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&instanceInfo, nullptr, &instance) != VK_SUCCESS) {
        std::puts("SKIP: cannot create Vulkan 1.2 instance");
        return 77;
    }
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    uint32_t queueFamily = UINT32_MAX;
    for (VkPhysicalDevice candidate : devices) {
        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
        for (uint32_t family = 0; family < familyCount; ++family) {
            if (families[family].queueCount && (families[family].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
                physical = candidate;
                queueFamily = family;
                break;
            }
        }
        if (physical)
            break;
    }
    if (!physical) {
        std::puts("SKIP: no Vulkan compute device");
        vkDestroyInstance(instance, nullptr);
        return 77;
    }
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    VkDevice device = VK_NULL_HANDLE;
    if (vkCreateDevice(physical, &deviceInfo, nullptr, &device) != VK_SUCCESS) {
        std::puts("SKIP: cannot create Vulkan compute device");
        vkDestroyInstance(instance, nullptr);
        return 77;
    }

    bool ok = true;
    for (uint32_t index = 0; index < FFX_VK_FSR3_3_1_6_FRAMEGEN_EMBEDDED_SPIRV_COUNT; ++index) {
        const auto& module = FFX_VK_FSR3_3_1_6_FRAMEGEN_EMBEDDED_SPIRV[index];
        FfxVkFsr3_3_1_5PipelineCreateInfo info{};
        info.structSize = sizeof(info);
        info.device = device;
        info.spirvWords = module.words;
        info.spirvWordCount = module.wordCount;
        FfxVkFsr3_3_1_5Pipeline pipeline{};
        if (ffxVkFsr3_3_1_5CreatePipeline(&info, &pipeline) != FFX_VK_PORTABLE_OK ||
            pipeline.pipeline == VK_NULL_HANDLE || pipeline.bindingCount == 0u) {
            std::fprintf(stderr, "pipeline creation failed for %s\n", module.filename);
            ok = false;
        }
        ffxVkFsr3_3_1_5DestroyPipeline(&pipeline);
        if (!ok)
            break;
    }
    vkDeviceWaitIdle(device);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    if (ok)
        std::printf("FSR3.1.6 FI/OF Vulkan pipeline test passed (%u/%u modules)\n",
                    FFX_VK_FSR3_3_1_6_FRAMEGEN_EMBEDDED_SPIRV_COUNT,
                    FFX_VK_FSR3_3_1_6_FRAMEGEN_EMBEDDED_SPIRV_COUNT);
    return ok ? 0 : 1;
}
