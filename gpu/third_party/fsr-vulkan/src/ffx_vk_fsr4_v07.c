// ffx_fsr4_vk.c  —  Vulkan backend for FidelityFX FSR4 (INT8 / dot4add path)
//
// Implements all 19 FfxInterface function pointers so the existing
// ffx_provider_fsr4 dispatch logic runs on Vulkan unchanged.
//
// The generated graph uses two descriptor ABIs:
//
//   model passes (1-12): storage buffers
//     Compiled with: -fvk-t-shift 0 0 -fvk-u-shift 2 0
//     bind 0  STORAGE_BUFFER  input SRV    (t0 ByteAddressBuffer)
//     bind 1  STORAGE_BUFFER  weights SRV  (t1 ByteAddressBuffer)
//     bind 2  STORAGE_BUFFER  output UAV   (u0 RWByteAddressBuffer)
//     bind 3  STORAGE_BUFFER  scratch UAV  (u1 RWByteAddressBuffer)
//
//   full passes (0, 13, 14, 15): textures + images + buffers + cbuffers
//     Compiled with: -fvk-t-shift 0 0 -fvk-s-shift 35 0 -fvk-u-shift 21 0 -fvk-b-shift 43 0
//     bind  0..20  SAMPLED_IMAGE           t0..t20 (Texture2D SRVs)
//     bind 21..33  STORAGE_IMAGE           u0..u12 (RWTexture2D UAVs)
//     bind 32      STORAGE_BUFFER          u11=ScratchBuffer (pre/post/spd)
//                  STORAGE_IMAGE           u11=rw_rcas_output (rcas only)
//     bind 34      UNIFORM_BUFFER          cbPass_Weights
//     bind 35      SAMPLER                 s0
//     bind 43      UNIFORM_BUFFER          b0 (MLSR_Optimized_Constants / cbRCAS / etc.)

#include "ffx_vk_fsr4_v07.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ── tuneable limits ──────────────────────────────────────────────────────────
#define MAX_RESOURCES       192
#define MAX_PIPELINES        32
#define MAX_PENDING_JOBS     96
#define MAX_DESC_SETS        64   /* per pool — up to 16 FSR4/SPD/RCAS passes */
#define NUM_POOL_FRAMES       3   /* maximum unretired host dispatches */
#define MAX_STAGING          32
#define MAX_EXTERNAL_IMAGES   8   /* color, depth, motion, output + host slack */
#define PROF_MAX_JOBS        32   /* timestamped dispatches per frame */
#define CBUF_FRAME_BYTES   (512 * 1024)
#define CBUF_RING_BYTES    (CBUF_FRAME_BYTES * NUM_POOL_FRAMES)

// Slot offsets inside LAYOUT_FULL (matching -fvk-*-shift flags)
#define SLOT_SRV_TEX_BASE    0    // t0..t20 → SAMPLED_IMAGE
#define SLOT_UAV_BASE       21    // u0..u12 → STORAGE_IMAGE (or STORAGE_BUFFER for u11)
#define SLOT_SCRATCH        32    // u11+21  (STORAGE_BUFFER in pre/post, STORAGE_IMAGE in rcas)
#define SLOT_CBUF_WEIGHTS   34    // cbPass_Weights → UNIFORM_BUFFER
#define SLOT_SAMPLER        35    // s0+35    → SAMPLER
#define SLOT_CBUF_MAIN      43    // b0+43   → UNIFORM_BUFFER
#define FULL_LAYOUT_COUNT   44    // bindings 0..43

/* Minimal SPIR-V reflection used to prove that an opaque generated module
 * agrees with this provider's descriptor ABI.  Keeping this small avoids a
 * new runtime dependency for Vulkan applications while preventing a newer
 * shader bundle from silently being paired with the old hand-authored layout.
 * Values are the stable SPIR-V 1.x opcode/decoration/storage-class values. */
#define SPV_MAGIC_NUMBER              0x07230203u
#define SPV_OP_TYPE_IMAGE             25u
#define SPV_OP_TYPE_SAMPLER           26u
#define SPV_OP_TYPE_SAMPLED_IMAGE     27u
#define SPV_OP_TYPE_ARRAY             28u
#define SPV_OP_TYPE_RUNTIME_ARRAY     29u
#define SPV_OP_TYPE_STRUCT            30u
#define SPV_OP_TYPE_POINTER           32u
#define SPV_OP_VARIABLE               59u
#define SPV_OP_DECORATE               71u
#define SPV_DECORATION_BINDING         33u
#define SPV_DECORATION_DESCRIPTOR_SET  34u
#define SPV_DECORATION_BUFFER_BLOCK     3u
#define SPV_STORAGE_UNIFORM_CONSTANT    0u
#define SPV_STORAGE_UNIFORM             2u
#define SPV_STORAGE_STORAGE_BUFFER     12u

typedef enum {
    SPV_TYPE_NONE = 0,
    SPV_TYPE_IMAGE,
    SPV_TYPE_SAMPLER,
    SPV_TYPE_SAMPLED_IMAGE,
    SPV_TYPE_ARRAY,
    SPV_TYPE_RUNTIME_ARRAY,
    SPV_TYPE_STRUCT,
    SPV_TYPE_POINTER,
} SpirvTypeKind;

// ── internal types ───────────────────────────────────────────────────────────

typedef enum { RES_NONE=0, RES_BUFFER=1, RES_IMAGE=2 } ResKind;

typedef struct {
    ResKind        kind;
    VkBuffer       buf;
    VkDeviceMemory mem;
    VkDeviceSize   size;
    VkImage        img;
    VkImageView    view;
    VkFormat       fmt;
    uint32_t       w, h;
    VkDeviceSize   allocationSize;
    int            external;
    int            needsInit;  /* 1 = image needs UNDEFINED→GENERAL transition */
    uint32_t       apiState;
    VkImageLayout  layout;
    VkPipelineStageFlags stageMask;
    VkAccessFlags  accessMask;
    VkImageLayout  restoreLayout;
    VkPipelineStageFlags restoreStageMask;
    VkAccessFlags  restoreAccessMask;
    int            externalStateKnown;
    int            externalTouched;
} VkRes;

typedef struct {
    FfxFsr4VkExternalImageState state;
} ExternalImageState;

typedef enum { PIPE_MODEL=0, PIPE_FULL=1 } PipeKind;

typedef struct {
    VkDescriptorSetLayoutBinding bindings[FULL_LAYOUT_COUNT];
    VkBool32 bindingPresent[FULL_LAYOUT_COUNT];
    uint32_t bindingCount;
} SpirvDescriptorLayout;

typedef struct {
    VkPipeline            pipeline;
    VkPipelineLayout      layout;
    VkDescriptorSetLayout dsLayout;
    PipeKind              kind;
    VkBool32              bindingPresent[FULL_LAYOUT_COUNT];
} VkPipe;

typedef struct { FfxGpuJobDescription d; } Job;

typedef struct {
    VkBuffer       buf;
    VkDeviceMemory mem;
    uint32_t       dstIdx;
    VkDeviceSize   size;
    VkDeviceSize   allocationSize;
    int            submitted;
    uint64_t       frameId;
} Staging;

typedef struct {
    uint64_t frameId;
    uint32_t cbOff;
    VkBool32 inUse;
} FrameLifetime;

struct FfxFsr4VkContext {
    VkDevice                     dev;
    VkPhysicalDevice             phys;
    const VkAllocationCallbacks* alloc;

    VkDescriptorPool  pool[NUM_POOL_FRAMES];
    VkSampler         linearSampler;

    VkBuffer       cbRing;
    VkDeviceMemory cbMem;
    uint8_t*       cbMap;
    FrameLifetime  frame[NUM_POOL_FRAMES];
    uint32_t       recordingFrame;
    VkDeviceSize   cbAllocationSize;

    VkRes    res[MAX_RESOURCES];
    uint32_t resCount;
    ExternalImageState externalImages[MAX_EXTERNAL_IMAGES];
    uint32_t externalImageCount;

    VkPipe   pipe[MAX_PIPELINES];

    Job      jobs[MAX_PENDING_JOBS];
    uint32_t jobCount;

    Staging  staging[MAX_STAGING];
    uint32_t stagingCount;

    FfxFsr4VkShaderBlob blobs[FFX_FSR4_VK_PASS_COUNT];
    void*     modelInitializer;
    size_t    modelInitializerSize;
    void*     prePassWeights;
    size_t    prePassWeightsSize;
    VkBool32  fp16Supported;
    VkBool32  int8Supported;
    VkBool32  dotProductSupported;
    VkBool32  computeDerivativesSupported;

    /* bbport: BB_FSR4_PROFILE=1 — GPU time per pass (timestamps around each
     * dispatch, read back when the frame retires, printed every 300 frames). */
    VkQueryPool profPool;
    float       profPeriodNs;
    uint32_t    profCount[NUM_POOL_FRAMES];
    uint32_t    profPass[NUM_POOL_FRAMES][PROF_MAX_JOBS];
    double      profMs[MAX_PIPELINES];
    uint32_t    profCalls[MAX_PIPELINES];
    uint64_t    profFrames;
};

static FfxErrorCode restoreExternalImages(FfxFsr4VkContext* c,
                                          VkCommandBuffer cmd);

static VkDescriptorType expectedDescriptorType(uint32_t passIdx, uint32_t binding)
{
    if (passIdx >= FFX_FSR4_VK_PASS_COUNT) return VK_DESCRIPTOR_TYPE_MAX_ENUM;
    if (passIdx >= 1u && passIdx <= 12u)
        return binding < 4u ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                            : VK_DESCRIPTOR_TYPE_MAX_ENUM;
    if (binding <= 20u) return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    if (binding >= 21u && binding <= 33u) {
        if (binding == SLOT_SCRATCH && passIdx != 14u)
            return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    }
    if (binding == SLOT_CBUF_WEIGHTS || binding == SLOT_CBUF_MAIN)
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    if (binding == SLOT_SAMPLER) return VK_DESCRIPTOR_TYPE_SAMPLER;
    return VK_DESCRIPTOR_TYPE_MAX_ENUM;
}

static const FfxFsr4VkExternalImageState* findExternalImageState(
    const FfxFsr4VkContext* c, VkImageView view)
{
    if (!c || !view) return NULL;
    for (uint32_t i = 0; i < c->externalImageCount; ++i) {
        if (c->externalImages[i].state.view == view)
            return &c->externalImages[i].state;
    }
    return NULL;
}

