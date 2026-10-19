#!/usr/bin/env bash
# Copyright (c) 2026 Q2RTX FSR Vulkan contributors
# SPDX-License-Identifier: MIT

set -euo pipefail

if (( $# != 1 )); then
    printf 'usage: FFX_VK_FFX_SC=/path/FidelityFX_SC.exe %s OUTPUT_DIRECTORY\n' "$0" >&2
    exit 2
fi

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
module_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
sdk_dir="$module_dir/upstream/ffx-1.1.4/sdk"
shader_dir="$sdk_dir/src/backends/vk/shaders/fsr3upscaler"
gpu_dir="$sdk_dir/include/FidelityFX/gpu"
manifest="$module_dir/generated/ffx-1.1.4/vk/fsr3upscaler/SHA256SUMS"
output_dir=$1

: "${FFX_VK_FFX_SC:?set FFX_VK_FFX_SC to the v1.1.4 FidelityFX_SC.exe path}"
FFX_VK_GLSLANG_EXE=${FFX_VK_GLSLANG_EXE:-"$(dirname -- "$FFX_VK_FFX_SC")/glslangValidator.exe"}
FFX_VK_WINE=${FFX_VK_WINE:-wine}

expected_sc=75480f2245e7b2cac300b013fad1453d4b966e5bd52c69205a40537de05f02a9
expected_glslang=8106440be591596425d7ef401e62b8e21ce778be7a4aa8d240d319b614691a8c

check_tool()
{
    local path=$1
    local expected=$2
    local label=$3
    if [[ ! -f $path ]]; then
        printf '%s not found: %s\n' "$label" "$path" >&2
        exit 2
    fi
    local actual
    actual=$(sha256sum -- "$path")
    actual=${actual%% *}
    if [[ $actual != "$expected" && ${FFX_VK_ALLOW_UNPINNED_TOOLS:-0} != 1 ]]; then
        printf '%s SHA-256 mismatch\nexpected: %s\nactual:   %s\n' "$label" "$expected" "$actual" >&2
        printf 'Set FFX_VK_ALLOW_UNPINNED_TOOLS=1 only for an intentional generator update.\n' >&2
        exit 2
    fi
}

check_tool "$FFX_VK_FFX_SC" "$expected_sc" FidelityFX_SC.exe
check_tool "$FFX_VK_GLSLANG_EXE" "$expected_glslang" glslangValidator.exe
command -v "$FFX_VK_WINE" >/dev/null 2>&1 || {
    printf 'Wine launcher not found: %s\n' "$FFX_VK_WINE" >&2
    exit 2
}

if [[ -d $output_dir ]] && [[ -n $(find "$output_dir" -mindepth 1 -maxdepth 1 -print -quit) ]]; then
    printf 'output directory must be empty: %s\n' "$output_dir" >&2
    exit 2
fi
mkdir -p -- "$output_dir"
output_dir=$(CDPATH= cd -- "$output_dir" && pwd)

common_args=(
    -reflection
    -deps=gcc
    -num-threads=1
    -DFFX_GPU=1
    -DFFX_FSR3UPSCALER_OPTION_UPSAMPLE_SAMPLERS_USE_DATA_HALF=0
    -DFFX_FSR3UPSCALER_OPTION_ACCUMULATE_SAMPLERS_USE_DATA_HALF=0
    -DFFX_FSR3UPSCALER_OPTION_REPROJECT_SAMPLERS_USE_DATA_HALF=1
    -DFFX_FSR3UPSCALER_OPTION_POSTPROCESSLOCKSTATUS_SAMPLERS_USE_DATA_HALF=0
    -DFFX_FSR3UPSCALER_OPTION_UPSAMPLE_USE_LANCZOS_TYPE=2
    -compiler=glslang
    "-glslangexe=$FFX_VK_GLSLANG_EXE"
    -e CS
    --target-env vulkan1.2
    -S comp
    -Os
    -DFFX_GLSL=1
    '-DFFX_FSR3UPSCALER_OPTION_REPROJECT_USE_LANCZOS_TYPE={0,1}'
    '-DFFX_FSR3UPSCALER_OPTION_HDR_COLOR_INPUT={0,1}'
    '-DFFX_FSR3UPSCALER_OPTION_LOW_RESOLUTION_MOTION_VECTORS={0,1}'
    '-DFFX_FSR3UPSCALER_OPTION_JITTERED_MOTION_VECTORS={0,1}'
    '-DFFX_FSR3UPSCALER_OPTION_INVERTED_DEPTH={0,1}'
    '-DFFX_FSR3UPSCALER_OPTION_APPLY_SHARPENING={0,1}'
    "-I$gpu_dir"
    "-I$gpu_dir/fsr3upscaler"
    "-output=$output_dir"
)

for shader in "$shader_dir"/*.glsl; do
    stem=$(basename -- "$shader" .glsl)
    "$FFX_VK_WINE" "$FFX_VK_FFX_SC" "${common_args[@]}" "-name=$stem" \
        -DFFX_HALF=0 "$shader"
    "$FFX_VK_WINE" "$FFX_VK_FFX_SC" "${common_args[@]}" "-name=${stem}_wave64" \
        -DFFX_HALF=0 "$shader"
    "$FFX_VK_WINE" "$FFX_VK_FFX_SC" "${common_args[@]}" "-name=${stem}_16bit" \
        -DFFX_HALF=1 "$shader"
    "$FFX_VK_WINE" "$FFX_VK_FFX_SC" "${common_args[@]}" "-name=${stem}_wave64_16bit" \
        -DFFX_HALF=1 "$shader"
done

header_count=$(find "$output_dir" -maxdepth 1 -type f -name '*.h' | wc -l)
if [[ $header_count -ne 200 ]]; then
    printf 'expected 200 generated headers, found %s\n' "$header_count" >&2
    exit 1
fi

(cd "$output_dir" && sha256sum -c --quiet "$manifest")
printf 'Generated and verified %s deterministic FSR3 upscaler headers in %s\n' "$header_count" "$output_dir"
printf 'Depfiles are retained for inspection but are not vendored because Wine records absolute paths.\n'
