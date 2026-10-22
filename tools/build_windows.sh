#!/usr/bin/env bash
# Builds the desktop program for Windows x64 with clang-cl: tools/recompile.sh, then the game (CMakeLists.txt) into
# build/windows/PerfectDarkRecompiled.exe, beside the DLLs it needs (SDL2, dxcompiler, dxil). Run tools/build_decomp.sh
# first, in WSL2 on the same checkout, which makes the relink the code is recompiled from (docs/building.md, Windows).
# From Git Bash, anywhere in the checkout:
#
#     tools/build_windows.sh [cpus]
#
# On PATH: clang-cl and lld-link, CMake 3.20-3.31 and Ninja, and wsl, whose default distro has clang, ld.lld and make
# for patches/ (CMakeLists.txt, PATCHES_MAKE). clang-cl finds the MSVC libraries and the Windows SDK by itself where
# Visual Studio or its Build Tools are installed, or else from INCLUDE and LIB.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CPUS=${1:-$(nproc)}
cd "$ROOT"
for tool in clang-cl lld-link cmake ninja wsl; do
    command -v "$tool" > /dev/null || { echo "$tool isn't on PATH (tools/build_windows.sh says what the build needs)" >&2; exit 1; }
done
wsl which clang ld.lld make > /dev/null || { echo "WSL has no clang, ld.lld or make, which patches/ needs" >&2; exit 1; }
git submodule update --init --recursive lib/N64ModernRuntime lib/rt64 lib/RecompFrontend lib/N64Recomp-browser
tools/recompile.sh -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl
mkdir -p build
cmake -S . -B build/windows -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_COMPILER=clang-cl \
    -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_LINKER=lld-link > build/configure-windows.log 2>&1 ||
    { tail -30 build/configure-windows.log; exit 1; }
ninja -C build/windows -j"$CPUS" PerfectDarkRecompiled