static VkResult reflectShaderLayout(const FfxFsr4VkShaderBlob* shader,
                                    uint32_t passIdx,
                                    SpirvDescriptorLayout* layout)
{
    const uint32_t *words;
    const size_t wordCount = shader ? shader->sizeBytes / sizeof(uint32_t) : 0u;
    uint32_t bound;
    uint8_t *typeKind = NULL, *typeImageSampled = NULL, *isBufferBlock = NULL;
    uint32_t *typeElement = NULL, *varType = NULL, *varStorage = NULL;
    uint32_t *binding = NULL, *descriptorSet = NULL;
    VkResult result = VK_ERROR_INVALID_SHADER_NV;

    if (layout) memset(layout, 0, sizeof(*layout));
    if (!shader || !shader->spirv || !wordCount ||
        shader->sizeBytes % sizeof(uint32_t) || passIdx >= FFX_FSR4_VK_PASS_COUNT)
        return VK_ERROR_INVALID_SHADER_NV;
    words = shader->spirv;
    if (wordCount < 5u || words[0] != SPV_MAGIC_NUMBER || !(bound = words[3]) ||
        bound > (1u << 20u))
        return VK_ERROR_INVALID_SHADER_NV;

    typeKind = calloc(bound, sizeof(*typeKind));
    typeImageSampled = calloc(bound, sizeof(*typeImageSampled));
    isBufferBlock = calloc(bound, sizeof(*isBufferBlock));
    typeElement = calloc(bound, sizeof(*typeElement));
    varType = calloc(bound, sizeof(*varType));
    varStorage = calloc(bound, sizeof(*varStorage));
    binding = malloc(bound * sizeof(*binding));
    descriptorSet = malloc(bound * sizeof(*descriptorSet));
    if (!typeKind || !typeImageSampled || !isBufferBlock || !typeElement ||
        !varType || !varStorage || !binding || !descriptorSet) {
        result = VK_ERROR_OUT_OF_HOST_MEMORY;
        goto cleanup;
    }
    for (uint32_t i = 0; i < bound; ++i) {
        binding[i] = UINT32_MAX;
        descriptorSet[i] = UINT32_MAX;
    }

    for (size_t offset = 5u; offset < wordCount;) {
        const uint32_t instruction = words[offset];
        const uint32_t instructionWords = instruction >> 16u;
        const uint32_t opcode = instruction & 0xffffu;
        if (!instructionWords || instructionWords > wordCount - offset)
            goto cleanup;
        switch (opcode) {
        case SPV_OP_DECORATE:
            if (instructionWords >= 4u && words[offset + 1u] < bound) {
                const uint32_t target = words[offset + 1u];
                const uint32_t decoration = words[offset + 2u];
                if (decoration == SPV_DECORATION_BINDING)
                    binding[target] = words[offset + 3u];
                else if (decoration == SPV_DECORATION_DESCRIPTOR_SET)
                    descriptorSet[target] = words[offset + 3u];
                else if (decoration == SPV_DECORATION_BUFFER_BLOCK)
                    isBufferBlock[target] = 1u;
            }
            break;
        case SPV_OP_TYPE_IMAGE:
            if (instructionWords >= 9u && words[offset + 1u] < bound) {
                typeKind[words[offset + 1u]] = SPV_TYPE_IMAGE;
                typeImageSampled[words[offset + 1u]] = (uint8_t)words[offset + 7u];
            }
            break;
        case SPV_OP_TYPE_SAMPLER:
            if (instructionWords >= 2u && words[offset + 1u] < bound)
                typeKind[words[offset + 1u]] = SPV_TYPE_SAMPLER;
            break;
        case SPV_OP_TYPE_SAMPLED_IMAGE:
            if (instructionWords >= 3u && words[offset + 1u] < bound) {
                typeKind[words[offset + 1u]] = SPV_TYPE_SAMPLED_IMAGE;
                typeElement[words[offset + 1u]] = words[offset + 2u];
            }
            break;
        case SPV_OP_TYPE_ARRAY:
        case SPV_OP_TYPE_RUNTIME_ARRAY:
            if (instructionWords >= 3u && words[offset + 1u] < bound) {
                typeKind[words[offset + 1u]] = opcode == SPV_OP_TYPE_ARRAY
                    ? SPV_TYPE_ARRAY : SPV_TYPE_RUNTIME_ARRAY;
                typeElement[words[offset + 1u]] = words[offset + 2u];
            }
            break;
        case SPV_OP_TYPE_STRUCT:
            if (instructionWords >= 2u && words[offset + 1u] < bound)
                typeKind[words[offset + 1u]] = SPV_TYPE_STRUCT;
            break;
        case SPV_OP_TYPE_POINTER:
            if (instructionWords >= 4u && words[offset + 1u] < bound) {
                typeKind[words[offset + 1u]] = SPV_TYPE_POINTER;
                typeElement[words[offset + 1u]] = words[offset + 3u];
            }
            break;
        case SPV_OP_VARIABLE:
            if (instructionWords >= 4u && words[offset + 2u] < bound) {
                varType[words[offset + 2u]] = words[offset + 1u];
                varStorage[words[offset + 2u]] = words[offset + 3u];
            }
            break;
        default:
            break;
        }
        offset += instructionWords;
    }

    {
        uint8_t seen[FULL_LAYOUT_COUNT] = {0};
        for (uint32_t id = 0; id < bound; ++id) {
            VkDescriptorType actual;
            uint32_t type = varType[id];
            uint32_t descriptorBinding = binding[id];
            if (!type || descriptorBinding == UINT32_MAX)
                continue;
            if (descriptorSet[id] != 0u || descriptorBinding >= FULL_LAYOUT_COUNT ||
                seen[descriptorBinding])
                goto cleanup;
            while (type < bound &&
                   (typeKind[type] == SPV_TYPE_POINTER ||
                    typeKind[type] == SPV_TYPE_ARRAY ||
                    typeKind[type] == SPV_TYPE_RUNTIME_ARRAY))
                type = typeElement[type];
            if (type >= bound) goto cleanup;
            if (varStorage[id] == SPV_STORAGE_UNIFORM_CONSTANT) {
                if (typeKind[type] == SPV_TYPE_SAMPLER)
                    actual = VK_DESCRIPTOR_TYPE_SAMPLER;
                else if (typeKind[type] == SPV_TYPE_IMAGE && typeImageSampled[type] == 1u)
                    actual = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                else if (typeKind[type] == SPV_TYPE_IMAGE && typeImageSampled[type] == 2u)
                    actual = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                else
                    goto cleanup;
            } else if (varStorage[id] == SPV_STORAGE_STORAGE_BUFFER ||
                       (varStorage[id] == SPV_STORAGE_UNIFORM && isBufferBlock[type])) {
                actual = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            } else if (varStorage[id] == SPV_STORAGE_UNIFORM) {
                actual = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            } else {
                goto cleanup;
            }
            if (expectedDescriptorType(passIdx, descriptorBinding) != actual)
                goto cleanup;
            seen[descriptorBinding] = 1u;
            if (layout) {
                VkDescriptorSetLayoutBinding* reflected =
                    &layout->bindings[layout->bindingCount++];
                reflected->binding = descriptorBinding;
                reflected->descriptorType = actual;
                reflected->descriptorCount = 1u;
                reflected->stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
                layout->bindingPresent[descriptorBinding] = VK_TRUE;
            }
        }
    }
    result = VK_SUCCESS;

cleanup:
    free(descriptorSet);
    free(binding);
    free(varStorage);
    free(varType);
    free(typeElement);
    free(isBufferBlock);
    free(typeImageSampled);
    free(typeKind);
    return result;
}

VkResult ffxFsr4VkValidateShaderLayout(const FfxFsr4VkShaderBlob* shader,
                                       uint32_t passIdx)
{
    return reflectShaderLayout(shader, passIdx, NULL);
}

// ── helpers ──────────────────────────────────────────────────────────────────

static uint32_t findMemType(VkPhysicalDevice phys, uint32_t bits, VkMemoryPropertyFlags f)
{
    VkPhysicalDeviceMemoryProperties p;
    vkGetPhysicalDeviceMemoryProperties(phys, &p);
    for (uint32_t i = 0; i < p.memoryTypeCount; i++)
        if ((bits & (1u<<i)) && (p.memoryTypes[i].propertyFlags & f) == f)
            return i;
    return UINT32_MAX;
}

static int hasDeviceExtension(VkPhysicalDevice physicalDevice, const char* name)
{
    uint32_t count = 0;
    VkExtensionProperties* properties;
    int found = 0;

    if (vkEnumerateDeviceExtensionProperties(physicalDevice, NULL, &count, NULL) != VK_SUCCESS ||
        !count)
        return 0;
    properties = (VkExtensionProperties*)malloc(sizeof(*properties) * count);
    if (!properties) return 0;
    if (vkEnumerateDeviceExtensionProperties(physicalDevice, NULL, &count,
                                              properties) == VK_SUCCESS) {
        for (uint32_t i = 0; i < count; ++i) {
            if (strcmp(properties[i].extensionName, name) == 0) {
                found = 1;
                break;
            }
        }
    }
    free(properties);
    return found;
}

static VkResult queryRequiredFeatures(FfxFsr4VkContext* c)
{
    VkPhysicalDeviceVulkan13Features features13;
    VkPhysicalDeviceVulkan12Features features12;
    VkPhysicalDeviceFeatures2 features2;
    VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR derivatives;
    const int hasDerivatives = hasDeviceExtension(
        c->phys, VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME);

    memset(&features13, 0, sizeof(features13));
    memset(&features12, 0, sizeof(features12));
    memset(&features2, 0, sizeof(features2));
    memset(&derivatives, 0, sizeof(derivatives));
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    derivatives.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR;
    features2.pNext = &features13;
    features13.pNext = &features12;
    if (hasDerivatives)
        features12.pNext = &derivatives;
    vkGetPhysicalDeviceFeatures2(c->phys, &features2);

    c->fp16Supported = features12.shaderFloat16;
    c->int8Supported = features12.shaderInt8;
    c->dotProductSupported = features13.shaderIntegerDotProduct;
    c->computeDerivativesSupported = hasDerivatives &&
        derivatives.computeDerivativeGroupLinear;

    if (!c->fp16Supported || !c->int8Supported ||
        !c->dotProductSupported || !c->computeDerivativesSupported ||
        !features2.features.shaderInt16 ||
        !features2.features.shaderStorageImageExtendedFormats ||
        !features2.features.shaderStorageImageWriteWithoutFormat)
        return VK_ERROR_FEATURE_NOT_PRESENT;
    return VK_SUCCESS;
}

static int formatSupportsLinearSampling(VkPhysicalDevice physicalDevice,
                                        VkFormat format)
{
    VkFormatProperties properties;
    const VkFormatFeatureFlags required =
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;

    memset(&properties, 0, sizeof(properties));
    vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &properties);
    return (properties.optimalTilingFeatures & required) == required;
}

static VkResult queryRequiredFormats(FfxFsr4VkContext* c)
{
    /* Every image sampled by the current pre/post graph uses the one immutable
     * linear sampler.  Check the concrete Q2RTX/provider formats up front so a
     * device cannot reach descriptor creation with an unsupported format. */
    static const VkFormat sampledFormats[] = {
        VK_FORMAT_R16G16B16A16_SFLOAT, /* color, motion, history, reprojected */
        VK_FORMAT_R8G8B8A8_UNORM,      /* recurrent state */
        VK_FORMAT_R32_SFLOAT,           /* view-Z and explicit exposure */
    };

    for (size_t i = 0; i < sizeof(sampledFormats) / sizeof(sampledFormats[0]); ++i) {
        if (!formatSupportsLinearSampling(c->phys, sampledFormats[i]))
            return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }
    return VK_SUCCESS;
}

