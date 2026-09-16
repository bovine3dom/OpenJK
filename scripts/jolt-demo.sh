#!/usr/bin/env bash
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
if [[ ${1:-} == --help ]]; then
    printf 'Usage: bash jolt-demo.sh [launch-sp.sh display options] [engine arguments]\nSet OJK_ASSETS to your GameData directory. Set OJK_PACKAGE to select a build.\n'
    exit 0
fi
if [[ -f "$here/launch-sp.sh" && -f "$here/build-id.txt" ]]; then
    package=${OJK_PACKAGE:-$here}
    assets=${OJK_ASSETS:?Set OJK_ASSETS to your GameData directory}
else
    package=${OJK_PACKAGE:-$here/../build/ready}
    assets=${OJK_ASSETS:-$here/../GameData}
fi
exec bash "$package/launch-sp.sh" "$assets" --jolt-demo "$@"
