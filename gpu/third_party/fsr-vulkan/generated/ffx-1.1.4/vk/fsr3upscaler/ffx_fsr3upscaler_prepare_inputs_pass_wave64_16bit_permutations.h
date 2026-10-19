#include "ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_62846bae7690833cc2835fdf32556678.h"
#include "ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_02e6f590c3210a29d9d685615dabebf5.h"
#include "ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_f9c0aa138b63f5b9bcd3390fd16578d7.h"
#include "ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_0ee4c0c08fb6c8f74a903195c5d07e16.h"
#include "ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_d448e7603ebb95610708639fdaa4087c.h"
#include "ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_58eca904ac7a11a6bf76664a2d335388.h"
#include "ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_e180efcc15bb19553affd5c4d5f9a9ea.h"
#include "ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_41a9171d8d459fc08a6618f9784fa127.h"

typedef union ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_PermutationKey {
    struct {
        uint32_t FFX_FSR3UPSCALER_OPTION_LOW_RESOLUTION_MOTION_VECTORS : 1;
        uint32_t FFX_FSR3UPSCALER_OPTION_JITTERED_MOTION_VECTORS : 1;
        uint32_t FFX_FSR3UPSCALER_OPTION_INVERTED_DEPTH : 1;
        uint32_t FFX_FSR3UPSCALER_OPTION_REPROJECT_USE_LANCZOS_TYPE : 1;
        uint32_t FFX_FSR3UPSCALER_OPTION_HDR_COLOR_INPUT : 1;
        uint32_t FFX_FSR3UPSCALER_OPTION_APPLY_SHARPENING : 1;
    };
    uint32_t index;
} ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_PermutationKey;

typedef struct ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_PermutationInfo {
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
} ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_PermutationInfo;

