# Initial Development Tasks

## Build Limit

**Use one job per build.** Use `cmake --build build/sp --parallel 1`.
Independent worktrees may build concurrently. Do not use automatic job counts.
Apply the per-build limit to container and dependency builds too. Keep local
build and package locks; do not add global build or benchmark locks.

## JA/JO Campaign Integration

Use one gameplay implementation for both campaigns. Campaign data controls
characters, equipment, objectives, progression, and content compatibility.
Automated checks now cover Artus, Yavin training, armoured Galak, and the known
JO water-boundary gap. Fix the timed crossing. Then complete normal-play checks
through Kejim and Yavin. Manual results remain in `human_todo.md`.

### Statistics, Bouncers, and Mission Preparation

- [x] Keep JO completion statistics visible until a click or Enter. Pause the next map while the player reads them.
- [x] Restore the vestless Rodian back panel. Check native bar bouncers and save/load in both renderers.
- [x] Add weapon selection, including JA-only weapons, at true mission boundaries. Do not open setup on internal map transitions.
- [x] Use the shared RmlUi Force and weapon screens for JA and JO. Check input, display proportions, and saved choices in both renderers.
- [x] Correct JO route-check bounds so Lando can use the ship stairs in `ns_starpad`. Check the roof and fuel objectives and recovery from a stuck save.
- [x] Keep all JO powers story-controlled. Allow seven total points for the five JA-only powers, with three ranks each.
- [x] Check two-main-weapon and one-explosive limits, full saves, entry autosaves, story upgrades, and earlier-save restoration in both renderers.
- [x] Fix the Rend2 fog-array overflow found while checking the Yavin transition. Use the map-format limit in CPU and shader blocks.

### 1. Whole-Campaign Compatibility Report

- [x] Audit all 26 JO single-player maps for missing entity handlers and empty stubs.
- [x] Compare documented spawn flags and report differences with map and entity references. Review the remaining `valley` mission-screen flag separately from gameplay flags.
- [x] Check NPC classes, animation references, script dependencies, and required assets.
- [x] Record objectives, inventory grants, Force and saber commands, and map transitions.
- [x] Generate a repeatable report with explicit limits: `python3 scripts/audit-jo.py`. See `docs/jo-compatibility.md`.

Acceptance: identify likely campaign blockers before a human reaches each map.
A successful static lookup does not establish correct runtime behavior.

### 2. Artus and Yavin Progression

- [x] Fix missing prisoner head surfaces and check both prisoner variants in both renderers, including save/load.
- [x] Fix the Artus Topside cinematic hang after Tavion presents Jan to Desann. Verify the handoff and the start of the following fight without skipping in both renderers.
- [x] Fix confirmed Artus and Yavin Trial gaps. Give legacy saber pickups a valid shared saber definition.
- [x] Verify the initial Yavin Force reset, holocron levels, a push button, a pull step, jump height, and active speed. Check that push is unavailable before its pickup.
- [x] Verify saber acquisition, its scripted level-one skills, selection, and the Force wheel's available powers.
- [x] Verify save/load and progression into `ns_streets` through the retail exit target.

Acceptance: complete the training sequence, acquire the saber, save and reload,
and continue with the correct abilities. Keep normal mission playthroughs as
separate manual checks; record any diagnostic trigger activation in automated tests.

### 3. Known Later Blockers

- [x] Compile JO's armoured Galak controller with the shared NPC, weapon, damage, animation, and save systems.
- [x] Verify missile firing, shield and generator damage, save/load, death, and the encounter's completion target in both renderers. Fix the exposed null-client collision crash in the following debris sequence.
- [x] Check the `cm_*` collision loaders for `JK2_MODE` rules. Enable the JO water rule and verify a nonsolid opaque boundary in Yavin Swamp.

### 4. Campaign Configuration and Regression Checks

- [x] Use named importer tables for NPC class translations, cinematic actors, and animation aliases. Keep entity repairs in one campaign load hook.
- [x] Keep common AI, combat, movement, and rendering shared. Translate legacy data at import or load time where possible.
- [ ] Consolidate the remaining startup character and equipment overrides when further campaign profiles are needed.
- [x] Run focused JA and JO regression checks, document limits, and publish a tested desktop package. The package `20260915T225923812721918-783a0e8f` passed all publication checks.

## Complete JO Cinematic Animation Sets

