/*
 * ffx_vk_fsr4_v07_types.h
 *
 * Self-contained type definitions extracted from the AMD FidelityFX SDK
 * (Kits/FidelityFX/api/internal/) for the source-v07 Vulkan provider.
 *
 * This replaces all dependencies on:
 *   ffx_api.h, ffx_api.hpp, ffx_api_types.h, ffx_interface.h,
 *   ffx_internal_types.h, ffx_error.h, ffx_upscale.h
 *
 * Source: AMD FidelityFX SDK 2.0.0  (MIT licence)
 * Extracted and adapted for standalone Linux/GCC compilation.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── version ─────────────────────────────────────────────────────────────── */

#define FFX_SDK_VERSION_MAJOR  2
#define FFX_SDK_VERSION_MINOR  0
#define FFX_SDK_VERSION_PATCH  0
#define FFX_SDK_MAKE_VERSION(major, minor, patch) \
    (((major) << 22) | ((minor) << 12) | (patch))

/* ── basic typedefs ──────────────────────────────────────────────────────── */

typedef uint8_t   FfxUInt8;
typedef uint16_t  FfxUInt16;
typedef uint32_t  FfxUInt32;
typedef uint64_t  FfxUInt64;
typedef int8_t    FfxInt8;
typedef int16_t   FfxInt16;
typedef int32_t   FfxInt32;
typedef int64_t   FfxInt64;
typedef float     FfxFloat32;
typedef bool      FfxBoolean;
typedef uint32_t  FfxVersionNumber;

/* ── error codes ─────────────────────────────────────────────────────────── */

typedef int32_t FfxErrorCode;
typedef enum FfxErrorCodes {
    FFX_OK                      =  0,
    FFX_ERROR_INVALID_POINTER   = (int32_t)0x80000000,
    FFX_ERROR_INVALID_ALIGNMENT = (int32_t)0x80000001,
    FFX_ERROR_INVALID_SIZE      = (int32_t)0x80000002,
    FFX_ERROR_INVALID_PATH      = (int32_t)0x80000004,
    FFX_ERROR_OUT_OF_MEMORY     = (int32_t)0x80000007,
    FFX_ERROR_INVALID_ENUM      = (int32_t)0x80000009,
    FFX_ERROR_INVALID_ARGUMENT  = (int32_t)0x8000000a,
    FFX_ERROR_OUT_OF_RANGE      = (int32_t)0x8000000b,
    FFX_ERROR_BACKEND_API_ERROR = (int32_t)0x8000000d,
    FFX_ERROR_INVALID_VERSION   = (int32_t)0x8000000f,
} FfxErrorCodes;

/* ── API return codes (ffx_api.h) ────────────────────────────────────────── */

typedef uint32_t ffxReturnCode_t;
#define FFX_API_RETURN_OK                0u
#define FFX_API_RETURN_ERROR             1u
#define FFX_API_RETURN_ERROR_PARAMETER   6u

/* ── opaque types ────────────────────────────────────────────────────────── */

typedef void *FfxDevice;
typedef void *FfxCommandList;
typedef void *FfxCommandQueue;
typedef void *FfxSwapchain;
typedef void *FfxRootSignature;
typedef void *FfxCommandSignature;
typedef void *FfxPipeline;
typedef void *ffxContext;

/* ── limits ──────────────────────────────────────────────────────────────── */

#define FFX_MAX_NUM_SRVS           64
#define FFX_MAX_NUM_UAVS           64
#define FFX_MAX_NUM_CONST_BUFFERS   3
#define FFX_RESOURCE_NAME_SIZE     64

/* ── surface format ──────────────────────────────────────────────────────── */

