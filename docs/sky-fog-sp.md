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

## Selected High-Resolution Skies

`r_highResSkies 1` selects 2048-pixel reconstructions of the Krildor (`wedge`)
and Yavin skies. It requires `r_seamlessSky 1`. Set it to `0` for native assets.
Missing replacements use the native sky. The base comparison uses native assets.

The build script generates `OpenJK/sky-hd.pk3` from the local game archives.
`scripts/build-sky-assets.py` adds borders sampled from adjacent faces before
Lanczos reconstruction. It preserves the source layout and colors. It does not
recover missing detail or add new painted features. This is a source-derived
reconstruction, not new high-resolution artwork. Original archives stay intact.
The generated package records input hashes in `sky-assets.json`.

Each selected sky adds a 128 MiB RGBA8 cube, including mipmaps. Source pixels
are released after upload. There is no duplicate high-resolution 2D texture set.
The live toggle changes sampling, not allocation. A future asset budget can make
allocation optional on memory-limited systems.

## Verification

Run `python3 scripts/test-sky-fog.py`. This headless EGL test checks six view
directions on `t2_wedge`, live fallback, and renderer restart. It rejects shader
and OpenGL errors. Use `--msaa 4` to check the MSAA path.

The first P630 run passed. Mean absolute differences between stock and cube
views were 0.026 to 0.054 on a 0–255 scale. The restart difference was 0.045.
These figures check orientation and color preservation. They are not a measure
of source art quality. Both renderer smoke tests also passed.

The replacement stage passed the same checks. Its downward-view difference
from the native cube was 0.056. A short P630 sky-heavy benchmark measured 70.40
FPS with native assets and 69.37 FPS with replacements. Each result used two
five-second samples after a three-second warmup. Shared server load can affect
these results. This is a cost check, not evidence of new source detail.

See [the research notes](sky-fog-investigation.md) for sources and stage order.
