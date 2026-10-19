/*
 * Small, window-system-neutral policy helpers for a generated-then-real
 * Vulkan presentation pair. Applications still own acquire, submit, present,
 * fences, and swapchain lifetime.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

/* FIFO is required for an enabled generated→real pair. Otherwise preserve the
 * normal low-latency preference: Immediate when offered, then Mailbox. */
VkPresentModeKHR ffxVkFrameGenerationSelectPresentMode(
    bool frameGenerationEnabled, bool vsyncEnabled,
    const VkPresentModeKHR *availableModes, uint32_t availableModeCount);

/* A generated and real image can both be acquired while the presentation
 * engine retains its surface minimum. Returns zero if minImageCount would
 * overflow the representable Vulkan image count. */
uint32_t ffxVkFrameGenerationRequiredImageCount(uint32_t minImageCount,
                                                bool frameGenerationEnabled);

/* Applies Vulkan's maxImageCount (zero means no limit) to the request. */
uint32_t ffxVkFrameGenerationRequestedImageCount(uint32_t minImageCount,
                                                 uint32_t maxImageCount,
                                                 bool frameGenerationEnabled);

/* Verify that two successful acquires can form a generated→real pair. */
bool ffxVkFrameGenerationValidateAcquiredPair(uint32_t generatedImageIndex,
                                              uint32_t realImageIndex,
                                              uint32_t swapchainImageCount);

/* The host retains all WSI ownership, but a frame-generation host needs the
 * same careful two-acquire result handling on every window system. This
 * callback can wrap vkAcquireNextImageKHR, vkAcquireNextImage2KHR, or a
 * renderer's WSI abstraction. It must signal `availableSemaphore` only when
 * it returns VK_SUCCESS or VK_SUBOPTIMAL_KHR. */
typedef VkResult (*FfxVkFrameGenerationAcquireImageFn)(void *userData,
                                                        VkSemaphore availableSemaphore,
                                                        uint32_t *outImageIndex);

/* A first acquired image remains valid even when the second acquisition
 * cannot form a generated->real pair. In that case `generatedImageAcquired`
 * is true while `paired` is false; render and present it normally using
 * `generatedImageIndex` and `generatedAvailableSemaphore`. Do not acquire a
 * third image merely to fall back. */
typedef struct FfxVkFrameGenerationAcquiredPair {
    uint32_t generatedImageIndex;
    uint32_t realImageIndex;
    VkSemaphore generatedAvailableSemaphore;
    VkSemaphore realAvailableSemaphore;
    VkResult generatedAcquireResult;
    VkResult realAcquireResult;
    bool generatedImageAcquired;
    bool realImageAcquired;
    bool paired;
} FfxVkFrameGenerationAcquiredPair;

/* Acquire the generated target followed by the real target through the host's
 * callback. A valid pair returns VK_SUCCESS or VK_SUBOPTIMAL_KHR. If the
 * second acquire fails, that result is returned and the output still records
 * the first image for a normal one-image fallback. If the first acquire fails,
 * the callback is not invoked a second time. */
VkResult ffxVkFrameGenerationAcquirePair(
    FfxVkFrameGenerationAcquireImageFn acquireImage,
    void *userData,
    VkSemaphore generatedAvailableSemaphore,
    VkSemaphore realAvailableSemaphore,
    uint32_t swapchainImageCount,
    FfxVkFrameGenerationAcquiredPair *outPair);

/* The policy layer does not record a renderer's command buffers or submit to
 * its queues, but it can make the acquired-image ownership explicit before
 * the host does so. A one-slot plan means the first acquire must be rendered
 * and presented normally. A two-slot plan is always ordered generated then
 * real. The first slot may still use the real scene when interpolation was
 * reset or rejected; it must nevertheless be submitted and presented so its
 * acquired WSI image is released. */
typedef struct FfxVkFrameGenerationPresentSlot {
    uint32_t imageIndex;
    VkSemaphore imageAvailableSemaphore;
    bool useInterpolatedScene;
} FfxVkFrameGenerationPresentSlot;

typedef struct FfxVkFrameGenerationPresentPlan {
    FfxVkFrameGenerationPresentSlot slots[2];
    uint32_t slotCount;
} FfxVkFrameGenerationPresentPlan;

/* Build the exact render/present work plan from an acquire outcome. The
 * caller records, submits, and presents each returned slot in order. This
 * function returns false for an invalid/stranded pair (for example duplicate
 * acquired image indices), so the host can recreate the swapchain instead of
 * presenting only one of two acquired images. */
bool ffxVkFrameGenerationBuildPresentPlan(
    const FfxVkFrameGenerationAcquiredPair *acquiredPair,
    bool interpolationDispatched,
    bool reset,
    FfxVkFrameGenerationPresentPlan *outPlan);

/* A successful reset dispatch establishes interpolation history but has no
 * preceding image from which an intermediate image can be generated. Present
 * the real image for that paired slot and resume generation next frame. */
bool ffxVkFrameGenerationShouldPresentGenerated(bool interpolationDispatched,
                                                bool reset);

/* Switching between ordinary and generated→real presentation can leave the
 * prior mode's binary present wait pending after its rendering fence completes.
 * Quiesce the present queue once before reusing presentation semaphores. */
bool ffxVkFrameGenerationTransitionNeedsQuiescence(bool wasFrameGenerationActive,
                                                   bool isFrameGenerationActive);

/* Render-complete binary semaphores must be owned by image and GPU, never a
 * frame-in-flight. Callers allocate imageCount * deviceCount entries. */
size_t ffxVkFrameGenerationRenderFinishedSemaphoreIndex(uint32_t imageIndex,
                                                         uint32_t gpuIndex,
                                                         uint32_t deviceCount);

#ifdef __cplusplus
}
#endif
