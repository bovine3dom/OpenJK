#!/usr/bin/env bash
set -euo pipefail

config=${OJK_DESKTOP_CONFIG:-${XDG_CONFIG_HOME:-$HOME/.config}/openjk-desktop.conf}
if [[ ${1:-} == --help ]]; then
    printf 'Usage: %s --configure SSH_HOST /path/to/GameData [/path/to/GameData_JO]\n       %s --configure-jo /path/to/GameData_JO\n       %s [--worktree NAME] [--campaign ja|jo] [--new-game] [--desktop|--resolution WIDTHxHEIGHT] [--atmosphere-review [all]|--atmosphere-edit] [engine arguments]\nConfiguration: %s\n' "$0" "$0" "$0" "$config"
    exit 0
fi
if [[ -f "$config" ]]; then
    # This is a trusted, local Bash configuration file.
    source "$config"
fi
remote_root=${OJK_REMOTE_ROOT:-/home/olie/projects/OpenJK}
desktop=${OJK_DESKTOP_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/openjk-playtest}
profile=${OJK_PROFILE:-${XDG_DATA_HOME:-$HOME/.local/share}/openjk-dev}
if [[ ${1:-} == --configure ]]; then
    [[ $# == 3 || $# == 4 ]] || { printf 'Use --configure SSH_HOST /path/to/GameData [/path/to/GameData_JO]\n' >&2; exit 1; }
    configured_assets=$(realpath -e -- "$3")
    jo_assets=${4:-${OJK_JO_ASSETS:-}}
    if [[ -n $jo_assets ]]; then jo_assets=$(realpath -e -- "$jo_assets"); fi
    mkdir -p -- "$(dirname -- "$config")"
    umask 077
    printf 'OJK_HOST=%q\nOJK_ASSETS=%q\nOJK_REMOTE_ROOT=%q\nOJK_DESKTOP_DIR=%q\nOJK_PROFILE=%q\n' \
        "$2" "$configured_assets" "$remote_root" "$desktop" "$profile" > "$config"
    printf 'OJK_JO_ASSETS=%q\n' "$jo_assets" >> "$config"
    printf 'Configuration written to %s\n' "$config"
    exit 0
fi
if [[ ${1:-} == --configure-jo ]]; then
    [[ $# == 2 && -f $config ]] || { printf 'Configure the SSH host and Academy assets first, then use --configure-jo /path/to/GameData_JO\n' >&2; exit 1; }
    jo_assets=$(realpath -e -- "$2")
    printf 'OJK_JO_ASSETS=%q\n' "$jo_assets" >> "$config"
    printf 'JO asset path written to %s\n' "$config"
    exit 0
fi
worktree=
campaign=ja
while [[ ${1:-} == --worktree || ${1:-} == --campaign ]]; do
if [[ $1 == --campaign ]]; then
    campaign=${2:-}
    [[ $campaign == ja || $campaign == jo ]] || { printf 'Use --campaign ja|jo\n' >&2; exit 1; }
    shift 2
else
    [[ -z $worktree ]] || { printf 'Use --worktree only once\n' >&2; exit 1; }
    [[ ${2:-} =~ ^[a-zA-Z0-9_][a-zA-Z0-9_./-]*$ && $2 != *..* && $2 != */ && $2 != *//* ]] || {
        printf 'Use --worktree NAME with letters, digits, underscores, dots, hyphens, or slashes\n' >&2
        exit 1
    }
    worktree=${2//\//-}
    desktop="${desktop%/}/worktrees/openjk-$worktree"
    profile="${profile%/}/worktrees/openjk-$worktree"
    shift 2
fi
done
host=${OJK_HOST:?Run --configure SSH_HOST /path/to/GameData first}
[[ "$host" =~ ^[a-zA-Z0-9_][a-zA-Z0-9_.@-]*$ ]] || { printf 'Use an SSH host alias or user@hostname\n' >&2; exit 1; }
[[ "$remote_root" == /* && "$remote_root" != *[$'\r\n']* ]] || { printf 'OJK_REMOTE_ROOT must be an absolute, single-line path\n' >&2; exit 1; }
assets=$(realpath -e -- "${OJK_ASSETS:?Set OJK_ASSETS to the desktop GameData directory}")
profile=$(realpath -m -- "$profile")
destination=$(realpath -m -- "$desktop/build")
asset_paths=("$assets" "$profile")
if [[ $campaign == jo ]]; then
    OJK_JO_ASSETS=$(realpath -e -- "${OJK_JO_ASSETS:?Run --configure-jo /path/to/GameData_JO first}")
    export OJK_JO_ASSETS
    asset_paths+=("$OJK_JO_ASSETS")
    for index in 0 1 2 5; do
        test -r "$OJK_JO_ASSETS/base/assets$index.pk3" || { printf 'Missing JO assets%s.pk3\n' "$index" >&2; exit 1; }
    done
fi
for path in "${asset_paths[@]}"; do
    if [[ "$path/" == "$destination/"* || "$destination/" == "$path/"* ]]; then
        printf 'The managed build must be separate from assets and profiles: %s\n' "$destination" >&2
        exit 1
    fi
done
if [[ "$(realpath -- "${BASH_SOURCE[0]}")" == "$destination/"* ]]; then
    printf 'Install this updater outside the managed build directory\n' >&2
    exit 1
fi
for index in 0 1 2 3; do
    test -r "$assets/base/assets$index.pk3" || { printf 'Missing assets%s.pk3\n' "$index" >&2; exit 1; }
done
mkdir -p -- "$destination"
exec 9>>"$destination/.play-lock"
flock -n 9 || { printf 'This build is already running or updating\n' >&2; exit 1; }
if [[ ! -f "$destination/.openjk-managed" ]]; then
    shopt -s dotglob nullglob
    for path in "$destination"/*; do
        [[ "$path" == "$destination/.play-lock" ]] || {
            printf 'Refusing to replace a nonempty, unmanaged directory: %s\n' "$destination" >&2
            exit 1
        }
    done
    printf 'OpenJK desktop build\n' > "$destination/.openjk-managed"
fi

# Resolve once: a new server publication must not change the source mid-transfer.
quoted_root=${remote_root//\'/\'\\\'\'}
remote=$(ssh -- "$host" "sh -s -- '$quoted_root' '$worktree' '$campaign'" <<'REMOTE'
set -eu
root=$(realpath -e -- "$1")
if [ -n "$2" ]; then
    root=$(realpath -e -- "$root/../worktrees/openjk-$2")
fi
package=$(realpath -e -- "$root/build/ready")
case "$package" in "$root/build/packages/"*) ;; *) exit 1 ;; esac
for file in openjk_sp.x86_64 rdsp-vanilla_x86_64.so OpenJK/jagamex86_64.so launch-sp.sh build-id.txt; do
    test -s "$package/$file"
done
test "$(cat "$package/build-id.txt")" = "${package##*/}"
grep -Fxq 'PASS: t1_sour' "$package/smoke-result.txt"
if [ "$3" = jo ]; then
    test -s "$package/import-jo.py"
    test -s "$package/jo-patches.json"
fi
printf '%s\n' "$package"
REMOTE
) || { printf 'Cannot resolve a published, smoke-tested build on %s\n' "$host" >&2; exit 1; }
[[ "$remote" == /* && "$remote" != *[$'\r\n']* ]] || { printf 'Invalid package path from server\n' >&2; exit 1; }
printf 'Updating %s from %s:%s/\n' "$destination" "$host" "$remote"
touch "$destination/.update-incomplete"
rsync -a --secluded-args --no-whole-file --modify-window=-1 \
    --delay-updates --delete-delay --partial-dir=.rsync-partial \
    --exclude=/.play-lock --exclude=/.openjk-managed --exclude=/.update-incomplete \
    --info=stats2,name1 -e ssh -- "$host:$remote/" "$destination/"
for file in openjk_sp.x86_64 rdsp-vanilla_x86_64.so OpenJK/jagamex86_64.so launch-sp.sh; do
    test -s "$destination/$file"
done
[[ $(<"$destination/build-id.txt") == "${remote##*/}" ]]
test -x "$destination/openjk_sp.x86_64"
rm -- "$destination/.update-incomplete"
export OJK_PROFILE="$profile"
# The launcher inherits descriptor 9 and keeps the same lock through game exit.
exec bash "$destination/launch-sp.sh" "$assets" --campaign "$campaign" "$@"
