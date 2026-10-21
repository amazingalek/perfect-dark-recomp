#!/usr/bin/env bash
# Builds the decompilation (lib/perfect_dark) as ntsc-final from the player's ROM, in the image tools/docker/Dockerfile,
# and checks it matches: the decomp's own `make test` (its checksums.ntsc-final.md5, every segment) and the built
# ROM's MD5 against the ROM's. Linux with Docker; run from anywhere in the checkout:
#
#     tools/build_decomp.sh path/to/pd.z64 [cpus]
#
# The decomp writes its build/, extracted/ and src/assets/ inside lib/perfect_dark, which its own .gitignore ignores,
# so the submodule stays clean; the ROM is copied there as pd.ntsc-final.z64 (ignored too). `make extract` runs only
# the first time (extracted/ntsc-final/ missing): it writes nothing over an asset that is already there.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
ROM=$(realpath "$1")
CPUS=${2:-$(nproc)}
MD5=e03b088b6ac9e0080440efed07c1e40f

[ "$(md5sum < "$ROM" | cut -d' ' -f1)" = "$MD5" ] || { echo "$ROM is not Perfect Dark (U) (V1.1), MD5 $MD5" >&2; exit 1; }
git -C "$ROOT" submodule update --init --recursive lib/perfect_dark

IMAGE=pd-decomp:$(sha256sum "$ROOT/tools/docker/Dockerfile" | cut -c1-12)
docker image inspect "$IMAGE" >/dev/null 2>&1 || docker build -t "$IMAGE" "$ROOT/tools/docker"

cp "$ROM" "$ROOT/lib/perfect_dark/pd.ntsc-final.z64"
docker run --rm -u "$(id -u):$(id -g)" -v "$ROOT:/src" -w /src/lib/perfect_dark -e ROMID=ntsc-final "$IMAGE" bash -euo pipefail -c "
    [ -f extracted/ntsc-final/lib.bin ] || make extract
    make -j$CPUS rom
    make -j$CPUS test
    echo \"\$(md5sum < build/ntsc-final/pd.z64 | cut -d' ' -f1) build/ntsc-final/pd.z64\"
    [ \"\$(md5sum < build/ntsc-final/pd.z64 | cut -d' ' -f1)\" = $MD5 ]
    echo 'The decompilation matches the ROM.'
"
