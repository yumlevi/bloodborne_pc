#!/usr/bin/env bash
# tools/fsr4cap/capture_all.sh <dir with fsr4cap.exe, the loader and the upscaler DLL>
# Records FSR 4 (the DLL's 4.1.1 version) for output 1080p/1440p/2160p with the five quality
# ratios, plus other output sizes (Steam Deck, 1600x900, ultrawide), through umu-run (Proton).
# Needs PROTONPATH (default: the newest GE-Proton in Steam's compatibilitytools.d) and umu-run.
set -euo pipefail
R=$(realpath "$1")
export WINEPREFIX=$R/pfx GAMEID=umu-fsr4cap WINEDEBUG=-all
export PROTONPATH=${PROTONPATH:-$(ls -d "$HOME"/.local/share/Steam/compatibilitytools.d/GE-Proton* | tail -1)}
record() { # record <render WxH> <output WxH>
    rm -rf "$R/capture_$1_$2"
    umu-run "$R/fsr4cap.exe" 4.1.1 "$1" "$2" 3 > "$R/umu.log" 2>&1 || true
    printf '%-9s <- %-9s %s\n' "$2" "$1" "$(tail -1 "$R/fsr4cap.log")"
}
for out in 1920x1080 2560x1440 3840x2160; do
    ow=${out%x*}; oh=${out#*x}
    for ratio in 1.0 1.5 1.7 2.0 3.0; do
        record "$(awk -v w="$ow" -v h="$oh" -v r="$ratio" 'BEGIN { printf "%dx%d", int(w / r + 0.5), int(h / r + 0.5) }')" "$out"
    done
done
for c in '853x533 1280x800' '640x400 1280x800' '1067x600 1600x900' '1707x720 2560x1080' '2293x960 3440x1440'; do
    record $c
done