typedef enum FfxSurfaceFormat {
    FFX_SURFACE_FORMAT_UNKNOWN                 = 0,
    FFX_SURFACE_FORMAT_R32G32B32A32_TYPELESS   = 1,
    FFX_SURFACE_FORMAT_R32G32B32A32_UINT       = 2,
    FFX_SURFACE_FORMAT_R32G32B32A32_FLOAT      = 3,
    FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT      = 4,
    FFX_SURFACE_FORMAT_R32G32B32_FLOAT         = 5,
    FFX_SURFACE_FORMAT_R32G32_FLOAT            = 6,
    FFX_SURFACE_FORMAT_R8_UINT                 = 7,
    FFX_SURFACE_FORMAT_R32_UINT                = 8,
    FFX_SURFACE_FORMAT_R8G8B8A8_TYPELESS       = 9,
    FFX_SURFACE_FORMAT_R8G8B8A8_UNORM          = 10,
    FFX_SURFACE_FORMAT_R8G8B8A8_SNORM          = 11,
    FFX_SURFACE_FORMAT_R8G8B8A8_SRGB           = 12,
    FFX_SURFACE_FORMAT_B8G8R8A8_TYPELESS       = 13,
    FFX_SURFACE_FORMAT_B8G8R8A8_UNORM          = 14,
    FFX_SURFACE_FORMAT_B8G8R8A8_SRGB           = 15,
    FFX_SURFACE_FORMAT_R11G11B10_FLOAT         = 16,
    FFX_SURFACE_FORMAT_R10G10B10A2_UNORM       = 17,
    FFX_SURFACE_FORMAT_R16G16_FLOAT            = 18,
    FFX_SURFACE_FORMAT_R16G16_UINT             = 19,
    FFX_SURFACE_FORMAT_R16G16_SINT             = 20,
    FFX_SURFACE_FORMAT_R16_FLOAT               = 21,
    FFX_SURFACE_FORMAT_R16_UINT                = 22,
    FFX_SURFACE_FORMAT_R16_UNORM               = 23,
    FFX_SURFACE_FORMAT_R16_SNORM               = 24,
    FFX_SURFACE_FORMAT_R8_UNORM                = 25,
    FFX_SURFACE_FORMAT_R8G8_UNORM              = 26,
    FFX_SURFACE_FORMAT_R8G8_UINT               = 27,
    FFX_SURFACE_FORMAT_R32_FLOAT               = 28,
    FFX_SURFACE_FORMAT_R9G9B9E5_SHAREDEXP      = 29,
} FfxSurfaceFormat;

/* Keep backward compat with FFX_API_SURFACE_FORMAT_* names */
#define FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT  FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT

/* ── resource types ──────────────────────────────────────────────────────── */

typedef enum FfxResourceType {
    FFX_RESOURCE_TYPE_BUFFER      = 0,
    FFX_RESOURCE_TYPE_TEXTURE1D   = 1,
    FFX_RESOURCE_TYPE_TEXTURE2D   = 2,
    FFX_RESOURCE_TYPE_TEXTURE_CUBE= 3,
    FFX_RESOURCE_TYPE_TEXTURE3D   = 4,
} FfxResourceType;

/* ffx_api_types.h uses FfxApiResourceType with same values */
typedef FfxResourceType FfxApiResourceType;
#define FFX_API_RESOURCE_TYPE_BUFFER     FFX_RESOURCE_TYPE_BUFFER
#define FFX_API_RESOURCE_TYPE_TEXTURE1D  FFX_RESOURCE_TYPE_TEXTURE1D
#define FFX_API_RESOURCE_TYPE_TEXTURE2D  FFX_RESOURCE_TYPE_TEXTURE2D
#define FFX_API_RESOURCE_TYPE_TEXTURE3D  FFX_RESOURCE_TYPE_TEXTURE3D

/* ── resource flags ──────────────────────────────────────────────────────── */

typedef uint32_t FfxApiResourceFlags;
#define FFX_RESOURCE_FLAGS_NONE       0u
#define FFX_RESOURCE_FLAGS_ALIASABLE  1u

/* Kept numerically compatible with SDK 2.3's FfxApiResourceState.  The
 * provider needs this to distinguish a sampled external image from an output
 * image even though Vulkan uses VK_IMAGE_LAYOUT_GENERAL for both. */
