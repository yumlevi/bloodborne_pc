/* Copyright (c) 2026 Q2RTX FSR Vulkan contributors SPDX-License-Identifier: MIT */
#ifndef FFX_VK_FSR3_3_1_5_CATALOG_H
#define FFX_VK_FSR3_3_1_5_CATALOG_H
#include "ffx_vk_portable.h"
#if defined(__cplusplus)
extern "C" {
#endif

typedef struct FfxVkFsr3_3_1_5Module {
    const char* filename;
    uint32_t expectedPermutation;
} FfxVkFsr3_3_1_5Module;

/* SDK FfxFsr3UpscalerPass numeric values, kept API-free for reuse. */
FfxVkPortableResult ffxVkFsr3_3_1_5GetModule(
    uint32_t pass, uint32_t permutation, FfxVkFsr3_3_1_5Module* outModule);
#if defined(__cplusplus)
}
#endif
#endif
