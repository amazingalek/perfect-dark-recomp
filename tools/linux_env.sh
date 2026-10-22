# Sourced by tools/run_linux.sh and tools/play_linux.sh: the image the program runs in and how it draws.
#
# PD_RUN_IMAGE is tools/docker/Dockerfile.run on top of the build image (tools/build_linux.sh's), built here the first
# time, tagged with a hash of both files. PD_GPU_ARGS has the container draw with Mesa's lavapipe, on the CPU, which
# draws the title at a few frames a second and a level at a few a minute. It never takes the host's GPU, WSL2's
# /dev/dxg through Mesa's Dozen included: a device lost there left the program stuck in the host's GPU driver, where
# nothing could stop it, and froze the whole Windows host until it restarted. On a GPU the game plays natively, on
# Windows (docs/building.md), or as its page.
PD_BUILD_IMAGE=pd-linux:$(sha256sum "$ROOT/tools/docker/Dockerfile.linux" | cut -c1-12)
PD_RUN_IMAGE=pd-run:$(cat "$ROOT/tools/docker/Dockerfile.linux" "$ROOT/tools/docker/Dockerfile.run" | sha256sum | cut -c1-12)
docker image inspect "$PD_BUILD_IMAGE" > /dev/null 2>&1 ||
    docker build -t "$PD_BUILD_IMAGE" -f "$ROOT/tools/docker/Dockerfile.linux" "$ROOT/tools/docker"
docker image inspect "$PD_RUN_IMAGE" > /dev/null 2>&1 ||
    docker build -t "$PD_RUN_IMAGE" --build-arg BASE="$PD_BUILD_IMAGE" -f "$ROOT/tools/docker/Dockerfile.run" "$ROOT/tools/docker"
PD_GPU_ARGS=(-e VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json)
