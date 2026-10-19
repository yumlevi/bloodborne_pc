#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
mkdir -p out
if [[ -z ${CC:-} ]]; then
    CC=$(command -v cc || command -v gcc || true)
    if [[ -z $CC ]]; then
        for candidate in /nix/store/*-gcc-wrapper-*/bin/gcc; do
            if [[ -x $candidate ]]; then CC=$candidate; break; fi
        done
    fi
fi
if [[ -z ${CC:-} ]]; then echo 'Install GCC/Clang or set CC.' >&2; exit 1; fi
# Dependencies come from pkg-config (Vulkan loader/headers, SDL3). On NixOS the
# environment is provided by shell.nix; re-enter it automatically if needed.
if ! { command -v pkg-config >/dev/null && pkg-config --exists vulkan sdl3 && command -v cmake >/dev/null && command -v ninja >/dev/null; }; then
    if [[ -z ${BB_IN_NIX_SHELL:-} ]] && command -v nix-shell >/dev/null; then
        exec env BB_IN_NIX_SHELL=1 nix-shell shell.nix --run "bash build.sh $*"
    fi
    echo 'Need pkg-config with vulkan and sdl3, cmake and ninja (see shell.nix).' >&2; exit 1
fi
read -r -a includes <<< "$(pkg-config --cflags vulkan sdl3)"
read -r -a libraries <<< "$(pkg-config --libs vulkan sdl3)"
# Windows: MSYS2 CLANG64 shell (README "Windows": clang, libc++ and lld, whose threads and locks
# are the Win32 ones). The GPU library is static there (a DLL cannot leave the runtime_* symbols
# to the executable), the runtime adds its Win32 files (src/win32_*.c), runs without pthreads,
# and the executable keeps a console and bottom-up addresses (guest-visible host memory must
# stay below 1 TiB).
windows=
case $(uname -s) in MINGW*|MSYS*|CYGWIN*) windows=1 ;; esac
cstd=(-std=c11)
threads=(-pthread)
if [[ -n $windows ]]; then
    threads=()
    libraries=("${libraries[@]/-mwindows/-mconsole}")
    cstd=(-std=gnu11 -D_FILE_OFFSET_BITS=64 -D_WIN32_WINNT=0x0A00)
fi
# GPU library (shadPS4 video core + drivers), built by CMake into out/gpu/libbbgpu.so.
# BB_PGO: generate (instrumented build that writes pgo/ while the game runs), use, off.
# Default: use the profile in pgo/ when there is one. BB_LTO=OFF disables link-time optimization.
pgo=${BB_PGO:-}
if [[ -z $pgo ]]; then
    if [[ -n $(find pgo -name '*.gcda' -print -quit 2>/dev/null) ]]; then pgo=use; else pgo=off; fi
fi
mkdir -p pgo
# Submodules (git clone --recursive, or: git submodule update --init) and this port's changes
# to FSR-Vulkan (gpu/patches/fsr-vulkan), applied to its working tree once.
if [[ ! -f gpu/third_party/fsr-vulkan/CMakeLists.txt || ! -f gpu/third_party/imgui/imgui.h ]]; then
    git submodule update --init --recursive