static VkResult mkBuf(FfxFsr4VkContext* c, VkDeviceSize sz,
                      VkBufferUsageFlags usage, VkMemoryPropertyFlags mf,
                      VkBuffer* ob, VkDeviceMemory* om,
                      VkDeviceSize* allocationSize)
{
    if (!ob || !om || !allocationSize || !sz) return VK_ERROR_INITIALIZATION_FAILED;
    *ob = VK_NULL_HANDLE;
    *om = VK_NULL_HANDLE;
    *allocationSize = 0;
    VkBufferCreateInfo bi;
    memset(&bi, 0, sizeof(bi));
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size=sz; bi.usage=usage; bi.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    VkResult r = vkCreateBuffer(c->dev, &bi, c->alloc, ob);
    if (r) return r;
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(c->dev, *ob, &mr);
    uint32_t mt = findMemType(c->phys, mr.memoryTypeBits, mf);
    if (mt == UINT32_MAX) {
        vkDestroyBuffer(c->dev, *ob, c->alloc);
        *ob = VK_NULL_HANDLE;
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    VkMemoryAllocateInfo ai;
    memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize=mr.size; ai.memoryTypeIndex=mt;
    r = vkAllocateMemory(c->dev, &ai, c->alloc, om);
    if (r) {
        vkDestroyBuffer(c->dev, *ob, c->alloc);
        *ob = VK_NULL_HANDLE;
        return r;
    }
    r = vkBindBufferMemory(c->dev, *ob, *om, 0);
    if (r) {
        vkFreeMemory(c->dev, *om, c->alloc);
        vkDestroyBuffer(c->dev, *ob, c->alloc);
        *om = VK_NULL_HANDLE;
        *ob = VK_NULL_HANDLE;
    } else {
        *allocationSize = mr.size;
    }
    return r;
}

static VkResult mkImg(FfxFsr4VkContext* c, uint32_t w, uint32_t h,
                      VkFormat fmt, VkImageUsageFlags usage,
                      VkImage* oi, VkDeviceMemory* om, VkImageView* ov,
                      VkDeviceSize* allocationSize)
{
    if (!oi || !om || !ov || !allocationSize || !w || !h || fmt == VK_FORMAT_UNDEFINED)
        return VK_ERROR_INITIALIZATION_FAILED;
    *oi = VK_NULL_HANDLE;
    *om = VK_NULL_HANDLE;
    *ov = VK_NULL_HANDLE;
    *allocationSize = 0;
    VkImageCreateInfo ii;
    memset(&ii, 0, sizeof(ii));
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType=VK_IMAGE_TYPE_2D; ii.format=fmt;
    ii.extent=(VkExtent3D){w, h>0?h:1, 1};
    ii.mipLevels=1; ii.arrayLayers=1;
    ii.samples=VK_SAMPLE_COUNT_1_BIT;
    ii.tiling=VK_IMAGE_TILING_OPTIMAL; ii.usage=usage;
    ii.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
    VkResult r = vkCreateImage(c->dev, &ii, c->alloc, oi);
    if (r) return r;
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(c->dev, *oi, &mr);
    uint32_t mt = findMemType(c->phys, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mt == UINT32_MAX) {
        vkDestroyImage(c->dev, *oi, c->alloc);
        *oi = VK_NULL_HANDLE;
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    VkMemoryAllocateInfo ai;
    memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize=mr.size; ai.memoryTypeIndex=mt;
    r = vkAllocateMemory(c->dev, &ai, c->alloc, om);
    if (r) {
        vkDestroyImage(c->dev, *oi, c->alloc);
        *oi = VK_NULL_HANDLE;
        return r;
    }
    r = vkBindImageMemory(c->dev, *oi, *om, 0);
    if (r) {
        vkFreeMemory(c->dev, *om, c->alloc);
        vkDestroyImage(c->dev, *oi, c->alloc);
        *om = VK_NULL_HANDLE;
        *oi = VK_NULL_HANDLE;
        return r;
    }
    VkImageViewCreateInfo vi;
    memset(&vi, 0, sizeof(vi));
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image=*oi; vi.viewType=VK_IMAGE_VIEW_TYPE_2D; vi.format=fmt;
    vi.subresourceRange=(VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    r = vkCreateImageView(c->dev, &vi, c->alloc, ov);
    if (r) {
        vkFreeMemory(c->dev, *om, c->alloc);
        vkDestroyImage(c->dev, *oi, c->alloc);
        *om = VK_NULL_HANDLE;
        *oi = VK_NULL_HANDLE;
    } else {
        *allocationSize = mr.size;
    }
    return r;
}

static VkFormat toVkFmt(FfxSurfaceFormat f)
{
    switch(f) {
    case FFX_SURFACE_FORMAT_R32G32B32A32_FLOAT:  return VK_FORMAT_R32G32B32A32_SFLOAT;
    case FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT:  return VK_FORMAT_R16G16B16A16_SFLOAT;
    case FFX_SURFACE_FORMAT_R16G16_FLOAT:        return VK_FORMAT_R16G16_SFLOAT;
    case FFX_SURFACE_FORMAT_R32_FLOAT:           return VK_FORMAT_R32_SFLOAT;
    case FFX_SURFACE_FORMAT_R32_UINT:            return VK_FORMAT_R32_UINT;
    case FFX_SURFACE_FORMAT_R16_FLOAT:           return VK_FORMAT_R16_SFLOAT;
    case FFX_SURFACE_FORMAT_R8G8B8A8_UNORM:      return VK_FORMAT_R8G8B8A8_UNORM;
    case FFX_SURFACE_FORMAT_R11G11B10_FLOAT:     return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    case FFX_SURFACE_FORMAT_R16G16_UINT:         return VK_FORMAT_R16G16_UINT;
    case FFX_SURFACE_FORMAT_R8_UINT:             return VK_FORMAT_R8_UINT;
    case FFX_SURFACE_FORMAT_R8_UNORM:            return VK_FORMAT_R8_UNORM;
    default:                                      return VK_FORMAT_UNDEFINED;
    }
}

// ── pipeline creation ─────────────────────────────────────────────────────────

/* bbport: BB_FSR4_STATS=1 (the device must enable pipelineExecutableInfo) prints the
 * driver's statistics of each pass: registers, spills, instruction counts. */
static void printPipelineStats(FfxFsr4VkContext* c, uint32_t passIdx, VkPipeline pipeline)
{
    PFN_vkGetPipelineExecutableStatisticsKHR getStats =
        (PFN_vkGetPipelineExecutableStatisticsKHR)vkGetDeviceProcAddr(
            c->dev, "vkGetPipelineExecutableStatisticsKHR");
    if (!getStats) return;
    VkPipelineExecutableInfoKHR info;
    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR;
    info.pipeline = pipeline;
    uint32_t count = 0;
    if (getStats(c->dev, &info, &count, NULL) != VK_SUCCESS || count == 0) return;
    VkPipelineExecutableStatisticKHR* s = calloc(count, sizeof(*s));
    if (!s) return;
    for (uint32_t i = 0; i < count; ++i) s[i].sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR;
    if (getStats(c->dev, &info, &count, s) == VK_SUCCESS) {
        printf("FSR 4 pass %u:", passIdx);
        for (uint32_t i = 0; i < count; ++i) {
            if (s[i].format == VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR)
                printf(" %s=%llu", s[i].name, (unsigned long long)s[i].value.u64);
            else if (s[i].format == VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR)
                printf(" %s=%u", s[i].name, s[i].value.b32);
        }
        printf("\n");
    }
    free(s);
}

static VkResult mkPipeline(FfxFsr4VkContext* c, uint32_t passIdx, VkPipe* out)
{
    if (!c || !out || passIdx >= FFX_FSR4_VK_PASS_COUNT)
        return VK_ERROR_INITIALIZATION_FAILED;
    const FfxFsr4VkShaderBlob* blob = &c->blobs[passIdx];
    if (!blob->spirv || blob->sizeBytes < sizeof(uint32_t) ||
        (blob->sizeBytes % sizeof(uint32_t)) != 0)
        return VK_ERROR_INVALID_SHADER_NV;

    PipeKind kind = (passIdx==0||passIdx==13||passIdx==14||passIdx==15)
                    ? PIPE_FULL : PIPE_MODEL;
    SpirvDescriptorLayout reflected;
    VkResult r = reflectShaderLayout(blob, passIdx, &reflected);
    if (r != VK_SUCCESS || !reflected.bindingCount)
        return r == VK_SUCCESS ? VK_ERROR_INVALID_SHADER_NV : r;
    // The SPIR-V ID order is not necessarily descriptor-binding order. Vulkan
    // accepts either, but sorting keeps layouts and diagnostics deterministic.
    for (uint32_t i = 0; i + 1u < reflected.bindingCount; ++i) {
        for (uint32_t j = i + 1u; j < reflected.bindingCount; ++j) {
            if (reflected.bindings[j].binding < reflected.bindings[i].binding) {
                VkDescriptorSetLayoutBinding tmp = reflected.bindings[i];
                reflected.bindings[i] = reflected.bindings[j];
                reflected.bindings[j] = tmp;
            }
        }
    }
    for (uint32_t i = 0; i < reflected.bindingCount; ++i) {
        if (reflected.bindings[i].binding == SLOT_SAMPLER)
            reflected.bindings[i].pImmutableSamplers = &c->linearSampler;
    }
    VkDescriptorSetLayoutCreateInfo dsci;
    memset(&dsci, 0, sizeof(dsci));
    dsci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsci.bindingCount = reflected.bindingCount;
    dsci.pBindings = reflected.bindings;
    memset(out, 0, sizeof(*out));
    r = vkCreateDescriptorSetLayout(c->dev, &dsci, c->alloc, &out->dsLayout);
    if (r != VK_SUCCESS)
        return r;
    out->kind = kind;
    memcpy(out->bindingPresent, reflected.bindingPresent,
           sizeof(out->bindingPresent));

    VkPipelineLayoutCreateInfo plci;
    memset(&plci, 0, sizeof(plci));
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount=1; plci.pSetLayouts=&out->dsLayout;
    /* Generated shaders consume constants exclusively through the b0 UBO at
     * binding 43.  Advertising/pushing the old 88-byte range is a second,
     * incompatible constant ABI (the current OptimizedConstants UBO is 104 B). */
    plci.pushConstantRangeCount = 0;
    plci.pPushConstantRanges = NULL;

    r = vkCreatePipelineLayout(c->dev, &plci, c->alloc, &out->layout);
    if (r) {
        vkDestroyDescriptorSetLayout(c->dev, out->dsLayout, c->alloc);
        out->dsLayout = VK_NULL_HANDLE;
        out->layout = VK_NULL_HANDLE;
        return r;
    }

    VkShaderModuleCreateInfo smci;
    memset(&smci, 0, sizeof(smci));
    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smci.codeSize=blob->sizeBytes; smci.pCode=blob->spirv;
    VkShaderModule sm;
    r = vkCreateShaderModule(c->dev, &smci, c->alloc, &sm);
    if (r) {
        vkDestroyPipelineLayout(c->dev,out->layout,c->alloc);
        vkDestroyDescriptorSetLayout(c->dev,out->dsLayout,c->alloc);
        out->dsLayout = VK_NULL_HANDLE;
        out->layout = VK_NULL_HANDLE;
        return r;
    }

    VkComputePipelineCreateInfo cpci;
    memset(&cpci, 0, sizeof(cpci));
    cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;
    cpci.stage.module=sm;
    cpci.stage.pName=blob->entryPoint ? blob->entryPoint : "main";
    cpci.layout=out->layout;

    const char* statsEnv = getenv("BB_FSR4_STATS");
    const int stats = statsEnv && statsEnv[0] == '1';
    if (stats) cpci.flags |= VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR;
    r = vkCreateComputePipelines(c->dev, VK_NULL_HANDLE, 1, &cpci, c->alloc, &out->pipeline);
    if (stats && r == VK_SUCCESS) printPipelineStats(c, passIdx, out->pipeline);
    vkDestroyShaderModule(c->dev, sm, c->alloc);
    if (r) {
        vkDestroyPipelineLayout(c->dev, out->layout, c->alloc);
        vkDestroyDescriptorSetLayout(c->dev, out->dsLayout, c->alloc);
        out->dsLayout = VK_NULL_HANDLE;
        out->layout = VK_NULL_HANDLE;
        out->pipeline = VK_NULL_HANDLE;
    }
    return r;
}

static FfxPipeline encodePipelineHandle(uint32_t passIdx)
{
    return (FfxPipeline)(uintptr_t)(passIdx + 1u);
}

static int decodePipelineHandle(FfxPipeline handle, uint32_t* passIdx)
{
    uintptr_t encoded = (uintptr_t)handle;
    if (!passIdx || encoded == 0 || encoded > FFX_FSR4_VK_PASS_COUNT)
        return 0;
    *passIdx = (uint32_t)(encoded - 1u);
    return 1;
}

static void destroyPipeline(FfxFsr4VkContext* c, VkPipe* pipeline)
{
    if (!c || !pipeline) return;
    if (pipeline->pipeline)
        vkDestroyPipeline(c->dev, pipeline->pipeline, c->alloc);
    if (pipeline->layout)
        vkDestroyPipelineLayout(c->dev, pipeline->layout, c->alloc);
    if (pipeline->dsLayout)
        vkDestroyDescriptorSetLayout(c->dev, pipeline->dsLayout, c->alloc);
    memset(pipeline, 0, sizeof(*pipeline));
}

static void destroyResource(FfxFsr4VkContext* c, VkRes* resource)
{
    if (!c || !resource) return;
    if (!resource->external) {
        if (resource->buf)
            vkDestroyBuffer(c->dev, resource->buf, c->alloc);
        if (resource->view)
            vkDestroyImageView(c->dev, resource->view, c->alloc);
        if (resource->img)
            vkDestroyImage(c->dev, resource->img, c->alloc);
        if (resource->mem)
            vkFreeMemory(c->dev, resource->mem, c->alloc);
    }
    memset(resource, 0, sizeof(*resource));
}

// ── FfxInterface callbacks ────────────────────────────────────────────────────

static FfxVersionNumber cbGetSDKVersion(FfxInterface* I) {
    (void)I;
    return FFX_SDK_MAKE_VERSION(FFX_SDK_VERSION_MAJOR,FFX_SDK_VERSION_MINOR,FFX_SDK_VERSION_PATCH);
}

static FfxErrorCode cbGetEffectGpuMemUsage(FfxInterface* I, FfxUInt32 id, FfxApiEffectMemoryUsage* o) {
    (void)id;
    if (!I || !I->device || !o)
        return FFX_ERROR_INVALID_POINTER;

    /* Report allocations owned by this effect context.  These values are the
     * Vulkan memory requirement sizes, so they include driver alignment but
     * deliberately exclude descriptor-pool implementation storage. */
    FfxFsr4VkContext* c = (FfxFsr4VkContext*)I->device;
    uint64_t total = c->cbAllocationSize;
    for (uint32_t i = 0; i < c->resCount; ++i)
        if (!c->res[i].external)
            total += c->res[i].allocationSize;
    for (uint32_t i = 0; i < c->stagingCount; ++i)
        total += c->staging[i].allocationSize;

    o->totalUsageInBytes = total;
    o->aliasableUsageInBytes = 0;
    return FFX_OK;
}

static FfxErrorCode cbCreateBackendCtx(FfxInterface* I, FfxEffect eff,
    FfxEffectBindlessConfig* bc, FfxUInt32* outId)
{
    (void)bc;
    if (!I || !I->device || !outId || eff != FFX_EFFECT_FSR4UPSCALER)
        return FFX_ERROR_INVALID_ARGUMENT;
    FfxFsr4VkContext* c = (FfxFsr4VkContext*)I->device;
    *outId = 0;
    c->jobCount = 0;

    /* Pre/model/post are the mandatory upscaling graph.  RCAS/SPD remain
     * optional until scheduled, but a missing mandatory shader must not turn
     * into a context that silently skips work. */
    for (int i = 0; i <= 13; ++i) {
        if (!c->blobs[i].spirv || !c->blobs[i].sizeBytes)
            return FFX_ERROR_INVALID_ARGUMENT;
    }

    for (int i=0; i<FFX_FSR4_VK_PASS_COUNT; i++) {
        if (!c->blobs[i].spirv) continue;
        VkResult r = mkPipeline(c, i, &c->pipe[i]);
        if (r != VK_SUCCESS) {
            for (int j = 0; j <= i; ++j)
                destroyPipeline(c, &c->pipe[j]);
            c->jobCount = 0;
            return FFX_ERROR_BACKEND_API_ERROR;
        }
    }
    return FFX_OK;
}

static FfxErrorCode cbGetDeviceCaps(FfxInterface* I, FfxDeviceCapabilities* o) {
    if (!o) return FFX_ERROR_INVALID_ARGUMENT;
    FfxFsr4VkContext* c = (FfxFsr4VkContext*)I->device;
    memset(o,0,sizeof(*o));
    o->minimumSupportedShaderModel=FFX_SHADER_MODEL_6_4;
    o->fp16Supported=c->fp16Supported;
    o->int8Supported=c->int8Supported && c->dotProductSupported;
    return FFX_OK;
}

static FfxErrorCode cbDestroyBackendCtx(FfxInterface* I, FfxUInt32 id) {
    (void)id;
    if (!I || !I->device) return FFX_ERROR_INVALID_POINTER;
    FfxFsr4VkContext* c = (FfxFsr4VkContext*)I->device;

    // Wait for all GPU work to finish before destroying anything.
    // This prevents "vkDestroyPipeline on in-use" errors when context
    // is recreated while a command buffer is still in flight.
    if (vkDeviceWaitIdle(c->dev) != VK_SUCCESS) {
        c->jobCount = 0;
        return FFX_ERROR_BACKEND_API_ERROR;
    }

    // Destroy pipelines
    for (int i=0; i<MAX_PIPELINES; i++) {
        destroyPipeline(c, &c->pipe[i]);
    }

    // Free all non-external resources (internal textures, buffers) so they
    // don't leak across context recreations.
    for (uint32_t i=0; i<c->resCount; i++) {
        destroyResource(c, &c->res[i]);
    }
    c->resCount = 0;

    /* The device is idle here, so deferred upload buffers are now safe to
     * release.  Context recreation may enqueue a fresh model upload. */
    for (uint32_t i = 0; i < c->stagingCount; ++i) {
        vkDestroyBuffer(c->dev, c->staging[i].buf, c->alloc);
        vkFreeMemory(c->dev, c->staging[i].mem, c->alloc);
    }
    c->stagingCount = 0;
    c->jobCount = 0;

    // The device is idle, so reset all explicit frame-lifetime state.
    for (int p = 0; p < NUM_POOL_FRAMES; p++) {
        if (vkResetDescriptorPool(c->dev, c->pool[p], 0) != VK_SUCCESS)
            return FFX_ERROR_BACKEND_API_ERROR;
    }
    memset(c->frame, 0, sizeof(c->frame));
    c->recordingFrame = UINT32_MAX;

    return FFX_OK;
}

static FfxErrorCode cbCreateRes(FfxInterface* I,
    const FfxCreateResourceDescription* desc, FfxUInt32 id, FfxResourceInternal* out)
{
    (void)id;
    if (!I || !I->device || !desc || !out)
        return FFX_ERROR_INVALID_POINTER;
    out->internalIndex = UINT32_MAX;
    FfxFsr4VkContext* c = (FfxFsr4VkContext*)I->device;
    if (c->resCount >= MAX_RESOURCES) return FFX_ERROR_OUT_OF_RANGE;

    uint32_t idx = c->resCount;
    VkRes* r = &c->res[idx];
    memset(r, 0, sizeof(*r));

    const int isTexture = (desc->type==FFX_RESOURCE_TYPE_TEXTURE2D ||
                           desc->type==FFX_RESOURCE_TYPE_TEXTURE1D);
    if (isTexture) {
        VkFormat fmt = toVkFmt(desc->format);
        if (!desc->width ||
            (desc->type == FFX_RESOURCE_TYPE_TEXTURE2D && !desc->height) ||
            fmt == VK_FORMAT_UNDEFINED)
            return FFX_ERROR_INVALID_ARGUMENT;
        VkImageUsageFlags usage =
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        VkResult vr = mkImg(c, desc->width, desc->height>0?desc->height:1,
                            fmt, usage, &r->img, &r->mem, &r->view,
                            &r->allocationSize);
        if (vr) return FFX_ERROR_BACKEND_API_ERROR;
        r->kind=RES_IMAGE; r->fmt=fmt; r->w=desc->width; r->h=desc->height>0?desc->height:1;
        r->needsInit=1;  /* needs UNDEFINED→GENERAL transition before first use */
    } else {
        if (desc->type != FFX_RESOURCE_TYPE_BUFFER)
            return FFX_ERROR_INVALID_ENUM;
        VkDeviceSize sz = desc->initDataSize>0
            ? (VkDeviceSize)desc->initDataSize
            : (VkDeviceSize)((desc->width?desc->width:1)*4);
        if (desc->initData && !desc->initDataSize)
            return FFX_ERROR_INVALID_SIZE;
        if (desc->initData && c->stagingCount >= MAX_STAGING)
            return FFX_ERROR_OUT_OF_RANGE;
        VkResult vr = mkBuf(c, sz,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT|
            VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &r->buf, &r->mem,
            &r->allocationSize);
        if (vr) return FFX_ERROR_BACKEND_API_ERROR;
        r->kind=RES_BUFFER; r->size=sz;

        // Upload initial data via a staging buffer kept alive until ExecuteGpuJobs
        if (desc->initData && desc->initDataSize>0) {
            Staging pending;
            memset(&pending, 0, sizeof(pending));
            vr = mkBuf(c, desc->initDataSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                &pending.buf, &pending.mem, &pending.allocationSize);
            if (vr) {
                destroyResource(c, r);
                return FFX_ERROR_BACKEND_API_ERROR;
            }
            void* p = NULL;
            vr = vkMapMemory(c->dev, pending.mem, 0, desc->initDataSize, 0, &p);
            if (vr != VK_SUCCESS || !p) {
                vkDestroyBuffer(c->dev, pending.buf, c->alloc);
                vkFreeMemory(c->dev, pending.mem, c->alloc);
                destroyResource(c, r);
                return FFX_ERROR_BACKEND_API_ERROR;
            }
            memcpy(p, desc->initData, desc->initDataSize);
            vkUnmapMemory(c->dev, pending.mem);
            pending.dstIdx=idx;
            pending.size=(VkDeviceSize)desc->initDataSize;
            c->staging[c->stagingCount++] = pending;
        }
    }
    c->resCount++;
    out->internalIndex=idx;
    return FFX_OK;
}

static FfxErrorCode cbRegisterRes(FfxInterface* I,
    const FfxApiResource* in, FfxUInt32 id, FfxResourceInternal* out)
{
    (void)id;
    if (!I || !I->device || !out) return FFX_ERROR_INVALID_POINTER;
    out->internalIndex = UINT32_MAX;
    if (!in || !in->resource) return FFX_OK; /* optional API resource */
    FfxFsr4VkContext* c = (FfxFsr4VkContext*)I->device;
    if (c->resCount >= MAX_RESOURCES) return FFX_ERROR_OUT_OF_RANGE;

    uint32_t idx = c->resCount;
    VkRes* r = &c->res[idx];
    memset(r, 0, sizeof(*r));
    r->external = 1;

    // Caller convention:
    //   Texture: resource = VkImageView,  description.type = TEXTURE2D
    //   Buffer:  resource = VkBuffer,     description.type = BUFFER
    if (in->description.type==FFX_RESOURCE_TYPE_TEXTURE2D ||
        in->description.type==FFX_RESOURCE_TYPE_TEXTURE1D) {
        VkFormat fmt = toVkFmt(in->description.format);
        if (!in->description.width ||
            (in->description.type == FFX_RESOURCE_TYPE_TEXTURE2D &&
             !in->description.height) ||
            fmt == VK_FORMAT_UNDEFINED)
            return FFX_ERROR_INVALID_ARGUMENT;
        r->kind=RES_IMAGE;
        r->view=(VkImageView)in->resource;
        const FfxFsr4VkExternalImageState* state =
            findExternalImageState(c, r->view);
        /* The FFX resource ABI exposes only a view.  Requiring the separate
         * state record prevents a reusable Vulkan host from silently relying
         * on Q2RTX's historic GENERAL-layout convention. */
        if (!state)
            return FFX_ERROR_INVALID_ARGUMENT;
        r->img = state->image;
        r->w=in->description.width;
        r->h=in->description.height>0?in->description.height:1;
        r->fmt=fmt;
        r->apiState = in->state;
        r->layout = state->layout;
        r->stageMask = state->stageMask;
        r->accessMask = state->accessMask;
        r->restoreLayout = state->restoreLayout;
        r->restoreStageMask = state->restoreStageMask;
        r->restoreAccessMask = state->restoreAccessMask;
        r->externalStateKnown = 1;
    } else if (in->description.type == FFX_RESOURCE_TYPE_BUFFER) {
        if (!in->description.size) return FFX_ERROR_INVALID_SIZE;
        r->kind=RES_BUFFER;
        r->buf=(VkBuffer)in->resource;
        r->size=in->description.width;
    } else {
        return FFX_ERROR_INVALID_ENUM;
    }
    c->resCount++;
    out->internalIndex=idx;
    return FFX_OK;
}

static FfxApiResource cbGetRes(FfxInterface* I, FfxResourceInternal ri) {
    FfxFsr4VkContext* c = (FfxFsr4VkContext*)I->device;
    FfxApiResource o={0};
    if (ri.internalIndex<c->resCount)
        o.resource = (c->res[ri.internalIndex].kind==RES_IMAGE)
            ? (void*)c->res[ri.internalIndex].view
            : (void*)c->res[ri.internalIndex].buf;
    return o;
}

static FfxErrorCode cbUnregisterRes(FfxInterface* I, FfxCommandList cmd, FfxUInt32 id) {
    (void)id;
    if (!I || !I->device) return FFX_ERROR_INVALID_POINTER;
    FfxFsr4VkContext* c = (FfxFsr4VkContext*)I->device;

    /* The provider calls unregister on the same command buffer after Execute.
     * Restore imports before dropping their tracking records, including the
     * error path after partially recorded work. */
    if (restoreExternalImages(c, (VkCommandBuffer)cmd) != FFX_OK)
        return FFX_ERROR_INVALID_POINTER;

    /* If scheduling failed before ExecuteGpuJobs, queued compute/copy jobs may
     * still name the soon-to-be-removed external indices.  Preserve only the
     * one-time internal clear jobs; successful execution has already emptied
     * the queue, so this does not alter the normal path. */
    uint32_t queued = 0;
    for (uint32_t i = 0; i < c->jobCount; ++i) {
        const FfxGpuJobDescription* job = &c->jobs[i].d;
        if (job->jobType == FFX_GPU_JOB_CLEAR_FLOAT) {
            uint32_t target = job->clearJobDescriptor.target.internalIndex;
            if (target < c->resCount && !c->res[target].external)
                c->jobs[queued++] = c->jobs[i];
        }
    }
    c->jobCount = queued;

    uint32_t n=0;
    for (uint32_t i=0; i<c->resCount; i++) {
        if (c->res[i].external) memset(&c->res[i],0,sizeof(c->res[i]));
        else { if(i!=n) c->res[n]=c->res[i]; n++; }
    }
    c->resCount=n;
    return FFX_OK;
}

static FfxErrorCode cbRegStaticRes(FfxInterface* I,
    const FfxStaticResourceDescription* d, FfxUInt32 id)
{ (void)I;(void)d;(void)id; return FFX_OK; }

static FfxApiResourceDescription cbGetResDesc(FfxInterface* I, FfxResourceInternal ri) {
    FfxFsr4VkContext* c = (FfxFsr4VkContext*)I->device;
    FfxApiResourceDescription o={0};
    if (ri.internalIndex<c->resCount) {
        VkRes* r=&c->res[ri.internalIndex];
        o.width  = (r->kind==RES_IMAGE)?r->w:(uint32_t)r->size;
        o.height = (r->kind==RES_IMAGE)?r->h:1;
        o.depth=1; o.mipCount=1;
        o.type=(r->kind==RES_IMAGE)?FFX_RESOURCE_TYPE_TEXTURE2D:FFX_RESOURCE_TYPE_BUFFER;
    }
    return o;
}

static FfxErrorCode cbDestroyRes(FfxInterface* I, FfxResourceInternal ri, FfxUInt32 id) {
    (void)id;
    if (!I || !I->device) return FFX_ERROR_INVALID_POINTER;
    FfxFsr4VkContext* c = (FfxFsr4VkContext*)I->device;
    if (ri.internalIndex>=c->resCount) return FFX_ERROR_OUT_OF_RANGE;
    VkRes* r=&c->res[ri.internalIndex];
    destroyResource(c, r);
    return FFX_OK;
}

static FfxErrorCode cbMapRes(FfxInterface* I, FfxResourceInternal ri, void** p) {
    if (!I || !I->device || !p) return FFX_ERROR_INVALID_POINTER;
    FfxFsr4VkContext* c=(FfxFsr4VkContext*)I->device;
    if (ri.internalIndex>=c->resCount ||
        c->res[ri.internalIndex].kind != RES_BUFFER ||
        !c->res[ri.internalIndex].mem)
        return FFX_ERROR_INVALID_ARGUMENT;
    return vkMapMemory(c->dev,c->res[ri.internalIndex].mem,0,VK_WHOLE_SIZE,0,p)==VK_SUCCESS
        ? FFX_OK : FFX_ERROR_BACKEND_API_ERROR;
}

static FfxErrorCode cbUnmapRes(FfxInterface* I, FfxResourceInternal ri) {
    if (!I || !I->device) return FFX_ERROR_INVALID_POINTER;
    FfxFsr4VkContext* c=(FfxFsr4VkContext*)I->device;
    if (ri.internalIndex>=c->resCount ||
        c->res[ri.internalIndex].kind != RES_BUFFER ||
        !c->res[ri.internalIndex].mem)
        return FFX_ERROR_INVALID_ARGUMENT;
    vkUnmapMemory(c->dev, c->res[ri.internalIndex].mem);
    return FFX_OK;
}

static FfxErrorCode cbStageCbuf(FfxInterface* I, void* data, FfxUInt32 sz, FfxConstantBuffer* out) {
    if (!I || !I->device || !data || !out) return FFX_ERROR_INVALID_POINTER;
    if (!sz || sz > CBUF_FRAME_BYTES) return FFX_ERROR_INVALID_SIZE;
    FfxFsr4VkContext* c = (FfxFsr4VkContext*)I->device;
    if (c->recordingFrame >= NUM_POOL_FRAMES ||
        !c->frame[c->recordingFrame].inUse)
        return FFX_ERROR_INVALID_ARGUMENT;
    FrameLifetime* frame = &c->frame[c->recordingFrame];
    uint32_t off = (frame->cbOff + 255u) & ~255u;
    if (off > CBUF_FRAME_BYTES || sz > CBUF_FRAME_BYTES - off)
        return FFX_ERROR_OUT_OF_RANGE;
    uint32_t absoluteOff = c->recordingFrame * CBUF_FRAME_BYTES + off;
    memcpy(c->cbMap + absoluteOff, data, sz);
    frame->cbOff = off + sz;
    // Encode the ring offset as the resource pointer; the descriptor carries
    // the exact staged UBO byte range.
    out->resource.resource = (void*)(uintptr_t)absoluteOff;
    out->resource.description.type = FFX_RESOURCE_TYPE_BUFFER;
    out->resource.description.size = sz;
    return FFX_OK;
}

static FfxErrorCode cbCreatePipeline(FfxInterface* I, FfxShaderBlob* blob,
    const FfxPipelineDescription* pd, FfxUInt32 id, FfxPipelineState* out)
{
    (void)id;
    if (!I || !I->device || !pd || !out)
        return FFX_ERROR_INVALID_POINTER;
    FfxFsr4VkContext* c = (FfxFsr4VkContext*)I->device;
    memset(out, 0, sizeof(*out));

    uint32_t passIdx = UINT32_MAX;
    const wchar_t* n = pd->name;
    const wchar_t* e = n;
    while (*e) e++;
    while (e>n && e[-1]>=L'0' && e[-1]<=L'9') e--;
    if (*e >= L'0' && *e <= L'9') passIdx = (uint32_t)wcstoul(e, NULL, 10);
    if (wcsstr(n, L"PRE")) passIdx=0;
    else if (wcsstr(n, L"POST")) passIdx=13;
    else if (wcsstr(n, L"RCAS")) passIdx=14;
    else if (wcsstr(n, L"SPD")) passIdx=15;
    else if (passIdx == UINT32_MAX && pd->passIndex < FFX_FSR4_VK_PASS_COUNT)
        passIdx = pd->passIndex;
    if (passIdx >= FFX_FSR4_VK_PASS_COUNT)
        return FFX_ERROR_INVALID_ARGUMENT;
    if (!c->blobs[passIdx].spirv || !c->pipe[passIdx].pipeline)
        return FFX_ERROR_BACKEND_API_ERROR;

    out->pipeline = encodePipelineHandle(passIdx);

    if (blob) {
        out->srvTextureCount = blob->srvTextureCount;
        out->uavTextureCount = blob->uavTextureCount;
        out->srvBufferCount  = blob->srvBufferCount;
        out->uavBufferCount  = blob->uavBufferCount;
        out->constCount      = blob->cbvCount;
    } else {
        out->srvBufferCount=2; out->uavBufferCount=2;
    }
    return FFX_OK;
}

static FfxErrorCode cbDestroyPipeline(FfxInterface* I, FfxPipelineState* ps, FfxUInt32 id) {
    (void)I;(void)id; if(ps) ps->pipeline=NULL; return FFX_OK;
}

static FfxErrorCode cbScheduleJob(FfxInterface* I, const FfxGpuJobDescription* j) {
    if (!I || !I->device || !j) return FFX_ERROR_INVALID_POINTER;
    FfxFsr4VkContext* c = (FfxFsr4VkContext*)I->device;
    switch (j->jobType) {
    case FFX_GPU_JOB_CLEAR_FLOAT: {
        uint32_t idx = j->clearJobDescriptor.target.internalIndex;
        if (idx >= c->resCount || c->res[idx].kind == RES_NONE)
            return FFX_ERROR_INVALID_ARGUMENT;
        break;
    }
    case FFX_GPU_JOB_COPY: {
        uint32_t src = j->copyJobDescriptor.src.internalIndex;
        uint32_t dst = j->copyJobDescriptor.dst.internalIndex;
        if (src >= c->resCount || dst >= c->resCount ||
            c->res[src].kind != RES_BUFFER || c->res[dst].kind != RES_BUFFER ||
            !j->copyJobDescriptor.size ||
            j->copyJobDescriptor.srcOffset > c->res[src].size ||
            j->copyJobDescriptor.size >
                c->res[src].size - j->copyJobDescriptor.srcOffset ||
            j->copyJobDescriptor.dstOffset > c->res[dst].size ||
            j->copyJobDescriptor.size >
                c->res[dst].size - j->copyJobDescriptor.dstOffset)
            return FFX_ERROR_INVALID_ARGUMENT;
        break;
    }
    case FFX_GPU_JOB_COMPUTE: {
        const FfxComputeJobDescription* compute = &j->computeJobDescriptor;
        uint32_t passIdx;
        if (!decodePipelineHandle(compute->pipeline.pipeline, &passIdx) ||
            !c->blobs[passIdx].spirv || !c->pipe[passIdx].pipeline ||
            !compute->dimensions[0] || !compute->dimensions[1] ||
            !compute->dimensions[2])
            return FFX_ERROR_INVALID_ARGUMENT;
        break;
    }
    default:
        return FFX_ERROR_INVALID_ENUM;
    }
    if (c->jobCount >= MAX_PENDING_JOBS) return FFX_ERROR_OUT_OF_RANGE;
    c->jobs[c->jobCount++].d = *j;
    return FFX_OK;
}

// ── image layout barrier ──────────────────────────────────────────────────────

static void imgBarrier(VkCommandBuffer cmd, VkImage img,
    VkImageLayout from, VkImageLayout to,
    VkAccessFlags sa, VkAccessFlags da,
    VkPipelineStageFlags ss, VkPipelineStageFlags ds)
{
    VkImageMemoryBarrier b;
    memset(&b, 0, sizeof(b));
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.srcAccessMask=sa; b.dstAccessMask=da;
    b.oldLayout=from; b.newLayout=to;
    b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    b.image=img;
    b.subresourceRange=(VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    vkCmdPipelineBarrier(cmd,ss,ds,0,0,NULL,0,NULL,1,&b);
}

static void transitionExternalImagesToCompute(FfxFsr4VkContext* c,
                                              VkCommandBuffer cmd)
{
    for (uint32_t i = 0; i < c->resCount; ++i) {
        VkRes* r = &c->res[i];
        if (!r->external || r->kind != RES_IMAGE || !r->img ||
            !r->externalStateKnown)
            continue;
        VkPipelineStageFlags srcStage = r->layout == VK_IMAGE_LAYOUT_UNDEFINED
            ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : r->stageMask;
        VkAccessFlags srcAccess = r->layout == VK_IMAGE_LAYOUT_UNDEFINED
            ? 0u : r->accessMask;
        imgBarrier(cmd, r->img, r->layout, VK_IMAGE_LAYOUT_GENERAL,
            srcAccess, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            srcStage, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        r->layout = VK_IMAGE_LAYOUT_GENERAL;
        r->stageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        r->accessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        r->externalTouched = 1;
    }
}

static FfxErrorCode restoreExternalImages(FfxFsr4VkContext* c,
                                          VkCommandBuffer cmd)
{
    if (!cmd) return FFX_ERROR_INVALID_POINTER;
    for (uint32_t i = 0; i < c->resCount; ++i) {
        VkRes* r = &c->res[i];
        if (!r->external || r->kind != RES_IMAGE || !r->img ||
            !r->externalStateKnown || !r->externalTouched)
            continue;
        imgBarrier(cmd, r->img, r->layout, r->restoreLayout,
            r->accessMask, r->restoreAccessMask,
            r->stageMask, r->restoreStageMask);
        r->layout = r->restoreLayout;
        r->stageMask = r->restoreStageMask;
        r->accessMask = r->restoreAccessMask;
        r->externalTouched = 0;
    }
    return FFX_OK;
}

// ── descriptor writing helpers ────────────────────────────────────────────────

static FfxErrorCode writeModelDs(FfxFsr4VkContext* c, const VkPipe* pipe,
    VkDescriptorSet ds, const FfxComputeJobDescription* cj)
{
    VkWriteDescriptorSet wr[4];
    VkDescriptorBufferInfo bi[4];
    memset(wr,0,sizeof(wr)); memset(bi,0,sizeof(bi));
    uint32_t wc=0;
    uint8_t written[4] = {0};

    if (!c || !pipe || !ds || !cj || cj->pipeline.srvBufferCount != 2 ||
        cj->pipeline.uavBufferCount != 2)
        return FFX_ERROR_INVALID_ARGUMENT;

    const FfxResourceInternal resources[4] = {
        cj->srvBuffers[0].resource, cj->srvBuffers[1].resource,
        cj->uavBuffers[0].resource, cj->uavBuffers[1].resource};
    for (uint32_t binding = 0; binding < 4; ++binding) {
        if (!pipe->bindingPresent[binding]) continue;
        uint32_t idx = resources[binding].internalIndex;
        if (idx < c->resCount && c->res[idx].kind == RES_BUFFER && c->res[idx].buf) {
            bi[wc]=(VkDescriptorBufferInfo){c->res[idx].buf,0,VK_WHOLE_SIZE};
            wr[wc]=(VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                NULL,ds,binding,0,1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,NULL,&bi[wc],NULL};
            written[binding] = 1u;
            wc++;
        } else return FFX_ERROR_INVALID_ARGUMENT;
    }
    for (uint32_t binding = 0; binding < 4; ++binding)
        if (pipe->bindingPresent[binding] && !written[binding])
            return FFX_ERROR_INVALID_ARGUMENT;
    if (!wc) return FFX_ERROR_INVALID_ARGUMENT;
    vkUpdateDescriptorSets(c->dev,wc,wr,0,NULL);
    return FFX_OK;
}

static FfxErrorCode writeFullDs(FfxFsr4VkContext* c, const VkPipe* pipe, VkDescriptorSet ds,
    const FfxComputeJobDescription* cj)
{
    // Upper bound on writes: 44 bindings + spare
    enum { MAX_WR=50 };
    VkWriteDescriptorSet wr[MAX_WR];
    VkDescriptorImageInfo  imgs[21+13];   // SRV textures + UAV textures
    VkDescriptorBufferInfo bufs[8];       // UAV bufs + scratch + cbuffers
    memset(wr,0,sizeof(wr)); memset(imgs,0,sizeof(imgs)); memset(bufs,0,sizeof(bufs));
    uint32_t wc=0, ic=0, bc=0;
    uint8_t written[FULL_LAYOUT_COUNT] = {0};

    if (!c || !pipe || !ds || !cj || cj->pipeline.srvTextureCount > 21 ||
        cj->pipeline.uavTextureCount > 13 ||
        cj->pipeline.uavBufferCount > 1 ||
        cj->pipeline.srvBufferCount != 0 ||
        cj->pipeline.constCount > 2)
        return FFX_ERROR_OUT_OF_RANGE;

    // ── SRV textures t0..t20 → bindings 0..20 (SAMPLED_IMAGE) ──
    // Use VK_IMAGE_LAYOUT_GENERAL for all images.  Internal images (history,
    // recurrent, reprojected, exposure) are also used as UAV STORAGE_IMAGE in
    // other passes, so they must stay in GENERAL. External images enter
    // GENERAL through the public state-registration contract before dispatch.
    for (uint32_t i=0; i<cj->pipeline.srvTextureCount && i<21; i++) {
        uint32_t idx=cj->srvTextures[i].resource.internalIndex;
        if (idx == UINT32_MAX) continue;
        if (!pipe->bindingPresent[SLOT_SRV_TEX_BASE + i]) continue;
        if (idx<c->resCount && c->res[idx].kind==RES_IMAGE && c->res[idx].view) {
            if (c->res[idx].external &&
                !(c->res[idx].apiState & FFX_API_RESOURCE_STATE_COMPUTE_READ))
                return FFX_ERROR_INVALID_ARGUMENT;
            imgs[ic]=(VkDescriptorImageInfo){VK_NULL_HANDLE, c->res[idx].view,
                VK_IMAGE_LAYOUT_GENERAL};
            wr[wc]=(VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                NULL,ds,SLOT_SRV_TEX_BASE+i,0,1,
                VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,&imgs[ic],NULL,NULL};
            written[SLOT_SRV_TEX_BASE + i] = 1u; ic++; wc++;
        } else return FFX_ERROR_INVALID_ARGUMENT;
    }

    // ── UAV textures u0..u12 → bindings 21..33 (STORAGE_IMAGE) ──
    for (uint32_t i=0; i<cj->pipeline.uavTextureCount && i<13; i++) {
        uint32_t idx=cj->uavTextures[i].resource.internalIndex;
        if (idx == UINT32_MAX) continue;
        if (!pipe->bindingPresent[SLOT_UAV_BASE + i]) continue;
        if (idx<c->resCount && c->res[idx].kind==RES_IMAGE && c->res[idx].view) {
            if (c->res[idx].external &&
                !(c->res[idx].apiState & FFX_API_RESOURCE_STATE_UNORDERED_ACCESS))
                return FFX_ERROR_INVALID_ARGUMENT;
            imgs[ic]=(VkDescriptorImageInfo){VK_NULL_HANDLE, c->res[idx].view,
                VK_IMAGE_LAYOUT_GENERAL};
            wr[wc]=(VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                NULL,ds,SLOT_UAV_BASE+i,0,1,
                VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,&imgs[ic],NULL,NULL};
            written[SLOT_UAV_BASE + i] = 1u; ic++; wc++;
        } else return FFX_ERROR_INVALID_ARGUMENT;
    }

    // ── UAV buffers → STORAGE_BUFFER at u-register + SLOT_UAV_BASE ──
    // Use uavBufferBindings[i].bindingIndex (the u-register number) to compute
    // the Vulkan binding.  If bindingIndex is 0 and i>0, treat as unset and skip.
    for (uint32_t i=0; i<cj->pipeline.uavBufferCount; i++) {
        uint32_t idx=cj->uavBuffers[i].resource.internalIndex;
        if (idx>=c->resCount || c->res[idx].kind!=RES_BUFFER || !c->res[idx].buf)
            return FFX_ERROR_INVALID_ARGUMENT;
        uint32_t u_reg = cj->pipeline.uavBufferBindings[i].bindingIndex;
        if (u_reg != 11) return FFX_ERROR_INVALID_ARGUMENT;
        uint32_t binding = SLOT_UAV_BASE + u_reg;
        if (!pipe->bindingPresent[binding]) continue;
        bufs[bc]=(VkDescriptorBufferInfo){c->res[idx].buf,0,VK_WHOLE_SIZE};
        wr[wc]=(VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            NULL,ds,binding,0,1,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,NULL,&bufs[bc],NULL};
        written[binding] = 1u; bc++; wc++;
    }

    // ── Main constants (b0) → binding 43 (UNIFORM_BUFFER) ──
    if (cj->pipeline.constCount>0 && pipe->bindingPresent[SLOT_CBUF_MAIN]) {
        uint32_t off=(uint32_t)(uintptr_t)cj->cbs[0].resource.resource;
        VkDeviceSize range = cj->cbs[0].resource.description.size;
        if (!range || off > CBUF_RING_BYTES || range > CBUF_RING_BYTES - off)
            return FFX_ERROR_INVALID_SIZE;
        bufs[bc]=(VkDescriptorBufferInfo){c->cbRing, off, range};
        wr[wc]=(VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            NULL,ds,SLOT_CBUF_MAIN,0,1,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,NULL,&bufs[bc],NULL};
        written[SLOT_CBUF_MAIN] = 1u; bc++; wc++;
    }

    // ── Pass weights (cbPass_Weights) → binding 34 (UNIFORM_BUFFER) ──
    if (cj->pipeline.constCount>1 && pipe->bindingPresent[SLOT_CBUF_WEIGHTS]) {
        uint32_t off=(uint32_t)(uintptr_t)cj->cbs[1].resource.resource;
        VkDeviceSize range = cj->cbs[1].resource.description.size;
        if (!range || off > CBUF_RING_BYTES || range > CBUF_RING_BYTES - off)
            return FFX_ERROR_INVALID_SIZE;
        bufs[bc]=(VkDescriptorBufferInfo){c->cbRing, off, range};
        wr[wc]=(VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            NULL,ds,SLOT_CBUF_WEIGHTS,0,1,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,NULL,&bufs[bc],NULL};
        written[SLOT_CBUF_WEIGHTS] = 1u; bc++; wc++;
    }

    for (uint32_t binding = 0; binding < FULL_LAYOUT_COUNT; ++binding) {
        if (pipe->bindingPresent[binding] && binding != SLOT_SAMPLER &&
            !written[binding])
            return FFX_ERROR_INVALID_ARGUMENT;
    }
    if (!wc) return FFX_ERROR_INVALID_ARGUMENT;
    vkUpdateDescriptorSets(c->dev, wc, wr, 0, NULL);
    return FFX_OK;
}

// ── execute all queued jobs ───────────────────────────────────────────────────

static void profCollect(FfxFsr4VkContext* c, uint32_t slot)
{
    const uint32_t count = c->profCount[slot];
    if (!c->profPool || count == 0) return;
    c->profCount[slot] = 0;
    uint64_t ts[PROF_MAX_JOBS + 1];
    if (vkGetQueryPoolResults(c->dev, c->profPool, slot * (PROF_MAX_JOBS + 1), count + 1,
                              sizeof(ts), ts, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS)
        return;
    for (uint32_t j = 0; j < count; ++j) {
        const uint32_t pass = c->profPass[slot][j];
        c->profMs[pass] += (double)(ts[j + 1] - ts[j]) * c->profPeriodNs * 1e-6;
        c->profCalls[pass]++;
    }
    if (++c->profFrames % 300 != 0) return;
    static const char* names[] = {"pre", "model 1", "model 2", "model 3", "model 4",
        "model 5", "model 6", "model 7", "model 8", "model 9", "model 10", "model 11",
        "model 12", "post", "rcas", "spd auto exposure"};
    double total = 0.0;
    for (uint32_t p = 0; p < MAX_PIPELINES; ++p) total += c->profMs[p];
    printf("FSR 4 profile: %.3f ms/frame over %llu frames\n", total / 300.0,
           (unsigned long long)300);
    for (uint32_t p = 0; p < MAX_PIPELINES; ++p) {
        if (!c->profCalls[p]) continue;
        printf("  %6.3f ms/frame  %s\n", c->profMs[p] / 300.0,
               p < sizeof(names) / sizeof(names[0]) ? names[p] : "?");
        c->profMs[p] = 0.0;
        c->profCalls[p] = 0;
    }
    fflush(stdout);
}

static FfxErrorCode cbExecuteJobs(FfxInterface* I, FfxCommandList cl, FfxUInt32 id) {
    (void)id;
    if (!I || !I->device) return FFX_ERROR_INVALID_POINTER;
    FfxFsr4VkContext* c = (FfxFsr4VkContext*)I->device;
    if (!cl) {
        c->jobCount = 0;
        return FFX_ERROR_INVALID_POINTER;
    }
    VkCommandBuffer cmd = (VkCommandBuffer)cl;

#define EXECUTE_FAIL(error_code) do { \
        c->jobCount = 0; c->recordingFrame = UINT32_MAX; return (error_code); \
    } while (0)

    if (c->recordingFrame >= NUM_POOL_FRAMES ||
        !c->frame[c->recordingFrame].inUse)
        EXECUTE_FAIL(FFX_ERROR_INVALID_ARGUMENT);
    const uint32_t cur = c->recordingFrame;
    VkDescriptorPool activePool = c->pool[cur];

    // Flush staging uploads first (recorded before any compute work).  A
    // staging allocation must remain alive until the submitted command buffer
    // has completed.  Keep it until backend destruction; model uploads are
    // one-shot and tiny compared with the activation scratch allocation.
    uint32_t pendingUploads = 0;
    for (uint32_t i=0; i<c->stagingCount; i++) {
        Staging* st=&c->staging[i];
        if (st->submitted) continue;
        if (st->dstIdx >= c->resCount ||
            c->res[st->dstIdx].kind != RES_BUFFER || !c->res[st->dstIdx].buf)
            EXECUTE_FAIL(FFX_ERROR_INVALID_ARGUMENT);
        VkBufferCopy bc={0,0,st->size};
        vkCmdCopyBuffer(cmd, st->buf, c->res[st->dstIdx].buf, 1, &bc);
        st->submitted = 1;
        st->frameId = c->frame[cur].frameId;
        pendingUploads++;
    }
    if (pendingUploads>0) {
        VkMemoryBarrier mb;
        memset(&mb, 0, sizeof(mb));
        mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        mb.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,1,&mb,0,NULL,0,NULL);
    }

    // Transition any internal images that haven't been initialised yet
    // from UNDEFINED to GENERAL (needed on first frame).
    {
        int init_count = 0;
        for (uint32_t i = 0; i < c->resCount; i++) {
            if (c->res[i].kind == RES_IMAGE && c->res[i].needsInit && c->res[i].img) {
                imgBarrier(cmd, c->res[i].img,
                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                    0, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
                c->res[i].needsInit = 0;
                init_count++;
            }
        }
        (void)init_count;
    }

    /* Imported resources carry their exact host layout/stage/access metadata.
     * Move them to the provider's GENERAL compute ABI only after all resource
     * registration has succeeded, then restore them during unregister. */
    transitionExternalImagesToCompute(c, cmd);
    const uint32_t profBase = cur * (PROF_MAX_JOBS + 1);
    uint32_t profJobs = 0;
    if (c->profPool) {
        vkCmdResetQueryPool(cmd, c->profPool, profBase, PROF_MAX_JOBS + 1);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, c->profPool, profBase);
    }

    for (uint32_t ji=0; ji<c->jobCount; ji++) {
        FfxGpuJobDescription* job=&c->jobs[ji].d;

        if (job->jobType==FFX_GPU_JOB_CLEAR_FLOAT) {
            uint32_t idx=job->clearJobDescriptor.target.internalIndex;
            if (idx>=c->resCount || c->res[idx].kind == RES_NONE)
                EXECUTE_FAIL(FFX_ERROR_INVALID_ARGUMENT);
            {
                VkRes* r=&c->res[idx];
                if (r->kind==RES_BUFFER) {
                    if (!r->buf) EXECUTE_FAIL(FFX_ERROR_INVALID_ARGUMENT);
                    vkCmdFillBuffer(cmd,r->buf,0,VK_WHOLE_SIZE,0);
                } else if (r->kind==RES_IMAGE && r->img) {
                    imgBarrier(cmd,r->img,
                        VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,
                        VK_ACCESS_TRANSFER_WRITE_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
                    /* FFX clear jobs carry a real float clear value.  In
                     * particular, the explicit exposure fallback must start
                     * at 1.0; silently replacing every clear with zero makes
                     * the first temporal frame depend on undefined shader
                     * behaviour. */
                    VkClearColorValue cv;
                    memcpy(cv.float32, job->clearJobDescriptor.color,
                           sizeof(cv.float32));
                    VkImageSubresourceRange sr={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
                    vkCmdClearColorImage(cmd,r->img,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&cv,1,&sr);
                    imgBarrier(cmd,r->img,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,
                        VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
                } else EXECUTE_FAIL(FFX_ERROR_INVALID_ARGUMENT);
            }
        }
        else if (job->jobType==FFX_GPU_JOB_COPY) {
            uint32_t si=job->copyJobDescriptor.src.internalIndex;
            uint32_t di=job->copyJobDescriptor.dst.internalIndex;
            if (si<c->resCount && di<c->resCount &&
                c->res[si].kind==RES_BUFFER && c->res[di].kind==RES_BUFFER) {
                VkBufferCopy bc={
                    job->copyJobDescriptor.srcOffset,
                    job->copyJobDescriptor.dstOffset,
                    job->copyJobDescriptor.size};
                vkCmdCopyBuffer(cmd,c->res[si].buf,c->res[di].buf,1,&bc);
            } else EXECUTE_FAIL(FFX_ERROR_INVALID_ARGUMENT);
        }
        else if (job->jobType==FFX_GPU_JOB_COMPUTE) {
            FfxComputeJobDescription* cj=&job->computeJobDescriptor;
            uint32_t passIdx;
            if (!decodePipelineHandle(cj->pipeline.pipeline, &passIdx))
                EXECUTE_FAIL(FFX_ERROR_INVALID_ARGUMENT);
            VkPipe* bp=&c->pipe[passIdx];
            if (!bp->pipeline || !bp->layout || !bp->dsLayout)
                EXECUTE_FAIL(FFX_ERROR_BACKEND_API_ERROR);

            // Allocate descriptor set from this frame's pool
            VkDescriptorSetAllocateInfo dsai;
            memset(&dsai, 0, sizeof(dsai));
            dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            dsai.descriptorPool=activePool;
            dsai.descriptorSetCount=1;
            dsai.pSetLayouts=&bp->dsLayout;
            VkDescriptorSet ds;
            if (vkAllocateDescriptorSets(c->dev,&dsai,&ds)!=VK_SUCCESS)
                EXECUTE_FAIL(FFX_ERROR_BACKEND_API_ERROR);

            FfxErrorCode writeResult = (bp->kind==PIPE_FULL)
                ? writeFullDs(c,bp,ds,cj)
                : writeModelDs(c,bp,ds,cj);
            if (writeResult != FFX_OK)
                EXECUTE_FAIL(writeResult);

            vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,bp->pipeline);
            vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,
                bp->layout,0,1,&ds,0,NULL);

            vkCmdDispatch(cmd,cj->dimensions[0],cj->dimensions[1],cj->dimensions[2]);
            if (c->profPool && profJobs < PROF_MAX_JOBS) {
                vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, c->profPool,
                                    profBase + 1 + profJobs);
                c->profPass[cur][profJobs++] = passIdx;
            }

            // Memory barrier between compute passes — ensures writes from this
            // dispatch are visible to the next dispatch (pre→model→post chain).
            {
                VkMemoryBarrier mb;
                memset(&mb, 0, sizeof(mb));
                mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0, 1, &mb, 0, NULL, 0, NULL);
            }

            // Inter-pass memory barrier
            VkMemoryBarrier mb;
    memset(&mb, 0, sizeof(mb));
    mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            mb.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
            mb.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0,1,&mb,0,NULL,0,NULL);
        } else EXECUTE_FAIL(FFX_ERROR_INVALID_ENUM);
    }

    c->profCount[cur] = profJobs;
    c->jobCount=0;
    c->recordingFrame = UINT32_MAX;
#undef EXECUTE_FAIL
    return FFX_OK;
}

static FfxErrorCode cbSwapChain(FfxFrameGenerationConfig const* cfg){ (void)cfg; return FFX_OK; }
static void cbRegCbAlloc(FfxInterface* I, FfxConstantBufferAllocator a){ (void)I;(void)a; }

// ── public API ────────────────────────────────────────────────────────────────

size_t ffxFsr4VkGetScratchMemorySize(void) { return sizeof(FfxFsr4VkContext); }

VkResult ffxFsr4VkCreateContext(const FfxFsr4VkCreateInfo* ci, FfxInterface* out)
{
    if (!ci||!out||!ci->scratchBuffer) return VK_ERROR_INITIALIZATION_FAILED;
    memset(out, 0, sizeof(*out));
    if (ci->scratchBufferSize < sizeof(FfxFsr4VkContext)) return VK_ERROR_INITIALIZATION_FAILED;
    if (!ci->device || !ci->physicalDevice) return VK_ERROR_INITIALIZATION_FAILED;

    FfxFsr4VkContext* c = (FfxFsr4VkContext*)ci->scratchBuffer;
    memset(c, 0, sizeof(*c));
    c->dev=ci->device; c->phys=ci->physicalDevice; c->alloc=ci->allocator;
    VkResult featureResult = queryRequiredFeatures(c);
    if (featureResult != VK_SUCCESS) return featureResult;
    VkResult formatResult = queryRequiredFormats(c);
    if (formatResult != VK_SUCCESS) return formatResult;
    /* Deep-copy the SPIR-V blobs so this context owns the data.
     * The caller may free its copy immediately after this call returns. */
    VkResult r = VK_SUCCESS;
    for (int i = 0; i < FFX_FSR4_VK_PASS_COUNT; i++) {
        const FfxFsr4VkShaderBlob* source = &ci->shaders[i];
        if (!source->spirv && !source->sizeBytes) continue;
        if (!source->spirv || !source->sizeBytes ||
            (source->sizeBytes % sizeof(uint32_t)) != 0) {
            r = VK_ERROR_INVALID_SHADER_NV;
            goto fail;
        }
        uint32_t* copy = (uint32_t*)malloc(source->sizeBytes);
        if (!copy) { r = VK_ERROR_OUT_OF_HOST_MEMORY; goto fail; }
        memcpy(copy, source->spirv, source->sizeBytes);
        c->blobs[i] = *source;
        c->blobs[i].spirv = copy;
        r = ffxFsr4VkValidateShaderLayout(&c->blobs[i], (uint32_t)i);
        if (r != VK_SUCCESS)
            goto fail;
    }

    if ((!ci->modelInitializer) != (!ci->modelInitializerSize) ||
        (!ci->prePassWeights) != (!ci->prePassWeightsSize)) {
        r = VK_ERROR_INITIALIZATION_FAILED;
        goto fail;
    }

    if (ci->modelInitializer && ci->modelInitializerSize) {
        c->modelInitializer = malloc(ci->modelInitializerSize);
        if (!c->modelInitializer) {
            r = VK_ERROR_OUT_OF_HOST_MEMORY;
            goto fail;
        }
        memcpy(c->modelInitializer, ci->modelInitializer,
               ci->modelInitializerSize);
        c->modelInitializerSize = ci->modelInitializerSize;
    }
    if (ci->prePassWeights && ci->prePassWeightsSize) {
        c->prePassWeights = malloc(ci->prePassWeightsSize);
        if (!c->prePassWeights) {
            r = VK_ERROR_OUT_OF_HOST_MEMORY;
            goto fail;
        }
        memcpy(c->prePassWeights, ci->prePassWeights,
               ci->prePassWeightsSize);
        c->prePassWeightsSize = ci->prePassWeightsSize;
    }

    // Sampler (used as an immutable sampler by reflected full-pass layouts)
    VkSamplerCreateInfo sci;
    memset(&sci, 0, sizeof(sci));
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter=VK_FILTER_LINEAR; sci.minFilter=VK_FILTER_LINEAR;
    sci.mipmapMode=VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sci.addressModeU=sci.addressModeV=sci.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.maxLod=VK_LOD_CLAMP_NONE;
    r=vkCreateSampler(c->dev,&sci,c->alloc,&c->linearSampler);
    {
        const char* prof = getenv("BB_FSR4_PROFILE");
        if (r == VK_SUCCESS && prof && prof[0] == '1') {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(c->phys, &props);
            c->profPeriodNs = props.limits.timestampPeriod;
            VkQueryPoolCreateInfo qci;
            memset(&qci, 0, sizeof(qci));
            qci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            qci.queryType = VK_QUERY_TYPE_TIMESTAMP;
            qci.queryCount = NUM_POOL_FRAMES * (PROF_MAX_JOBS + 1);
            if (vkCreateQueryPool(c->dev, &qci, c->alloc, &c->profPool) != VK_SUCCESS)
                c->profPool = VK_NULL_HANDLE;
        }
    }
    if (r) goto fail;

    // cbuffer ring buffer (host-visible, persistently mapped)
    r=mkBuf(c, CBUF_RING_BYTES,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &c->cbRing, &c->cbMem, &c->cbAllocationSize);
    if (r) goto fail;
    r = vkMapMemory(c->dev, c->cbMem, 0, CBUF_RING_BYTES, 0,
                    (void**)&c->cbMap);
    if (r) goto fail;

    // Three explicit frame-lifetime pools.  The host selects and retires them
    // with ffxFsr4VkBeginFrame/RetireFrame after its own fence boundaries.
    VkDescriptorPoolSize ps[] = {
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         MAX_DESC_SETS * 16},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         MAX_DESC_SETS},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          MAX_DESC_SETS * 21},
        {VK_DESCRIPTOR_TYPE_SAMPLER,                MAX_DESC_SETS},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          MAX_DESC_SETS * 13},
    };
    VkDescriptorPoolCreateInfo dpci;
    memset(&dpci, 0, sizeof(dpci));
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.flags=VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dpci.maxSets=MAX_DESC_SETS;
    dpci.poolSizeCount=5; dpci.pPoolSizes=ps;
    for (int p = 0; p < NUM_POOL_FRAMES; p++) {
        r=vkCreateDescriptorPool(c->dev, &dpci, c->alloc, &c->pool[p]);
        if (r) goto fail;
    }
    c->recordingFrame = UINT32_MAX;

    // Populate FfxInterface table
    out->fpGetSDKVersion                     = cbGetSDKVersion;
    out->fpGetEffectGpuMemoryUsage           = cbGetEffectGpuMemUsage;
    out->fpCreateBackendContext              = cbCreateBackendCtx;
    out->fpGetDeviceCapabilities             = cbGetDeviceCaps;
    out->fpDestroyBackendContext             = cbDestroyBackendCtx;
    out->fpCreateResource                    = cbCreateRes;
    out->fpRegisterResource                  = cbRegisterRes;
    out->fpGetResource                       = cbGetRes;
    out->fpUnregisterResources               = cbUnregisterRes;
    out->fpRegisterStaticResource            = cbRegStaticRes;
    out->fpGetResourceDescription            = cbGetResDesc;
    out->fpDestroyResource                   = cbDestroyRes;
    out->fpMapResource                       = cbMapRes;
    out->fpUnmapResource                     = cbUnmapRes;
    out->fpStageConstantBufferDataFunc       = cbStageCbuf;
    out->fpCreatePipeline                    = cbCreatePipeline;
    out->fpDestroyPipeline                   = cbDestroyPipeline;
    out->fpScheduleGpuJob                    = cbScheduleJob;
    out->fpExecuteGpuJobs                    = cbExecuteJobs;
    out->fpSwapChainConfigureFrameGeneration = cbSwapChain;
    out->fpRegisterConstantBufferAllocator   = cbRegCbAlloc;

    out->scratchBuffer=ci->scratchBuffer;
    out->scratchBufferSize=ci->scratchBufferSize;
    out->device=(FfxDevice)c;
    return VK_SUCCESS;

