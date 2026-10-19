#!/usr/bin/env bash
# Copyright (c) 2026 Q2RTX FSR Vulkan contributors
# SPDX-License-Identifier: MIT

# Import the exact, MIT-licensed Frame Interpolation and Optical Flow source
# closure used by the portable FSR3 Vulkan backend.  This is intentionally a
# narrow, reproducible git-archive import rather than a copy of an arbitrary
# SDK working tree.

set -euo pipefail

if (( $# != 1 )); then
    printf 'usage: %s /path/to/FidelityFX-SDK\n' "$0" >&2
    exit 2
fi

sdk_repo=$1
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
module_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
destination="$module_dir/upstream/ffx-1.1.4"
revision=v1.1.4
expected_commit=c6efa6bf7f2027b3ec94f28578bb5965eabb9e55

if [[ ! -d $sdk_repo/.git ]]; then
    printf 'not a FidelityFX SDK git checkout: %s\n' "$sdk_repo" >&2
    exit 2
fi

actual_commit=$(git -C "$sdk_repo" rev-parse "$revision^{commit}")
if [[ $actual_commit != "$expected_commit" ]]; then
    printf 'unexpected %s commit\nexpected: %s\nactual:   %s\n' \
        "$revision" "$expected_commit" "$actual_commit" >&2
    exit 1
fi

paths=(
    sdk/include/FidelityFX/gpu/frameinterpolation
    sdk/include/FidelityFX/gpu/opticalflow
    sdk/src/backends/vk/shaders/frameinterpolation
    sdk/src/backends/vk/shaders/opticalflow
    sdk/src/backends/shared/blob_accessors/ffx_frameinterpolation_shaderblobs.cpp
    sdk/src/backends/shared/blob_accessors/ffx_frameinterpolation_shaderblobs.h
    sdk/src/backends/shared/blob_accessors/ffx_opticalflow_shaderblobs.cpp
    sdk/src/backends/shared/blob_accessors/ffx_opticalflow_shaderblobs.h
)

while IFS= read -r path; do
    target="$destination/$path"
    if [[ -e $target ]]; then
        expected_blob=$(git -C "$sdk_repo" rev-parse "$revision:$path")
        actual_blob=$(git hash-object -- "$target")
        if [[ $actual_blob != "$expected_blob" ]]; then
            printf 'refusing to overwrite changed upstream import: %s\n' "$target" >&2
            exit 1
        fi
    fi
done < <(git -C "$sdk_repo" ls-tree -r --name-only "$revision" -- "${paths[@]}")

git -C "$sdk_repo" archive --format=tar "$revision" -- "${paths[@]}" |
    tar --skip-old-files -x -C "$destination"

printf 'Imported pinned FSR3 FI/OF source closure from %s (%s).\n' \
    "$revision" "$expected_commit"
