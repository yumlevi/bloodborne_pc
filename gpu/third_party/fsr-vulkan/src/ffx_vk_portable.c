/*
 * Copyright (c) 2026 Q2RTX FSR Vulkan contributors
 * SPDX-License-Identifier: MIT
 */

#include "ffx_vk_portable.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define FFX_VK_PORTABLE_PI 3.14159265358979323846f

static VkBool32 is_finite(float value)
{
    return isfinite(value) ? VK_TRUE : VK_FALSE;
}

static VkBool32 extent_is_valid(FfxVkPortableExtent2D extent)
{
    return extent.width > 0 && extent.height > 0;
}

static VkBool32 extent_fits(FfxVkPortableExtent2D extent, FfxVkPortableExtent2D container)
{
    return extent.width <= container.width && extent.height <= container.height;
}

static VkBool32 image_is_present(const FfxVkPortableImage* image)
{
    return image != NULL && image->image != VK_NULL_HANDLE;
}

static uint64_t validate_required_image(
    const FfxVkPortableImage* image,
    FfxVkPortableExtent2D requiredExtent,
    VkImageUsageFlags requiredUsage)
{
    uint64_t issues = FFX_VK_PORTABLE_VALIDATION_NONE;

    if (image == NULL || image->structSize < sizeof(*image))
        return FFX_VK_PORTABLE_VALIDATION_STRUCT_SIZE;
    if (image->image == VK_NULL_HANDLE)
        issues |= FFX_VK_PORTABLE_VALIDATION_NULL_HANDLE;
    if (image->format == VK_FORMAT_UNDEFINED)
        issues |= FFX_VK_PORTABLE_VALIDATION_RESOURCE_FORMAT_UNDEFINED;
    if (!extent_is_valid(image->extent) || !extent_fits(requiredExtent, image->extent))
        issues |= FFX_VK_PORTABLE_VALIDATION_RESOURCE_TOO_SMALL;
    if ((image->usage & requiredUsage) != requiredUsage)
        issues |= FFX_VK_PORTABLE_VALIDATION_RESOURCE_USAGE;
    if (image->mipCount == 0 || image->arrayLayers != 1 || image->aspect == 0 ||
        image->state < FFX_VK_PORTABLE_RESOURCE_STATE_UNDEFINED ||
        image->state > FFX_VK_PORTABLE_RESOURCE_STATE_PRESENT)
        issues |= FFX_VK_PORTABLE_VALIDATION_RESOURCE_USAGE;

    return issues;
}

static uint64_t validate_optional_sampled_image(
    const FfxVkPortableImage* image,
    FfxVkPortableExtent2D requiredExtent)
{
    if (image == NULL || image->structSize < sizeof(*image))
        return FFX_VK_PORTABLE_VALIDATION_STRUCT_SIZE;
    if (!image_is_present(image))
        return FFX_VK_PORTABLE_VALIDATION_NONE;
    return validate_required_image(image, requiredExtent, VK_IMAGE_USAGE_SAMPLED_BIT);
}

static uint64_t validate_camera(
    float cameraNear,
    float cameraFar,
    float verticalFov,
    float viewSpaceToMeters,
    VkBool32 infiniteDepth)
{
    uint64_t issues = FFX_VK_PORTABLE_VALIDATION_NONE;
    if (!is_finite(cameraNear) || !is_finite(cameraFar) || !is_finite(verticalFov) || !is_finite(viewSpaceToMeters))
        issues |= FFX_VK_PORTABLE_VALIDATION_NONFINITE_VALUE;
    if (cameraNear <= 0.0f || verticalFov <= 0.0f || verticalFov >= FFX_VK_PORTABLE_PI || viewSpaceToMeters <= 0.0f)
        issues |= FFX_VK_PORTABLE_VALIDATION_CAMERA;
    if (!infiniteDepth && cameraFar <= cameraNear)
        issues |= FFX_VK_PORTABLE_VALIDATION_CAMERA;
    return issues;
}

