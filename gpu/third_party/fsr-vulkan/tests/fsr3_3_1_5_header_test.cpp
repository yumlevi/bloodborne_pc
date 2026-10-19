/*
 * Copyright (c) 2026 Q2RTX FSR Vulkan contributors
 * SPDX-License-Identifier: MIT
 */

#include <cstddef>
#include <cstdio>

#include "ffx_fsr3upscaler.h"

static_assert(FFX_FSR3UPSCALER_VERSION_MAJOR == 3);
static_assert(FFX_FSR3UPSCALER_VERSION_MINOR == 1);
static_assert(FFX_FSR3UPSCALER_VERSION_PATCH == 5);

// The imported SDK's default context is an array of uint32_t values.  Linux
// needs the widened 1 MiB opaque allocation because the private scheduler
// contains fixed wchar_t binding labels and wchar_t is four bytes here.
static_assert(sizeof(FfxFsr3UpscalerContext) >= 1024u * 1024u);

int main()
{
    std::printf("FSR3 %d.%d.%d opaque context: %zu bytes\n",
                FFX_FSR3UPSCALER_VERSION_MAJOR,
                FFX_FSR3UPSCALER_VERSION_MINOR,
                FFX_FSR3UPSCALER_VERSION_PATCH,
                sizeof(FfxFsr3UpscalerContext));
    return 0;
}
