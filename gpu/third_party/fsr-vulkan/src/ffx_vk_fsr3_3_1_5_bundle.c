/* Copyright (c) 2026 Q2RTX FSR Vulkan contributors SPDX-License-Identifier: MIT */
#include "ffx_vk_fsr3_3_1_5_bundle.h"
#include "ffx_vk_fsr3_3_1_5_embedded_spirv.h"

#include <string.h>

FfxVkPortableResult ffxVkFsr3_3_1_5GetEmbeddedModule(
    uint32_t pass, uint32_t permutation, const uint32_t** outWords, size_t* outWordCount)
{
    FfxVkFsr3_3_1_5Module module;
    if (!outWords || !outWordCount)
        return FFX_VK_PORTABLE_ERROR_INVALID_POINTER;
    if (ffxVkFsr3_3_1_5GetModule(pass, permutation, &module) != FFX_VK_PORTABLE_OK)
        return FFX_VK_PORTABLE_ERROR_UNSUPPORTED;
    for (uint32_t index = 0; index < FFX_VK_FSR3_3_1_5_EMBEDDED_SPIRV_COUNT; ++index) {
        if (strcmp(module.filename, FFX_VK_FSR3_3_1_5_EMBEDDED_SPIRV[index].filename) == 0) {
            *outWords = FFX_VK_FSR3_3_1_5_EMBEDDED_SPIRV[index].words;
            *outWordCount = FFX_VK_FSR3_3_1_5_EMBEDDED_SPIRV[index].wordCount;
            return FFX_VK_PORTABLE_OK;
        }
    }
    return FFX_VK_PORTABLE_ERROR_BACKEND;
}