fail:
    ffxFsr4VkDestroyContext(c);
    memset(out, 0, sizeof(*out));
    return r;
}

void ffxFsr4VkDestroyContext(FfxFsr4VkContext* c)
{
    if (!c) return;
    c->jobCount = 0;
    for (int i = 0; i < MAX_PIPELINES; ++i)
        destroyPipeline(c, &c->pipe[i]);
    /* Free the deep-copied SPIR-V blobs allocated in ffxFsr4VkCreateContext. */
    for (int i = 0; i < FFX_FSR4_VK_PASS_COUNT; i++) {
        free((void*)c->blobs[i].spirv);
        c->blobs[i].spirv = NULL;
    }
    free(c->modelInitializer);
    c->modelInitializer = NULL;
    c->modelInitializerSize = 0;
    free(c->prePassWeights);
    c->prePassWeights = NULL;
    c->prePassWeightsSize = 0;
    for (uint32_t i=0; i<c->resCount; i++)
        destroyResource(c, &c->res[i]);
    c->resCount = 0;
    for (uint32_t i=0; i<c->stagingCount; i++) {
        if (c->staging[i].buf)
            vkDestroyBuffer(c->dev,c->staging[i].buf,c->alloc);
        if (c->staging[i].mem)
            vkFreeMemory(c->dev,c->staging[i].mem,c->alloc);
        memset(&c->staging[i], 0, sizeof(c->staging[i]));
    }
    c->stagingCount = 0;
    if (c->cbMap) {
        vkUnmapMemory(c->dev,c->cbMem);
        c->cbMap = NULL;
    }
    if (c->cbRing) {
        vkDestroyBuffer(c->dev,c->cbRing,c->alloc);
        c->cbRing = VK_NULL_HANDLE;
    }
    if (c->cbMem) {
        vkFreeMemory(c->dev,c->cbMem,c->alloc);
        c->cbMem = VK_NULL_HANDLE;
    }
    for (int p = 0; p < NUM_POOL_FRAMES; p++) {
        if (c->pool[p]) {
            vkDestroyDescriptorPool(c->dev,c->pool[p],c->alloc);
            c->pool[p] = VK_NULL_HANDLE;
        }
    }
    memset(c->frame, 0, sizeof(c->frame));
    c->recordingFrame = UINT32_MAX;
    if (c->profPool) {
        vkDestroyQueryPool(c->dev, c->profPool, c->alloc);
        c->profPool = VK_NULL_HANDLE;
    }
    if (c->linearSampler) {
        vkDestroySampler(c->dev,c->linearSampler,c->alloc);
        c->linearSampler = VK_NULL_HANDLE;
    }
}

