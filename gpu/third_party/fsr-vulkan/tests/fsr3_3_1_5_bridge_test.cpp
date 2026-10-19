/*
 * Copyright (c) 2026 Q2RTX FSR Vulkan contributors
 * SPDX-License-Identifier: MIT
 */

#include "ffx_vk_fsr3_3_1_5_bridge.h"
#include "ffx_vk_vulkan_compat.h"

#include "ffx_interface.h"
#include "ffx_fsr3upscaler.h"
#include "ffx_frameinterpolation.h"
#include "ffx_opticalflow.h"
#include "ffx_vk_fsr3_3_1_6_framegen_embedded_spirv.h"

#include <cwchar>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

extern "C" FfxErrorCode ffxVkFsr3_3_1_5BridgeCreatePipeline(
    FfxInterface*, FfxShaderBlob*, const FfxPipelineDescription*, FfxUInt32,
    FfxPipelineState*);
extern "C" FfxErrorCode ffxVkFsr3_3_1_5BridgeDestroyPipeline(
    FfxInterface*, FfxPipelineState*, FfxUInt32);
extern "C" FfxErrorCode ffxVkFsr3_3_1_5BridgeCreateResource(
    FfxInterface*, const FfxCreateResourceDescription*, FfxUInt32, FfxResourceInternal*);
extern "C" FfxErrorCode ffxVkFsr3_3_1_5BridgeDestroyResource(
    FfxInterface*, FfxResourceInternal, FfxUInt32);
extern "C" FfxApiResource ffxVkFsr3_3_1_5BridgeGetResource(
    FfxInterface*, FfxResourceInternal);
extern "C" FfxApiResourceDescription ffxVkFsr3_3_1_5BridgeGetResourceDescription(
    FfxInterface*, FfxResourceInternal);
extern "C" FfxApiResource ffxVkFsr3_3_1_5BridgeResolveResource(
    FfxVkFsr3_3_1_5Bridge*, FfxVkFsr3_3_1_5Resource);
extern "C" FfxErrorCode ffxVkFsr3_3_1_5BridgeRegisterResource(
    FfxInterface*, const FfxApiResource*, FfxUInt32, FfxResourceInternal*);
extern "C" FfxErrorCode ffxVkFsr3_3_1_5BridgeUnregisterResources(
    FfxInterface*, FfxCommandList, FfxUInt32);
extern "C" FfxErrorCode ffxVkFsr3_3_1_5BridgeScheduleGpuJob(
    FfxInterface*, const FfxGpuJobDescription*);
extern "C" FfxErrorCode ffxVkFsr3_3_1_5BridgeExecuteGpuJobs(
    FfxInterface*, FfxCommandList, FfxUInt32);
extern "C" FfxVersionNumber ffxVkFsr3_3_1_5BridgeGetSDKVersion(FfxInterface*);
extern "C" FfxErrorCode ffxVkFsr3_3_1_5BridgeCreateBackendContext(
    FfxInterface*, FfxEffect, FfxEffectBindlessConfig*, FfxUInt32*);
extern "C" FfxErrorCode ffxVkFsr3_3_1_5BridgeDestroyBackendContext(FfxInterface*, FfxUInt32);
extern "C" FfxErrorCode ffxVkFsr3_3_1_5BridgeGetDeviceCapabilities(
    FfxInterface*, FfxDeviceCapabilities*);
extern "C" FfxErrorCode ffxVkFsr3_3_1_5BridgeStageConstantBufferData(
    FfxInterface*, void*, FfxUInt32, FfxConstantBuffer*);

extern "C" FfxErrorCode fsr3UpscalerGetPermutationBlobByIndex(
    FfxFsr3UpscalerPass, uint32_t, FfxShaderBlob* outBlob)
{
    if (!outBlob)
        return static_cast<FfxErrorCode>(FFX_ERROR_INVALID_POINTER);
    std::memset(outBlob, 0, sizeof(*outBlob));
    return FFX_OK;
}

extern "C" FfxErrorCode fsr3UpscalerIsWave64(uint32_t, bool& isWave64)
{
    isWave64 = false;
    return FFX_OK;
}

namespace {

bool choose_compute_device(VkInstance instance, VkPhysicalDevice* physical,
                           uint32_t* queueFamily)
{
    uint32_t physicalCount = 0;
    if (vkEnumeratePhysicalDevices(instance, &physicalCount, nullptr) != VK_SUCCESS ||
        physicalCount == 0u || physicalCount > 16u)
        return false;
    VkPhysicalDevice devices[16]{};
    if (vkEnumeratePhysicalDevices(instance, &physicalCount, devices) != VK_SUCCESS)
        return false;
    for (uint32_t index = 0; index < physicalCount; ++index) {
        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[index], &count, nullptr);
        if (count > 32u)
            continue;
        VkQueueFamilyProperties properties[32]{};
        vkGetPhysicalDeviceQueueFamilyProperties(devices[index], &count, properties);
        for (uint32_t queue = 0; queue < count; ++queue) {
            if (properties[queue].queueCount && (properties[queue].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
                *physical = devices[index];
                *queueFamily = queue;
                return true;
            }
        }
    }
    return false;
}

bool has_extension(VkPhysicalDevice physical, const char* required)
{
    uint32_t count = 0;
    if (vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr) != VK_SUCCESS || count > 256u)
        return false;
    VkExtensionProperties extensions[256]{};
    if (vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, extensions) != VK_SUCCESS)
        return false;
    for (uint32_t index = 0; index < count; ++index) {
        if (std::strcmp(extensions[index].extensionName, required) == 0)
            return true;
    }
    return false;
}

struct ValidationState {
    std::atomic_uint warnings{0u};
    std::atomic_uint errors{0u};
};

VKAPI_ATTR VkBool32 VKAPI_CALL validation_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data, void* userData)
{
    ValidationState* state = static_cast<ValidationState*>(userData);
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        ++state->warnings;
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        ++state->errors;
    if (severity & (VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT))
        std::fprintf(stderr, "FSR3.1.5 bridge Vulkan validation: %s\n",
                     data && data->pMessage ? data->pMessage : "(no message)");
    return VK_FALSE;
}

bool has_validation_layer()
{
    uint32_t count = 0u;
    if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS)
        return false;
    std::vector<VkLayerProperties> properties(count);
    if (vkEnumerateInstanceLayerProperties(&count, properties.data()) != VK_SUCCESS)
        return false;
    for (const VkLayerProperties& property : properties) {
        if (std::strcmp(property.layerName, "VK_LAYER_KHRONOS_validation") == 0)
            return true;
    }
    return false;
}

bool expect(bool condition, const char* message)
{
    if (!condition)
        std::fprintf(stderr, "FSR3.1.5 bridge test failure: %s\n", message);
    return condition;
}

