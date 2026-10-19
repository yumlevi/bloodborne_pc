/*
 * Copyright (c) 2026 Q2RTX FSR Vulkan contributors
 * SPDX-License-Identifier: MIT
 *
 * Exercises the public SDK 2.3 FSR3.1.5 scheduler against a deliberately
 * minimal backend.  It validates host-side resource/pipeline lifecycle before
 * the new Vulkan FfxInterface implementation is allowed to record GPU work.
 */

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#include "ffx_fsr3upscaler.h"
#include "ffx_fsr3upscaler_shaderblobs.h"

struct MockBackend {
    uint32_t resourcesCreated = 0;
    uint32_t resourcesDestroyed = 0;
    uint32_t copyResourcesDestroyed = 0;
    uint32_t duplicateResourceDestroyRequests = 0;
    uint32_t nullResourceDestroyRequests = 0;
    uint32_t unexpectedResourceDestroyRequests = 0;
    uint32_t pipelinesCreated = 0;
    uint32_t pipelinesDestroyed = 0;
    uint32_t nullPipelineDestroyRequests = 0;
    uint32_t contextsCreated = 0;
    uint32_t contextsDestroyed = 0;
    uint32_t resourcesRegistered = 0;
    uint32_t unregisterCalls = 0;
    uint32_t constantBufferStages = 0;
    uint32_t executeCalls = 0;
    uint32_t scheduledJobs = 0;
    uint32_t lastExecutedJobs = 0;
    int32_t nextResourceHandle = 1;
    int32_t nextDynamicResourceHandle = 1000;
    std::vector<uint32_t> resourceIds;
    std::unordered_set<int32_t> primaryResourceHandles;
    std::unordered_set<int32_t> releasedPrimaryResourceHandles;
    std::unordered_set<int32_t> copyResourceHandles;
    std::unordered_map<int32_t, FfxApiResourceDescription> dynamicDescriptions;
    std::array<uint32_t, 5> jobsByType{};
    std::array<uint32_t, 5> lastExecutedJobsByType{};
};

static MockBackend& mock(FfxInterface* backend)
{
    return *static_cast<MockBackend*>(backend->device);
}

extern "C" FfxErrorCode fsr3UpscalerGetPermutationBlobByIndex(
    FfxFsr3UpscalerPass,
    uint32_t,
    FfxShaderBlob* outBlob)
{
    if (!outBlob) {
        return FFX_ERROR_INVALID_POINTER;
    }
    std::memset(outBlob, 0, sizeof(*outBlob));
    return FFX_OK;
}

extern "C" FfxErrorCode fsr3UpscalerIsWave64(uint32_t, bool& isWave64)
{
    isWave64 = false;
    return FFX_OK;
}

static FfxVersionNumber getSdkVersion(FfxInterface*)
{
    return FFX_SDK_MAKE_VERSION(2, 3, 0);
}

static FfxErrorCode createContext(FfxInterface* backend, FfxEffect,
                                  FfxEffectBindlessConfig*, FfxUInt32* id)
{
    if (!id) {
        return FFX_ERROR_INVALID_POINTER;
    }
    mock(backend).contextsCreated++;
    *id = 1;
    return FFX_OK;
}

static FfxErrorCode destroyContext(FfxInterface* backend, FfxUInt32)
{
    mock(backend).contextsDestroyed++;
    return FFX_OK;
}

static FfxErrorCode getCapabilities(FfxInterface*, FfxDeviceCapabilities* caps)
{
    if (!caps) {
        return FFX_ERROR_INVALID_POINTER;
    }
    std::memset(caps, 0, sizeof(*caps));
    caps->maximumSupportedShaderModel = FFX_SHADER_MODEL_6_2;
    caps->waveLaneCountMin = 32;
    caps->waveLaneCountMax = 64;
    return FFX_OK;
}

static FfxErrorCode createResource(FfxInterface* backend,
                                   const FfxCreateResourceDescription* desc,
                                   FfxUInt32, FfxResourceInternal* resource)
{
    if (!desc || !resource) {
        return FFX_ERROR_INVALID_POINTER;
    }
    MockBackend& state = mock(backend);
    state.resourcesCreated++;
    state.resourceIds.push_back(desc->id);
    // The SDK's release helper represents an upload/copy resource by the
    // primary handle plus one.  Reserve a pair for every primary allocation,
    // so this mock models the handle convention without collisions.
    resource->internalIndex = state.nextResourceHandle;
    state.primaryResourceHandles.insert(resource->internalIndex);
    if (desc->initData.type != FFX_RESOURCE_INIT_DATA_TYPE_UNINITIALIZED) {
        state.copyResourceHandles.insert(resource->internalIndex + 1);
    }
    state.nextResourceHandle += 2;
    return FFX_OK;
}

