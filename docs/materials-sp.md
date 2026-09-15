# Material Calibration

Rend2 can read authored normal maps or generate normals from diffuse textures.
Generated normals infer height from image brightness. Painted details and baked
shadows can therefore become false surface relief.

## Controls

These settings are archived. The first six change live without texture reloads.

| Control | Default | Range | Effect |
| --- | ---: | --- | --- |
| `r_normalStrength` | `1` | `0` to `4` | Multiply normal-map XY strength for all lit materials. Zero gives the geometric normal. |
| `r_generatedNormalStrength` | `0.25` | `0` to `4` | Additional multiplier for generated normals only. Authored maps do not use this multiplier. |
| `r_parallaxScale` | `0.5` | `0` to `4` | Multiply material height displacement. Requires a height-mapped material and `r_parallaxMapping 1`. |
| `r_specularStrength` | `1` | `0` to `4` | Multiply specular lighting, including dynamic lights and environment reflections. Zero removes this contribution. |
| `r_roughnessScale` | `1` | `0.05` to `4` | Multiply material roughness. Higher values broaden highlights. |
| `r_roughnessFloor` | `0` | `0` to `1` | Set a minimum roughness after scaling. This can limit sharp highlights in texture packs. |
| `r_generatedNormalBrighten` | `0` | `0` to `1` | Diffuse brightening for generated normals. Zero keeps the source colour. One restores the old compensation. Requires `vid_restart`. |
| `r_normalMapCache` | `1` | `0` or `1` | Read and write the local generated-normal cache during texture loading. |

Normal strength multiplies the material's existing scale. For a generated map,
the final multiplier is `r_normalStrength * r_generatedNormalStrength`.
The new generated-map default is one quarter of the old strength. Diffuse
brightening is now disabled. Parallax height uses half its previous scale.
Specular and roughness controls start at neutral values.

`r_genNormalMaps` defaults to `1` and generates missing normal maps. Existing
profiles retain saved values. Set it to `1`, then use `vid_restart`, to enable
generation in an existing profile. Authored `_n` maps take priority.
`r_normalMapping`, `r_specularMapping`, and `r_parallaxMapping` select shader
features and still require a restart. A live strength of zero is useful for
comparison but does not unload textures or remove all shader work.

## Calibration Procedure

Start with a fixed camera and these settings:

```text
r_normalStrength 1
r_generatedNormalStrength 0.25
r_parallaxScale 0.5
r_specularStrength 1
r_roughnessScale 1
r_roughnessFloor 0
```

1. Compare `r_normalStrength 0` with `1` to isolate normal-map lighting.
2. Adjust `r_generatedNormalStrength` for stock textures. Try `0.1`, `0.25`,
   and `0.5`. Adjust `r_normalStrength` when authored normal maps also need a change.
3. Compare `r_specularStrength 0` with `1` to identify specular highlights.
4. Increase roughness to broaden highlights. Use the floor to limit only the
   smoothest materials. The final roughness is clamped between `0.01` and `1`.
5. Adjust parallax separately on materials that contain height data.

Keep `r_generatedNormalBrighten 0` while making these comparisons. The old
generator changed diffuse colour as well as normals, which made a normal-strength
comparison harder to interpret. Existing profiles keep any values they already
contain. Use the commands above to apply the starting settings explicitly.

The user approved `r_normalStrength 1` and `r_generatedNormalStrength 0.25` on
stock assets. Roughness, specular, and parallax changes had no obvious visual
effect in that test. Desktop loading still felt slow despite the normal cache.
Further calibration and loading-time investigation remain open.

## Generated-Normal Cache

The cache is local to the game profile:

```text
OpenJK/cache/rd2n1/
```

It stores raw generated normal pixels before upload and mip generation. Keys
include the decoded diffuse pixel content, dimensions, edge-clamping mode, and
algorithm version. Each entry has a size/header check and a payload checksum.
An invalid or incomplete entry is regenerated. Changes to strength, roughness,
parallax scale, or diffuse-brightening amount do not invalidate the normal data.
An authored normal map is checked before the cache.

Entries larger than 64 MiB are not cached. The cache does not contain driver
texture objects. Image decoding, mip generation, compression, and GPU upload
still occur. It is not a replacement for the shader cache.

Set `r_normalMapCache 0` before a texture reload to bypass cache reads and writes.
This does not remove existing entries. The `cache/rd2n1` directory can be deleted
while the game is stopped. There is no automatic disk-size limit or eviction.
Do not include the cache in published packages; its pixels derive from local
game assets or texture packs.

## Validation

```sh
python3 scripts/test-materials-sp.py --package build/ready
```

The test uses headless hardware EGL and isolated profile data. It checks live
normal-strength changes, equivalent combined scales, round-trip restoration,
specular strength, roughness, cold generation, warm reuse, and corrupt-cache
recovery. It compares fixed wall/floor pixels across uncached and cached runs.
The stock-map fixture does not validate every authored height-map material.

In `build/smoke/materials.3wppdhcb`, the warm run reused 562 maps and generated
none. The cold run generated 536 maps and reused 26 duplicate-content entries.
Normal generation took about 5.8 seconds in that cold run and zero in the warm
run. Total test-process time was about 27.5 seconds cold and 21.2 seconds warm.
Process time includes the capture fixture and is not a pure loading-time metric.
Cached, uncached, and repaired-cache captures passed the same image comparisons.
