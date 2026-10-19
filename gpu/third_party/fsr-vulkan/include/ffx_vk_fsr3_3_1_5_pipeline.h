/*
 * Copyright (c) 2026 Q2RTX FSR Vulkan contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef FFX_VK_FSR3_3_1_5_PIPELINE_H
#define FFX_VK_FSR3_3_1_5_PIPELINE_H

#include "ffx_vk_fsr3_3_1_5_reflection.h"

#if defined(__cplusplus)
extern "C" {
#endif

/*
 * The public FSR 3.1.5 shader set currently has no module with more than
 * fourteen descriptors.  Keep the ABI deliberately generous, while making a
 * malformed or future shader fail clearly instead of truncating bindings.
 */
#define FFX_VK_FSR3_3_1_5_MAX_PIPELINE_BINDINGS 32u

typedef struct FfxVkFsr3_3_1_5PipelineCreateInfo {
    uint32_t structSize;
    VkDevice device;
    const VkAllocationCallbacks* allocationCallbacks;
    const uint32_t* spirvWords;
    size_t spirvWordCount;
} FfxVkFsr3_3_1_5PipelineCreateInfo;

/*
 * This is intentionally a pipeline-only object.  It proves that the emitted
 * descriptor ABI is accepted by Vulkan and is reused by the SDK FfxInterface
 * backend.  Descriptor sets, image views, and uniform-buffer staging remain
 * per-dispatch backend work and are not owned here.
 */
typedef struct FfxVkFsr3_3_1_5Pipeline {
    VkDevice device;
    const VkAllocationCallbacks* allocationCallbacks;
    VkShaderModule shaderModule;
    VkDescriptorSetLayout descriptorSetLayout;
    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;
    uint32_t bindingCount;
    FfxVkFsr3_3_1_5DescriptorBinding bindings[FFX_VK_FSR3_3_1_5_MAX_PIPELINE_BINDINGS];
    VkSampler samplers[FFX_VK_FSR3_3_1_5_MAX_PIPELINE_BINDINGS];
} FfxVkFsr3_3_1_5Pipeline;

FfxVkPortableResult ffxVkFsr3_3_1_5CreatePipeline(
    const FfxVkFsr3_3_1_5PipelineCreateInfo* createInfo,
    FfxVkFsr3_3_1_5Pipeline* outPipeline);

void ffxVkFsr3_3_1_5DestroyPipeline(FfxVkFsr3_3_1_5Pipeline* pipeline);

#if defined(__cplusplus)
}
#endif

#endif
