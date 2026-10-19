/*
 * Copyright (c) 2026 Q2RTX FSR Vulkan contributors
 * SPDX-License-Identifier: MIT
 */

#include "ffx_vk_fsr3_3_1_5_bridge.h"

#include "ffx_interface.h"
#include "ffx_fsr3upscaler.h"

#include <new>
#include <cstring>

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
extern "C" FfxErrorCode ffxVkFsr3_3_1_5BridgeGetEffectGpuMemoryUsage(
    FfxInterface*, FfxUInt32, FfxApiEffectMemoryUsage*);
extern "C" FfxErrorCode ffxVkFsr3_3_1_5BridgeStageConstantBufferData(
    FfxInterface*, void*, FfxUInt32, FfxConstantBuffer*);

struct FfxVkFsr3_3_1_5UpscalerContext {
    FfxVkFsr3_3_1_5Bridge* bridge = nullptr;
    FfxInterface backend{};
    FfxFsr3UpscalerContext context{};
    bool created = false;
};

namespace {

static FfxVkFsr3_3_1_5Result result_from_ffx(FfxErrorCode result)
{
    if (result == FFX_OK)
        return FFX_VK_FSR3_3_1_5_OK;
    if (result == static_cast<FfxErrorCode>(FFX_ERROR_OUT_OF_MEMORY))
        return FFX_VK_FSR3_3_1_5_ERROR_OUT_OF_MEMORY;
    if (result == static_cast<FfxErrorCode>(FFX_ERROR_INVALID_POINTER) ||
        result == static_cast<FfxErrorCode>(FFX_ERROR_INVALID_ARGUMENT))
        return FFX_VK_FSR3_3_1_5_ERROR_INVALID_ARGUMENT;
    return FFX_VK_FSR3_3_1_5_ERROR_BACKEND;
}

static FfxVkFsr3_3_1_5SharedResourceDescription export_description(
    const FfxApiResourceDescription& description)
{
    FfxVkFsr3_3_1_5SharedResourceDescription exported{};
    switch (description.format) {
    case FFX_API_SURFACE_FORMAT_R32_FLOAT:
        exported.format = VK_FORMAT_R32_SFLOAT;
        break;
    case FFX_API_SURFACE_FORMAT_R16G16_FLOAT:
        exported.format = VK_FORMAT_R16G16_SFLOAT;
        break;
    case FFX_API_SURFACE_FORMAT_R32_UINT:
        exported.format = VK_FORMAT_R32_UINT;
        break;
    default:
        exported.format = VK_FORMAT_UNDEFINED;
        break;
    }
    exported.width = description.width;
    exported.height = description.height;
    exported.mipCount = description.mipCount;
    return exported;
}

static void initialize_backend(FfxVkFsr3_3_1_5UpscalerContext* context)
{
    FfxInterface& backend = context->backend;
    backend.device = context->bridge;
    backend.fpGetSDKVersion = ffxVkFsr3_3_1_5BridgeGetSDKVersion;
    backend.fpCreateBackendContext = ffxVkFsr3_3_1_5BridgeCreateBackendContext;
    backend.fpDestroyBackendContext = ffxVkFsr3_3_1_5BridgeDestroyBackendContext;
    backend.fpGetDeviceCapabilities = ffxVkFsr3_3_1_5BridgeGetDeviceCapabilities;
    backend.fpGetEffectGpuMemoryUsage = ffxVkFsr3_3_1_5BridgeGetEffectGpuMemoryUsage;
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
}

} // namespace

