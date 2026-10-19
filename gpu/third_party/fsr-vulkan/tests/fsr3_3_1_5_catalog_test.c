/* Copyright (c) 2026 Q2RTX FSR Vulkan contributors SPDX-License-Identifier: MIT */
#include "ffx_vk_fsr3_3_1_5_catalog.h"
#include <string.h>
int main(void) {
 FfxVkFsr3_3_1_5Module m; const uint32_t base=7u;
 if (ffxVkFsr3_3_1_5GetModule(0,base,&m)||strcmp(m.filename,"fsr3_3_1_5_prepare_inputs.spv")) return 1;
 if (ffxVkFsr3_3_1_5GetModule(7,base|32u,&m)||strcmp(m.filename,"fsr3_3_1_5_accumulate_sharpen.spv")) return 1;
 if (ffxVkFsr3_3_1_5GetModule(7,base,&m)!=FFX_VK_PORTABLE_ERROR_UNSUPPORTED) return 1;
 if (ffxVkFsr3_3_1_5GetModule(11,base,&m)!=FFX_VK_PORTABLE_ERROR_UNSUPPORTED) return 1;
 return 0;
}
