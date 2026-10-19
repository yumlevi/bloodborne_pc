#include "ffx_vk_fsr4_v07_schedule.h"

#include <limits.h>
#include <string.h>

static uint32_t div_round_up(uint32_t value, uint32_t divisor)
{
    return value / divisor + (value % divisor != 0u);
}

static FfxFsr4DispatchSize dispatch_size(uint32_t x, uint32_t y)
{
    FfxFsr4DispatchSize result = {x, y, 1u};
    return result;
}

bool ffxFsr4BuildDot4Schedule(uint32_t outputWidth,
                             uint32_t outputHeight,
                             FfxFsr4Dot4Schedule *outSchedule)
{
    FfxFsr4Dot4Schedule schedule;
    uint32_t encoder0Width, encoder0Height;
    uint32_t encoder1Width, encoder1Height;
    uint32_t encoder2Width, encoder2Height;

    if (!outSchedule || !outputWidth || !outputHeight ||
        outputWidth > FFX_FSR4_DOT4_MAX_OUTPUT_WIDTH ||
        outputHeight > FFX_FSR4_DOT4_MAX_OUTPUT_HEIGHT)
        return false;

    memset(&schedule, 0, sizeof(schedule));
    schedule.alignedWidth = (outputWidth + 7u) & ~7u;
    schedule.alignedHeight = (outputHeight + 7u) & ~7u;

    encoder0Width = schedule.alignedWidth / 2u;
    encoder0Height = schedule.alignedHeight / 2u;
    encoder1Width = schedule.alignedWidth / 4u;
    encoder1Height = schedule.alignedHeight / 4u;
    encoder2Width = schedule.alignedWidth / 8u;
    encoder2Height = schedule.alignedHeight / 8u;

    schedule.pre = dispatch_size(div_round_up(schedule.alignedWidth, 16u),
                                 div_round_up(schedule.alignedHeight, 16u));

    schedule.model[0] = dispatch_size(div_round_up(encoder0Width, 64u), encoder0Height);
    schedule.model[1] = schedule.model[0];
    schedule.model[2] = dispatch_size(div_round_up(encoder1Width, 64u), encoder1Height);
    schedule.model[3] = schedule.model[2];
    schedule.model[4] = schedule.model[2];
    schedule.model[5] = dispatch_size(div_round_up(encoder2Width, 64u), encoder2Height);
    schedule.model[6] = schedule.model[5];
    schedule.model[7] = schedule.model[5];
    schedule.model[8] = dispatch_size(div_round_up(encoder2Width, 8u),
                                      div_round_up(encoder2Height, 8u));
    schedule.model[9] = dispatch_size(div_round_up(encoder1Width, 64u), encoder1Height);
    schedule.model[10] = schedule.model[9];
    schedule.model[11] = dispatch_size(div_round_up(encoder0Width, 64u), encoder0Height);

    schedule.post = dispatch_size(div_round_up(schedule.alignedWidth, 16u),
                                  div_round_up(schedule.alignedHeight, 16u));
    schedule.rcas = schedule.post;

    *outSchedule = schedule;
    return true;
}

bool ffxFsr4BuildSpdSchedule(uint32_t renderWidth,
                             uint32_t renderHeight,
                             FfxFsr4SpdSchedule *outSchedule)
{
    FfxFsr4SpdSchedule schedule;
    uint32_t maxDimension;

    if (!outSchedule || !renderWidth || !renderHeight)
        return false;

    memset(&schedule, 0, sizeof(schedule));
    schedule.dispatch = dispatch_size(div_round_up(renderWidth, 64u),
                                      div_round_up(renderHeight, 64u));
    if (schedule.dispatch.x > UINT32_MAX / schedule.dispatch.y)
        return false;
    schedule.workgroupCount = schedule.dispatch.x * schedule.dispatch.y;
    maxDimension = renderWidth > renderHeight ? renderWidth : renderHeight;
    while (maxDimension > 1u && schedule.mipCount < 12u) {
        maxDimension >>= 1u;
        ++schedule.mipCount;
    }

    *outSchedule = schedule;
    return true;
}

size_t ffxFsr4GetDot4ScratchSize(uint32_t maxOutputWidth,
                                uint32_t maxOutputHeight)
{
    if (!maxOutputWidth || !maxOutputHeight ||
        maxOutputWidth > FFX_FSR4_DOT4_MAX_OUTPUT_WIDTH ||
        maxOutputHeight > FFX_FSR4_DOT4_MAX_OUTPUT_HEIGHT)
        return 0u;
    if (maxOutputWidth <= 1920u && maxOutputHeight <= 1080u)
        return 20880256u;
    if (maxOutputWidth <= 3840u && maxOutputHeight <= 2160u)
        return 83232256u;
    return 332352256u;
}

FfxFsr4ModelPreset ffxFsr4SelectModelPreset(uint32_t renderWidth,
                                           uint32_t outputWidth,
                                           bool dynamicResolutionModel)
{
    float ratio;

    if (dynamicResolutionModel)
        return FFX_FSR4_MODEL_PRESET_DRS;
    if (!renderWidth)
        return FFX_FSR4_MODEL_PRESET_NATIVE_AA;

    ratio = (float)outputWidth / (float)renderWidth;
    if (ratio >= 2.99f)
        return FFX_FSR4_MODEL_PRESET_ULTRA_PERFORMANCE;
    if (ratio >= 1.99f)
        return FFX_FSR4_MODEL_PRESET_PERFORMANCE;
    if (ratio >= 1.69f)
        return FFX_FSR4_MODEL_PRESET_BALANCED;
    if (ratio >= 1.50f)
        return FFX_FSR4_MODEL_PRESET_QUALITY;
    return FFX_FSR4_MODEL_PRESET_NATIVE_AA;
}

const char *ffxFsr4ModelPresetName(FfxFsr4ModelPreset preset)
{
    switch (preset) {
    case FFX_FSR4_MODEL_PRESET_NATIVE_AA:         return "native";
    case FFX_FSR4_MODEL_PRESET_QUALITY:           return "quality";
    case FFX_FSR4_MODEL_PRESET_BALANCED:          return "balanced";
    case FFX_FSR4_MODEL_PRESET_PERFORMANCE:       return "performance";
    case FFX_FSR4_MODEL_PRESET_ULTRA_PERFORMANCE: return "ultraperf";
    case FFX_FSR4_MODEL_PRESET_DRS:               return "drs";
    default:                                      return "unknown";
    }
}