static VkBool32 has_compute_queue(VkPhysicalDevice physicalDevice)
{
    uint32_t count = 0;
    VkQueueFamilyProperties* properties;
    VkBool32 found = VK_FALSE;

    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, NULL);
    if (count == 0)
        return VK_FALSE;

    properties = (VkQueueFamilyProperties*)calloc(count, sizeof(*properties));
    if (properties == NULL)
        return VK_FALSE;

    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, properties);
    for (uint32_t index = 0; index < count; ++index) {
        if (properties[index].queueCount > 0 && (properties[index].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
            found = VK_TRUE;
            break;
        }
    }
    free(properties);
    return found;
}

static VkBool32 format_supports(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatFeatureFlags features)
{
    VkFormatProperties properties;
    vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &properties);
    return (properties.optimalTilingFeatures & features) == features;
}

static VkBool32 has_required_fsr3_storage_formats(VkPhysicalDevice physicalDevice)
{
    const VkFormatFeatureFlags sampled =
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    const VkFormatFeatureFlags storage =
        sampled | VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    const VkFormatFeatureFlags colorStorage = storage | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
    const struct {
        VkFormat format;
        VkFormatFeatureFlags features;
    } requirements[] = {
        {VK_FORMAT_R8_UNORM, colorStorage},
        {VK_FORMAT_R8G8_UNORM, storage},
        {VK_FORMAT_R8G8B8A8_UNORM, storage},
        {VK_FORMAT_R16_SNORM, sampled},
        {VK_FORMAT_R16_SFLOAT, colorStorage},
        {VK_FORMAT_R16G16_SFLOAT, colorStorage},
        {VK_FORMAT_R16G16B16A16_SFLOAT, colorStorage},
        {VK_FORMAT_R16G16_SINT, storage},
        {VK_FORMAT_R32_SFLOAT, colorStorage},
        {VK_FORMAT_R32G32_SFLOAT, sampled},
        {VK_FORMAT_R32G32B32A32_SFLOAT, storage},
        {VK_FORMAT_R32_UINT, storage | VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT}
    };

    for (size_t index = 0; index < sizeof(requirements) / sizeof(requirements[0]); ++index) {
        if (!format_supports(physicalDevice, requirements[index].format, requirements[index].features))
            return VK_FALSE;
    }
    return VK_TRUE;
}

