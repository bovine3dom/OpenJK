# Rend2 Human Test Checklist

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

## Report Results

For each failure, record the build ID, map or save, exact steps, settings, and
expected result. Attach the log and a screenshot or short video. State whether
the same problem occurs in vanilla. Keep passing results as well as failures.
Leave unchecked tasks open; an automated pass does not replace a human result.
