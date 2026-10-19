#include "ffx_vk_fsr4_v07_assets.h"

#include <stdio.h>
#include <string.h>

static bool format_asset(char *destination, size_t destinationSize,
                         const char *format, const char *model,
                         const char *tier, unsigned pass)
{
    int written = snprintf(destination, destinationSize, format, model, tier,
                           pass);
    return written >= 0 && (size_t)written < destinationSize;
}

const char *ffxFsr4V07SelectAssetTier(uint32_t outputWidth,
                                      uint32_t outputHeight)
{
    if (!outputWidth || !outputHeight ||
        outputWidth > FFX_FSR4_DOT4_MAX_OUTPUT_WIDTH ||
        outputHeight > FFX_FSR4_DOT4_MAX_OUTPUT_HEIGHT)
        return NULL;
    if (outputWidth > 3840u || outputHeight > 2160u)
        return "4320";
    if (outputWidth > 1920u || outputHeight > 1080u)
        return "2160";
    return "1080";
}

bool ffxFsr4V07BuildAssetSet(FfxFsr4ModelPreset preset,
                             uint32_t outputWidth,
                             uint32_t outputHeight,
                             FfxFsr4V07AssetSet *outAssets)
{
    const char *model;
    const char *tier;

    if (!outAssets || preset > FFX_FSR4_MODEL_PRESET_DRS)
        return false;
    model = ffxFsr4ModelPresetName(preset);
    tier = ffxFsr4V07SelectAssetTier(outputWidth, outputHeight);
    if (!model || !tier || !strcmp(model, "unknown"))
        return false;

    memset(outAssets, 0, sizeof(*outAssets));
    outAssets->preset = preset;
    snprintf(outAssets->tier, sizeof(outAssets->tier), "%s", tier);
    if (!format_asset(outAssets->pre, sizeof(outAssets->pre),
                      "fsr4_model_v07_i8_%s_%s_pre.spv", model, tier, 0) ||
        !format_asset(outAssets->post, sizeof(outAssets->post),
                      "fsr4_model_v07_i8_%s_%s_post.spv", model, tier, 0) ||
        !format_asset(outAssets->initializer, sizeof(outAssets->initializer),
                      "fsr4_model_v07_i8_%s_initializers.bin", model, "", 0) ||
        !format_asset(outAssets->prePassWeights, sizeof(outAssets->prePassWeights),
                      "fsr4_model_v07_i8_%s_pre_weights.bin", model, "", 0))
        return false;

    for (unsigned pass = 1; pass <= FFX_FSR4_MODEL_PASS_COUNT; ++pass) {
        if (!format_asset(outAssets->model[pass - 1],
                          sizeof(outAssets->model[pass - 1]),
                          "fsr4_model_v07_i8_%s_%s_pass%u.spv",
                          model, tier, pass))
            return false;
    }
    snprintf(outAssets->rcas, sizeof(outAssets->rcas), "%s", "rcas.spv");
    snprintf(outAssets->spdAutoExposure, sizeof(outAssets->spdAutoExposure),
             "%s", "spd_auto_exposure.spv");
    return true;
}
