# RmlUi Reticle MVP

## Use

The Jedi Academy SP client enables the RmlUi reticle by default. Both vanilla
and Rend2 support it. Use matching client, game, and renderer binaries from
the same package. The SP renderer API version is now 19.

Use these console settings:

| Setting | Function |
| --- | --- |
| `cg_rmluiReticle 1` | Use the new reticle. |
| `cg_rmluiReticle 0` | Use the selected legacy crosshair. |
| `cg_rmluiReticleScale 0.75` | Use the default dot and indicator scale. The range is 0.25 to 4. |
| `cg_rmluiHud 1` | Use contextual resource indicators. This is the default. |
| `cg_rmluiHud 0` | Restore the bottom-right panel. Keep the RmlUi dot. |
| `cg_crosshairSize 24` | Set the base size in legacy vertical UI units. |
| `cg_drawCrosshair 0` | Hide the reticle and restore the bottom-right panel. |

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

The resource arcs start at the top and extend clockwise. A faint full outline
shows the capacity, including when the resource is empty. The Force ring can
appear with either ammo or stance. There are no permanent numbers or labels.
At idle with full Force energy, only the small dot remains.

The bottom-right panel is hidden only when the new HUD can draw in that frame.
The panel returns if the dot is disabled or cannot be drawn. Vehicles and
controlled entities keep their existing HUDs. The health and armor panel,
weapon selection, datapad, and other HUD elements are unchanged.

## Integration Limits

This change replaces the normal SP crosshair and resource panel. It does not replace
the panel-turret artwork, Force corona, MP reticle, or Jedi Outcast reticle.
It does not change collision tests or the timing of target identification.

The client owns one RmlUi context. It draws only when cgame requests a reticle.
The context does not receive input or take focus. The client destroys it before
renderer shutdown and creates it again after registration. This includes map
changes and `vid_restart`. Initialization failure selects the legacy path.

The embedded RML is project-owned source under GPL-2.0-or-later. It needs no
font or image files. The renderer adapter supports untextured triangles and
rectangular clipping only. Text, textures, transforms, and advanced RmlUi
effects are not supported by this MVP.

Each renderer command owns its geometry and clip data. One command accepts at
most 512 vertices and 1536 indices. It uses straight alpha and restores clipping
after drawing. This is not yet a complete renderer for general RmlUi screens.

## Tests

Build and test with one build job:

```sh
bash scripts/build-sp.sh
python3 scripts/test-rmlui-reticle.py build/ready
python3 scripts/test-rmlui-reticle.py --hud build/ready
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

Rend2 screenshots now capture the completed frame, including the HUD. Leave
at least two `wait` frames after a screenshot command before changing the test
state or restarting the renderer.

Human checks are still required for collision movement, enemy and friendly
colors, Force hints, vehicles, pickup animation, reduced view size, and hardware
rendering. The automated test uses a fixed center. It does not prove those
gameplay cases or the full migration test list in `ui_todo.md`.

See [RmlUi Dependencies](rmlui-dependencies.md) for versions, hashes, build
settings, offline source overrides, and licenses.
