/*
 * Copyright (c) 2026 Q2RTX FSR Vulkan contributors
 * SPDX-License-Identifier: MIT
 */

#include "ffx_vk_fsr4_v07.h"
#include "ffx_vk_fsr4_v07_assets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef FFX_VK_FSR4_V07_TEST_ASSET_DIR
#error "FFX_VK_FSR4_V07_TEST_ASSET_DIR must identify an external asset bundle"
#endif

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "check failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

static int read_shader(const char *name, FfxFsr4VkShaderBlob *out, void **storage)
{
    char path[1024];
    FILE *file;
    long length;
    void *data;
    const int written = snprintf(path, sizeof(path), "%s/%s",
                                 FFX_VK_FSR4_V07_TEST_ASSET_DIR, name);
    if (written < 0 || (size_t)written >= sizeof(path) ||
        !(file = fopen(path, "rb")))
        return 0;
    if (fseek(file, 0, SEEK_END) || (length = ftell(file)) <= 0 ||
        fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return 0;
    }
    data = malloc((size_t)length);
    if (!data || fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return 0;
    }
    fclose(file);
    out->spirv = data;
    out->sizeBytes = (size_t)length;
    out->entryPoint = "main";
    *storage = data;
    return 1;
}

static int validate_asset(const char *name, uint32_t pass)
{
    FfxFsr4VkShaderBlob shader;
    void *storage = NULL;
    const int read = read_shader(name, &shader, &storage);
    const VkResult result = read ? ffxFsr4VkValidateShaderLayout(&shader, pass)
                                 : VK_ERROR_INVALID_SHADER_NV;
    free(storage);
    return result == VK_SUCCESS;
}

int main(void)
{
    const uint32_t tier_widths[] = {1280u, 2560u, 5000u};
    FfxFsr4V07AssetSet assets;
    for (uint32_t preset = FFX_FSR4_MODEL_PRESET_NATIVE_AA;
         preset <= FFX_FSR4_MODEL_PRESET_DRS; ++preset) {
        for (size_t tier = 0; tier < sizeof(tier_widths) / sizeof(tier_widths[0]); ++tier) {
            CHECK(ffxFsr4V07BuildAssetSet((FfxFsr4ModelPreset)preset,
                                           tier_widths[tier], 720u, &assets));
            CHECK(validate_asset(assets.pre, 0u));
            for (uint32_t pass = 0; pass < FFX_FSR4_MODEL_PASS_COUNT; ++pass)
                CHECK(validate_asset(assets.model[pass], pass + 1u));
            CHECK(validate_asset(assets.post, 13u));
            CHECK(validate_asset(assets.rcas, 14u));
            CHECK(validate_asset(assets.spdAutoExposure, 15u));
        }
    }

    /* A valid pre-pass cannot masquerade as a model pass. This catches an
     * accidental pass-index/asset-set mismatch before pipeline creation. */
    {
        FfxFsr4VkShaderBlob shader;
        void *storage = NULL;
        CHECK(read_shader(assets.pre, &shader, &storage));
        CHECK(ffxFsr4VkValidateShaderLayout(&shader, 1u) != VK_SUCCESS);
        free(storage);
    }
    return 0;
}