VkResult ffxFsr4VkBeginFrame(FfxInterface* iface, uint64_t frameId)
{
    if (!iface || !iface->device) return VK_ERROR_INITIALIZATION_FAILED;
    FfxFsr4VkContext* c = (FfxFsr4VkContext*)iface->device;
    if (c->recordingFrame != UINT32_MAX) return VK_ERROR_VALIDATION_FAILED_EXT;

    for (uint32_t i = 0; i < NUM_POOL_FRAMES; ++i) {
        FrameLifetime* frame = &c->frame[i];
        if (frame->inUse) continue;
        if (!c->pool[i] ||
            vkResetDescriptorPool(c->dev, c->pool[i], 0) != VK_SUCCESS)
            return VK_ERROR_DEVICE_LOST;
        frame->frameId = frameId;
        frame->cbOff = 0;
        frame->inUse = VK_TRUE;
        c->recordingFrame = i;
        return VK_SUCCESS;
    }
    return VK_NOT_READY;
}

VkResult ffxFsr4VkRetireFrame(FfxInterface* iface, uint64_t completedFrameId)
{
    if (!iface || !iface->device) return VK_ERROR_INITIALIZATION_FAILED;
    FfxFsr4VkContext* c = (FfxFsr4VkContext*)iface->device;
    /* A scheduler/dispatch error can leave an opened frame with partially
     * recorded work.  The host's completion guarantee is sufficient to retire
     * that frame too; otherwise it would permanently consume a pool slot. */
    if (c->recordingFrame != UINT32_MAX) {
        const FrameLifetime* recording = &c->frame[c->recordingFrame];
        if (recording->frameId > completedFrameId)
            return VK_ERROR_VALIDATION_FAILED_EXT;
        c->recordingFrame = UINT32_MAX;
    }

    for (uint32_t i = 0; i < NUM_POOL_FRAMES; ++i) {
        FrameLifetime* frame = &c->frame[i];
        if (!frame->inUse || frame->frameId > completedFrameId) continue;
        profCollect(c, i);
        if (vkResetDescriptorPool(c->dev, c->pool[i], 0) != VK_SUCCESS)
            return VK_ERROR_DEVICE_LOST;
        memset(frame, 0, sizeof(*frame));
    }
    for (uint32_t i = 0; i < c->stagingCount;) {
        Staging* st = &c->staging[i];
        if (!st->submitted || st->frameId > completedFrameId) {
            ++i;
            continue;
        }
        vkDestroyBuffer(c->dev, st->buf, c->alloc);
        vkFreeMemory(c->dev, st->mem, c->alloc);
        c->staging[i] = c->staging[--c->stagingCount];
    }
    return VK_SUCCESS;
}

