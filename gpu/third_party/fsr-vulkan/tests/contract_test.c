/*
 * Copyright (c) 2026 Q2RTX FSR Vulkan contributors
 * SPDX-License-Identifier: MIT
 */

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "ffx_vk_portable.h"

static FfxVkPortableImage make_image(uintptr_t handle, VkFormat format, uint32_t width, uint32_t height, VkImageUsageFlags usage)
{
    FfxVkPortableImage image = {
        .structSize = sizeof(image),
        .image = (VkImage)handle,
        .format = format,
        .extent = {width, height},
        .mipCount = 1,
        .arrayLayers = 1,
        .usage = usage,
        .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
        .state = FFX_VK_PORTABLE_RESOURCE_STATE_COMPUTE_READ
    };
    return image;
}

static void test_upscale_contract(void)
{
    FfxVkPortableUpscaleCreateInfo createInfo = {
        .structSize = sizeof(createInfo),
        .flags = FFX_VK_PORTABLE_CONTEXT_HDR_COLOR_INPUT | FFX_VK_PORTABLE_CONTEXT_DEPTH_INVERTED,
        .maxRenderSize = {2560, 1440},
        .maxOutputSize = {3840, 2160}
    };
    FfxVkPortableUpscaleDispatchInfo dispatchInfo = {
        .structSize = sizeof(dispatchInfo),
        .commandBuffer = (VkCommandBuffer)(uintptr_t)1,
        .color = make_image(2, VK_FORMAT_R16G16B16A16_SFLOAT, 1920, 1080, VK_IMAGE_USAGE_SAMPLED_BIT),
        .depth = make_image(3, VK_FORMAT_R32_SFLOAT, 1920, 1080, VK_IMAGE_USAGE_SAMPLED_BIT),
        .motionVectors = make_image(4, VK_FORMAT_R16G16_SFLOAT, 1920, 1080, VK_IMAGE_USAGE_SAMPLED_BIT),
        .exposure = make_image(0, VK_FORMAT_UNDEFINED, 0, 0, 0),
        .reactiveMask = make_image(0, VK_FORMAT_UNDEFINED, 0, 0, 0),
        .transparencyAndCompositionMask = make_image(0, VK_FORMAT_UNDEFINED, 0, 0, 0),
        .output = make_image(5, VK_FORMAT_R16G16B16A16_SFLOAT, 3840, 2160, VK_IMAGE_USAGE_STORAGE_BIT),
        .jitterOffset = {0.25f, -0.25f},
        .motionVectorScale = {1920.0f, 1080.0f},
        .renderSize = {1920, 1080},
        .outputSize = {3840, 2160},
        .frameTimeMilliseconds = 16.6667f,
        .preExposure = 1.0f,
        .cameraNear = 0.1f,
        .cameraFar = 1000.0f,
        .cameraVerticalFovRadians = 1.2f,
        .viewSpaceToMeters = 1.0f,
        .sharpness = 0.2f,
        .enableSharpening = VK_TRUE,
        .reset = VK_FALSE,
        .frameId = 7
    };

    assert(ffxVkPortableValidateUpscaleCreateInfo(&createInfo) == 0);
    assert(ffxVkPortableValidateUpscaleDispatchInfo(&createInfo, &dispatchInfo) == 0);

    dispatchInfo.sharpness = 2.0f;
    assert(ffxVkPortableValidateUpscaleDispatchInfo(&createInfo, &dispatchInfo) & FFX_VK_PORTABLE_VALIDATION_SHARPNESS);
    dispatchInfo.sharpness = 0.2f;

    dispatchInfo.output.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    assert(ffxVkPortableValidateUpscaleDispatchInfo(&createInfo, &dispatchInfo) & FFX_VK_PORTABLE_VALIDATION_RESOURCE_USAGE);
}