typedef enum FfxApiResourceState {
    FFX_API_RESOURCE_STATE_COMMON             = 1u << 0,
    FFX_API_RESOURCE_STATE_UNORDERED_ACCESS   = 1u << 1,
    FFX_API_RESOURCE_STATE_COMPUTE_READ       = 1u << 2,
    FFX_API_RESOURCE_STATE_PIXEL_READ         = 1u << 3,
    FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ =
        FFX_API_RESOURCE_STATE_PIXEL_READ | FFX_API_RESOURCE_STATE_COMPUTE_READ,
    FFX_API_RESOURCE_STATE_COPY_SRC           = 1u << 4,
    FFX_API_RESOURCE_STATE_COPY_DEST          = 1u << 5,
    FFX_API_RESOURCE_STATE_GENERIC_READ =
        FFX_API_RESOURCE_STATE_COPY_SRC | FFX_API_RESOURCE_STATE_COMPUTE_READ,
    FFX_API_RESOURCE_STATE_INDIRECT_ARGUMENT  = 1u << 6,
    FFX_API_RESOURCE_STATE_PRESENT            = 1u << 7,
    FFX_API_RESOURCE_STATE_RENDER_TARGET      = 1u << 8,
    FFX_API_RESOURCE_STATE_DEPTH_ATTACHMENT   = 1u << 9,
} FfxApiResourceState;

/* ── resource description ────────────────────────────────────────────────── */

typedef struct FfxApiResourceDescription {
    uint32_t  type;
    uint32_t  format;
    union { uint32_t width;  uint32_t size;   };
    union { uint32_t height; uint32_t stride; };
    union { uint32_t depth;  uint32_t alignment; };
    uint32_t  mipCount;
    uint32_t  flags;
    uint32_t  usage;
} FfxApiResourceDescription;

typedef FfxApiResourceDescription FfxCreateResourceDescriptionBase;

/* ── FfxApiResource ──────────────────────────────────────────────────────── */

typedef struct FfxApiResource {
    void                      *resource;
    FfxApiResourceDescription  description;
    uint32_t                   state;
} FfxApiResource;

/* ── FfxApiEffectMemoryUsage ─────────────────────────────────────────────── */

typedef struct FfxApiEffectMemoryUsage {
    uint64_t totalUsageInBytes;
    uint64_t aliasableUsageInBytes;
} FfxApiEffectMemoryUsage;

/* ── coordinates / dimensions ────────────────────────────────────────────── */

typedef struct FfxApiFloatCoords2D { float x, y; } FfxApiFloatCoords2D;
typedef struct FfxApiDimensions2D  { uint32_t width, height; } FfxApiDimensions2D;
typedef struct FfxApiRect2D        { int32_t left, top, width, height; } FfxApiRect2D;

/* ── shader model ────────────────────────────────────────────────────────── */

typedef enum FfxShaderModel {
    FFX_SHADER_MODEL_5_1 = 0x51,
    FFX_SHADER_MODEL_6_0 = 0x60,
    FFX_SHADER_MODEL_6_1 = 0x61,
    FFX_SHADER_MODEL_6_2 = 0x62,
    FFX_SHADER_MODEL_6_3 = 0x63,
    FFX_SHADER_MODEL_6_4 = 0x64,
    FFX_SHADER_MODEL_6_5 = 0x65,
    FFX_SHADER_MODEL_6_6 = 0x66,
    FFX_SHADER_MODEL_6_7 = 0x67,
} FfxShaderModel;

/* ── device capabilities ─────────────────────────────────────────────────── */

typedef struct FfxDeviceCapabilities {
    FfxShaderModel  minimumSupportedShaderModel;
    FfxShaderModel  maximumSupportedShaderModel;
    bool            fp16Supported;
    bool            int8Supported;
    bool            raytracingSupported;
    bool            deviceCoherentMemorySupported;
    bool            reservedSamplerDescriptorsSupported;
    bool            waveLaneCountMin;
    bool            waveLaneCountMax;
    uint32_t        packedMathSupported;
} FfxDeviceCapabilities;

