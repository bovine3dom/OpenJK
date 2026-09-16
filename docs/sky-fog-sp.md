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

## Map-Controlled Haze

`r_mapHaze 1` enables a map profile. Set it to `0` for a live comparison.
Maps without a profile keep their original fog. The first profile is
`maps/t2_wedge.haze`. It limits haze to the cloud layer below the platforms.

A profile contains 14 finite numbers in this order:

1. RGB color and maximum opacity (four values).
2. Density, reference height, height falloff, and start distance (four values).
3. Minimum XYZ and maximum XYZ bounds (six values).

Use world units for distances. Density is inverse world distance. Height falloff
is inverse world height. Color values must be 0–1. Maximum opacity must be 0–0.5.
Density must be 0–0.001. Falloff must be 0–0.01. Start distance must be 0–8192.
Bounds must be ordered and within ±65536. Invalid profiles disable the effect.
Comments use `//`. Reload the map after a profile edit.

The shader clips the view ray to the box and integrates exponential height
density. The view distance is capped at 8192 units. Opaque, alpha-tested, and
standard alpha-blended single-pass materials use their own surface position.
Additive materials receive attenuation without added fog color. Existing fogged
surfaces and custom multipass materials retain their original rendering.
Weapon depth-hack views, shadow passes, UI, and the skin-diffusion fill are excluded.

Sky cubemaps use the main camera origin, including when drawn through a sky
portal. Portal scenery keeps its original rendering. The base enhancement
comparison disables haze. There is no temporal history or full-screen depth
approximation. Author bounds around outdoor regions; this box is not an indoor
visibility detector. Do not put enclosed rooms inside a haze box.

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

The haze stage passed both renderer smoke tests and the EGL fixture. In the
downward view, mean image change was 0.639. Restart and disable errors were
0.007 and 0.031. The MSAA 4 capture also showed the bounded effect. A short P630
sky-heavy benchmark measured 71.65 FPS with haze off and 68.03 FPS with haze on,
using the same sample lengths as the replacement test.

See [the research notes](sky-fog-investigation.md) for sources and stage order.
