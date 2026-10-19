/*
 * Compile/link contract for a host that vendors the full reusable FSR3/FSR4
 * project.  It proves that the versioned APIs may coexist in one application;
 * a production host creates its Vulkan device and supplies real images/fences.
 */
#include <ffx_vk_fsr3_3_1_5_bridge.h>
#include <ffx_vk_fsr4_v07.h>
#include <ffx_vk_fsr4_v07_assets.h>
#include <ffx_vk_framegeneration_presenter.h>
#include <ffx_vk_portable.h>
#include <ffx_vk_radiancecache_contract.h>
#include <ffx_vk_rayregeneration_contract.h>
#include <ffx_vk_temporal_lifecycle.h>

int main()
{
    FfxFsr4V07AssetSet fsr4_assets{};
    if (!ffxFsr4V07BuildAssetSet(FFX_FSR4_MODEL_PRESET_QUALITY,
                                 1920u, 1080u, &fsr4_assets))
        return 1;

    /* These calls have no Vulkan side effects and verify the public utilities
     * from the FSR3 upscaler/FI and FSR4 provider closures are linkable. */
    if (ffxVkPortableValidationIssueName(
            FFX_VK_PORTABLE_VALIDATION_ZERO_EXTENT) == nullptr ||
        ffxFsr4VkValidateShaderLayout(nullptr, 0u) != VK_ERROR_INVALID_SHADER_NV ||
        ffxVkFrameGenerationRequiredImageCount(3u, true) != 5u)
        return 2;

    FfxVkFrameGenerationAcquiredPair acquired_pair{};
    acquired_pair.generatedImageIndex = 1u;
    acquired_pair.realImageIndex = 3u;
    acquired_pair.generatedAvailableSemaphore =
        reinterpret_cast<VkSemaphore>(static_cast<uintptr_t>(1u));
    acquired_pair.realAvailableSemaphore =
        reinterpret_cast<VkSemaphore>(static_cast<uintptr_t>(2u));
    acquired_pair.generatedImageAcquired = true;
    acquired_pair.realImageAcquired = true;
    acquired_pair.paired = true;
    FfxVkFrameGenerationPresentPlan present_plan{};
    if (!ffxVkFrameGenerationBuildPresentPlan(&acquired_pair, true, false,
                                               &present_plan) ||
        present_plan.slotCount != 2u ||
        !present_plan.slots[0].useInterpolatedScene ||
        present_plan.slots[1].useInterpolatedScene)
        return 2;

    /* Taking function addresses forces linker resolution of both SDK-2.3
     * reusable APIs without fabricating invalid Vulkan handles. */
    auto fsr3_portable_memory = &ffxVkPortableUpscaleContextGetMemoryUsage;
    auto fsr3_create = &ffxVkFsr3_3_1_5UpscalerContextCreate;
    auto fsr3_memory = &ffxVkFsr3_3_1_5UpscalerContextGetMemoryUsage;
    auto fi_create = &ffxVkFsr3_3_1_6FrameGenerationContextCreate;
    auto fi_memory = &ffxVkFsr3_3_1_6FrameGenerationContextGetMemoryUsage;
    auto rr_validate = &ffxVkRayRegenerationValidateInputs;
    auto radiance_cache_validate = &ffxVkRadianceCacheValidateDispatchInfo;
    auto camera_cut = &ffxVkTemporalCameraCutDetected;
    auto presentation_changed = &ffxVkTemporalPresentationAvailabilityChanged;
    auto acquire_pair = &ffxVkFrameGenerationAcquirePair;
    auto build_present_plan = &ffxVkFrameGenerationBuildPresentPlan;
    return fsr3_portable_memory != nullptr && fsr3_create != nullptr &&
           fsr3_memory != nullptr && fi_create != nullptr && fi_memory != nullptr &&
           rr_validate != nullptr && radiance_cache_validate != nullptr &&
           camera_cut != nullptr &&
           presentation_changed != nullptr &&
           acquire_pair != nullptr && build_present_plan != nullptr ? 0 : 3;
}
