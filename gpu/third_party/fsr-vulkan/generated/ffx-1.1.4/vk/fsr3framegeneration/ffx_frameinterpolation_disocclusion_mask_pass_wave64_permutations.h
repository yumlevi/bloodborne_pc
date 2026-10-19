#include "ffx_frameinterpolation_disocclusion_mask_pass_wave64_5c9b2cb9699516e6a4024e5ebde4b678.h"
#include "ffx_frameinterpolation_disocclusion_mask_pass_wave64_ec9c17fbe20ec1a71ad084380b4b2f35.h"

typedef union ffx_frameinterpolation_disocclusion_mask_pass_wave64_PermutationKey {
    struct {
        uint32_t FFX_FRAMEINTERPOLATION_OPTION_LOW_RES_MOTION_VECTORS : 1;
        uint32_t FFX_FRAMEINTERPOLATION_OPTION_JITTER_MOTION_VECTORS : 1;
        uint32_t FFX_FRAMEINTERPOLATION_OPTION_INVERTED_DEPTH : 1;
    };
    uint32_t index;
} ffx_frameinterpolation_disocclusion_mask_pass_wave64_PermutationKey;

typedef struct ffx_frameinterpolation_disocclusion_mask_pass_wave64_PermutationInfo {
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
} ffx_frameinterpolation_disocclusion_mask_pass_wave64_PermutationInfo;

static const uint32_t g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_IndirectionTable[] = {
    1,
    1,
    1,
    1,
    0,
    0,
    0,
    0,
};

static const ffx_frameinterpolation_disocclusion_mask_pass_wave64_PermutationInfo g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_PermutationInfo[] = {
    { g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_5c9b2cb9699516e6a4024e5ebde4b678_size, g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_5c9b2cb9699516e6a4024e5ebde4b678_data, 1, g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_5c9b2cb9699516e6a4024e5ebde4b678_CBVResourceNames, g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_5c9b2cb9699516e6a4024e5ebde4b678_CBVResourceBindings, g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_5c9b2cb9699516e6a4024e5ebde4b678_CBVResourceCounts, g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_5c9b2cb9699516e6a4024e5ebde4b678_CBVResourceSets, 7, g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_5c9b2cb9699516e6a4024e5ebde4b678_TextureSRVResourceNames, g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_5c9b2cb9699516e6a4024e5ebde4b678_TextureSRVResourceBindings, g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_5c9b2cb9699516e6a4024e5ebde4b678_TextureSRVResourceCounts, g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_5c9b2cb9699516e6a4024e5ebde4b678_TextureSRVResourceSets, 1, g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_5c9b2cb9699516e6a4024e5ebde4b678_TextureUAVResourceNames, g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_5c9b2cb9699516e6a4024e5ebde4b678_TextureUAVResourceBindings, g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_5c9b2cb9699516e6a4024e5ebde4b678_TextureUAVResourceCounts, g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_5c9b2cb9699516e6a4024e5ebde4b678_TextureUAVResourceSets, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_5c9b2cb9699516e6a4024e5ebde4b678_SamplerResourceNames, g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_5c9b2cb9699516e6a4024e5ebde4b678_SamplerResourceBindings, g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_5c9b2cb9699516e6a4024e5ebde4b678_SamplerResourceCounts, g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_5c9b2cb9699516e6a4024e5ebde4b678_SamplerResourceSets, 0, 0, 0, 0, 0, },
    { g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_ec9c17fbe20ec1a71ad084380b4b2f35_size, g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_ec9c17fbe20ec1a71ad084380b4b2f35_data, 1, g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_ec9c17fbe20ec1a71ad084380b4b2f35_CBVResourceNames, g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_ec9c17fbe20ec1a71ad084380b4b2f35_CBVResourceBindings, g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_ec9c17fbe20ec1a71ad084380b4b2f35_CBVResourceCounts, g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_ec9c17fbe20ec1a71ad084380b4b2f35_CBVResourceSets, 7, g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_ec9c17fbe20ec1a71ad084380b4b2f35_TextureSRVResourceNames, g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_ec9c17fbe20ec1a71ad084380b4b2f35_TextureSRVResourceBindings, g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_ec9c17fbe20ec1a71ad084380b4b2f35_TextureSRVResourceCounts, g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_ec9c17fbe20ec1a71ad084380b4b2f35_TextureSRVResourceSets, 1, g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_ec9c17fbe20ec1a71ad084380b4b2f35_TextureUAVResourceNames, g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_ec9c17fbe20ec1a71ad084380b4b2f35_TextureUAVResourceBindings, g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_ec9c17fbe20ec1a71ad084380b4b2f35_TextureUAVResourceCounts, g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_ec9c17fbe20ec1a71ad084380b4b2f35_TextureUAVResourceSets, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_ec9c17fbe20ec1a71ad084380b4b2f35_SamplerResourceNames, g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_ec9c17fbe20ec1a71ad084380b4b2f35_SamplerResourceBindings, g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_ec9c17fbe20ec1a71ad084380b4b2f35_SamplerResourceCounts, g_ffx_frameinterpolation_disocclusion_mask_pass_wave64_ec9c17fbe20ec1a71ad084380b4b2f35_SamplerResourceSets, 0, 0, 0, 0, 0, },
};

