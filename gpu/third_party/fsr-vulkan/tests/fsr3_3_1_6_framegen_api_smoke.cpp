/*
 * Copyright (c) 2026 Q2RTX FSR Vulkan contributors
 * SPDX-License-Identifier: MIT
 */

#include "ffx_vk_fsr3_3_1_5_bridge.h"
#include "ffx_vk_vulkan_compat.h"

#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace {

struct Image {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
};

struct ValidationState {
    uint32_t warnings = 0u;
    uint32_t errors = 0u;
};

VKAPI_ATTR VkBool32 VKAPI_CALL validation_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data, void* user)
{
    ValidationState* state = static_cast<ValidationState*>(user);
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        ++state->warnings;
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        ++state->errors;
    if (severity & (VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT))
        std::fprintf(stderr, "FSR3.1.6 public FI/OF validation: %s\n",
                     data && data->pMessage ? data->pMessage : "(no message)");
    return VK_FALSE;
}

bool has_validation_layer()
{
    uint32_t count = 0u;
    if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS)
        return false;
    std::vector<VkLayerProperties> layers(count);
    if (vkEnumerateInstanceLayerProperties(&count, layers.data()) != VK_SUCCESS)
        return false;
    for (const VkLayerProperties& layer : layers) {
        if (std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0)
            return true;
    }
    return false;
}

bool expect(bool value, const char* what)
{
    if (!value)
        std::fprintf(stderr, "FSR3.1.6 public FI/OF smoke failure: %s\n", what);
    return value;
}

bool choose_compute_device(VkInstance instance, VkPhysicalDevice* physical, uint32_t* queueFamily)
{
    uint32_t count = 0u;
    if (vkEnumeratePhysicalDevices(instance, &count, nullptr) != VK_SUCCESS || count == 0u)
        return false;
    std::vector<VkPhysicalDevice> devices(count);
    if (vkEnumeratePhysicalDevices(instance, &count, devices.data()) != VK_SUCCESS)
        return false;
    for (VkPhysicalDevice device : devices) {
        uint32_t familyCount = 0u;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families.data());
        for (uint32_t index = 0u; index < familyCount; ++index) {
            if (families[index].queueCount && (families[index].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
                *physical = device;
                *queueFamily = index;
                return true;
            }
        }
    }
    return false;
}

bool has_extension(VkPhysicalDevice physical, const char* required)
{
    uint32_t count = 0u;
    if (vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr) != VK_SUCCESS)
        return false;
    std::vector<VkExtensionProperties> extensions(count);
    if (vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, extensions.data()) != VK_SUCCESS)
        return false;
    for (const VkExtensionProperties& extension : extensions) {
        if (std::strcmp(extension.extensionName, required) == 0)
            return true;
    }
    return false;
}