static FfxErrorCode destroyResource(FfxInterface* backend, FfxResourceInternal resource,
                                    FfxUInt32)
{
    MockBackend& state = mock(backend);
    if (state.primaryResourceHandles.erase(resource.internalIndex) != 0) {
        state.resourcesDestroyed++;
        state.releasedPrimaryResourceHandles.insert(resource.internalIndex);
    } else if (state.copyResourceHandles.erase(resource.internalIndex) != 0) {
        state.copyResourcesDestroyed++;
    } else if (state.releasedPrimaryResourceHandles.count(resource.internalIndex) != 0) {
        // The upstream scheduler aliases luma-history state and asks a
        // backend to release one already-released primary handle.  AMD's
        // reference DX12 backend intentionally treats this as idempotent.
        state.duplicateResourceDestroyRequests++;
    } else if (resource.internalIndex == 0) {
        state.nullResourceDestroyRequests++;
    } else {
        state.unexpectedResourceDestroyRequests++;
    }
    return FFX_OK;
}

static FfxErrorCode registerResource(FfxInterface* backend,
                                     const FfxApiResource* resource,
                                     FfxUInt32, FfxResourceInternal* outResource)
{
    if (!resource || !resource->resource || !outResource) {
        return FFX_ERROR_INVALID_POINTER;
    }
    MockBackend& state = mock(backend);
    outResource->internalIndex = state.nextDynamicResourceHandle++;
    state.dynamicDescriptions.emplace(outResource->internalIndex,
                                      resource->description);
    state.resourcesRegistered++;
    return FFX_OK;
}

static FfxErrorCode unregisterResources(FfxInterface* backend, FfxCommandList,
                                        FfxUInt32)
{
    MockBackend& state = mock(backend);
    state.dynamicDescriptions.clear();
    state.unregisterCalls++;
    return FFX_OK;
}

static FfxApiResourceDescription getResourceDescription(
    FfxInterface* backend, FfxResourceInternal resource)
{
    const MockBackend& state = mock(backend);
    const auto it = state.dynamicDescriptions.find(resource.internalIndex);
    return it == state.dynamicDescriptions.end() ? FfxApiResourceDescription{}
                                                  : it->second;
}

static FfxErrorCode stageConstantBufferData(FfxInterface* backend, void* data,
                                            FfxUInt32 size,
                                            FfxConstantBuffer* constantBuffer)
{
    if (!data || !size || size % sizeof(uint32_t) != 0 || !constantBuffer) {
        return FFX_ERROR_INVALID_ARGUMENT;
    }
    constantBuffer->num32BitEntries = size / sizeof(uint32_t);
    constantBuffer->data = static_cast<uint32_t*>(data);
    mock(backend).constantBufferStages++;
    return FFX_OK;
}

static FfxErrorCode scheduleGpuJob(FfxInterface* backend,
                                   const FfxGpuJobDescription* job)
{
    if (!job || job->jobType > FFX_GPU_JOB_DISCARD) {
        return FFX_ERROR_INVALID_ARGUMENT;
    }
    MockBackend& state = mock(backend);
    state.scheduledJobs++;
    state.jobsByType[job->jobType]++;
    return FFX_OK;
}

static FfxErrorCode executeGpuJobs(FfxInterface* backend, FfxCommandList,
                                   FfxUInt32)
{
    MockBackend& state = mock(backend);
    state.executeCalls++;
    state.lastExecutedJobs = state.scheduledJobs;
    state.lastExecutedJobsByType = state.jobsByType;
    state.scheduledJobs = 0;
    state.jobsByType.fill(0);
    return FFX_OK;
}

static FfxErrorCode createPipeline(FfxInterface* backend, FfxShaderBlob*,
                                   const FfxPipelineDescription*, FfxUInt32,
                                   FfxPipelineState* pipeline)
{
    if (!pipeline) {
        return FFX_ERROR_INVALID_POINTER;
    }
    MockBackend& state = mock(backend);
    state.pipelinesCreated++;
    std::memset(pipeline, 0, sizeof(*pipeline));
    pipeline->pipeline = reinterpret_cast<FfxPipeline>(
        static_cast<uintptr_t>(state.pipelinesCreated));
    return FFX_OK;
}

