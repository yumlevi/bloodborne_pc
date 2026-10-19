/*
 * Copyright (c) 2026 Q2RTX FSR Vulkan contributors
 * SPDX-License-Identifier: MIT
 *
 * Fixed-profile public SDK 3.1.6 FI/OF shader-blob accessors.  AMD's source
 * host scheduler asks for DX12 blobs by pass/permutation; this Vulkan port
 * instead returns the checked embedded SPIR-V module for the one profile
 * actually supported by the reusable backend.
 */

#include "ffx_frameinterpolation_shaderblobs.h"
#include "ffx_frameinterpolation_private.h"
#include "ffx_opticalflow_shaderblobs.h"
#include "ffx_opticalflow_private.h"

#include "ffx_vk_fsr3_3_1_6_framegen_embedded_spirv.h"

#include <cstring>

namespace {

constexpr uint32_t kFiProfile = FRAMEINTERPOLATION_SHADER_PERMUTATION_LOW_RES_MOTION_VECTORS;
constexpr uint32_t kOfProfile = 0u;
constexpr uint32_t kFiModuleCount = FFX_FRAMEINTERPOLATION_PASS_COUNT;
constexpr uint32_t kOfModuleBase = kFiModuleCount;

static_assert(FFX_VK_FSR3_3_1_6_FRAMEGEN_EMBEDDED_SPIRV_COUNT ==
                  static_cast<uint32_t>(FFX_FRAMEINTERPOLATION_PASS_COUNT) +
                      static_cast<uint32_t>(FFX_OPTICALFLOW_PASS_COUNT),
              "embedded FI/OF module table must cover every SDK 3.1.6 pass");

FfxErrorCode set_blob(uint32_t index, FfxShaderBlob* outBlob)
{
    if (!outBlob)
        return static_cast<FfxErrorCode>(FFX_ERROR_INVALID_POINTER);
    if (index >= FFX_VK_FSR3_3_1_6_FRAMEGEN_EMBEDDED_SPIRV_COUNT)
        return static_cast<FfxErrorCode>(FFX_ERROR_INVALID_ARGUMENT);
    const FfxVkFsr3_3_1_6FramegenEmbeddedSpirv& module =
        FFX_VK_FSR3_3_1_6_FRAMEGEN_EMBEDDED_SPIRV[index];
    std::memset(outBlob, 0, sizeof(*outBlob));
    outBlob->data = reinterpret_cast<const uint8_t*>(module.words);
    outBlob->size = static_cast<uint32_t>(module.wordCount * sizeof(uint32_t));
    outBlob->entryName = "CS";
    return FFX_OK;
}

} // namespace

FfxErrorCode frameInterpolationGetPermutationBlobByIndex(
    FfxFrameInterpolationPass passId,
    uint32_t permutationOptions,
    FfxShaderBlob* outBlob)
{
    if (permutationOptions != kFiProfile)
        return static_cast<FfxErrorCode>(FFX_ERROR_INVALID_ARGUMENT);
    if (passId < 0 || passId >= FFX_FRAMEINTERPOLATION_PASS_COUNT)
        return static_cast<FfxErrorCode>(FFX_ERROR_INVALID_ARGUMENT);
    return set_blob(static_cast<uint32_t>(passId), outBlob);
}

FfxErrorCode frameInterpolationIsWave64(uint32_t permutationOptions, bool& isWave64)
{
    isWave64 = (permutationOptions & FRAMEINTERPOLATION_SHADER_PERMUTATION_FORCE_WAVE64) != 0u;
    return FFX_OK;
}

FfxErrorCode opticalflowGetPermutationBlobByIndex(
    FfxOpticalflowPass passId,
    uint32_t permutationOptions,
    FfxShaderBlob* outBlob)
{
    if (permutationOptions != kOfProfile)
        return static_cast<FfxErrorCode>(FFX_ERROR_INVALID_ARGUMENT);
    if (passId < 0 || passId >= FFX_OPTICALFLOW_PASS_COUNT)
        return static_cast<FfxErrorCode>(FFX_ERROR_INVALID_ARGUMENT);
    return set_blob(kOfModuleBase + static_cast<uint32_t>(passId), outBlob);
}

FfxErrorCode opticalflowIsWave64(uint32_t permutationOptions, bool& isWave64)
{
    isWave64 = (permutationOptions & OPTICALFLOW_SHADER_PERMUTATION_FORCE_WAVE64) != 0u;
    return FFX_OK;
}
