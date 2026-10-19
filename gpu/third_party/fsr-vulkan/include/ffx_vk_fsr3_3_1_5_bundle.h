/* Copyright (c) 2026 Q2RTX FSR Vulkan contributors SPDX-License-Identifier: MIT */
#ifndef FFX_VK_FSR3_3_1_5_BUNDLE_H
#define FFX_VK_FSR3_3_1_5_BUNDLE_H

#include "ffx_vk_fsr3_3_1_5_catalog.h"

#if defined(__cplusplus)
extern "C" {
#endif

/* Returns process-lifetime checked SPIR-V owned by the portable backend. */
FfxVkPortableResult ffxVkFsr3_3_1_5GetEmbeddedModule(
    uint32_t pass, uint32_t permutation, const uint32_t** outWords, size_t* outWordCount);

#if defined(__cplusplus)
}
#endif
#endif