uint32_t find_host_visible_memory(VkPhysicalDevice physical, uint32_t typeBits)
{
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    for (uint32_t index = 0u; index < properties.memoryTypeCount; ++index) {
        const VkMemoryPropertyFlags flags = properties.memoryTypes[index].propertyFlags;
        if ((typeBits & (1u << index)) && (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
            return index;
    }
    return UINT32_MAX;
}

uint32_t find_device_local_memory(VkPhysicalDevice physical, uint32_t typeBits)
{
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    for (uint32_t index = 0u; index < properties.memoryTypeCount; ++index) {
        const VkMemoryPropertyFlags flags = properties.memoryTypes[index].propertyFlags;
        if ((typeBits & (1u << index)) && (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            return index;
    }
    return UINT32_MAX;
}

struct ExternalImage {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
};

bool create_external_image(VkPhysicalDevice physical, VkDevice device,
                           uint32_t width, uint32_t height, VkFormat format,
                           VkImageUsageFlags usage, ExternalImage* outImage)
{
    if (!outImage)
        return false;
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    VkMemoryAllocateInfo allocationInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    VkMemoryRequirements requirements{};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = {width, height, 1u};
    imageInfo.mipLevels = 1u;
    imageInfo.arrayLayers = 1u;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usage;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &imageInfo, nullptr, &outImage->image) != VK_SUCCESS)
        return false;
    vkGetImageMemoryRequirements(device, outImage->image, &requirements);
    allocationInfo.allocationSize = requirements.size;
    allocationInfo.memoryTypeIndex = find_device_local_memory(physical, requirements.memoryTypeBits);
    if (allocationInfo.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(device, &allocationInfo, nullptr, &outImage->memory) != VK_SUCCESS ||
        vkBindImageMemory(device, outImage->image, outImage->memory, 0u) != VK_SUCCESS) {
        if (outImage->memory != VK_NULL_HANDLE)
            vkFreeMemory(device, outImage->memory, nullptr);
        vkDestroyImage(device, outImage->image, nullptr);
        *outImage = {};
        return false;
    }
    return true;
}

void destroy_external_image(VkDevice device, ExternalImage* image)
{
    if (!image)
        return;
    if (image->image != VK_NULL_HANDLE)
        vkDestroyImage(device, image->image, nullptr);
    if (image->memory != VK_NULL_HANDLE)
        vkFreeMemory(device, image->memory, nullptr);
    *image = {};
}

VkFormat vk_format_from_ffx(uint32_t format)
{
    switch (format) {
    case FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
    case FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT: return VK_FORMAT_R16G16B16A16_SFLOAT;
    case FFX_API_SURFACE_FORMAT_R32_FLOAT: return VK_FORMAT_R32_SFLOAT;
    case FFX_API_SURFACE_FORMAT_R32_UINT: return VK_FORMAT_R32_UINT;
    case FFX_API_SURFACE_FORMAT_R16G16_FLOAT: return VK_FORMAT_R16G16_SFLOAT;
    case FFX_API_SURFACE_FORMAT_R16G16_SINT: return VK_FORMAT_R16G16_SINT;
    case FFX_API_SURFACE_FORMAT_R8_UNORM: return VK_FORMAT_R8_UNORM;
    default: return VK_FORMAT_UNDEFINED;
    }
}

/*
 * Execute the public SDK 3.1.6 reset graph against real Vulkan images.  This
 * deliberately uses the common 3.1.5-named bridge: the bridge is an
 * effect-neutral FfxInterface implementation and must not have an upscaler
 * dependency hidden in its resource or descriptor paths.
 */
bool dispatch_fi_of_reset_frame(
    VkPhysicalDevice physical, VkDevice device, uint32_t queueFamily,
    FfxVkFsr3_3_1_5Bridge* bridge, FfxInterface* backend,
    FfxOpticalflowContext* opticalFlow, FfxFrameInterpolationContext* frameInterpolation)
{
    FfxOpticalflowSharedResourceDescriptions ofShared{};
    FfxFrameInterpolationSharedResourceDescriptions fiShared{};
    ExternalImage images[9]{};
    FfxVkFsr3_3_1_5Resource imported[9]{};
    FfxApiResource resources[9]{};
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    bool ok = bridge && backend && opticalFlow && frameInterpolation;
    const char* stage = "validate arguments";

    struct ImageDescription {
        VkFormat format;
        uint32_t width;
        uint32_t height;
        bool writable;
    } descriptions[9]{};
    if (ok) {
        stage = "query FI/OF shared resources";
        ok = ffxOpticalflowGetSharedResourceDescriptions(opticalFlow, &ofShared) == FFX_OK &&
             ffxFrameInterpolationGetSharedResourceDescriptions(frameInterpolation, &fiShared) == FFX_OK;
    }
    if (ok) {
        descriptions[0] = {VK_FORMAT_R16G16B16A16_SFLOAT, 128u, 128u, false};
        descriptions[1] = {vk_format_from_ffx(ofShared.opticalFlowVector.resourceDescription.format),
                           ofShared.opticalFlowVector.resourceDescription.width,
                           ofShared.opticalFlowVector.resourceDescription.height, true};
        descriptions[2] = {vk_format_from_ffx(ofShared.opticalFlowSCD.resourceDescription.format),
                           ofShared.opticalFlowSCD.resourceDescription.width,
                           ofShared.opticalFlowSCD.resourceDescription.height, true};
        descriptions[3] = {VK_FORMAT_R32_SFLOAT, 64u, 64u, false};
        descriptions[4] = {VK_FORMAT_R16G16_SFLOAT, 64u, 64u, false};
        descriptions[5] = {vk_format_from_ffx(fiShared.dilatedDepth.resourceDescription.format),
                           fiShared.dilatedDepth.resourceDescription.width,
                           fiShared.dilatedDepth.resourceDescription.height, true};
        descriptions[6] = {vk_format_from_ffx(fiShared.dilatedMotionVectors.resourceDescription.format),
                           fiShared.dilatedMotionVectors.resourceDescription.width,
                           fiShared.dilatedMotionVectors.resourceDescription.height, true};
        descriptions[7] = {vk_format_from_ffx(fiShared.reconstructedPrevNearestDepth.resourceDescription.format),
                           fiShared.reconstructedPrevNearestDepth.resourceDescription.width,
                           fiShared.reconstructedPrevNearestDepth.resourceDescription.height, true};
        descriptions[8] = {VK_FORMAT_R16G16B16A16_SFLOAT, 128u, 128u, true};
    }
    for (uint32_t index = 0u; ok && index < 9u; ++index) {
        const VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                                        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        FfxVkFsr3_3_1_5ImportedImageDescription description{};
        stage = "create and import FI/OF application image";
        ok = descriptions[index].format != VK_FORMAT_UNDEFINED &&
             create_external_image(physical, device, descriptions[index].width,
                                   descriptions[index].height, descriptions[index].format,
                                   usage, &images[index]);
        if (!ok) {
            std::fprintf(stderr, "FSR3.1.6 FI/OF image %u allocation failed (format %d, %ux%u, writable %u)\n",
                         index, (int)descriptions[index].format, descriptions[index].width,
                         descriptions[index].height, descriptions[index].writable ? 1u : 0u);
            break;
        }
        description.image = images[index].image;
        description.format = descriptions[index].format;
        description.width = descriptions[index].width;
        description.height = descriptions[index].height;
        description.mipCount = 1u;
        description.arrayLayers = 1u;
        description.layout = descriptions[index].writable ? VK_IMAGE_LAYOUT_GENERAL :
                                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        description.state = descriptions[index].writable
            ? FFX_VK_FSR3_3_1_5_RESOURCE_STATE_UNORDERED_ACCESS
            : FFX_VK_FSR3_3_1_5_RESOURCE_STATE_COMPUTE_READ;
        description.usage = usage;
        imported[index] = ffxVkFsr3_3_1_5BridgeImportImage(bridge, &description);
        resources[index] = ffxVkFsr3_3_1_5BridgeResolveResource(bridge, imported[index]);
        ok = imported[index].resource != nullptr && resources[index].resource != nullptr;
        if (!ok)
            std::fprintf(stderr, "FSR3.1.6 FI/OF image %u import failed (format %d, %ux%u, writable %u)\n",
                         index, (int)description.format, description.width, description.height,
                         descriptions[index].writable ? 1u : 0u);
    }
    poolInfo.queueFamilyIndex = queueFamily;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1u;
    if (ok) {
        stage = "create FI/OF dispatch command pool";
        ok = vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) == VK_SUCCESS;
    }
    allocateInfo.commandPool = commandPool;
    if (ok) {
        stage = "allocate FI/OF dispatch command buffer";
        ok = vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer) == VK_SUCCESS;
    }
    const auto recordFrame = [&](uint64_t frameId, bool reset, bool initializeLayouts) {
        if (initializeLayouts) {
        VkImageMemoryBarrier barriers[9]{};
        for (uint32_t index = 0u; index < 9u; ++index) {
            barriers[index].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barriers[index].dstAccessMask = descriptions[index].writable
                ? VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_SHADER_READ_BIT;
            barriers[index].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barriers[index].newLayout = descriptions[index].writable
                ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barriers[index].image = images[index].image;
            barriers[index].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barriers[index].subresourceRange.levelCount = 1u;
            barriers[index].subresourceRange.layerCount = 1u;
        }
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u, 0u, nullptr,
                             0u, nullptr, 9u, barriers);
        }

        FfxOpticalflowDispatchDescription opticalFlowDispatch{};
        opticalFlowDispatch.commandList = reinterpret_cast<FfxCommandList>(commandBuffer);
        opticalFlowDispatch.color = resources[0];
        opticalFlowDispatch.opticalFlowVector = resources[1];
        opticalFlowDispatch.opticalFlowSCD = resources[2];
        opticalFlowDispatch.reset = reset;
        opticalFlowDispatch.backbufferTransferFunction = FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
        opticalFlowDispatch.minMaxLuminance = {0.0f, 1.0f};
        stage = "record FI/OF optical-flow dispatch";
        const FfxErrorCode opticalFlowResult =
            ffxOpticalflowContextDispatch(opticalFlow, &opticalFlowDispatch);
        if (opticalFlowResult != FFX_OK) {
            std::fprintf(stderr, "FSR3.1.6 optical-flow dispatch returned %u\n",
                         static_cast<unsigned>(opticalFlowResult));
            return false;
        }

        FfxFrameInterpolationPrepareDescription prepare{};
        prepare.commandList = reinterpret_cast<FfxCommandList>(commandBuffer);
        prepare.renderSize = {64u, 64u};
        prepare.motionVectorScale = {64.0f, 64.0f};
        prepare.frameTimeDelta = 16.6667f;
        prepare.cameraNear = 0.1f;
        prepare.cameraFar = 1000.0f;
        prepare.viewSpaceToMetersFactor = 1.0f;
        prepare.cameraFovAngleVertical = 1.0471975512f;
        prepare.depth = resources[3];
        prepare.motionVectors = resources[4];
        prepare.frameID = frameId;
        prepare.dilatedDepth = resources[5];
        prepare.dilatedMotionVectors = resources[6];
        prepare.reconstructedPrevDepth = resources[7];
        prepare.cameraUp[1] = 1.0f;
        prepare.cameraRight[0] = 1.0f;
        prepare.cameraForward[2] = -1.0f;
        stage = "record FI prepare dispatch";
        const FfxErrorCode prepareResult = ffxFrameInterpolationPrepare(frameInterpolation, &prepare);
        if (prepareResult != FFX_OK) {
            std::fprintf(stderr, "FSR3.1.6 FI prepare returned %u\n",
                         static_cast<unsigned>(prepareResult));
            return false;
        }

        FfxFrameInterpolationDispatchDescription dispatch{};
        dispatch.commandList = reinterpret_cast<FfxCommandList>(commandBuffer);
        dispatch.displaySize = {128u, 128u};
        dispatch.renderSize = {64u, 64u};
        dispatch.currentBackBuffer = resources[0];
        dispatch.output = resources[8];
        dispatch.interpolationRect = {0, 0, 128, 128};
        dispatch.opticalFlowVector = resources[1];
        dispatch.opticalFlowSceneChangeDetection = resources[2];
        dispatch.opticalFlowBufferSize = {ofShared.opticalFlowVector.resourceDescription.width,
                                          ofShared.opticalFlowVector.resourceDescription.height};
        dispatch.opticalFlowScale = {1.0f, 1.0f};
        dispatch.opticalFlowBlockSize = 8u;
        dispatch.frameTimeDelta = 16.6667f;
        dispatch.reset = reset;
        dispatch.cameraNear = prepare.cameraNear;
        dispatch.cameraFar = prepare.cameraFar;
        dispatch.cameraFovAngleVertical = prepare.cameraFovAngleVertical;
        dispatch.viewSpaceToMetersFactor = prepare.viewSpaceToMetersFactor;
        dispatch.backBufferTransferFunction = FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
        dispatch.minMaxLuminance[0] = 0.0f;
        dispatch.minMaxLuminance[1] = 1.0f;
        dispatch.frameID = frameId;
        dispatch.dilatedDepth = resources[5];
        dispatch.dilatedMotionVectors = resources[6];
        dispatch.reconstructedPrevDepth = resources[7];
        stage = "record FI dispatch";
        const FfxErrorCode dispatchResult = ffxFrameInterpolationDispatch(frameInterpolation, &dispatch);
        if (dispatchResult != FFX_OK) {
            std::fprintf(stderr, "FSR3.1.6 FI dispatch returned %u\n",
                         static_cast<unsigned>(dispatchResult));
            return false;
        }
        return true;
    };
    if (ok) {
        stage = "begin FI/OF reset-frame command buffer";
        ok = vkBeginCommandBuffer(commandBuffer, &beginInfo) == VK_SUCCESS;
    }
    if (ok)
        ok = recordFrame(1u, true, true);
    if (ok) {
        stage = "end FI/OF dispatch command buffer";
        const VkResult endResult = vkEndCommandBuffer(commandBuffer);
        if (endResult != VK_SUCCESS)
            std::fprintf(stderr, "FSR3.1.6 FI/OF vkEndCommandBuffer returned %d\n", (int)endResult);
        ok = endResult == VK_SUCCESS;
    }
    if (ok) {
        vkGetDeviceQueue(device, queueFamily, 0u, &queue);
        submitInfo.commandBufferCount = 1u;
        submitInfo.pCommandBuffers = &commandBuffer;
        stage = "submit FI/OF reset frame";
        ok = vkQueueSubmit(queue, 1u, &submitInfo, VK_NULL_HANDLE) == VK_SUCCESS &&
             vkQueueWaitIdle(queue) == VK_SUCCESS;
    }
    if (ok) {
        stage = "reset FI/OF temporal-frame command pool";
        ok = vkResetCommandPool(device, commandPool, 0u) == VK_SUCCESS;
    }
    if (ok) {
        stage = "begin FI/OF temporal-frame command buffer";
        ok = vkBeginCommandBuffer(commandBuffer, &beginInfo) == VK_SUCCESS;
    }
    if (ok)
        ok = recordFrame(2u, false, false);
    if (ok) {
        stage = "end FI/OF temporal-frame command buffer";
        const VkResult endResult = vkEndCommandBuffer(commandBuffer);
        if (endResult != VK_SUCCESS)
            std::fprintf(stderr, "FSR3.1.6 FI/OF temporal vkEndCommandBuffer returned %d\n",
                         (int)endResult);
        ok = endResult == VK_SUCCESS;
    }
    if (ok) {
        stage = "submit FI/OF temporal frame";
        ok = vkQueueSubmit(queue, 1u, &submitInfo, VK_NULL_HANDLE) == VK_SUCCESS &&
             vkQueueWaitIdle(queue) == VK_SUCCESS;
    }
    if (commandPool != VK_NULL_HANDLE)
        vkDestroyCommandPool(device, commandPool, nullptr);
    for (uint32_t index = 0u; index < 9u; ++index) {
        ffxVkFsr3_3_1_5BridgeReleaseImportedImage(bridge, imported[index]);
        destroy_external_image(device, &images[index]);
    }
    if (!ok)
        std::fprintf(stderr, "FSR3.1.6 FI/OF frame-sequence failure at: %s\n", stage);
    return expect(ok, "record and submit real SDK 3.1.6 FI/OF reset and temporal frames");
}

