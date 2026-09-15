# Live Graphics Comparison

Use Rend2 and enter one of these commands in the console:

```text
r_compareEnhancements 0
r_compareEnhancements 1
r_compareEnhancements 2
```

| Value | Result |
| --- | --- |
| `0` | Normal enhanced scene. This is the default. |
| `1` | Base lighting across the screen. |
| `2` | Base lighting on the left; enhanced rendering on the right. |

Changes are immediate. They do not run `vid_restart`, load a save, or change the
camera. The split uses the same scene, camera, and simulation time for both sides.
Your graphics settings stay unchanged.

Optional key bindings:

```text
bind F10 "toggle r_compareEnhancements 0 2"
bind F11 "toggle r_compareEnhancements 0 1"
```

These commands replace any existing bindings for those keys. Alternatively,
`exec rend2-compare.cfg` defines `vstr rend2_compare` and `vstr rend2_split` without
changing key bindings.

## Comparison Scope

The base view removes normal mapping, parallax, material specular light, cube
reflections, screen AO, capsule occlusion, skin diffusion, soft-particle fading,
SMAA, torch lighting, sun-shadow sampling, tone mapping, exposure, bloom, and sun
rays from the main scene. The enhanced view uses your current settings.

This is a comparison within Rend2. The base view uses the loaded assets and
Rend2's underlying lighting pipeline. It is not a pixel-exact reconstruction of
the 2003 renderer. UI and gameplay continue normally. Debug images and auxiliary
portal views retain their own rendering rules.

The split renders the scene twice and retains one colour image. It therefore
costs more than normal rendering. Use mode `0` for performance measurements.
Normal mode does not allocate the comparison image or render a second scene.
The image is allocated on first use and released with the renderer.

## Checks

```sh
python3 scripts/test-renderer-compare.py
python3 scripts/test-renderer-compare.py --msaa 4
python3 scripts/test-renderer-compare.py --model
```

The world test compares each split half with its full-screen reference. It also
checks immediate restoration, unchanged player state, retained custom settings,
save/load, and renderer restart. The world regions had zero comparison error with
MSAA off and at 4x in the software-rendering tests.

The model test includes a humanoid with capsule and skin effects. Its immediate
split and restore comparisons passed. Debug models are recreated after load and
restart, so their old pixels are not used as an exact saved-state reference.
The test checks that the model renders again after each transition.
