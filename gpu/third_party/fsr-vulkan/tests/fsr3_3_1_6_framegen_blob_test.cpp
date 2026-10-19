/*
 * Copyright (c) 2026 Q2RTX FSR Vulkan contributors
 * SPDX-License-Identifier: MIT
 */

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "ffx_frameinterpolation_shaderblobs.h"
#include "ffx_opticalflow_shaderblobs.h"
#include "ffx_frameinterpolation_private.h"
#include "ffx_opticalflow_private.h"

namespace {

constexpr uint32_t kSpirvMagic = 0x07230203u;
constexpr uint32_t kFiProfile = FRAMEINTERPOLATION_SHADER_PERMUTATION_LOW_RES_MOTION_VECTORS;

bool check_blob(const char* family, uint32_t pass, const FfxShaderBlob& blob)
{
    uint32_t magic = 0;
    if (!blob.data || !blob.entryName || std::strcmp(blob.entryName, "CS") != 0 ||
        blob.size < sizeof(magic) || blob.size % sizeof(uint32_t) != 0) {
        std::fprintf(stderr, "%s pass %u returned an invalid blob descriptor\n", family, pass);
        return false;
    }
    std::memcpy(&magic, blob.data, sizeof(magic));
    if (magic != kSpirvMagic) {
        std::fprintf(stderr, "%s pass %u is not SPIR-V\n", family, pass);
        return false;
    }
    return true;
}

} // namespace

int main()
{
    for (uint32_t pass = 0; pass < static_cast<uint32_t>(FFX_FRAMEINTERPOLATION_PASS_COUNT); ++pass) {
        FfxShaderBlob blob{};
        if (frameInterpolationGetPermutationBlobByIndex(
                static_cast<FfxFrameInterpolationPass>(pass), kFiProfile, &blob) != FFX_OK ||
            !check_blob("frame interpolation", pass, blob)) {
            return 1;
        }
    }

    for (uint32_t pass = 0; pass < static_cast<uint32_t>(FFX_OPTICALFLOW_PASS_COUNT); ++pass) {
        FfxShaderBlob blob{};
        if (opticalflowGetPermutationBlobByIndex(
                static_cast<FfxOpticalflowPass>(pass), 0u, &blob) != FFX_OK ||
            !check_blob("optical flow", pass, blob)) {
            return 1;
        }
    }

    FfxShaderBlob rejected{};
    if (frameInterpolationGetPermutationBlobByIndex(
            FFX_FRAMEINTERPOLATION_PASS_SETUP, 0u, &rejected) == FFX_OK ||
        frameInterpolationGetPermutationBlobByIndex(
            FFX_FRAMEINTERPOLATION_PASS_SETUP,
            kFiProfile | FRAMEINTERPOLATION_SHADER_PERMUTATION_JITTER_MOTION_VECTORS,
            &rejected) == FFX_OK ||
        frameInterpolationGetPermutationBlobByIndex(
            static_cast<FfxFrameInterpolationPass>(FFX_FRAMEINTERPOLATION_PASS_COUNT),
            kFiProfile, &rejected) == FFX_OK ||
        frameInterpolationGetPermutationBlobByIndex(
            FFX_FRAMEINTERPOLATION_PASS_SETUP, kFiProfile, nullptr) == FFX_OK ||
        opticalflowGetPermutationBlobByIndex(
            FFX_OPTICALFLOW_PASS_PREPARE_LUMA,
            OPTICALFLOW_SHADER_PERMUTATION_FORCE_WAVE64, &rejected) == FFX_OK ||
        opticalflowGetPermutationBlobByIndex(
            static_cast<FfxOpticalflowPass>(FFX_OPTICALFLOW_PASS_COUNT), 0u, &rejected) == FFX_OK ||
        opticalflowGetPermutationBlobByIndex(
            FFX_OPTICALFLOW_PASS_PREPARE_LUMA, 0u, nullptr) == FFX_OK) {
        std::fprintf(stderr, "unsupported FI/OF blob request was accepted\n");
        return 1;
    }

    bool isWave64 = false;
    if (frameInterpolationIsWave64(kFiProfile, isWave64) != FFX_OK || isWave64 ||
        frameInterpolationIsWave64(kFiProfile | FRAMEINTERPOLATION_SHADER_PERMUTATION_FORCE_WAVE64, isWave64) != FFX_OK || !isWave64 ||
        opticalflowIsWave64(0u, isWave64) != FFX_OK || isWave64 ||
        opticalflowIsWave64(OPTICALFLOW_SHADER_PERMUTATION_FORCE_WAVE64, isWave64) != FFX_OK || !isWave64) {
        std::fprintf(stderr, "FI/OF wave64 query returned an inconsistent result\n");
        return 1;
    }

    std::printf("verified %u FI and %u OF fixed-profile SPIR-V blobs\n",
                static_cast<uint32_t>(FFX_FRAMEINTERPOLATION_PASS_COUNT),
                static_cast<uint32_t>(FFX_OPTICALFLOW_PASS_COUNT));
    return 0;
}
