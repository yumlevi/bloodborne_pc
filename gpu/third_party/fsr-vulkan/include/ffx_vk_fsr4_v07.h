// Experimental source-v07 FSR4 INT8/DOT4 Vulkan backend.
//
// Drop-in replacement for the DX12 backend. Implements the FfxInterface
// callback table so the existing ffx_provider_fsr4 dispatch logic can run
// on Vulkan without modification.
//
// Requirements:
//   Vulkan 1.3  (or 1.2 + VK_KHR_shader_integer_dot_product)
//   VK_KHR_shader_float16_int8          (for int8 storage in SPIR-V)
//   VK_KHR_16bit_storage                (for float16 I/O)
//   VK_KHR_shader_integer_dot_product   (core in 1.3, promoted from extension)
//   Linear sampled-image filtering for R16G16B16A16_SFLOAT,
//     R8G8B8A8_UNORM, and R32_SFLOAT
//
// Build FSR4 INT8 shaders to SPIR-V with:
//   dxc -spirv -T cs_6_4 -enable-16bit-types -HV 2021
//       -DWMMA_ENABLED=0 -DFSR4_ENABLE_DOT4=1
//       -fspv-target-env=vulkan1.3
//       -fspv-extension=SPV_KHR_integer_dot_product
//       -I <sdk>/upscalers/fsr4/dx12
//       -I <sdk>/api/internal/dx12
//       -E <entry> <shader>.hlsl -Fo <shader>.spv

#pragma once

#include "ffx_vk_vulkan_compat.h"
#include <stdint.h>
#include <stddef.h>

#include "ffx_vk_fsr4_v07_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Opaque context handed back to the caller. Store the VkDevice and the
// allocator so the backend can create/destroy its own resources.
// ---------------------------------------------------------------------------
typedef struct FfxFsr4VkContext FfxFsr4VkContext;

// ---------------------------------------------------------------------------
// One SPIR-V blob. The backend deep-copies the bytecode during context
// creation, so caller storage may be released after a successful call.
// ---------------------------------------------------------------------------
typedef struct FfxFsr4VkShaderBlob {
    const uint32_t* spirv;      ///< Pointer to SPIR-V words
    size_t          sizeBytes;  ///< Size in bytes (must be multiple of 4)
    const char*     entryPoint; ///< e.g. "main" or the DXC-exported name
} FfxFsr4VkShaderBlob;

/*
 * The opaque FFX resource ABI carries only a VkImageView.  A Vulkan provider
 * cannot legally transition or restore that image without its VkImage and the
 * host's synchronization state.  Register every external image used by the
 * next dispatch with this record before calling ffxFsr4V07Dispatch.
 *
 * The provider transitions `layout` to GENERAL before its compute jobs and
 * restores `restoreLayout` while unregistering the frame's imports.  Stage and
 * access fields describe the corresponding producer/consumer operations.  A
 * GENERAL-to-GENERAL record is valid and still establishes the documented
 * compute visibility boundary.  The record may be updated every frame; it is
 * matched by `view` and does not transfer image/view ownership.
 */
typedef struct FfxFsr4VkExternalImageState {
    uint32_t                structSize;
    VkImage                 image;
    VkImageView             view;
    VkImageLayout           layout;
    VkPipelineStageFlags    stageMask;
    VkAccessFlags           accessMask;
    VkImageLayout           restoreLayout;
    VkPipelineStageFlags    restoreStageMask;
    VkAccessFlags           restoreAccessMask;
} FfxFsr4VkExternalImageState;

// FSR4 has: pass0 (pre), pass1-12 (model), pass13 (post), rcas, spd_auto_exposure
#define FFX_FSR4_VK_PASS_COUNT 16

typedef struct FfxFsr4VkCreateInfo {
    VkDevice                    device;
    VkPhysicalDevice            physicalDevice;

    // Optional custom allocator; pass NULL for default VkAllocationCallbacks
    const VkAllocationCallbacks* allocator;

    // All SPIR-V blobs for each shader pass.
    // Index mapping (same as DX12 pass IDs):
    //   [0]    = pre-pass  (fsr4_model_v07_i8_<preset>/pre.hlsl)
    //   [1-12] = model passes (passes_<res>.hlsl, MLSR_PASS_N define)
    //   [13]   = post-pass (post.hlsl)
    //   [14]   = RCAS sharpening (rcas.hlsl)
    //   [15]   = SPD auto-exposure (spd_auto_exposure.hlsl)
    FfxFsr4VkShaderBlob shaders[FFX_FSR4_VK_PASS_COUNT];

    /*
     * Model data is deliberately supplied separately from shader bytecode.
     * The generated INT8 passes read an initializer buffer and the pre-pass
     * reads a second, 1 KiB constant block.  Omitting either produces valid
     * Vulkan dispatches but undefined neural-network output.
     *
     * The backend deep-copies both blobs during creation.
     */
    const void* modelInitializer;
    size_t      modelInitializerSize;
    const void* prePassWeights;
    size_t      prePassWeightsSize;

    // Scratch memory for the backend's internal bookkeeping.
    // Allocate at least ffxFsr4VkGetScratchMemorySize() bytes.
    void*  scratchBuffer;
    size_t scratchBufferSize;
} FfxFsr4VkCreateInfo;

