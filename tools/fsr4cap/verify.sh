#!/usr/bin/env bash
# tools/fsr4cap/verify.sh <fsr4cap dir> [frames]: runs AMD's FSR 4.1.1 DLL (fsr4cap.exe under
# umu-run) and the Vulkan replay (out/gpu/fsr4-bench --fsr411, assets in fsr4_411) on the same
# pseudo-random inputs for several sizes and both models, and compares the outputs byte by byte.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
R=$(realpath "$1")
frames=${2:-8}
export WINEPREFIX=$R/pfx GAMEID=umu-fsr4cap WINEDEBUG=-all
export PROTONPATH=${PROTONPATH:-$(ls -d "$HOME"/.local/share/Steam/compatibilitytools.d/GE-Proton* | tail -1)}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
fail=0
for c in '1280x720 1920x1080 2' '640x360 1920x1080 4' '1067x600 1600x900 2' '1707x960 2560x1440 2' \
         '2259x1271 3840x2160 2' '1280x720 3840x2160 4' '1707x720 2560x1080 2'; do
    set -- $c
    umu-run "$R/fsr4cap.exe" 4.1.1 "$1" "$2" "$frames" noise > "$R/umu.log" 2>&1 || true
    BENCH_NOISE=1 BENCH_DUMP=$tmp/replay.raw out/gpu/fsr4-bench "$1" "$2" "$3" "$frames" --fsr411 > "$tmp/bench.log" 2>&1
    if cmp -s "$R/output_$2.raw" "$tmp/replay.raw"; then
        result=bit-exact
    else
        result="DIFFERS ($( (cmp -l "$R/output_$2.raw" "$tmp/replay.raw" || true) | wc -l) bytes)"
        fail=1
    fi
    printf '%-9s <- %-9s preset %s, %s frames: %s\n' "$2" "$1" "$3" "$frames" "$result"
done
exit $fail
