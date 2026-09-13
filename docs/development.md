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

For changes that need more than the basic smoke test, use
`bash scripts/build-sp.sh --stage-only`. This prints a candidate path without
updating `build/ready`. Pass that path to the relevant test with `--package`,
then publish with the normal build command after checks pass.

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

Install the pull-and-launch command on the desktop once. Replace `BUILD_SERVER`
with the SSH alias or `user@hostname` used to connect to this build machine:

```bash
mkdir -p ~/.local/bin
scp BUILD_SERVER:/home/olie/projects/OpenJK/scripts/play-sp.sh ~/.local/bin/openjk-play
chmod +x ~/.local/bin/openjk-play
~/.local/bin/openjk-play --configure BUILD_SERVER /path/to/GameData
```

Then update and start the game with one command:

```bash
~/.local/bin/openjk-play
```

If `~/.local/bin` is on `PATH`, use `openjk-play`. Extra arguments go to the engine:

```bash
openjk-play +devmap t2_wedge +exec krildor-route.cfg
```

The updater resolves the server's `build/ready` link once, then transfers that
fixed package into the same local directory on every run. Rsync uses existing
files for delta transfers and removes obsolete package files. It does not build
on either machine. The build server must publish with `scripts/build-sp.sh` first.

Configuration is stored in `${XDG_CONFIG_HOME:-$HOME/.config}/openjk-desktop.conf`.
It is a trusted Bash file. These settings are available:

| Setting | Purpose and default |
| --- | --- |
| `OJK_HOST` | SSH alias or `user@hostname`. Use SSH config for ports and identity files. |
| `OJK_ASSETS` | Desktop `GameData` directory, supplied during configuration. |
| `OJK_REMOTE_ROOT` | Server repository, default `/home/olie/projects/OpenJK`. |
| `OJK_DESKTOP_DIR` | Local work directory, default `${XDG_DATA_HOME:-$HOME/.local/share}/openjk-playtest`. The managed package is its `build/` subdirectory. |
| `OJK_PROFILE` | Writable game profile, default `${XDG_DATA_HOME:-$HOME/.local/share}/openjk-dev`. |
| `OJK_DESKTOP_CONFIG` | Optional alternative configuration-file path. Set it before running the command. |

Keep assets, profiles, and the updater outside the managed build directory. The
first update requires that directory to be empty. Later updates retain a local
management marker. Do not add personal files there; rsync can delete them.

The updater and package launcher share a lock held until the game exits. A second
launch or update is refused while that lock is held. Run through these scripts,
not the engine binary directly, to retain this protection. A failed transfer does
not launch the game and leaves an incomplete-update marker. Run the updater again
to repair it. Delayed updates reduce partial replacement, but do not provide a
transactional rollback. Older server packages remain available for comparison.

Assets are not included. Use an existing `GameData/base/assets*.pk3` installation
or transfer the assets once. To launch the local package without an update, run
this command inside its `build/` directory:

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
details. Squad memory now uses recorded positions for lost-contact tracking;
coordinated flank roles are not yet implemented. Fresh encounters are preferred
over cross-build saves.
Add `+exec squad-smoke.cfg` for the diagnostic spawn fixture. It enables player
invulnerability and adds three enemies; it is not a campaign playtest.

For the first multi-route candidate, load `t2_wedge` and execute
`krildor-route.cfg`. See `encounter-krildor.md` in the package or
`docs/encounter-krildor.md` in the repository for coordinates and evidence limits.

Run `python3 scripts/test-traversal-sp.py` to test actual NPC traversal of both
paths, navigator-selected movement, visibility loss, and probe cleanup. The suite
uses `krildor-traverse.cfg`, which clears native NPCs in fresh test sessions.
Use `--case north` or another documented case to repeat one check. Python 3 and
the existing smoke-test dependencies are required. Run headless cases sequentially.

Run `python3 scripts/test-ai-memory.py` for sight, shared-memory, target-switch,
and search-expiry checks. Use `--case shared-async` to repeat the two-member case.
The fixtures and `nav memory` snapshot command are described in `squad-ai.md`.

Both machines are x86-64 Arch Linux, but runtime library versions still need a
desktop check. The package includes source and runtime manifests, debug symbols,
and the tracked source diff. It does not bundle system libraries or proprietary
game assets. The user has confirmed desktop update and launch on the GTX 1080 Ti.
Detailed hardware performance, audio, and campaign checks remain manual tasks.

`python3 scripts/test-play-sp.py` tests the updater with real rsync, a local SSH
stand-in, and a fake game. It checks delta reuse, argument quoting, publication
changes, failed-transfer recovery, running-game locks, and path protection.
These tests do not establish SSH access or graphics support on the desktop.