/* ── resource internal ───────────────────────────────────────────────────── */

typedef struct FfxResourceInternal {
    uint32_t internalIndex;
} FfxResourceInternal;

/* ── resource binding ────────────────────────────────────────────────────── */

typedef struct FfxResourceBinding {
    uint32_t  resourceIdentifier;
    uint32_t  bindingIndex;
    uint32_t  arrayIndex;
    wchar_t   name[FFX_RESOURCE_NAME_SIZE];
} FfxResourceBinding;

/* ── pipeline state ──────────────────────────────────────────────────────── */

typedef struct FfxPipelineState {
    FfxRootSignature  rootSignature;
    FfxCommandSignature cmdSignature;
    FfxPipeline       pipeline;
    uint32_t          uavTextureCount;
    uint32_t          srvTextureCount;
    uint32_t          srvBufferCount;
    uint32_t          uavBufferCount;
    uint32_t          staticTextureSrvCount;
    uint32_t          staticBufferSrvCount;
    uint32_t          staticTextureUavCount;
    uint32_t          staticBufferUavCount;
    uint32_t          constCount;
    FfxResourceBinding uavTextureBindings[FFX_MAX_NUM_UAVS];
    FfxResourceBinding srvTextureBindings[FFX_MAX_NUM_SRVS];
    FfxResourceBinding srvBufferBindings[FFX_MAX_NUM_SRVS];
    FfxResourceBinding uavBufferBindings[FFX_MAX_NUM_UAVS];
    FfxResourceBinding constantBufferBindings[FFX_MAX_NUM_CONST_BUFFERS];
    wchar_t            name[FFX_RESOURCE_NAME_SIZE];
} FfxPipelineState;

/* ── GPU job types ───────────────────────────────────────────────────────── */

typedef enum FfxGpuJobType {
    FFX_GPU_JOB_CLEAR_FLOAT = 0,
    FFX_GPU_JOB_COPY        = 1,
    FFX_GPU_JOB_COMPUTE     = 2,
    FFX_GPU_JOB_BARRIER     = 3,
    FFX_GPU_JOB_RASTER      = 4,
    FFX_GPU_JOB_DISCARD     = 5,
} FfxGpuJobType;

#define FFX_GPU_JOB_FLAGS_NONE        0u
#define FFX_GPU_JOB_FLAGS_SKIP_BARRIERS 1u

/* ── SRV / UAV view structs ──────────────────────────────────────────────── */

typedef struct FfxTextureSRV {
    FfxResourceInternal resource;
    uint32_t            allMips;
    uint32_t            mipIndex;
} FfxTextureSRV;

typedef struct FfxBufferSRV {
    FfxResourceInternal resource;
    uint32_t            offset;
    uint32_t            size;
    uint32_t            stride;
} FfxBufferSRV;

typedef struct FfxTextureUAV {
    FfxResourceInternal resource;
    uint32_t            mipIndex;
} FfxTextureUAV;

typedef struct FfxBufferUAV {
    FfxResourceInternal resource;
    uint32_t            offset;
    uint32_t            size;
    uint32_t            stride;
} FfxBufferUAV;

/* ── constant buffer ─────────────────────────────────────────────────────── */

typedef struct FfxConstantAllocation {
    void    *ptr;
    uint32_t size;
} FfxConstantAllocation;

typedef FfxConstantAllocation (*FfxConstantBufferAllocator)(
    void *pUserData, uint32_t size);

typedef struct FfxConstantBuffer {
    FfxApiResource resource;
} FfxConstantBuffer;

/* ── job descriptions ────────────────────────────────────────────────────── */

typedef struct FfxClearFloatJobDescription {
    float               color[4];
    FfxResourceInternal target;
} FfxClearFloatJobDescription;

typedef struct FfxCopyJobDescription {
    FfxResourceInternal src;
    uint32_t            srcOffset;
    FfxResourceInternal dst;
    uint32_t            dstOffset;
    uint32_t            size;
} FfxCopyJobDescription;

