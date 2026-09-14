#!/usr/bin/env bash
set -euo pipefail

[[ $# == 1 ]] || { printf 'Usage: bash scripts/add-worktree.sh BRANCH\n' >&2; exit 1; }
root=$(git -C "$(dirname -- "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)
branch=$(git -C "$root" check-ref-format --branch "$1")
common=$(git -C "$root" rev-parse --path-format=absolute --git-common-dir)
main=$(dirname -- "$common")
assets=$(realpath -e -- "${OJK_ASSETS:-$main/GameData}")
[[ -d "$assets/base" ]] || { printf 'Missing asset directory: %s/base\n' "$assets" >&2; exit 1; }

# Use the same parent from the main checkout and from linked worktrees.
parent=$(realpath -m -- "$main/../worktrees")
destination="$parent/openjk-${branch//\//-}"
[[ ! -e "$destination" && ! -L "$destination" ]] || {
    printf 'Destination already exists: %s\n' "$destination" >&2
    exit 1
}
mkdir -p -- "$parent"
if git -C "$root" show-ref --verify --quiet "refs/heads/$branch"; then
    git -C "$root" worktree add -- "$destination" "$branch"
else
    git -C "$root" worktree add -b "$branch" -- "$destination" HEAD
fi
ln -sT -- "$assets" "$destination/GameData"
printf 'Worktree: %s\nAssets: %s\n' "$destination" "$assets"
