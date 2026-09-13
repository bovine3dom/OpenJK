#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
package=$(realpath -- "${1:?Usage: smoke-sp.sh PACKAGE [MAP] [engine arguments]}")
map=${2:-t1_sour}
shift "$(( $# > 1 ? 2 : 1 ))"
[[ "$map" =~ ^[a-zA-Z0-9_]+$ ]] || { printf 'Invalid map name\n' >&2; exit 1; }
assets=${OJK_ASSETS:-$root/GameData}
output=${OJK_SMOKE_ROOT:-$root/build/smoke}
mkdir -p -- "$output"
run=$(mktemp -d "$output/$map.XXXXXXXX")
printf 'Smoke-test output: %s\n' "$run"

# A fresh profile prevents a stale screenshot from passing a failed run.
command=(timeout --kill-after=5s 120s xvfb-run -a -s '-screen 0 640x480x24' \
    env LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=1 SDL_AUDIODRIVER=dummy \
    OJK_PROFILE="$run/profile" bash "$package/launch-sp.sh" "$assets" \
    +safe +set r_fullscreen 0 +set r_mode 3 +set r_swapInterval 0 \
    +set com_maxfps 60 +set s_initsound 0 +set developer 1 \
    +set logfile 2 +devmap "$map" "$@" \
    +wait 100 +screenshot_png smoke +wait 10 +quit)
printf '%q ' "${command[@]}" > "$run/command.txt"
if ! "${command[@]}" > "$run/console.log" 2>&1; then
    printf 'FAIL: process failed or timed out. See %s/console.log\n' "$run" >&2
    exit 1
fi

image="$run/profile/OpenJK/screenshots/smoke.png"
if grep -Eq "Can't find map |ERROR:|Error:|Failed to load|Couldn't load|couldn't exec|Unknown command" "$run/console.log" ||
    ! grep -Fq "CM_LoadMap( maps/$map.bsp, 1 )" "$run/console.log" ||
    ! test -s "$image"; then
    printf 'FAIL: map-load or screenshot check failed. See %s/console.log\n' "$run" >&2
    exit 1
fi
ffmpeg -v error -xerror -i "$image" -frames:v 1 -f null - > "$run/image-check.log" 2>&1
printf 'PASS: %s\n' "$map" | tee "$run/result.txt"