typedef struct FfxComputeJobDescription {
    FfxPipelineState  pipeline;
    uint32_t          dimensions[3];
    FfxResourceInternal cmdArgument;
    uint32_t          cmdArgumentOffset;
    FfxTextureSRV     srvTextures[FFX_MAX_NUM_SRVS];
    FfxBufferSRV      srvBuffers[FFX_MAX_NUM_SRVS];
    FfxTextureUAV     uavTextures[FFX_MAX_NUM_UAVS];
    FfxBufferUAV      uavBuffers[FFX_MAX_NUM_UAVS];
    FfxConstantBuffer cbs[FFX_MAX_NUM_CONST_BUFFERS];
    uint32_t          flags;
} FfxComputeJobDescription;

typedef struct FfxBarrierDescription {
    FfxResourceInternal resource;
    uint32_t            state;
    uint32_t            subResourceID;
} FfxBarrierDescription;

typedef struct FfxDiscardJobDescription {
    FfxResourceInternal target;
} FfxDiscardJobDescription;

typedef struct FfxGpuJobDescription {
    FfxGpuJobType jobType;
    wchar_t       jobLabel[FFX_RESOURCE_NAME_SIZE];
    union {
        FfxClearFloatJobDescription clearJobDescriptor;
        FfxCopyJobDescription       copyJobDescriptor;
        FfxComputeJobDescription    computeJobDescriptor;
        FfxBarrierDescription       barrierDescriptor;
        FfxDiscardJobDescription    discardJobDescriptor;
    };
} FfxGpuJobDescription;

/* ── shader blob ─────────────────────────────────────────────────────────── */

typedef struct FfxShaderBlob {
    const uint8_t  *data;
    uint32_t        size;
    uint32_t        cbvCount;
    uint32_t        srvTextureCount;
    uint32_t        uavTextureCount;
    uint32_t        srvBufferCount;
    uint32_t        uavBufferCount;
    uint32_t        samplerCount;
    uint32_t        rtAccelStructCount;
    const char    **boundConstantBufferNames;
    const uint32_t *boundConstantBuffers;
    const uint32_t *boundConstantBufferCounts;
    const uint32_t *boundConstantBufferSpaces;
    const char    **boundSRVTextureNames;
    const uint32_t *boundSRVTextures;
    const uint32_t *boundSRVTextureCounts;
    const uint32_t *boundSRVTextureSpaces;
    const char    **boundUAVTextureNames;
    const uint32_t *boundUAVTextures;
    const uint32_t *boundUAVTextureCounts;
    const uint32_t *boundUAVTextureSpaces;
    const char    **boundSRVBufferNames;
    const uint32_t *boundSRVBuffers;
    const uint32_t *boundSRVBufferCounts;
    const uint32_t *boundSRVBufferSpaces;
    const char    **boundUAVBufferNames;
    const uint32_t *boundUAVBuffers;
    const uint32_t *boundUAVBufferCounts;
    const uint32_t *boundUAVBufferSpaces;
    const char    **boundSamplerNames;
    const uint32_t *boundSamplers;
    const uint32_t *boundSamplerCounts;
    const uint32_t *boundSamplerSpaces;
    const char    **boundRTAccelerationStructureNames;
    const uint32_t *boundRTAccelerationStructures;
    const uint32_t *boundRTAccelerationStructureCounts;
    const uint32_t *boundRTAccelerationStructureSpaces;
} FfxShaderBlob;

/* ── create / describe resource ─────────────────────────────────────────── */

typedef struct FfxCreateResourceDescription {
    uint32_t              type;
    uint32_t              format;
    uint32_t              width;
    uint32_t              height;
    uint32_t              depth;
    uint32_t              mipCount;
    uint32_t              flags;
    uint32_t              usage;
    uint32_t              state;
    const void           *initData;
    size_t                initDataSize;
    wchar_t               name[FFX_RESOURCE_NAME_SIZE];
} FfxCreateResourceDescription;

/* ── pipeline description ────────────────────────────────────────────────── */

