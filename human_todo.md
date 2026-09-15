# Human Test Checklist

## Status

Human testing is pending. These tasks do not block automated AI work.
Software-renderer tests already cover map loading, characters, sabers, 4K output,
renderer restarts, save migration, and selected shadow modes. They do not prove
visual quality, audio quality, campaign completion, or GTX 1080 Ti performance.

## Prepare

- [ ] Use the separate development profile. Do not overwrite original saves or settings.
- [ ] Record the package build ID, GPU, driver version, resolution, display refresh rate, and graphics settings.
- [ ] Keep one package fixed during comparisons. The update command can fetch a newer package.
- [ ] Start Rend2 with `openjk-play +set cl_renderer rdsp-rend2 +set logfile 2`.
- [ ] Check the log for `----- rdsp-rend2 -----`. A fallback to vanilla is not a Rend2 test.
- [ ] Check normal startup, menus, character creation, input, fullscreen, and application switching.

The renderer choice is saved in the development profile. Use
`openjk-play +set cl_renderer rdsp-vanilla` to return to vanilla.

## Compare Images

Use the same save, camera position, resolution, and exposure settings for each
comparison. Change one effect at a time. For settings that require a restart,
enter `vid_restart` in the console before the comparison.

- [ ] Compare vanilla and Rend2 in Tatooine (`t1_sour`), Kril'dor (`t2_wedge`), and Hoth (`hoth2`).
- [ ] Compare `r_ssao 0` and `r_ssao 1`. Check corners, feet, and nearby objects. Look for dark halos, flicker, and shading through thin walls.
- [ ] Compare `r_dynamicGlow 0` and `r_dynamicGlow 1`. Check sabers and lights. Bright areas must retain useful detail.
- [ ] Check tone mapping and automatic exposure in bright exteriors and dark interiors. Walk between them. Check brightness changes and readability.
- [ ] Check normal and specular mapping on stock materials. Record missing textures, excessive shine, and incorrect surface detail.
- [ ] Compare character shadow modes with `cg_shadows 1`, `2`, and `3`. Check moving characters, stairs, slopes, and nearby walls.
- [ ] Test beams against opaque scenery. Check beam color, depth occlusion, and visibility during camera movement.
- [ ] Check transparent surfaces, particles, decals, saber trails, Force effects, disintegration, fog, rain, and snow.

Do not assume that parallax mapping or cubemap reflections improve stock maps.
They need suitable material or map data. Test them separately if that data exists.

## Play Campaign Sections

- [ ] Complete a mission in Rend2. Check animated debrief portraits, voice playback, lip synchronization, and cursor visibility. Click Continue and Okay, then select the next mission. Compare with vanilla if it fails. See `docs/debrief-sp.md` for automated coverage.

- [ ] Check conversations, faces, character animation, attached weapons, and cinematic transitions.
- [ ] Check saber combat, ranged weapons, Force powers, and vehicle sections.
- [ ] Complete selected objectives and mission transitions. Loading a map alone does not check progression.
- [ ] Save and load during normal play. Repeat after a renderer restart and a mission transition.
- [ ] Check sound, music, spatial direction, tactical calls, and subtitles during combat.
- [ ] Check HUD and menu readability at standard and wide aspect ratios. Record stretching, clipping, and incorrect pointer alignment.

## Measure Performance

- [ ] Repeat fixed routes at 1920x1080, 2560x1440, and 3840x2160 on the GTX 1080 Ti.
- [ ] Record VSync, frame cap, anti-aliasing, and all changed effects. Keep these settings fixed between comparable runs.
- [ ] Measure frame times with an available capture tool. Record average performance and slow frames, not only peak FPS.
- [ ] Separate first-load shader compilation from repeated runs. Include busy combat and weather scenes.
- [ ] Measure AO, glow, shadows, and anti-aliasing separately before combining them.
- [ ] Select a resolution and frame-rate target from the results. Do not label a preset as verified before these tests pass.

## New Raster Feature Checks

The default Twi'lek player's face and explicit torso-skin materials passed the
SSS eligibility gate. See `docs/raster-features-sp.md` for controls and limits.

- [ ] Compare capsule shadows with `r_capsuleShadows 0` and `1`. Use `r_ssao 1`, `r_depthPrepass 1`, and `r_ssaoAmbientOnly 0`. Temporarily use `cg_shadows 0` to isolate them.
- [ ] Check capsule shadows at feet, on stairs and slopes, near walls, and after dismemberment. Record shadow overlap or leakage through nearby walls.
- [ ] On the default Twi'lek player, inspect `r_sssDebug 1`. Only the supported face and exposed torso regions should be bright. Eyes, teeth, and clothing must remain outside the mask.
- [ ] Compare `r_sss 0` and `0.5`, starting at `r_sssRadius 0.3`. Check facial texture detail, sharp specular highlights, boundaries, and overlap with smoke or refractive effects.
- [ ] Compare `r_softParticleDistance 0` and `8` with `r_softParticles 1`. Check smoke, explosions, additive effects, camera intersections, and depth edges. Particle texture animation can obscure a comparison.
- [ ] Compare `r_smaa 0` and `1` with sample shading disabled. Check diagonals, foliage, small geometry, and motion shimmer. HUD and menu text should retain native-resolution sharpness.
- [ ] Measure effects separately before enabling them together. Record resolution, MSAA, build ID, and slow frame times. Capsule shadows, SSS, and SMAA currently start disabled.