- [x] Audit 869 animation commands, 247 distinct values, and nine hand-prop references across all retail scripts. No unresolved animation names or identified-actor frame gaps remain.
- [x] Check shared names for different JO and JA poses. Use the original JO skeleton and frame data for staged actors.
- [x] Fix the Artus Topside restraint poses and the Kejim office gem and chair scene. Check both from older saves with Rend2.
- [x] Add all 314 JO-only engine animation names. Import complete cinematic sets and all skin variants. Keep the shared gameplay animation set intact.
- [x] Check the Desann fight handoff, Jan's return as a gameplay escort, and format-3 to format-4 migration. Preserve existing actor state and attachments during model migration.
- [x] Add a strict static audit and six full cutscene cases. Check the office, restraint, bartender, and reunion scenes in both renderers. Record remaining manual scene checks.
- [x] Publish the passing package `20260916T020444391132401-8d0e9087` after all required checks.

## JO Mission Statistics and Behavior Parity

- [x] Restore the statistics display during JO mission loading. Keep a snapshot of the completed mission before its counters can reset.
- [x] Support normal exit targets, `SET_MISSION_STATUS_SCREEN`, and JO's `HIDEINFO` flag. Hide completion statistics on new games, direct map loads, and save loads.
- [x] Check kills, secrets, shots, hits, accuracy, favorite weapon, and Force/saber counters. Include zero-shot and pre-saber cases.
- [x] Test visible and hidden transitions, counter carry/reset rules, save loading, and both renderers.
- [x] Extend the audit to registered script properties, actual dispatch handlers, empty handlers, and reviewed behavior differences. Separate implementation from runtime verification.
- [x] Update the compatibility report and publish the tested package `20260916T073530525195835-50a3e4ec`.

## JO Combat and Scripted Progression Follow-up

- [x] Verify actual Desann attacks and the Artus defeat sequence. Restore missing Desann and Tavion sabers.
- [x] Correct the Kejim CCTV crew movement. Check actual walk-animation playback in both renderers.
- [x] Check Kyle's rendered facing toward Desann during the aiming and startled poses. Keep the normal-play camera review as a manual check.
- [x] Complete the shrine scene and the Yavin Trial ending without skipping, in both renderers.
- [x] Verify the fountain pulls, water and mover heights, and bridge load response in both renderers.
- [ ] Resolve the intermittent timed-crossing failure. Focused runs passed in both renderers, but the full publication run failed all three crossing attempts. Evidence: `build/jo-tests/rdsp-vanilla.mv1m6219/console.log`.
- [x] Check older JO scene saves and retain the documented project migration paths without changing the source save.
- [x] Keep the completed progression changes separate from the statistics work in Git history.
- [ ] Publish the current update after the timed crossing and the required integration checks pass.

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
The script always enables both SP renderers. Both smoke tests must pass before
publication. Results are in `smoke-result.txt` and `smoke-rend2-result.txt`.
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