// ---------------------------------------------------------------------------
// Query minimum scratch memory required.
// ---------------------------------------------------------------------------
size_t ffxFsr4VkGetScratchMemorySize(void);

// Validate that a generated FSR4-v07 SPIR-V module uses only the descriptor
// set/binding/type ABI accepted by this portable provider.  Applications can
// use this before retaining a third-party asset bundle; CreateContext invokes
// the same validation for every supplied module.
VkResult ffxFsr4VkValidateShaderLayout(const FfxFsr4VkShaderBlob* shader,
                                       uint32_t passIndex);

// ---------------------------------------------------------------------------
// Create a Vulkan backend context and fill in the FfxInterface table.
// outInterface->device will be set to the opaque FfxFsr4VkContext*.
// ---------------------------------------------------------------------------
VkResult ffxFsr4VkCreateContext(
    const FfxFsr4VkCreateInfo* createInfo,
    FfxInterface*              outInterface   ///< Populated on success
);

// ---------------------------------------------------------------------------
// Destroy and free all Vulkan objects created by ffxFsr4VkCreateContext().
// Call AFTER destroying the FfxEffectContext that used this backend.
// ---------------------------------------------------------------------------
void ffxFsr4VkDestroyContext(FfxFsr4VkContext* ctx);

// ---------------------------------------------------------------------------
// Explicit per-frame lifetime.  Call BeginFrame before the provider records a
// dispatch into a command buffer.  Call RetireFrame only after the host fence
// proves every submission for all frame IDs through completedFrameId has
// finished.  This makes descriptor-set and host-visible constant-buffer reuse
// safe for arbitrary Vulkan frame-in-flight counts; it does not rely on a
// renderer-specific pool-cycling convention.
//
// The backend retains at most three unretired dispatches.  BeginFrame returns
// VK_NOT_READY when all three are still in flight.  Frame IDs are host-defined
// monotonically increasing values (zero is valid).
// ---------------------------------------------------------------------------
VkResult ffxFsr4VkBeginFrame(FfxInterface* iface, uint64_t frameId);
VkResult ffxFsr4VkRetireFrame(FfxInterface* iface, uint64_t completedFrameId);

/* Register or update host-owned state for one externally imported image.
 * Unlike provider-owned resources, imports are never assumed to start in
 * GENERAL.  Returns VK_ERROR_INITIALIZATION_FAILED for an invalid interface,
 * VK_ERROR_VALIDATION_FAILED_EXT for an incomplete state record, and
 * VK_ERROR_OUT_OF_POOL_MEMORY when the bounded per-context registry is full. */
VkResult ffxFsr4VkSetExternalImageState(
    FfxInterface* iface, const FfxFsr4VkExternalImageState* state);

// ---------------------------------------------------------------------------
// Helpers for provider-side direct access to backend-owned resources.
//
// Retrieve the underlying VkImage / dimensions for a resource previously
// registered or created via the FfxInterface.  Returns VK_NULL_HANDLE / 0
// if the index is invalid or the resource is not an image.
//
// Intended for narrow cases where the provider needs to issue its own
// Vulkan commands (e.g. vkCmdClearColorImage on an internal history
// texture) that the FfxInterface job abstraction does not expose.
// ---------------------------------------------------------------------------
VkImage  ffxFsr4VkGetImage (FfxInterface* iface, FfxResourceInternal ri);
uint32_t ffxFsr4VkGetWidth (FfxInterface* iface, FfxResourceInternal ri);
uint32_t ffxFsr4VkGetHeight(FfxInterface* iface, FfxResourceInternal ri);

// Returns non-zero if the resource is still in VK_IMAGE_LAYOUT_UNDEFINED
// (i.e. ExecuteGpuJobs hasn't transitioned it to GENERAL yet).  The caller
// is responsible for the subsequent layout transition; if it does one,
// it should call ffxFsr4VkMarkImageInitialized() to clear the flag so
// ExecuteGpuJobs doesn't redundantly transition again.
int  ffxFsr4VkNeedsInitialTransition(FfxInterface* iface, FfxResourceInternal ri);
void ffxFsr4VkMarkImageInitialized  (FfxInterface* iface, FfxResourceInternal ri);

/* Provider-side access to the backend-owned deep copies of model data. */
const void* ffxFsr4VkGetModelInitializer(FfxInterface* iface, size_t* sizeBytes);
const void* ffxFsr4VkGetPrePassWeights(FfxInterface* iface, size_t* sizeBytes);

#ifdef __cplusplus
}
#endif
