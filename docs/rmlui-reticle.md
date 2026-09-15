# RmlUi Reticle MVP

## Use

The Jedi Academy SP client enables the RmlUi reticle by default. Both vanilla
and Rend2 support it. Use matching client, game, and renderer binaries from
the same package. The SP renderer API version is now 22.

Use these console settings:

| Setting | Function |
| --- | --- |
| `cg_rmluiReticle 1` | Use the new reticle. |
| `cg_rmluiReticle 0` | Use the selected legacy crosshair. |
| `cg_rmluiReticleScale 0.75` | Use the default dot and indicator scale. The range is 0.25 to 4. |
| `cg_rmluiHud 1` | Use contextual resource indicators. This is the default. |
| `cg_rmluiHud 0` | Restore both bottom panels. Keep the RmlUi dot. |
| `cg_crosshairSize 24` | Set the base size in legacy vertical UI units. |
| `cg_drawCrosshair 0` | Hide the reticle and restore both bottom panels. |

The new reticle is a small round dot with 65% fill opacity and a faint dark edge.
Other nonzero `cg_drawCrosshair` values do not
change that shape. They still select the legacy artwork when the new path is off.

The reticle size uses the framebuffer height, not its width. At 720 pixels high,
`cg_crosshairSize 32` gives a 6 pixel diameter at scale 1. Scale 2 gives
12 pixels. The dot uses one eighth of the legacy crosshair size.
The center keeps the existing collision-based position and
crosshair offsets. The existing target logic supplies the colors. Item pickup
still changes the size. Existing zoom, death, and cinematic rules still apply.

Existing saved scale settings take priority over the new default. Use
`reset cg_rmluiReticleScale` to select 0.75.

## Contextual Resource Indicators

The indicators follow the reticle's collision-based position. They use uniform
pixel scaling and fixed radii, so item pickup does not expand the rings.

- **Force:** A thin light-blue inner ring shows the remaining energy. It stays
  visible while a Force power is active, while energy is below maximum, or after
  a failed Force action. It fades after recharge completes. The existing
  low-Force sound remains active.
- **Ammo:** A thin light-yellow outer ring shows the remaining ammo for the
  equipped weapon. Firing, charging, weapon changes, and ammo changes reveal it.
  It stays visible for 1.5 seconds after activity, then fades for 0.6 seconds.
- **Saber stance:** A short colored arc replaces the ammo ring while using a
  saber. Fast is blue at the upper left. Medium is yellow at the top. Strong is
  red at the upper right. Dual and staff use the medium position, as in the old
  panel. Stance changes, attacks, blocks, locks, and saber throws reveal it.
  It stays visible for 0.8 seconds after activity, then fades for 0.6 seconds.

The Force and ammo arcs start at the top and extend clockwise. A faint full outline
shows the capacity, including when the resource is empty. The Force ring can
appear with either ammo or stance. There are no permanent numbers or labels.
At idle with full Force energy and health above 25%, only the small dot remains.

## Health And Shields

Two short arcs sit below the resource rings. Health is muted red at the lower
left. Shields are green at the lower right. Each arc shows its own remaining
percentage. The game uses maximum health as the shield capacity.

- Health or shield loss reveals both arcs for 5 seconds after the last loss.
- Healing or a health or shield pickup reveals both arcs for 3 seconds. A pickup
  does not shorten an active damage display.
- Both arcs then fade for 0.6 seconds. Partial depletion does not keep them visible.
- At 25% health or below, a faint health arc remains visible. It does not pulse
  or flash. This also applies after a save load or renderer restart.
- Empty shields do not cause a persistent warning. Their empty outline appears
  with the health arc after damage, then fades.

The bottom panels are hidden only when the new HUD can draw in that frame.
They return if the dot is disabled or cannot be drawn. Vehicles and controlled
entities keep their existing HUDs. Weapon selection and the datapad are unchanged.
The first version has no hold-to-check binding or numeric status display.

## Integration Limits

This change replaces the normal SP crosshair and both resource panels. It does not replace
the panel-turret artwork, Force corona, MP reticle, or Jedi Outcast reticle.
It does not change collision tests or the timing of target identification.

The client owns the reticle and selection-wheel contexts. The reticle context does
not receive input or take focus. The client destroys the contexts before
renderer shutdown and creates them again after registration. This includes map
changes and `vid_restart`. Initialization failure selects the legacy path.

The embedded RML is project-owned source under GPL-2.0-or-later. The reticle
uses geometry only. The Force and weapon wheel labels use bundled IBM Plex Mono and
FreeType. The renderer supports triangles, rectangular clipping, and generated
RGBA font textures. Image-file loading, transforms, and advanced RmlUi effects
are not supported by this MVP.

Each renderer command owns its geometry and clip data. One command accepts at
most 512 vertices and 1536 indices. It uses premultiplied alpha and restores
clipping after drawing. Generated textures have explicit release calls that
drain queued draws. This is not yet a complete renderer for general RmlUi screens.

## Tests

Build and test with one build job:

```sh
bash scripts/build-sp.sh
python3 scripts/test-rmlui-reticle.py build/ready
python3 scripts/test-rmlui-reticle.py --hud build/ready
python3 scripts/test-rmlui-reticle.py --vitals build/ready
c++ -std=c++11 -I shared tests/reticle_hud.cpp -o build/reticle-hud-test
build/reticle-hud-test
```

The test uses Xvfb and software OpenGL. It checks both renderers at 960 by 720
and 1280 by 720. It measures the dot center, equal width and height, scale,
rounded corners, and transparency. It also checks hiding, legacy fallback, and `vid_restart`.
Each run stores screenshots, logs, and a JSON report in `build/reticle-tests`.
The HUD test uses gameplay commands to check resource arcs, stance positions,
attack visibility, idle fading, and panel replacement. The C++ test checks
activity timing, resource limits, simultaneous states, and reset behavior.
The vitals test checks health and shield changes, critical health, recovery,
restart behavior, left-panel fallback, and simultaneous resource drawing.

Rend2 screenshots now capture the completed frame, including the HUD. Leave
at least two `wait` frames after a screenshot command before changing the test
state or restarting the renderer.

Human checks are still required for collision movement, enemy and friendly
colors, Force hints, vehicles, pickup animation, reduced view size, and hardware
rendering. The automated test uses a fixed center. It does not prove those
gameplay cases or the full migration test list in `ui_todo.md`.

See [RmlUi Dependencies](rmlui-dependencies.md) for versions, hashes, build
settings, offline source overrides, and licenses.