- [x] Configure the server package path and the desktop build, asset, and profile directories.
- [ ] Record runtime library versions for the current package. Check that the executable and all native modules load on the desktop. Avoid server-specific CPU optimisation.
- [x] Reuse the existing desktop assets and exclude them from routine build transfers.
- [x] Package `openjedvibe_sp.x86_64`, `rdsp-vanilla_x86_64.so`, `rdsp-rend2_x86_64.so`, and `OpenJK/jagamex86_64.so` with a launcher and build manifest.
- [x] Assign each package a unique identifier and source checksums, including uncommitted files. Publish only complete packages that passed both renderer smoke tests.
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
- [x] Verify 13 memory cases with `scripts/test-ai-memory.py`: the previous eight, short-loss checks in both modes, solo pursuit, solo unseen assignment, and solo target replacement.
- [x] Check same-target memory retention and unseen-target goal cleanup. Solo fixtures use `d_noGroupAI 1`, not the separate `SCF_NO_GROUPS` formation controller.
- [ ] Add dedicated runtime cases for rejected alert acquisition, blocked-shot sight, no-route holds, merge ordering, and combat-point reuse in real multi-squad encounters.
- [x] Use known personal or same-enemy group positions for ST hidden facing, distance, short-loss CP search, and solo enemy goals. Sort from actual member nodes to the target record node and fix the legacy insertion `k++` stack overflow. Keep the supplied danger point in `NPC_StartFlee` CP retries.
- [x] Clear memory and old memory goals on accepted target changes or clears, but preserve memory for same-target or rejected locked assignments. Tag temporary memory goals with `tempGoal.enemy`.
- [ ] Correct hearing, steering, and chase restrictions in the separate `SCF_NO_GROUPS` controller, `AI_HazardTrooper`. Review other NPC controllers, generic callers, full FOV, and attention before claiming engine-wide perception correctness.
- [ ] Audit existing cover, flank, and lost-contact voice clips. Map suitable clips to real squad events.
- [x] Add default-off traces for existing membership, squad states, commander decisions, combat-point searches and reservations, movement, and bark requests, suppression, and dispatch.
- [x] Check trace levels 0, 3, and 4 with `bash scripts/test-squad-sp.sh`. This is a diagnostic fixture, not a coordinated-flank test.
- [x] Add local report and tactical-role diagnostics, including fixed goal/threat positions and cleanup events.
- [x] Implement report-backed recruitment, one supported flank per group, and bounded regrouping without increasing production NPC health or damage.
- [x] Use contact calls, delayed acknowledgement attempts, and action-linked outflank/cover barks under existing speech restrictions.
- [x] Verify 16 tactical cases: the baseline nine plus death, timeout, cinematic interruption, contested reservation, save-reservation, and non-tactical ownership checks.
- [x] Clear movement speech and chance on tactic cancellation or group removal. Restore full-save CP occupancy from NPC claims, not autosaves. Release all NPC ownership claims and clear the ID when replacement fails.
- [x] Verify `cp-low` and `cp-high`: release/reuse/save/load in both entity orders, failed replacement, and stale occupancy cleanup.
- [ ] Add large-chain/range, mixed-team, grenade-preemption, and further lifecycle tests. Add a full pending ICARUS script and a native multi-squad encounter.
- [ ] Extend perception with confidence and better direct sound/damage reports when needed.
- [x] Coordinate autonomous thermal throws with recent recorded targets, group cooldowns, teammate checks, and arc rejection. Check normal combat release, hidden-target memory, stale records, and save/load in three headless cases.
- [x] Add role-based fire control that counts actual releases. Check three firing profiles, bounded suppression, and stale-contact rejection.
- [x] Add earlier cover requests after visible contact. Check proactive native movement and its opt-out without damage or close-pressure triggers.
- [ ] Playtest firing pauses, suppression points, and proactive retreat frequency. Add moving-ally firing-line, fire-control opt-out, and dedicated burst-state save/load cases.
- [ ] Playtest with diagnostics hidden. Check that movement and barks explain coordination without revealing hidden player information. Defer manual Rend2 checks to the root `human_todo.md` checklist.
- [x] Use recent damage to request retreat cover before a new flank. Keep a bounded crouched hold at the cover point. Verify damage, movement, arrival, release, and no-chase protection in three new cases. All 19 squad cases and 13 memory cases pass under vanilla.
- [x] Add a timed firing-and-cover cycle without damage. Return to a checked firing position after a quiet crouched hold. Request running for autonomous tactical retreats. Add cycle, cancellation, and measured gait checks.
- [x] Add short firing steps beside the same cover anchor, bounded firing intervals, and return movement. React to nearby hostile missile segments without changing enemy memory. Verify repeated peeks, pressure decay and withdrawal, friendly/distant controls, and active-pair save/load.
- [x] Add an adjustable 112-unit pressure radius and immediate local cover through movement cooldowns. Add retreat support, flank-support replacement, and cancellation without a replacement. Recheck cover against confirmed enemy movement and repeated blocked shots.
- [x] Add eleven cases for cooldown bypass, radius and wall shielding, support and handoff, competing local destinations, cover reassessment, hidden movement, and saves during both peek movement directions.
- [x] Add visible close-saber pressure for ranged actors, with a 192-unit default radius and checked direct escape when needed. Add four cases for both commander modes, very close escape, and weapon, visibility, radius, decay, and no-chase controls.
- [x] Let shot, damage, and saber pressure temporarily override combat no-chase and no-retreat orders. Preserve the original flags and active script control. Add held-actor, opt-out, cinematic, and save/load checks.
- [x] Check native `t1_sour` spawners and hold scripts. Enable pressure movement for native ranged classes and snipers. Override scripted crouch and walk during the retreat, then resume the original stance.
- [x] Separate squad membership and reports from ranged movement roles. Include Sith, snipers, and combat droids in local squads while preserving their combat controllers.
- [x] Add adjustable local recruitment, member-contact merges that preserve active plans, rally cover selection, and a short support allowance. Check all 59 squad cases and 13 memory cases across runs.
- [x] Detect blocked movement from actual position changes. Test physical enclosure, claim release before timeout, and recovery after removal. Retain detours and reset samples after pauses or time rollback.
- [ ] Add dedicated recruitment-radius, large-group merge, and rally-hold tests. Make the native `sour-shot` trigger reliable; one run missed its short-lived missile before a passing repeat.
- [ ] Extend contact combat with animated leaning. Test repeated hits, no available cover, pending exposure interruption, larger multi-squad encounters, script-walk orders, and damage during active flank or support roles. Check campaign combat with diagnostics hidden.

