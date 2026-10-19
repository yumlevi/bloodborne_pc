/*
 * Exact, API-independent scheduling helpers for the FSR4 v07 INT8/DOT4
 * network.  Keeping this logic outside the Q2RTX renderer makes the Vulkan
 * backend usable by other applications and gives us something small enough
 * to validate against AMD's reference provider.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FFX_FSR4_MODEL_PASS_COUNT 12u
#define FFX_FSR4_DOT4_MAX_OUTPUT_WIDTH 7680u
#define FFX_FSR4_DOT4_MAX_OUTPUT_HEIGHT 4320u

typedef struct FfxFsr4DispatchSize {
    uint32_t x;
    uint32_t y;
    uint32_t z;
} FfxFsr4DispatchSize;

typedef struct FfxFsr4Dot4Schedule {
    uint32_t alignedWidth;
    uint32_t alignedHeight;
    FfxFsr4DispatchSize pre;
    FfxFsr4DispatchSize model[FFX_FSR4_MODEL_PASS_COUNT];
    FfxFsr4DispatchSize post;
    FfxFsr4DispatchSize rcas;
} FfxFsr4Dot4Schedule;

/*
 * SPD auto-exposure uses one 64x64 workgroup per input tile.  The shader can
 * reduce up to 12 mip levels in one dispatch; these values are independent of
 * Q2RTX and are useful to any Vulkan host that uses the shipped SPD shader.
 */
typedef struct FfxFsr4SpdSchedule {
    FfxFsr4DispatchSize dispatch;
    uint32_t workgroupCount;
    uint32_t mipCount;
} FfxFsr4SpdSchedule;

typedef enum FfxFsr4ModelPreset {
    FFX_FSR4_MODEL_PRESET_NATIVE_AA = 0,
    FFX_FSR4_MODEL_PRESET_QUALITY,
    FFX_FSR4_MODEL_PRESET_BALANCED,
    FFX_FSR4_MODEL_PRESET_PERFORMANCE,
    FFX_FSR4_MODEL_PRESET_ULTRA_PERFORMANCE,
    FFX_FSR4_MODEL_PRESET_DRS
} FfxFsr4ModelPreset;

/*
 * Build the non-WMMA schedule used by the INT8/DOT4 model.  Output dimensions
 * are aligned to eight pixels exactly as in AMD's provider.  Returns false
 * for zero dimensions, an extent beyond the 8K model specialization, or a
 * null output pointer.
 */
bool ffxFsr4BuildDot4Schedule(uint32_t outputWidth,
                             uint32_t outputHeight,
                             FfxFsr4Dot4Schedule *outSchedule);

/* Build the auto-exposure SPD schedule for a non-zero render extent. */
bool ffxFsr4BuildSpdSchedule(uint32_t renderWidth,
                             uint32_t renderHeight,
                             FfxFsr4SpdSchedule *outSchedule);

/* Exact activation scratch sizes used by the v07 generated shaders. */
size_t ffxFsr4GetDot4ScratchSize(uint32_t maxOutputWidth,
                                uint32_t maxOutputHeight);

/* Select the model asset matching the horizontal upscale ratio. */
FfxFsr4ModelPreset ffxFsr4SelectModelPreset(uint32_t renderWidth,
                                           uint32_t outputWidth,
                                           bool dynamicResolutionModel);

const char *ffxFsr4ModelPresetName(FfxFsr4ModelPreset preset);

#ifdef __cplusplus
}
#endif