uint32_t find_memory_type(VkPhysicalDevice physical, uint32_t bits)
{
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    for (uint32_t index = 0u; index < memory.memoryTypeCount; ++index) {
        if ((bits & (1u << index)) &&
            (memory.memoryTypes[index].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            return index;
    }
    return UINT32_MAX;
}

bool create_image(VkPhysicalDevice physical, VkDevice device, uint32_t width, uint32_t height,
                  VkFormat format, Image* out)
{
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = {width, height, 1u};
    imageInfo.mipLevels = 1u;
    imageInfo.arrayLayers = 1u;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (vkCreateImage(device, &imageInfo, nullptr, &out->image) != VK_SUCCESS)
        return false;
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, out->image, &requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = find_memory_type(physical, requirements.memoryTypeBits);
    return allocation.memoryTypeIndex != UINT32_MAX &&
           vkAllocateMemory(device, &allocation, nullptr, &out->memory) == VK_SUCCESS &&
           vkBindImageMemory(device, out->image, out->memory, 0u) == VK_SUCCESS;
}

void destroy_image(VkDevice device, Image* image)
{
    if (image->memory)
        vkFreeMemory(device, image->memory, nullptr);
    if (image->image)
        vkDestroyImage(device, image->image, nullptr);
    *image = {};
}

FfxVkFsr3_3_1_6FrameGenerationImage image_info(
    const Image& image, VkFormat format, uint32_t width, uint32_t height, VkImageLayout layout)
{
    FfxVkFsr3_3_1_6FrameGenerationImage result{};
    result.image = image.image;
    result.format = format;
    result.width = width;
    result.height = height;
    result.layout = layout;
    result.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    return result;
}

bool initialize_images(VkCommandBuffer commandBuffer, const Image* images, uint32_t count)
{
    VkImageMemoryBarrier barriers[5]{};
    for (uint32_t index = 0u; index < count; ++index) {
        barriers[index].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barriers[index].dstAccessMask = index == 3u ? VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_SHADER_READ_BIT;
        barriers[index].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barriers[index].newLayout = index == 3u ? VK_IMAGE_LAYOUT_GENERAL :
                                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barriers[index].image = images[index].image;
        barriers[index].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barriers[index].subresourceRange.levelCount = 1u;
        barriers[index].subresourceRange.layerCount = 1u;
    }
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u, 0u, nullptr,
                         0u, nullptr, count, barriers);
    return true;
}

bool record_frame(FfxVkFsr3_3_1_6FrameGenerationContext* context, VkCommandBuffer commandBuffer,
                  const Image* images, VkFormat colorFormat, VkFormat motionFormat,
                  FfxVkFsr3_3_1_6FrameGenerationTransferFunction transferFunction,
                  float maxLuminance,
                  uint64_t frameId, bool reset, bool initialize, bool useDistortion,
                  bool useDefaultInterpolationRect)
{
    if (initialize)
        initialize_images(commandBuffer, images, 5u);
    FfxVkFsr3_3_1_6FrameGenerationPrepareInfo prepare{};
    prepare.commandBuffer = commandBuffer;
    prepare.color = image_info(images[0], colorFormat, 128u, 128u,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    prepare.depth = image_info(images[1], VK_FORMAT_R32_SFLOAT, 64u, 64u,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    prepare.motionVectors = image_info(images[2], motionFormat, 64u, 64u,
                                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    prepare.renderWidth = 64u;
    prepare.renderHeight = 64u;
    prepare.motionVectorScaleX = 64.0f;
    prepare.motionVectorScaleY = 64.0f;
    prepare.frameTimeMilliseconds = 16.6667f;
    prepare.minLuminance = 0.0f;
    prepare.maxLuminance = maxLuminance;
    prepare.transferFunction = transferFunction;
    prepare.cameraNear = 0.1f;
    prepare.cameraFar = 1000.0f;
    prepare.viewSpaceToMeters = 1.0f;
    prepare.cameraVerticalFovRadians = 1.0471975512f;
    prepare.cameraUp[1] = 1.0f;
    prepare.cameraRight[0] = 1.0f;
    prepare.cameraForward[2] = -1.0f;
    prepare.frameId = frameId;
    prepare.reset = reset ? VK_TRUE : VK_FALSE;
    /* The public FI/OF context owns display-sized shared images.  Its scene
     * color input therefore cannot silently change extent between a resize
     * and the caller recreating the context. */
    {
        FfxVkFsr3_3_1_6FrameGenerationPrepareInfo invalid = prepare;
        invalid.color.width = 127u;
        if (ffxVkFsr3_3_1_6FrameGenerationContextRecordPrepare(context, &invalid) !=
            FFX_VK_FSR3_3_1_6_FRAMEGEN_ERROR_INVALID_ARGUMENT)
            return false;
        invalid = prepare;
        invalid.cameraForward[2] = std::numeric_limits<float>::quiet_NaN();
        if (ffxVkFsr3_3_1_6FrameGenerationContextRecordPrepare(context, &invalid) !=
            FFX_VK_FSR3_3_1_6_FRAMEGEN_ERROR_INVALID_ARGUMENT)
            return false;
    }
    if (ffxVkFsr3_3_1_6FrameGenerationContextRecordPrepare(context, &prepare) !=
        FFX_VK_FSR3_3_1_6_FRAMEGEN_OK)
        return false;
    FfxVkFsr3_3_1_6FrameGenerationDispatchInfo dispatch{};
    dispatch.commandBuffer = commandBuffer;
    dispatch.color = prepare.color;
    dispatch.output = image_info(images[3], colorFormat, 128u, 128u,
                                 VK_IMAGE_LAYOUT_GENERAL);
    dispatch.displayWidth = 128u;
    dispatch.displayHeight = 128u;
    dispatch.interpolationWidth = useDefaultInterpolationRect ? 0u : 128u;
    dispatch.interpolationHeight = useDefaultInterpolationRect ? 0u : 128u;
    dispatch.frameTimeMilliseconds = prepare.frameTimeMilliseconds;
    dispatch.cameraNear = prepare.cameraNear;
    dispatch.cameraFar = prepare.cameraFar;
    dispatch.viewSpaceToMeters = prepare.viewSpaceToMeters;
    dispatch.cameraVerticalFovRadians = prepare.cameraVerticalFovRadians;
    dispatch.maxLuminance = maxLuminance;
    dispatch.transferFunction = transferFunction;
    dispatch.frameId = frameId;
    dispatch.reset = reset ? VK_TRUE : VK_FALSE;
    /* The public wrapper must fail closed when a host tries to reuse its
     * source image as a generated-frame target. This otherwise corrupts
     * temporal input/history in the host renderer. */
    {
        FfxVkFsr3_3_1_6FrameGenerationDispatchInfo invalid = dispatch;
        invalid.output.image = images[0].image;
        if (ffxVkFsr3_3_1_6FrameGenerationContextRecordDispatch(context, &invalid) !=
            FFX_VK_FSR3_3_1_6_FRAMEGEN_ERROR_INVALID_ARGUMENT)
            return false;
        invalid = dispatch;
        invalid.cameraVerticalFovRadians = std::numeric_limits<float>::infinity();
        if (ffxVkFsr3_3_1_6FrameGenerationContextRecordDispatch(context, &invalid) !=
            FFX_VK_FSR3_3_1_6_FRAMEGEN_ERROR_INVALID_ARGUMENT)
            return false;
    }
    /* A non-default interpolation rect must be non-empty and inside the
     * fixed context display extent.  These checks run before the SDK records
     * work, which prevents resize glitches from becoming bad generated frames. */
    {
        FfxVkFsr3_3_1_6FrameGenerationDispatchInfo invalid = dispatch;
        invalid.interpolationX = 1u;
        invalid.interpolationWidth = 128u;
        if (ffxVkFsr3_3_1_6FrameGenerationContextRecordDispatch(context, &invalid) !=
            FFX_VK_FSR3_3_1_6_FRAMEGEN_ERROR_INVALID_ARGUMENT)
            return false;
        invalid = dispatch;
        invalid.interpolationWidth = 0u;
        invalid.interpolationHeight = 128u;
        if (ffxVkFsr3_3_1_6FrameGenerationContextRecordDispatch(context, &invalid) !=
            FFX_VK_FSR3_3_1_6_FRAMEGEN_ERROR_INVALID_ARGUMENT)
            return false;
    }
    if (useDistortion) {
        dispatch.distortionField = image_info(images[4], VK_FORMAT_R16G16_SFLOAT, 128u, 128u,
                                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        /* The public field is explicitly float2 UV displacement.  Reject a
         * same-sized image with a superficially plausible but incompatible
         * format before recording the valid external-field dispatch. */
        FfxVkFsr3_3_1_6FrameGenerationDispatchInfo invalid = dispatch;
        invalid.distortionField.format = VK_FORMAT_R16_SFLOAT;
        if (ffxVkFsr3_3_1_6FrameGenerationContextRecordDispatch(context, &invalid) !=
            FFX_VK_FSR3_3_1_6_FRAMEGEN_ERROR_INVALID_ARGUMENT)
            return false;
    }
    return ffxVkFsr3_3_1_6FrameGenerationContextRecordDispatch(context, &dispatch) ==
           FFX_VK_FSR3_3_1_6_FRAMEGEN_OK;
}

} // namespace

int main()
{
#if defined(FSR316_SMOKE_FLOAT16)
    const VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
#else
    const VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
#endif
#if defined(FSR316_SMOKE_MOTION_RGBA16F)
    const VkFormat motionFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
#else
    const VkFormat motionFormat = VK_FORMAT_R16G16_SFLOAT;
#endif
#if defined(FSR316_SMOKE_SCRGB)
    const FfxVkFsr3_3_1_6FrameGenerationTransferFunction transferFunction =
        FFX_VK_FSR3_3_1_6_FRAMEGEN_TRANSFER_SCRGB;
    const float maxLuminance = 1000.0f;
#else
    const FfxVkFsr3_3_1_6FrameGenerationTransferFunction transferFunction =
        FFX_VK_FSR3_3_1_6_FRAMEGEN_TRANSFER_SRGB;
    const float maxLuminance = 1.0f;
#endif
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    PFN_vkDestroyDebugUtilsMessengerEXT destroyMessenger = nullptr;
    FfxVkFsr3_3_1_6FrameGenerationContext* context = nullptr;
    Image images[5]{};
    const float priority = 1.0f;
    uint32_t family = 0u;
    int result = 1;
    ValidationState validation{};
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_2;
    if (!has_validation_layer())
        return 77;
    VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debug.pfnUserCallback = validation_callback;
    debug.pUserData = &validation;
    const char* layers[] = {"VK_LAYER_KHRONOS_validation"};
    const char* instanceExtensions[] = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo = &app;
    instanceInfo.pNext = &debug;
    instanceInfo.enabledLayerCount = 1u;
    instanceInfo.ppEnabledLayerNames = layers;
    instanceInfo.enabledExtensionCount = 1u;
    instanceInfo.ppEnabledExtensionNames = instanceExtensions;
    if (vkCreateInstance(&instanceInfo, nullptr, &instance) != VK_SUCCESS ||
        !choose_compute_device(instance, &physical, &family))
        goto cleanup;
    {
        const auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
        destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (!createMessenger || !destroyMessenger ||
            createMessenger(instance, &debug, nullptr, &messenger) != VK_SUCCESS) {
            result = 77;
            goto cleanup;
        }
    }
    {
        VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = 1u;
        queueInfo.pQueuePriorities = &priority;
        VkPhysicalDeviceFeatures features{};
        VkPhysicalDeviceFeatures supported{};
        vkGetPhysicalDeviceFeatures(physical, &supported);
        features.shaderStorageImageReadWithoutFormat = supported.shaderStorageImageReadWithoutFormat;
        features.shaderStorageImageWriteWithoutFormat = supported.shaderStorageImageWriteWithoutFormat;
        if (!features.shaderStorageImageReadWithoutFormat || !features.shaderStorageImageWriteWithoutFormat) {
            result = 77;
            goto cleanup;
        }
        VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR derivative{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR};
        VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR supportedDerivative{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR};
        const char* extensions[1]{};
        uint32_t extensionCount = 0u;
        if (has_extension(physical, VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME)) {
            VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
            features2.pNext = &supportedDerivative;
            vkGetPhysicalDeviceFeatures2(physical, &features2);
            if (supportedDerivative.computeDerivativeGroupLinear) {
                derivative.computeDerivativeGroupLinear = VK_TRUE;
                extensions[extensionCount++] = VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME;
            }
        }
        VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        deviceInfo.queueCreateInfoCount = 1u;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        deviceInfo.pEnabledFeatures = &features;
        deviceInfo.pNext = extensionCount ? &derivative : nullptr;
        deviceInfo.enabledExtensionCount = extensionCount;
        deviceInfo.ppEnabledExtensionNames = extensionCount ? extensions : nullptr;
        if (vkCreateDevice(physical, &deviceInfo, nullptr, &device) != VK_SUCCESS) {
            result = 77;
            goto cleanup;
        }
    }
    for (uint32_t index = 0u; index < 5u; ++index) {
        const VkFormat formats[] = {colorFormat, VK_FORMAT_R32_SFLOAT,
                                    motionFormat, colorFormat, VK_FORMAT_R16G16_SFLOAT};
        const uint32_t sizes[] = {128u, 64u, 64u, 128u, 128u};
        if (!expect(create_image(physical, device, sizes[index], sizes[index], formats[index], &images[index]),
                    "create public API image"))
            goto cleanup;
    }
    {
        FfxVkFsr3_3_1_6FrameGenerationCreateInfo create{};
        create.physicalDevice = physical;
        create.device = device;
        create.maxRenderWidth = 64u;
        create.maxRenderHeight = 64u;
        create.displayWidth = 128u;
        create.displayHeight = 128u;
        create.colorFormat = colorFormat;
        const FfxVkFsr3_3_1_6FrameGenerationResult createResult =
            ffxVkFsr3_3_1_6FrameGenerationContextCreate(&create, &context);
        if (createResult != FFX_VK_FSR3_3_1_6_FRAMEGEN_OK)
            std::fprintf(stderr, "FSR3.1.6 public FI/OF create result: %d\n", (int)createResult);
        if (!expect(createResult == FFX_VK_FSR3_3_1_6_FRAMEGEN_OK, "create public FI/OF context"))
            goto cleanup;
        FfxVkFsr3_3_1_6FrameGenerationMemoryUsage memoryUsage{};
        if (!expect(ffxVkFsr3_3_1_6FrameGenerationContextGetMemoryUsage(
                        context, &memoryUsage) == FFX_VK_FSR3_3_1_6_FRAMEGEN_OK &&
                    memoryUsage.totalUsageInBytes > 0u &&
                    memoryUsage.totalUsageInBytes >= memoryUsage.aliasableUsageInBytes,
                    "get public FI/OF memory usage"))
            goto cleanup;
    }
    {
        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.queueFamilyIndex = family;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocation.commandPool = pool;
        allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount = 1u;
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        if (!expect(vkCreateCommandPool(device, &poolInfo, nullptr, &pool) == VK_SUCCESS,
                    "create public API command pool"))
            goto cleanup;
        allocation.commandPool = pool;
        if (!expect(vkAllocateCommandBuffers(device, &allocation, &commands) == VK_SUCCESS,
                    "allocate public API command buffer") ||
            !expect(vkBeginCommandBuffer(commands, &begin) == VK_SUCCESS,
                    "begin reset frame") ||
            !expect(record_frame(context, commands, images, colorFormat, motionFormat,
                                 transferFunction, maxLuminance,
                                 1u, true, true, false, false), "record reset frame") ||
            !expect(vkEndCommandBuffer(commands) == VK_SUCCESS, "end reset frame"))
            goto cleanup;
        vkGetDeviceQueue(device, family, 0u, &queue);
        submit.commandBufferCount = 1u;
        submit.pCommandBuffers = &commands;
        if (!expect(vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) == VK_SUCCESS &&
                    vkQueueWaitIdle(queue) == VK_SUCCESS, "submit reset frame") ||
            !expect(vkResetCommandPool(device, pool, 0u) == VK_SUCCESS, "reset temporal pool") ||
            !expect(vkBeginCommandBuffer(commands, &begin) == VK_SUCCESS, "begin temporal frame") ||
            !expect(record_frame(context, commands, images, colorFormat, motionFormat,
                                 transferFunction, maxLuminance,
                                 2u, false, false, true, true), "record temporal frame with distortion field") ||
            !expect(vkEndCommandBuffer(commands) == VK_SUCCESS, "end temporal frame") ||
            !expect(vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) == VK_SUCCESS &&
                    vkQueueWaitIdle(queue) == VK_SUCCESS, "submit temporal frame") ||
            !expect(ffxVkFsr3_3_1_6FrameGenerationContextRetireFrame(context, 2u) ==
                    FFX_VK_FSR3_3_1_6_FRAMEGEN_OK, "retire reset+temporal frames"))
            goto cleanup;
    }
    if (!expect(validation.errors == 0u, "Vulkan validation errors"))
        goto cleanup;
    std::printf("FSR3.1.6 public FI/OF Vulkan API reset+temporal smoke passed (external distortion field; %s color, %s motion, %s; validation warnings=%u)\n",
                colorFormat == VK_FORMAT_R16G16B16A16_SFLOAT ? "RGBA16F" : "RGBA8",
                motionFormat == VK_FORMAT_R16G16B16A16_SFLOAT ? "RGBA16F" : "RG16F",
                transferFunction == FFX_VK_FSR3_3_1_6_FRAMEGEN_TRANSFER_SCRGB ? "scRGB" : "sRGB",
                validation.warnings);
    result = validation.warnings ? 1 : 0;

cleanup:
    if (device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(device);
    ffxVkFsr3_3_1_6FrameGenerationContextDestroy(context);
    for (Image& image : images)
        destroy_image(device, &image);
    if (pool != VK_NULL_HANDLE)
        vkDestroyCommandPool(device, pool, nullptr);
    if (device != VK_NULL_HANDLE)
        vkDestroyDevice(device, nullptr);
    if (messenger != VK_NULL_HANDLE && destroyMessenger)
        destroyMessenger(instance, messenger, nullptr);
    if (instance != VK_NULL_HANDLE)
        vkDestroyInstance(instance, nullptr);
    return result;
}
