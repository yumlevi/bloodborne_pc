/*
 * Copyright (c) 2026 Q2RTX FSR Vulkan contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef FFX_VK_FSR3_3_1_5_BRIDGE_H
#define FFX_VK_FSR3_3_1_5_BRIDGE_H

#include <vulkan/vulkan.h>

#if defined(__cplusplus)
extern "C" {
#endif

/*
 * Opaque implementation state used by the SDK 2.3 FfxInterface Vulkan
 * callbacks.  It intentionally contains no application-owned resources: the
 * embedding application supplies those through RegisterResource later in the
 * bridge.  Pipelines use the checked embedded Q2-compatible 3.1.5 modules.
 */
typedef struct FfxVkFsr3_3_1_5Bridge FfxVkFsr3_3_1_5Bridge;
typedef struct FfxVkFsr3_3_1_5UpscalerContext FfxVkFsr3_3_1_5UpscalerContext;

/* Opaque at this boundary: Q2RTX's v07 provider has a separate set of
 * legacy FfxApi* declarations, so leaking the public SDK types here prevents
 * both providers from coexisting in one renderer translation unit. */
typedef struct FfxVkFsr3_3_1_5Resource {
    void* resource;
} FfxVkFsr3_3_1_5Resource;

typedef struct FfxVkFsr3_3_1_5SharedResourceDescription {
    VkFormat format;
    uint32_t width;
    uint32_t height;
    uint32_t mipCount;
} FfxVkFsr3_3_1_5SharedResourceDescription;

enum {
    FFX_VK_FSR3_3_1_5_RESOURCE_STATE_COMPUTE_READ = (1u << 2),
    FFX_VK_FSR3_3_1_5_RESOURCE_STATE_UNORDERED_ACCESS = (1u << 1),
};

typedef enum FfxVkFsr3_3_1_5Result {
    FFX_VK_FSR3_3_1_5_OK = 0,
    FFX_VK_FSR3_3_1_5_ERROR_INVALID_ARGUMENT = -1,
    FFX_VK_FSR3_3_1_5_ERROR_OUT_OF_MEMORY = -2,
    FFX_VK_FSR3_3_1_5_ERROR_BACKEND = -3,
} FfxVkFsr3_3_1_5Result;

typedef struct FfxVkFsr3_3_1_5UpscalerCreateInfo {
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    uint32_t maxRenderWidth;
    uint32_t maxRenderHeight;
    uint32_t maxUpscaleWidth;
    uint32_t maxUpscaleHeight;
    VkBool32 hdrColorInput;
    VkBool32 autoExposure;
} FfxVkFsr3_3_1_5UpscalerCreateInfo;

typedef struct FfxVkFsr3_3_1_5UpscalerDispatchInfo {
    VkCommandBuffer commandBuffer;
    FfxVkFsr3_3_1_5Resource color;
    FfxVkFsr3_3_1_5Resource depth;
    FfxVkFsr3_3_1_5Resource motionVectors;
    FfxVkFsr3_3_1_5Resource exposure;
    FfxVkFsr3_3_1_5Resource reactive;
    FfxVkFsr3_3_1_5Resource transparencyAndComposition;
    FfxVkFsr3_3_1_5Resource dilatedDepth;
    FfxVkFsr3_3_1_5Resource dilatedMotionVectors;
    FfxVkFsr3_3_1_5Resource reconstructedPrevNearestDepth;
    FfxVkFsr3_3_1_5Resource output;
    float jitterOffsetX;
    float jitterOffsetY;
    float motionVectorScaleX;
    float motionVectorScaleY;
    uint32_t renderWidth;
    uint32_t renderHeight;
    uint32_t upscaleWidth;
    uint32_t upscaleHeight;
    float frameTimeMilliseconds;
    float preExposure;
    VkBool32 enableSharpening;
    float sharpness;
    VkBool32 reset;
    float cameraNear;
    float cameraFar;
    float cameraVerticalFovRadians;
    float viewSpaceToMeters;
    uint32_t flags;
    /* Monotonic caller frame ID. The caller retires this dispatch after its
     * submission fence signals, allowing transient descriptor/constant data
     * to be recycled instead of accumulating for the context lifetime. */
    uint64_t frameId;
} FfxVkFsr3_3_1_5UpscalerDispatchInfo;

typedef struct FfxVkFsr3_3_1_5SharedResourceDescriptions {
    FfxVkFsr3_3_1_5SharedResourceDescription dilatedDepth;
    FfxVkFsr3_3_1_5SharedResourceDescription dilatedMotionVectors;
    FfxVkFsr3_3_1_5SharedResourceDescription reconstructedPrevNearestDepth;
} FfxVkFsr3_3_1_5SharedResourceDescriptions;

