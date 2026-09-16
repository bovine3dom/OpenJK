#!/usr/bin/env bash
set -euo pipefail

integration=false
local_sky_assets=false
for option in "$@"; do
    case "$option" in
        --integration) integration=true ;;
        --local-sky-assets) local_sky_assets=true ;;
        *) printf 'Usage: build-sp.sh [--integration] [--local-sky-assets]\n' >&2; exit 1 ;;
    esac
done

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd -- "$root"
[[ $(uname -s) == Linux && $(uname -m) == x86_64 ]] || {
    printf 'build-sp.sh supports Linux x86-64 packages only\n' >&2
    exit 1
}
mkdir -p build/sp build/packages
# Do not allow a second invocation to mix objects or staged modules.
exec 9>build/sp/build.lock
flock -n 9 || { printf 'Another SP build is running\n' >&2; exit 1; }
cmake -S . -B build/sp -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DProjectName=OpenJedvibe \
    -DBuildMPEngine=OFF -DBuildMPRdVanilla=OFF -DBuildMPDed=OFF \
    -DBuildMPGame=OFF -DBuildMPCGame=OFF -DBuildMPUI=OFF -DBuildMPRend2=OFF \
    -DBuildSPEngine=ON -DBuildSPGame=ON -DBuildSPRdVanilla=ON -DBuildSPRend2=ON \
    -DBuildJK2SPEngine=OFF -DBuildJK2SPGame=OFF -DBuildJK2SPRdVanilla=OFF \
    -DBuildLauncher=ON -DBuildRmlUi=ON \
    -DBuildTests=OFF > build/sp/configure.log 2>&1
printf 'Building with one job. Log: %s/build/sp/build.log\n' "$root"
cmake --build build/sp --parallel 1 > build/sp/build.log 2>&1

stage=$(mktemp -d "$root/build/packages/.candidate.XXXXXXXX")
cmake --install build/sp --prefix "$stage" > "$stage/install.log"
package="$stage/JediAcademy"
cp scripts/launch-sp.sh "$package/launch-sp.sh"
cp scripts/audition-barks.py "$package/"
cp scripts/setup-atmosphere-review.py "$package/"
cp scripts/atmosphere_profiles.py "$package/"
cp docs/ai-tactics-research.md docs/bark-listening-report.md "$package/"
cp docs/development.md "$package/README.md"
cp docs/squad-ai.md "$package/squad-ai.md"
cp docs/squad-tactics.md "$package/squad-tactics.md"
cp docs/save-migration.md "$package/save-migration.md"
cp docs/encounter-krildor.md "$package/encounter-krildor.md"
cp docs/encounter-tatooine.md "$package/"
cp docs/encounter-kejim.md "$package/"
cp docs/rend2-sp.md "$package/"
cp docs/gpu-skinning-sp.md "$package/"
cp docs/ssao-sp.md "$package/"
cp docs/materials-sp.md "$package/"
cp docs/glass-presentation.md "$package/"
cp docs/debrief-sp.md "$package/"
cp docs/jo-campaign.md "$package/"
cp docs/jo-campaign-patches.md "$package/"
cp docs/jo-compatibility.md "$package/"
cp docs/jo-cinematics.md "$package/"
cp docs/jo-statistics.md "$package/"
cp docs/jo-mission-preparation.md "$package/"
cp docs/rmlui-selection.md "$package/"
cp docs/raster-features-sp.md "$package/"
cp docs/torch-sp.md "$package/"
cp docs/hud-reveal-sp.md "$package/"
cp docs/automap-sp.md "$package/"
cp docs/steam-audio-sp.md "$package/"
cp animation_todo.md "$package/"
cp docs/jolt-animation.md "$package/"
cp docs/jolt-performance.md "$package/"
cp docs/jolt-melee-plan.md "$package/"
cp docs/reactive-animation-research.md scripts/jolt-demo.sh "$package/"
cp scripts/jolt-demo.cfg "$package/OpenJK/"
cp docs/benchmark-sp.md "$package/"
cp docs/graphics-comparison.md "$package/"
cp docs/sky-fog-sp.md docs/sky-fog-investigation.md "$package/"
cp docs/procedural-atmosphere-plan.md "$package/"
cp docs/atmosphere-sp.md "$package/"
cp docs/atmosphere-editor.md "$package/"
cp docs/atmosphere-review.md docs/atmosphere-map-audit.md docs/volumetric-clouds.md "$package/"
if $local_sky_assets; then
    python3 scripts/build-sky-assets.py "${OJK_ASSETS:-$root/GameData}" "$package/OpenJK/sky-hd.pk3"
