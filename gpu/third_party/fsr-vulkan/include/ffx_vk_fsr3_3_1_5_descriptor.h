/* Copyright (c) 2026 Q2RTX FSR Vulkan contributors SPDX-License-Identifier: MIT */
#ifndef FFX_VK_FSR3_3_1_5_DESCRIPTOR_H
#define FFX_VK_FSR3_3_1_5_DESCRIPTOR_H

#include "ffx_vk_fsr3_3_1_5_pipeline.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct FfxVkFsr3_3_1_5DescriptorResource {
    const char* name;
    VkImageView imageView;
    VkImageLayout imageLayout;
    VkBuffer buffer;
    VkDeviceSize bufferOffset;
    VkDeviceSize bufferRange;
} FfxVkFsr3_3_1_5DescriptorResource;

typedef struct FfxVkFsr3_3_1_5DescriptorSet {
    VkDevice device;
    const VkAllocationCallbacks* allocationCallbacks;
    VkDescriptorPool descriptorPool;
    VkDescriptorSet descriptorSet;
} FfxVkFsr3_3_1_5DescriptorSet;

/*
 * Allocates and writes every non-immutable descriptor in `pipeline`. Resource
 * names must match the reflected FSR names exactly; each required resource
 * must be supplied exactly once and unexpected resources are rejected.
 */
FfxVkPortableResult ffxVkFsr3_3_1_5CreateDescriptorSet(
    const FfxVkFsr3_3_1_5Pipeline* pipeline,
    const FfxVkFsr3_3_1_5DescriptorResource* resources,
    uint32_t resourceCount,
    FfxVkFsr3_3_1_5DescriptorSet* outDescriptorSet);

void ffxVkFsr3_3_1_5DestroyDescriptorSet(FfxVkFsr3_3_1_5DescriptorSet* descriptorSet);

#if defined(__cplusplus)
}
#endif
#endif
