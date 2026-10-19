#include "ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_917adb7414eb68311fcbc8663f69526f.h"
#include "ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_cdb4db664096c12878e6126d9f1d37a5.h"

typedef union ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_PermutationKey {
    struct {
        uint32_t FFX_FRAMEINTERPOLATION_OPTION_LOW_RES_MOTION_VECTORS : 1;
        uint32_t FFX_FRAMEINTERPOLATION_OPTION_JITTER_MOTION_VECTORS : 1;
        uint32_t FFX_FRAMEINTERPOLATION_OPTION_INVERTED_DEPTH : 1;
    };
    uint32_t index;
} ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_PermutationKey;

typedef struct ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_PermutationInfo {
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
} ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_PermutationInfo;

static const uint32_t g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_IndirectionTable[] = {
    1,
    1,
    1,
    1,
    0,
    0,
    0,
    0,
};

static const ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_PermutationInfo g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_PermutationInfo[] = {
    { g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_917adb7414eb68311fcbc8663f69526f_size, g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_917adb7414eb68311fcbc8663f69526f_data, 1, g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_917adb7414eb68311fcbc8663f69526f_CBVResourceNames, g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_917adb7414eb68311fcbc8663f69526f_CBVResourceBindings, g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_917adb7414eb68311fcbc8663f69526f_CBVResourceCounts, g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_917adb7414eb68311fcbc8663f69526f_CBVResourceSets, 3, g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_917adb7414eb68311fcbc8663f69526f_TextureSRVResourceNames, g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_917adb7414eb68311fcbc8663f69526f_TextureSRVResourceBindings, g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_917adb7414eb68311fcbc8663f69526f_TextureSRVResourceCounts, g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_917adb7414eb68311fcbc8663f69526f_TextureSRVResourceSets, 1, g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_917adb7414eb68311fcbc8663f69526f_TextureUAVResourceNames, g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_917adb7414eb68311fcbc8663f69526f_TextureUAVResourceBindings, g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_917adb7414eb68311fcbc8663f69526f_TextureUAVResourceCounts, g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_917adb7414eb68311fcbc8663f69526f_TextureUAVResourceSets, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_917adb7414eb68311fcbc8663f69526f_SamplerResourceNames, g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_917adb7414eb68311fcbc8663f69526f_SamplerResourceBindings, g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_917adb7414eb68311fcbc8663f69526f_SamplerResourceCounts, g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_917adb7414eb68311fcbc8663f69526f_SamplerResourceSets, 0, 0, 0, 0, 0, },
    { g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_cdb4db664096c12878e6126d9f1d37a5_size, g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_cdb4db664096c12878e6126d9f1d37a5_data, 1, g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_cdb4db664096c12878e6126d9f1d37a5_CBVResourceNames, g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_cdb4db664096c12878e6126d9f1d37a5_CBVResourceBindings, g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_cdb4db664096c12878e6126d9f1d37a5_CBVResourceCounts, g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_cdb4db664096c12878e6126d9f1d37a5_CBVResourceSets, 3, g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_cdb4db664096c12878e6126d9f1d37a5_TextureSRVResourceNames, g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_cdb4db664096c12878e6126d9f1d37a5_TextureSRVResourceBindings, g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_cdb4db664096c12878e6126d9f1d37a5_TextureSRVResourceCounts, g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_cdb4db664096c12878e6126d9f1d37a5_TextureSRVResourceSets, 1, g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_cdb4db664096c12878e6126d9f1d37a5_TextureUAVResourceNames, g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_cdb4db664096c12878e6126d9f1d37a5_TextureUAVResourceBindings, g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_cdb4db664096c12878e6126d9f1d37a5_TextureUAVResourceCounts, g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_cdb4db664096c12878e6126d9f1d37a5_TextureUAVResourceSets, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_cdb4db664096c12878e6126d9f1d37a5_SamplerResourceNames, g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_cdb4db664096c12878e6126d9f1d37a5_SamplerResourceBindings, g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_cdb4db664096c12878e6126d9f1d37a5_SamplerResourceCounts, g_ffx_frameinterpolation_reconstruct_previous_depth_pass_16bit_cdb4db664096c12878e6126d9f1d37a5_SamplerResourceSets, 0, 0, 0, 0, 0, },
};

