#!/usr/bin/env bash
# Copyright (c) 2026 Q2RTX FSR Vulkan contributors
# SPDX-License-Identifier: MIT

# Generate the public SDK 2.3 FSR3.1.5 HLSL upscaler passes as Vulkan SPIR-V.
# This is deliberately a narrow, Q2RTX-compatible permutation: linear HDR,
# low-resolution unjittered current-to-previous motion vectors, conventional
# (not inverted) depth, and the Lanczos-2 reproject path.  The resulting eleven
# modules are an auditable bridge milestone, not yet a complete FfxInterface
# backend or a replacement for the proven FSR3.1.4 path.

set -euo pipefail

if (( $# != 1 )); then
    printf 'usage: DXC_BIN=/path/to/dxc %s OUTPUT_DIRECTORY\n' "$0" >&2
    exit 2
fi

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
module_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
sdk_dir="$module_dir/upstream/ffx-2.3.0/Kits/FidelityFX"
shader_dir="$sdk_dir/upscalers/fsr3/internal/shaders"
core_include="$sdk_dir/api/internal/gpu"
fsr_include="$sdk_dir/upscalers/fsr3/include/gpu"
output_dir=$1

: "${DXC_BIN:?set DXC_BIN to the native DirectXShaderCompiler dxc executable}"
SPIRV_VAL_BIN=${SPIRV_VAL_BIN:-spirv-val}
SPIRV_DIS_BIN=${SPIRV_DIS_BIN:-spirv-dis}

[[ -x $DXC_BIN ]] || { printf 'DXC_BIN is not executable: %s\n' "$DXC_BIN" >&2; exit 2; }
command -v "$SPIRV_VAL_BIN" >/dev/null 2>&1 || {
    printf 'SPIR-V validator not found: %s\n' "$SPIRV_VAL_BIN" >&2
    exit 2
}
command -v "$SPIRV_DIS_BIN" >/dev/null 2>&1 || {
    printf 'SPIR-V disassembler not found: %s\n' "$SPIRV_DIS_BIN" >&2
    exit 2
}

validate_module() {
    local module=$1
    local duplicate
    "$SPIRV_VAL_BIN" --target-env vulkan1.2 "$module"
    duplicate=$("$SPIRV_DIS_BIN" "$module" | awk '/OpDecorate .* Binding/ { print $NF }' | sort -n | uniq -d)
    [[ -z $duplicate ]] || {
        printf 'descriptor binding collision in %s: %s\n' "$module" "$duplicate" >&2
        exit 1
    }
}

# Pin the locally built DXC that has been used for the FSR4 ABI-validated
# assets.  An intentional toolchain migration must be explicit and leaves a
# new checked manifest for review.
expected_dxc_sha256=7bb324044a6e5ad5a89070457f91a21e0854985d8e66bd88fc01a222315eaa66
actual_dxc_sha256=$(sha256sum -- "$DXC_BIN")
actual_dxc_sha256=${actual_dxc_sha256%% *}
if [[ $actual_dxc_sha256 != "$expected_dxc_sha256" && ${FSR3_315_ALLOW_UNPINNED_DXC:-0} != 1 ]]; then
    printf 'DXC SHA-256 mismatch\nexpected: %s\nactual:   %s\n' \
        "$expected_dxc_sha256" "$actual_dxc_sha256" >&2
    printf 'Set FSR3_315_ALLOW_UNPINNED_DXC=1 only for an intentional generator update.\n' >&2
    exit 2
fi

if [[ -d $output_dir ]] && [[ -n $(find "$output_dir" -mindepth 1 -maxdepth 1 -print -quit) ]]; then
    printf 'output directory must be empty: %s\n' "$output_dir" >&2
    exit 2
fi
mkdir -p -- "$output_dir"
output_dir=$(CDPATH= cd -- "$output_dir" && pwd)

common_args=(
    -spirv
    -fspv-target-env=vulkan1.2
    -T cs_6_2
    -E CS
    # Keep the sampler range consistent with the proven FidelityFX Vulkan
    # backend.  Texture/UAV/CBV shifts are selected per pass below so all
    # non-sampler bindings are tightly packed; one global category shift would
    # leave the CBV at a needlessly sparse index and has crashed RADV's pipeline
    # compiler in practice.
    -fvk-t-shift 0 all
    -fvk-s-shift 1000 all
    -DFFX_GPU=1
    -DFFX_HLSL=1
    -DFFX_FSR3UPSCALER_EMBED_ROOTSIG=0
    -DFFX_FSR3UPSCALER_OPTION_UPSAMPLE_SAMPLERS_USE_DATA_HALF=0
    -DFFX_FSR3UPSCALER_OPTION_ACCUMULATE_SAMPLERS_USE_DATA_HALF=0
    -DFFX_FSR3UPSCALER_OPTION_REPROJECT_SAMPLERS_USE_DATA_HALF=1
    -DFFX_FSR3UPSCALER_OPTION_POSTPROCESSLOCKSTATUS_SAMPLERS_USE_DATA_HALF=0
    -DFFX_FSR3UPSCALER_OPTION_UPSAMPLE_USE_LANCZOS_TYPE=2
    -DFFX_FSR3UPSCALER_OPTION_REPROJECT_USE_LANCZOS_TYPE=1
    -DFFX_FSR3UPSCALER_OPTION_HDR_COLOR_INPUT=1
    -DFFX_FSR3UPSCALER_OPTION_LOW_RESOLUTION_MOTION_VECTORS=1
    -DFFX_FSR3UPSCALER_OPTION_JITTERED_MOTION_VECTORS=0
    -DFFX_FSR3UPSCALER_OPTION_INVERTED_DEPTH=0
    # The generic HLSL SPD quad path reuses LDS between wave reductions.  GPU-AV
    # correctly exposes its cross-wave shared-memory hazards on the RDNA2 Vulkan
    # target. Select AMD's documented LDS-only permutation instead: it has an
    # explicit workgroup barrier around each intermediate reduction and avoids
    # requiring an assumed wave/subgroup configuration from the embedding app.
    -DFFX_SPD_NO_WAVE_OPERATIONS=1
    -I "$core_include"
    -I "$fsr_include"
)

passes=(
    accumulate
    autogen_reactive
    debug_view
    luma_instability
    luma_pyramid
    prepare_inputs
    prepare_reactivity
    rcas
    shading_change
    shading_change_pyramid
)

# Each value places the first UAV immediately after the last SRV used by that
# pass. The second puts b0 just after the highest possible SRV/UAV; b1 (SPD or
# RCAS) follows it. They are derived from the public shader register tables and
# deliberately remain explicit so a public-SDK update must review the ABI.
declare -A uav_shifts=(
    [accumulate]=9 [autogen_reactive]=2 [debug_view]=4
    [luma_instability]=7 [luma_pyramid]=2 [prepare_inputs]=3
    [prepare_reactivity]=9 [rcas]=2 [shading_change]=1
    [shading_change_pyramid]=4
)
declare -A cbv_shifts=(
    [accumulate]=12 [autogen_reactive]=2 [debug_view]=5
    [luma_instability]=9 [luma_pyramid]=11 [prepare_inputs]=8
    [prepare_reactivity]=12 [rcas]=3 [shading_change]=2
    [shading_change_pyramid]=12
)

for pass in "${passes[@]}"; do
    source="$shader_dir/ffx_fsr3upscaler_${pass}_pass.hlsl"
    output="$output_dir/fsr3_3_1_5_${pass}.spv"
    [[ -f $source ]] || { printf 'missing SDK shader: %s\n' "$source" >&2; exit 1; }
    "$DXC_BIN" "${common_args[@]}" \
        -fvk-u-shift "${uav_shifts[$pass]}" all \
        -fvk-b-shift "${cbv_shifts[$pass]}" all \
        -DFFX_FSR3UPSCALER_OPTION_APPLY_SHARPENING=0 \
        "$source" -Fo "$output"
    validate_module "$output"
done

# The SDK creates a distinct AccumulateSharpen pipeline and selects it when a
# dispatch requests RCAS. Its HLSL changes at compile time, so it cannot share
# the ordinary Accumulate module even though both derive from one source file.
"$DXC_BIN" "${common_args[@]}" \
    -fvk-u-shift "${uav_shifts[accumulate]}" all \
    -fvk-b-shift "${cbv_shifts[accumulate]}" all \
    -DFFX_FSR3UPSCALER_OPTION_APPLY_SHARPENING=1 \
    "$shader_dir/ffx_fsr3upscaler_accumulate_pass.hlsl" \
    -Fo "$output_dir/fsr3_3_1_5_accumulate_sharpen.spv"
validate_module "$output_dir/fsr3_3_1_5_accumulate_sharpen.spv"

(cd "$output_dir" && sha256sum -- *.spv | LC_ALL=C sort > SHA256SUMS)
python3 "$script_dir/package_fsr3_3_1_5_spirv.py" "$output_dir" \
    "$output_dir/ffx_vk_fsr3_3_1_5_embedded_spirv.h"
printf 'Generated and validated %s FSR3.1.5 Vulkan modules in %s\n' \
    "$(( ${#passes[@]} + 1 ))" "$output_dir"