static FfxErrorCode destroyPipeline(FfxInterface* backend, FfxPipelineState* pipeline,
                                    FfxUInt32)
{
    // FSR3.1.5 releases all optional pipeline slots.  A correct backend must
    // treat an unused/null pipeline as a no-op.
    if (pipeline && pipeline->pipeline) {
        mock(backend).pipelinesDestroyed++;
    } else {
        mock(backend).nullPipelineDestroyRequests++;
    }
    return FFX_OK;
}

// The host implementation uses this helper only for the optional public
// memory-usage query.  A real Vulkan bridge will calculate allocation size
// and alignment through vkGet*MemoryRequirements; this mock only proves the
// scheduler graph and must never claim a memory estimate.
FfxErrorCode GetResourceSizeFromDescription(FfxDevice,
                                            const FfxCreateResourceDescription*,
                                            uint64_t* sizeInBytes,
                                            uint64_t* alignment)
{
    if (sizeInBytes) {
        *sizeInBytes = 0;
    }
    if (alignment) {
        *alignment = 0;
    }
    return FFX_OK;
}

static FfxApiResource makeTexture(void* token, uint32_t width, uint32_t height,
                                  FfxApiSurfaceFormat format,
                                  FfxApiResourceState state)
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
    backend.fpStageConstantBufferDataFunc = stageConstantBufferData;
    backend.fpCreatePipeline = createPipeline;
    backend.fpDestroyPipeline = destroyPipeline;
    backend.fpScheduleGpuJob = scheduleGpuJob;
    backend.fpExecuteGpuJobs = executeGpuJobs;

    FfxFsr3UpscalerContextDescription desc{};
    desc.backendInterface = backend;
    desc.maxRenderSize = {640, 360};
    desc.maxUpscaleSize = {1280, 720};
    desc.flags = FFX_FSR3UPSCALER_ENABLE_HIGH_DYNAMIC_RANGE |
                 FFX_FSR3UPSCALER_ENABLE_AUTO_EXPOSURE;

    FfxFsr3UpscalerContext context{};
    const auto expect = [](bool condition, const char* message) {
        if (!condition) {
            std::fprintf(stderr, "FSR3.1.5 host graph failure: %s\n", message);
            std::abort();
        }
    };

    expect(ffxFsr3UpscalerContextCreate(&context, &desc) == FFX_OK,
           "context creation");

    // FSR3.1.5 declares 19 persistent internal resources and builds 11 pass
    // pipelines at context creation.  The three shared resources are supplied
    // by a parent provider/combined context and are intentionally not owned
    // by this standalone upscaler context.
    expect(state.contextsCreated == 1, "one backend context");
    expect(state.resourcesCreated == 19, "19 persistent resources");
    expect(state.pipelinesCreated == 11, "11 context pipelines");
    expect(state.resourceIds.size() == state.resourcesCreated,
           "one resource id per allocation");

    FfxFsr3UpscalerSharedResourceDescriptions shared{};
    expect(ffxFsr3UpscalerGetSharedResourceDescriptions(&context, &shared) == FFX_OK,
           "shared-resource descriptions");
    expect(shared.dilatedDepth.resourceDescription.width == 640,
           "shared depth width");
    expect(shared.dilatedDepth.resourceDescription.height == 360,
           "shared depth height");
    expect(shared.dilatedMotionVectors.resourceDescription.width == 640,
           "shared motion width");
    expect(shared.reconstructedPrevNearestDepth.resourceDescription.height == 360,
           "shared nearest-depth height");

    // Record two complete frames through the public host scheduler.  The mock
    // deliberately never creates a GPU object; it proves the exact resource,
    // constant-buffer, and job-queue contract a Vulkan implementation must
    // satisfy before shader compilation/recording is introduced.
    int resourceTokens[9]{};
    FfxFsr3UpscalerDispatchDescription dispatch{};
    dispatch.commandList = &state;
    dispatch.color = makeTexture(&resourceTokens[0], 640, 360,
                                 FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT,
                                 FFX_API_RESOURCE_STATE_COMPUTE_READ);
    dispatch.depth = makeTexture(&resourceTokens[1], 640, 360,
                                 FFX_API_SURFACE_FORMAT_R32_FLOAT,
                                 FFX_API_RESOURCE_STATE_COMPUTE_READ);
    dispatch.motionVectors = makeTexture(&resourceTokens[2], 640, 360,
                                         FFX_API_SURFACE_FORMAT_R16G16_FLOAT,
                                         FFX_API_RESOURCE_STATE_COMPUTE_READ);
    dispatch.reactive = makeTexture(&resourceTokens[3], 640, 360,
                                    FFX_API_SURFACE_FORMAT_R8_UNORM,
                                    FFX_API_RESOURCE_STATE_COMPUTE_READ);
    dispatch.transparencyAndComposition = makeTexture(
        &resourceTokens[4], 640, 360, FFX_API_SURFACE_FORMAT_R8_UNORM,
        FFX_API_RESOURCE_STATE_COMPUTE_READ);
    dispatch.dilatedDepth = makeTexture(&resourceTokens[5], 640, 360,
                                        FFX_API_SURFACE_FORMAT_R32_FLOAT,
                                        FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    dispatch.dilatedMotionVectors = makeTexture(
        &resourceTokens[6], 640, 360, FFX_API_SURFACE_FORMAT_R16G16_FLOAT,
        FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    dispatch.reconstructedPrevNearestDepth = makeTexture(
        &resourceTokens[7], 640, 360, FFX_API_SURFACE_FORMAT_R32_UINT,
        FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    dispatch.output = makeTexture(&resourceTokens[8], 1280, 720,
                                  FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT,
                                  FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    dispatch.motionVectorScale = {640.0f, 360.0f};
    dispatch.renderSize = {640, 360};
    dispatch.upscaleSize = {1280, 720};
    dispatch.frameTimeDelta = 16.6667f;
    dispatch.preExposure = 1.0f;
    dispatch.reset = true;
    dispatch.cameraNear = 0.1f;
    dispatch.cameraFar = 1000.0f;
    dispatch.cameraFovAngleVertical = 1.0471975512f;
    dispatch.viewSpaceToMetersFactor = 1.0f;

    expect(ffxFsr3UpscalerContextDispatch(&context, &dispatch) == FFX_OK,
           "first public dispatch");
    expect(state.resourcesRegistered == 9, "nine dynamic resources on frame one");
    expect(state.constantBufferStages == 3, "three staged constant buffers on frame one");
    expect(state.executeCalls == 1 && state.unregisterCalls == 1,
           "execute and unregister on frame one");
    expect(state.lastExecutedJobs == 27, "27 first-frame jobs");
    expect(state.lastExecutedJobsByType[FFX_GPU_JOB_CLEAR_FLOAT] == 10,
           "10 first-frame clears");
    expect(state.lastExecutedJobsByType[FFX_GPU_JOB_BARRIER] == 5,
           "five first-frame barriers");
    expect(state.lastExecutedJobsByType[FFX_GPU_JOB_DISCARD] == 5,
           "five first-frame discards");
    expect(state.lastExecutedJobsByType[FFX_GPU_JOB_COMPUTE] == 7,
           "seven first-frame compute jobs");

    dispatch.reset = false;
    dispatch.enableSharpening = true;
    dispatch.sharpness = 0.25f;
    expect(ffxFsr3UpscalerContextDispatch(&context, &dispatch) == FFX_OK,
           "temporal sharpened public dispatch");
    expect(state.resourcesRegistered == 18,
           "nine dynamic resources on each dispatch");
    expect(state.constantBufferStages == 6,
           "three staged constant buffers on each dispatch");
    expect(state.executeCalls == 2 && state.unregisterCalls == 2,
           "execute and unregister on temporal frame");
    expect(state.lastExecutedJobs == 21, "21 temporal sharpened jobs");
    expect(state.lastExecutedJobsByType[FFX_GPU_JOB_CLEAR_FLOAT] == 3,
           "three temporal clears");
    expect(state.lastExecutedJobsByType[FFX_GPU_JOB_BARRIER] == 5,
           "five temporal barriers");
    expect(state.lastExecutedJobsByType[FFX_GPU_JOB_DISCARD] == 5,
           "five temporal discards");
    expect(state.lastExecutedJobsByType[FFX_GPU_JOB_COMPUTE] == 8,
           "eight temporal sharpened compute jobs");

    expect(ffxFsr3UpscalerContextDestroy(&context) == FFX_OK, "context destruction");
    expect(state.pipelinesDestroyed == state.pipelinesCreated,
           "all pipelines destroyed");
    expect(state.resourcesDestroyed == state.resourcesCreated,
           "all resources destroyed");
    expect(state.copyResourcesDestroyed == 4,
           "all four initialized-resource uploads destroyed");
    expect(state.duplicateResourceDestroyRequests == 1,
           "one documented idempotent resource release");
    expect(state.unexpectedResourceDestroyRequests == 0,
           "no unknown resource destroy request");
    expect(state.nullPipelineDestroyRequests == 1,
           "one unused optional pipeline release");
    expect(state.contextsDestroyed == state.contextsCreated,
           "backend context destroyed");

    std::printf("FSR3.1.5 graph: %u resources, %u pipelines\n",
                state.resourcesCreated, state.pipelinesCreated);
    return 0;
}
