#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
package=${1:-$root/build/ready}
mkdir -p "$root/build/smoke"
suite=$(mktemp -d "$root/build/smoke/display.XXXXXXXX")
LP_NUM_THREADS=${LP_NUM_THREADS:-4} OJK_SMOKE_ROOT="$suite" \
    OJK_SMOKE_DISPLAY=3840x2160 OJK_SMOKE_WAIT=10 OJK_SMOKE_TIMEOUT=600 \
    bash "$root/scripts/smoke-sp.sh" "$package" t2_wedge \
    +set r_mode -1 +set r_customwidth 3840 +set r_customheight 2160 \
    +set cg_fovAspectAdjust 1 +set r_picmip 2 +set r_norefresh 1 +exec display-smoke.cfg
images=("$suite"/t2_wedge.*/profile/OpenJK/screenshots/smoke.png)
size=$(ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=s=x:p=0 "${images[0]}")
[[ "$size" == 3840x2160 ]] || { printf 'FAIL: expected 3840x2160, got %s\n' "$size" >&2; exit 1; }
ffmpeg -hide_banner -i "${images[0]}" -vf blackframe=amount=99:threshold=8 -f null - > "$suite/pixels.log" 2>&1
if grep -q 'pblack:' "$suite/pixels.log"; then
    printf 'FAIL: screenshot is almost entirely black\n' >&2
    exit 1
fi
printf 'PASS: rendered 3840x2160 scene. Results: %s\n' "$suite"