VkResult ffxFsr4VkSetExternalImageState(
    FfxInterface* iface, const FfxFsr4VkExternalImageState* state)
{
    if (!iface || !iface->device)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (!state || state->structSize != sizeof(*state) || !state->image ||
        !state->view || !state->stageMask || !state->restoreStageMask ||
        state->restoreLayout == VK_IMAGE_LAYOUT_UNDEFINED ||
        state->restoreLayout == VK_IMAGE_LAYOUT_PREINITIALIZED)
        return VK_ERROR_VALIDATION_FAILED_EXT;

    FfxFsr4VkContext* c = (FfxFsr4VkContext*)iface->device;
    for (uint32_t i = 0; i < c->externalImageCount; ++i) {
        if (c->externalImages[i].state.view == state->view) {
            c->externalImages[i].state = *state;
            return VK_SUCCESS;
        }
    }
    if (c->externalImageCount >= MAX_EXTERNAL_IMAGES)
        return VK_ERROR_OUT_OF_POOL_MEMORY;
    c->externalImages[c->externalImageCount++].state = *state;
    return VK_SUCCESS;
}

// ---------------------------------------------------------------------------
// Helpers for narrow provider-side direct Vulkan access.
//
// Note: the FfxInterface indirection is intentional — the provider holds
// an FfxInterface* and an FfxResourceInternal index, not a VkImage handle.
// These helpers let the provider translate (interface, ri) → VkImage so it
// can issue its own vkCmd calls (e.g. vkCmdClearColorImage on internal
// history textures) without the interface needing a generic "clear" job.
// ---------------------------------------------------------------------------
VkImage ffxFsr4VkGetImage(FfxInterface* iface, FfxResourceInternal ri)
{
    if (!iface || !iface->device) return VK_NULL_HANDLE;
    FfxFsr4VkContext* c = (FfxFsr4VkContext*)iface->device;
    if (ri.internalIndex >= c->resCount) return VK_NULL_HANDLE;
    VkRes* r = &c->res[ri.internalIndex];
    return (r->kind == RES_IMAGE) ? r->img : VK_NULL_HANDLE;
}

