/*
 * Copyright (c) 2026 Q2RTX FSR Vulkan contributors
 * SPDX-License-Identifier: MIT
 */

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <cmath>
#include <stdio.h>

#include <FidelityFX/host/ffx_frameinterpolation.h>
#include <FidelityFX/host/ffx_fsr3.h>
#include <FidelityFX/host/ffx_fsr3upscaler.h>
#include <FidelityFX/host/ffx_opticalflow.h>

static void expect_version(FfxVersionNumber version, uint32_t major, uint32_t minor, uint32_t patch)
{
    assert((version >> 22) == major);
    assert(((version >> 12) & 0x3ffu) == minor);
    assert((version & 0xfffu) == patch);
}

int main(void)
{
    float jitterX = 0.0f;
    float jitterY = 0.0f;
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;

    expect_version(ffxFsr3GetEffectVersion(), 3, 1, 4);
    expect_version(ffxFsr3UpscalerGetEffectVersion(), 3, 1, 4);
    expect_version(ffxFrameInterpolationGetEffectVersion(), 1, 1, 3);
    expect_version(ffxOpticalflowGetEffectVersion(), 1, 1, 2);

    assert(std::fabs(ffxFsr3GetUpscaleRatioFromQualityMode(FFX_FSR3_QUALITY_MODE_QUALITY) - 1.5f) < 0.0001f);
    assert(ffxFsr3GetRenderResolutionFromQualityMode(
        &renderWidth,
        &renderHeight,
        3840,
        2160,
        FFX_FSR3_QUALITY_MODE_PERFORMANCE) == FFX_OK);
    assert(renderWidth == 1920 && renderHeight == 1080);

    assert(ffxFsr3GetJitterOffset(
        &jitterX,
        &jitterY,
        0,
        ffxFsr3GetJitterPhaseCount(1920, 3840)) == FFX_OK);
    assert(std::isfinite(jitterX) && std::isfinite(jitterY));
    assert(jitterX >= -0.5f && jitterX <= 0.5f);
    assert(jitterY >= -0.5f && jitterY <= 0.5f);

    puts("AMD FSR 3.1.4 host scheduler tests passed");
    return 0;
}
