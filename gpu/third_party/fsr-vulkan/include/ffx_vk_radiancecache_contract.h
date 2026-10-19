/*
 * Copyright (c) 2026 Q2RTX FSR Vulkan contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef FFX_VK_RADIANCECACHE_CONTRACT_H
#define FFX_VK_RADIANCECACHE_CONTRACT_H

#include "ffx_vk_portable.h"

#if defined(__cplusplus)
extern "C" {
#endif

/* Provider-neutral host contract for the public FSR Radiance Caching API.
 * It validates only application-owned buffers, counters, and state
 * transitions. It has no neural kernels, model weights, or AMD provider. */
#define FFX_VK_RADIANCECACHE_CONTRACT_VERSION 1u

typedef enum FfxVkRadianceCacheDispatchFlagBits {
    FFX_VK_RADIANCECACHE_DISPATCH_INFERENCE = 1u << 0,
    FFX_VK_RADIANCECACHE_DISPATCH_TRAINING = 1u << 1,
    FFX_VK_RADIANCECACHE_CLEAR_INFERENCE_COUNTER = 1u << 2,
    FFX_VK_RADIANCECACHE_CLEAR_TRAINING_COUNTER = 1u << 3,
    FFX_VK_RADIANCECACHE_RESET = 1u << 4,
    FFX_VK_RADIANCECACHE_OVERRIDE_LEARNING_RATE = 1u << 5,
    FFX_VK_RADIANCECACHE_OVERRIDE_WEIGHT_SMOOTHING = 1u << 6
} FfxVkRadianceCacheDispatchFlagBits;

typedef enum FfxVkRadianceCacheValidationIssueBits {
    FFX_VK_RADIANCECACHE_VALIDATION_NONE = 0,
    FFX_VK_RADIANCECACHE_VALIDATION_STRUCT_SIZE = 1ull << 0,
    FFX_VK_RADIANCECACHE_VALIDATION_SAMPLE_CAPACITY = 1ull << 1,
    FFX_VK_RADIANCECACHE_VALIDATION_FLAGS = 1ull << 2,
    FFX_VK_RADIANCECACHE_VALIDATION_BUFFER_HANDLE = 1ull << 3,
    FFX_VK_RADIANCECACHE_VALIDATION_BUFFER_USAGE = 1ull << 4,
    FFX_VK_RADIANCECACHE_VALIDATION_BUFFER_STATE = 1ull << 5,
    FFX_VK_RADIANCECACHE_VALIDATION_COUNTER_BUFFER = 1ull << 6,
    FFX_VK_RADIANCECACHE_VALIDATION_NONFINITE_OVERRIDE = 1ull << 7
} FfxVkRadianceCacheValidationIssueBits;

typedef struct FfxVkRadianceCacheCreateInfo {
    uint32_t structSize;
    uint32_t contractVersion;
    /* Maximum per-frame samples supported by application-owned buffers. */
    uint32_t maxInferenceSampleCount;
    uint32_t maxTrainingSampleCount;
} FfxVkRadianceCacheCreateInfo;

typedef struct FfxVkRadianceCacheDispatchInfo {
    uint32_t structSize;
    uint32_t contractVersion;
    uint32_t flags;
    /* Prediction buffers are required for INFERENCE. Training buffers are
     * required for TRAINING. Their element layouts are provider-defined, so
     * this contract validates lifetime/state only. */
    FfxVkPortableBuffer predictionInputs;
    FfxVkPortableBuffer predictionOutputs;
    FfxVkPortableBuffer trainingInputs;
    FfxVkPortableBuffer trainingTargets;
    /* Two uint32 atomics: inference count then training count. The provider
     * owns their in-dispatch read/write transition. */
    FfxVkPortableBuffer sampleCounters;
    float learningRate;
    float weightSmoothing;
} FfxVkRadianceCacheDispatchInfo;

FfxVkPortableResult ffxVkRadianceCacheValidateCreateInfo(
    const FfxVkRadianceCacheCreateInfo* createInfo, uint64_t* issues);

FfxVkPortableResult ffxVkRadianceCacheValidateDispatchInfo(
    const FfxVkRadianceCacheCreateInfo* createInfo,
    const FfxVkRadianceCacheDispatchInfo* dispatchInfo, uint64_t* issues);

#if defined(__cplusplus)
}
#endif

#endif
