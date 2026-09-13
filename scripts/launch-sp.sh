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
printf 'Package: %s\nProfile: %s\n' "$package" "$profile"
if [[ -f "$package/build-id.txt" ]]; then
    cat -- "$package/build-id.txt"
fi
cd -- "$package"
exec ./openjk_sp.x86_64 \
    +set fs_basepath "$package" +set fs_cdpath "$assets" \
    +set fs_homepath "$profile" +set fs_game OpenJK "$@"