FfxVkPortableResult ffxVkPortableQueryDeviceCapabilities(
    VkPhysicalDevice physicalDevice,
    FfxVkPortableDeviceCapabilities* capabilities)
{
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceSubgroupProperties subgroupProperties = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES
    };
    VkPhysicalDeviceSubgroupSizeControlProperties subgroupSizeProperties = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES,
        .pNext = &subgroupProperties
    };
    VkPhysicalDeviceShaderIntegerDotProductProperties dotProperties = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_PROPERTIES,
        .pNext = &subgroupSizeProperties
    };
    VkPhysicalDeviceProperties2 properties2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &dotProperties
    };
    VkPhysicalDeviceVulkan12Features features12 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES
    };
    VkPhysicalDeviceSynchronization2Features synchronization2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
        .pNext = &features12
    };
    VkPhysicalDeviceShaderIntegerDotProductFeatures dotFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_FEATURES,
        .pNext = &synchronization2
    };
    VkPhysicalDeviceSubgroupSizeControlFeatures subgroupSizeFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES,
        .pNext = &dotFeatures
    };
    VkPhysicalDeviceFeatures2 features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &subgroupSizeFeatures
    };
    const VkSubgroupFeatureFlags requiredSubgroupOperations =
        VK_SUBGROUP_FEATURE_BASIC_BIT |
        VK_SUBGROUP_FEATURE_VOTE_BIT |
        VK_SUBGROUP_FEATURE_ARITHMETIC_BIT |
        VK_SUBGROUP_FEATURE_BALLOT_BIT |
        VK_SUBGROUP_FEATURE_SHUFFLE_BIT |
        VK_SUBGROUP_FEATURE_QUAD_BIT;

    if (physicalDevice == VK_NULL_HANDLE || capabilities == NULL)
        return FFX_VK_PORTABLE_ERROR_INVALID_POINTER;
    if (capabilities->structSize < sizeof(*capabilities))
        return FFX_VK_PORTABLE_ERROR_INVALID_STRUCT_SIZE;

    memset(&properties, 0, sizeof(properties));
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);

    memset(capabilities, 0, sizeof(*capabilities));
    capabilities->structSize = sizeof(*capabilities);
    capabilities->apiVersion = properties.apiVersion;
    capabilities->driverVersion = properties.driverVersion;
    capabilities->vendorId = properties.vendorID;
    capabilities->deviceId = properties.deviceID;
    capabilities->hasComputeQueue = has_compute_queue(physicalDevice);
    capabilities->requiredFsr3StorageFormats = has_required_fsr3_storage_formats(physicalDevice);

    if (VK_API_VERSION_MAJOR(properties.apiVersion) < 1 ||
        (VK_API_VERSION_MAJOR(properties.apiVersion) == 1 && VK_API_VERSION_MINOR(properties.apiVersion) < 2)) {
        return FFX_VK_PORTABLE_OK;
    }

    vkGetPhysicalDeviceProperties2(physicalDevice, &properties2);
    vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);

    capabilities->subgroupSize = subgroupProperties.subgroupSize;
    capabilities->minSubgroupSize = subgroupSizeProperties.minSubgroupSize;
    capabilities->maxSubgroupSize = subgroupSizeProperties.maxSubgroupSize;
    capabilities->subgroupOperations =
        (subgroupProperties.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT) != 0 &&
        (subgroupProperties.supportedOperations & requiredSubgroupOperations) == requiredSubgroupOperations;
    capabilities->subgroupSizeControl = subgroupSizeFeatures.subgroupSizeControl;
    capabilities->computeFullSubgroups = subgroupSizeFeatures.computeFullSubgroups;
    capabilities->shaderFloat16 = features12.shaderFloat16;
    capabilities->shaderInt8 = features12.shaderInt8;
    capabilities->storageBuffer8BitAccess = features12.storageBuffer8BitAccess;
    capabilities->uniformAndStorageBuffer8BitAccess = features12.uniformAndStorageBuffer8BitAccess;
    capabilities->shaderIntegerDotProduct = dotFeatures.shaderIntegerDotProduct;
    capabilities->acceleratedSignedInt8DotProduct = dotProperties.integerDotProduct8BitSignedAccelerated;
    capabilities->timelineSemaphore = features12.timelineSemaphore;
    capabilities->synchronization2 = synchronization2.synchronization2;

    capabilities->fsr3ComputePrerequisites =
        capabilities->hasComputeQueue &&
        capabilities->subgroupOperations &&
        capabilities->requiredFsr3StorageFormats &&
        /* The app-controlled upscaled-output UAV has no GLSL format
         * qualifier, so every generated accumulate shader declares
         * StorageImageWriteWithoutFormat. */
        features2.features.shaderStorageImageWriteWithoutFormat;
    capabilities->fsr3FrameGenerationPrerequisites =
        capabilities->fsr3ComputePrerequisites &&
        capabilities->timelineSemaphore;
    capabilities->fsr4Int8Prerequisites =
        capabilities->hasComputeQueue &&
        capabilities->shaderInt8 &&
        capabilities->storageBuffer8BitAccess &&
        capabilities->uniformAndStorageBuffer8BitAccess &&
        capabilities->shaderIntegerDotProduct &&
        capabilities->acceleratedSignedInt8DotProduct &&
        capabilities->subgroupOperations;

    return FFX_VK_PORTABLE_OK;
}

