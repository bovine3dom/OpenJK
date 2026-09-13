# Initial Development Tasks

## Build Limit

**Use one build job until instructed otherwise.** Use
`cmake --build build/sp --parallel 1`. Do not run concurrent builds or use
automatic job counts. Apply the same limit to container and dependency builds.

## Verified Prerequisites

- [x] Confirm that this server runs x86-64 Arch Linux.
- [x] Confirm that `GameData/base/` contains `assets0.pk3` through `assets3.pk3`. All four archives pass `unzip -tqq`.
- [x] Confirm that `GameData/` and build directories are ignored by Git.
- [x] Find CMake, Ninja, Make, GCC, G++, and pkg-config.
- [x] Find SDL2, OpenGL, JPEG, PNG, and zlib through package metadata and CMake configuration.
- [x] Find Xvfb, xvfb-run, glxinfo, FFmpeg, rsync, SSH, timeout, and flock.
- [x] Create a virtual display and an LLVMpipe OpenGL context. `glxinfo -B` reports OpenGL 4.6 compatibility support.
- [x] Configure the single-player engine, game module, and vanilla renderer in `build/sp` with debug symbols.

No missing dependencies were found. The single-player build and headless map test
pass. Optional Boost-based unit tests are disabled; check their dependency
separately if enabled. No packages were installed during these checks.

## 1. Build the Baseline

- [x] Compile the configured targets with one job. Existing compiler warnings remain; no build errors were found.
- [x] Record the source revision, local changes, compiler version, build settings, and runtime dependencies in each package.
- [x] Stage the engine, renderer, and game module together. Verify module loading in the headless map test.
- [x] Keep the original assets, configs, and saves unchanged. Use a separate development profile for writable files.

Build, stage, and smoke-test from the repository root:

```bash
bash scripts/build-sp.sh
```

The script enforces `--parallel 1`. Logs are in `build/sp/`. Completed packages
are in `build/packages/`; `build/ready` points to the latest passing package.
See `docs/development.md` for launch and test commands.

## 2. Headless Smoke Test

- [x] Add a repeatable launch script using Xvfb and software OpenGL at 640x480 with an isolated writable profile.
- [x] Verify game startup, renderer loading, game-module loading, and original asset search paths.
- [x] Load `t1_sour`, allow a short run, capture and decode a screenshot, and quit.
- [x] Apply an external timeout. Keep logs, the screenshot, the command, and the build identifier for each run.
- [x] Require client map-loading evidence and a valid screenshot. A clean process exit alone is not sufficient.
- [x] Reject a deliberately invalid map. Each run uses a fresh directory to exclude stale output.
- [x] Reject a missing test config. Require a completion marker from the squad fixture.

Single-player has no supported renderer-free dedicated mode. Xvfb and LLVMpipe
provide a display and rendering without a physical monitor. This test checks basic
function, not GPU performance, audio quality, or gameplay quality.

## 3. Desktop Deployment

The playtest machine runs x86-64 Arch Linux with a GTX 1080 Ti and can connect to
this server over SSH. Prefer rsync and local execution over running from SSHFS.
Desktop access and validation are postponed. No files have been transferred.

- [ ] Obtain the server SSH address and remote build path as seen from the desktop. Choose local build, asset, and profile directories.
- [ ] Compare runtime library versions on both machines. Check that the executable and all native modules load on the desktop. Avoid server-specific CPU optimisation.
- [ ] Copy game assets once, or use an existing desktop installation. Exclude assets from routine build transfers.
- [x] Package `openjk_sp.x86_64`, `rdsp-vanilla_x86_64.so`, and `OpenJK/jagamex86_64.so` with a launcher and build manifest.
- [x] Assign each package a unique identifier and source checksums, including uncommitted files. Publish only complete packages that passed the smoke test.
- [ ] Add a desktop pull command. Transfer into a new directory and select it for launch only after transfer succeeds. Do not overwrite a running build.
- [x] Retain older packages and use an external writable profile. Desktop validation remains pending.
- [x] Provide normal and direct-map launches plus a diagnostic spawn fixture. Include the build identifier in test logs.
- [x] Copy a package locally with rsync and pass the smoke test from the new path. This does not establish compatibility with the desktop's libraries or GPU.

## 4. Native Baseline Check

- [ ] Start a fresh campaign on the desktop. Check movement, aiming, saber combat, Force powers, sound, and cinematics.
- [ ] Test saving and loading within this build. Do not require saves to work across future incompatible builds.
- [ ] Record resolution, graphics settings, driver version, frame times, and existing defects.
- [ ] Confirm that updating the package leaves the original installation and development profile intact.

## 5. First AI Encounter

- [ ] Select a repeatable encounter with three or four ranged enemies and two usable routes. Record the map, start position, settings, and current behaviour.
- [ ] Audit existing cover, flank, and lost-contact voice clips. Map suitable clips to real squad events.
- [x] Add default-off traces for existing membership, squad states, commander decisions, combat-point searches and reservations, movement, and bark requests, suppression, and dispatch.
- [x] Check trace levels 0, 3, and 4 with `bash scripts/test-squad-sp.sh`. This is a diagnostic fixture, not a coordinated-flank test.
- [ ] Extend diagnostics to new observation confidence, tactical roles, and plan phases when those systems are implemented.
- [ ] Implement one engage-and-flank plan with unchanged enemy health and damage. Include tactical orders, acknowledgements, and failure barks from the start.
- [ ] Test completed and disrupted plans, lost sight, blocked routes, casualties, and script control. Add automated assertions where practical.
- [ ] Playtest with diagnostics hidden. Check that movement and barks explain coordination without revealing hidden player information.

Use `roadmap.md` for detailed acceptance checks. Investigate the raster-only Rend2
port separately after the vanilla build and deployment workflow are reliable.
