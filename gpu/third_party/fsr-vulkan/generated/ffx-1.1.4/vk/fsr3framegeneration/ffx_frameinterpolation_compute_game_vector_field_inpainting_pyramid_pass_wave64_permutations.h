#include "ffx_frameinterpolation_compute_game_vector_field_inpainting_pyramid_pass_wave64_93963115f7d4030e9a55a13119d0666c.h"

typedef union ffx_frameinterpolation_compute_game_vector_field_inpainting_pyramid_pass_wave64_PermutationKey {
    struct {
        uint32_t FFX_FRAMEINTERPOLATION_OPTION_LOW_RES_MOTION_VECTORS : 1;
        uint32_t FFX_FRAMEINTERPOLATION_OPTION_JITTER_MOTION_VECTORS : 1;
        uint32_t FFX_FRAMEINTERPOLATION_OPTION_INVERTED_DEPTH : 1;
    };
    uint32_t index;
} ffx_frameinterpolation_compute_game_vector_field_inpainting_pyramid_pass_wave64_PermutationKey;

typedef struct ffx_frameinterpolation_compute_game_vector_field_inpainting_pyramid_pass_wave64_PermutationInfo {
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
} ffx_frameinterpolation_compute_game_vector_field_inpainting_pyramid_pass_wave64_PermutationInfo;

static const uint32_t g_ffx_frameinterpolation_compute_game_vector_field_inpainting_pyramid_pass_wave64_IndirectionTable[] = {
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
};

static const ffx_frameinterpolation_compute_game_vector_field_inpainting_pyramid_pass_wave64_PermutationInfo g_ffx_frameinterpolation_compute_game_vector_field_inpainting_pyramid_pass_wave64_PermutationInfo[] = {
    { g_ffx_frameinterpolation_compute_game_vector_field_inpainting_pyramid_pass_wave64_93963115f7d4030e9a55a13119d0666c_size, g_ffx_frameinterpolation_compute_game_vector_field_inpainting_pyramid_pass_wave64_93963115f7d4030e9a55a13119d0666c_data, 2, g_ffx_frameinterpolation_compute_game_vector_field_inpainting_pyramid_pass_wave64_93963115f7d4030e9a55a13119d0666c_CBVResourceNames, g_ffx_frameinterpolation_compute_game_vector_field_inpainting_pyramid_pass_wave64_93963115f7d4030e9a55a13119d0666c_CBVResourceBindings, g_ffx_frameinterpolation_compute_game_vector_field_inpainting_pyramid_pass_wave64_93963115f7d4030e9a55a13119d0666c_CBVResourceCounts, g_ffx_frameinterpolation_compute_game_vector_field_inpainting_pyramid_pass_wave64_93963115f7d4030e9a55a13119d0666c_CBVResourceSets, 2, g_ffx_frameinterpolation_compute_game_vector_field_inpainting_pyramid_pass_wave64_93963115f7d4030e9a55a13119d0666c_TextureSRVResourceNames, g_ffx_frameinterpolation_compute_game_vector_field_inpainting_pyramid_pass_wave64_93963115f7d4030e9a55a13119d0666c_TextureSRVResourceBindings, g_ffx_frameinterpolation_compute_game_vector_field_inpainting_pyramid_pass_wave64_93963115f7d4030e9a55a13119d0666c_TextureSRVResourceCounts, g_ffx_frameinterpolation_compute_game_vector_field_inpainting_pyramid_pass_wave64_93963115f7d4030e9a55a13119d0666c_TextureSRVResourceSets, 12, g_ffx_frameinterpolation_compute_game_vector_field_inpainting_pyramid_pass_wave64_93963115f7d4030e9a55a13119d0666c_TextureUAVResourceNames, g_ffx_frameinterpolation_compute_game_vector_field_inpainting_pyramid_pass_wave64_93963115f7d4030e9a55a13119d0666c_TextureUAVResourceBindings, g_ffx_frameinterpolation_compute_game_vector_field_inpainting_pyramid_pass_wave64_93963115f7d4030e9a55a13119d0666c_TextureUAVResourceCounts, g_ffx_frameinterpolation_compute_game_vector_field_inpainting_pyramid_pass_wave64_93963115f7d4030e9a55a13119d0666c_TextureUAVResourceSets, 0, 0, 0, 0, 0, 1, g_ffx_frameinterpolation_compute_game_vector_field_inpainting_pyramid_pass_wave64_93963115f7d4030e9a55a13119d0666c_BufferUAVResourceNames, g_ffx_frameinterpolation_compute_game_vector_field_inpainting_pyramid_pass_wave64_93963115f7d4030e9a55a13119d0666c_BufferUAVResourceBindings, g_ffx_frameinterpolation_compute_game_vector_field_inpainting_pyramid_pass_wave64_93963115f7d4030e9a55a13119d0666c_BufferUAVResourceCounts, g_ffx_frameinterpolation_compute_game_vector_field_inpainting_pyramid_pass_wave64_93963115f7d4030e9a55a13119d0666c_BufferUAVResourceSets, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
};

