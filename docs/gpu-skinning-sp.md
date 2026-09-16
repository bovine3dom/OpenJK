# SP GPU Skinning

## Controls

`r_g2GpuSkinning` defaults to `1` in Rend2. Supported Ghoul2 surfaces use static
mesh buffers and vertex-shader skinning. Set it to `0` to use the CPU path.
Changes are live. Mesh buffers are created on first use.

Animation, bone evaluation, IK, ragdolls, attachments, and collision remain in
native SP Ghoul2. The renderer uploads evaluated bone palettes. It preserves the
original floating-point weights instead of reducing them to byte weights.

The GPU path calculates MikkTSpace tangents for the original mesh, then transforms
them with the bones. The CPU path calculates tangents on the deformed mesh.
Shading can therefore differ around bending joints. The reference path remains
available for comparison. Sample counts, texture quality, AO, and shadow settings
are unchanged.

## Fallbacks and Resources

CPU fallbacks handle gore and its marked base surface, stencil-shadow batches,
shader deformations, distortion, disintegration, blended surfaces, vertex-lit
materials, external GLSL, oversized bone palettes, and projected-shadow mode 4.
Ordinary colour, depth, fog, weapon-AO, and skin-diffusion passes can use GPU
skinning.

Static mesh buffers have a 64 MiB budget. Bone palettes use a separate 4 MiB
buffer per renderer frame slot. The existing frame fences control slot reuse.
Palette storage does not consume the scene and camera uniform allocator. A full
budget selects the CPU path. Buffers are released during renderer shutdown,
including soft map resets.

`r_g2GpuValidate 1` enables development readback. A transform-feedback shader
deforms the actual uploaded vertices, weights, and palette. The test compares
those positions with CPU skinning. It checks finite results and a small numerical
tolerance. Readback is slow and is disabled by default. `r_speeds 100` reports GPU
surface, vertex, fallback, and mesh-buffer counts; the benchmark records them in
`ghoul2_gpu`.

## Validation

```sh
python3 scripts/test-gpu-skinning.py
python3 scripts/test-gpu-skinning.py --msaa 4 --shadows 2
python3 scripts/test-gpu-skinning.py --software
python3 scripts/test-rend2-sp.py --gpu-skinning
python3 scripts/test-rend2-sp.py --gpu-skinning --shadows 2 --buffer-storage
python3 scripts/test-ssao-weapons.py --hardware --method 1 --half-res 1 --width 1280 --height 720 --gpu-skinning
python3 scripts/test-raster-features.py --gpu-skinning --msaa 4
```

Checks passed on the Intel P630 and LLVMpipe. Maximum GPU/CPU position differences
were about 0.00001 game units in these tests. The character image comparison
passed with animation round-trip controls. Tests cover static and animated poses,
saber swings, gun firing and switching, weapon AO, skin profiles, detached parts,
the live enhancement split, save/load, map changes, and renderer restart.

The gore probe creates a real blaster projectile through a test-only command.
It requires damage to the intended NPC and a recorded gore fallback. It does not
assign a gore flag directly. Stencil-shadow tests also require CPU fallback
evidence. Hoth map startup passed with GPU readback enabled.

These checks do not replace desktop campaign play, unusual custom-model tests,
or NVIDIA measurements. See `benchmark-sp.md` for the measured performance.