Use `roadmap.md` for detailed acceptance checks. Builds that include the
raster-only Rend2 port now select it by default.

## Door Follow-up

- [x] Reproduce the Kril'dor hangar controller-selection defect and allow eligible alternate NPC triggers.
- [x] Verify hangar traversal both ways, an ordinary automatic door, a locked control, and remembered-target approach with restored contact.
- [ ] Test additional player-use, Force-use, inactive, team/named-NPC, and one-sided trigger combinations. Investigate threshold attachment if other doors still fail.

## Display Follow-up

- [x] Expose desktop and custom resolutions through `openjk-play --desktop` and `--resolution WIDTHxHEIGHT`, with aspect-adjusted world FOV.
- [x] Render and validate a non-black 3840x2160 screenshot with vanilla. Keep existing profile settings unless an override is requested.
- [ ] Replace the legacy resolution menu list and investigate widescreen HUD/menu layout separately.

## Save Migration

- [x] Read known project formats 1 through 3 and write format 4. Format 3 adds the cover anchor; format 4 extends the JO animation tables.
- [x] Retain the recorded v1-to-v2, v2-to-v3, and v3-to-v4 migration checks. Verify state preservation and unchanged source files.
- [x] Verify a format-4 round trip and rejection of versions 0 and 5.
- [ ] Run direct v1-to-v4, v2-to-v4, and v3-to-v4 checks with the current package in both renderers after save-layout changes.
- [ ] Qualify additional historical/modded save layouts separately; do not promise compatibility from the version number alone.

## Audio

- [x] Fix overlapping Steam Audio blocks and remove DSP from the SDL device lock.
- [x] Add capture continuity checks, separate effect checks, and peak timing counters.
- [ ] Check the timing fix on the desktop output device. Compare internal and
  device-loopback recordings if crackling continues.
- [ ] After clean playback is confirmed, check many-source cost, tune Kejim,
  Artus Mine, and Yavin, and review sparse probes. See [Audio Handoff](audio_todo.md).

## Rend2 Port

- [x] Replace the compile-only `BuildSPRend2Port` option with `BuildSPRend2`. Link and install `rdsp-rend2_x86_64.so` with shared MP raster code and one shader generator.
- [x] Build both SP renderers with Linux GCC and one job. Rend2 is now the default; vanilla remains available.
- [x] Adapt SP scene/entity submission and native API 23 imports and exports. Retain native SP Ghoul2 array and handle ownership, bones, collision, save data, IK, and ragdolls.
- [x] Add CPU skinning with packed normals and tangents in Rend2 dynamic buffers.
- [x] Require both renderer smoke tests before publication. Verify Rend2 identity and reject vanilla fallback in strict tests.
- [x] Pass `t1_sour` with both renderers and `t2_wedge` with Rend2. Check screenshots with NPCs, textured maps, the player, and a yellow saber.
- [x] Pass OpenGL debug lifecycle checks for `vid_restart`, format-4 save/load in one process, and the `t2_wedge` to `t1_sour` transition.
- [x] Pass the baseline nine squad-tactics cases under Rend2, including active-tactic save/load. The expanded 16-case suite passes with vanilla; Rend2 lifecycle, autosave, and migration checks also pass with the AI changes.
- [x] Pass the lifecycle test with persistent buffers enabled. Keep tag-only weapon models without GPU geometry.
- [x] Fix projected shadows and pass lifecycle checks with stencil and projected shadows. Test with patch stitching disabled.
- [ ] Add a dedicated visual test for the beam draw-order and color fix.
- [x] Render and validate a non-black Rend2 scene at 3840x2160.
- [ ] Test Rend2 performance on the NVIDIA GTX 1080 Ti, audio, and broader manual campaign play. Verify more effects, UI, and cinematic scenes.