uint64_t ffxVkPortableValidateUpscaleCreateInfo(
    const FfxVkPortableUpscaleCreateInfo* createInfo)
{
    uint64_t issues = FFX_VK_PORTABLE_VALIDATION_NONE;
    if (createInfo == NULL || createInfo->structSize < sizeof(*createInfo))
        return FFX_VK_PORTABLE_VALIDATION_STRUCT_SIZE;
    if (!extent_is_valid(createInfo->maxRenderSize) || !extent_is_valid(createInfo->maxOutputSize))
        issues |= FFX_VK_PORTABLE_VALIDATION_ZERO_EXTENT;
    if (!extent_fits(createInfo->maxRenderSize, createInfo->maxOutputSize))
        issues |= FFX_VK_PORTABLE_VALIDATION_OUTPUT_SIZE_EXCEEDS_MAXIMUM;
    return issues;
}

uint64_t ffxVkPortableValidateUpscaleDispatchInfo(
    const FfxVkPortableUpscaleCreateInfo* createInfo,
    const FfxVkPortableUpscaleDispatchInfo* dispatchInfo)
{
    uint64_t issues = ffxVkPortableValidateUpscaleCreateInfo(createInfo);
    VkBool32 infiniteDepth;

    if (dispatchInfo == NULL || dispatchInfo->structSize < sizeof(*dispatchInfo))
        return issues | FFX_VK_PORTABLE_VALIDATION_STRUCT_SIZE;
    if (createInfo == NULL || createInfo->structSize < sizeof(*createInfo))
        return issues;

    if (dispatchInfo->commandBuffer == VK_NULL_HANDLE)
        issues |= FFX_VK_PORTABLE_VALIDATION_NULL_HANDLE;
    if (!extent_is_valid(dispatchInfo->renderSize) || !extent_is_valid(dispatchInfo->outputSize))
        issues |= FFX_VK_PORTABLE_VALIDATION_ZERO_EXTENT;
    if (!extent_fits(dispatchInfo->renderSize, createInfo->maxRenderSize))
        issues |= FFX_VK_PORTABLE_VALIDATION_RENDER_SIZE_EXCEEDS_MAXIMUM;
    if (!extent_fits(dispatchInfo->outputSize, createInfo->maxOutputSize))
        issues |= FFX_VK_PORTABLE_VALIDATION_OUTPUT_SIZE_EXCEEDS_MAXIMUM;

    issues |= validate_required_image(&dispatchInfo->color, dispatchInfo->renderSize, VK_IMAGE_USAGE_SAMPLED_BIT);
    issues |= validate_required_image(&dispatchInfo->depth, dispatchInfo->renderSize, VK_IMAGE_USAGE_SAMPLED_BIT);
    issues |= validate_required_image(&dispatchInfo->motionVectors, dispatchInfo->renderSize, VK_IMAGE_USAGE_SAMPLED_BIT);
    issues |= validate_required_image(&dispatchInfo->output, dispatchInfo->outputSize, VK_IMAGE_USAGE_STORAGE_BIT);
    issues |= validate_optional_sampled_image(&dispatchInfo->exposure, (FfxVkPortableExtent2D){1, 1});
    issues |= validate_optional_sampled_image(&dispatchInfo->reactiveMask, dispatchInfo->renderSize);
    issues |= validate_optional_sampled_image(&dispatchInfo->transparencyAndCompositionMask, dispatchInfo->renderSize);

    if (!is_finite(dispatchInfo->jitterOffset.x) || !is_finite(dispatchInfo->jitterOffset.y) ||
        !is_finite(dispatchInfo->motionVectorScale.x) || !is_finite(dispatchInfo->motionVectorScale.y) ||
        !is_finite(dispatchInfo->frameTimeMilliseconds) || !is_finite(dispatchInfo->preExposure) ||
        !is_finite(dispatchInfo->sharpness))
        issues |= FFX_VK_PORTABLE_VALIDATION_NONFINITE_VALUE;
    if (dispatchInfo->frameTimeMilliseconds <= 0.0f)
        issues |= FFX_VK_PORTABLE_VALIDATION_FRAME_TIME;
    if (dispatchInfo->preExposure <= 0.0f)
        issues |= FFX_VK_PORTABLE_VALIDATION_PRE_EXPOSURE;
    if (dispatchInfo->sharpness < 0.0f || dispatchInfo->sharpness > 1.0f)
        issues |= FFX_VK_PORTABLE_VALIDATION_SHARPNESS;

    infiniteDepth = (createInfo->flags & FFX_VK_PORTABLE_CONTEXT_DEPTH_INFINITE) != 0;
    issues |= validate_camera(
        dispatchInfo->cameraNear,
        dispatchInfo->cameraFar,
        dispatchInfo->cameraVerticalFovRadians,
        dispatchInfo->viewSpaceToMeters,
        infiniteDepth);
    return issues;
}