/* SDK-reported FSR3 effect allocation. It excludes application-imported
 * temporal images, whose ownership remains with the caller. */
typedef struct FfxVkFsr3_3_1_5MemoryUsage {
    uint64_t totalUsageInBytes;
    uint64_t aliasableUsageInBytes;
} FfxVkFsr3_3_1_5MemoryUsage;

/* An application-owned 2D image supplied to the SDK 3.1.5 scheduler.  The
 * caller retains VkImage and memory ownership.  `layout` must describe the
 * image at import time and will be restored after each scheduler dispatch;
 * `state` is the matching FFX abstract state supplied in the resulting API
 * resource.  The bridge creates only temporary VkImageViews. */
typedef struct FfxVkFsr3_3_1_5ImportedImageDescription {
    VkImage image;
    VkFormat format;
    uint32_t width;
    uint32_t height;
    uint32_t mipCount;
    uint32_t arrayLayers;
    VkImageLayout layout;
    uint32_t state;
    VkImageUsageFlags usage;
} FfxVkFsr3_3_1_5ImportedImageDescription;

FfxVkFsr3_3_1_5Bridge* ffxVkFsr3_3_1_5CreateBridge(
    VkDevice device, const VkAllocationCallbacks* allocationCallbacks);

/* Required for SDK-owned image/buffer allocation callbacks.  The narrower
 * CreateBridge form remains useful for pipeline-only verification. */
FfxVkFsr3_3_1_5Bridge* ffxVkFsr3_3_1_5CreateBridgeWithPhysicalDevice(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    const VkAllocationCallbacks* allocationCallbacks);

void ffxVkFsr3_3_1_5DestroyBridge(FfxVkFsr3_3_1_5Bridge* bridge);

/* Low-level bridge frame boundary used by the opaque upscaler lifecycle. */
void ffxVkFsr3_3_1_5BridgeBeginFrame(FfxVkFsr3_3_1_5Bridge* bridge,
                                     uint64_t frameId);
void ffxVkFsr3_3_1_5BridgeRetireFrame(FfxVkFsr3_3_1_5Bridge* bridge,
                                      uint64_t completedFrameId);

/* Resolve an API resource token returned by the bridge to its native image.
 * The bridge retains ownership; callers may use this only while the resource
 * and bridge remain alive.  It is primarily useful for portable diagnostics,
 * readback, and an embedding application's explicit synchronization layer. */
VkImage ffxVkFsr3_3_1_5BridgeGetNativeImage(
    FfxVkFsr3_3_1_5Bridge* bridge, const void* resourceToken);

/* Import a caller-owned image and return the exact FFX resource token to put
 * into an FSR3.1.5 dispatch description.  Failure is represented by a zeroed
 * FfxVkFsr3_3_1_5Resource.  Release only destroys bridge-owned views; it never destroys
 * the caller's image or memory. */
FfxVkFsr3_3_1_5Resource ffxVkFsr3_3_1_5BridgeImportImage(
    FfxVkFsr3_3_1_5Bridge* bridge,
    const FfxVkFsr3_3_1_5ImportedImageDescription* description);

void ffxVkFsr3_3_1_5BridgeReleaseImportedImage(
    FfxVkFsr3_3_1_5Bridge* bridge, FfxVkFsr3_3_1_5Resource resource);

/* A small C lifecycle layer around the public SDK scheduler.  Context creation
 * queues the SDK's immutable-resource uploads; the first RecordDispatch emits
 * them before frame work in the caller command buffer, preserving Vulkan queue
 * ordering without an internal submit or queue wait. */
FfxVkFsr3_3_1_5Result ffxVkFsr3_3_1_5UpscalerContextCreate(
    const FfxVkFsr3_3_1_5UpscalerCreateInfo* createInfo,
    FfxVkFsr3_3_1_5UpscalerContext** outContext);

FfxVkFsr3_3_1_5Bridge* ffxVkFsr3_3_1_5UpscalerContextGetBridge(
    FfxVkFsr3_3_1_5UpscalerContext* context);

FfxVkFsr3_3_1_5Result ffxVkFsr3_3_1_5UpscalerContextGetSharedResourceDescriptions(
    FfxVkFsr3_3_1_5UpscalerContext* context,
    FfxVkFsr3_3_1_5SharedResourceDescriptions* outDescriptions);

