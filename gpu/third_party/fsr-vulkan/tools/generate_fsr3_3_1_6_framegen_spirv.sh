#!/usr/bin/env bash
# Copyright (c) 2026 Q2RTX FSR Vulkan contributors
# SPDX-License-Identifier: MIT

# Generate the public SDK 2.3.0 FSR3 Frame Interpolation 3.1.6 and Optical
# Flow SPIR-V modules.  This is the Q2RTX-compatible analytical profile:
# low-resolution, unjittered current-to-previous motion and conventional
# device depth.  It is source-derived only; no AMD binary provider or shader
# blob is consumed.

set -euo pipefail

if (( $# != 1 )); then
    printf 'usage: DXC_BIN=/path/to/dxc %s OUTPUT_DIRECTORY\n' "$0" >&2
    exit 2
fi

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
module_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
sdk_dir="$module_dir/upstream/ffx-2.3.0/Kits/FidelityFX"
shader_dir="$sdk_dir/framegeneration/fsr3/internal/shaders"
core_include="$sdk_dir/api/internal/gpu"
framegen_include="$sdk_dir/framegeneration/fsr3/include/gpu"
output_dir=$1

: "${DXC_BIN:?set DXC_BIN to the native DirectXShaderCompiler dxc executable}"
SPIRV_VAL_BIN=${SPIRV_VAL_BIN:-spirv-val}
SPIRV_DIS_BIN=${SPIRV_DIS_BIN:-spirv-dis}

[[ -x $DXC_BIN ]] || { printf 'DXC_BIN is not executable: %s\n' "$DXC_BIN" >&2; exit 2; }
command -v "$SPIRV_VAL_BIN" >/dev/null 2>&1 || {
    printf 'SPIR-V validator not found: %s\n' "$SPIRV_VAL_BIN" >&2; exit 2;
}
command -v "$SPIRV_DIS_BIN" >/dev/null 2>&1 || {
    printf 'SPIR-V disassembler not found: %s\n' "$SPIRV_DIS_BIN" >&2; exit 2;
}

# This is the same audited DXC used by the 3.1.5 and FSR4 validation paths.
# A toolchain transition must be deliberate, reviewable, and accompanied by a
# new output manifest.
expected_dxc_sha256=7bb324044a6e5ad5a89070457f91a21e0854985d8e66bd88fc01a222315eaa66
actual_dxc_sha256=$(sha256sum -- "$DXC_BIN")
actual_dxc_sha256=${actual_dxc_sha256%% *}
if [[ $actual_dxc_sha256 != "$expected_dxc_sha256" && ${FSR3_316_ALLOW_UNPINNED_DXC:-0} != 1 ]]; then
    printf 'DXC SHA-256 mismatch\nexpected: %s\nactual:   %s\n' \
        "$expected_dxc_sha256" "$actual_dxc_sha256" >&2
    printf 'Set FSR3_316_ALLOW_UNPINNED_DXC=1 only for an intentional generator update.\n' >&2
    exit 2
fi

if [[ -d $output_dir ]] && [[ -n $(find "$output_dir" -mindepth 1 -maxdepth 1 -print -quit) ]]; then
    printf 'output directory must be empty: %s\n' "$output_dir" >&2
    exit 2
fi
mkdir -p -- "$output_dir"
output_dir=$(CDPATH= cd -- "$output_dir" && pwd)

staging=$(mktemp -d)
trap 'find -- "$staging" -depth -delete; rmdir -- "$staging" 2>/dev/null || true' EXIT
overlay_dir="$staging/overlay"
python3 "$script_dir/prepare_fsr3_3_1_6_vulkan_sources.py" "$framegen_include" "$overlay_dir"

common_args=(
    -spirv
    -fspv-target-env=vulkan1.2
    -T cs_6_2
    -E CS
    -DFFX_GPU=1
    -DFFX_HLSL=1
    -I "$core_include"
    -I "$overlay_dir"
    -I "$framegen_include"
)

validate_module() {
    local module=$1 duplicate
    "$SPIRV_VAL_BIN" --target-env vulkan1.2 "$module"
    duplicate=$("$SPIRV_DIS_BIN" "$module" |
        awk '$1 == "OpDecorate" && $3 == "Binding" { print $4 }' |
        sort -n | uniq -d)
    [[ -z $duplicate ]] || {
        printf 'descriptor binding collision in %s: %s\n' "$module" "$duplicate" >&2
        exit 1
    }
}

# HLSL reuses t0/u0/b0 across resource categories.  Probe at disjoint ranges,
# then derive the smallest non-overlapping per-pass ranges.  This avoids the
# old broad 1000/2000/3000 category ABI that caused excessive sparse binding
# layouts on RADV, while keeping all SPIR-V binding choices reproducible from
# the module itself.
compile_module() {
    local prefix=$1 source=$2
    shift 2
    local stem probe output binding tmax=-1 umax=-1 u_shift b_shift
    local -a effect_args=("$@")

    stem=$(basename -- "$source" .hlsl)
    stem=${stem%_pass}
    probe="$staging/${prefix}_${stem}.spv"
    output="$output_dir/${prefix}_${stem}.spv"
    [[ -f $source ]] || { printf 'missing SDK shader: %s\n' "$source" >&2; exit 1; }

    "$DXC_BIN" "${common_args[@]}" "${effect_args[@]}" \
        -fvk-t-shift 0 all -fvk-u-shift 64 all -fvk-b-shift 96 all \
        -fvk-s-shift 1000 all "$source" -Fo "$probe"
    validate_module "$probe"

    while IFS= read -r binding; do
        if (( binding < 64 )); then
            (( binding > tmax )) && tmax=$binding
        elif (( binding < 96 )); then
            (( binding > umax )) && umax=$binding
        fi
    done < <("$SPIRV_DIS_BIN" "$probe" |
        awk '$1 == "OpDecorate" && $3 == "Binding" { print $4 }')

    u_shift=$((tmax + 1))
    if (( umax < 0 )); then
        b_shift=$u_shift
    else
        b_shift=$((u_shift + umax - 64 + 1))
    fi
    "$DXC_BIN" "${common_args[@]}" "${effect_args[@]}" \
        -fvk-t-shift 0 all -fvk-u-shift "$u_shift" all \
        -fvk-b-shift "$b_shift" all -fvk-s-shift 1000 all \
        "$source" -Fo "$output"
    validate_module "$output"
}

fi_passes=(
    ffx_frameinterpolation_reconstruct_and_dilate_pass.hlsl
    ffx_frameinterpolation_setup_pass.hlsl
    ffx_frameinterpolation_reconstruct_previous_depth_pass.hlsl
    ffx_frameinterpolation_game_motion_vector_field_pass.hlsl
    ffx_frameinterpolation_optical_flow_vector_field_pass.hlsl
    ffx_frameinterpolation_disocclusion_mask_pass.hlsl
    ffx_frameinterpolation_pass.hlsl
    ffx_frameinterpolation_compute_inpainting_pyramid_pass.hlsl
    ffx_frameinterpolation_inpainting_pass.hlsl
    ffx_frameinterpolation_compute_game_vector_field_inpainting_pyramid_pass.hlsl
    ffx_frameinterpolation_debug_view_pass.hlsl
)
of_passes=(
    ffx_opticalflow_compute_luminance_pyramid_pass.hlsl
    ffx_opticalflow_prepare_luma_pass.hlsl
    ffx_opticalflow_generate_scd_histogram_pass.hlsl
    ffx_opticalflow_compute_scd_divergence_pass.hlsl
    ffx_opticalflow_compute_optical_flow_advanced_pass_v5.hlsl
    ffx_opticalflow_filter_optical_flow_pass_v5.hlsl
    ffx_opticalflow_scale_optical_flow_advanced_pass_v5.hlsl
)
fi_args=(
    -DFFX_FRAMEINTERPOLATION_EMBED_ROOTSIG=0
    -DFFX_FRAMEINTERPOLATION_OPTION_LOW_RES_MOTION_VECTORS=1
    -DFFX_FRAMEINTERPOLATION_OPTION_JITTERED_MOTION_VECTORS=0
    -DFFX_FRAMEINTERPOLATION_OPTION_INVERTED_DEPTH=0
)
of_args=(
    -DFFX_OPTICALFLOW_EMBED_ROOTSIG=0
    -DFFX_OPTICALFLOW_OPTION_HDR_COLOR_INPUT=0
)

for pass in "${fi_passes[@]}"; do
    compile_module fsr3_3_1_6_fi "$shader_dir/$pass" "${fi_args[@]}"
done
for pass in "${of_passes[@]}"; do
    compile_module fsr3_3_1_6_of "$shader_dir/$pass" "${of_args[@]}"
done

(cd "$output_dir" && sha256sum -- *.spv | LC_ALL=C sort > SHA256SUMS)
(
    cd "$sdk_dir"
    for pass in "${fi_passes[@]}" "${of_passes[@]}"; do
        sha256sum "framegeneration/fsr3/internal/shaders/$pass"
    done | LC_ALL=C sort > "$output_dir/SOURCE_SHA256SUMS"
)
python3 "$script_dir/package_fsr3_3_1_6_framegen_spirv.py" "$output_dir" \
    "$output_dir/ffx_vk_fsr3_3_1_6_framegen_embedded_spirv.h"
printf 'Generated and validated %s public FSR3.1.6 FI/OF Vulkan modules in %s\n' \
    "$(( ${#fi_passes[@]} + ${#of_passes[@]} ))" "$output_dir"
