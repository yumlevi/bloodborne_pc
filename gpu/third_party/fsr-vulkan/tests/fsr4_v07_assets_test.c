#include "ffx_vk_fsr4_v07_schedule.h"
#include "ffx_vk_fsr4_v07_assets.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(expression)                                                        \
    do {                                                                         \
        if (!(expression)) {                                                     \
            fprintf(stderr, "%s:%d: check failed: %s\n",                       \
                    __FILE__, __LINE__, #expression);                            \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static void check_dispatch(FfxFsr4DispatchSize actual, uint32_t x, uint32_t y)
{
    CHECK(actual.x == x);
    CHECK(actual.y == y);
    CHECK(actual.z == 1u);
}

static void test_1440p_schedule(void)
{
    FfxFsr4Dot4Schedule schedule;

    CHECK(ffxFsr4BuildDot4Schedule(2560u, 1440u, &schedule));
    CHECK(schedule.alignedWidth == 2560u);
    CHECK(schedule.alignedHeight == 1440u);
    check_dispatch(schedule.pre, 160u, 90u);
    check_dispatch(schedule.model[0], 20u, 720u);
    check_dispatch(schedule.model[1], 20u, 720u);
    check_dispatch(schedule.model[2], 10u, 360u);
    check_dispatch(schedule.model[3], 10u, 360u);
    check_dispatch(schedule.model[4], 10u, 360u);
    check_dispatch(schedule.model[5], 5u, 180u);
    check_dispatch(schedule.model[6], 5u, 180u);
    check_dispatch(schedule.model[7], 5u, 180u);
    check_dispatch(schedule.model[8], 40u, 23u);
    check_dispatch(schedule.model[9], 10u, 360u);
    check_dispatch(schedule.model[10], 10u, 360u);
    check_dispatch(schedule.model[11], 20u, 720u);
    check_dispatch(schedule.post, 160u, 90u);
    check_dispatch(schedule.rcas, 160u, 90u);
}

static void test_alignment_and_validation(void)
{
    FfxFsr4Dot4Schedule schedule;

    memset(&schedule, 0xcd, sizeof(schedule));
    CHECK(ffxFsr4BuildDot4Schedule(1919u, 1079u, &schedule));
    CHECK(schedule.alignedWidth == 1920u);
    CHECK(schedule.alignedHeight == 1080u);
    check_dispatch(schedule.pre, 120u, 68u);

    CHECK(!ffxFsr4BuildDot4Schedule(0u, 1080u, &schedule));
    CHECK(!ffxFsr4BuildDot4Schedule(1920u, 0u, &schedule));
    CHECK(ffxFsr4BuildDot4Schedule(7680u, 4320u, &schedule));
    CHECK(!ffxFsr4BuildDot4Schedule(7681u, 4320u, &schedule));
    CHECK(!ffxFsr4BuildDot4Schedule(7680u, 4321u, &schedule));
    CHECK(!ffxFsr4BuildDot4Schedule(UINT32_MAX, 1080u, &schedule));
    CHECK(!ffxFsr4BuildDot4Schedule(1920u, 1080u, NULL));
}

static void test_scratch_sizes(void)
{
    CHECK(ffxFsr4GetDot4ScratchSize(1920u, 1080u) == 20880256u);
    CHECK(ffxFsr4GetDot4ScratchSize(1921u, 1080u) == 83232256u);
    CHECK(ffxFsr4GetDot4ScratchSize(3840u, 2160u) == 83232256u);
    CHECK(ffxFsr4GetDot4ScratchSize(3841u, 2160u) == 332352256u);
    CHECK(ffxFsr4GetDot4ScratchSize(7680u, 4320u) == 332352256u);
    CHECK(ffxFsr4GetDot4ScratchSize(7681u, 4320u) == 0u);
    CHECK(ffxFsr4GetDot4ScratchSize(7680u, 4321u) == 0u);
}

static void test_spd_schedule(void)
{
    FfxFsr4SpdSchedule schedule;

    CHECK(ffxFsr4BuildSpdSchedule(640u, 360u, &schedule));
    check_dispatch(schedule.dispatch, 10u, 6u);
    CHECK(schedule.workgroupCount == 60u);
    CHECK(schedule.mipCount == 9u);

    CHECK(ffxFsr4BuildSpdSchedule(2560u, 1440u, &schedule));
    check_dispatch(schedule.dispatch, 40u, 23u);
    CHECK(schedule.workgroupCount == 920u);
    CHECK(schedule.mipCount == 11u);

    CHECK(ffxFsr4BuildSpdSchedule(1u, 1u, &schedule));
    check_dispatch(schedule.dispatch, 1u, 1u);
    CHECK(schedule.workgroupCount == 1u);
    CHECK(schedule.mipCount == 0u);

    CHECK(ffxFsr4BuildSpdSchedule(65536u, 1u, &schedule));
    check_dispatch(schedule.dispatch, 1024u, 1u);
    CHECK(schedule.workgroupCount == 1024u);
    CHECK(schedule.mipCount == 12u);

    CHECK(!ffxFsr4BuildSpdSchedule(0u, 1u, &schedule));
    CHECK(!ffxFsr4BuildSpdSchedule(1u, 0u, &schedule));
    CHECK(!ffxFsr4BuildSpdSchedule(UINT32_MAX, UINT32_MAX, &schedule));
    CHECK(!ffxFsr4BuildSpdSchedule(1u, 1u, NULL));
}

static void test_presets(void)
{
    CHECK(ffxFsr4SelectModelPreset(1920u, 1920u, false) ==
          FFX_FSR4_MODEL_PRESET_NATIVE_AA);
    CHECK(ffxFsr4SelectModelPreset(1280u, 1920u, false) ==
          FFX_FSR4_MODEL_PRESET_QUALITY);
    CHECK(ffxFsr4SelectModelPreset(1129u, 1920u, false) ==
          FFX_FSR4_MODEL_PRESET_BALANCED);
    CHECK(ffxFsr4SelectModelPreset(960u, 1920u, false) ==
          FFX_FSR4_MODEL_PRESET_PERFORMANCE);
    CHECK(ffxFsr4SelectModelPreset(640u, 1920u, false) ==
          FFX_FSR4_MODEL_PRESET_ULTRA_PERFORMANCE);
    CHECK(ffxFsr4SelectModelPreset(960u, 1920u, true) ==
          FFX_FSR4_MODEL_PRESET_DRS);
    CHECK(strcmp(ffxFsr4ModelPresetName(FFX_FSR4_MODEL_PRESET_PERFORMANCE),
                 "performance") == 0);
}

static void test_asset_sets(void)
{
    FfxFsr4V07AssetSet assets;

    CHECK(!ffxFsr4V07BuildAssetSet(FFX_FSR4_MODEL_PRESET_PERFORMANCE,
                                    0u, 1440u, &assets));
    CHECK(!ffxFsr4V07BuildAssetSet(FFX_FSR4_MODEL_PRESET_PERFORMANCE,
                                    7681u, 1440u, &assets));
    CHECK(ffxFsr4V07BuildAssetSet(FFX_FSR4_MODEL_PRESET_QUALITY,
                                   2560u, 1440u, &assets));
    CHECK(!strcmp(assets.tier, "2160"));
    CHECK(!strcmp(assets.pre, "fsr4_model_v07_i8_quality_2160_pre.spv"));
    CHECK(!strcmp(assets.model[0],
                  "fsr4_model_v07_i8_quality_2160_pass1.spv"));
    CHECK(!strcmp(assets.model[11],
                  "fsr4_model_v07_i8_quality_2160_pass12.spv"));
    CHECK(!strcmp(assets.initializer,
                  "fsr4_model_v07_i8_quality_initializers.bin"));
    CHECK(!strcmp(assets.prePassWeights,
                  "fsr4_model_v07_i8_quality_pre_weights.bin"));
    CHECK(ffxFsr4V07BuildAssetSet(FFX_FSR4_MODEL_PRESET_DRS,
                                   1280u, 720u, &assets));
    CHECK(!strcmp(assets.tier, "1080"));
    CHECK(!strcmp(assets.pre, "fsr4_model_v07_i8_drs_1080_pre.spv"));
    CHECK(!strcmp(assets.rcas, "rcas.spv"));
    CHECK(!strcmp(assets.spdAutoExposure, "spd_auto_exposure.spv"));
    CHECK(FFX_FSR4_V07_INITIALIZER_BYTES == 89216u);
    CHECK(FFX_FSR4_V07_PRE_PASS_WEIGHTS_BYTES == 1024u);
}

int main(void)
{
    test_1440p_schedule();
    test_alignment_and_validation();
    test_scratch_sizes();
    test_spd_schedule();
    test_presets();
    test_asset_sets();

    if (failures)
        fprintf(stderr, "%d FSR4 schedule check(s) failed\n", failures);
    return failures ? 1 : 0;
}