extern "C" FfxVkFsr3_3_1_5Result ffxVkFsr3_3_1_5UpscalerContextCreate(
    const FfxVkFsr3_3_1_5UpscalerCreateInfo* createInfo,
    FfxVkFsr3_3_1_5UpscalerContext** outContext)
{
    if (!createInfo || !outContext || createInfo->physicalDevice == VK_NULL_HANDLE ||
        createInfo->device == VK_NULL_HANDLE || createInfo->maxRenderWidth == 0u ||
        createInfo->maxRenderHeight == 0u || createInfo->maxUpscaleWidth == 0u ||
        createInfo->maxUpscaleHeight == 0u)
        return FFX_VK_FSR3_3_1_5_ERROR_INVALID_ARGUMENT;
    *outContext = nullptr;
    FfxVkFsr3_3_1_5UpscalerContext* owned = new (std::nothrow) FfxVkFsr3_3_1_5UpscalerContext;
    if (!owned)
        return FFX_VK_FSR3_3_1_5_ERROR_OUT_OF_MEMORY;
    owned->bridge = ffxVkFsr3_3_1_5CreateBridgeWithPhysicalDevice(
        createInfo->physicalDevice, createInfo->device, nullptr);
    if (!owned->bridge) {
        delete owned;
        return FFX_VK_FSR3_3_1_5_ERROR_OUT_OF_MEMORY;
    }
    initialize_backend(owned);
    FfxFsr3UpscalerContextDescription description{};
    description.backendInterface = owned->backend;
    description.maxRenderSize = {createInfo->maxRenderWidth, createInfo->maxRenderHeight};
    description.maxUpscaleSize = {createInfo->maxUpscaleWidth, createInfo->maxUpscaleHeight};
    if (createInfo->hdrColorInput)
        description.flags |= FFX_FSR3UPSCALER_ENABLE_HIGH_DYNAMIC_RANGE;
    if (createInfo->autoExposure)
        description.flags |= FFX_FSR3UPSCALER_ENABLE_AUTO_EXPOSURE;
    const FfxErrorCode result = ffxFsr3UpscalerContextCreate(&owned->context, &description);
    if (result != FFX_OK) {
        ffxVkFsr3_3_1_5DestroyBridge(owned->bridge);
        delete owned;
        return result_from_ffx(result);
    }
    owned->created = true;
    *outContext = owned;
    return FFX_VK_FSR3_3_1_5_OK;
}

extern "C" FfxVkFsr3_3_1_5Bridge* ffxVkFsr3_3_1_5UpscalerContextGetBridge(
    FfxVkFsr3_3_1_5UpscalerContext* context)
{
    return context && context->created ? context->bridge : nullptr;
}

extern "C" FfxVkFsr3_3_1_5Result ffxVkFsr3_3_1_5UpscalerContextGetSharedResourceDescriptions(
    FfxVkFsr3_3_1_5UpscalerContext* context,
    FfxVkFsr3_3_1_5SharedResourceDescriptions* outDescriptions)
{
    if (!context || !context->created || !outDescriptions)
        return FFX_VK_FSR3_3_1_5_ERROR_INVALID_ARGUMENT;
    FfxFsr3UpscalerSharedResourceDescriptions shared{};
    const FfxErrorCode result = ffxFsr3UpscalerGetSharedResourceDescriptions(
        &context->context, &shared);
    if (result != FFX_OK)
        return result_from_ffx(result);
    outDescriptions->dilatedDepth = export_description(shared.dilatedDepth.resourceDescription);
    outDescriptions->dilatedMotionVectors =
        export_description(shared.dilatedMotionVectors.resourceDescription);
    outDescriptions->reconstructedPrevNearestDepth =
        export_description(shared.reconstructedPrevNearestDepth.resourceDescription);
    return FFX_VK_FSR3_3_1_5_OK;
}

extern "C" FfxVkFsr3_3_1_5Result ffxVkFsr3_3_1_5UpscalerContextGetMemoryUsage(
    FfxVkFsr3_3_1_5UpscalerContext* context,
    FfxVkFsr3_3_1_5MemoryUsage* outUsage)
{
    if (!context || !context->created || !outUsage)
        return FFX_VK_FSR3_3_1_5_ERROR_INVALID_ARGUMENT;
    FfxApiEffectMemoryUsage usage{};
    const FfxErrorCode result = ffxFsr3UpscalerContextGetGpuMemoryUsage(
        &context->context, &usage);
    if (result != FFX_OK)
        return result_from_ffx(result);
    outUsage->totalUsageInBytes = usage.totalUsageInBytes;
    outUsage->aliasableUsageInBytes = usage.aliasableUsageInBytes;
    return FFX_VK_FSR3_3_1_5_OK;
}

