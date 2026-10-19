/* Copyright (c) 2026 Q2RTX FSR Vulkan contributors SPDX-License-Identifier: MIT */
#include "ffx_vk_fsr3_3_1_5_bundle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    for (uint32_t pass = 0; pass <= 10u; ++pass) {
        char path[512];
        const uint32_t* words;
        size_t wordCount;
        FfxVkFsr3_3_1_5Module module;
        FILE* file;
        long bytes;
        const uint32_t permutation = pass == 7u ? 39u : 7u;
        if (ffxVkFsr3_3_1_5GetModule(pass, permutation, &module) != FFX_VK_PORTABLE_OK ||
            ffxVkFsr3_3_1_5GetEmbeddedModule(pass, permutation, &words, &wordCount) != FFX_VK_PORTABLE_OK ||
            !words || wordCount < 5u || words[0] != 0x07230203u ||
            snprintf(path, sizeof(path), "%s/%s", FSR3_315_SPV_DIR, module.filename) < 0)
            return 1;
        /* The module filename comes from the table, without duplicating its map here. */
        file = fopen(path, "rb");
        if (!file)
            return 1;
        if (fseek(file, 0, SEEK_END) != 0 || (bytes = ftell(file)) != (long)(wordCount * sizeof(*words)) ||
            fseek(file, 0, SEEK_SET) != 0) {
            fclose(file);
            return 1;
        }
        {
            uint32_t* disk = malloc((size_t)bytes);
            const int match = disk && fread(disk, 1u, (size_t)bytes, file) == (size_t)bytes &&
                              memcmp(disk, words, (size_t)bytes) == 0;
            free(disk);
            fclose(file);
            if (!match)
                return 1;
        }
    }
    return 0;
}