uint64_t ffxVkPortableValidateFrameGenerationCreateInfo(
    const FfxVkPortableFrameGenerationCreateInfo* createInfo)
{
    uint64_t issues = FFX_VK_PORTABLE_VALIDATION_NONE;
    if (createInfo == NULL || createInfo->structSize < sizeof(*createInfo))
        return FFX_VK_PORTABLE_VALIDATION_STRUCT_SIZE;
    if (!extent_is_valid(createInfo->maxRenderSize) || !extent_is_valid(createInfo->displaySize))
        issues |= FFX_VK_PORTABLE_VALIDATION_ZERO_EXTENT;
    if (!extent_fits(createInfo->maxRenderSize, createInfo->displaySize))
        issues |= FFX_VK_PORTABLE_VALIDATION_OUTPUT_SIZE_EXCEEDS_MAXIMUM;
    if (createInfo->interpolationSourceFormat == VK_FORMAT_UNDEFINED || createInfo->outputFormat == VK_FORMAT_UNDEFINED)
        issues |= FFX_VK_PORTABLE_VALIDATION_RESOURCE_FORMAT_UNDEFINED;
    return issues;
}

uint64_t ffxVkPortableValidateFrameGenerationPrepareInfo(
    const FfxVkPortableFrameGenerationCreateInfo* createInfo,
    const FfxVkPortableFrameGenerationPrepareInfo* prepareInfo)
{
    uint64_t issues = ffxVkPortableValidateFrameGenerationCreateInfo(createInfo);
    VkBool32 infiniteDepth;

    if (prepareInfo == NULL || prepareInfo->structSize < sizeof(*prepareInfo))
        return issues | FFX_VK_PORTABLE_VALIDATION_STRUCT_SIZE;
    if (createInfo == NULL || createInfo->structSize < sizeof(*createInfo))
        return issues;

    if (prepareInfo->commandBuffer == VK_NULL_HANDLE)
        issues |= FFX_VK_PORTABLE_VALIDATION_NULL_HANDLE;
    if (!extent_is_valid(prepareInfo->renderSize))
        issues |= FFX_VK_PORTABLE_VALIDATION_ZERO_EXTENT;
    if (!extent_fits(prepareInfo->renderSize, createInfo->maxRenderSize))
        issues |= FFX_VK_PORTABLE_VALIDATION_RENDER_SIZE_EXCEEDS_MAXIMUM;
    issues |= validate_required_image(&prepareInfo->depth, prepareInfo->renderSize, VK_IMAGE_USAGE_SAMPLED_BIT);
    issues |= validate_required_image(&prepareInfo->motionVectors, prepareInfo->renderSize, VK_IMAGE_USAGE_SAMPLED_BIT);

    if (!is_finite(prepareInfo->jitterOffset.x) || !is_finite(prepareInfo->jitterOffset.y) ||
        !is_finite(prepareInfo->motionVectorScale.x) || !is_finite(prepareInfo->motionVectorScale.y) ||
        !is_finite(prepareInfo->frameTimeMilliseconds))
        issues |= FFX_VK_PORTABLE_VALIDATION_NONFINITE_VALUE;
    if (prepareInfo->frameTimeMilliseconds <= 0.0f)
        issues |= FFX_VK_PORTABLE_VALIDATION_FRAME_TIME;
    if (!is_finite(prepareInfo->minLuminance) || !is_finite(prepareInfo->maxLuminance))
        issues |= FFX_VK_PORTABLE_VALIDATION_NONFINITE_VALUE;
    if (prepareInfo->minLuminance < 0.0f || prepareInfo->maxLuminance <= prepareInfo->minLuminance ||
        prepareInfo->transferFunction < FFX_VK_PORTABLE_TRANSFER_FUNCTION_SRGB ||
        prepareInfo->transferFunction > FFX_VK_PORTABLE_TRANSFER_FUNCTION_SCRGB)
        issues |= FFX_VK_PORTABLE_VALIDATION_LUMINANCE_RANGE;

    infiniteDepth = (createInfo->flags & FFX_VK_PORTABLE_CONTEXT_DEPTH_INFINITE) != 0;
    issues |= validate_camera(
        prepareInfo->cameraNear,
        prepareInfo->cameraFar,
        prepareInfo->cameraVerticalFovRadians,
        prepareInfo->viewSpaceToMeters,
        infiniteDepth);
    return issues;
}

