#!/usr/bin/env bash
# Copyright (c) 2026 Q2RTX FSR Vulkan contributors
# SPDX-License-Identifier: MIT

# Generate the exact FSR 3.1.4 analytical Frame Interpolation and Optical Flow
# Vulkan tables.  The four invocations per pass mirror AMD's
# CMakeCompileShaders.txt (base, wave64, FP16, wave64+FP16).  The generated
# headers are build inputs, not handwritten sources.

set -euo pipefail

if (( $# != 1 )); then
    printf 'usage: FFX_VK_FFX_SC=/path/FidelityFX_SC.exe %s OUTPUT_DIRECTORY\n' "$0" >&2
    exit 2
fi

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
module_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
sdk_dir="$module_dir/upstream/ffx-1.1.4/sdk"
gpu_dir="$sdk_dir/include/FidelityFX/gpu"
output_dir=$1

: "${FFX_VK_FFX_SC:?set FFX_VK_FFX_SC to the v1.1.4 FidelityFX_SC.exe path}"
FFX_VK_GLSLANG_EXE=${FFX_VK_GLSLANG_EXE:-"$(dirname -- "$FFX_VK_FFX_SC")/glslangValidator.exe"}
FFX_VK_WINE=${FFX_VK_WINE:-wine}

expected_sc=75480f2245e7b2cac300b013fad1453d4b966e5bd52c69205a40537de05f02a9
expected_glslang=8106440be591596425d7ef401e62b8e21ce778be7a4aa8d240d319b614691a8c

check_tool()
{
    local path=$1 expected=$2 label=$3 actual
    [[ -f $path ]] || { printf '%s not found: %s\n' "$label" "$path" >&2; exit 2; }
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

to_windows_path()
{
    WINEDEBUG=-all winepath -w "$1"
}

wine_output_dir=$(to_windows_path "$output_dir")
wine_glslang_exe=$(to_windows_path "$FFX_VK_GLSLANG_EXE")
wine_gpu_dir=$(to_windows_path "$gpu_dir")

common_args=(
    -reflection
    -deps=gcc
    -num-threads=1
    -DFFX_GPU=1
    -compiler=glslang
    "-glslangexe=$wine_glslang_exe"
    -e CS
    --target-env vulkan1.2
    -S comp
    -Os
    -DFFX_GLSL=1
    "-I$wine_gpu_dir"
    "-output=$wine_output_dir"
)

compile_passes()
{
    local effect_dir=$1
    shift
    local shader stem
    local wine_effect_dir wine_shader
    local -a permutation_args
    if [[ $effect_dir == frameinterpolation ]]; then
        permutation_args=(
            '-DFFX_FRAMEINTERPOLATION_OPTION_LOW_RES_MOTION_VECTORS={0,1}'
            '-DFFX_FRAMEINTERPOLATION_OPTION_JITTER_MOTION_VECTORS={0,1}'
            '-DFFX_FRAMEINTERPOLATION_OPTION_INVERTED_DEPTH={0,1}'
        )
    else
        permutation_args=(
            '-DFFX_OPTICALFLOW_OPTION_HDR_COLOR_INPUT={0,1}'
        )
    fi
    wine_effect_dir=$(to_windows_path "$gpu_dir/$effect_dir")
    for shader in "$@"; do
        stem=$(basename -- "$shader" .glsl)
        wine_shader=$(to_windows_path "$shader")
        env WINEDEBUG=-all "$FFX_VK_WINE" "$FFX_VK_FFX_SC" "${common_args[@]}" "${permutation_args[@]}" "-I$wine_effect_dir" \
            -name="$stem" -DFFX_HALF=0 "$wine_shader"
        env WINEDEBUG=-all "$FFX_VK_WINE" "$FFX_VK_FFX_SC" "${common_args[@]}" "${permutation_args[@]}" "-I$wine_effect_dir" \
            -name="${stem}_wave64" -DFFX_HALF=0 "$wine_shader"
        env WINEDEBUG=-all "$FFX_VK_WINE" "$FFX_VK_FFX_SC" "${common_args[@]}" "${permutation_args[@]}" "-I$wine_effect_dir" \
            -name="${stem}_16bit" -DFFX_HALF=1 "$wine_shader"
        env WINEDEBUG=-all "$FFX_VK_WINE" "$FFX_VK_FFX_SC" "${common_args[@]}" "${permutation_args[@]}" "-I$wine_effect_dir" \
            -name="${stem}_wave64_16bit" -DFFX_HALF=1 "$wine_shader"
    done
}

fi_shader_dir="$sdk_dir/src/backends/vk/shaders/frameinterpolation"
of_shader_dir="$sdk_dir/src/backends/vk/shaders/opticalflow"

pushd "$output_dir" >/dev/null
compile_passes frameinterpolation "$fi_shader_dir"/*.glsl
compile_passes opticalflow "$of_shader_dir"/*.glsl
popd >/dev/null

header_count=$(find "$output_dir" -maxdepth 1 -type f -name '*.h' | wc -l)
if (( header_count < 72 )); then
    printf 'expected at least 72 generated aggregate headers, found %s\n' "$header_count" >&2
    exit 1
fi
printf 'Generated %s FSR3 Frame Interpolation/Optical Flow headers in %s\n' \
    "$header_count" "$output_dir"
printf 'Depfiles are retained for inspection but are not vendored because Wine records absolute paths.\n'
