/*
 * Copyright (c) 2026 Q2RTX FSR Vulkan contributors
 * SPDX-License-Identifier: MIT
 */

#include "ffx_vk_radiancecache_contract.h"

#include <math.h>

static int buffer_is_compute_readable(const FfxVkPortableBuffer* buffer,
                                      uint64_t* issues) {
    if (buffer->structSize != sizeof(*buffer)) {
        *issues |= FFX_VK_RADIANCECACHE_VALIDATION_STRUCT_SIZE;
        return 0;
    }
    if (buffer->buffer == VK_NULL_HANDLE) {
        *issues |= FFX_VK_RADIANCECACHE_VALIDATION_BUFFER_HANDLE;
        return 0;
    }
    if ((buffer->usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) == 0)
        *issues |= FFX_VK_RADIANCECACHE_VALIDATION_BUFFER_USAGE;
    if (buffer->state != FFX_VK_PORTABLE_RESOURCE_STATE_GENERIC_READ &&
        buffer->state != FFX_VK_PORTABLE_RESOURCE_STATE_COMPUTE_READ)
        *issues |= FFX_VK_RADIANCECACHE_VALIDATION_BUFFER_STATE;
    return 1;
}

static int buffer_is_compute_writable(const FfxVkPortableBuffer* buffer,
                                      uint64_t* issues) {
    if (buffer->structSize != sizeof(*buffer)) {
        *issues |= FFX_VK_RADIANCECACHE_VALIDATION_STRUCT_SIZE;
        return 0;
    }
    if (buffer->buffer == VK_NULL_HANDLE) {
        *issues |= FFX_VK_RADIANCECACHE_VALIDATION_BUFFER_HANDLE;
        return 0;
    }
    if ((buffer->usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) == 0)
        *issues |= FFX_VK_RADIANCECACHE_VALIDATION_BUFFER_USAGE;
    if (buffer->state != FFX_VK_PORTABLE_RESOURCE_STATE_UNORDERED_ACCESS)
        *issues |= FFX_VK_RADIANCECACHE_VALIDATION_BUFFER_STATE;
    return 1;
}

FfxVkPortableResult ffxVkRadianceCacheValidateCreateInfo(
    const FfxVkRadianceCacheCreateInfo* create_info, uint64_t* issues) {
    uint64_t result = FFX_VK_RADIANCECACHE_VALIDATION_NONE;

    if (!issues)
        return FFX_VK_PORTABLE_ERROR_INVALID_POINTER;
    *issues = FFX_VK_RADIANCECACHE_VALIDATION_NONE;
    if (!create_info)
        return FFX_VK_PORTABLE_ERROR_INVALID_POINTER;
    if (create_info->structSize != sizeof(*create_info) ||
        create_info->contractVersion != FFX_VK_RADIANCECACHE_CONTRACT_VERSION)
        result |= FFX_VK_RADIANCECACHE_VALIDATION_STRUCT_SIZE;
    if (!create_info->maxInferenceSampleCount ||
        !create_info->maxTrainingSampleCount)
        result |= FFX_VK_RADIANCECACHE_VALIDATION_SAMPLE_CAPACITY;
    *issues = result;
    return result == FFX_VK_RADIANCECACHE_VALIDATION_NONE ?
        FFX_VK_PORTABLE_OK : FFX_VK_PORTABLE_ERROR_INVALID_ARGUMENT;
}

FfxVkPortableResult ffxVkRadianceCacheValidateDispatchInfo(
    const FfxVkRadianceCacheCreateInfo* create_info,
    const FfxVkRadianceCacheDispatchInfo* dispatch_info, uint64_t* issues) {
    const uint32_t known_flags = FFX_VK_RADIANCECACHE_DISPATCH_INFERENCE |
        FFX_VK_RADIANCECACHE_DISPATCH_TRAINING |
        FFX_VK_RADIANCECACHE_CLEAR_INFERENCE_COUNTER |
        FFX_VK_RADIANCECACHE_CLEAR_TRAINING_COUNTER |
        FFX_VK_RADIANCECACHE_RESET |
        FFX_VK_RADIANCECACHE_OVERRIDE_LEARNING_RATE |
        FFX_VK_RADIANCECACHE_OVERRIDE_WEIGHT_SMOOTHING;
    uint64_t result = FFX_VK_RADIANCECACHE_VALIDATION_NONE;

    if (!issues)
        return FFX_VK_PORTABLE_ERROR_INVALID_POINTER;
    *issues = FFX_VK_RADIANCECACHE_VALIDATION_NONE;
    if (!create_info || !dispatch_info)
        return FFX_VK_PORTABLE_ERROR_INVALID_POINTER;
    if (ffxVkRadianceCacheValidateCreateInfo(create_info, &result) !=
        FFX_VK_PORTABLE_OK) {
        *issues = result;
        return FFX_VK_PORTABLE_ERROR_INVALID_ARGUMENT;
    }
    if (dispatch_info->structSize != sizeof(*dispatch_info) ||
        dispatch_info->contractVersion != FFX_VK_RADIANCECACHE_CONTRACT_VERSION)
        result |= FFX_VK_RADIANCECACHE_VALIDATION_STRUCT_SIZE;
    if (dispatch_info->flags & ~known_flags)
        result |= FFX_VK_RADIANCECACHE_VALIDATION_FLAGS;
    if (dispatch_info->flags & FFX_VK_RADIANCECACHE_DISPATCH_INFERENCE) {
        buffer_is_compute_readable(&dispatch_info->predictionInputs, &result);
        buffer_is_compute_writable(&dispatch_info->predictionOutputs, &result);
    }
    if (dispatch_info->flags & FFX_VK_RADIANCECACHE_DISPATCH_TRAINING) {
        buffer_is_compute_readable(&dispatch_info->trainingInputs, &result);
        buffer_is_compute_readable(&dispatch_info->trainingTargets, &result);
    }
    if (dispatch_info->flags != 0) {
        if (buffer_is_compute_writable(&dispatch_info->sampleCounters, &result) &&
            dispatch_info->sampleCounters.size < 2u * sizeof(uint32_t))
            result |= FFX_VK_RADIANCECACHE_VALIDATION_COUNTER_BUFFER;
    }
    if (((dispatch_info->flags &
          FFX_VK_RADIANCECACHE_OVERRIDE_LEARNING_RATE) != 0 &&
         !isfinite(dispatch_info->learningRate)) ||
        ((dispatch_info->flags &
          FFX_VK_RADIANCECACHE_OVERRIDE_WEIGHT_SMOOTHING) != 0 &&
         !isfinite(dispatch_info->weightSmoothing)))
        result |= FFX_VK_RADIANCECACHE_VALIDATION_NONFINITE_OVERRIDE;

    *issues = result;
    return result == FFX_VK_RADIANCECACHE_VALIDATION_NONE ?
        FFX_VK_PORTABLE_OK : FFX_VK_PORTABLE_ERROR_INVALID_ARGUMENT;
}
