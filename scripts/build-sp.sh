#!/usr/bin/env bash
set -euo pipefail

[[ $# == 0 ]] || { printf 'Usage: build-sp.sh\n' >&2; exit 1; }

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd -- "$root"
mkdir -p build/sp build/packages
# Do not allow a second invocation to mix objects or staged modules.
exec 9>build/sp/build.lock
flock -n 9 || { printf 'Another SP build is running\n' >&2; exit 1; }
cmake -S . -B build/sp -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DBuildMPEngine=OFF -DBuildMPRdVanilla=OFF -DBuildMPDed=OFF \
    -DBuildMPGame=OFF -DBuildMPCGame=OFF -DBuildMPUI=OFF -DBuildMPRend2=OFF \
    -DBuildSPEngine=ON -DBuildSPGame=ON -DBuildSPRdVanilla=ON -DBuildSPRend2=ON \
    -DBuildJK2SPEngine=OFF -DBuildJK2SPGame=OFF -DBuildJK2SPRdVanilla=OFF \
    -DBuildTests=OFF > build/sp/configure.log 2>&1
printf 'Building with one job. Log: %s/build/sp/build.log\n' "$root"
cmake --build build/sp --parallel 1 > build/sp/build.log 2>&1

stage=$(mktemp -d "$root/build/packages/.candidate.XXXXXXXX")
cmake --install build/sp --prefix "$stage" > "$stage/install.log"
package="$stage/JediAcademy"
cp scripts/launch-sp.sh "$package/launch-sp.sh"
cp scripts/import-jo.py "$package/import-jo.py"
cp docs/development.md "$package/README.md"
cp docs/squad-ai.md "$package/squad-ai.md"
cp docs/squad-tactics.md "$package/squad-tactics.md"
cp docs/save-migration.md "$package/save-migration.md"
cp docs/encounter-krildor.md "$package/encounter-krildor.md"
cp docs/encounter-tatooine.md "$package/"
cp docs/rend2-sp.md "$package/"
cp docs/ssao-sp.md "$package/"
cp docs/materials-sp.md "$package/"
cp docs/debrief-sp.md "$package/"
cp docs/jo-campaign.md "$package/"
cp docs/raster-features-sp.md "$package/"
cp animation_todo.md "$package/"
cp docs/benchmark-sp.md "$package/"
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
    for binary in "$package/openjk_sp.x86_64" "$package/rdsp-vanilla_x86_64.so" "$package/rdsp-rend2_x86_64.so" "$package/OpenJK/jagamex86_64.so"; do
        ldd "$binary"
        sha256sum "$binary"
    done
} > "$package/runtime-manifest.txt"

OJK_SMOKE_RENDERER=rdsp-vanilla bash scripts/smoke-sp.sh "$package" | tee "$package/smoke-result.txt"
OJK_SMOKE_RENDERER=rdsp-rend2 OJK_SMOKE_TIMEOUT=${OJK_SMOKE_TIMEOUT:-600} \
    bash scripts/smoke-sp.sh "$package" | tee "$package/smoke-rend2-result.txt"
if [[ -d ${OJK_JO_ASSETS:-$root/GameData_JO}/base ]]; then
    OJK_JO_ASSETS=${OJK_JO_ASSETS:-$root/GameData_JO} OJK_SMOKE_CAMPAIGN=jo \
        bash scripts/smoke-sp.sh "$package" kejim_post | tee "$package/smoke-jo-result.txt"
    OJK_JO_ASSETS=${OJK_JO_ASSETS:-$root/GameData_JO} \
        python3 scripts/test-jo-sp.py --package "$package" | tee "$package/jo-mvp-result.txt"
fi
mv -- "$package" "build/packages/$id"
ln -s "packages/$id" "$stage/ready"
mv -Tf "$stage/ready" build/ready
printf 'Ready for rsync: %s/build/packages/%s/\n' "$root" "$id"