static const uint32_t g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_IndirectionTable[] = {
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

static const ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_PermutationInfo g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_PermutationInfo[] = {
    { g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_62846bae7690833cc2835fdf32556678_size, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_62846bae7690833cc2835fdf32556678_data, 1, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_62846bae7690833cc2835fdf32556678_CBVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_62846bae7690833cc2835fdf32556678_CBVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_62846bae7690833cc2835fdf32556678_CBVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_62846bae7690833cc2835fdf32556678_CBVResourceSets, 3, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_62846bae7690833cc2835fdf32556678_TextureSRVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_62846bae7690833cc2835fdf32556678_TextureSRVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_62846bae7690833cc2835fdf32556678_TextureSRVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_62846bae7690833cc2835fdf32556678_TextureSRVResourceSets, 5, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_62846bae7690833cc2835fdf32556678_TextureUAVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_62846bae7690833cc2835fdf32556678_TextureUAVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_62846bae7690833cc2835fdf32556678_TextureUAVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_62846bae7690833cc2835fdf32556678_TextureUAVResourceSets, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
    { g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_02e6f590c3210a29d9d685615dabebf5_size, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_02e6f590c3210a29d9d685615dabebf5_data, 1, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_02e6f590c3210a29d9d685615dabebf5_CBVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_02e6f590c3210a29d9d685615dabebf5_CBVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_02e6f590c3210a29d9d685615dabebf5_CBVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_02e6f590c3210a29d9d685615dabebf5_CBVResourceSets, 3, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_02e6f590c3210a29d9d685615dabebf5_TextureSRVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_02e6f590c3210a29d9d685615dabebf5_TextureSRVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_02e6f590c3210a29d9d685615dabebf5_TextureSRVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_02e6f590c3210a29d9d685615dabebf5_TextureSRVResourceSets, 5, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_02e6f590c3210a29d9d685615dabebf5_TextureUAVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_02e6f590c3210a29d9d685615dabebf5_TextureUAVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_02e6f590c3210a29d9d685615dabebf5_TextureUAVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_02e6f590c3210a29d9d685615dabebf5_TextureUAVResourceSets, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
    { g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_f9c0aa138b63f5b9bcd3390fd16578d7_size, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_f9c0aa138b63f5b9bcd3390fd16578d7_data, 1, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_f9c0aa138b63f5b9bcd3390fd16578d7_CBVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_f9c0aa138b63f5b9bcd3390fd16578d7_CBVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_f9c0aa138b63f5b9bcd3390fd16578d7_CBVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_f9c0aa138b63f5b9bcd3390fd16578d7_CBVResourceSets, 3, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_f9c0aa138b63f5b9bcd3390fd16578d7_TextureSRVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_f9c0aa138b63f5b9bcd3390fd16578d7_TextureSRVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_f9c0aa138b63f5b9bcd3390fd16578d7_TextureSRVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_f9c0aa138b63f5b9bcd3390fd16578d7_TextureSRVResourceSets, 5, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_f9c0aa138b63f5b9bcd3390fd16578d7_TextureUAVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_f9c0aa138b63f5b9bcd3390fd16578d7_TextureUAVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_f9c0aa138b63f5b9bcd3390fd16578d7_TextureUAVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_f9c0aa138b63f5b9bcd3390fd16578d7_TextureUAVResourceSets, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
    { g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_0ee4c0c08fb6c8f74a903195c5d07e16_size, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_0ee4c0c08fb6c8f74a903195c5d07e16_data, 1, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_0ee4c0c08fb6c8f74a903195c5d07e16_CBVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_0ee4c0c08fb6c8f74a903195c5d07e16_CBVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_0ee4c0c08fb6c8f74a903195c5d07e16_CBVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_0ee4c0c08fb6c8f74a903195c5d07e16_CBVResourceSets, 3, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_0ee4c0c08fb6c8f74a903195c5d07e16_TextureSRVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_0ee4c0c08fb6c8f74a903195c5d07e16_TextureSRVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_0ee4c0c08fb6c8f74a903195c5d07e16_TextureSRVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_0ee4c0c08fb6c8f74a903195c5d07e16_TextureSRVResourceSets, 5, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_0ee4c0c08fb6c8f74a903195c5d07e16_TextureUAVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_0ee4c0c08fb6c8f74a903195c5d07e16_TextureUAVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_0ee4c0c08fb6c8f74a903195c5d07e16_TextureUAVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_0ee4c0c08fb6c8f74a903195c5d07e16_TextureUAVResourceSets, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
    { g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_d448e7603ebb95610708639fdaa4087c_size, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_d448e7603ebb95610708639fdaa4087c_data, 1, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_d448e7603ebb95610708639fdaa4087c_CBVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_d448e7603ebb95610708639fdaa4087c_CBVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_d448e7603ebb95610708639fdaa4087c_CBVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_d448e7603ebb95610708639fdaa4087c_CBVResourceSets, 3, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_d448e7603ebb95610708639fdaa4087c_TextureSRVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_d448e7603ebb95610708639fdaa4087c_TextureSRVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_d448e7603ebb95610708639fdaa4087c_TextureSRVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_d448e7603ebb95610708639fdaa4087c_TextureSRVResourceSets, 5, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_d448e7603ebb95610708639fdaa4087c_TextureUAVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_d448e7603ebb95610708639fdaa4087c_TextureUAVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_d448e7603ebb95610708639fdaa4087c_TextureUAVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_d448e7603ebb95610708639fdaa4087c_TextureUAVResourceSets, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
    { g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_58eca904ac7a11a6bf76664a2d335388_size, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_58eca904ac7a11a6bf76664a2d335388_data, 1, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_58eca904ac7a11a6bf76664a2d335388_CBVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_58eca904ac7a11a6bf76664a2d335388_CBVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_58eca904ac7a11a6bf76664a2d335388_CBVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_58eca904ac7a11a6bf76664a2d335388_CBVResourceSets, 3, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_58eca904ac7a11a6bf76664a2d335388_TextureSRVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_58eca904ac7a11a6bf76664a2d335388_TextureSRVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_58eca904ac7a11a6bf76664a2d335388_TextureSRVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_58eca904ac7a11a6bf76664a2d335388_TextureSRVResourceSets, 5, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_58eca904ac7a11a6bf76664a2d335388_TextureUAVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_58eca904ac7a11a6bf76664a2d335388_TextureUAVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_58eca904ac7a11a6bf76664a2d335388_TextureUAVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_58eca904ac7a11a6bf76664a2d335388_TextureUAVResourceSets, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
    { g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_e180efcc15bb19553affd5c4d5f9a9ea_size, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_e180efcc15bb19553affd5c4d5f9a9ea_data, 1, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_e180efcc15bb19553affd5c4d5f9a9ea_CBVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_e180efcc15bb19553affd5c4d5f9a9ea_CBVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_e180efcc15bb19553affd5c4d5f9a9ea_CBVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_e180efcc15bb19553affd5c4d5f9a9ea_CBVResourceSets, 3, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_e180efcc15bb19553affd5c4d5f9a9ea_TextureSRVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_e180efcc15bb19553affd5c4d5f9a9ea_TextureSRVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_e180efcc15bb19553affd5c4d5f9a9ea_TextureSRVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_e180efcc15bb19553affd5c4d5f9a9ea_TextureSRVResourceSets, 5, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_e180efcc15bb19553affd5c4d5f9a9ea_TextureUAVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_e180efcc15bb19553affd5c4d5f9a9ea_TextureUAVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_e180efcc15bb19553affd5c4d5f9a9ea_TextureUAVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_e180efcc15bb19553affd5c4d5f9a9ea_TextureUAVResourceSets, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
    { g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_41a9171d8d459fc08a6618f9784fa127_size, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_41a9171d8d459fc08a6618f9784fa127_data, 1, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_41a9171d8d459fc08a6618f9784fa127_CBVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_41a9171d8d459fc08a6618f9784fa127_CBVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_41a9171d8d459fc08a6618f9784fa127_CBVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_41a9171d8d459fc08a6618f9784fa127_CBVResourceSets, 3, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_41a9171d8d459fc08a6618f9784fa127_TextureSRVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_41a9171d8d459fc08a6618f9784fa127_TextureSRVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_41a9171d8d459fc08a6618f9784fa127_TextureSRVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_41a9171d8d459fc08a6618f9784fa127_TextureSRVResourceSets, 5, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_41a9171d8d459fc08a6618f9784fa127_TextureUAVResourceNames, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_41a9171d8d459fc08a6618f9784fa127_TextureUAVResourceBindings, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_41a9171d8d459fc08a6618f9784fa127_TextureUAVResourceCounts, g_ffx_fsr3upscaler_prepare_inputs_pass_wave64_16bit_41a9171d8d459fc08a6618f9784fa127_TextureUAVResourceSets, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
};

