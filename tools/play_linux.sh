#!/usr/bin/env bash
# Plays the program tools/build_linux.sh built, by hand, where there is no display: tools/run_linux.sh's container
# (tools/linux_env.sh: the host's GPU through Mesa's Dozen on WSL2, else lavapipe), left running between commands, so
# keys can be held and the screen and the game's variables looked at whenever. Everything goes to build/play/: the
# log (run.log), screenshots, and the sound (sound.wav, written at stop). The program's data, the saves included,
# goes to build/home, which a later start finds again.
#
#     tools/play_linux.sh start path/to/pd.z64 [WxH]  start the game (the launcher is skipped) in a window of WxH,
#                                                 1280x960 by default (1280x720 for widescreen)
#     tools/play_linux.sh hold <ms> <key>...      hold keys together for ms, then let go (xdotool's key names: Return,
#                                                 space, Shift_L, w, a, s, d, Up, ...; the bindings are src/mouse.cpp's)
#     tools/play_linux.sh mouse <dx> <dy> [n]     move the mouse by dx, dy, n times a frame apart (1)
#     tools/play_linux.sh click <ms> <button>     hold a mouse button for ms (1 left, 3 right; 4 and 5 the wheel, 0 ms)
#     tools/play_linux.sh shot <name>             a screenshot, build/play/<name>.png
#     tools/play_linux.sh peek <expr>...          the game's variables (tools/peek.py)
#     tools/play_linux.sh stop                    stop it, and write build/play/sound.wav
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NAME=pd-play-$(echo "$ROOT" | sha1sum | cut -c1-8)
cmd=${1:?usage: tools/play_linux.sh start|hold|shot|peek|stop ...}
shift
in_container() { docker exec -e DISPLAY=:99 "$NAME" "$@"; }

case "$cmd" in
start)
    ROM=$(realpath "${1:?the ROM}"); SIZE=${2:-1280x960}; source "$ROOT/tools/linux_env.sh"
    docker rm -f "$NAME" > /dev/null 2>&1 || true
    rm -rf "$ROOT/build/play"; mkdir -p "$ROOT/build/play" "$ROOT/build/home"
    docker run -d --name "$NAME" --cap-add SYS_PTRACE -u "$(id -u):$(id -g)" -v "$ROOT:/src" -v "$ROM:/rom.z64:ro" \
        -w /src -e HOME=/src/build/home -e SDL_AUDIODRIVER=disk -e SDL_DISKAUDIOFILE=/src/build/play/sound.raw \
        "${PD_GPU_ARGS[@]}" "$PD_RUN_IMAGE" bash -c "
            Xvfb :99 -screen 0 ${SIZE}x24 -nolisten tcp > /dev/null 2>&1 &
            sleep 1
            DISPLAY=:99 exec build/linux/PerfectDarkRecompiled --rom /rom.z64 --boot --window $SIZE > build/play/run.log 2>&1
        " > /dev/null
    echo "started $NAME"
    ;;
hold)
    ms=${1:?ms}; shift
    in_container bash -c 'xdotool keydown "$@"; sleep '"$(awk "BEGIN{print $ms/1000}")"'; xdotool keyup "$@"' _ "$@"
    ;;
mouse)
    in_container bash -c 'for i in $(seq "$3"); do xdotool mousemove_relative -- "$1" "$2"; sleep 0.016; done' _ "${1:?dx}" "${2:?dy}" "${3:-1}"
    ;;
click)
    in_container bash -c 'xdotool mousedown "$2"; sleep '"$(awk "BEGIN{print ${1:?ms}/1000}")"'; xdotool mouseup "$2"' _ "$1" "${2:?button}"
    ;;
shot)
    in_container import -window root "/src/build/play/${1:?name}.png"
    echo "build/play/$1.png"
    ;;
peek)
    # As root, whose SYS_PTRACE (start) lets it read the game's memory where Yama allows only a parent to.
    docker exec -u 0 "$NAME" python3 -I tools/peek.py "$@"
    ;;
stop)
    docker rm -f "$NAME" > /dev/null
    if [[ -f "$ROOT/build/play/sound.raw" ]]; then
        python3 -I "$ROOT/tools/raw_to_wav.py" "$ROOT/build/play/sound.raw" "$ROOT/build/play/sound.wav"
        rm "$ROOT/build/play/sound.raw"
    fi
    tail -5 "$ROOT/build/play/run.log"
    ;;
*)
    echo "no such command: $cmd" >&2; exit 1
    ;;
esac
