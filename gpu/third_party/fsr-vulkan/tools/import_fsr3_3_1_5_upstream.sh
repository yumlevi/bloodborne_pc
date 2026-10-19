#!/usr/bin/env bash
# Copyright (c) 2026 Q2RTX FSR Vulkan contributors
# SPDX-License-Identifier: MIT

# Import the public SDK 2.3.0 FSR3 closure: the 3.1.5 upscaler and the newer
# 3.1.6 analytical frame-interpolation/optical-flow sources. This is
# deliberately separate from the proven 1.1.4 implementation: SDK 2.3 has a
# different backend ABI and must not be mixed with the older Vulkan backend.

set -euo pipefail

if (( $# != 1 )); then
    printf 'usage: %s /path/to/FidelityFX-SDK\n' "$0" >&2
    exit 2
fi

sdk_repo=$1
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
module_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
destination="$module_dir/upstream/ffx-2.3.0"
revision=v2.3.0
expected_commit=60f4ea81909200d8542eca14dccb2628b763a9a3

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

# This is the complete source/header closure needed to begin a standalone
# FSR3.1.5 host/backend port.  It intentionally excludes the API loader,
# provider wrappers, DX12 backend, signed binaries, and every amdinternal
# file.  The two absent amdinternal watermark headers are an explicit porting
# task, not something this importer papers over.
paths=(
    Kits/FidelityFX/api/include
    Kits/FidelityFX/api/internal
    Kits/FidelityFX/backend/dx12/ffx_dx12.h
    Kits/FidelityFX/backend/dx12/ffx_dx12.cpp
    Kits/FidelityFX/backend/dx12/ffx_backends_dx12.cpp
    Kits/FidelityFX/upscalers/include
    Kits/FidelityFX/upscalers/fsr3/include
    Kits/FidelityFX/upscalers/fsr3/internal
    Kits/FidelityFX/framegeneration/include
    Kits/FidelityFX/framegeneration/fsr3/include
    Kits/FidelityFX/framegeneration/fsr3/internal
)

while IFS= read -r path; do
    target="$destination/$path"
    if [[ -e $target ]]; then
        expected_blob=$(git -C "$sdk_repo" rev-parse "$revision:$path")
        actual_blob=$(git hash-object -- "$target")
        if [[ $actual_blob != "$expected_blob" ]]; then
            # A deliberate portable patch is permitted only when it exactly
            # matches the checked current-source manifest.  This keeps reruns
            # safe after the documented port overlay while still rejecting an
            # accidental/manual source edit.
            current_manifest="$destination/CURRENT_SHA256SUMS"
            expected_current=
            if [[ -f $current_manifest ]]; then
                expected_current=$(awk -v path="$path" '$2 == path { print $1; exit }' "$current_manifest")
            fi
            actual_sha256=$(sha256sum -- "$target")
            actual_sha256=${actual_sha256%% *}
            if [[ $actual_sha256 != "$expected_current" ]]; then
                printf 'refusing to overwrite changed upstream import: %s\n' "$target" >&2
                exit 1
            fi
        fi
    fi

    # SDK 2.3's repository license has restrictions for files without an
    # overriding notice.  Keep this import narrowly source-only and make the
    # per-file MIT grant an executable gate.
    # Do not use rg -q here: it exits early and makes git return SIGPIPE under
    # pipefail even for a valid notice.
    if ! git -C "$sdk_repo" show "$revision:$path" | \
            rg 'Permission is hereby granted' >/dev/null; then
        printf 'refusing to import file without an individual MIT notice: %s\n' "$path" >&2
        exit 1
    fi
done < <(git -C "$sdk_repo" ls-tree -r --name-only "$revision" -- "${paths[@]}")

mkdir -p -- "$destination"
git -C "$sdk_repo" archive --format=tar "$revision" -- "${paths[@]}" |
    tar --skip-old-files -x -C "$destination"

# Always calculate pristine hashes from a fresh archive, never from the
# destination: the latter intentionally contains the documented Linux patches.
staging=$(mktemp -d)
trap 'find -- "$staging" -depth -delete; rmdir -- "$staging" 2>/dev/null || true' EXIT
git -C "$sdk_repo" archive --format=tar "$revision" -- "${paths[@]}" |
    tar -x -C "$staging"
(
    cd "$staging"
    find Kits -type f -print0 | sort -z | xargs -0 sha256sum > ORIGINAL_SHA256SUMS
)
mv -- "$staging/ORIGINAL_SHA256SUMS" "$destination/ORIGINAL_SHA256SUMS"

# CURRENT records the exact working tree accepted on a later import.  It is
# intentionally separate from ORIGINAL because the portable source overlay
# has a few reviewed Linux/backend ABI fixes.  The preflight above verifies
# every pre-existing file against either its upstream blob or this manifest
# before this refresh can bless any newly imported source.
(
    cd "$destination"
    find Kits -type f -print0 | sort -z | xargs -0 sha256sum > CURRENT_SHA256SUMS
)

printf 'Imported %s public SDK 2.3 FSR source/header files from %s (%s).\n' \
    "$(find "$destination/Kits" -type f | wc -l)" "$revision" "$expected_commit"
