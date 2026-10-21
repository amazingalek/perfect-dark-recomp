# Sourced by tools/run_linux.sh and tools/play_linux.sh: the image the program runs in and how it reaches a GPU.
#
# PD_RUN_IMAGE is tools/docker/Dockerfile.run on top of the build image (tools/build_linux.sh's), built here the first
# time, tagged with a hash of both files. PD_GPU_ARGS gives the container a GPU: on WSL2 (/dev/dxg) the host's,
# through Mesa's Dozen; elsewhere Mesa's lavapipe, on the CPU, which draws the title at a few frames a second and a
# level at a few a minute. PD_GPU=lavapipe takes lavapipe on WSL2 too.
PD_BUILD_IMAGE=pd-linux:$(sha256sum "$ROOT/tools/docker/Dockerfile.linux" | cut -c1-12)
PD_RUN_IMAGE=pd-run:$(cat "$ROOT/tools/docker/Dockerfile.linux" "$ROOT/tools/docker/Dockerfile.run" | sha256sum | cut -c1-12)
docker image inspect "$PD_BUILD_IMAGE" > /dev/null 2>&1 ||
    docker build -t "$PD_BUILD_IMAGE" -f "$ROOT/tools/docker/Dockerfile.linux" "$ROOT/tools/docker"
docker image inspect "$PD_RUN_IMAGE" > /dev/null 2>&1 ||
    docker build -t "$PD_RUN_IMAGE" --build-arg BASE="$PD_BUILD_IMAGE" -f "$ROOT/tools/docker/Dockerfile.run" "$ROOT/tools/docker"
if [[ -e /dev/dxg && "${PD_GPU:-}" != lavapipe ]]; then
    PD_GPU_ARGS=(--device /dev/dxg -v /usr/lib/wsl:/usr/lib/wsl:ro -e LD_LIBRARY_PATH=/usr/lib/wsl/lib
        -e VK_ICD_FILENAMES=/opt/dozen/share/vulkan/icd.d/dzn_icd.x86_64.json)
else
    PD_GPU_ARGS=(-e VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json)
fi
