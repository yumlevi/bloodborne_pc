#!/usr/bin/env bash
set -euo pipefail

cd -- "$(dirname -- "$0")/.."
runtime_dir=${1:-runtime}
clang_bin=${2:-/clang64/bin}
mkdir -p "$runtime_dir"

# Keep all traversal state local to this fresh process. In particular, a previous interactive
# shell's associative arrays must never suppress dependencies on a later run.
unset seen queue unresolved copied
declare -A seen=()
declare -A unresolved=()
declare -A copied=()
declare -a queue=("out/bb-probe.exe" "out/bb-gpu-capabilities.exe")

for executable in "${queue[@]}"; do
    if [[ ! -f $executable ]]; then
        echo "Missing executable: $executable" >&2
        exit 1
    fi
done

while ((${#queue[@]})); do
    file=${queue[0]}
    queue=("${queue[@]:1}")
    while IFS= read -r dll; do
        [[ -n $dll ]] || continue
        key=${dll,,}
        if [[ -n ${seen[$key]+set} ]]; then continue; fi
        seen[$key]=1

        source="$clang_bin/$dll"
        destination="$runtime_dir/$dll"
        if [[ $key == vulkan-1.dll || $key == api-ms-win-*.dll || $key == ext-ms-win-*.dll ||
              -f "/c/Windows/System32/$dll" ]]; then
            : # Windows or the installed GPU driver supplies it, even if MSYS2 has a copy.
        elif [[ -f $source ]]; then
            cp -f -- "$source" "$destination"
            copied[$key]=$dll
            queue+=("$destination")
        else
            unresolved[$key]=$dll
        fi
    done < <(llvm-objdump -p "$file" | sed -n 's/^[[:space:]]*DLL Name: //p')
done

printf 'Runtime DLL closure: %d file(s) copied to %s\n' "${#copied[@]}" "$runtime_dir"
if ((${#unresolved[@]})); then
    printf 'Unresolved non-system DLL(s):\n' >&2
    printf '  %s\n' "${unresolved[@]}" | sort -f >&2
    exit 2
fi
echo 'Unresolved non-system DLLs: none'