The original Rend2 functional tests used Xvfb and LLVMpipe. Hardware P630
performance results are now available in `docs/benchmark-sp.md`.
Keep both renderer options available.
Do not add ray tracing. See `docs/rend2-sp.md` for build, launch, fallback,
and test commands.

## Rend2 Performance

- [x] Add SP GPU skinning with native CPU bone evaluation, float weights, bounded static buffers, and separate frame-slot palettes. Retain CPU fallbacks and verify GPU positions by readback.
- [x] Compare CPU and GPU rendering, animation, weapons, gore, stencil shadows, save/load, and renderer restart. Enable GPU skinning for supported surfaces after measured gains. See `docs/gpu-skinning-sp.md`.
- [x] Add fitted capsule proxies for non-humanoid rigs and bounds-based proxies for rigid droids. Check twenty stock models, detached parts, and scenery exclusions.

- [x] Add an exact-pose Ghoul2 geometry cache with an 8 MiB data budget and reference validation. Test animation, weapons, restart, save/load, map changes, and stencil shadows.
- [x] Skip empty GTAO filter pixels, simplify accepted-sample normalization, and use single-channel GTAO colour buffers. Check storage equivalence and retain the legacy format fallback.
- [x] Measure the second pass in regular first/third-person views and a front-facing character view. Results range from 7% to 36% on the P630. See `docs/benchmark-sp.md`.

- [x] Reduce capsule shader work with conservative receiver rejection and precomputed segment reciprocals. Measure the default-feature scene on the P630 at 720p and 1080p. See `docs/benchmark-sp.md`.
- [x] Add instant enhancement toggles and a same-frame 50/50 comparison. Check split halves, custom settings, humanoids, save/load, and renderer restart. See `docs/graphics-comparison.md`.
- [ ] Repeat performance measurements on the GTX 1080 Ti. Profile CPU skinning, submission, and remaining GPU passes before choosing a Vulkan port.

- [x] Add live world and weapon SSAO strength/radius controls. Preserve the old world sampling footprint at the reference FOV and aspect.
- [x] Add native-resolution spatial GTAO and independent weapon GTAO. Add live quality presets; use the original 8-sample Low as the new default Medium and add a 4-sample Low.
- [x] Add optional full sample shading for main-scene MSAA and alpha-tested depth. Keep it off by default. Check image changes, state restoration, and native-resolution UI.
- [x] Isolate viewmodel AO from world AO, with normal AO depth and the existing visible-weapon depth hack. Add weapon mask/AO debug views.
- [x] Test MSAA 0/4, weapon visibility, wall independence, firing, switching, restart, and save/load on software and P630 hardware rendering.

- [x] Add runtime SSAO ambient-only/broader-lighting comparison and raw/filtered debug views. Keep ambient-only as the default.
- [x] Resolve current MSAA depth before SSAO. Verify controlled MSAA 0/4 captures, mode restoration, and prepass-off behavior.
- [x] Capture SP console screenshots after postprocessing and drain pending requests before restart or quit.
- [ ] Test SSAO during camera motion and in portal views. Static captures do not establish motion-latency correctness. See `docs/ssao-sp.md`.

- [x] Skip unnecessary SP MikkTSpace work and remove the experimental frame-pose tangent cache. Keep skinning and fallback/gore writes unchanged. See `docs/rend2-sp.md` for the final design and full-path conditions.
- [x] Retain linked GL programs and CPU uniform state across soft map resets only. Keep independent mutable state, exact keys, and context cleanup. Do not add lazy compilation or a disk binary cache.
- [x] Limit detailed uniform reflection and logging to `r_verbose`. Guard external-shader reads against missing files and null pointers.
- [x] Pass SP Release, SP Debug, and MP builds; shadows-3 lifecycle checks with persistent buffers; program-cache source/SSAO miss, reuse, and hard-restart checks; and clean shutdown after deliberate external-shader failure.
- [x] Pass parallax-enabled lifecycle smoke, 4K display, and genuine v1 migration checks with Rend2.
- [x] Record paired P630 measurements and verify benchmark `--reloads` with old and new versions. Add recorded `--cvar NAME VALUE` overrides. See `docs/benchmark-sp.md` for results and limits.
- [x] Complete final benchmarks, cache tests, lifecycle, parallax, 4K, and migration checks after frame-pose tangent-cache removal. Test shader failure before and after partial reuse.
- [ ] Profile full-screen passes, copies, fence waits, character worst cases, and GPU times. Measure actual input lag manually on the target desktop; do not infer it from engine-work p99 or predict NVIDIA results from P630.
