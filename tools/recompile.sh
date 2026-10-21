#!/usr/bin/env bash
# Generates the recompiled code from the relink tools/build_decomp.sh made (build/relink/stage1.bin) and syms/:
# RecompiledFuncs/ with N64Recomp (us.toml), and rsp/aspMain.cpp, the audio microcode, with RSPRecomp (aspMain.us.toml). It is wiped and written again, and the game's CMake configure needs it; a
# file that comes out the same as before keeps its old time, so a build recompiles only the ones that changed. The
# recompiler is the N64Recomp the runtime pins (lib/N64ModernRuntime/N64Recomp) with N64Recomp-browser's patches/
# applied (patches/apply.cmake, which does nothing if they are), built into build/n64recomp the first time; arguments
# go to that configure. patches/ is recompiled by the game's own build (CMakeLists.txt).
#
# Usage: tools/recompile.sh [cmake arguments, e.g. -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl]
set -euo pipefail
cd "$(dirname "$0")/.."
[[ -f build/relink/stage1.bin ]] || { echo "no build/relink/stage1.bin: run tools/build_decomp.sh first" >&2; exit 1; }
cmake -P lib/N64Recomp-browser/patches/apply.cmake lib
bin=build/n64recomp
mkdir -p build
if [[ ! -f "$bin/build.ninja" ]]; then
    cmake -S lib/N64ModernRuntime/N64Recomp -B "$bin" -G Ninja -DCMAKE_BUILD_TYPE=Release "$@" > "$bin.log" 2>&1 ||
        { tail -20 "$bin.log"; exit 1; }
fi
ninja -C "$bin" N64RecompCLI RSPRecomp >> "$bin.log" 2>&1 || { tail -20 "$bin.log"; exit 1; }
old="$(mktemp -d)"
trap 'rm -rf "$old"' EXIT
mkdir -p RecompiledFuncs
mv RecompiledFuncs "$old/"
mkdir RecompiledFuncs
"$bin/N64Recomp" us.toml > build/recomp.log 2>&1 || { tail -20 build/recomp.log; exit 1; }
for f in RecompiledFuncs/*; do
    prev="$old/RecompiledFuncs/${f#RecompiledFuncs/}"
    if cmp -s "$f" "$prev"; then touch -r "$prev" "$f"; fi
done
echo "RecompiledFuncs/: $(ls RecompiledFuncs | wc -l) files"
mkdir -p rsp
"$bin/RSPRecomp" aspMain.us.toml > build/rsp_recomp.log 2>&1 || { tail -20 build/rsp_recomp.log; exit 1; }
echo "rsp/aspMain.cpp: $(wc -l < rsp/aspMain.cpp) lines"