uint32_t ffxFsr4VkGetWidth(FfxInterface* iface, FfxResourceInternal ri)
{
    if (!iface || !iface->device) return 0;
    FfxFsr4VkContext* c = (FfxFsr4VkContext*)iface->device;
    if (ri.internalIndex >= c->resCount) return 0;
    VkRes* r = &c->res[ri.internalIndex];
    return (r->kind == RES_IMAGE) ? r->w : 0;
}

uint32_t ffxFsr4VkGetHeight(FfxInterface* iface, FfxResourceInternal ri)
{
    if (!iface || !iface->device) return 0;
    FfxFsr4VkContext* c = (FfxFsr4VkContext*)iface->device;
    if (ri.internalIndex >= c->resCount) return 0;
    VkRes* r = &c->res[ri.internalIndex];
    return (r->kind == RES_IMAGE) ? r->h : 0;
}

int ffxFsr4VkNeedsInitialTransition(FfxInterface* iface, FfxResourceInternal ri)
{
    if (!iface || !iface->device) return 0;
    FfxFsr4VkContext* c = (FfxFsr4VkContext*)iface->device;
    if (ri.internalIndex >= c->resCount) return 0;
    VkRes* r = &c->res[ri.internalIndex];
    return (r->kind == RES_IMAGE) ? r->needsInit : 0;
}

void ffxFsr4VkMarkImageInitialized(FfxInterface* iface, FfxResourceInternal ri)
{
    if (!iface || !iface->device) return;
    FfxFsr4VkContext* c = (FfxFsr4VkContext*)iface->device;
    if (ri.internalIndex >= c->resCount) return;
    VkRes* r = &c->res[ri.internalIndex];
    if (r->kind == RES_IMAGE) r->needsInit = 0;
}

const void* ffxFsr4VkGetModelInitializer(FfxInterface* iface, size_t* sizeBytes)
{
    if (sizeBytes) *sizeBytes = 0;
    if (!iface || !iface->device) return NULL;
    FfxFsr4VkContext* c = (FfxFsr4VkContext*)iface->device;
    if (sizeBytes) *sizeBytes = c->modelInitializerSize;
    return c->modelInitializer;
}

const void* ffxFsr4VkGetPrePassWeights(FfxInterface* iface, size_t* sizeBytes)
{
    if (sizeBytes) *sizeBytes = 0;
    if (!iface || !iface->device) return NULL;
    FfxFsr4VkContext* c = (FfxFsr4VkContext*)iface->device;
    if (sizeBytes) *sizeBytes = c->prePassWeightsSize;
    return c->prePassWeights;
}