typedef struct FfxPipelineDescription {
    uint32_t  stage;
    uint32_t  contextFlags;
    uint32_t  passIndex;
    uint32_t  permutationOptions;
    wchar_t   name[FFX_RESOURCE_NAME_SIZE];
} FfxPipelineDescription;

/* ── static resource ─────────────────────────────────────────────────────── */

typedef struct FfxStaticResourceDescription {
    const FfxApiResource *resource;
    uint32_t              descriptorIndex;
} FfxStaticResourceDescription;

/* ── effect / bindless config ─────────────────────────────────────────────── */

typedef enum FfxEffect {
    FFX_EFFECT_FSR4UPSCALER   = 22,
    FFX_EFFECT_SHAREDRESOURCES = 127,
    FFX_EFFECT_SHAREDAPIBACKEND = 128,
} FfxEffect;

typedef struct FfxEffectBindlessConfig {
    uint32_t maxStaticTextureSrvDescriptors;
    uint32_t maxStaticBufferSrvDescriptors;
    uint32_t maxStaticTextureUavDescriptors;
    uint32_t maxStaticBufferUavDescriptors;
} FfxEffectBindlessConfig;

/* ── frame generation (stub, not used in Q2RTX) ──────────────────────────── */

typedef struct FfxFrameGenerationConfig {
    void *swapChain;
} FfxFrameGenerationConfig;

/* ── FfxInterface function pointer typedefs ──────────────────────────────── */

typedef struct FfxInterface FfxInterface;

typedef FfxVersionNumber (*FfxGetSDKVersionFunc)(FfxInterface *);
typedef FfxErrorCode     (*FfxGetEffectGpuMemoryUsageFunc)(FfxInterface *, FfxUInt32, FfxApiEffectMemoryUsage *);
typedef FfxErrorCode     (*FfxCreateBackendContextFunc)(FfxInterface *, FfxEffect, FfxEffectBindlessConfig *, FfxUInt32 *);
typedef FfxErrorCode     (*FfxGetDeviceCapabilitiesFunc)(FfxInterface *, FfxDeviceCapabilities *);
typedef FfxErrorCode     (*FfxDestroyBackendContextFunc)(FfxInterface *, FfxUInt32);
typedef FfxErrorCode     (*FfxCreateResourceFunc)(FfxInterface *, const FfxCreateResourceDescription *, FfxUInt32, FfxResourceInternal *);
typedef FfxErrorCode     (*FfxRegisterResourceFunc)(FfxInterface *, const FfxApiResource *, FfxUInt32, FfxResourceInternal *);
typedef FfxApiResource   (*FfxGetResourceFunc)(FfxInterface *, FfxResourceInternal);
typedef FfxErrorCode     (*FfxUnregisterResourcesFunc)(FfxInterface *, FfxCommandList, FfxUInt32);
typedef FfxErrorCode     (*FfxRegisterStaticResourceFunc)(FfxInterface *, const FfxStaticResourceDescription *, FfxUInt32);
typedef FfxApiResourceDescription (*FfxGetResourceDescriptionFunc)(FfxInterface *, FfxResourceInternal);
typedef FfxErrorCode     (*FfxDestroyResourceFunc)(FfxInterface *, FfxResourceInternal, FfxUInt32);
typedef FfxErrorCode     (*FfxMapResourceFunc)(FfxInterface *, FfxResourceInternal, void **);
typedef FfxErrorCode     (*FfxUnmapResourceFunc)(FfxInterface *, FfxResourceInternal);
typedef FfxErrorCode     (*FfxStageConstantBufferDataFunc)(FfxInterface *, void *, FfxUInt32, FfxConstantBuffer *);
typedef FfxErrorCode     (*FfxCreatePipelineFunc)(FfxInterface *, FfxShaderBlob *, const FfxPipelineDescription *, FfxUInt32, FfxPipelineState *);
typedef FfxErrorCode     (*FfxDestroyPipelineFunc)(FfxInterface *, FfxPipelineState *, FfxUInt32);
typedef FfxErrorCode     (*FfxScheduleGpuJobFunc)(FfxInterface *, const FfxGpuJobDescription *);
typedef FfxErrorCode     (*FfxExecuteGpuJobsFunc)(FfxInterface *, FfxCommandList, FfxUInt32);
typedef FfxErrorCode     (*FfxSwapChainConfigureFrameGenerationFunc)(FfxFrameGenerationConfig const *);
typedef void             (*FfxRegisterConstantBufferAllocatorFunc)(FfxInterface *, FfxConstantBufferAllocator);

