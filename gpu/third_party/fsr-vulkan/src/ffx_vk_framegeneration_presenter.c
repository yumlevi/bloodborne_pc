#include "ffx_vk_framegeneration_presenter.h"

#include <limits.h>

VkPresentModeKHR ffxVkFrameGenerationSelectPresentMode(
    bool frameGenerationEnabled, bool vsyncEnabled,
    const VkPresentModeKHR *availableModes, uint32_t availableModeCount)
{
    if (frameGenerationEnabled || vsyncEnabled)
        return VK_PRESENT_MODE_FIFO_KHR;

    for (uint32_t i = 0; i < availableModeCount; ++i)
        if (availableModes && availableModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR)
            return VK_PRESENT_MODE_IMMEDIATE_KHR;
    return VK_PRESENT_MODE_MAILBOX_KHR;
}

uint32_t ffxVkFrameGenerationRequiredImageCount(uint32_t minImageCount,
                                                bool frameGenerationEnabled)
{
    if (!frameGenerationEnabled)
        return minImageCount > 2u ? minImageCount : 2u;
    if (minImageCount > UINT32_MAX - 2u)
        return 0;
    return minImageCount + 2u;
}

uint32_t ffxVkFrameGenerationRequestedImageCount(uint32_t minImageCount,
                                                 uint32_t maxImageCount,
                                                 bool frameGenerationEnabled)
{
    uint32_t requested = ffxVkFrameGenerationRequiredImageCount(
        minImageCount, frameGenerationEnabled);
    if (!requested)
        return 0;
    if (maxImageCount && requested > maxImageCount)
        requested = maxImageCount;
    return requested < minImageCount ? minImageCount : requested;
}

bool ffxVkFrameGenerationValidateAcquiredPair(uint32_t generatedImageIndex,
                                              uint32_t realImageIndex,
                                              uint32_t swapchainImageCount)
{
    return swapchainImageCount > 1u &&
           generatedImageIndex < swapchainImageCount &&
           realImageIndex < swapchainImageCount &&
           generatedImageIndex != realImageIndex;
}

static bool ffx_vk_framegeneration_acquire_succeeded(VkResult result)
{
    return result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR;
}

VkResult ffxVkFrameGenerationAcquirePair(
    FfxVkFrameGenerationAcquireImageFn acquireImage,
    void *userData,
    VkSemaphore generatedAvailableSemaphore,
    VkSemaphore realAvailableSemaphore,
    uint32_t swapchainImageCount,
    FfxVkFrameGenerationAcquiredPair *outPair)
{
    if (!outPair)
        return VK_ERROR_INITIALIZATION_FAILED;

    *outPair = (FfxVkFrameGenerationAcquiredPair) {
        .generatedAvailableSemaphore = generatedAvailableSemaphore,
        .realAvailableSemaphore = realAvailableSemaphore,
        .generatedAcquireResult = VK_ERROR_INITIALIZATION_FAILED,
        .realAcquireResult = VK_ERROR_INITIALIZATION_FAILED,
    };
    if (!acquireImage || swapchainImageCount < 2u)
        return VK_ERROR_INITIALIZATION_FAILED;

    outPair->generatedAcquireResult = acquireImage(userData,
        generatedAvailableSemaphore, &outPair->generatedImageIndex);
    if (!ffx_vk_framegeneration_acquire_succeeded(
            outPair->generatedAcquireResult))
        return outPair->generatedAcquireResult;
    outPair->generatedImageAcquired = true;

    outPair->realAcquireResult = acquireImage(userData,
        realAvailableSemaphore, &outPair->realImageIndex);
    if (!ffx_vk_framegeneration_acquire_succeeded(outPair->realAcquireResult))
        return outPair->realAcquireResult;
    outPair->realImageAcquired = true;
    outPair->paired = ffxVkFrameGenerationValidateAcquiredPair(
        outPair->generatedImageIndex, outPair->realImageIndex,
        swapchainImageCount);
    if (!outPair->paired)
        return VK_ERROR_INITIALIZATION_FAILED;
    return outPair->generatedAcquireResult == VK_SUBOPTIMAL_KHR ||
           outPair->realAcquireResult == VK_SUBOPTIMAL_KHR
        ? VK_SUBOPTIMAL_KHR : VK_SUCCESS;
}

bool ffxVkFrameGenerationBuildPresentPlan(
    const FfxVkFrameGenerationAcquiredPair *acquiredPair,
    bool interpolationDispatched,
    bool reset,
    FfxVkFrameGenerationPresentPlan *outPlan)
{
    if (!outPlan)
        return false;
    *outPlan = (FfxVkFrameGenerationPresentPlan) { 0 };
    if (!acquiredPair || !acquiredPair->generatedImageAcquired)
        return false;

    outPlan->slots[0] = (FfxVkFrameGenerationPresentSlot) {
        .imageIndex = acquiredPair->generatedImageIndex,
        .imageAvailableSemaphore = acquiredPair->generatedAvailableSemaphore,
        .useInterpolatedScene = acquiredPair->paired &&
            ffxVkFrameGenerationShouldPresentGenerated(interpolationDispatched,
                reset),
    };
    outPlan->slotCount = 1;

    if (!acquiredPair->paired)
        return !acquiredPair->realImageAcquired;
    if (!acquiredPair->realImageAcquired ||
        acquiredPair->generatedImageIndex == acquiredPair->realImageIndex)
        return false;

    outPlan->slots[1] = (FfxVkFrameGenerationPresentSlot) {
        .imageIndex = acquiredPair->realImageIndex,
        .imageAvailableSemaphore = acquiredPair->realAvailableSemaphore,
        .useInterpolatedScene = false,
    };
    outPlan->slotCount = 2;
    return true;
}

bool ffxVkFrameGenerationShouldPresentGenerated(bool interpolationDispatched,
                                                bool reset)
{
    return interpolationDispatched && !reset;
}

bool ffxVkFrameGenerationTransitionNeedsQuiescence(bool wasFrameGenerationActive,
                                                   bool isFrameGenerationActive)
{
    return wasFrameGenerationActive != isFrameGenerationActive;
}

size_t ffxVkFrameGenerationRenderFinishedSemaphoreIndex(uint32_t imageIndex,
                                                         uint32_t gpuIndex,
                                                         uint32_t deviceCount)
{
    if (!deviceCount || gpuIndex >= deviceCount)
        return SIZE_MAX;
    if ((size_t)imageIndex > (SIZE_MAX - (size_t)gpuIndex) / (size_t)deviceCount)
        return SIZE_MAX;
    return (size_t)imageIndex * (size_t)deviceCount + (size_t)gpuIndex;
}
