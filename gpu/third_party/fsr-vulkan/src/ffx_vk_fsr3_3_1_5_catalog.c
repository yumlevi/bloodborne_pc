/* Copyright (c) 2026 Q2RTX FSR Vulkan contributors SPDX-License-Identifier: MIT */
#include "ffx_vk_fsr3_3_1_5_catalog.h"

enum { LANCZOS = 1u, HDR = 2u, LOW_RES_MV = 4u, SHARPEN = 32u };
enum { BASE = LANCZOS | HDR | LOW_RES_MV };

FfxVkPortableResult ffxVkFsr3_3_1_5GetModule(
    uint32_t pass, uint32_t permutation, FfxVkFsr3_3_1_5Module* outModule)
{
    static const char* const modules[] = {
        "fsr3_3_1_5_prepare_inputs.spv", "fsr3_3_1_5_luma_pyramid.spv",
        "fsr3_3_1_5_shading_change_pyramid.spv", "fsr3_3_1_5_shading_change.spv",
        "fsr3_3_1_5_prepare_reactivity.spv", "fsr3_3_1_5_luma_instability.spv",
        "fsr3_3_1_5_accumulate.spv", "fsr3_3_1_5_accumulate_sharpen.spv",
        "fsr3_3_1_5_rcas.spv", "fsr3_3_1_5_debug_view.spv",
        "fsr3_3_1_5_autogen_reactive.spv"
    };
    if (!outModule)
        return FFX_VK_PORTABLE_ERROR_INVALID_POINTER;
    if (pass >= sizeof(modules) / sizeof(modules[0]))
        return FFX_VK_PORTABLE_ERROR_UNSUPPORTED;
    if (permutation != (pass == 7u ? BASE | SHARPEN : BASE))
        return FFX_VK_PORTABLE_ERROR_UNSUPPORTED;
    outModule->filename = modules[pass];
    outModule->expectedPermutation = permutation;
    return FFX_VK_PORTABLE_OK;
}