bool verify_rgba16f_output(VkPhysicalDevice physical, VkDevice device, VkQueue queue,
                           VkCommandPool commandPool, VkImage output,
                           uint32_t width, uint32_t height)
{
    const VkDeviceSize byteCount = static_cast<VkDeviceSize>(width) * height * 4u * sizeof(uint16_t);
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    VkMemoryAllocateInfo allocationInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    VkMemoryRequirements requirements{};
    VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    bool result = false;

    bufferInfo.size = byteCount;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (!expect(vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) == VK_SUCCESS,
                "create SDK output readback buffer"))
        goto cleanup;
    vkGetBufferMemoryRequirements(device, buffer, &requirements);
    allocationInfo.allocationSize = requirements.size;
    allocationInfo.memoryTypeIndex = find_host_visible_memory(physical, requirements.memoryTypeBits);
    if (!expect(allocationInfo.memoryTypeIndex != UINT32_MAX, "find SDK output readback memory") ||
        !expect(vkAllocateMemory(device, &allocationInfo, nullptr, &memory) == VK_SUCCESS,
                "allocate SDK output readback memory") ||
        !expect(vkBindBufferMemory(device, buffer, memory, 0u) == VK_SUCCESS,
                "bind SDK output readback memory"))
        goto cleanup;
    allocateInfo.commandPool = commandPool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1u;
    if (!expect(vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer) == VK_SUCCESS,
                "allocate SDK output readback command buffer") ||
        !expect(vkResetCommandPool(device, commandPool, 0u) == VK_SUCCESS,
                "reset SDK output readback command pool") ||
        !expect(vkBeginCommandBuffer(commandBuffer, &beginInfo) == VK_SUCCESS,
                "begin SDK output readback command buffer"))
        goto cleanup;
    {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        VkBufferImageCopy copy{};
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.image = output;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1u;
        barrier.subresourceRange.layerCount = 1u;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, nullptr,
                             0u, nullptr, 1u, &barrier);
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1u;
        copy.imageExtent = {width, height, 1u};
        vkCmdCopyImageToBuffer(commandBuffer, output, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               buffer, 1u, &copy);
    }
    if (!expect(vkEndCommandBuffer(commandBuffer) == VK_SUCCESS,
                "end SDK output readback command buffer"))
        goto cleanup;
    submitInfo.commandBufferCount = 1u;
    submitInfo.pCommandBuffers = &commandBuffer;
    if (!expect(vkQueueSubmit(queue, 1u, &submitInfo, VK_NULL_HANDLE) == VK_SUCCESS,
                "submit SDK output readback") ||
        !expect(vkQueueWaitIdle(queue) == VK_SUCCESS, "finish SDK output readback"))
        goto cleanup;
    {
        void* mapped = nullptr;
        if (!expect(vkMapMemory(device, memory, 0u, byteCount, 0u, &mapped) == VK_SUCCESS,
                    "map SDK output readback"))
            goto cleanup;
        uint32_t nonzeroRgb = 0u;
        uint32_t poisonRgb = 0u;
        uint32_t nonfinite = 0u;
        const uint16_t* components = static_cast<const uint16_t*>(mapped);
        for (uint32_t pixel = 0u; pixel < width * height; ++pixel) {
            for (uint32_t channel = 0u; channel < 3u; ++channel) {
                const uint16_t value = components[pixel * 4u + channel];
                nonzeroRgb += value != 0u;
                poisonRgb += value == 0xbc00u;
                nonfinite += (value & 0x7c00u) == 0x7c00u;
            }
        }
        vkUnmapMemory(device, memory);
        if (!expect(nonfinite == 0u, "SDK output RGB is finite") ||
            !expect(nonzeroRgb > 0u, "SDK output RGB is nonzero") ||
            !expect(poisonRgb == 0u, "SDK output RGB fully overwrites poison"))
            goto cleanup;
        std::printf("FSR3.1.5 SDK Vulkan bridge output: %u nonzero RGB components\n", nonzeroRgb);
    }
    result = true;

cleanup:
    if (memory != VK_NULL_HANDLE)
        vkFreeMemory(device, memory, nullptr);
    if (buffer != VK_NULL_HANDLE)
        vkDestroyBuffer(device, buffer, nullptr);
    return result;
}

} // namespace

