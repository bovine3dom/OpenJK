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
The user has confirmed that desktop update and launch work. No remote session
was needed for the automated checks on the build machine.

- [ ] Obtain the server SSH address and remote build path as seen from the desktop. Choose local build, asset, and profile directories.
- [ ] Compare runtime library versions on both machines. Check that the executable and all native modules load on the desktop. Avoid server-specific CPU optimisation.
- [ ] Copy game assets once, or use an existing desktop installation. Exclude assets from routine build transfers.
- [x] Package `openjk_sp.x86_64`, `rdsp-vanilla_x86_64.so`, and `OpenJK/jagamex86_64.so` with a launcher and build manifest.
- [x] Assign each package a unique identifier and source checksums, including uncommitted files. Publish only complete packages that passed the smoke test.
- [x] Add a configured desktop pull-and-launch command in `scripts/play-sp.sh`. Reuse one managed directory for rsync delta updates, resolve a fixed server package, and refuse updates while the game is running.
- [x] Test delta reuse, transfer failure/retry, path protection, publication changes, and launcher locking locally. Actual desktop SSH and GPU validation remain pending.
- [x] Retain older packages and use an external writable profile. Desktop validation remains pending.
- [x] Provide normal and direct-map launches plus a diagnostic spawn fixture. Include the build identifier in test logs.
- [x] Copy a package locally with rsync and pass the smoke test from the new path. This does not establish compatibility with the desktop's libraries or GPU.

## 4. Native Baseline Check

- [ ] Start a fresh campaign on the desktop. Check movement, aiming, saber combat, Force powers, sound, and cinematics.
- [ ] Test saving and loading within this build. Do not require saves to work across future incompatible builds.
- [ ] Record resolution, graphics settings, driver version, frame times, and existing defects.
- [ ] Confirm that updating the package leaves the original installation and development profile intact.

## 5. First AI Encounter

- [x] Select the `t2_wedge` central interior as the first candidate. Record two connected graph routes and a three-member enemy group in `docs/encounter-krildor.md`.
- [x] Verify designated-trooper traversal of both routes, including measured arrivals. A final-goal-only test also exercises navigator-selected movement.
- [x] Verify blocked and frozen timeouts, cancel/busy/restart, killed-actor cleanup, and save cancellation with `python3 scripts/test-traversal-sp.py`.
- [x] Observe geometric line-of-sight loss around the pillar. This does not yet verify enemy perception or memory.
- [ ] Add goal-replacement, direct-free, and active-probe load/map-change lifecycle cases.
- [x] Trace real enemy lost-contact behaviour in this room. Pair sight time with position, remove PVS-only sight refresh, retain valid groups, and use shared recorded positions in the commander's lost-contact paths.
- [x] Verify seven memory cases with `scripts/test-ai-memory.py`: loss/reacquisition and shared observations in both commander modes, unseen assignment, search expiry, and target switching.
- [ ] Add dedicated runtime cases for rejected alert acquisition, blocked-shot sight, no-route holds, merge ordering, and competing combat-point reuse.
- [ ] Address remaining live-position use in facing, short-loss tactics, solo pursuit, path-cost sorting, and generic fleeing before claiming engine-wide perception correctness.
- [ ] Audit existing cover, flank, and lost-contact voice clips. Map suitable clips to real squad events.
- [x] Add default-off traces for existing membership, squad states, commander decisions, combat-point searches and reservations, movement, and bark requests, suppression, and dispatch.
- [x] Check trace levels 0, 3, and 4 with `bash scripts/test-squad-sp.sh`. This is a diagnostic fixture, not a coordinated-flank test.
- [ ] Extend diagnostics to new observation confidence, tactical roles, and plan phases when those systems are implemented.
- [ ] Implement one engage-and-flank plan with unchanged enemy health and damage. Include tactical orders, acknowledgements, and failure barks from the start.
- [ ] Test completed and disrupted plans, lost sight, blocked routes, casualties, and script control. Add automated assertions where practical.
- [ ] Playtest with diagnostics hidden. Check that movement and barks explain coordination without revealing hidden player information.

Use `roadmap.md` for detailed acceptance checks. Investigate the raster-only Rend2
port separately after the vanilla build and deployment workflow are reliable.

## Display Follow-up

- [x] Expose desktop and custom resolutions through `openjk-play --desktop` and `--resolution WIDTHxHEIGHT`, with aspect-adjusted world FOV.
- [x] Render and validate a non-black 3840x2160 screenshot. Keep existing profile settings unless an override is requested.
- [ ] Replace the legacy resolution menu list and investigate widescreen HUD/menu layout separately.

## Rend2 Port

- [x] Add an opt-in SP-native object target for shared shader, allocator, math, and tangent-space sources, with one shared shader generator.
- [x] Build the SP objects, MP Rend2, and SP vanilla on Linux with one job.
- [ ] Adapt scene/entity submission and integrate SP Ghoul2 ownership, animation, collision, and skinning.
- [ ] Complete the SP renderer interface and resource lifetime, then link and load a real `rdsp-rend2` module.
- [ ] Verify renderer identity, representative SP maps, visual effects, and performance on the GTX 1080 Ti. Do not add ray tracing.
