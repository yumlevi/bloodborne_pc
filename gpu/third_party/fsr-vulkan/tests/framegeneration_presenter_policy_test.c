#include "ffx_vk_framegeneration_presenter.h"

#include <stdio.h>

static int failures;

typedef struct acquire_sequence_s {
    VkResult results[2];
    uint32_t indices[2];
    unsigned int calls;
} acquire_sequence_t;

static VkResult acquire_sequence(void *user_data, VkSemaphore semaphore,
    uint32_t *out_image_index)
{
    acquire_sequence_t *sequence = user_data;
    const unsigned int call = sequence->calls++;
    (void)semaphore;
    if (call >= 2u)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (sequence->results[call] == VK_SUCCESS ||
        sequence->results[call] == VK_SUBOPTIMAL_KHR)
        *out_image_index = sequence->indices[call];
    return sequence->results[call];
}

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); \
        ++failures; \
    } \
} while (0)

int main(void)
{
    const VkPresentModeKHR immediate[] = { VK_PRESENT_MODE_FIFO_KHR,
                                           VK_PRESENT_MODE_IMMEDIATE_KHR };
    const VkPresentModeKHR mailbox[] = { VK_PRESENT_MODE_FIFO_KHR,
                                         VK_PRESENT_MODE_MAILBOX_KHR };

    CHECK(ffxVkFrameGenerationSelectPresentMode(true, false, immediate, 2) ==
          VK_PRESENT_MODE_FIFO_KHR);
    CHECK(ffxVkFrameGenerationSelectPresentMode(false, true, immediate, 2) ==
          VK_PRESENT_MODE_FIFO_KHR);
    CHECK(ffxVkFrameGenerationSelectPresentMode(false, false, immediate, 2) ==
          VK_PRESENT_MODE_IMMEDIATE_KHR);
    CHECK(ffxVkFrameGenerationSelectPresentMode(false, false, mailbox, 2) ==
          VK_PRESENT_MODE_MAILBOX_KHR);

    CHECK(ffxVkFrameGenerationRequiredImageCount(3, true) == 5);
    CHECK(ffxVkFrameGenerationRequiredImageCount(3, false) == 3);
    CHECK(ffxVkFrameGenerationRequestedImageCount(3, 0, true) == 5);
    CHECK(ffxVkFrameGenerationRequestedImageCount(3, 4, true) == 4);
    CHECK(ffxVkFrameGenerationRequestedImageCount(1, 0, false) == 2);

    CHECK(ffxVkFrameGenerationValidateAcquiredPair(0, 1, 4));
    CHECK(!ffxVkFrameGenerationValidateAcquiredPair(0, 0, 4));
    CHECK(!ffxVkFrameGenerationValidateAcquiredPair(0, 4, 4));
    CHECK(!ffxVkFrameGenerationValidateAcquiredPair(0, 1, 1));

    {
        acquire_sequence_t sequence = {
            .results = { VK_SUCCESS, VK_SUCCESS }, .indices = { 1, 3 },
        };
        FfxVkFrameGenerationAcquiredPair pair;
        FfxVkFrameGenerationPresentPlan plan;
        CHECK(ffxVkFrameGenerationAcquirePair(acquire_sequence, &sequence,
            (VkSemaphore)(uintptr_t)1, (VkSemaphore)(uintptr_t)2, 4, &pair) == VK_SUCCESS);
        CHECK(sequence.calls == 2u);
        CHECK(pair.generatedImageAcquired && pair.realImageAcquired && pair.paired);
        CHECK(pair.generatedImageIndex == 1u && pair.realImageIndex == 3u);
        CHECK(ffxVkFrameGenerationBuildPresentPlan(&pair, true, false, &plan));
        CHECK(plan.slotCount == 2u);
        CHECK(plan.slots[0].imageIndex == 1u &&
              plan.slots[0].imageAvailableSemaphore == (VkSemaphore)(uintptr_t)1 &&
              plan.slots[0].useInterpolatedScene);
        CHECK(plan.slots[1].imageIndex == 3u &&
              plan.slots[1].imageAvailableSemaphore == (VkSemaphore)(uintptr_t)2 &&
              !plan.slots[1].useInterpolatedScene);
        CHECK(ffxVkFrameGenerationBuildPresentPlan(&pair, true, true, &plan));
        CHECK(plan.slotCount == 2u && !plan.slots[0].useInterpolatedScene);
    }
    {
        acquire_sequence_t sequence = {
            .results = { VK_SUBOPTIMAL_KHR, VK_SUCCESS }, .indices = { 0, 2 },
        };
        FfxVkFrameGenerationAcquiredPair pair;
        CHECK(ffxVkFrameGenerationAcquirePair(acquire_sequence, &sequence,
            VK_NULL_HANDLE, VK_NULL_HANDLE, 4, &pair) == VK_SUBOPTIMAL_KHR);
        CHECK(pair.generatedImageAcquired && pair.realImageAcquired && pair.paired);
    }
    {
        acquire_sequence_t sequence = {
            .results = { VK_SUCCESS, VK_NOT_READY }, .indices = { 2, 0 },
        };
        FfxVkFrameGenerationAcquiredPair pair;
        FfxVkFrameGenerationPresentPlan plan;
        CHECK(ffxVkFrameGenerationAcquirePair(acquire_sequence, &sequence,
            VK_NULL_HANDLE, VK_NULL_HANDLE, 4, &pair) == VK_NOT_READY);
        CHECK(sequence.calls == 2u);
        CHECK(pair.generatedImageAcquired && !pair.realImageAcquired && !pair.paired);
        CHECK(pair.generatedImageIndex == 2u);
        CHECK(ffxVkFrameGenerationBuildPresentPlan(&pair, true, false, &plan));
        CHECK(plan.slotCount == 1u && plan.slots[0].imageIndex == 2u &&
              !plan.slots[0].useInterpolatedScene);
    }
    {
        acquire_sequence_t sequence = {
            .results = { VK_ERROR_OUT_OF_DATE_KHR, VK_SUCCESS }, .indices = { 0, 1 },
        };
        FfxVkFrameGenerationAcquiredPair pair;
        CHECK(ffxVkFrameGenerationAcquirePair(acquire_sequence, &sequence,
            VK_NULL_HANDLE, VK_NULL_HANDLE, 4, &pair) == VK_ERROR_OUT_OF_DATE_KHR);
        CHECK(sequence.calls == 1u && !pair.generatedImageAcquired);
    }
    {
        acquire_sequence_t sequence = {
            .results = { VK_SUCCESS, VK_SUCCESS }, .indices = { 1, 1 },
        };
        FfxVkFrameGenerationAcquiredPair pair;
        CHECK(ffxVkFrameGenerationAcquirePair(acquire_sequence, &sequence,
            VK_NULL_HANDLE, VK_NULL_HANDLE, 4, &pair) == VK_ERROR_INITIALIZATION_FAILED);
        CHECK(pair.generatedImageAcquired && pair.realImageAcquired && !pair.paired);
        CHECK(!ffxVkFrameGenerationBuildPresentPlan(&pair, true, false, NULL));
    }

    CHECK(ffxVkFrameGenerationShouldPresentGenerated(true, false));
    CHECK(!ffxVkFrameGenerationShouldPresentGenerated(true, true));
    CHECK(!ffxVkFrameGenerationShouldPresentGenerated(false, false));

    CHECK(ffxVkFrameGenerationTransitionNeedsQuiescence(true, false));
    CHECK(ffxVkFrameGenerationTransitionNeedsQuiescence(false, true));
    CHECK(!ffxVkFrameGenerationTransitionNeedsQuiescence(true, true));
    CHECK(!ffxVkFrameGenerationTransitionNeedsQuiescence(false, false));

    CHECK(ffxVkFrameGenerationRenderFinishedSemaphoreIndex(3, 0, 2) == 6u);
    CHECK(ffxVkFrameGenerationRenderFinishedSemaphoreIndex(3, 1, 2) == 7u);
    CHECK(ffxVkFrameGenerationRenderFinishedSemaphoreIndex(0, 1, 0) == SIZE_MAX);
    CHECK(ffxVkFrameGenerationRenderFinishedSemaphoreIndex(0, 2, 2) == SIZE_MAX);
    return failures ? 1 : 0;
}