uint64_t ffxVkPortableValidateFrameGenerationDispatchInfo(
    const FfxVkPortableFrameGenerationCreateInfo* createInfo,
    const FfxVkPortableFrameGenerationDispatchInfo* dispatchInfo)
{
    uint64_t issues = ffxVkPortableValidateFrameGenerationCreateInfo(createInfo);
    const FfxVkPortableImage* interpolationSource;
    VkBool32 infiniteDepth;

    if (dispatchInfo == NULL || dispatchInfo->structSize < sizeof(*dispatchInfo))
        return issues | FFX_VK_PORTABLE_VALIDATION_STRUCT_SIZE;
    if (createInfo == NULL || createInfo->structSize < sizeof(*createInfo))
        return issues;

    if (dispatchInfo->commandBuffer == VK_NULL_HANDLE)
        issues |= FFX_VK_PORTABLE_VALIDATION_NULL_HANDLE;
    if (!extent_is_valid(dispatchInfo->displaySize))
        issues |= FFX_VK_PORTABLE_VALIDATION_ZERO_EXTENT;
    if (!extent_fits(dispatchInfo->displaySize, createInfo->displaySize))
        issues |= FFX_VK_PORTABLE_VALIDATION_OUTPUT_SIZE_EXCEEDS_MAXIMUM;

    interpolationSource = image_is_present(&dispatchInfo->hudlessColor)
        ? &dispatchInfo->hudlessColor
        : &dispatchInfo->currentColor;
    /* FI copies the selected source into its temporal history and samples its
     * output in later passes, so these usages are part of the Vulkan contract,
     * not merely conveniences for the caller. */
    issues |= validate_required_image(interpolationSource, dispatchInfo->displaySize,
                                      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    issues |= validate_required_image(&dispatchInfo->output, dispatchInfo->displaySize,
                                      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
    issues |= validate_optional_sampled_image(&dispatchInfo->distortionField, dispatchInfo->displaySize);

    if (dispatchInfo->interpolationRect.x < 0 || dispatchInfo->interpolationRect.y < 0 ||
        dispatchInfo->interpolationRect.width == 0 || dispatchInfo->interpolationRect.height == 0 ||
        (uint64_t)(uint32_t)dispatchInfo->interpolationRect.x + dispatchInfo->interpolationRect.width > dispatchInfo->displaySize.width ||
        (uint64_t)(uint32_t)dispatchInfo->interpolationRect.y + dispatchInfo->interpolationRect.height > dispatchInfo->displaySize.height)
        issues |= FFX_VK_PORTABLE_VALIDATION_INTERPOLATION_RECT;

    if (!is_finite(dispatchInfo->frameTimeMilliseconds) || !is_finite(dispatchInfo->minLuminance) ||
        !is_finite(dispatchInfo->maxLuminance))
        issues |= FFX_VK_PORTABLE_VALIDATION_NONFINITE_VALUE;
    if (dispatchInfo->frameTimeMilliseconds <= 0.0f)
        issues |= FFX_VK_PORTABLE_VALIDATION_FRAME_TIME;
    if (dispatchInfo->minLuminance < 0.0f || dispatchInfo->maxLuminance <= dispatchInfo->minLuminance)
        issues |= FFX_VK_PORTABLE_VALIDATION_LUMINANCE_RANGE;
    if (dispatchInfo->transferFunction < FFX_VK_PORTABLE_TRANSFER_FUNCTION_SRGB ||
        dispatchInfo->transferFunction > FFX_VK_PORTABLE_TRANSFER_FUNCTION_SCRGB)
        issues |= FFX_VK_PORTABLE_VALIDATION_LUMINANCE_RANGE;

    infiniteDepth = (createInfo->flags & FFX_VK_PORTABLE_CONTEXT_DEPTH_INFINITE) != 0;
    issues |= validate_camera(
        dispatchInfo->cameraNear,
        dispatchInfo->cameraFar,
        dispatchInfo->cameraVerticalFovRadians,
        dispatchInfo->viewSpaceToMeters,
        infiniteDepth);
    return issues;
}

const char* ffxVkPortableValidationIssueName(uint64_t singleIssueBit)
{
    switch (singleIssueBit) {
    case FFX_VK_PORTABLE_VALIDATION_NONE: return "none";
    case FFX_VK_PORTABLE_VALIDATION_STRUCT_SIZE: return "struct size";
    case FFX_VK_PORTABLE_VALIDATION_NULL_HANDLE: return "null handle";
    case FFX_VK_PORTABLE_VALIDATION_ZERO_EXTENT: return "zero extent";
    case FFX_VK_PORTABLE_VALIDATION_RENDER_SIZE_EXCEEDS_MAXIMUM: return "render size exceeds maximum";
    case FFX_VK_PORTABLE_VALIDATION_OUTPUT_SIZE_EXCEEDS_MAXIMUM: return "output size exceeds maximum";
    case FFX_VK_PORTABLE_VALIDATION_RESOURCE_TOO_SMALL: return "resource too small";
    case FFX_VK_PORTABLE_VALIDATION_RESOURCE_FORMAT_UNDEFINED: return "resource format undefined";
    case FFX_VK_PORTABLE_VALIDATION_RESOURCE_USAGE: return "resource usage";
    case FFX_VK_PORTABLE_VALIDATION_NONFINITE_VALUE: return "non-finite value";
    case FFX_VK_PORTABLE_VALIDATION_FRAME_TIME: return "frame time";
    case FFX_VK_PORTABLE_VALIDATION_PRE_EXPOSURE: return "pre-exposure";
    case FFX_VK_PORTABLE_VALIDATION_SHARPNESS: return "sharpness";
    case FFX_VK_PORTABLE_VALIDATION_CAMERA: return "camera";
    case FFX_VK_PORTABLE_VALIDATION_INTERPOLATION_RECT: return "interpolation rectangle";
    case FFX_VK_PORTABLE_VALIDATION_LUMINANCE_RANGE: return "luminance range";
    default: return "unknown";
    }
}

#if !defined(FFX_VK_PORTABLE_HAS_FSR3_UPSCALER)
FfxVkPortableResult ffxVkPortableUpscaleContextCreate(
    const FfxVkPortableDeviceInfo* deviceInfo,
    const FfxVkPortableUpscaleCreateInfo* createInfo,
    FfxVkPortableUpscaleContext** context)
{
    if (context == NULL)
        return FFX_VK_PORTABLE_ERROR_INVALID_POINTER;
    *context = NULL;
    if (deviceInfo == NULL || createInfo == NULL)
        return FFX_VK_PORTABLE_ERROR_INVALID_POINTER;
    if (deviceInfo->structSize < sizeof(*deviceInfo) || createInfo->structSize < sizeof(*createInfo))
        return FFX_VK_PORTABLE_ERROR_INVALID_STRUCT_SIZE;
    return FFX_VK_PORTABLE_ERROR_UNSUPPORTED;
}

FfxVkPortableResult ffxVkPortableUpscaleContextRecordDispatch(
    FfxVkPortableUpscaleContext* context,
    const FfxVkPortableUpscaleDispatchInfo* dispatchInfo)
{
    if (context == NULL || dispatchInfo == NULL)
        return FFX_VK_PORTABLE_ERROR_INVALID_POINTER;
    if (dispatchInfo->structSize < sizeof(*dispatchInfo))
        return FFX_VK_PORTABLE_ERROR_INVALID_STRUCT_SIZE;
    return FFX_VK_PORTABLE_ERROR_UNSUPPORTED;
}

FfxVkPortableResult ffxVkPortableUpscaleContextDestroy(
    FfxVkPortableUpscaleContext* context)
{
    return context == NULL
        ? FFX_VK_PORTABLE_OK
        : FFX_VK_PORTABLE_ERROR_UNSUPPORTED;
}

FfxVkPortableResult ffxVkPortableFrameGenerationContextCreate(
    const FfxVkPortableDeviceInfo* deviceInfo,
    const FfxVkPortableFrameGenerationCreateInfo* createInfo,
    FfxVkPortableFrameGenerationContext** context)
{
    if (context == NULL)
        return FFX_VK_PORTABLE_ERROR_INVALID_POINTER;
    *context = NULL;
    if (deviceInfo == NULL || createInfo == NULL)
        return FFX_VK_PORTABLE_ERROR_INVALID_POINTER;
    if (deviceInfo->structSize < sizeof(*deviceInfo) || createInfo->structSize < sizeof(*createInfo))
        return FFX_VK_PORTABLE_ERROR_INVALID_STRUCT_SIZE;
    return FFX_VK_PORTABLE_ERROR_UNSUPPORTED;
}

FfxVkPortableResult ffxVkPortableFrameGenerationContextPrepare(
    FfxVkPortableFrameGenerationContext* context,
    const FfxVkPortableFrameGenerationPrepareInfo* prepareInfo,
    const FfxVkPortableImage* currentColor)
{
    if (context == NULL || prepareInfo == NULL || currentColor == NULL)
        return FFX_VK_PORTABLE_ERROR_INVALID_POINTER;
    if (prepareInfo->structSize < sizeof(*prepareInfo) || currentColor->structSize < sizeof(*currentColor))
        return FFX_VK_PORTABLE_ERROR_INVALID_STRUCT_SIZE;
    return FFX_VK_PORTABLE_ERROR_UNSUPPORTED;
}

FfxVkPortableResult ffxVkPortableFrameGenerationContextRecordDispatch(
    FfxVkPortableFrameGenerationContext* context,
    const FfxVkPortableFrameGenerationDispatchInfo* dispatchInfo)
{
    if (context == NULL || dispatchInfo == NULL)
        return FFX_VK_PORTABLE_ERROR_INVALID_POINTER;
    if (dispatchInfo->structSize < sizeof(*dispatchInfo))
        return FFX_VK_PORTABLE_ERROR_INVALID_STRUCT_SIZE;
    return FFX_VK_PORTABLE_ERROR_UNSUPPORTED;
}

FfxVkPortableResult ffxVkPortableFrameGenerationContextDestroy(
    FfxVkPortableFrameGenerationContext* context)
{
    return context == NULL
        ? FFX_VK_PORTABLE_OK
        : FFX_VK_PORTABLE_ERROR_UNSUPPORTED;
}
#endif