fi
mkdir -p "$package/OpenJK/maps"
cp scripts/maps/*.haze "$package/OpenJK/maps/"
cp scripts/maps/*.volfog "$package/OpenJK/maps/"
cp -a scripts/maps/shared "$package/OpenJK/maps/"
cp -a scripts/maps/*.atmosphere "$package/OpenJK/maps/"
python3 scripts/build-atmosphere-review.py --output "$package/OpenJK"
cp human_todo.md "$package/"
cp docs/door-navigation.md "$package/"
cp scripts/squad-smoke.cfg "$package/OpenJK/squad-smoke.cfg"
cp scripts/krildor-route.cfg "$package/OpenJK/krildor-route.cfg"
cp scripts/krildor-traverse.cfg "$package/OpenJK/krildor-traverse.cfg"
cp scripts/ai-memory*.cfg "$package/OpenJK/"
cp scripts/ai-squad*.cfg "$package/OpenJK/"
cp scripts/display-smoke.cfg "$package/OpenJK/display-smoke.cfg"
cp scripts/rend2-*.cfg "$package/OpenJK/"
chmod +x "$package/launch-sp.sh"
revision=$(git rev-parse --short HEAD)
id="$(date -u +%Y%m%dT%H%M%S%N)-$revision"
printf '%s\n' "$id" > "$package/build-id.txt"
{
    git rev-parse HEAD
    git status --short
    # Include tracked and untracked source files, but not ignored assets or builds.
    git ls-files --cached --others --exclude-standard -z |
        while IFS= read -r -d '' file; do
            if [[ -f "$file" ]]; then
                sha256sum -- "$file"
            fi
        done
} > "$package/source-manifest.txt"
git diff HEAD > "$package/source.patch"
cp build/sp/CMakeCache.txt "$package/CMakeCache.txt"
{
    uname -sm
    c++ --version
    for binary in "$package/openjedvibe-launcher" "$package/openjedvibe-import-jo" "$package/openjedvibe_sp.x86_64" "$package/rdsp-vanilla_x86_64.so" "$package/rdsp-rend2_x86_64.so" "$package/OpenJK/jagamex86_64.so"; do
        ldd "$binary"
        sha256sum "$binary"
    done
} > "$package/runtime-manifest.txt"

test -s "$package/launcher/launcher.rml"
test -s "$package/launcher/launcher.rcss"
test -x "$package/openjedvibe.desktop"
"$package/openjedvibe-launcher" --headless-check --profile "$stage/launcher-profile" \
    --ja-path "${OJK_ASSETS:-$root/GameData}" > "$package/launcher-check.txt"

OJK_SMOKE_RENDERER=rdsp-vanilla bash scripts/smoke-sp.sh "$package" | tee "$package/smoke-result.txt"
OJK_SMOKE_RENDERER=rdsp-rend2 OJK_SMOKE_TIMEOUT=${OJK_SMOKE_TIMEOUT:-600} \
    bash scripts/smoke-sp.sh "$package" | tee "$package/smoke-rend2-result.txt"
if $integration && grep -q '^BuildRmlUi:BOOL=ON$' build/sp/CMakeCache.txt; then
    for renderer in rdsp-vanilla rdsp-rend2; do
        python3 scripts/test-rmlui-selection.py --package "$package" --renderer "$renderer" \
            | tee "$package/rmlui-selection-$renderer-result.txt"
    done
fi
if $integration && [[ -d ${OJK_JO_ASSETS:-$root/GameData_JO}/base ]]; then
    python3 scripts/audit-jo.py --academy "${OJK_ASSETS:-$root/GameData}" \
        --outcast "${OJK_JO_ASSETS:-$root/GameData_JO}" | tee "$package/jo-audit-result.txt"
    python3 scripts/audit-jo-cinematics.py --academy "${OJK_ASSETS:-$root/GameData}" \
        --outcast "${OJK_JO_ASSETS:-$root/GameData_JO}" --check | tee "$package/jo-animation-audit-result.txt"
    OJK_JO_ASSETS=${OJK_JO_ASSETS:-$root/GameData_JO} OJK_SMOKE_CAMPAIGN=jo \
        bash scripts/smoke-sp.sh "$package" kejim_post | tee "$package/smoke-jo-result.txt"
    OJK_JO_ASSETS=${OJK_JO_ASSETS:-$root/GameData_JO} \
        python3 scripts/test-jo-sp.py --package "$package" | tee "$package/jo-mvp-result.txt"
    OJK_JO_ASSETS=${OJK_JO_ASSETS:-$root/GameData_JO} \
        python3 scripts/test-jo-sp.py --package "$package" --ai | tee "$package/jo-ai-result.txt"
    OJK_JO_ASSETS=${OJK_JO_ASSETS:-$root/GameData_JO} \
        python3 scripts/test-jo-sp.py --package "$package" --content | tee "$package/jo-content-result.txt"
    OJK_JO_ASSETS=${OJK_JO_ASSETS:-$root/GameData_JO} \
        python3 scripts/test-jo-cinematics.py --package "$package" | tee "$package/jo-cinematics-result.txt"
    for case in boarding jan-door; do
        python3 scripts/test-jo-cinematics.py --package "$package" --case "$case" --renderer rdsp-rend2 \
            | tee "$package/jo-$case-rdsp-rend2-result.txt"
    done
    for renderer in rdsp-vanilla rdsp-rend2; do
        python3 scripts/test-jo-stats.py --package "$package" --renderer "$renderer" \
            | tee "$package/jo-stats-$renderer-result.txt"
        python3 scripts/test-jo-sp.py --package "$package" --bouncers --renderer "$renderer" \
            | tee "$package/jo-bouncers-$renderer-result.txt"
        python3 scripts/test-jo-preparation.py --package "$package" --renderer "$renderer" \
            | tee "$package/jo-preparation-$renderer-result.txt"
        python3 scripts/test-jo-sp.py --package "$package" --world --renderer "$renderer" \
            | tee "$package/jo-world-$renderer-result.txt"
        python3 scripts/test-jo-sp.py --package "$package" --puzzle --renderer "$renderer" \
            | tee "$package/jo-puzzle-$renderer-result.txt"
        OJK_JO_ASSETS=${OJK_JO_ASSETS:-$root/GameData_JO} \
            python3 scripts/test-jo-sp.py --package "$package" --prisoners --renderer "$renderer" \
            | tee "$package/jo-prisoners-$renderer-result.txt"
        if grep -q '^UseJoltReactions:BOOL=ON$' build/sp/CMakeCache.txt; then
            OJK_JO_ASSETS=${OJK_JO_ASSETS:-$root/GameData_JO} \
                python3 scripts/test-jo-sp.py --package "$package" --jolt --renderer "$renderer" \
                | tee "$package/jo-jolt-$renderer-result.txt"
        fi
    done
    python3 scripts/test-jo-sp.py --package "$package" --progression | tee "$package/jo-progression-result.txt"
    python3 scripts/test-jo-sp.py --package "$package" --galak | tee "$package/jo-galak-result.txt"
fi
mv -- "$package" "build/packages/$id"
ln -s "packages/$id" "$stage/ready"
mv -Tf "$stage/ready" build/ready
printf 'Ready for rsync: %s/build/packages/%s/\n' "$root" "$id"
