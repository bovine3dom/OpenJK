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
if [[ ${1:-} == --barks ]]; then
    shift
    exec python3 "$package/audition-barks.py" --assets "$assets/base" "$@"
fi
for index in 0 1 2 3; do
    test -r "$assets/base/assets$index.pk3" || { printf 'Missing assets%s.pk3\n' "$index" >&2; exit 1; }
done
profile=${OJK_PROFILE:-${XDG_DATA_HOME:-$HOME/.local/share}/openjk-dev}
campaign=ja
start_map=yavin1
if [[ ${1:-} == --campaign ]]; then
    campaign=${2:-}
    [[ $campaign == ja || $campaign == jo ]] || { printf 'Use --campaign ja|jo\n' >&2; exit 1; }
    shift 2
fi
campaign_args=(+set com_outcast 0)
if [[ $campaign == jo ]]; then
    campaign_args=(+set com_outcast 1)
    start_map=kejim_post
fi
display=()
review=
atmosphere_edit=false
while [[ $# -gt 0 ]]; do
case $1 in
    --jolt-demo)
        profile="${profile%/}/jolt-demo"
        campaign_args+=(+exec jolt-demo.cfg)
        shift
        ;;
    --atmosphere-edit)
        atmosphere_edit=true
        shift
        ;;
    --atmosphere-review)
        review=profiles
        if [[ ${2:-} == all ]]; then review=all; shift; fi
        shift
        ;;
    --new-game)
        campaign_args+=(+map "$start_map")
        shift
        ;;
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
    *) break ;;
esac
done
if [[ -n $review ]]; then profile="${profile%/}/atmosphere-review"; fi
if [[ $campaign == jo ]]; then
    profile="${profile%/}/campaigns/jo"
    jo_assets=$(realpath -e -- "${OJK_JO_ASSETS:?Set OJK_JO_ASSETS to the Jedi Outcast GameData directory}")
    "$package/openjk-import-jo" "$assets" "$jo_assets" "$profile"
fi
mkdir -p -- "$profile"
profile=$(realpath -- "$profile")
if [[ ${#display[@]} == 0 && ! -f "$profile/OpenJK/openjk_sp.cfg" && ! -f "$profile/base/openjk_sp.cfg" ]]; then
    display=(+set r_mode -2 +set r_fullscreen 1 +set cg_fovAspectAdjust 1)
fi
if [[ -n $review ]]; then
    python3 "$package/setup-atmosphere-review.py" "$profile" "$campaign"
    suffix=
    if [[ $review == all ]]; then suffix=-all; fi
    campaign_args+=(+set cl_renderer rdsp-rend2 +exec "atmosphere-review-$campaign$suffix.cfg")
elif $atmosphere_edit; then
    python3 "$package/atmosphere_profiles.py" "$profile" "$campaign"
    campaign_args+=(+set cl_renderer rdsp-rend2 +set r_atmosphere 1 +exec atmosphere-edit-paths.cfg)
fi
printf 'Package: %s\nProfile: %s\n' "$package" "$profile"
if [[ -f "$package/build-id.txt" ]]; then
    cat -- "$package/build-id.txt"
fi
cd -- "$package"
exec ./openjk_sp.x86_64 \
    +set fs_basepath "$package" +set fs_cdpath "$assets" \
    +set fs_homepath "$profile" +set fs_game OpenJK \
    +set g_dismemberment 3 +set g_dismemberProbabilities 0 +set broadsword 1 \
    "${display[@]}" "${campaign_args[@]}" "$@"