static void test_frame_generation_contract(void)
{
    FfxVkPortableFrameGenerationCreateInfo createInfo = {
        .structSize = sizeof(createInfo),
        .flags = FFX_VK_PORTABLE_CONTEXT_HDR_COLOR_INPUT | FFX_VK_PORTABLE_CONTEXT_DEPTH_INVERTED,
        .maxRenderSize = {2560, 1440},
        .displaySize = {3840, 2160},
        .interpolationSourceFormat = VK_FORMAT_R16G16B16A16_SFLOAT,
        .outputFormat = VK_FORMAT_R16G16B16A16_SFLOAT
    };
    FfxVkPortableFrameGenerationPrepareInfo prepareInfo = {
        .structSize = sizeof(prepareInfo),
        .commandBuffer = (VkCommandBuffer)(uintptr_t)1,
        .depth = make_image(2, VK_FORMAT_R32_SFLOAT, 1920, 1080, VK_IMAGE_USAGE_SAMPLED_BIT),
        .motionVectors = make_image(3, VK_FORMAT_R16G16_SFLOAT, 1920, 1080, VK_IMAGE_USAGE_SAMPLED_BIT),
        .renderSize = {1920, 1080},
        .jitterOffset = {0.25f, -0.25f},
        .motionVectorScale = {1920.0f, 1080.0f},
        .frameTimeMilliseconds = 16.6667f,
        .cameraNear = 0.1f,
        .cameraFar = 1000.0f,
        .cameraVerticalFovRadians = 1.2f,
        .viewSpaceToMeters = 1.0f,
        .minLuminance = 0.0f,
        .maxLuminance = 1000.0f,
        .transferFunction = FFX_VK_PORTABLE_TRANSFER_FUNCTION_SCRGB,
        .cameraUp = {0.0f, 0.0f, 1.0f},
        .cameraRight = {1.0f, 0.0f, 0.0f},
        .cameraForward = {0.0f, 1.0f, 0.0f},
        .frameId = 7
    };
    FfxVkPortableFrameGenerationDispatchInfo dispatchInfo = {
        .structSize = sizeof(dispatchInfo),
        .commandBuffer = (VkCommandBuffer)(uintptr_t)1,
        .currentColor = make_image(4, VK_FORMAT_R16G16B16A16_SFLOAT, 3840, 2160,
                                   VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT),
        .hudlessColor = make_image(5, VK_FORMAT_R16G16B16A16_SFLOAT, 3840, 2160,
                                   VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT),
        .distortionField = make_image(0, VK_FORMAT_UNDEFINED, 0, 0, 0),
        .output = make_image(6, VK_FORMAT_R16G16B16A16_SFLOAT, 3840, 2160,
                             VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT),
        .displaySize = {3840, 2160},
        .interpolationRect = {0, 0, 3840, 2160},
        .frameTimeMilliseconds = 16.6667f,
        .cameraNear = 0.1f,
        .cameraFar = 1000.0f,
        .cameraVerticalFovRadians = 1.2f,
        .viewSpaceToMeters = 1.0f,
        .minLuminance = 0.0f,
        .maxLuminance = 1000.0f,
        .transferFunction = FFX_VK_PORTABLE_TRANSFER_FUNCTION_SCRGB,
        .frameId = 7
    };

    assert(ffxVkPortableValidateFrameGenerationCreateInfo(&createInfo) == 0);
    assert(ffxVkPortableValidateFrameGenerationPrepareInfo(&createInfo, &prepareInfo) == 0);
    assert(ffxVkPortableValidateFrameGenerationDispatchInfo(&createInfo, &dispatchInfo) == 0);

    dispatchInfo.hudlessColor.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    assert(ffxVkPortableValidateFrameGenerationDispatchInfo(&createInfo, &dispatchInfo) &
           FFX_VK_PORTABLE_VALIDATION_RESOURCE_USAGE);
    dispatchInfo.hudlessColor.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    dispatchInfo.interpolationRect.width = 4000;
    assert(ffxVkPortableValidateFrameGenerationDispatchInfo(&createInfo, &dispatchInfo) & FFX_VK_PORTABLE_VALIDATION_INTERPOLATION_RECT);
}

static void test_upscale_context_lifecycle_errors(void)
{
    FfxVkPortableDeviceInfo deviceInfo = {0};
    FfxVkPortableUpscaleCreateInfo createInfo = {
        .structSize = sizeof(createInfo),
        .maxRenderSize = {64, 64},
        .maxOutputSize = {128, 128}
    };
    FfxVkPortableUpscaleContext* context = (FfxVkPortableUpscaleContext*)(uintptr_t)1;

    assert(ffxVkPortableUpscaleContextCreate(NULL, &createInfo, &context) ==
           FFX_VK_PORTABLE_ERROR_INVALID_POINTER);
    assert(context == NULL);

    context = (FfxVkPortableUpscaleContext*)(uintptr_t)1;
    assert(ffxVkPortableUpscaleContextCreate(&deviceInfo, &createInfo, &context) ==
           FFX_VK_PORTABLE_ERROR_INVALID_STRUCT_SIZE);
    assert(context == NULL);
    assert(ffxVkPortableUpscaleContextRecordDispatch(NULL, NULL) ==
           FFX_VK_PORTABLE_ERROR_INVALID_POINTER);
    assert(ffxVkPortableUpscaleContextDestroy(NULL) == FFX_VK_PORTABLE_OK);
}

int main(void)
{
    test_upscale_contract();
    test_frame_generation_contract();
    test_upscale_context_lifecycle_errors();
    puts("portable FSR Vulkan contract tests passed");
    return 0;
}
