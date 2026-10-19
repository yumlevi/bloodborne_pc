/*
 * Copyright (c) 2026 Q2RTX FSR Vulkan contributors
 * SPDX-License-Identifier: MIT
 *
 * Exercises the public SDK 2.3 FI/OF context creation path with the actual
 * fixed-profile Vulkan blob accessors.  This is deliberately a host-graph
 * test: no Vulkan work is recorded until the separate scheduler bridge owns
 * resource allocation, descriptors, barriers, and command-buffer lifetime.
 */

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

#include "ffx_frameinterpolation.h"
#include "ffx_opticalflow.h"

namespace {

constexpr uint32_t kSpirvMagic = 0x07230203u;

struct MockBackend {
    uint32_t contextsCreated = 0;
    uint32_t contextsDestroyed = 0;
    uint32_t resourcesCreated = 0;
    uint32_t resourcesDestroyed = 0;
    uint32_t pipelinesCreated = 0;
    uint32_t pipelinesDestroyed = 0;
    uint32_t spirvPipelinesCreated = 0;
    uint32_t resourcesRegistered = 0;
    uint32_t unregisterCalls = 0;
    uint32_t constantBufferStages = 0;
    uint32_t scheduledJobs = 0;
    uint32_t executeCalls = 0;
    uint32_t lastExecutedJobs = 0;
    int32_t nextResource = 1;
    int32_t nextDynamicResource = 1000;
    std::unordered_set<int32_t> liveResources;
    std::unordered_map<int32_t, FfxApiResourceDescription> dynamicDescriptions;
};

MockBackend& mock(FfxInterface* backend)
{
    return *static_cast<MockBackend*>(backend->device);
}

FfxVersionNumber getSdkVersion(FfxInterface*)
{
    return FFX_SDK_MAKE_VERSION(2, 3, 0);
}

FfxErrorCode createContext(FfxInterface* backend, FfxEffect,
                           FfxEffectBindlessConfig*, FfxUInt32* outId)
{
    if (!outId)
        return static_cast<FfxErrorCode>(FFX_ERROR_INVALID_POINTER);
    MockBackend& state = mock(backend);
    state.contextsCreated++;
    *outId = state.contextsCreated;
    return FFX_OK;
}

FfxErrorCode destroyContext(FfxInterface* backend, FfxUInt32)
{
    mock(backend).contextsDestroyed++;
    return FFX_OK;
}

FfxErrorCode getCapabilities(FfxInterface*, FfxDeviceCapabilities* outCaps)
{
    if (!outCaps)
        return static_cast<FfxErrorCode>(FFX_ERROR_INVALID_POINTER);
    std::memset(outCaps, 0, sizeof(*outCaps));
    outCaps->maximumSupportedShaderModel = FFX_SHADER_MODEL_6_2;
    outCaps->waveLaneCountMin = 32;
    outCaps->waveLaneCountMax = 32;
    return FFX_OK;
}

FfxErrorCode createResource(FfxInterface* backend,
                            const FfxCreateResourceDescription*, FfxUInt32,
                            FfxResourceInternal* outResource)
{
    if (!outResource)
        return static_cast<FfxErrorCode>(FFX_ERROR_INVALID_POINTER);
    MockBackend& state = mock(backend);
    outResource->internalIndex = state.nextResource++;
    state.liveResources.insert(outResource->internalIndex);
    state.resourcesCreated++;
    return FFX_OK;
}

FfxErrorCode destroyResource(FfxInterface* backend, FfxResourceInternal resource,
                             FfxUInt32)
{
    MockBackend& state = mock(backend);
    if (state.liveResources.erase(resource.internalIndex) != 0)
        state.resourcesDestroyed++;
    return FFX_OK;
}

FfxErrorCode registerResource(FfxInterface* backend, const FfxApiResource* resource, FfxUInt32,
                              FfxResourceInternal* outResource)
{
    if (!resource || !resource->resource || !outResource)
        return static_cast<FfxErrorCode>(FFX_ERROR_INVALID_POINTER);
    MockBackend& state = mock(backend);
    outResource->internalIndex = state.nextDynamicResource++;
    state.dynamicDescriptions.emplace(outResource->internalIndex, resource->description);
    state.resourcesRegistered++;
    return FFX_OK;
}

FfxErrorCode unregisterResources(FfxInterface* backend, FfxCommandList, FfxUInt32)
{
    MockBackend& state = mock(backend);
    state.dynamicDescriptions.clear();
    state.unregisterCalls++;
    return FFX_OK;
}

FfxApiResourceDescription getResourceDescription(FfxInterface* backend,
                                                  FfxResourceInternal resource)
{
    const auto& descriptions = mock(backend).dynamicDescriptions;
    const auto it = descriptions.find(resource.internalIndex);
    return it == descriptions.end() ? FfxApiResourceDescription{} : it->second;
}

FfxErrorCode stageConstantBuffer(FfxInterface* backend, void*, FfxUInt32 size,
                                 FfxConstantBuffer* outBuffer)
{
    if (!size || !outBuffer)
        return static_cast<FfxErrorCode>(FFX_ERROR_INVALID_ARGUMENT);
    outBuffer->num32BitEntries = size / sizeof(uint32_t);
    mock(backend).constantBufferStages++;
    return FFX_OK;
}

FfxErrorCode createPipeline(FfxInterface* backend, FfxShaderBlob* blob,
                            const FfxPipelineDescription*, FfxUInt32,
                            FfxPipelineState* outPipeline)
{
    if (!blob || !blob->data || blob->size < sizeof(uint32_t) || !outPipeline)
        return static_cast<FfxErrorCode>(FFX_ERROR_INVALID_POINTER);
    uint32_t magic = 0;
    std::memcpy(&magic, blob->data, sizeof(magic));
    if (magic != kSpirvMagic)
        return static_cast<FfxErrorCode>(FFX_ERROR_INVALID_ARGUMENT);
    MockBackend& state = mock(backend);
    state.pipelinesCreated++;
    state.spirvPipelinesCreated++;
    std::memset(outPipeline, 0, sizeof(*outPipeline));
    outPipeline->pipeline = reinterpret_cast<FfxPipeline>(
        static_cast<uintptr_t>(state.pipelinesCreated));
    return FFX_OK;
}

FfxErrorCode destroyPipeline(FfxInterface* backend, FfxPipelineState* pipeline,
                             FfxUInt32)
{
    if (pipeline && pipeline->pipeline)
        mock(backend).pipelinesDestroyed++;
    return FFX_OK;
}

FfxErrorCode scheduleGpuJob(FfxInterface* backend, const FfxGpuJobDescription* job)
{
    if (!job)
        return static_cast<FfxErrorCode>(FFX_ERROR_INVALID_POINTER);
    mock(backend).scheduledJobs++;
    return FFX_OK;
}

FfxErrorCode executeGpuJobs(FfxInterface* backend, FfxCommandList, FfxUInt32)
{
    MockBackend& state = mock(backend);
    state.executeCalls++;
    state.lastExecutedJobs = state.scheduledJobs;
    state.scheduledJobs = 0;
    return FFX_OK;
}

void expect(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FSR3.1.6 FI/OF host graph failure: %s\n", message);
        std::abort();
    }
}

