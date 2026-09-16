# SP Sky and Fog

## Seamless Sky Sampling

`r_seamlessSky 1` enables cross-face cubemap sampling. Set it to `0` for the
original six-image path. The switch is live. The base side of enhancement
comparison uses the original path.

The loader converts the stock face orientation to OpenGL cube orientation.
It preserves the loaded image color space and HDR values. It builds mipmaps
after all six faces are present. Hardware filtering samples adjacent faces.
Source image discontinuities can still be visible.

Incomplete skies, non-square faces, mixed formats, and faces larger than 1024
pixels use the original path. Each extra cube uses at most 64 MiB, including
mipmaps. Cubes share the renderer image cache and normal shutdown cleanup.
The original faces remain available for live comparison. Conversion reads
textures once during loading; there is no per-frame readback.

## Verification

Run `python3 scripts/test-sky-fog.py`. This headless EGL test checks six view
directions on `t2_wedge`, live fallback, and renderer restart. It rejects shader
and OpenGL errors. Use `--msaa 4` to check the MSAA path.

The first P630 run passed. Mean absolute differences between stock and cube
views were 0.026 to 0.054 on a 0–255 scale. The restart difference was 0.045.
These figures check orientation and color preservation. They are not a measure
of source art quality. Both renderer smoke tests also passed.

See [the research notes](sky-fog-investigation.md) for sources and stage order.