FfxVkFsr3_3_1_5Result ffxVkFsr3_3_1_5UpscalerContextGetMemoryUsage(
    FfxVkFsr3_3_1_5UpscalerContext* context,
    FfxVkFsr3_3_1_5MemoryUsage* outUsage);

FfxVkFsr3_3_1_5Result ffxVkFsr3_3_1_5UpscalerContextRecordDispatch(
    FfxVkFsr3_3_1_5UpscalerContext* context,
    const FfxVkFsr3_3_1_5UpscalerDispatchInfo* dispatchInfo);

/* Retire all transient descriptor sets and constant buffers recorded through
 * completedFrameId after the matching GPU submission fence has signaled. */
void ffxVkFsr3_3_1_5UpscalerContextRetireFrame(
    FfxVkFsr3_3_1_5UpscalerContext* context, uint64_t completedFrameId);

void ffxVkFsr3_3_1_5UpscalerContextDestroy(
    FfxVkFsr3_3_1_5UpscalerContext* context);

/*
 * SDK 2.3's analytical FSR 3.1.6 optical-flow and frame-interpolation
 * scheduler.  It shares the versioned bridge above, but deliberately has a
 * separate public lifecycle: the 3.1.5 upscaler and 3.1.6 frame generator
 * can be linked into the same Vulkan application without exposing either
 * SDK's unversioned Ffx* symbols.
 *
 * RecordPrepare and RecordDispatch keep bridge-created VkImageViews alive
 * until RetireFrame.  Call RetireFrame only after the submission containing
 * both record calls has completed on the GPU (normally after its fence).
 * If either record call reports a backend error, conservatively retire after
 * the command buffer is no longer in use as it may contain partial work.
 * `completedFrameId` retires every recorded frame through that monotonic ID,
 * allowing an application to record several queue-ordered frames before the
 * corresponding fences signal.
 */
typedef struct FfxVkFsr3_3_1_6FrameGenerationContext
    FfxVkFsr3_3_1_6FrameGenerationContext;

/* Resident allocation owned by the consolidated FI/OF lifecycle. This includes
 * SDK-owned bridge resources and its five shared Vulkan images, but excludes
 * every imported application frame image. The bridge intentionally does no
 * Vulkan heap aliasing, so aliasableUsageInBytes is zero. */
typedef struct FfxVkFsr3_3_1_6FrameGenerationMemoryUsage {
    uint64_t totalUsageInBytes;
    uint64_t aliasableUsageInBytes;
} FfxVkFsr3_3_1_6FrameGenerationMemoryUsage;

typedef enum FfxVkFsr3_3_1_6FrameGenerationResult {
    FFX_VK_FSR3_3_1_6_FRAMEGEN_OK = 0,
    FFX_VK_FSR3_3_1_6_FRAMEGEN_ERROR_INVALID_ARGUMENT = -1,
    FFX_VK_FSR3_3_1_6_FRAMEGEN_ERROR_OUT_OF_MEMORY = -2,
    FFX_VK_FSR3_3_1_6_FRAMEGEN_ERROR_BACKEND = -3,
    FFX_VK_FSR3_3_1_6_FRAMEGEN_ERROR_IN_FLIGHT = -4,
} FfxVkFsr3_3_1_6FrameGenerationResult;

/* Matches the public SDK FfxApiBackbufferTransferFunction values without
 * exposing its unversioned headers through this reusable Vulkan ABI. */
typedef enum FfxVkFsr3_3_1_6FrameGenerationTransferFunction {
    FFX_VK_FSR3_3_1_6_FRAMEGEN_TRANSFER_SRGB = 0,
    FFX_VK_FSR3_3_1_6_FRAMEGEN_TRANSFER_PQ = 1,
    FFX_VK_FSR3_3_1_6_FRAMEGEN_TRANSFER_SCRGB = 2,
} FfxVkFsr3_3_1_6FrameGenerationTransferFunction;

typedef struct FfxVkFsr3_3_1_6FrameGenerationImage {
    VkImage image;
    VkFormat format;
    uint32_t width;
    uint32_t height;
    VkImageLayout layout;
    VkImageUsageFlags usage;
} FfxVkFsr3_3_1_6FrameGenerationImage;

typedef struct FfxVkFsr3_3_1_6FrameGenerationCreateInfo {
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    uint32_t maxRenderWidth;
    uint32_t maxRenderHeight;
    uint32_t displayWidth;
    uint32_t displayHeight;
    VkFormat colorFormat;
} FfxVkFsr3_3_1_6FrameGenerationCreateInfo;

