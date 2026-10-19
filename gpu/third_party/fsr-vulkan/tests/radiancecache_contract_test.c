#include "ffx_vk_radiancecache_contract.h"

#include <assert.h>
#include <math.h>
#include <string.h>

static FfxVkPortableBuffer buffer(FfxVkPortableResourceState state) {
    return (FfxVkPortableBuffer){
        .structSize = sizeof(FfxVkPortableBuffer),
        .buffer = (VkBuffer)(uintptr_t)1,
        .size = 4096,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .state = state,
    };
}

int main(void) {
    FfxVkRadianceCacheCreateInfo create_info = {
        .structSize = sizeof(FfxVkRadianceCacheCreateInfo),
        .contractVersion = FFX_VK_RADIANCECACHE_CONTRACT_VERSION,
        .maxInferenceSampleCount = 960 * 540,
        .maxTrainingSampleCount = 960 * 54,
    };
    FfxVkRadianceCacheDispatchInfo dispatch_info;
    uint64_t issues = 0;

    assert(ffxVkRadianceCacheValidateCreateInfo(&create_info, &issues) ==
        FFX_VK_PORTABLE_OK);
    memset(&dispatch_info, 0, sizeof(dispatch_info));
    dispatch_info.structSize = sizeof(dispatch_info);
    dispatch_info.contractVersion = FFX_VK_RADIANCECACHE_CONTRACT_VERSION;
    dispatch_info.flags = FFX_VK_RADIANCECACHE_DISPATCH_INFERENCE |
        FFX_VK_RADIANCECACHE_DISPATCH_TRAINING |
        FFX_VK_RADIANCECACHE_CLEAR_INFERENCE_COUNTER |
        FFX_VK_RADIANCECACHE_CLEAR_TRAINING_COUNTER;
    dispatch_info.predictionInputs =
        buffer(FFX_VK_PORTABLE_RESOURCE_STATE_COMPUTE_READ);
    dispatch_info.predictionOutputs =
        buffer(FFX_VK_PORTABLE_RESOURCE_STATE_UNORDERED_ACCESS);
    dispatch_info.trainingInputs =
        buffer(FFX_VK_PORTABLE_RESOURCE_STATE_COMPUTE_READ);
    dispatch_info.trainingTargets =
        buffer(FFX_VK_PORTABLE_RESOURCE_STATE_GENERIC_READ);
    dispatch_info.sampleCounters =
        buffer(FFX_VK_PORTABLE_RESOURCE_STATE_UNORDERED_ACCESS);
    assert(ffxVkRadianceCacheValidateDispatchInfo(&create_info, &dispatch_info,
        &issues) == FFX_VK_PORTABLE_OK);
    assert(issues == FFX_VK_RADIANCECACHE_VALIDATION_NONE);

    dispatch_info.sampleCounters.size = 4;
    assert(ffxVkRadianceCacheValidateDispatchInfo(&create_info, &dispatch_info,
        &issues) == FFX_VK_PORTABLE_ERROR_INVALID_ARGUMENT);
    assert((issues & FFX_VK_RADIANCECACHE_VALIDATION_COUNTER_BUFFER) != 0);
    dispatch_info.sampleCounters.size = 4096;

    dispatch_info.predictionOutputs.state =
        FFX_VK_PORTABLE_RESOURCE_STATE_COMPUTE_READ;
    assert(ffxVkRadianceCacheValidateDispatchInfo(&create_info, &dispatch_info,
        &issues) == FFX_VK_PORTABLE_ERROR_INVALID_ARGUMENT);
    assert((issues & FFX_VK_RADIANCECACHE_VALIDATION_BUFFER_STATE) != 0);
    dispatch_info.predictionOutputs =
        buffer(FFX_VK_PORTABLE_RESOURCE_STATE_UNORDERED_ACCESS);

    dispatch_info.flags = FFX_VK_RADIANCECACHE_OVERRIDE_LEARNING_RATE;
    dispatch_info.learningRate = NAN;
    assert(ffxVkRadianceCacheValidateDispatchInfo(&create_info, &dispatch_info,
        &issues) == FFX_VK_PORTABLE_ERROR_INVALID_ARGUMENT);
    assert((issues & FFX_VK_RADIANCECACHE_VALIDATION_NONFINITE_OVERRIDE) != 0);
    return 0;
}