FfxApiResource makeTexture(void* token, uint32_t width, uint32_t height,
                           FfxApiSurfaceFormat format, FfxApiResourceState state)
{
    FfxApiResource resource{};
    resource.resource = token;
    resource.description.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
    resource.description.format = format;
    resource.description.width = width;
    resource.description.height = height;
    resource.description.depth = 1;
    resource.description.mipCount = 1;
    resource.state = state;
    return resource;
}

} // namespace

FfxErrorCode GetResourceSizeFromDescription(FfxDevice,
                                            const FfxCreateResourceDescription*,
                                            uint64_t* sizeInBytes,
                                            uint64_t* alignment)
{
    if (sizeInBytes)
        *sizeInBytes = 0;
    if (alignment)
        *alignment = 0;
    return FFX_OK;
}

int main()
{
    MockBackend state{};
    FfxInterface backend{};
    backend.device = &state;
    backend.fpGetSDKVersion = getSdkVersion;
    backend.fpCreateBackendContext = createContext;
    backend.fpDestroyBackendContext = destroyContext;
    backend.fpGetDeviceCapabilities = getCapabilities;
    backend.fpCreateResource = createResource;
    backend.fpDestroyResource = destroyResource;
    backend.fpRegisterResource = registerResource;
    backend.fpUnregisterResources = unregisterResources;
    backend.fpGetResourceDescription = getResourceDescription;
    backend.fpStageConstantBufferDataFunc = stageConstantBuffer;
    backend.fpCreatePipeline = createPipeline;
    backend.fpDestroyPipeline = destroyPipeline;
    backend.fpScheduleGpuJob = scheduleGpuJob;
    backend.fpExecuteGpuJobs = executeGpuJobs;

    FfxOpticalflowContextDescription ofDesc{};
    ofDesc.backendInterface = backend;
    ofDesc.resolution = {1280, 720};
    FfxOpticalflowContext opticalFlow{};
    expect(ffxOpticalflowContextCreate(&opticalFlow, &ofDesc) == FFX_OK,
           "optical-flow context creation");

    FfxFrameInterpolationContextDescription fiDesc{};
    fiDesc.backendInterface = backend;
    fiDesc.maxRenderSize = {640, 360};
    fiDesc.displaySize = {1280, 720};
    fiDesc.backBufferFormat = FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
    fiDesc.previousInterpolationSourceFormat = FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
    FfxFrameInterpolationContext frameInterpolation{};
    expect(ffxFrameInterpolationContextCreate(&frameInterpolation, &fiDesc) == FFX_OK,
           "frame-interpolation context creation");

    expect(state.contextsCreated == 2, "one backend context per effect");
    expect(state.pipelinesCreated == 18, "all 11 FI and 7 OF pipelines");
    expect(state.spirvPipelinesCreated == 18, "every pipeline used an embedded SPIR-V blob");
    expect(state.resourcesCreated > 0, "persistent FI/OF resources created");

    FfxOpticalflowSharedResourceDescriptions ofShared{};
    FfxFrameInterpolationSharedResourceDescriptions fiShared{};
    expect(ffxOpticalflowGetSharedResourceDescriptions(&opticalFlow, &ofShared) == FFX_OK,
           "optical-flow shared resource descriptions");
    expect(ffxFrameInterpolationGetSharedResourceDescriptions(&frameInterpolation, &fiShared) == FFX_OK,
           "frame-interpolation shared resource descriptions");

    int tokens[12]{};
    const FfxApiResource color = makeTexture(&tokens[0], 1280, 720,
                                              FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM,
                                              FFX_API_RESOURCE_STATE_COMPUTE_READ);
    const FfxApiResource opticalFlowVector = makeTexture(
        &tokens[1], ofShared.opticalFlowVector.resourceDescription.width,
        ofShared.opticalFlowVector.resourceDescription.height,
        static_cast<FfxApiSurfaceFormat>(ofShared.opticalFlowVector.resourceDescription.format),
        FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    const FfxApiResource opticalFlowScd = makeTexture(
        &tokens[2], ofShared.opticalFlowSCD.resourceDescription.width,
        ofShared.opticalFlowSCD.resourceDescription.height,
        static_cast<FfxApiSurfaceFormat>(ofShared.opticalFlowSCD.resourceDescription.format),
        FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    FfxOpticalflowDispatchDescription ofDispatch{};
    ofDispatch.commandList = &state;
    ofDispatch.color = color;
    ofDispatch.opticalFlowVector = opticalFlowVector;
    ofDispatch.opticalFlowSCD = opticalFlowScd;
    ofDispatch.reset = true;
    ofDispatch.backbufferTransferFunction = FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
    ofDispatch.minMaxLuminance = {0.0f, 1.0f};
    expect(ffxOpticalflowContextDispatch(&opticalFlow, &ofDispatch) == FFX_OK,
           "optical-flow dispatch");

    const FfxApiResource depth = makeTexture(&tokens[3], 640, 360,
                                              FFX_API_SURFACE_FORMAT_R32_FLOAT,
                                              FFX_API_RESOURCE_STATE_COMPUTE_READ);
    const FfxApiResource motionVectors = makeTexture(&tokens[4], 640, 360,
                                                      FFX_API_SURFACE_FORMAT_R16G16_FLOAT,
                                                      FFX_API_RESOURCE_STATE_COMPUTE_READ);
    const FfxApiResource dilatedDepth = makeTexture(
        &tokens[5], fiShared.dilatedDepth.resourceDescription.width,
        fiShared.dilatedDepth.resourceDescription.height,
        static_cast<FfxApiSurfaceFormat>(fiShared.dilatedDepth.resourceDescription.format),
        FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    const FfxApiResource dilatedMotion = makeTexture(
        &tokens[6], fiShared.dilatedMotionVectors.resourceDescription.width,
        fiShared.dilatedMotionVectors.resourceDescription.height,
        static_cast<FfxApiSurfaceFormat>(fiShared.dilatedMotionVectors.resourceDescription.format),
        FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    const FfxApiResource reconstructedDepth = makeTexture(
        &tokens[7], fiShared.reconstructedPrevNearestDepth.resourceDescription.width,
        fiShared.reconstructedPrevNearestDepth.resourceDescription.height,
        static_cast<FfxApiSurfaceFormat>(fiShared.reconstructedPrevNearestDepth.resourceDescription.format),
        FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    FfxFrameInterpolationPrepareDescription prepare{};
    prepare.commandList = &state;
    prepare.renderSize = {640, 360};
    prepare.motionVectorScale = {640.0f, 360.0f};
    prepare.frameTimeDelta = 16.6667f;
    prepare.cameraNear = 0.1f;
    prepare.cameraFar = 1000.0f;
    prepare.viewSpaceToMetersFactor = 1.0f;
    prepare.cameraFovAngleVertical = 1.0471975512f;
    prepare.depth = depth;
    prepare.motionVectors = motionVectors;
    prepare.frameID = 1;
    prepare.dilatedDepth = dilatedDepth;
    prepare.dilatedMotionVectors = dilatedMotion;
    prepare.reconstructedPrevDepth = reconstructedDepth;
    prepare.cameraUp[0] = 0.0f;
    prepare.cameraUp[1] = 1.0f;
    prepare.cameraUp[2] = 0.0f;
    prepare.cameraRight[0] = 1.0f;
    prepare.cameraRight[1] = 0.0f;
    prepare.cameraRight[2] = 0.0f;
    prepare.cameraForward[0] = 0.0f;
    prepare.cameraForward[1] = 0.0f;
    prepare.cameraForward[2] = -1.0f;
    expect(ffxFrameInterpolationPrepare(&frameInterpolation, &prepare) == FFX_OK,
           "frame-interpolation prepare");

    const FfxApiResource output = makeTexture(&tokens[8], 1280, 720,
                                               FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM,
                                               FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    FfxFrameInterpolationDispatchDescription fiDispatch{};
    fiDispatch.commandList = &state;
    fiDispatch.displaySize = {1280, 720};
    fiDispatch.renderSize = {640, 360};
    fiDispatch.currentBackBuffer = color;
    fiDispatch.output = output;
    fiDispatch.interpolationRect = {0, 0, 1280, 720};
    fiDispatch.opticalFlowVector = opticalFlowVector;
    fiDispatch.opticalFlowSceneChangeDetection = opticalFlowScd;
    fiDispatch.opticalFlowBufferSize = {ofShared.opticalFlowVector.resourceDescription.width,
                                        ofShared.opticalFlowVector.resourceDescription.height};
    /* FI reconstructs the flow-field extent from this normalized scale. */
    fiDispatch.opticalFlowScale = {1.0f / 1280.0f, 1.0f / 720.0f};
    fiDispatch.opticalFlowBlockSize = 8;
    fiDispatch.cameraNear = 0.1f;
    fiDispatch.cameraFar = 1000.0f;
    fiDispatch.cameraFovAngleVertical = 1.0471975512f;
    fiDispatch.viewSpaceToMetersFactor = 1.0f;
    fiDispatch.frameTimeDelta = 16.6667f;
    fiDispatch.reset = true;
    fiDispatch.backBufferTransferFunction = FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
    fiDispatch.minMaxLuminance[0] = 0.0f;
    fiDispatch.minMaxLuminance[1] = 1.0f;
    fiDispatch.frameID = 1;
    fiDispatch.dilatedDepth = dilatedDepth;
    fiDispatch.dilatedMotionVectors = dilatedMotion;
    fiDispatch.reconstructedPrevDepth = reconstructedDepth;
    expect(ffxFrameInterpolationDispatch(&frameInterpolation, &fiDispatch) == FFX_OK,
           "frame-interpolation dispatch");
    expect(state.executeCalls == 3 && state.unregisterCalls == 3,
           "one execute/unregister pair per OF/FI operation");
    expect(state.resourcesRegistered == 15,
           "exact OF/FI dynamic resource registration count");
    expect(state.constantBufferStages == 12,
           "exact OF/FI constant-buffer staging count");
    expect(state.lastExecutedJobs == 30,
           "exact reset FI dispatch GPU-job count");

    expect(ffxFrameInterpolationContextDestroy(&frameInterpolation) == FFX_OK,
           "frame-interpolation context destruction");
    expect(ffxOpticalflowContextDestroy(&opticalFlow) == FFX_OK,
           "optical-flow context destruction");
    expect(state.resourcesDestroyed == state.resourcesCreated,
           "all persistent resources released");
    expect(state.pipelinesDestroyed == state.pipelinesCreated,
           "all non-null pipelines released");
    expect(state.contextsDestroyed == state.contextsCreated,
           "all backend contexts released");

    std::printf("FSR3.1.6 FI/OF graph: %u resources, %u embedded-SPIR-V pipelines, "
                "%u dynamic registrations, %u constant buffers, %u final jobs\n",
                state.resourcesCreated, state.pipelinesCreated,
                state.resourcesRegistered, state.constantBufferStages, state.lastExecutedJobs);
    return 0;
}