/* ── FfxInterface ────────────────────────────────────────────────────────── */

struct FfxInterface {
    FfxGetSDKVersionFunc               fpGetSDKVersion;
    FfxGetEffectGpuMemoryUsageFunc     fpGetEffectGpuMemoryUsage;
    FfxCreateBackendContextFunc        fpCreateBackendContext;
    FfxGetDeviceCapabilitiesFunc       fpGetDeviceCapabilities;
    FfxDestroyBackendContextFunc       fpDestroyBackendContext;
    FfxCreateResourceFunc              fpCreateResource;
    FfxRegisterResourceFunc            fpRegisterResource;
    FfxGetResourceFunc                 fpGetResource;
    FfxUnregisterResourcesFunc         fpUnregisterResources;
    FfxRegisterStaticResourceFunc      fpRegisterStaticResource;
    FfxGetResourceDescriptionFunc      fpGetResourceDescription;
    FfxDestroyResourceFunc             fpDestroyResource;
    FfxMapResourceFunc                 fpMapResource;
    FfxUnmapResourceFunc               fpUnmapResource;
    FfxStageConstantBufferDataFunc     fpStageConstantBufferDataFunc;
    FfxCreatePipelineFunc              fpCreatePipeline;
    FfxDestroyPipelineFunc             fpDestroyPipeline;
    FfxScheduleGpuJobFunc              fpScheduleGpuJob;
    FfxExecuteGpuJobsFunc              fpExecuteGpuJobs;
    FfxSwapChainConfigureFrameGenerationFunc fpSwapChainConfigureFrameGeneration;
    FfxRegisterConstantBufferAllocatorFunc   fpRegisterConstantBufferAllocator;
    void  *scratchBuffer;
    size_t scratchBufferSize;
    FfxDevice device;
};

/* ── ffx_api header (struct types used by ffxCreateContext / Dispatch) ───── */

typedef uint64_t ffxStructType_t;

typedef struct ffxApiHeader {
    ffxStructType_t        type;
    struct ffxApiHeader   *pNext;
} ffxApiHeader;

typedef ffxApiHeader ffxCreateContextDescHeader;
typedef ffxApiHeader ffxQueryDescHeader;
typedef ffxApiHeader ffxDispatchDescHeader;

/* ── FSR4 upscaler structs (extracted from ffx_upscale.h) ────────────────── */

/* Context creation flags */
#define FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE        (1<<0)
#define FFX_UPSCALE_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS (1<<1)
#define FFX_UPSCALE_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION (1<<2)
#define FFX_UPSCALE_ENABLE_DEPTH_INVERTED             (1<<3)
#define FFX_UPSCALE_ENABLE_DEPTH_INFINITE             (1<<4)
#define FFX_UPSCALE_ENABLE_AUTO_EXPOSURE             (1<<5)
#define FFX_UPSCALE_ENABLE_DYNAMIC_RESOLUTION         (1<<6)
#define FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE     (1<<8)

/* Struct type IDs */
#define FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE     0x00010000u
#define FFX_API_DISPATCH_DESC_TYPE_UPSCALE           0x00010001u
#define FFX_API_QUERY_DESC_TYPE_UPSCALE_GET_JITTER_PHASE_COUNT 0x00010004u
#define FFX_API_QUERY_DESC_TYPE_UPSCALE_GET_JITTER_OFFSET      0x00010005u

