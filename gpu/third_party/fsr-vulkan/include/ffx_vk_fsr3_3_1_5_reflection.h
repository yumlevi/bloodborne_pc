/*
 * Copyright (c) 2026 Q2RTX FSR Vulkan contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef FFX_VK_FSR3_3_1_5_REFLECTION_H
#define FFX_VK_FSR3_3_1_5_REFLECTION_H

#include "ffx_vk_portable.h"

#if defined(__cplusplus)
extern "C" {
#endif

/* Resource classes are identified from the stable public FSR names, rather
 * than from numeric ranges: t/u/b bindings are deliberately packed per pass,
 * while static samplers use the established 1000+ Vulkan range. */
typedef enum FfxVkFsr3_3_1_5DescriptorClass {
    FFX_VK_FSR3_3_1_5_DESCRIPTOR_SRV = 0,
    FFX_VK_FSR3_3_1_5_DESCRIPTOR_UAV,
    FFX_VK_FSR3_3_1_5_DESCRIPTOR_BUFFER_SRV,
    FFX_VK_FSR3_3_1_5_DESCRIPTOR_BUFFER_UAV,
    FFX_VK_FSR3_3_1_5_DESCRIPTOR_SAMPLER,
    FFX_VK_FSR3_3_1_5_DESCRIPTOR_CONSTANT_BUFFER
} FfxVkFsr3_3_1_5DescriptorClass;

typedef struct FfxVkFsr3_3_1_5DescriptorBinding {
    uint32_t binding;
    FfxVkFsr3_3_1_5DescriptorClass descriptorClass;
    char name[64];
} FfxVkFsr3_3_1_5DescriptorBinding;

/*
 * Reflect the deliberately narrow binding convention used by the generated
 * 3.1.5 modules. `outBindings` may be NULL to query the required count.  On
 * success `inOutBindingCount` receives the number of descriptors; an
 * insufficient non-NULL output array returns INVALID_ARGUMENT without writing
 * a partial result.  This is intentionally a small SPIR-V metadata reader,
 * not a general reflection library.
 */
FfxVkPortableResult ffxVkFsr3_3_1_5ReflectSpirv(
    const uint32_t* words,
    size_t wordCount,
    FfxVkFsr3_3_1_5DescriptorBinding* outBindings,
    uint32_t* inOutBindingCount);

/*
 * Return the sole GLCompute OpEntryPoint name in a module.  Generated AMD
 * modules normally use "CS", but SDK-supplied direct SPIR-V can use "main";
 * callers must use the module declaration instead of an assumed convention.
 */
FfxVkPortableResult ffxVkFsr3_3_1_5ReflectComputeEntryPoint(
    const uint32_t* words,
    size_t wordCount,
    char outEntryPoint[64]);

#if defined(__cplusplus)
}
#endif

#endif
