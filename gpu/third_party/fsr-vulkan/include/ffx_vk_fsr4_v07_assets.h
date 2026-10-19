/*
 * Reusable naming/selection contract for the source-bearing FSR4 v07 INT8
 * Vulkan asset bundle.  It deliberately contains no filesystem or Vulkan
 * calls: applications provide loading, pipeline creation and lifetime policy.
 */
#pragma once

#include "ffx_vk_fsr4_v07_schedule.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FFX_FSR4_V07_ASSET_PATH_MAX 96u
#define FFX_FSR4_V07_INITIALIZER_BYTES 89216u
#define FFX_FSR4_V07_PRE_PASS_WEIGHTS_BYTES 1024u

typedef struct FfxFsr4V07AssetSet {
    FfxFsr4ModelPreset preset;
    char tier[5];
    char pre[FFX_FSR4_V07_ASSET_PATH_MAX];
    char model[FFX_FSR4_MODEL_PASS_COUNT][FFX_FSR4_V07_ASSET_PATH_MAX];
    char post[FFX_FSR4_V07_ASSET_PATH_MAX];
    char initializer[FFX_FSR4_V07_ASSET_PATH_MAX];
    char prePassWeights[FFX_FSR4_V07_ASSET_PATH_MAX];
    char rcas[FFX_FSR4_V07_ASSET_PATH_MAX];
    char spdAutoExposure[FFX_FSR4_V07_ASSET_PATH_MAX];
} FfxFsr4V07AssetSet;

/* Selects the resolution-specialized tensor graph tier for an output extent. */
const char *ffxFsr4V07SelectAssetTier(uint32_t outputWidth,
                                      uint32_t outputHeight);

/*
 * Produces all canonical relative names for one coherent model bundle. Returns
 * false for an invalid preset, zero/unsupported output extent, or null output.
 * The initializer and pass-0 weights are expected to be 89,216 and 1,024
 * bytes, respectively; those bytes are intentionally not embedded here.
 */
bool ffxFsr4V07BuildAssetSet(FfxFsr4ModelPreset preset,
                             uint32_t outputWidth,
                             uint32_t outputHeight,
                             FfxFsr4V07AssetSet *outAssets);

#ifdef __cplusplus
}
#endif
