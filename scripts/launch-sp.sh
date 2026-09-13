#!/usr/bin/env bash
set -euo pipefail

package=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# Reuse the updater's lock, or take it when the launcher is run directly.
if [[ ! "$package/.play-lock" -ef /proc/self/fd/9 ]]; then
    exec 9>>"$package/.play-lock"
fi
flock -n 9 || { printf 'This build is already running or updating\n' >&2; exit 1; }
if [[ -e "$package/.update-incomplete" ]]; then
    printf 'The last update did not finish. Run the desktop updater again\n' >&2
    exit 1
fi
assets=$(realpath -- "${1:?Usage: launch-sp.sh /path/to/GameData [engine arguments]}")
shift
for index in 0 1 2 3; do
    test -r "$assets/base/assets$index.pk3" || { printf 'Missing assets%s.pk3\n' "$index" >&2; exit 1; }
done
profile=${OJK_PROFILE:-${XDG_DATA_HOME:-$HOME/.local/share}/openjk-dev}
mkdir -p -- "$profile"
profile=$(realpath -- "$profile")
display=()
if [[ ! -f "$profile/OpenJK/openjk_sp.cfg" && ! -f "$profile/base/openjk_sp.cfg" ]]; then
    display=(+set r_mode -2 +set r_fullscreen 1 +set cg_fovAspectAdjust 1)
fi
case ${1:-} in
    --desktop)
        display=(+set r_mode -2 +set r_fullscreen 1 +set cg_fovAspectAdjust 1)
        shift
        ;;
    --resolution)
        if [[ ! ${2:-} =~ ^([1-9][0-9]{1,4})x([1-9][0-9]{1,4})$ ]]; then
            printf 'Use --resolution WIDTHxHEIGHT\n' >&2
            exit 1
        fi
        width=${BASH_REMATCH[1]}
        height=${BASH_REMATCH[2]}
        if (( width < 64 || width > 16384 || height < 64 || height > 16384 )); then
            printf 'Display dimensions must be between 64 and 16384\n' >&2
            exit 1
        fi
        display=(+set r_mode -1 +set r_customwidth "$width" +set r_customheight "$height" +set r_fullscreen 1 +set cg_fovAspectAdjust 1)
        shift 2
        ;;
esac
printf 'Package: %s\nProfile: %s\n' "$package" "$profile"
if [[ -f "$package/build-id.txt" ]]; then
    cat -- "$package/build-id.txt"
fi
cd -- "$package"
exec ./openjk_sp.x86_64 \
    +set fs_basepath "$package" +set fs_cdpath "$assets" \
    +set fs_homepath "$profile" +set fs_game OpenJK "${display[@]}" "$@"
