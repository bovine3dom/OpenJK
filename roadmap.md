# Single-Player Roadmap

## Scope

Improve Jedi Academy single-player in an OpenJK-derived project. This roadmap
describes work to investigate, not approved implementation specifications.

- Do not require compatibility with existing mods or saves.
- Give single-player priority. Network functionality is not an acceptance requirement.
- Keep the original campaign as the initial test target. Test its scripts and progression.
- Use raster rendering only. Do not add ray tracing or require ray-tracing hardware.
- Investigate controls, camera, movement, collision, enemy behaviour, and a Rend2 port.
- Defer broad simulation-timing changes and physical-environment features.

These goals differ from upstream OpenJK, which excludes major gameplay changes.
The source licence does not grant permission to redistribute the original assets.

The original Jedi Academy assets are in GameData/

## Review Findings

Contemporary reviews identify the following technical complaints. They do not
establish that each problem remains in OpenJK.

| Complaint | Evidence and limits |
| --- | --- |
| Dated graphics | WorthPlaying and Eurogamer criticised the ageing visuals. Eurogamer also noted limited environmental detail and crude facial animation. |
| Weak ranged-enemy AI | Eurogamer described stationary enemies that did not use cover or flank effectively. This concerns game behaviour and encounter design, not an inherent engine limit. |
| Awkward controls and camera | Eurogamer criticised Force-power selection and combat perspective. IGN noted awkward directional inputs but considered them learnable. |
| Movement and animation | Eurogamer's Xbox follow-up criticised jumping and contact with scenery. It also reported scenery pop-in in the PC version. These findings have less independent support. |

Reviews were not unanimous. IGN praised saber animation. WorthPlaying praised
responsiveness. Story and mission-design complaints need separate content changes.

Sources:

