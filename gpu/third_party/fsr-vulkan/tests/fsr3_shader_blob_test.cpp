/*
 * Copyright (c) 2026 Q2RTX FSR Vulkan contributors
 * SPDX-License-Identifier: MIT
 */

#include <FidelityFX/host/ffx_fsr3upscaler.h>
#include <FidelityFX/host/ffx_frameinterpolation.h>
#include <FidelityFX/host/ffx_opticalflow.h>
#include <FidelityFX/host/ffx_types.h>

#include "ffx_shader_blobs.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <utility>

namespace {

std::uint64_t fnv1a64(const std::uint8_t* data, std::size_t size)
{
    std::uint64_t hash = UINT64_C(14695981039346656037);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

bool reflectionIsConsistent(const FfxShaderBlob& blob)
{
    return (blob.cbvCount == 0 || (blob.boundConstantBufferNames && blob.boundConstantBuffers &&
                                   blob.boundConstantBufferCounts && blob.boundConstantBufferSpaces)) &&
           (blob.srvTextureCount == 0 || (blob.boundSRVTextureNames && blob.boundSRVTextures &&
                                          blob.boundSRVTextureCounts && blob.boundSRVTextureSpaces)) &&
           (blob.uavTextureCount == 0 || (blob.boundUAVTextureNames && blob.boundUAVTextures &&
                                          blob.boundUAVTextureCounts && blob.boundUAVTextureSpaces)) &&
           (blob.srvBufferCount == 0 || (blob.boundSRVBufferNames && blob.boundSRVBuffers &&
                                         blob.boundSRVBufferCounts && blob.boundSRVBufferSpaces)) &&
           (blob.uavBufferCount == 0 || (blob.boundUAVBufferNames && blob.boundUAVBuffers &&
                                         blob.boundUAVBufferCounts && blob.boundUAVBufferSpaces)) &&
           (blob.samplerCount == 0 || (blob.boundSamplerNames && blob.boundSamplers &&
                                       blob.boundSamplerCounts && blob.boundSamplerSpaces));
}

} // namespace

int main(int argc, char** argv)
{
    std::filesystem::path dumpDirectory;
    if (argc == 3 && std::strcmp(argv[1], "--dump") == 0) {
        dumpDirectory = argv[2];
        std::error_code error;
        std::filesystem::create_directories(dumpDirectory, error);
        if (error) {
            std::fprintf(stderr, "could not create dump directory: %s\n", error.message().c_str());
            return 2;
        }
    } else if (argc != 1) {
        std::fprintf(stderr, "usage: %s [--dump DIRECTORY]\n", argv[0]);
        return 2;
    }

    constexpr FfxFsr3UpscalerPass passes[] = {
        FFX_FSR3UPSCALER_PASS_PREPARE_INPUTS,
        FFX_FSR3UPSCALER_PASS_LUMA_PYRAMID,
        FFX_FSR3UPSCALER_PASS_SHADING_CHANGE_PYRAMID,
        FFX_FSR3UPSCALER_PASS_SHADING_CHANGE,
        FFX_FSR3UPSCALER_PASS_PREPARE_REACTIVITY,
        FFX_FSR3UPSCALER_PASS_LUMA_INSTABILITY,
        FFX_FSR3UPSCALER_PASS_ACCUMULATE,
        FFX_FSR3UPSCALER_PASS_ACCUMULATE_SHARPEN,
        FFX_FSR3UPSCALER_PASS_RCAS,
        FFX_FSR3UPSCALER_PASS_DEBUG_VIEW,
        FFX_FSR3UPSCALER_PASS_GENERATE_REACTIVE,
    };

    std::set<std::pair<std::uint32_t, std::uint64_t>> uniqueBlobs;
    std::size_t referencesChecked = 0;
    for (const FfxFsr3UpscalerPass pass : passes) {
        for (std::uint32_t options = 0; options < 256; ++options) {
            FfxShaderBlob blob{};
            const FfxErrorCode result = ffxGetPermutationBlobByIndex(
                FFX_EFFECT_FSR3UPSCALER, static_cast<FfxPass>(pass), FFX_BIND_COMPUTE_SHADER_STAGE,
                options, &blob);
            if (result != FFX_OK || !blob.data || blob.size < sizeof(std::uint32_t) ||
                blob.size % sizeof(std::uint32_t) != 0) {
                std::fprintf(stderr, "invalid blob: pass=%u options=0x%02x result=%d size=%u\n",
                             static_cast<unsigned>(pass), options, static_cast<int>(result), blob.size);
                return 1;
            }

            std::uint32_t magic = 0;
            std::memcpy(&magic, blob.data, sizeof(magic));
            if (magic != UINT32_C(0x07230203) || !reflectionIsConsistent(blob)) {
                std::fprintf(stderr, "invalid SPIR-V or reflection: pass=%u options=0x%02x\n",
                             static_cast<unsigned>(pass), options);
                return 1;
            }

            const std::uint64_t hash = fnv1a64(blob.data, blob.size);
            const auto inserted = uniqueBlobs.emplace(blob.size, hash);
            if (inserted.second && !dumpDirectory.empty()) {
                char name[80]{};
                std::snprintf(name, sizeof(name), "pass_%02u_%08x_%016llx.spv",
                              static_cast<unsigned>(pass), blob.size,
                              static_cast<unsigned long long>(hash));
                std::ofstream output(dumpDirectory / name, std::ios::binary);
                output.write(reinterpret_cast<const char*>(blob.data), blob.size);
                if (!output) {
                    std::fprintf(stderr, "could not write %s\n", name);
                    return 2;
                }
            }
            ++referencesChecked;
        }
    }

    if (uniqueBlobs.size() < 10) {
        std::fprintf(stderr, "unexpectedly few unique shader blobs: %zu\n", uniqueBlobs.size());
        return 1;
    }

    auto validateFrameGenerationEffect = [&](FfxEffect effect, uint32_t passCount,
                                             uint32_t optionCount, const char* effectName) {
        std::set<std::pair<std::uint32_t, std::uint64_t>> uniqueEffectBlobs;
        std::size_t checked = 0;
        for (uint32_t pass = 0; pass < passCount; ++pass) {
            for (uint32_t options = 0; options < optionCount; ++options) {
                FfxShaderBlob blob{};
                const FfxErrorCode result = ffxGetPermutationBlobByIndex(
                    effect, static_cast<FfxPass>(pass), FFX_BIND_COMPUTE_SHADER_STAGE,
                    options, &blob);
                if (result != FFX_OK || !blob.data || blob.size < sizeof(std::uint32_t) ||
                    blob.size % sizeof(std::uint32_t) != 0) {
                    std::fprintf(stderr, "invalid %s blob: pass=%u options=0x%02x result=%d size=%u\n",
                                 effectName, pass, options, static_cast<int>(result), blob.size);
                    return false;
                }

                std::uint32_t magic = 0;
                std::memcpy(&magic, blob.data, sizeof(magic));
                if (magic != UINT32_C(0x07230203) || !reflectionIsConsistent(blob)) {
                    std::fprintf(stderr, "invalid %s SPIR-V or reflection: pass=%u options=0x%02x\n",
                                 effectName, pass, options);
                    return false;
                }

                const std::uint64_t hash = fnv1a64(blob.data, blob.size);
                const auto inserted = uniqueEffectBlobs.emplace(blob.size, hash);
                if (inserted.second && !dumpDirectory.empty()) {
                    char name[96]{};
                    std::snprintf(name, sizeof(name), "%s_%02u_%08x_%016llx.spv", effectName,
                                  pass, blob.size, static_cast<unsigned long long>(hash));
                    std::ofstream output(dumpDirectory / name, std::ios::binary);
                    output.write(reinterpret_cast<const char*>(blob.data), blob.size);
                    if (!output) {
                        std::fprintf(stderr, "could not write %s\n", name);
                        return false;
                    }
                }
                ++checked;
            }
        }
        if (uniqueEffectBlobs.size() < passCount * 2u) {
            std::fprintf(stderr, "unexpectedly few unique %s shader blobs: %zu\n",
                         effectName, uniqueEffectBlobs.size());
            return false;
        }
        std::printf("validated %zu %s permutation references (%zu unique SPIR-V modules)\n",
                    checked, effectName, uniqueEffectBlobs.size());
        return true;
    };

    /* FI has three shader defines plus wave64 and FP16 table selectors. */
    if (!validateFrameGenerationEffect(FFX_EFFECT_FRAMEINTERPOLATION,
                                       FFX_FRAMEINTERPOLATION_PASS_COUNT, 32, "frameinterpolation")) {
        return 1;
    }
    /* Optical Flow has HDR input plus wave64 and FP16 table selectors. */
    if (!validateFrameGenerationEffect(FFX_EFFECT_OPTICALFLOW,
                                       FFX_OPTICALFLOW_PASS_COUNT, 8, "opticalflow")) {
        return 1;
    }

    std::printf("validated %zu FSR3 upscaler permutation references (%zu unique SPIR-V modules)\n",
                referencesChecked, uniqueBlobs.size());
    return 0;
}
