#!/usr/bin/env bash
# Runs the program tools/build_linux.sh built where there is no display: in the image tools/linux_env.sh names, on
# Xvfb, with Mesa's lavapipe on the CPU (tools/linux_env.sh) and SDL's disk audio
# driver in place of a sound device, from the checkout (assets/ and recompcontrollerdb.txt are read from there). It stores the ROM (--rom), starts the game (--boot) and writes a
# screenshot every few seconds to build/shots/, the log to build/run.log and the sound to build/sound.wav. The
# program's data goes to build/home.
#
#     tools/run_linux.sh path/to/pd.z64 [seconds, 60] [seconds between shots, 5]
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
ROM=$(realpath "$1")
SECS=${2:-60}
STEP=${3:-5}
source "$ROOT/tools/linux_env.sh"
rm -rf "$ROOT/build/shots"
mkdir -p "$ROOT/build/shots" "$ROOT/build/home"
docker run --rm -u "$(id -u):$(id -g)" -v "$ROOT:/src" -v "$ROM:/rom.z64:ro" -w /src -e HOME=/src/build/home \
    -e SDL_AUDIODRIVER=disk -e SDL_DISKAUDIOFILE=/src/build/sound.raw "${PD_GPU_ARGS[@]}" "$PD_RUN_IMAGE" bash -uo pipefail -c "
    Xvfb :99 -screen 0 1280x960x24 -nolisten tcp > /dev/null 2>&1 &
    export DISPLAY=:99
    sleep 1
    build/linux/PerfectDarkRecompiled --rom /rom.z64 --boot --window 1280x960 > /src/build/run.log 2>&1 &
    pid=\$!
    for ((t = $STEP; t <= $SECS; t += $STEP)); do
        sleep $STEP
        kill -0 \$pid 2> /dev/null || { echo \"exited after about \${t}s\" >> /src/build/run.log; break; }
        import -window root /src/build/shots/\$(printf %03d \$t).png
    done
    # It may not stop on SIGTERM.
    kill \$pid 2> /dev/null; sleep 2; kill -9 \$pid 2> /dev/null; wait \$pid; echo \"exit \$?\" >> /src/build/run.log
"
python3 -I "$ROOT/tools/raw_to_wav.py" "$ROOT/build/sound.raw" "$ROOT/build/sound.wav"
rm "$ROOT/build/sound.raw"
tail -20 "$ROOT/build/run.log"
ls "$ROOT/build/shots"