typedef struct ffxCreateContextDescUpscale {
    ffxCreateContextDescHeader header;
    uint32_t                   flags;
    FfxApiDimensions2D         maxRenderSize;
    FfxApiDimensions2D         maxUpscaleSize;
} ffxCreateContextDescUpscale;

typedef struct ffxDispatchDescUpscale {
    ffxDispatchDescHeader  header;
    void                  *commandList;
    FfxApiResource         color;
    FfxApiResource         depth;
    FfxApiResource         motionVectors;
    FfxApiResource         exposure;
    FfxApiResource         reactive;
    FfxApiResource         transparencyAndComposition;
    FfxApiResource         output;
    FfxApiFloatCoords2D    jitterOffset;
    FfxApiFloatCoords2D    motionVectorScale;
    FfxApiDimensions2D     renderSize;
    FfxApiDimensions2D     upscaleSize;
    bool                   enableSharpening;
    bool                   enableAutoExposure;
    float                  sharpness;
    float                  frameTimeDelta;
    float                  preExposure;
    bool                   reset;
    float                  cameraNear;
    float                  cameraFar;
    float                  cameraFovAngleVertical;
    float                  viewSpaceToMetersFactor;
    uint32_t               flags;
} ffxDispatchDescUpscale;

typedef struct ffxQueryDescUpscaleGetJitterPhaseCount {
    ffxQueryDescHeader header;
    uint32_t           displayWidth;
    uint32_t           renderWidth;
    int32_t           *pOutPhaseCount;
} ffxQueryDescUpscaleGetJitterPhaseCount;

typedef struct ffxQueryDescUpscaleGetJitterOffset {
    ffxQueryDescHeader header;
    int32_t            index;
    int32_t            phaseCount;
    float             *pOutX;
    float             *pOutY;
} ffxQueryDescUpscaleGetJitterOffset;

/* Narrow Q2RTX FSR4-v07 diagnostic seam. These are not an AMD public API:
 * they expose a borrowed sampled resource for visual validation only, without
 * transferring ownership or making the provider's resource graph mutable. */
typedef enum FfxFsr4DebugResource_e {
    FFX_FSR4_DEBUG_RESOURCE_HISTORY = 0,
    FFX_FSR4_DEBUG_RESOURCE_REPROJECTED
} FfxFsr4DebugResource;

/* ── allocation callbacks (used by CreateContext/DestroyContext) ─────────── */

typedef void *(*ffxAlloc)(void *pUserData, uint64_t size);
typedef void  (*ffxDealloc)(void *pUserData, void *pMem);

typedef struct ffxAllocationCallbacks {
    void       *pUserData;
    ffxAlloc    fpAlloc;
    ffxDealloc  fpDealloc;
} ffxAllocationCallbacks;

/* ── Versioned v07 provider API ──────────────────────────────────────────── */

/*
 * Set the backend interface used by the next CreateContext call.  The provider
 * copies that interface into the created context, so it may be cleared once
 * creation returns.  This avoids an AMD-DLL/global-symbol dependency and lets
 * an application keep its Vulkan backend ownership explicit.
 */
void ffxFsr4V07SetBackendInterface(FfxInterface *backend);

ffxReturnCode_t ffxFsr4V07CreateContext(ffxContext *ctx,
                                        ffxCreateContextDescHeader *desc,
                                        const ffxAllocationCallbacks *mem);

ffxReturnCode_t ffxFsr4V07DestroyContext(ffxContext *ctx,
                                         const ffxAllocationCallbacks *mem);

ffxReturnCode_t ffxFsr4V07Query(ffxContext *ctx,
                                ffxQueryDescHeader *desc);

ffxReturnCode_t ffxFsr4V07Dispatch(ffxContext *ctx,
                                   const ffxDispatchDescHeader *desc);

int ffxFsr4GetDebugResource(ffxContext *ctx, FfxFsr4DebugResource resource,
                            FfxApiResource *out_resource);

ffxReturnCode_t ffxFsr4V07Configure(ffxContext *ctx,
                                    const ffxApiHeader *desc);

#ifdef __cplusplus
} /* extern "C" */
#endif