int main()
{
    static const wchar_t* kPipelineNames[] = {
        L"FSR3-PREPARE-INPUTS", L"FSR3-LUMA-PYRAMID", L"FSR3-SHADING-CHANGE-PYRAMID",
        L"FSR3-SHADING-CHANGE", L"FSR3-PREPARE-REACTIVITY", L"FSR3-LUMA-INSTABILITY",
        L"FSR3-ACCUMULATE", L"FSR3-ACCUM_SHARP", L"FSR3-RCAS", L"FSR3-DEBUG-VIEW",
        L"FSR3-GEN_REACTIVE",
    };
    VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    PFN_vkDestroyDebugUtilsMessengerEXT destroyMessenger = nullptr;
    uint32_t queueFamily = 0;
    int result = 1;
    bool storageImageWithoutFormat = false;
    ValidationState validation;
    const bool validationAvailable = has_validation_layer();
    VkDebugUtilsMessengerCreateInfoEXT debugInfo{
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugInfo.pfnUserCallback = validation_callback;
    debugInfo.pUserData = &validation;
    const char* layers[] = {"VK_LAYER_KHRONOS_validation"};
    const char* instanceExtensions[] = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
    appInfo.apiVersion = VK_API_VERSION_1_2;
    instanceInfo.pApplicationInfo = &appInfo;
    instanceInfo.pNext = validationAvailable ? &debugInfo : nullptr;
    instanceInfo.enabledLayerCount = validationAvailable ? 1u : 0u;
    instanceInfo.ppEnabledLayerNames = validationAvailable ? layers : nullptr;
    instanceInfo.enabledExtensionCount = validationAvailable ? 1u : 0u;
    instanceInfo.ppEnabledExtensionNames = validationAvailable ? instanceExtensions : nullptr;
    if (vkCreateInstance(&instanceInfo, nullptr, &instance) != VK_SUCCESS)
        return 77;
    if (!validationAvailable) {
        std::fprintf(stderr, "SKIP: Vulkan validation layer unavailable\n");
        result = 77;
        goto cleanup;
    }
    {
        const auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
        destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (!createMessenger || !destroyMessenger ||
            createMessenger(instance, &debugInfo, nullptr, &messenger) != VK_SUCCESS) {
            std::fprintf(stderr, "SKIP: Vulkan validation messenger unavailable\n");
            result = 77;
            goto cleanup;
        }
    }
    if (!choose_compute_device(instance, &physical, &queueFamily)) {
        result = 77;
        goto cleanup;
    }
    {
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        VkPhysicalDeviceFeatures2 enabledFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        VkPhysicalDeviceFeatures2 supportedFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR derivativeFeatures{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR};
        const char* extensions[1]{};
        queueInfo.queueFamilyIndex = queueFamily;
        queueInfo.queueCount = 1u;
        queueInfo.pQueuePriorities = &priority;
        vkGetPhysicalDeviceFeatures2(physical, &supportedFeatures);
        storageImageWithoutFormat =
            supportedFeatures.features.shaderStorageImageReadWithoutFormat == VK_TRUE &&
            supportedFeatures.features.shaderStorageImageWriteWithoutFormat == VK_TRUE;
        if (storageImageWithoutFormat) {
            enabledFeatures.features.shaderStorageImageReadWithoutFormat = VK_TRUE;
            enabledFeatures.features.shaderStorageImageWriteWithoutFormat = VK_TRUE;
        }
        if (has_extension(physical, VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME)) {
            VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR supportedDerivatives{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR};
            supportedFeatures.pNext = &supportedDerivatives;
            vkGetPhysicalDeviceFeatures2(physical, &supportedFeatures);
            if (supportedDerivatives.computeDerivativeGroupLinear) {
                derivativeFeatures.computeDerivativeGroupLinear = VK_TRUE;
                enabledFeatures.pNext = &derivativeFeatures;
                extensions[deviceInfo.enabledExtensionCount++] = VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME;
            }
        }
        deviceInfo.queueCreateInfoCount = 1u;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        deviceInfo.pNext = &enabledFeatures;
        deviceInfo.ppEnabledExtensionNames = extensions;
        if (vkCreateDevice(physical, &deviceInfo, nullptr, &device) != VK_SUCCESS) {
            result = 77;
            goto cleanup;
        }
    }
    {
        FfxVkFsr3_3_1_5Bridge* bridge =
            ffxVkFsr3_3_1_5CreateBridgeWithPhysicalDevice(physical, device, nullptr);
        FfxInterface backend{};
        backend.device = bridge;
        backend.fpGetSDKVersion = ffxVkFsr3_3_1_5BridgeGetSDKVersion;
        backend.fpCreateBackendContext = ffxVkFsr3_3_1_5BridgeCreateBackendContext;
        backend.fpDestroyBackendContext = ffxVkFsr3_3_1_5BridgeDestroyBackendContext;
        backend.fpGetDeviceCapabilities = ffxVkFsr3_3_1_5BridgeGetDeviceCapabilities;
        backend.fpStageConstantBufferDataFunc = ffxVkFsr3_3_1_5BridgeStageConstantBufferData;
        backend.fpCreateResource = ffxVkFsr3_3_1_5BridgeCreateResource;
        backend.fpDestroyResource = ffxVkFsr3_3_1_5BridgeDestroyResource;
        backend.fpGetResource = ffxVkFsr3_3_1_5BridgeGetResource;
        backend.fpGetResourceDescription = ffxVkFsr3_3_1_5BridgeGetResourceDescription;
        backend.fpRegisterResource = ffxVkFsr3_3_1_5BridgeRegisterResource;
        backend.fpUnregisterResources = ffxVkFsr3_3_1_5BridgeUnregisterResources;
        backend.fpScheduleGpuJob = ffxVkFsr3_3_1_5BridgeScheduleGpuJob;
        backend.fpExecuteGpuJobs = ffxVkFsr3_3_1_5BridgeExecuteGpuJobs;
        backend.fpCreatePipeline = ffxVkFsr3_3_1_5BridgeCreatePipeline;
        backend.fpDestroyPipeline = ffxVkFsr3_3_1_5BridgeDestroyPipeline;
        if (!expect(bridge != nullptr, "create bridge"))
            goto cleanup;
        if (storageImageWithoutFormat) {
            FfxOpticalflowContext opticalFlow{};
            FfxOpticalflowContextDescription opticalFlowDescription{};
            opticalFlowDescription.backendInterface = backend;
            opticalFlowDescription.resolution = {128u, 128u};
            FfxFrameInterpolationContext frameInterpolation{};
            FfxFrameInterpolationContextDescription frameInterpolationDescription{};
            frameInterpolationDescription.backendInterface = backend;
            frameInterpolationDescription.maxRenderSize = {64u, 64u};
            frameInterpolationDescription.displaySize = {128u, 128u};
            frameInterpolationDescription.backBufferFormat = FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT;
            frameInterpolationDescription.previousInterpolationSourceFormat =
                FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT;
            if (!expect(ffxOpticalflowContextCreate(&opticalFlow, &opticalFlowDescription) == FFX_OK,
                        "create FI/OF optical-flow Vulkan context") ||
                !expect(ffxFrameInterpolationContextCreate(
                            &frameInterpolation, &frameInterpolationDescription) == FFX_OK,
                        "create FI/OF frame-interpolation Vulkan context")) {
                ffxFrameInterpolationContextDestroy(&frameInterpolation);
                ffxOpticalflowContextDestroy(&opticalFlow);
                ffxVkFsr3_3_1_5DestroyBridge(bridge);
                goto cleanup;
            }
            VkCommandPool initializationPool = VK_NULL_HANDLE;
            VkCommandBuffer initializationCommands = VK_NULL_HANDLE;
            VkCommandPoolCreateInfo initializationPoolInfo{
                VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            VkCommandBufferAllocateInfo initializationAllocateInfo{
                VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            VkCommandBufferBeginInfo initializationBeginInfo{
                VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            VkSubmitInfo initializationSubmitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            VkQueue initializationQueue = VK_NULL_HANDLE;
            initializationPoolInfo.queueFamilyIndex = queueFamily;
            initializationPoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            initializationAllocateInfo.commandPool = initializationPool;
            initializationAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            initializationAllocateInfo.commandBufferCount = 1u;
            if (!expect(vkCreateCommandPool(device, &initializationPoolInfo, nullptr,
                                             &initializationPool) == VK_SUCCESS,
                        "create FI/OF initialization command pool")) {
                ffxFrameInterpolationContextDestroy(&frameInterpolation);
                ffxOpticalflowContextDestroy(&opticalFlow);
                ffxVkFsr3_3_1_5DestroyBridge(bridge);
                goto cleanup;
            }
            initializationAllocateInfo.commandPool = initializationPool;
            if (!expect(vkAllocateCommandBuffers(device, &initializationAllocateInfo,
                                                  &initializationCommands) == VK_SUCCESS,
                        "allocate FI/OF initialization commands") ||
                !expect(vkBeginCommandBuffer(initializationCommands, &initializationBeginInfo) == VK_SUCCESS,
                        "begin FI/OF initialization commands") ||
                !expect(ffxVkFsr3_3_1_5BridgeExecuteGpuJobs(
                            &backend, initializationCommands, 1u) == FFX_OK,
                        "record FI/OF initialization jobs") ||
                !expect(vkEndCommandBuffer(initializationCommands) == VK_SUCCESS,
                        "end FI/OF initialization commands")) {
                vkDestroyCommandPool(device, initializationPool, nullptr);
                ffxFrameInterpolationContextDestroy(&frameInterpolation);
                ffxOpticalflowContextDestroy(&opticalFlow);
                ffxVkFsr3_3_1_5DestroyBridge(bridge);
                goto cleanup;
            }
            vkGetDeviceQueue(device, queueFamily, 0u, &initializationQueue);
            initializationSubmitInfo.commandBufferCount = 1u;
            initializationSubmitInfo.pCommandBuffers = &initializationCommands;
            if (!expect(vkQueueSubmit(initializationQueue, 1u, &initializationSubmitInfo,
                                       VK_NULL_HANDLE) == VK_SUCCESS,
                        "submit FI/OF initialization jobs") ||
                !expect(vkQueueWaitIdle(initializationQueue) == VK_SUCCESS,
                        "complete FI/OF initialization jobs")) {
                vkDestroyCommandPool(device, initializationPool, nullptr);
                ffxFrameInterpolationContextDestroy(&frameInterpolation);
                ffxOpticalflowContextDestroy(&opticalFlow);
                ffxVkFsr3_3_1_5DestroyBridge(bridge);
                goto cleanup;
            }
            vkDestroyCommandPool(device, initializationPool, nullptr);
            if (!dispatch_fi_of_reset_frame(physical, device, queueFamily, bridge, &backend,
                                            &opticalFlow, &frameInterpolation)) {
                ffxFrameInterpolationContextDestroy(&frameInterpolation);
                ffxOpticalflowContextDestroy(&opticalFlow);
                ffxVkFsr3_3_1_5DestroyBridge(bridge);
                goto cleanup;
            }
            ffxFrameInterpolationContextDestroy(&frameInterpolation);
            ffxOpticalflowContextDestroy(&opticalFlow);
        } else {
            std::fprintf(stderr, "SKIP: storage-image read/write-without-format unavailable; FI/OF create omitted\n");
        }
        {
            ExternalImage image{};
            VkCommandPool commandPool = VK_NULL_HANDLE;
            VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
            VkQueue queue = VK_NULL_HANDLE;
            FfxVkFsr3_3_1_5ImportedImageDescription imported{};
            FfxVkFsr3_3_1_5Resource importedImage{};
            FfxApiResource apiImage{};
            FfxResourceInternal registered{};
            FfxGpuJobDescription transition{};
            VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            poolInfo.queueFamilyIndex = queueFamily;
            poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            if (!expect(create_external_image(
                            physical, device, 4u, 4u, VK_FORMAT_R16G16B16A16_SFLOAT,
                            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, &image),
                        "create caller-owned image") ||
                !expect(vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) == VK_SUCCESS,
                        "create external-image command pool")) {
                destroy_external_image(device, &image);
                ffxVkFsr3_3_1_5DestroyBridge(bridge);
                goto cleanup;
            }
            imported.image = image.image;
            imported.format = VK_FORMAT_R16G16B16A16_SFLOAT;
            imported.width = 4u;
            imported.height = 4u;
            imported.mipCount = 1u;
            imported.arrayLayers = 1u;
            imported.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imported.state = FFX_VK_FSR3_3_1_5_RESOURCE_STATE_COMPUTE_READ;
            imported.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                             VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            importedImage = ffxVkFsr3_3_1_5BridgeImportImage(bridge, &imported);
            apiImage = ffxVkFsr3_3_1_5BridgeResolveResource(bridge, importedImage);
            if (!expect(importedImage.resource != nullptr, "import caller-owned image") ||
                !expect(apiImage.description.format == FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT,
                        "imported image format") ||
                !expect(apiImage.state == FFX_API_RESOURCE_STATE_COMPUTE_READ,
                        "imported image state")) {
                ffxVkFsr3_3_1_5BridgeReleaseImportedImage(bridge, importedImage);
                destroy_external_image(device, &image);
                vkDestroyCommandPool(device, commandPool, nullptr);
                ffxVkFsr3_3_1_5DestroyBridge(bridge);
                goto cleanup;
            }
            allocateInfo.commandPool = commandPool;
            allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocateInfo.commandBufferCount = 1u;
            if (!expect(vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer) == VK_SUCCESS,
                        "allocate external-image command buffer") ||
                !expect(vkBeginCommandBuffer(commandBuffer, &beginInfo) == VK_SUCCESS,
                        "begin external-image command buffer")) {
                ffxVkFsr3_3_1_5BridgeReleaseImportedImage(bridge, importedImage);
                destroy_external_image(device, &image);
                vkDestroyCommandPool(device, commandPool, nullptr);
                ffxVkFsr3_3_1_5DestroyBridge(bridge);
                goto cleanup;
            }
            {
                VkImageMemoryBarrier initialBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                initialBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                initialBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                initialBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                initialBarrier.image = image.image;
                initialBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                initialBarrier.subresourceRange.levelCount = 1u;
                initialBarrier.subresourceRange.layerCount = 1u;
                vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u, 0u, nullptr,
                                     0u, nullptr, 1u, &initialBarrier);
            }
            transition.jobType = FFX_GPU_JOB_BARRIER;
            if (!expect(ffxVkFsr3_3_1_5BridgeRegisterResource(
                            &backend, &apiImage, 1u, &registered) == FFX_OK,
                        "register caller-owned image") ||
                !expect(registered.internalIndex > 0, "registered imported image")) {
                ffxVkFsr3_3_1_5BridgeReleaseImportedImage(bridge, importedImage);
                destroy_external_image(device, &image);
                vkDestroyCommandPool(device, commandPool, nullptr);
                ffxVkFsr3_3_1_5DestroyBridge(bridge);
                goto cleanup;
            }
            transition.barrierDescriptor.resource = registered;
            transition.barrierDescriptor.barrierType = FFX_BARRIER_TYPE_TRANSITION;
            transition.barrierDescriptor.currentState = FFX_API_RESOURCE_STATE_COMPUTE_READ;
            transition.barrierDescriptor.newState = FFX_API_RESOURCE_STATE_UNORDERED_ACCESS;
            if (!expect(ffxVkFsr3_3_1_5BridgeScheduleGpuJob(&backend, &transition) == FFX_OK,
                        "schedule imported image transition") ||
                !expect(ffxVkFsr3_3_1_5BridgeExecuteGpuJobs(
                            &backend, commandBuffer, 1u) == FFX_OK,
                        "record imported image transition") ||
                !expect(ffxVkFsr3_3_1_5BridgeUnregisterResources(
                            &backend, commandBuffer, 1u) == FFX_OK,
                        "restore imported image layout") ||
                !expect(vkEndCommandBuffer(commandBuffer) == VK_SUCCESS,
                        "end external-image command buffer")) {
                ffxVkFsr3_3_1_5BridgeReleaseImportedImage(bridge, importedImage);
                destroy_external_image(device, &image);
                vkDestroyCommandPool(device, commandPool, nullptr);
                ffxVkFsr3_3_1_5DestroyBridge(bridge);
                goto cleanup;
            }
            vkGetDeviceQueue(device, queueFamily, 0u, &queue);
            submitInfo.commandBufferCount = 1u;
            submitInfo.pCommandBuffers = &commandBuffer;
            if (!expect(vkQueueSubmit(queue, 1u, &submitInfo, VK_NULL_HANDLE) == VK_SUCCESS,
                        "submit imported image transition") ||
                !expect(vkQueueWaitIdle(queue) == VK_SUCCESS,
                        "finish imported image transition")) {
                ffxVkFsr3_3_1_5BridgeReleaseImportedImage(bridge, importedImage);
                destroy_external_image(device, &image);
                vkDestroyCommandPool(device, commandPool, nullptr);
                ffxVkFsr3_3_1_5DestroyBridge(bridge);
                goto cleanup;
            }
            ffxVkFsr3_3_1_5BridgeReleaseImportedImage(bridge, importedImage);
            destroy_external_image(device, &image);
            vkDestroyCommandPool(device, commandPool, nullptr);
        }
        {
            FfxVkFsr3_3_1_5UpscalerCreateInfo createInfo{};
            FfxVkFsr3_3_1_5UpscalerContext* portableContext = nullptr;
            FfxVkFsr3_3_1_5SharedResourceDescriptions shared{};
            FfxVkFsr3_3_1_5MemoryUsage memoryUsage{};
            ExternalImage images[9]{};
            FfxVkFsr3_3_1_5Resource resources[9]{};
            VkCommandPool commandPool = VK_NULL_HANDLE;
            VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
            VkQueue queue = VK_NULL_HANDLE;
            VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            createInfo.physicalDevice = physical;
            createInfo.device = device;
            createInfo.maxRenderWidth = 64u;
            createInfo.maxRenderHeight = 64u;
            createInfo.maxUpscaleWidth = 128u;
            createInfo.maxUpscaleHeight = 128u;
            createInfo.hdrColorInput = VK_TRUE;
            createInfo.autoExposure = VK_TRUE;
            const auto cleanupPortable = [&]() {
                if (portableContext) {
                    FfxVkFsr3_3_1_5Bridge* portableBridge =
                        ffxVkFsr3_3_1_5UpscalerContextGetBridge(portableContext);
                    if (portableBridge) {
                        for (const FfxVkFsr3_3_1_5Resource& resource : resources)
                            ffxVkFsr3_3_1_5BridgeReleaseImportedImage(portableBridge, resource);
                    }
                }
                for (ExternalImage& image : images)
                    destroy_external_image(device, &image);
                if (commandPool != VK_NULL_HANDLE)
                    vkDestroyCommandPool(device, commandPool, nullptr);
                ffxVkFsr3_3_1_5UpscalerContextDestroy(portableContext);
            };
            if (!expect(ffxVkFsr3_3_1_5UpscalerContextCreate(
                            &createInfo, &portableContext) == FFX_VK_FSR3_3_1_5_OK,
                        "create public SDK 3.1.5 upscaler context") ||
                !expect(ffxVkFsr3_3_1_5UpscalerContextGetBridge(portableContext) != nullptr,
                        "get public SDK 3.1.5 bridge") ||
                !expect(ffxVkFsr3_3_1_5UpscalerContextGetSharedResourceDescriptions(
                            portableContext, &shared) == FFX_VK_FSR3_3_1_5_OK,
                        "get public SDK 3.1.5 shared resource descriptions") ||
                !expect(ffxVkFsr3_3_1_5UpscalerContextGetMemoryUsage(
                            portableContext, &memoryUsage) == FFX_VK_FSR3_3_1_5_OK &&
                        memoryUsage.totalUsageInBytes > 0u &&
                        memoryUsage.totalUsageInBytes >= memoryUsage.aliasableUsageInBytes,
                        "get public SDK 3.1.5 memory usage")) {
                cleanupPortable();
                ffxVkFsr3_3_1_5DestroyBridge(bridge);
                goto cleanup;
            }
            struct ImportedFrameDescription {
                VkFormat format;
                uint32_t width;
                uint32_t height;
            };
            const ImportedFrameDescription descriptions[9] = {
                {VK_FORMAT_R16G16B16A16_SFLOAT, 64u, 64u},
                {VK_FORMAT_R32_SFLOAT, 64u, 64u},
                {VK_FORMAT_R16G16_SFLOAT, 64u, 64u},
                {VK_FORMAT_R8_UNORM, 64u, 64u},
                {VK_FORMAT_R8_UNORM, 64u, 64u},
                {shared.dilatedDepth.format, shared.dilatedDepth.width, shared.dilatedDepth.height},
                {shared.dilatedMotionVectors.format, shared.dilatedMotionVectors.width,
                    shared.dilatedMotionVectors.height},
                {shared.reconstructedPrevNearestDepth.format,
                    shared.reconstructedPrevNearestDepth.width,
                    shared.reconstructedPrevNearestDepth.height},
                {VK_FORMAT_R16G16B16A16_SFLOAT, 128u, 128u},
            };
            FfxVkFsr3_3_1_5Bridge* portableBridge =
                ffxVkFsr3_3_1_5UpscalerContextGetBridge(portableContext);
            bool importsOk = portableBridge != nullptr;
            for (uint32_t index = 0u; importsOk && index < 9u; ++index) {
                const bool writable = index >= 5u;
                const VkFormat format = descriptions[index].format;
                importsOk = format != VK_FORMAT_UNDEFINED &&
                    create_external_image(physical, device, descriptions[index].width,
                                          descriptions[index].height, format,
                                          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                                          VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                          &images[index]);
                if (importsOk) {
                    FfxVkFsr3_3_1_5ImportedImageDescription imported{};
                    imported.image = images[index].image;
                    imported.format = format;
                    imported.width = descriptions[index].width;
                    imported.height = descriptions[index].height;
                    imported.mipCount = 1u;
                    imported.arrayLayers = 1u;
                    imported.layout = writable ? VK_IMAGE_LAYOUT_GENERAL :
                                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    imported.state = writable
                        ? FFX_VK_FSR3_3_1_5_RESOURCE_STATE_UNORDERED_ACCESS
                        : FFX_VK_FSR3_3_1_5_RESOURCE_STATE_COMPUTE_READ;
                    imported.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                                     VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
                    resources[index] = ffxVkFsr3_3_1_5BridgeImportImage(portableBridge, &imported);
                    importsOk = resources[index].resource != nullptr;
                }
            }
            poolInfo.queueFamilyIndex = queueFamily;
            poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            if (!expect(importsOk, "import all public SDK 3.1.5 frame images") ||
                !expect(vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) == VK_SUCCESS,
                        "create public SDK dispatch command pool")) {
                cleanupPortable();
                ffxVkFsr3_3_1_5DestroyBridge(bridge);
                goto cleanup;
            }
            allocateInfo.commandPool = commandPool;
            allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocateInfo.commandBufferCount = 1u;
            if (!expect(vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer) == VK_SUCCESS,
                        "allocate public SDK dispatch command buffer") ||
                !expect(vkBeginCommandBuffer(commandBuffer, &beginInfo) == VK_SUCCESS,
                        "begin public SDK dispatch command buffer")) {
                cleanupPortable();
                ffxVkFsr3_3_1_5DestroyBridge(bridge);
                goto cleanup;
            }
            {
                VkImageMemoryBarrier barriers[9]{};
                for (uint32_t index = 0u; index < 9u; ++index) {
                    const bool writable = index >= 5u;
                    barriers[index].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    barriers[index].dstAccessMask = writable ? VK_ACCESS_SHADER_WRITE_BIT :
                                                            VK_ACCESS_SHADER_READ_BIT;
                    barriers[index].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                    barriers[index].newLayout = writable ? VK_IMAGE_LAYOUT_GENERAL :
                                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    barriers[index].image = images[index].image;
                    barriers[index].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    barriers[index].subresourceRange.levelCount = 1u;
                    barriers[index].subresourceRange.layerCount = 1u;
                }
                vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u, 0u, nullptr,
                                     0u, nullptr, 9u, barriers);
            }
            FfxVkFsr3_3_1_5UpscalerDispatchInfo dispatch{};
            dispatch.commandBuffer = commandBuffer;
            dispatch.color = resources[0];
            dispatch.depth = resources[1];
            dispatch.motionVectors = resources[2];
            dispatch.reactive = resources[3];
            dispatch.transparencyAndComposition = resources[4];
            dispatch.dilatedDepth = resources[5];
            dispatch.dilatedMotionVectors = resources[6];
            dispatch.reconstructedPrevNearestDepth = resources[7];
            dispatch.output = resources[8];
            dispatch.motionVectorScaleX = 64.0f;
            dispatch.motionVectorScaleY = 64.0f;
            dispatch.renderWidth = 64u;
            dispatch.renderHeight = 64u;
            dispatch.upscaleWidth = 128u;
            dispatch.upscaleHeight = 128u;
            dispatch.frameTimeMilliseconds = 16.6667f;
            dispatch.preExposure = 1.0f;
            dispatch.reset = VK_TRUE;
            dispatch.cameraNear = 0.1f;
            dispatch.cameraFar = 1000.0f;
            dispatch.cameraVerticalFovRadians = 1.0471975512f;
            dispatch.viewSpaceToMeters = 1.0f;
            if (!expect(ffxVkFsr3_3_1_5UpscalerContextRecordDispatch(
                            portableContext, &dispatch) == FFX_VK_FSR3_3_1_5_OK,
                        "record public SDK 3.1.5 imported-image dispatch") ||
                !expect(vkEndCommandBuffer(commandBuffer) == VK_SUCCESS,
                        "end public SDK dispatch command buffer")) {
                cleanupPortable();
                ffxVkFsr3_3_1_5DestroyBridge(bridge);
                goto cleanup;
            }
            vkGetDeviceQueue(device, queueFamily, 0u, &queue);
            submitInfo.commandBufferCount = 1u;
            submitInfo.pCommandBuffers = &commandBuffer;
            if (!expect(vkQueueSubmit(queue, 1u, &submitInfo, VK_NULL_HANDLE) == VK_SUCCESS,
                        "submit public SDK imported-image dispatch") ||
                !expect(vkQueueWaitIdle(queue) == VK_SUCCESS,
                        "finish public SDK imported-image dispatch")) {
                cleanupPortable();
                ffxVkFsr3_3_1_5DestroyBridge(bridge);
                goto cleanup;
            }
            cleanupPortable();
        }
        {
            /* A direct Vulkan scheduler blob must take precedence over the
             * legacy upscaler name catalogue.  Deliberately leave the name
             * empty: success proves the generic SPIR-V path was selected. */
            const auto& module = FFX_VK_FSR3_3_1_6_FRAMEGEN_EMBEDDED_SPIRV[0];
            FfxShaderBlob shaderBlob{};
            shaderBlob.data = reinterpret_cast<const uint8_t*>(module.words);
            shaderBlob.size = static_cast<uint32_t>(module.wordCount * sizeof(uint32_t));
            shaderBlob.entryName = "CS";
            FfxPipelineDescription description{};
            FfxPipelineState pipeline{};
            if (!expect(ffxVkFsr3_3_1_5BridgeCreatePipeline(
                            &backend, &shaderBlob, &description, 1u, &pipeline) == FFX_OK,
                        "create direct-SPIR-V SDK callback pipeline") ||
                !expect(pipeline.pipeline != nullptr, "direct-SPIR-V pipeline handle")) {
                ffxVkFsr3_3_1_5BridgeDestroyPipeline(&backend, &pipeline, 1u);
                ffxVkFsr3_3_1_5DestroyBridge(bridge);
                goto cleanup;
            }
            ffxVkFsr3_3_1_5BridgeDestroyPipeline(&backend, &pipeline, 1u);
        }
        for (const wchar_t* name : kPipelineNames) {
            FfxPipelineDescription description{};
            FfxPipelineState pipeline{};
            std::wcsncpy(description.name, name, 63u);
            if (!expect(ffxVkFsr3_3_1_5BridgeCreatePipeline(&backend, nullptr, &description, 1u, &pipeline) == FFX_OK,
                        "create SDK callback pipeline") ||
                !expect(pipeline.pipeline != nullptr, "pipeline handle") ||
                !expect(pipeline.srvTextureCount + pipeline.uavTextureCount + pipeline.constCount > 0u,
                        "reflected bindings")) {
                ffxVkFsr3_3_1_5BridgeDestroyPipeline(&backend, &pipeline, 1u);
                ffxVkFsr3_3_1_5DestroyBridge(bridge);
                goto cleanup;
            }
            if (!expect(ffxVkFsr3_3_1_5BridgeDestroyPipeline(&backend, &pipeline, 1u) == FFX_OK,
                        "destroy SDK callback pipeline")) {
                ffxVkFsr3_3_1_5DestroyBridge(bridge);
                goto cleanup;
            }
        }
        {
            FfxCreateResourceDescription resourceDescription{};
            FfxResourceInternal resource{};
            resourceDescription.heapInfo = FfxResourceHeapPlacementInfo::InitDefault();
            resourceDescription.resourceDescription.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
            resourceDescription.resourceDescription.format = FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT;
            resourceDescription.resourceDescription.width = 64u;
            resourceDescription.resourceDescription.height = 32u;
            resourceDescription.resourceDescription.depth = 1u;
            resourceDescription.resourceDescription.mipCount = 2u;
            resourceDescription.resourceDescription.usage = FFX_API_RESOURCE_USAGE_UAV;
            resourceDescription.initialState = FFX_API_RESOURCE_STATE_UNORDERED_ACCESS;
            resourceDescription.initData.type = FFX_RESOURCE_INIT_DATA_TYPE_UNINITIALIZED;
            if (!expect(ffxVkFsr3_3_1_5BridgeCreateResource(
                            &backend, &resourceDescription, 1u, &resource) == FFX_OK,
                        "create SDK-owned texture") ||
                !expect(resource.internalIndex > 0, "resource handle") ||
                !expect(ffxVkFsr3_3_1_5BridgeGetResource(&backend, resource).resource != nullptr,
                        "resource export") ||
                !expect(ffxVkFsr3_3_1_5BridgeDestroyResource(
                            &backend, resource, 1u) == FFX_OK,
                        "destroy SDK-owned texture")) {
                ffxVkFsr3_3_1_5DestroyBridge(bridge);
                goto cleanup;
            }
        }
        {
            FfxCreateResourceDescription initializedImage{};
            FfxResourceInternal resource{};
            VkCommandPool commandPool = VK_NULL_HANDLE;
            VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
            VkQueue queue = VK_NULL_HANDLE;
            VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            poolInfo.queueFamilyIndex = queueFamily;
            poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            initializedImage.heapInfo = FfxResourceHeapPlacementInfo::InitDefault();
            initializedImage.resourceDescription.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
            initializedImage.resourceDescription.format = FFX_API_SURFACE_FORMAT_R32_UINT;
            initializedImage.resourceDescription.width = 1u;
            initializedImage.resourceDescription.height = 1u;
            initializedImage.resourceDescription.depth = 1u;
            initializedImage.resourceDescription.mipCount = 1u;
            initializedImage.initialState = FFX_API_RESOURCE_STATE_COMPUTE_READ;
            initializedImage.initData.type = FFX_RESOURCE_INIT_DATA_TYPE_VALUE;
            initializedImage.initData.size = sizeof(uint32_t);
            initializedImage.initData.value = 0x5au;
            if (!expect(ffxVkFsr3_3_1_5BridgeCreateResource(
                            &backend, &initializedImage, 1u, &resource) == FFX_OK,
                        "queue initialized image upload") ||
                !expect(vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) == VK_SUCCESS,
                        "create initialization command pool")) {
                ffxVkFsr3_3_1_5DestroyBridge(bridge);
                goto cleanup;
            }
            allocateInfo.commandPool = commandPool;
            allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocateInfo.commandBufferCount = 1u;
            if (!expect(vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer) == VK_SUCCESS,
                        "allocate initialization command buffer") ||
                !expect(vkBeginCommandBuffer(commandBuffer, &beginInfo) == VK_SUCCESS,
                        "begin initialization command buffer") ||
                !expect(ffxVkFsr3_3_1_5BridgeExecuteGpuJobs(
                            &backend, commandBuffer, 1u) == FFX_OK,
                        "record initialized image upload") ||
                !expect(vkEndCommandBuffer(commandBuffer) == VK_SUCCESS,
                        "end initialization command buffer")) {
                vkDestroyCommandPool(device, commandPool, nullptr);
                ffxVkFsr3_3_1_5DestroyBridge(bridge);
                goto cleanup;
            }
            vkGetDeviceQueue(device, queueFamily, 0u, &queue);
            submitInfo.commandBufferCount = 1u;
            submitInfo.pCommandBuffers = &commandBuffer;
            if (!expect(vkQueueSubmit(queue, 1u, &submitInfo, VK_NULL_HANDLE) == VK_SUCCESS,
                        "submit initialized image upload") ||
                !expect(vkQueueWaitIdle(queue) == VK_SUCCESS,
                        "finish initialized image upload") ||
                !expect(ffxVkFsr3_3_1_5BridgeDestroyResource(
                            &backend, resource, 1u) == FFX_OK,
                        "destroy initialized image")) {
                vkDestroyCommandPool(device, commandPool, nullptr);
                ffxVkFsr3_3_1_5DestroyBridge(bridge);
                goto cleanup;
            }
            vkDestroyCommandPool(device, commandPool, nullptr);
        }
        {
            FfxFsr3UpscalerContextDescription description{};
            FfxFsr3UpscalerContext context{};
            VkCommandPool commandPool = VK_NULL_HANDLE;
            VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
            VkQueue queue = VK_NULL_HANDLE;
            VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            description.backendInterface = backend;
            description.maxRenderSize = {64u, 64u};
            description.maxUpscaleSize = {128u, 128u};
            description.flags = FFX_FSR3UPSCALER_ENABLE_HIGH_DYNAMIC_RANGE |
                                FFX_FSR3UPSCALER_ENABLE_AUTO_EXPOSURE;
            poolInfo.queueFamilyIndex = queueFamily;
            poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            if (!expect(ffxFsr3UpscalerContextCreate(&context, &description) == FFX_OK,
                        "create SDK host context with Vulkan resources") ||
                !expect(vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) == VK_SUCCESS,
                        "create SDK initialization command pool")) {
                ffxFsr3UpscalerContextDestroy(&context);
                ffxVkFsr3_3_1_5DestroyBridge(bridge);
                goto cleanup;
            }
            allocateInfo.commandPool = commandPool;
            allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocateInfo.commandBufferCount = 1u;
            if (!expect(vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer) == VK_SUCCESS,
                        "allocate SDK initialization command buffer") ||
                !expect(vkBeginCommandBuffer(commandBuffer, &beginInfo) == VK_SUCCESS,
                        "begin SDK initialization command buffer") ||
                !expect(ffxVkFsr3_3_1_5BridgeExecuteGpuJobs(&backend, commandBuffer, 1u) == FFX_OK,
                        "record all SDK initialization uploads") ||
                !expect(vkEndCommandBuffer(commandBuffer) == VK_SUCCESS,
                        "end SDK initialization command buffer")) {
                vkDestroyCommandPool(device, commandPool, nullptr);
                ffxFsr3UpscalerContextDestroy(&context);
                ffxVkFsr3_3_1_5DestroyBridge(bridge);
                goto cleanup;
            }
            vkGetDeviceQueue(device, queueFamily, 0u, &queue);
            submitInfo.commandBufferCount = 1u;
            submitInfo.pCommandBuffers = &commandBuffer;
            if (!expect(vkQueueSubmit(queue, 1u, &submitInfo, VK_NULL_HANDLE) == VK_SUCCESS,
                        "submit SDK initialization uploads") ||
                !expect(vkQueueWaitIdle(queue) == VK_SUCCESS,
                        "finish SDK initialization uploads")) {
                vkDestroyCommandPool(device, commandPool, nullptr);
                ffxVkFsr3_3_1_5DestroyBridge(bridge);
                goto cleanup;
            }

            /* The SDK itself owns 19 persistent resources.  These nine
             * bridge-owned images model an application's current-frame and
             * shared-resource imports, letting this test exercise the full
             * public scheduler against Vulkan rather than merely its init
             * uploads. */
            FfxFsr3UpscalerSharedResourceDescriptions shared{};
            FfxResourceInternal frameResources[9]{};
            FfxApiResource frameImages[9]{};
            const auto makeFrameImage = [&](uint32_t index,
                                            const FfxCreateResourceDescription& source,
                                            const void* initialData = nullptr,
                                            size_t initialSize = 0u) -> bool {
                FfxCreateResourceDescription resourceDescription = source;
                resourceDescription.heapInfo = FfxResourceHeapPlacementInfo::InitDefault();
                resourceDescription.initData = initialData ?
                    FfxResourceInitData::FfxResourceInitBuffer(initialSize, initialData) :
                    FfxResourceInitData{FFX_RESOURCE_INIT_DATA_TYPE_UNINITIALIZED};
                return ffxVkFsr3_3_1_5BridgeCreateResource(
                           &backend, &resourceDescription, 1u, &frameResources[index]) == FFX_OK &&
                       (frameImages[index] = ffxVkFsr3_3_1_5BridgeGetResource(
                            &backend, frameResources[index])).resource != nullptr;
            };
            const auto textureDescription = [](uint32_t width, uint32_t height,
                                               FfxApiSurfaceFormat format,
                                               FfxApiResourceUsage usage,
                                               FfxApiResourceState state) {
                FfxCreateResourceDescription resourceDescription{};
                resourceDescription.heapInfo = FfxResourceHeapPlacementInfo::InitDefault();
                resourceDescription.resourceDescription.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
                resourceDescription.resourceDescription.format = format;
                resourceDescription.resourceDescription.width = width;
                resourceDescription.resourceDescription.height = height;
                resourceDescription.resourceDescription.depth = 1u;
                resourceDescription.resourceDescription.mipCount = 1u;
                resourceDescription.resourceDescription.usage = usage;
                resourceDescription.initialState = state;
                resourceDescription.initData.type = FFX_RESOURCE_INIT_DATA_TYPE_UNINITIALIZED;
                return resourceDescription;
            };
            const std::vector<uint16_t> colorData(64u * 64u * 4u, 0x3c00u);
            const std::vector<uint32_t> depthData(64u * 64u, 0x3f000000u);
            const std::vector<uint16_t> motionData(64u * 64u * 2u, 0u);
            const std::vector<uint8_t> maskData(64u * 64u, 0u);
            const std::vector<uint16_t> outputPoison(128u * 128u * 4u, 0xbc00u);
            if (!expect(ffxFsr3UpscalerGetSharedResourceDescriptions(&context, &shared) == FFX_OK,
                        "get SDK shared resource descriptions") ||
                !expect(makeFrameImage(0u, textureDescription(64u, 64u,
                            FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT,
                            FFX_API_RESOURCE_USAGE_READ_ONLY,
                            FFX_API_RESOURCE_STATE_COMPUTE_READ),
                            colorData.data(), colorData.size() * sizeof(colorData[0])), "create current color") ||
                !expect(makeFrameImage(1u, textureDescription(64u, 64u,
                            FFX_API_SURFACE_FORMAT_R32_FLOAT,
                            FFX_API_RESOURCE_USAGE_READ_ONLY,
                            FFX_API_RESOURCE_STATE_COMPUTE_READ),
                            depthData.data(), depthData.size() * sizeof(depthData[0])), "create current depth") ||
                !expect(makeFrameImage(2u, textureDescription(64u, 64u,
                            FFX_API_SURFACE_FORMAT_R16G16_FLOAT,
                            FFX_API_RESOURCE_USAGE_READ_ONLY,
                            FFX_API_RESOURCE_STATE_COMPUTE_READ),
                            motionData.data(), motionData.size() * sizeof(motionData[0])), "create current motion") ||
                !expect(makeFrameImage(3u, textureDescription(64u, 64u,
                            FFX_API_SURFACE_FORMAT_R8_UNORM,
                            FFX_API_RESOURCE_USAGE_READ_ONLY,
                            FFX_API_RESOURCE_STATE_COMPUTE_READ),
                            maskData.data(), maskData.size()), "create reactive mask") ||
                !expect(makeFrameImage(4u, textureDescription(64u, 64u,
                            FFX_API_SURFACE_FORMAT_R8_UNORM,
                            FFX_API_RESOURCE_USAGE_READ_ONLY,
                            FFX_API_RESOURCE_STATE_COMPUTE_READ),
                            maskData.data(), maskData.size()), "create composition mask") ||
                !expect(makeFrameImage(5u, shared.dilatedDepth), "create shared dilated depth") ||
                !expect(makeFrameImage(6u, shared.dilatedMotionVectors), "create shared dilated motion") ||
                !expect(makeFrameImage(7u, shared.reconstructedPrevNearestDepth),
                        "create shared nearest depth") ||
                !expect(makeFrameImage(8u, textureDescription(128u, 128u,
                            FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT,
                            FFX_API_RESOURCE_USAGE_UAV,
                            FFX_API_RESOURCE_STATE_UNORDERED_ACCESS),
                            outputPoison.data(), outputPoison.size() * sizeof(outputPoison[0])),
                        "create upscaled output")) {
                vkDestroyCommandPool(device, commandPool, nullptr);
                ffxFsr3UpscalerContextDestroy(&context);
                ffxVkFsr3_3_1_5DestroyBridge(bridge);
                goto cleanup;
            }

            if (!expect(vkResetCommandPool(device, commandPool, 0u) == VK_SUCCESS,
                        "reset frame-input upload command pool") ||
                !expect(vkBeginCommandBuffer(commandBuffer, &beginInfo) == VK_SUCCESS,
                        "begin frame-input upload command buffer") ||
                !expect(ffxVkFsr3_3_1_5BridgeExecuteGpuJobs(
                            &backend, commandBuffer, 1u) == FFX_OK,
                        "record deterministic frame-input uploads") ||
                !expect(vkEndCommandBuffer(commandBuffer) == VK_SUCCESS,
                        "end frame-input upload command buffer") ||
                !expect(vkQueueSubmit(queue, 1u, &submitInfo, VK_NULL_HANDLE) == VK_SUCCESS,
                        "submit deterministic frame-input uploads") ||
                !expect(vkQueueWaitIdle(queue) == VK_SUCCESS,
                        "finish deterministic frame-input uploads")) {
                vkDestroyCommandPool(device, commandPool, nullptr);
                ffxFsr3UpscalerContextDestroy(&context);
                ffxVkFsr3_3_1_5DestroyBridge(bridge);
                goto cleanup;
            }

            FfxFsr3UpscalerDispatchDescription dispatch{};
            dispatch.commandList = reinterpret_cast<FfxCommandList>(commandBuffer);
            dispatch.color = frameImages[0];
            dispatch.depth = frameImages[1];
            dispatch.motionVectors = frameImages[2];
            dispatch.reactive = frameImages[3];
            dispatch.transparencyAndComposition = frameImages[4];
            dispatch.dilatedDepth = frameImages[5];
            dispatch.dilatedMotionVectors = frameImages[6];
            dispatch.reconstructedPrevNearestDepth = frameImages[7];
            dispatch.output = frameImages[8];
            dispatch.motionVectorScale = {64.0f, 64.0f};
            dispatch.renderSize = {64u, 64u};
            dispatch.upscaleSize = {128u, 128u};
            dispatch.frameTimeDelta = 16.6667f;
            dispatch.preExposure = 1.0f;
            dispatch.reset = true;
            dispatch.cameraNear = 0.1f;
            dispatch.cameraFar = 1000.0f;
            dispatch.cameraFovAngleVertical = 1.0471975512f;
            dispatch.viewSpaceToMetersFactor = 1.0f;
            if (!expect(vkResetCommandPool(device, commandPool, 0u) == VK_SUCCESS,
                        "reset SDK dispatch command pool") ||
                !expect(vkBeginCommandBuffer(commandBuffer, &beginInfo) == VK_SUCCESS,
                        "begin SDK dispatch command buffer") ||
                !expect(ffxFsr3UpscalerContextDispatch(&context, &dispatch) == FFX_OK,
                        "record first public SDK Vulkan dispatch") ||
                !expect(vkEndCommandBuffer(commandBuffer) == VK_SUCCESS,
                        "end SDK dispatch command buffer") ||
                !expect(vkQueueSubmit(queue, 1u, &submitInfo, VK_NULL_HANDLE) == VK_SUCCESS,
                        "submit first public SDK Vulkan dispatch") ||
                !expect(vkQueueWaitIdle(queue) == VK_SUCCESS,
                        "finish first public SDK Vulkan dispatch")) {
                vkDestroyCommandPool(device, commandPool, nullptr);
                ffxFsr3UpscalerContextDestroy(&context);
                ffxVkFsr3_3_1_5DestroyBridge(bridge);
                goto cleanup;
            }
            dispatch.reset = false;
            dispatch.enableSharpening = true;
            dispatch.sharpness = 0.25f;
            if (!expect(vkResetCommandPool(device, commandPool, 0u) == VK_SUCCESS,
                        "reset temporal SDK dispatch command pool") ||
                !expect(vkBeginCommandBuffer(commandBuffer, &beginInfo) == VK_SUCCESS,
                        "begin temporal SDK dispatch command buffer") ||
                !expect(ffxFsr3UpscalerContextDispatch(&context, &dispatch) == FFX_OK,
                        "record temporal sharpened public SDK Vulkan dispatch") ||
                !expect(vkEndCommandBuffer(commandBuffer) == VK_SUCCESS,
                        "end temporal SDK dispatch command buffer") ||
                !expect(vkQueueSubmit(queue, 1u, &submitInfo, VK_NULL_HANDLE) == VK_SUCCESS,
                        "submit temporal sharpened public SDK Vulkan dispatch") ||
                !expect(vkQueueWaitIdle(queue) == VK_SUCCESS,
                        "finish temporal sharpened public SDK Vulkan dispatch")) {
                vkDestroyCommandPool(device, commandPool, nullptr);
                ffxFsr3UpscalerContextDestroy(&context);
                ffxVkFsr3_3_1_5DestroyBridge(bridge);
                goto cleanup;
            }
            if (!expect(verify_rgba16f_output(
                            physical, device, queue, commandPool,
                            ffxVkFsr3_3_1_5BridgeGetNativeImage(bridge, frameImages[8].resource),
                            128u, 128u), "verify reconstructed SDK output")) {
                vkDestroyCommandPool(device, commandPool, nullptr);
                ffxFsr3UpscalerContextDestroy(&context);
                ffxVkFsr3_3_1_5DestroyBridge(bridge);
                goto cleanup;
            }
            if (!expect(ffxFsr3UpscalerContextDestroy(&context) == FFX_OK,
                        "destroy SDK host context")) {
                vkDestroyCommandPool(device, commandPool, nullptr);
                ffxVkFsr3_3_1_5DestroyBridge(bridge);
                goto cleanup;
            }
            for (const FfxResourceInternal resource : frameResources) {
                if (!expect(ffxVkFsr3_3_1_5BridgeDestroyResource(
                                &backend, resource, 1u) == FFX_OK,
                            "destroy bridge-owned dispatch resource")) {
                    vkDestroyCommandPool(device, commandPool, nullptr);
                    ffxVkFsr3_3_1_5DestroyBridge(bridge);
                    goto cleanup;
                }
            }
            vkDestroyCommandPool(device, commandPool, nullptr);
        }
        ffxVkFsr3_3_1_5DestroyBridge(bridge);
    }
    if (validation.warnings.load() == 0u && validation.errors.load() == 0u) {
        std::puts("FSR3.1.5 SDK Vulkan bridge test passed (11 pipelines; reset+temporal frames; validation clean)");
        result = 0;
    } else {
        std::fprintf(stderr, "FSR3.1.5 bridge validation failed: warnings=%u errors=%u\n",
                     validation.warnings.load(), validation.errors.load());
    }

cleanup:
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
        vkDestroyDevice(device, nullptr);
    }
    if (messenger != VK_NULL_HANDLE && destroyMessenger)
        destroyMessenger(instance, messenger, nullptr);
    vkDestroyInstance(instance, nullptr);
    return result;
}
