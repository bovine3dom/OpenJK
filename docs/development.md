# Single-Player Development

## Build and Test

Run from the repository root:

```bash
bash scripts/build-sp.sh
```

This command builds with one job, stages matching native modules, and runs a
headless smoke test. It updates `build/ready` only after the test passes. The
output gives an immutable package path under `build/packages/`. Do not change
source files during packaging. Failed candidates remain available for diagnosis.

Build logs are in `build/sp/`. Test logs and screenshots are in a new directory
under `build/smoke/` for each run. No original assets, configs, or saves are changed.

The smoke test uses Xvfb, Mesa software OpenGL, and FFmpeg image validation. It
disables audio. It checks map loading and image output, not sound, gameplay
quality, or hardware rendering. A virtual display does not make SP renderer-free.

To repeat a test or check a different map:

```bash
bash scripts/smoke-sp.sh build/ready t1_sour
bash scripts/smoke-sp.sh build/ready missing_test_map
```

The second command must fail. Set `OJK_ASSETS` to use an asset directory other than
the repository's `GameData/`. Optional engine arguments follow the map name.
Set `OJK_SMOKE_ROOT` to change the output parent directory. The test records its
command and uses a fresh profile each time. `wait` counts command-buffer delays,
not exact rendered frames; do not use this test as a deterministic benchmark.

To check squad and bark diagnostics at levels 0, 3, and 4:

```bash
bash scripts/test-squad-sp.sh
```

## Desktop Package

Copy a completed package directory to the desktop with rsync over SSH. Use the
versioned path printed by the build command, not a symlink that can change during
transfer. Transfer into a new local directory. Keep an older package for comparison.

Assets are not included. Use an existing `GameData/base/assets*.pk3` installation
or copy the assets once. Then run this command inside the downloaded package:

```bash
bash launch-sp.sh /path/to/GameData
```

The launcher prints the package ID and uses a separate development profile at
`${XDG_DATA_HOME:-$HOME/.local/share}/openjk-dev`. Set `OJK_PROFILE` to change it.
Keep that profile outside both the package and the original asset directory.
Do not put native modules from other builds in the profile.

For a direct test encounter with squad diagnostics:

```bash
bash launch-sp.sh /path/to/GameData +devmap t1_sour +set d_npcai 3
```

See `squad-ai.md` in the package or `docs/squad-ai.md` in the repository for trace
details. Current squad instrumentation does not change tactics. Fresh encounters
are preferred over cross-build saves.
Add `+exec squad-smoke.cfg` for the diagnostic spawn fixture. It enables player
invulnerability and adds three enemies; it is not a campaign playtest.

Both machines are x86-64 Arch Linux, but runtime library versions still need a
desktop check. The package includes source and runtime manifests, debug symbols,
and the tracked source diff. It does not bundle system libraries or proprietary
game assets. No desktop validation has been performed yet.