- [WorthPlaying PC review](https://worthplaying.com/article/2003/9/29/reviews/12787-pc-review-jedi-knight-jedi-academy/)
- [Eurogamer PC review](https://www.eurogamer.net/r-jediacademy-pc)
- [IGN PC review](https://www.ign.com/articles/2003/09/16/star-wars-jedi-knight-jedi-academy-review)
- [Eurogamer Xbox follow-up](https://www.eurogamer.net/r-jediacademy-x)

## Vulkan Investigation (Deferred)

Vulkan work is deferred at the user's request. Keep the current OpenGL renderer.

Measure CPU submission time, GPU pass time, and load phases before a renderer
migration. Vulkan can reduce driver overhead, but texture decoding, CPU skinning,
and expensive pixel shaders still need separate work. Pipeline creation can
still cause load delays.

[JKSunny/EternalJK](https://github.com/JKSunny/EternalJK) has a Quake3e-derived
Vulkan backend under `codemp/rd-vulkan`, a Rend2-derived PBR branch, and resource
and instancing experiments. [TaystJK](https://github.com/taysta/TaystJK) integrates
the Vulkan backend. The source review is recorded in
[`docs/vulkan-investigation.md`](docs/vulkan-investigation.md).

Evaluate this work before designing a new backend. The inspected target is MP;
SP support and parity with our rendering passes still need validation. The
instancing experiment reports both gains and regressions, depending on the scene.
No fork was built or benchmarked in this review. Implementation remains deferred.

[Mesa Zink](https://docs.mesa3d.org/drivers/zink.html) provides OpenGL over Vulkan.
Where supported, use it for an initial driver comparison. Its results do not
predict the performance of a native Vulkan renderer. Approve a migration only
after a prototype shows a measured benefit on the target hardware.

## Work Areas

Estimates are approximate full-time effort for one experienced developer. They
exclude extensive asset production and full platform testing. Source inspection,
not runtime testing, supports the initial assessment.

| Area | Initial scope | Estimated effort |
| --- | --- | --- |
| 1. Controls and camera | Investigate a Force-power radial menu, better bindings, and camera behaviour near obstacles. | 1-2 months |
| 2. Movement and collision | Reproduce scenery snags, step and jump problems, and specific saber-hit inconsistencies. Fix confirmed defects before changing combat rules. | 1-3 months |
| 3. Enemy behaviour | Improve squad decisions, reactions, positioning, and coordinated flanking. | 2-4 months for targeted improvements; 6-12+ months for broad AI and navigation replacement |
| 4. Lighting and shadows | Port the experimental multiplayer Rend2 renderer to single-player. Test raster lighting, shadow maps, materials, effects, and performance. | 3-9+ months |

OpenJK already includes raw mouse input, widescreen fixes, camera collision checks,
and substantial saber collision logic. Rend2 already includes HDR rendering,
normal and specular mapping, ambient occlusion, and shadow-map paths. Internal HDR
rendering does not imply HDR display support. Original assets will limit visual
improvements without further content work.

Rend2 now runs initial SP scenes with software rendering. Choose a minimum
graphics target and measure performance on the weakest target GPU.
The NVIDIA GTX 1080 Ti is available, but Rend2 performance is not yet tested.

### Investigation Checklist

Complete these steps before broad implementation:

1. Build and run the unmodified single-player target. Record the revision, build options, game-data version, GPU, driver, resolution, and settings.
2. Select repeatable campaign scenes: a narrow corridor, stairs and ledges, an open firefight, a saber duel, and a scripted sequence. Record map names, start positions, reproduction steps, and reference captures.
3. Record the available GPUs, then agree on minimum hardware, resolution, and frame-time targets. Do not assume that every available GPU supports every Rend2 feature.
4. Keep a comparison build and development switches for prototypes. These support testing, not long-term mod or save compatibility.
5. For each change, record the observed problem, expected result, test result, and remaining limitations. Use debug logs and repeatable seeds where practical.

### Controls and Camera

Start in `code/cgame/cg_view.cpp` and the input paths in `shared/sdl/`.

1. Test camera obstruction, recovery after obstruction, crosshair alignment, and rapid turns before selecting a fix.
2. Prototype Force-power selection. Decide whether the radial menu pauses, slows, or leaves gameplay unchanged. Keep direct bindings available.
3. Test mouse and keyboard input, input release when menus close, and transitions to vehicles and cinematics. Investigate controller support separately if required.

Acceptance: the camera does not enter solid scenery in the test cases; aiming
remains aligned; selection does not leave movement or attacks active after release.
Test standard and wide aspect ratios. Do not change scripted cameras by accident.

### Movement and Collision

Start in `code/game/bg_pmove.cpp`, `code/game/wp_saber.cpp`, and
`code/qcommon/cm_trace.cpp`.

1. Reproduce each snag or hit error. Capture movement input, collision traces, blade positions, animation state, and damage or block decisions as applicable.
2. Separate world collision defects from animation, blade sampling, and combat-rule problems. Do not treat every blocked attack as a missed collision.
3. Make one bounded correction at a time. Compare stairs, slopes, jumps, moving platforms, and saber duels with the reference build.

Acceptance: the reproduced defect is corrected without new failures in those
cases. Required campaign jumps, doors, and triggers still work. Test several frame
rates to detect regressions, without expanding scope into a simulation rewrite.

### Rend2 Port

Compare `codemp/rd-rend2/` with `code/rd-vanilla/` and the renderer build targets.

1. Complete: use native SP API 18 and Ghoul2 ownership with shared MP raster code. The first playable path uses CPU skinning in Rend2 dynamic buffers.
2. Complete: link, install, and load `rdsp-rend2_x86_64.so`. Pass the `t1_sour` baseline with both renderers and load `t2_wedge` with Rend2.
3. Validate animated characters, sabers, transparent surfaces, particles, decals, UI, and cinematics before enabling additional visual effects.
4. Add and measure raster lighting, shadows, and material features individually. Provide quality settings for expensive features. Do not add ray tracing.
5. Compare fixed camera captures and frame times on each target GPU. Record unsupported features and visual defects.

Builds that include SP Rend2 now select it by default. Vanilla remains available.
Linux GCC builds used one job. Xvfb and LLVMpipe checks passed for renderer
lifecycle, active AI tactic save/load, and save migration.
Migration and renderer lifecycle checks passed after the CP ownership changes.

Rend2 4K and lifecycle checks with stencil and projected shadows passed.
Hardware performance, audio, and broader manual campaign checks remain open.
Defer these manual checks to the root `human_todo.md` Rend2 checklist.
The beam fix still needs a dedicated visual test.
See `docs/rend2-sp.md` for commands and test limits.

Acceptance: the representative scene works without missing models, effects, or
UI; the agreed performance target is met on the minimum GPU. Follow with campaign
sampling before declaring the port complete. Track asset upgrades separately.

## Deferred Rendering Work

The following work is deferred. Reuse stock art where possible, but allow
generated caches and explicit material or map configuration.

- [ ] Volumetric fog: derive initial media from existing fog volumes; provide
  conservative density controls and preserve authored scene visibility.
- [ ] Light shafts: use known light sources and shadow information. Do not infer
  every baked light from texture brightness or force outdoor sunlight indoors.
- [ ] Indirect lighting: prototype screen-space colour bounce, including
  visibility-bitmask methods, with controls for existing baked illumination.
  Measure filtering cost and artifacts from missing off-screen geometry.

References: [Wronski's volumetric rendering work](https://bartwronski.com/publications/)
and [visibility-bitmask indirect lighting](https://arxiv.org/abs/2301.11376).
Physics-driven animation is tracked separately in `animation_todo.md`.

## Squad Behaviour

### Existing Systems

Single-player already has groups, commanders, combat-point reservations, cover and
flank tests, covering fire, morale, and last-seen records. Some morale-driven
combat-point choices appear to have no callers. ST hidden-target facing, distance,
short-loss CP search, and solo enemy goals now use known personal or same-enemy
group positions. Group sorting uses actual member nodes and the target record
node; the legacy insertion `k++` stack overflow is corrected. `NPC_StartFlee` CP
retries retain the supplied danger point rather than hidden enemy coordinates.

Accepted target changes or clears remove memory and old memory goals. Same-target
and rejected locked-target assignments preserve memory. Tactic cancellation and
group removal clear movement speech and chance. CP release clears all NPC claims;
failed replacement clears the ID. Full-save load restores occupancy from NPC
claims; autosave load does not.

Thirteen memory and 67 tactical cases have passed across test runs, including
`solo-switch`, `cp-low`, and `cp-high`. The `route-recovery` test uses a physical
enclosure and checks claim release before timeout. The cinematic test simulates
`BS_CINEMATIC` with an external goal, not a full pending ICARUS script. Contested
reservation testing uses the API, not a real encounter with multiple squads.

Local recruitment now has an adjustable 768-unit default radius. Member-contact
merges preserve active tactical plans. Cover selection can prefer a nearby ally's
rally position. Blocked-route recovery measures actual travel and retains valid
detours. Dedicated radius-boundary and rally-hold tests remain open. One native
short-shot test missed its missile before a passing repeat. See
`docs/squad-tactics.md` for the test limits.

Autonomous grenades now use recent recorded contact, group cooldowns, teammate
checks, and arc rejection. Fire control counts actual releases and separates
short bursts, deliberate shots, and automatic-weapon support. Suppression uses
bounded hypotheses around recent contact. Proactive cover can start before
damage or nearby shots. The suite now contains 67 cases. See
`docs/fire-control-research.md` for sources and game-specific intervals.

Solo fixtures use `d_noGroupAI 1`. `SCF_NO_GROUPS` selects the separate legacy
formation controller, `AI_HazardTrooper`; its hearing, steering, and chase
restrictions still need correction. Other NPC controllers, generic callers,
and full FOV and attention remain outside this work. Do not claim engine-wide
removal of hidden-target knowledge. See `docs/squad-ai.md` and
`docs/squad-tactics.md` for test details and limits.

Relevant code is in `code/game/AI_Stormtrooper.cpp`, `AI_Utils.cpp`,
`NPC_combat.cpp`, and `AI_Grenadier.cpp`.

### Proposed Approach

Extend the existing group controller before considering a new AI framework.

- Give squad members temporary roles: engage, flank, or reserve. Reserve positions and limit simultaneous grenade attacks.
- Use utility scores to select valid actions and combat points. Consider cover, firing angle, route cost, ally positions, danger, and morale.
- Keep roles stable for a short period. Cancel them when routes fail, observations expire, or immediate danger requires a response.
- Store shared observations with a source, time, and confidence. Separate direct sight, sound, and squad reports. Do not track hidden players through walls.
- Coordinate short plans: engage from cover, move a flanker, then attack from separate directions. Do not require a general-purpose planner for the first prototype.
- Judge distraction from observed player facing and attacks. Never assume that firing at the player forces their attention.
- Spread out against saber attacks and Force powers. Avoid ledges where practical. Keep retreat routes open instead of surrounding the player at close range.
- Use grenades to encourage movement, with warnings and limits. Do not remove the player's ability to return them with Force powers.
- Let casualties and isolation cause hesitation or retreat. Let nearby support restore confidence. Preserve opportunities for the player to break coordination.
- Make tactics visible through movement and tactical barks. Include barks in the first prototype, not only in final audio work.
- Limit combined attack pressure so that coordinated enemies remain fair. Do not add perfect aim, hidden-state knowledge, or forced player suppression.

Flanking must produce a useful advantage under the actual saber-blocking rules.
Test this before assuming that a rear attack bypasses defence. Any combat-rule
change is a separate design decision, not an AI bug fix.

### Tactical Barks

**Priority: use short spoken calls to make squad coordination understandable.**
The user's reference is the audible coordination they enjoyed in Crysis. This is
a design goal, not a requirement to copy its implementation or audio assets.

Barks should describe actual observations, orders, actions, and failures. They
must help the player recognise a threat and choose a response. Do not use random
tactical dialogue to imply a plan that the squad does not carry out.

| Event | Example intent, not confirmed available dialogue |
| --- | --- |
| Engage-and-flank plan starts | "Cover me!" followed by an acknowledgement from the covering member. |
| Flanker starts moving | "Going around!" |
| Jedi closes on the squad | "Spread out!" or "Keep your distance!" |
| Grenade attack starts | A warning that gives the player time to respond. |
| Sight is lost | "Lost contact!" rather than a claim about the player's hidden position. |
| Route fails or a member falls | "Can't get through!" or a call to regroup. |
| Squad retreats | A withdrawal order and a covering member's response. |

Implementation steps:

1. Audit `ST_Speech`, stored movement speech, and `G_AddVoiceEvent` in `code/game/AI_Stormtrooper.cpp`. Existing categories include cover, outflank, lost contact, and detection. Listen to the available assets; event names alone do not establish the spoken meaning.
2. Make a table that maps squad events to suitable clips and subtitle text. Mark missing lines. Reuse existing assets where their meaning fits; use clearly marked placeholders during development.
3. Emit bark requests when observations or plan phases change, not on every AI update. Attach the speaker, event, priority, expiry time, and plan identity where relevant.
4. Use per-speaker and squad-wide cooldowns. Avoid repeated clips and overlapping calls. Give urgent danger warnings priority over routine acknowledgements.
5. Discard queued lines when the speaker dies, the plan ends, or the information becomes stale. An interrupted spoken order may remain audible; a later failure call should explain the change when useful.
6. Use spatial audio for local calls. Define radio range and routing separately if radio calls are added. Provide optional subtitles with speaker identification, including for players who cannot hear the calls.
7. Keep AI coordination independent of audio playback. A muted or skipped clip must not stall a plan. Model communication failure explicitly if it becomes a gameplay feature.

Test barks with the debug display disabled. A player should be able to recognise
coordination and a change of plan from the calls and movement. Test repetition,
overlap, cancelled plans, dead speakers, subtitles, and audibility during combat.
Do not announce every hidden position or make every flank harmless through an
exact warning. Give enough information for a fair response, not a complete plan.

Record missing voice assets as content tasks. Final recording, editing,
localisation, and permission checks need a separate estimate. Do not use Crysis
audio assets.

### First Prototype

Use three or four ranged enemies in an area with two valid routes. Assign one
engaging enemy and one flanker. Keep another enemy available to cover a retreat.
Compare this with the current behaviour using unchanged health and damage.

Implement the prototype in this order:

1. Trace the active commander, combat-point, perception, and speech paths. Confirm which existing features run in the selected encounter.
2. Add debug displays and event logs for observations, assignments, plan phases, point reservations, and barks.
3. Add observation timestamps and confidence. Test loss of sight before adding flank decisions.
4. Assign engaging, flanking, and reserve roles. Score only points that pass route, occupancy, script, and friendly-fire checks. Add a minimum role duration to prevent rapid switching.
5. Implement prepare, engage, flank, and regroup phases. Set timeouts and handle blocked routes, casualties, stale observations, and player approach. Release reservations when plans end.
6. Connect orders, acknowledgements, and failure barks to those phases. Test the plan with audio and subtitles before adding more tactics.
7. Compare repeated encounters with unchanged health and damage. Record completed and cancelled flanks, time spent stuck, friendly-fire incidents, and player feedback on fairness and clarity.

Acceptance: demonstrate a completed flank and a player-disrupted flank. After loss
of sight, enemies search from observations rather than track the hidden player.
When a route or plan fails, enemies release reservations and choose a valid action.
Scripted NPC control takes precedence. Players can identify the coordination
without debug aids. Increased player deaths alone do not establish success.

Allow approximately 4-8 weeks for this bounded prototype and its debug tools.
This is part of the enemy-behaviour estimate, not an additional commitment.
Existing map routes and combat points may limit the result; test several campaign
encounters before deciding whether to add navigation data or replace navigation.

Research references for further investigation:

- [Game AI Pro](https://www.gameaipro.com/): utility decisions, tactical position selection, multi-unit plans, perception, and attack management.
- [Game AI Pro Online Edition 2021](https://www.gameaipro.com/): Squad Coordination in Days Gone and Knowledge is Power.
- [F.E.A.R. AI presentation](https://gdcvault.com/play/1013459/Three-States-and-a-Plan): a reference for combat planning, not a requirement to adopt its architecture.

## Deferred Work

| Area | Previous estimate | Status |
| --- | --- | --- |
| Consistent simulation | 3-9+ months to separate simulation timing more fully from rendering. | Deferred. This was an engineering proposal, not a review consensus. |
| Physical environments | 3-6 months for selected physics props and breakable objects; 12+ months for broad destruction. | Deferred. Broad destruction also requires map work. |

## Initial Sequence

1. Establish reproducible camera, movement, and collision test cases.
2. Prototype coordinated ranged enemies using existing groups and combat points.
3. Prove a raster-only Rend2 port on a representative single-player map.
4. Use those results to set implementation scope and revise the estimates.

A focused 3-6 month phase could improve controls, movement, and selected encounters.
Treat the renderer port as a separate effort. Dropping compatibility removes some
constraints, but it does not remove the need to test campaign progression.

---

- autosaves every few minutes with i guess grandfather-father-son - need to make them off the main thread / async so they don't cause stutter
- skybox improvements, i guess depth of field? volumetric fog? the problem is that screens are much higher resolution now than when they were first designed, so we can see every imperfection including seams. maybe volumetric fog?
- have lightsabers cast shadows like the torch, but less distance?
- check https://github.com/JKSunny/EternalJK for an already in-progress vulkan port
- consider looking at https://github.com/taysta/TaystJK for features to cherry-pick
- modern sound engine with attenuation etc through walls?
- better glass materials? still visible as glass, but less ... ugly and white? (started in glass_test, but it looks bad; refraction looks terrible and the reflections are too white and shiny. do more research on how games make glass visibly glass)
- make jolt apply to pull/push force powers. consider how to integrate jolt with lightsabers?
- what are the tradeoffs/benefits from porting rend2 to vulkan?
- animation: get the first physics-based ragdoll for a live stormtrooper working
- jedi outcast: what's the current status of it and how plausible is it to make one kind of grand unified game that unifies the UI, logic, AI, weapons, force powers etc. etc... between JA and JO?
- suppression: improve fire control of enemies - use real world rates of fire/bursts for standard troops, snipers, machine gunners. encourage enemies to suppress last known positions etc, particularly during manoeuvre
- gameplay: make it easier to activate 'moves' like wall-running, katas, jump slash... make the combinations more forgiving timing wise
- better glass materials? still visible as glass, but less ... ugly and white?
- make jolt apply to pull/push force powers. consider how to integrate jolt with lightsabers?