## Material Calibration Decisions

The user reports that half-resolution GTAO works well. It is now the default
when GTAO is selected. The user also approved generated normals with
`r_normalStrength 1` and `r_generatedNormalStrength 0.25`; generation now defaults
to enabled. Roughness, specular, and parallax changes had no obvious effect in
the desktop test. Loading still felt slow. Other calibration checks remain open.
See `docs/materials-sp.md` for full control descriptions and cache details.

- [ ] Choose a stock wall, a metal surface, a character, and a first-person weapon. Use fixed views and exposure for comparisons.
- [x] Confirm the generated-normal defaults on the desktop: generation enabled, normal strength `1`, generated strength `0.25`.
- [ ] Compare `r_normalStrength 0` and `1`. Decide whether relief comes from the normal map or from colour already painted into the texture.
- [ ] With `r_normalStrength 1`, compare `r_generatedNormalStrength 0.1`, `0.25`, `0.5`, and `1`. The current default is `0.25`. Select the preferred value for generated maps.
- [ ] Test authored normal maps separately if a texture pack supplies them. Use `r_normalStrength` to adjust them; `r_generatedNormalStrength` does not affect them.
- [ ] Check colour and exposure with `r_generatedNormalBrighten 0` (default). Compare with `1` only if needed; use `vid_restart` after each change. Decide whether any diffuse compensation is useful.
- [ ] Compare `r_specularStrength 0` and `1`, then select a strength. Check floors and characters for excessive shine. The default is `1`.
- [ ] Adjust `r_roughnessScale` (default `1`) and `r_roughnessFloor` (default `0`). Check that metal retains useful highlights and rough surfaces do not look wet.
- [ ] On a material with height data, enable `r_parallaxMapping 1` and restart. Compare live `r_parallaxScale 0`, `0.25`, `0.5`, and `1`. The default is `0.5`. Check grazing angles and texture edges. Stock generated normals alone do not establish this result.
- [ ] Load the same map twice with `r_genNormalMaps 1` and `r_normalMapCache 1`. Check perceived load time, image consistency, and the `Normal maps:` log counters. Repeat after a texture-pack change to check for stale relief.
- [ ] Record the preferred values, map/save, texture pack, resolution, and screenshots. Confirm which values should become the final defaults. Current material defaults are a starting point.

All strength and roughness controls above change live. Texture-generation
enablement, diffuse compensation, and parallax enablement require a restart.

## Jedi Outcast Campaign

The JO prototype loads on the desktop. Full mission completion is not yet
verified. The automated checks cover the opening, equipment, save/load, and an
explicit map transition. They do not complete the final Kejim puzzle.

Start a new JO test with:

```bash
openjk-play --worktree jed-joi --campaign jo --new-game --desktop
```

Omit `--new-game` to open the menu and load a save. Use one package for a
playthrough. Record its build ID. Enter `campaign_status` in the console to
record the map, position, equipment, and active objectives.
See `docs/jo-campaign.md` for setup and renderer options.

### Priority 1: Complete Kejim Through Normal Play

- [ ] Complete `kejim_post` without console workarounds. Collect the three codes, solve the code-entry puzzle, and use the mission's actual exit.
- [ ] Complete `kejim_base` without console workarounds. Check the objectives and the next mission transition.
- [ ] Check Jan's movement. She must follow Kyle, reach consoles, open required doors, and recover after combat without getting stuck.
- [ ] Check doors, lifts, switches, code notifications, and datapad objectives. Record a sequence that stops or an objective that does not update.
- [ ] Check weapon pickups, ammunition, enemy reactions, and friendly fire. Compare a nearby missed shot with a direct hit when investigating an unresponsive enemy.
- [ ] Check dialogue, subtitles, music changes between exploration and combat, and cinematic transitions. Test cinematic skipping in a separate session.
- [ ] Save during combat and during a scripted task. Load each save and check that the encounter or task continues.
- [ ] Die and reload. Check that objectives, equipment, and required NPCs remain correct.
- [ ] After a mission transition, check carried weapons, ammunition, health, and Force powers. Quit, restart, load a save, and continue.

Acceptance: complete both Kejim missions through normal play, then load a save
and continue correctly. Keep a save before each reproducible blocker. Record
what Jan or another required NPC was doing, what you expected, and what stopped.

### Priority 2: Force Training

After Kejim, check `yavin_temple` and `yavin_trial`. Direct map tests do not
establish that the intervening Artus missions are complete.

- [ ] Check the scripted Force unlocks and power levels.
- [ ] Complete the Force training puzzles and required jumps.
- [ ] Acquire the saber and check selection, attacks, and the shared controls.
- [ ] Save and load before and after a power unlock or saber acquisition. Check that progression remains correct.

### Priority 3: Remaining Campaign Systems

- [ ] Artus: check escorts, prisoner releases, multi-stage objectives, and mission transitions.
- [ ] Bespin: check saber encounters, scripted duels, and cinematics.
- [ ] Doomgiver: check Galak's boss states, damage rules, and encounter completion.
- [ ] Yavin Swamp: check water, collision, required movement routes, and scripted encounters.
- [ ] Finale: complete Desann's encounter and check the ending sequence.

## Report Results

For each failure, record the build ID, map or save, exact steps, settings, and
expected result. Attach the log and a screenshot or short video. State whether
the same problem occurs in vanilla. Keep passing results as well as failures.
Leave unchecked tasks open; an automated pass does not replace a human result.