typedef struct FfxVkFsr3_3_1_6FrameGenerationPrepareInfo {
    VkCommandBuffer commandBuffer;
    FfxVkFsr3_3_1_6FrameGenerationImage color;
    FfxVkFsr3_3_1_6FrameGenerationImage depth;
    FfxVkFsr3_3_1_6FrameGenerationImage motionVectors;
    uint32_t renderWidth;
    uint32_t renderHeight;
    float jitterOffsetX;
    float jitterOffsetY;
    float motionVectorScaleX;
    float motionVectorScaleY;
    float frameTimeMilliseconds;
    float minLuminance;
    float maxLuminance;
    FfxVkFsr3_3_1_6FrameGenerationTransferFunction transferFunction;
    float cameraNear;
    float cameraFar;
    float viewSpaceToMeters;
    float cameraVerticalFovRadians;
    float cameraPosition[3];
    float cameraUp[3];
    float cameraRight[3];
    float cameraForward[3];
    uint64_t frameId;
    VkBool32 reset;
} FfxVkFsr3_3_1_6FrameGenerationPrepareInfo;

/* All floating-point fields supplied to Prepare and Dispatch must be finite.
 * Timing, near/far, view-space scale, and vertical FOV must also be positive;
 * luminance minimum must be non-negative and maximum must not be below it. */

typedef struct FfxVkFsr3_3_1_6FrameGenerationDispatchInfo {
    VkCommandBuffer commandBuffer;
    FfxVkFsr3_3_1_6FrameGenerationImage color;
    /* Must be a distinct image from the color, depth, and motion inputs
     * supplied to RecordPrepare. Frame interpolation reads those inputs while
     * writing output; aliasing them corrupts temporal history. */
    FfxVkFsr3_3_1_6FrameGenerationImage output;
    /* Optional R16G16_SFLOAT field containing UV_after - UV_before.  Leave
     * image null when post-processing did not introduce lens/distortion
     * displacement; the SDK then uses its neutral internal 1x1 field. */
    FfxVkFsr3_3_1_6FrameGenerationImage distortionField;
    uint32_t displayWidth;
    uint32_t displayHeight;
    /* The all-zero rectangle selects the whole fixed display extent. Any
     * non-default rectangle must be non-empty and contained within it. */
    uint32_t interpolationX;
    uint32_t interpolationY;
    uint32_t interpolationWidth;
    uint32_t interpolationHeight;
    float frameTimeMilliseconds;
    float cameraNear;
    float cameraFar;
    float viewSpaceToMeters;
    float cameraVerticalFovRadians;
    float minLuminance;
    float maxLuminance;
    FfxVkFsr3_3_1_6FrameGenerationTransferFunction transferFunction;
    uint64_t frameId;
    VkBool32 reset;
} FfxVkFsr3_3_1_6FrameGenerationDispatchInfo;

FfxVkFsr3_3_1_6FrameGenerationResult
ffxVkFsr3_3_1_6FrameGenerationContextCreate(
    const FfxVkFsr3_3_1_6FrameGenerationCreateInfo* createInfo,
    FfxVkFsr3_3_1_6FrameGenerationContext** outContext);

FfxVkFsr3_3_1_6FrameGenerationResult
ffxVkFsr3_3_1_6FrameGenerationContextGetMemoryUsage(
    FfxVkFsr3_3_1_6FrameGenerationContext* context,
    FfxVkFsr3_3_1_6FrameGenerationMemoryUsage* outUsage);

FfxVkFsr3_3_1_6FrameGenerationResult
ffxVkFsr3_3_1_6FrameGenerationContextRecordPrepare(
    FfxVkFsr3_3_1_6FrameGenerationContext* context,
    const FfxVkFsr3_3_1_6FrameGenerationPrepareInfo* prepareInfo);

FfxVkFsr3_3_1_6FrameGenerationResult
ffxVkFsr3_3_1_6FrameGenerationContextRecordDispatch(
    FfxVkFsr3_3_1_6FrameGenerationContext* context,
    const FfxVkFsr3_3_1_6FrameGenerationDispatchInfo* dispatchInfo);

FfxVkFsr3_3_1_6FrameGenerationResult
ffxVkFsr3_3_1_6FrameGenerationContextRetireFrame(
    FfxVkFsr3_3_1_6FrameGenerationContext* context,
    uint64_t completedFrameId);

void ffxVkFsr3_3_1_6FrameGenerationContextDestroy(
    FfxVkFsr3_3_1_6FrameGenerationContext* context);

#if defined(__cplusplus)
}
#endif

#endif