extern "C" FfxVkFsr3_3_1_5Result ffxVkFsr3_3_1_5UpscalerContextRecordDispatch(
    FfxVkFsr3_3_1_5UpscalerContext* context,
    const FfxVkFsr3_3_1_5UpscalerDispatchInfo* dispatchInfo)
{
    if (!context || !context->created || !dispatchInfo ||
        dispatchInfo->commandBuffer == VK_NULL_HANDLE ||
        dispatchInfo->color.resource == nullptr || dispatchInfo->depth.resource == nullptr ||
        dispatchInfo->motionVectors.resource == nullptr || dispatchInfo->output.resource == nullptr ||
        dispatchInfo->dilatedDepth.resource == nullptr ||
        dispatchInfo->dilatedMotionVectors.resource == nullptr ||
        dispatchInfo->reconstructedPrevNearestDepth.resource == nullptr ||
        dispatchInfo->renderWidth == 0u || dispatchInfo->renderHeight == 0u ||
        dispatchInfo->upscaleWidth == 0u || dispatchInfo->upscaleHeight == 0u ||
        dispatchInfo->preExposure <= 0.0f || dispatchInfo->frameTimeMilliseconds <= 0.0f)
        return FFX_VK_FSR3_3_1_5_ERROR_INVALID_ARGUMENT;
    ffxVkFsr3_3_1_5BridgeBeginFrame(context->bridge, dispatchInfo->frameId);
    FfxFsr3UpscalerDispatchDescription dispatch{};
    dispatch.commandList = reinterpret_cast<FfxCommandList>(dispatchInfo->commandBuffer);
    dispatch.color = ffxVkFsr3_3_1_5BridgeResolveResource(context->bridge, dispatchInfo->color);
    dispatch.depth = ffxVkFsr3_3_1_5BridgeResolveResource(context->bridge, dispatchInfo->depth);
    dispatch.motionVectors = ffxVkFsr3_3_1_5BridgeResolveResource(
        context->bridge, dispatchInfo->motionVectors);
    dispatch.exposure = ffxVkFsr3_3_1_5BridgeResolveResource(context->bridge, dispatchInfo->exposure);
    dispatch.reactive = ffxVkFsr3_3_1_5BridgeResolveResource(context->bridge, dispatchInfo->reactive);
    dispatch.transparencyAndComposition = ffxVkFsr3_3_1_5BridgeResolveResource(
        context->bridge, dispatchInfo->transparencyAndComposition);
    dispatch.dilatedDepth = ffxVkFsr3_3_1_5BridgeResolveResource(
        context->bridge, dispatchInfo->dilatedDepth);
    dispatch.dilatedMotionVectors = ffxVkFsr3_3_1_5BridgeResolveResource(
        context->bridge, dispatchInfo->dilatedMotionVectors);
    dispatch.reconstructedPrevNearestDepth = ffxVkFsr3_3_1_5BridgeResolveResource(
        context->bridge, dispatchInfo->reconstructedPrevNearestDepth);
    dispatch.output = ffxVkFsr3_3_1_5BridgeResolveResource(context->bridge, dispatchInfo->output);
    dispatch.jitterOffset = {dispatchInfo->jitterOffsetX, dispatchInfo->jitterOffsetY};
    dispatch.motionVectorScale = {dispatchInfo->motionVectorScaleX, dispatchInfo->motionVectorScaleY};
    dispatch.renderSize = {dispatchInfo->renderWidth, dispatchInfo->renderHeight};
    dispatch.upscaleSize = {dispatchInfo->upscaleWidth, dispatchInfo->upscaleHeight};
    dispatch.enableSharpening = dispatchInfo->enableSharpening == VK_TRUE;
    dispatch.sharpness = dispatchInfo->sharpness;
    dispatch.frameTimeDelta = dispatchInfo->frameTimeMilliseconds;
    dispatch.preExposure = dispatchInfo->preExposure;
    dispatch.reset = dispatchInfo->reset == VK_TRUE;
    dispatch.cameraNear = dispatchInfo->cameraNear;
    dispatch.cameraFar = dispatchInfo->cameraFar;
    dispatch.cameraFovAngleVertical = dispatchInfo->cameraVerticalFovRadians;
    dispatch.viewSpaceToMetersFactor = dispatchInfo->viewSpaceToMeters;
    dispatch.flags = dispatchInfo->flags;
    return result_from_ffx(ffxFsr3UpscalerContextDispatch(&context->context, &dispatch));
}

extern "C" void ffxVkFsr3_3_1_5UpscalerContextRetireFrame(
    FfxVkFsr3_3_1_5UpscalerContext* context, uint64_t completedFrameId)
{
    if (context && context->created)
        ffxVkFsr3_3_1_5BridgeRetireFrame(context->bridge, completedFrameId);
}

extern "C" void ffxVkFsr3_3_1_5UpscalerContextDestroy(
    FfxVkFsr3_3_1_5UpscalerContext* context)
{
    if (!context)
        return;
    if (context->created)
        ffxFsr3UpscalerContextDestroy(&context->context);
    ffxVkFsr3_3_1_5DestroyBridge(context->bridge);
    delete context;
}
