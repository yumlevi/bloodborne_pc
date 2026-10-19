#include "ffx_fsr3upscaler_prepare_inputs_pass_wave64_b1eded381d02f7d2915beeccab5101b4.h"
#include "ffx_fsr3upscaler_prepare_inputs_pass_wave64_09bb7aef81ffe1d6e011e313a7338a34.h"
#include "ffx_fsr3upscaler_prepare_inputs_pass_wave64_fda78162e88f582912ebf303f95c65b5.h"
#include "ffx_fsr3upscaler_prepare_inputs_pass_wave64_431a83bd66ea806fd1845836eb9a5ffb.h"
#include "ffx_fsr3upscaler_prepare_inputs_pass_wave64_6619e0b8c65786c786e04962420180d3.h"
#include "ffx_fsr3upscaler_prepare_inputs_pass_wave64_4b3222f86f441f7a643de482cbdd2957.h"
#include "ffx_fsr3upscaler_prepare_inputs_pass_wave64_72e6f3b386361a7d5461983e6938c45f.h"
#include "ffx_fsr3upscaler_prepare_inputs_pass_wave64_29b03ce903d1bf08767246ea1322dec3.h"

typedef union ffx_fsr3upscaler_prepare_inputs_pass_wave64_PermutationKey {
    struct {
        uint32_t FFX_FSR3UPSCALER_OPTION_LOW_RESOLUTION_MOTION_VECTORS : 1;
        uint32_t FFX_FSR3UPSCALER_OPTION_JITTERED_MOTION_VECTORS : 1;
        uint32_t FFX_FSR3UPSCALER_OPTION_INVERTED_DEPTH : 1;
        uint32_t FFX_FSR3UPSCALER_OPTION_REPROJECT_USE_LANCZOS_TYPE : 1;
        uint32_t FFX_FSR3UPSCALER_OPTION_HDR_COLOR_INPUT : 1;
        uint32_t FFX_FSR3UPSCALER_OPTION_APPLY_SHARPENING : 1;
    };
    uint32_t index;
} ffx_fsr3upscaler_prepare_inputs_pass_wave64_PermutationKey;

typedef struct ffx_fsr3upscaler_prepare_inputs_pass_wave64_PermutationInfo {
    const uint32_t       blobSize;
    const unsigned char* blobData;


    const uint32_t  numConstantBuffers;
    const char**    constantBufferNames;
    const uint32_t* constantBufferBindings;
    const uint32_t* constantBufferCounts;
    const uint32_t* constantBufferSpaces;

    const uint32_t  numSRVTextures;
    const char**    srvTextureNames;
    const uint32_t* srvTextureBindings;
    const uint32_t* srvTextureCounts;
    const uint32_t* srvTextureSpaces;

    const uint32_t  numUAVTextures;
    const char**    uavTextureNames;
    const uint32_t* uavTextureBindings;
    const uint32_t* uavTextureCounts;
    const uint32_t* uavTextureSpaces;

    const uint32_t  numSRVBuffers;
    const char**    srvBufferNames;
    const uint32_t* srvBufferBindings;
    const uint32_t* srvBufferCounts;
    const uint32_t* srvBufferSpaces;

    const uint32_t  numUAVBuffers;
    const char**    uavBufferNames;
    const uint32_t* uavBufferBindings;
    const uint32_t* uavBufferCounts;
    const uint32_t* uavBufferSpaces;

    const uint32_t  numSamplers;
    const char**    samplerNames;
    const uint32_t* samplerBindings;
    const uint32_t* samplerCounts;
    const uint32_t* samplerSpaces;

    const uint32_t  numRTAccelerationStructures;
    const char**    rtAccelerationStructureNames;
    const uint32_t* rtAccelerationStructureBindings;
    const uint32_t* rtAccelerationStructureCounts;
    const uint32_t* rtAccelerationStructureSpaces;
} ffx_fsr3upscaler_prepare_inputs_pass_wave64_PermutationInfo;

static const uint32_t g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_IndirectionTable[] = {
    7,
    3,
    5,
    1,
    6,
    2,
    4,
    0,
    7,
    3,
    5,
    1,
    6,
    2,
    4,
    0,
    7,
    3,
    5,
    1,
    6,
    2,
    4,
    0,
    7,
    3,
    5,
    1,
    6,
    2,
    4,
    0,
    7,
    3,
    5,
    1,
    6,
    2,
    4,
    0,
    7,
    3,
    5,
    1,
    6,
    2,
    4,
    0,
    7,
    3,
    5,
    1,
    6,
    2,
    4,
    0,
    7,
    3,
    5,
    1,
    6,
    2,
    4,
    0,
};