fi
for patch in gpu/patches/fsr-vulkan/*.patch; do
    # Portable bundles can contain an already-patched FSR tree whose detached
    # .git file no longer resolves.  MSYS git cannot always reverse-check that
    # copied CRLF tree, so recognize this patch's stable source marker first.
    if [[ $(basename "$patch") == 0001-FSR-4-v07-* ]] &&
       grep -q '^#define PROF_MAX_JOBS' gpu/third_party/fsr-vulkan/src/ffx_vk_fsr4_v07.c; then
        continue
    fi
    if git -C gpu/third_party/fsr-vulkan rev-parse --git-dir >/dev/null 2>&1; then
        if ! git -C gpu/third_party/fsr-vulkan apply --reverse --check "$PWD/$patch" 2>/dev/null; then
            git -C gpu/third_party/fsr-vulkan apply "$PWD/$patch"
        fi
    elif ! git apply --no-index --directory=gpu/third_party/fsr-vulkan \
              --reverse --check "$patch" 2>/dev/null; then
        git apply --no-index --directory=gpu/third_party/fsr-vulkan "$patch"
    fi
done
# Prefer the checked-in local dependency fallbacks when this portable source tree has them.
# This keeps configuration offline and avoids FetchContent downloads on Windows.
local_deps=()
for dependency in magic_enum miniz xbyak; do
    if [[ -f "local_deps/$dependency/CMakeLists.txt" ]]; then
        variable=${dependency^^}
        local_deps+=("-DFETCHCONTENT_SOURCE_DIR_${variable}=$PWD/local_deps/$dependency")
    fi
done
# A copied build tree may still name its old drive/path. CMake --fresh replaces only generated
# configuration state; source dependencies and the rest of out/ stay in place.
cmake_fresh=()
if [[ -f out/gpu/CMakeCache.txt ]]; then
    expected="$PWD/gpu"
    if command -v cygpath >/dev/null; then expected=$(cygpath -m "$expected"); fi
    if ! grep -Fqx "CMAKE_HOME_DIRECTORY:INTERNAL=$expected" out/gpu/CMakeCache.txt; then
        cmake_fresh=(--fresh)
    fi
fi
cmake "${cmake_fresh[@]}" -S gpu -B out/gpu -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBB_PGO="$pgo" -DBB_LTO="${BB_LTO:-ON}" -DBB_PGO_DIR="$PWD/pgo" \
    "${local_deps[@]}" >/dev/null
echo "GPU library: PGO $pgo, LTO ${BB_LTO:-ON}"
# A failed GPU build must stop here: an older libbbgpu.so would otherwise be used silently.
ninja_jobs=()
if [[ -n ${BB_BUILD_JOBS:-} ]]; then ninja_jobs=(-j "$BB_BUILD_JOBS"); fi
if ! ninja "${ninja_jobs[@]}" -C out/gpu bbgpu > out/gpu-build.log 2>&1; then
    grep -v '^\[' out/gpu-build.log | tail -40 >&2
    echo 'GPU library build failed (full log: out/gpu-build.log)' >&2; exit 1
fi
# $ORIGIN/gpu: packaged copies keep the library next to the binary without patching it.
gpu=(-Lout/gpu -lbbgpu -Wl,-rpath,'$ORIGIN/gpu' -Wl,-rpath,"$PWD/out/gpu" -rdynamic)
runtime=(src/runtime*.c src/lan_match.c)
link=(-no-pie)
if [[ -n $windows ]]; then
    # out/gpu/bbgpu_link.txt (gpu/CMakeLists.txt): libbbgpu.a and its dependencies, one per line.
    mapfile -t gpu < out/gpu/bbgpu_link.txt
    runtime+=(src/win32_*.c)
    # The static library's link-time optimization (ThinLTO in lld) runs here; on Linux it runs in
    # the .so link.
    link=(-Wl,--disable-dynamicbase,--disable-high-entropy-va -lwinmm -lws2_32 -lpsapi -flto=thin)
fi
# Third-party decoders: compiled once, without this project's -Werror policy.
atrac9=(third_party/LibAtrac9/C/src/*.c)
if [[ ! -f out/libatrac9.a || -n $(find third_party/LibAtrac9/C/src -newer out/libatrac9.a -name '*.c') ]]; then
    rm -rf out/atrac9 && mkdir -p out/atrac9
    for source in "${atrac9[@]}"; do "$CC" -std=c99 -O2 -g -w -c "$source" -o "out/atrac9/$(basename "${source%.c}").o"; done
    ar rcs out/libatrac9.a out/atrac9/*.o
fi
# Windows: the link includes the GPU library's link-time optimization (a minute or more), and
# run.bat builds before every start, so an up-to-date executable is kept.
if [[ -n $windows && -f out/bb-probe.exe && -f out/bb-gpu-capabilities.exe &&
      -z $(find src gpu/bbgpu.h out/gpu/libbbgpu.a out/gpu/libbbnet.a out/gpu/bbgpu_link.txt out/libatrac9.a build.sh \
               tools/gpu_capabilities.c -newer out/bb-probe.exe -print -quit) ]]; then
    echo "Up to date: $PWD/out/bb-probe.exe"
else
"$CC" "${cstd[@]}" -O2 -g -Wall -Wextra -Werror "${threads[@]}" "${includes[@]}" -I. -Isrc src/probe.c "${runtime[@]}" src/vulkan_smoke.c out/libatrac9.a -lm "${gpu[@]}" "${libraries[@]}" "${link[@]}" -o out/bb-probe
echo "Built $PWD/out/bb-probe"
# GPU check for run.sh (live_resolution=auto): links only the Vulkan loader.
"$CC" "${cstd[@]}" -O2 -Wall -Wextra -Werror tools/gpu_capabilities.c "${libraries[@]}" -o out/bb-gpu-capabilities
fi
if [[ ${1:-} == --test ]]; then
    network_libraries=()
    file_mod_compat=()
    test_link=(-no-pie)
    if [[ -n $windows ]]; then
        network_libraries=(-lws2_32)
        file_mod_compat=(src/win32_compat.c)
        test_link=(-Wl,--disable-dynamicbase,--disable-high-entropy-va)
    fi
    "$CC" "${cstd[@]}" -O2 -g -Wall -Wextra -Werror "${threads[@]}" -I. -Isrc \
        tests/test_network.c src/runtime_network.c "${network_libraries[@]}" -o out/network-test
    out/network-test
    "$CC" "${cstd[@]}" -O2 -g -Wall -Wextra -Werror "${threads[@]}" -I. -Isrc \
        tests/test_lan_match.c src/lan_match.c "${network_libraries[@]}" -o out/lan-match-test
    out/lan-match-test
    "$CC" "${cstd[@]}" -O2 -g -Wall -Wextra -Werror "${threads[@]}" -I. -Isrc \
        tests/test_np_lan.c src/runtime_np_lan.c src/lan_match.c "${network_libraries[@]}" -o out/np-lan-test
    out/np-lan-test
    "$CC" "${cstd[@]}" -O2 -g -Wall -Wextra -Werror "${threads[@]}" -I. -Isrc \
        tests/test_np_lan_client.c src/runtime_np_lan.c src/lan_match.c "${network_libraries[@]}" -o out/np-lan-client-test
    out/np-lan-client-test
    "$CC" -std=c11 -O2 -g -Wall -Wextra -Werror -pthread "${includes[@]}" -I. -Isrc tests/test_pad.c "${libraries[@]}" -o out/pad-test
    out/pad-test
    "$CC" -std=c11 -O2 -g -Wall -Wextra -Werror -pthread -I. -Isrc tests/test_runtime.c "${runtime[@]}" out/libatrac9.a -lm "${gpu[@]}" "${libraries[@]}" "${test_link[@]}" -o out/runtime-test
    out/runtime-test
    "$CC" -std=c11 -O2 -g -Wall -Wextra -Werror -pthread -Isrc \
        tests/test_file_mods.c "${file_mod_compat[@]}" "${network_libraries[@]}" -o out/file-mods-test
    out/file-mods-test
    "$CC" -std=c11 -O2 -g -Wall -Wextra -Werror -pthread -I. -Isrc tests/test_sema.c "${runtime[@]}" out/libatrac9.a -lm "${gpu[@]}" "${libraries[@]}" "${test_link[@]}" -o out/sema-test
    out/sema-test
    "$CC" -std=c11 -D_GNU_SOURCE -O2 -g -Wall -Wextra -Werror -I. -Isrc tests/test_content.c src/runtime_content.c "${file_mod_compat[@]}" "${network_libraries[@]}" -o out/content-test
    out/content-test
fi
