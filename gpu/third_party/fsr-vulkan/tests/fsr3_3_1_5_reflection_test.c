/*
 * Copyright (c) 2026 Q2RTX FSR Vulkan contributors
 * SPDX-License-Identifier: MIT
 */

#include "ffx_vk_fsr3_3_1_5_reflection.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int compare_binding(const void* left, const void* right)
{
    const FfxVkFsr3_3_1_5DescriptorBinding* a = left;
    const FfxVkFsr3_3_1_5DescriptorBinding* b = right;
    return (a->binding > b->binding) - (a->binding < b->binding);
}

int main(void)
{
    static const uint32_t mainEntryPointSpirv[] = {
        0x07230203u, 0x00010000u, 0u, 2u, 0u,
        (uint32_t)((5u << 16u) | 15u), 5u, 1u, 0x6e69616du, 0u
    };
    static const uint32_t twoComputeEntryPointsSpirv[] = {
        0x07230203u, 0x00010000u, 0u, 3u, 0u,
        (uint32_t)((5u << 16u) | 15u), 5u, 1u, 0x6e69616du, 0u,
        (uint32_t)((4u << 16u) | 15u), 5u, 2u, 0x5343u
    };
    char entryPoint[64];
    const char path[] = FSR3_315_SPV_DIR "/fsr3_3_1_5_prepare_inputs.spv";
    FILE* file = fopen(path, "rb");
    long bytes;
    uint32_t* words;
    uint32_t count = 0;
    uint32_t shortCount = 1;
    FfxVkFsr3_3_1_5DescriptorBinding bindings[16];
    FfxVkFsr3_3_1_5DescriptorBinding shortOutput = {.binding = 0xdeadbeefu};
    int foundColor = 0;
    int foundDepth = 0;
    int foundCb = 0;
    const char fiPath[] = FSR3_316_FRAMEGEN_SPV_DIR "/fsr3_3_1_6_fi_ffx_frameinterpolation_setup.spv";
    FILE* fiFile;
    long fiBytes;
    uint32_t* fiWords;
    uint32_t fiCount = 16u;
    FfxVkFsr3_3_1_5DescriptorBinding fiBindings[16];
    int foundCounterUav = 0;

    if (ffxVkFsr3_3_1_5ReflectComputeEntryPoint(mainEntryPointSpirv,
                                                 sizeof(mainEntryPointSpirv) / sizeof(mainEntryPointSpirv[0]),
                                                 entryPoint) != FFX_VK_PORTABLE_OK ||
        strcmp(entryPoint, "main") != 0 ||
        ffxVkFsr3_3_1_5ReflectComputeEntryPoint(twoComputeEntryPointsSpirv,
                                                 sizeof(twoComputeEntryPointsSpirv) /
                                                     sizeof(twoComputeEntryPointsSpirv[0]),
                                                 entryPoint) != FFX_VK_PORTABLE_ERROR_UNSUPPORTED)
        return 1;

    if (!file) {
        fprintf(stderr, "cannot open generated module: %s\n", path);
        return 1;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (bytes = ftell(file)) <= 0 ||
        bytes % (long)sizeof(uint32_t) != 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 1;
    }
    words = malloc((size_t)bytes);
    if (!words || fread(words, 1u, (size_t)bytes, file) != (size_t)bytes) {
        free(words);
        fclose(file);
        return 1;
    }
    fclose(file);

    if (ffxVkFsr3_3_1_5ReflectComputeEntryPoint(words, (size_t)bytes / sizeof(*words),
                                                 entryPoint) != FFX_VK_PORTABLE_OK ||
        strcmp(entryPoint, "CS") != 0) {
        free(words);
        return 1;
    }

    if (ffxVkFsr3_3_1_5ReflectSpirv(words, (size_t)bytes / sizeof(*words),
                                     NULL, &count) != FFX_VK_PORTABLE_OK ||
        count != 9u) {
        free(words);
        return 1;
    }
    if (ffxVkFsr3_3_1_5ReflectSpirv(words, (size_t)bytes / sizeof(*words),
                                     &shortOutput, &shortCount) != FFX_VK_PORTABLE_ERROR_INVALID_ARGUMENT ||
        shortCount != 9u || shortOutput.binding != 0xdeadbeefu) {
        free(words);
        return 1;
    }
    if (ffxVkFsr3_3_1_5ReflectSpirv(words, (size_t)bytes / sizeof(*words),
                                     bindings, &count) != FFX_VK_PORTABLE_OK ||
        count != 9u) {
        free(words);
        return 1;
    }
    qsort(bindings, count, sizeof(bindings[0]), compare_binding);
    for (uint32_t index = 1; index < count; ++index) {
        if (bindings[index - 1u].binding == bindings[index].binding) {
            free(words);
            return 1;
        }
    }
    for (uint32_t index = 0; index < count; ++index) {
        foundColor |= strcmp(bindings[index].name, "r_input_color_jittered") == 0 &&
                      bindings[index].descriptorClass == FFX_VK_FSR3_3_1_5_DESCRIPTOR_SRV;
        foundDepth |= strcmp(bindings[index].name, "rw_dilated_depth") == 0 &&
                      bindings[index].descriptorClass == FFX_VK_FSR3_3_1_5_DESCRIPTOR_UAV;
        foundCb |= strcmp(bindings[index].name, "cbFSR3Upscaler") == 0 &&
                   bindings[index].descriptorClass == FFX_VK_FSR3_3_1_5_DESCRIPTOR_CONSTANT_BUFFER;
    }
    if (!foundColor || !foundDepth || !foundCb)
        goto fail;
    free(words);
    words = NULL;

    fiFile = fopen(fiPath, "rb");
    if (!fiFile || fseek(fiFile, 0, SEEK_END) != 0 || (fiBytes = ftell(fiFile)) <= 0 ||
        fiBytes % (long)sizeof(uint32_t) != 0 || fseek(fiFile, 0, SEEK_SET) != 0) {
        if (fiFile)
            fclose(fiFile);
        return 1;
    }
    fiWords = malloc((size_t)fiBytes);
    if (!fiWords || fread(fiWords, 1u, (size_t)fiBytes, fiFile) != (size_t)fiBytes) {
        free(fiWords);
        fclose(fiFile);
        return 1;
    }
    fclose(fiFile);
    if (ffxVkFsr3_3_1_5ReflectSpirv(fiWords, (size_t)fiBytes / sizeof(*fiWords),
                                     fiBindings, &fiCount) != FFX_VK_PORTABLE_OK) {
        free(fiWords);
        return 1;
    }
    for (uint32_t index = 0; index < fiCount; ++index) {
        foundCounterUav |= strcmp(fiBindings[index].name, "rw_counters") == 0 &&
                           fiBindings[index].descriptorClass ==
                               FFX_VK_FSR3_3_1_5_DESCRIPTOR_BUFFER_UAV;
    }
    free(fiWords);
    if (!foundCounterUav)
        return 1;
    puts("FSR3.1.5 SPIR-V reflection test passed");
    return 0;

fail:
    free(words);
    return 1;
}