static const ffx_fsr3upscaler_prepare_inputs_pass_wave64_PermutationInfo g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_PermutationInfo[] = {
    { g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_b1eded381d02f7d2915beeccab5101b4_size, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_b1eded381d02f7d2915beeccab5101b4_data, 1, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_b1eded381d02f7d2915beeccab5101b4_CBVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_b1eded381d02f7d2915beeccab5101b4_CBVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_b1eded381d02f7d2915beeccab5101b4_CBVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_b1eded381d02f7d2915beeccab5101b4_CBVResourceSets, 3, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_b1eded381d02f7d2915beeccab5101b4_TextureSRVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_b1eded381d02f7d2915beeccab5101b4_TextureSRVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_b1eded381d02f7d2915beeccab5101b4_TextureSRVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_b1eded381d02f7d2915beeccab5101b4_TextureSRVResourceSets, 5, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_b1eded381d02f7d2915beeccab5101b4_TextureUAVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_b1eded381d02f7d2915beeccab5101b4_TextureUAVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_b1eded381d02f7d2915beeccab5101b4_TextureUAVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_b1eded381d02f7d2915beeccab5101b4_TextureUAVResourceSets, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
    { g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_09bb7aef81ffe1d6e011e313a7338a34_size, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_09bb7aef81ffe1d6e011e313a7338a34_data, 1, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_09bb7aef81ffe1d6e011e313a7338a34_CBVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_09bb7aef81ffe1d6e011e313a7338a34_CBVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_09bb7aef81ffe1d6e011e313a7338a34_CBVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_09bb7aef81ffe1d6e011e313a7338a34_CBVResourceSets, 3, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_09bb7aef81ffe1d6e011e313a7338a34_TextureSRVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_09bb7aef81ffe1d6e011e313a7338a34_TextureSRVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_09bb7aef81ffe1d6e011e313a7338a34_TextureSRVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_09bb7aef81ffe1d6e011e313a7338a34_TextureSRVResourceSets, 5, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_09bb7aef81ffe1d6e011e313a7338a34_TextureUAVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_09bb7aef81ffe1d6e011e313a7338a34_TextureUAVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_09bb7aef81ffe1d6e011e313a7338a34_TextureUAVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_09bb7aef81ffe1d6e011e313a7338a34_TextureUAVResourceSets, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
    { g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_fda78162e88f582912ebf303f95c65b5_size, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_fda78162e88f582912ebf303f95c65b5_data, 1, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_fda78162e88f582912ebf303f95c65b5_CBVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_fda78162e88f582912ebf303f95c65b5_CBVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_fda78162e88f582912ebf303f95c65b5_CBVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_fda78162e88f582912ebf303f95c65b5_CBVResourceSets, 3, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_fda78162e88f582912ebf303f95c65b5_TextureSRVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_fda78162e88f582912ebf303f95c65b5_TextureSRVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_fda78162e88f582912ebf303f95c65b5_TextureSRVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_fda78162e88f582912ebf303f95c65b5_TextureSRVResourceSets, 5, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_fda78162e88f582912ebf303f95c65b5_TextureUAVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_fda78162e88f582912ebf303f95c65b5_TextureUAVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_fda78162e88f582912ebf303f95c65b5_TextureUAVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_fda78162e88f582912ebf303f95c65b5_TextureUAVResourceSets, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
    { g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_431a83bd66ea806fd1845836eb9a5ffb_size, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_431a83bd66ea806fd1845836eb9a5ffb_data, 1, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_431a83bd66ea806fd1845836eb9a5ffb_CBVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_431a83bd66ea806fd1845836eb9a5ffb_CBVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_431a83bd66ea806fd1845836eb9a5ffb_CBVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_431a83bd66ea806fd1845836eb9a5ffb_CBVResourceSets, 3, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_431a83bd66ea806fd1845836eb9a5ffb_TextureSRVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_431a83bd66ea806fd1845836eb9a5ffb_TextureSRVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_431a83bd66ea806fd1845836eb9a5ffb_TextureSRVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_431a83bd66ea806fd1845836eb9a5ffb_TextureSRVResourceSets, 5, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_431a83bd66ea806fd1845836eb9a5ffb_TextureUAVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_431a83bd66ea806fd1845836eb9a5ffb_TextureUAVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_431a83bd66ea806fd1845836eb9a5ffb_TextureUAVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_431a83bd66ea806fd1845836eb9a5ffb_TextureUAVResourceSets, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
    { g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_6619e0b8c65786c786e04962420180d3_size, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_6619e0b8c65786c786e04962420180d3_data, 1, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_6619e0b8c65786c786e04962420180d3_CBVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_6619e0b8c65786c786e04962420180d3_CBVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_6619e0b8c65786c786e04962420180d3_CBVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_6619e0b8c65786c786e04962420180d3_CBVResourceSets, 3, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_6619e0b8c65786c786e04962420180d3_TextureSRVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_6619e0b8c65786c786e04962420180d3_TextureSRVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_6619e0b8c65786c786e04962420180d3_TextureSRVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_6619e0b8c65786c786e04962420180d3_TextureSRVResourceSets, 5, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_6619e0b8c65786c786e04962420180d3_TextureUAVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_6619e0b8c65786c786e04962420180d3_TextureUAVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_6619e0b8c65786c786e04962420180d3_TextureUAVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_6619e0b8c65786c786e04962420180d3_TextureUAVResourceSets, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
    { g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_4b3222f86f441f7a643de482cbdd2957_size, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_4b3222f86f441f7a643de482cbdd2957_data, 1, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_4b3222f86f441f7a643de482cbdd2957_CBVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_4b3222f86f441f7a643de482cbdd2957_CBVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_4b3222f86f441f7a643de482cbdd2957_CBVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_4b3222f86f441f7a643de482cbdd2957_CBVResourceSets, 3, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_4b3222f86f441f7a643de482cbdd2957_TextureSRVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_4b3222f86f441f7a643de482cbdd2957_TextureSRVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_4b3222f86f441f7a643de482cbdd2957_TextureSRVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_4b3222f86f441f7a643de482cbdd2957_TextureSRVResourceSets, 5, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_4b3222f86f441f7a643de482cbdd2957_TextureUAVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_4b3222f86f441f7a643de482cbdd2957_TextureUAVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_4b3222f86f441f7a643de482cbdd2957_TextureUAVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_4b3222f86f441f7a643de482cbdd2957_TextureUAVResourceSets, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
    { g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_72e6f3b386361a7d5461983e6938c45f_size, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_72e6f3b386361a7d5461983e6938c45f_data, 1, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_72e6f3b386361a7d5461983e6938c45f_CBVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_72e6f3b386361a7d5461983e6938c45f_CBVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_72e6f3b386361a7d5461983e6938c45f_CBVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_72e6f3b386361a7d5461983e6938c45f_CBVResourceSets, 3, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_72e6f3b386361a7d5461983e6938c45f_TextureSRVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_72e6f3b386361a7d5461983e6938c45f_TextureSRVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_72e6f3b386361a7d5461983e6938c45f_TextureSRVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_72e6f3b386361a7d5461983e6938c45f_TextureSRVResourceSets, 5, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_72e6f3b386361a7d5461983e6938c45f_TextureUAVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_72e6f3b386361a7d5461983e6938c45f_TextureUAVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_72e6f3b386361a7d5461983e6938c45f_TextureUAVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_72e6f3b386361a7d5461983e6938c45f_TextureUAVResourceSets, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
    { g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_29b03ce903d1bf08767246ea1322dec3_size, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_29b03ce903d1bf08767246ea1322dec3_data, 1, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_29b03ce903d1bf08767246ea1322dec3_CBVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_29b03ce903d1bf08767246ea1322dec3_CBVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_29b03ce903d1bf08767246ea1322dec3_CBVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_29b03ce903d1bf08767246ea1322dec3_CBVResourceSets, 3, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_29b03ce903d1bf08767246ea1322dec3_TextureSRVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_29b03ce903d1bf08767246ea1322dec3_TextureSRVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_29b03ce903d1bf08767246ea1322dec3_TextureSRVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_29b03ce903d1bf08767246ea1322dec3_TextureSRVResourceSets, 5, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_29b03ce903d1bf08767246ea1322dec3_TextureUAVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_29b03ce903d1bf08767246ea1322dec3_TextureUAVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_29b03ce903d1bf08767246ea1322dec3_TextureUAVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_29b03ce903d1bf08767246ea1322dec3_TextureUAVResourceSets, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
};

