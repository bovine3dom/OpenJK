# Single-Player Development

## Build and Test

Run from the repository root:

```bash
bash scripts/build-sp.sh
```

This command always enables both SP renderers and builds with one job. It stages
matching native modules and runs headless smoke tests with vanilla and Rend2.
It records `smoke-result.txt` and `smoke-rend2-result.txt` in the package.
Both tests must pass before it updates `build/ready`. The output gives a fixed
package path under `build/packages/`. Do not change source files during packaging.
Failed candidates remain available for diagnosis.

Every successful build publishes its package through `build/ready`. For more
checks, pass the printed package path to the relevant test with `--package`.

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

To test Rend2 and its renderer lifecycle:

```bash
OJK_SMOKE_RENDERER=rdsp-rend2 OJK_SMOKE_TIMEOUT=600 \
  bash scripts/smoke-sp.sh build/ready t2_wedge
python3 scripts/test-rend2-sp.py --package build/ready
```

The strict Rend2 check requires its log identity and rejects vanilla fallback.
The lifecycle test checks OpenGL errors, `vid_restart`, and format-3 save/load
in one process. It also checks the transition from `t2_wedge` to `t1_sour`.

To check squad and bark diagnostics at levels 0, 3, and 4:

```bash
bash scripts/test-squad-sp.sh
```

## Worktrees

Create or check out a local branch in a separate worktree:

```bash
bash scripts/add-worktree.sh rend2-perf
```

This creates `../worktrees/openjk-rend2-perf` relative to the main checkout.
An existing local branch is checked out; a missing branch starts at the invoking
worktree's HEAD. Uncommitted changes are not copied. Branch-name slashes become
hyphens in directory names, so `ui/radial` uses `openjk-ui-radial`. Existing paths
are never replaced, and Git refuses branches already checked out elsewhere.
The helper also works from a linked worktree and uses the same parent directory.

Each worktree gets a `GameData` symlink to the main checkout's asset directory.
Set `OJK_ASSETS` when running the helper to select a different shared directory.
Keep shared assets unchanged. Each worktree has its own `build/sp`, packages,
and `build/ready`; do not share configured build directories. Run
`bash scripts/build-sp.sh` inside the selected worktree as usual.

Concurrent worktree builds are allowed. Each build still uses one job. Local
build and package locks remain; there are no global locks. Other builds and
game sessions can distort benchmark results, so compare performance on an idle
machine when practical.

Automated tests create separate profiles. For manual launches, select a distinct
writable profile for each worktree:

```bash
OJK_PROFILE="$HOME/.local/share/openjk-profiles/rend2-perf" \
  bash build/ready/launch-sp.sh GameData
```

The desktop updater can select these directories with `--worktree`:

```bash
openjk-play --worktree rend2-perf +set cl_renderer rdsp-rend2
openjk-play --worktree ui/radial --resolution 1920x1080
```

Put `--worktree NAME` before display options and engine arguments. Names accept
letters, digits, underscores, dots, hyphens, and slashes. Slashes become hyphens,
as in the worktree helper. The option selects a directory; it does not verify
which Git branch is checked out there.

Keep `OJK_REMOTE_ROOT` set to the main checkout. The updater resolves that path
on the server, then uses its sibling `worktrees/openjk-NAME/build/ready`.
The selected worktree must have a published build, not only a staged candidate.
A missing or invalid publication fails; it does not fall back to the main build.

Local files use the existing configuration as their base:

| Setting | With `--worktree NAME` |
| --- | --- |
| Installation | `<OJK_DESKTOP_DIR>/worktrees/openjk-NAME/build` |
| Profile | `<OJK_PROFILE>/worktrees/openjk-NAME` |
| Assets and SSH host | Unchanged |

No separate configuration file is needed. The updater does not rewrite the
configuration or copy the main profile. Each worktree starts with separate
settings and saves. Plain `openjk-play` retains its original paths. Local locks
protect each installation independently; no global lock is added.

If the desktop has an older installed updater, repeat the `scp` and `chmod`
commands below once. There is no need to repeat `--configure`. Game-package
updates do not replace the separately installed `openjk-play` script.

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

The engine and launcher still default to `rdsp-vanilla`. Rend2 is experimental
and opt-in. Select it explicitly, or return to vanilla:

```bash
openjk-play +set cl_renderer rdsp-rend2
openjk-play +set cl_renderer rdsp-vanilla
```

The profile saves the `cl_renderer` choice for later launches. The engine can
fall back to vanilla if it cannot load the selected module. Rend2 tests reject
this fallback.

Select desktop resolution with widescreen world FOV adjustment:

```bash
openjk-play --desktop
openjk-play --resolution 3840x2160
```

These options select fullscreen mode. Put display options before engine arguments.
Append `+set r_fullscreen 0` for a window. New profiles default to desktop mode and
aspect-adjusted FOV. Existing saved display preferences are retained unless an
explicit option is supplied. The old video menus do not list desktop/custom 4K
modes and can reset the resolution when applying a quality preset. HUD/menu
stretching is separate from world FOV correction.

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

`python3 scripts/test-doors-sp.py` verifies automatic-door traversal and a locked
control. See `door-navigation.md` in the package, or `docs/door-navigation.md` in
the repository, for the confirmed controller-selection bug.

`bash scripts/test-display-sp.sh` checks an actual 3840x2160 scene and rejects an
almost-black screenshot. It needs FFprobe as well as FFmpeg, uses four software
rasterizer threads by default, and allows up to 600 seconds. Builds still use
one job. Set `LP_NUM_THREADS` to change rasterizer threads. General smoke tests
default to one thread, 640x480, and 120 seconds; `OJK_SMOKE_DISPLAY`,
`OJK_SMOKE_WAIT`, and `OJK_SMOKE_TIMEOUT` provide explicit test overrides.
Both renderers passed the 4K check. Set `OJK_SMOKE_RENDERER=rdsp-rend2` to test Rend2.

SP Rend2 development progress and build commands are in `rend2-sp.md` in the
package, or `docs/rend2-sp.md` in the repository. The installed
`rdsp-rend2_x86_64.so` is playable but experimental. See that document for limits.

Run `python3 scripts/test-ai-memory.py` for sight, shared-memory, target-switch,
and search-expiry checks. Use `--case shared-async` to repeat the two-member case.
The fixtures and `nav memory` snapshot command are described in `squad-ai.md`.

Run `python3 scripts/test-squad-tactics.py` for recruitment, concealed flanking,
regrouping, interruption, and save/load checks. See `squad-tactics.md` in the
package or `docs/squad-tactics.md` in the repository. Supported pre-tactics v1 saves
now migrate on load; new saves use v2. See `save-migration.md` for scope and tests.

Both machines are x86-64 Arch Linux, but runtime library versions still need a
desktop check. The package includes source and runtime manifests, debug symbols,
and the tracked source diff. It does not bundle system libraries or proprietary
game assets. The user has confirmed desktop update and launch on the GTX 1080 Ti.
Detailed hardware performance, audio, and campaign checks remain manual tasks.

`python3 scripts/test-play-sp.py` tests the updater with real rsync, a local SSH
stand-in, and a fake game. It checks delta reuse, argument quoting, publication
changes, failed-transfer recovery, running-game locks, and path protection.
These tests do not establish SSH access or graphics support on the desktop.
