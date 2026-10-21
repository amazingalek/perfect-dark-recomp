#!/usr/bin/env bash
# Runs the program tools/build_linux.sh built where there is no display: in its image, on Xvfb, with Mesa's lavapipe
# (Vulkan on the CPU) and no sound device. It stores the ROM (--rom), starts the game (--boot) and writes a screenshot
# every few seconds to build/shots/, the log to build/run.log. The program's data goes to build/home.
#
#     tools/run_linux.sh path/to/pd.z64 [seconds, 60] [seconds between shots, 5]
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
ROM=$(realpath "$1")
SECS=${2:-60}
STEP=${3:-5}
IMAGE=pd-linux:$(sha256sum "$ROOT/tools/docker/Dockerfile.linux" | cut -c1-12)
rm -rf "$ROOT/build/shots"
mkdir -p "$ROOT/build/shots" "$ROOT/build/home"
docker run --rm -u "$(id -u):$(id -g)" -v "$ROOT:/src" -v "$ROM:/rom.z64:ro" -w /src/build/linux -e HOME=/src/build/home \
    -e SDL_AUDIODRIVER=dummy -e VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json "$IMAGE" bash -uo pipefail -c "
    Xvfb :99 -screen 0 1280x960x24 -nolisten tcp > /dev/null 2>&1 &
    export DISPLAY=:99
    sleep 1
    ./PerfectDarkRecompiled --rom /rom.z64 --boot --window 1280x960 > /src/build/run.log 2>&1 &
    pid=\$!
    for ((t = $STEP; t <= $SECS; t += $STEP)); do
        sleep $STEP
        kill -0 \$pid 2> /dev/null || { echo \"exited after about \${t}s\" >> /src/build/run.log; break; }
        import -window root /src/build/shots/\$(printf %03d \$t).png
    done
    kill \$pid 2> /dev/null; wait \$pid; echo \"exit \$?\" >> /src/build/run.log
"
tail -20 "$ROOT/build/run.log"
ls "$ROOT/build/shots"
