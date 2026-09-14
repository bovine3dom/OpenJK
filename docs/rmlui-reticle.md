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
| `cg_rmluiReticleScale 1` | Use the normal scale. The range is 0.25 to 4. |
| `cg_crosshairSize 24` | Set the base size in legacy vertical UI units. |
| `cg_drawCrosshair 0` | Hide the reticle. |

The new reticle has one shape. Other nonzero `cg_drawCrosshair` values do not
change that shape. They still select the legacy artwork when the new path is off.

The reticle size uses the framebuffer height, not its width. At 720 pixels high,
`cg_crosshairSize 32` gives a 48 by 48 pixel reticle at scale 1. Scale 2 gives
96 by 96 pixels. The center keeps the existing collision-based position and
crosshair offsets. The existing target logic supplies the colors. Item pickup
still changes the size. Existing zoom, death, and cinematic rules still apply.

## Integration Limits

This change replaces only the normal SP crosshair artwork. It does not replace
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
bash scripts/build-sp.sh --stage-only
python3 scripts/test-rmlui-reticle.py /path/to/staged/JediAcademy
```

The test uses Xvfb and software OpenGL. It checks both renderers at 960 by 720
and 1280 by 720. It measures the reticle center, equal width and height, scale,
arms, and outline. It also checks hiding, legacy fallback, and `vid_restart`.
Each run stores screenshots, logs, and a JSON report in `build/reticle-tests`.

Rend2 screenshots now capture the completed frame, including the HUD. Leave
at least two `wait` frames after a screenshot command before changing the test
state or restarting the renderer.

Human checks are still required for collision movement, enemy and friendly
colors, Force hints, vehicles, pickup animation, reduced view size, and hardware
rendering. The automated test uses a fixed center. It does not prove those
gameplay cases or the full migration test list in `ui_todo.md`.

See [RmlUi Dependencies](rmlui-dependencies.md) for versions, hashes, build
settings, offline source overrides, and licenses.
