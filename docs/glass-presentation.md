# Window Glass Presentation

## Decision

Thin-window rendering now has automatic local reflection probes in SP. It uses
clear transmission and angle-dependent reflections. Authored surface overlays
remain in use. Breakable-glass cues and gameplay are unchanged. Refraction is off.

## Implemented Controls

| Control | Default | Effect |
| --- | ---: | --- |
| `r_glass` | `1` | Enable thin-window rendering. Zero restores the original optical stages. |
| `r_glassReflection` | `1` | Scale reflection and its transmission loss. Range: zero to one. |
| `r_glassRoughness` | `0.12` | Soften reflections. Range: zero to one. The transmitted image stays sharp. |
| `r_glassProbes` | `1` in SP | Generate local window probes at map load. Zero uses the previous reflection source selection. |
| `r_glassProbeBudget` | `48` | Limit automatic probes per map. Range: 1 to 63. Authored probes also consume sort-key slots. |
| `r_glassExposure` | `2` | Adjust probe reflection brightness in stops. Range: -2 to 4. Two stops gives four times the captured light. |
| `r_glassDebug` | `0` | 1: reflection only. 2: green for a probe, red for fallback. 3: background attenuation without reflected light. |

`r_glassProbes` and `r_glassProbeBudget` require `vid_restart`. The other controls
change live. The base side of `r_compareEnhancements` uses the original stages.
The exposure boost is an art setting for reflection visibility. It does not
increase background attenuation. Debug modes require `r_glass 1`.

The shader calculates Fresnel per fragment and uses premultiplied reflection.
Framebuffer blending supplies transmission once. A matching legacy attenuation
stage is skipped. Security patterns and frosted overlays keep their own stages.
The original culling rules remain in use. Plain two-stage windows need one optical
draw instead of two. Probe generation adds map-load work, not a per-frame scene copy.

Automatic window probes work with `r_cubeMapping 0`, the default. Other materials
do not use these probes. Authored cubemaps can coexist with them when cubemapping
is enabled. Without a valid assignment, the window uses the old reflection image
at half brightness, weighted by Fresnel. Red and green reflection materials retain
the texture fallback to preserve their authored colour.

### Automatic Placement

1. Find the supported BSP panes, inline brush models, and map-declared MD3 windows.
   Apply entity origins, angles, and model scales. Curved MD3 windows use an average
   orientation; nonplanar BSP surfaces retain the fallback.
2. Find capture positions on each rendered side. Check small collision bounds,
   the visibility cluster, and a trace from the pane. Reject invalid positions.
3. Share a probe only within 512 units, with at most 96 units of vertical separation.
   Both positions must be on the correct side of both panes and have
   an unobstructed opaque-geometry trace. Select the nearest valid shared probe.
4. Capture the world and map-declared static MD3 props. Exclude windows, the player,
   actors, moving brush models, and transient effects. Filter the capture for roughness.
5. Assign probes to pane sides. Six directional traces supply approximate bounds
   for reflection parallax. Reject a saved assignment if its model moves or rotates.

Large surfaces receive priority when the budget is limited. The sort key has 63
probe slots, including authored probes. Each automatic HDR probe uses approximately
1 MiB for six 128-by-128 faces and mipmaps. Capture uses shared 256-by-256 buffers.
Automatic image slots are reused across direct world reloads.

Generation is synchronous during loading. There is no disk cache or background
scheduler yet. The log reports generated probes, assigned sides, shared probes,
invalid positions, and budget fallbacks. Debug mode 2 identifies fallback windows.

### Coverage

The local JA and JO shader definitions and BSP shader lists were inspected.
The converted families include:

- Clear and security glass under `textures/common/` and `textures/tests/`.
- The reflection layer of `textures/common/frosted_glass`.
- `textures/kejim/glass`, `textures/factory/env_glass`, and
  `textures/rbettenbergtest/rbettenbergtest_glass1`.
- `models/map_objects/factory/glass` and `glass_b`, including animated model draws.

Conversion also requires a supported transparent environment stage. JO's missing
Glass flags do not prevent conversion. Opaque windows, crystals, portals, glass
effects, and plain alpha-only materials retain their authored rendering. This
preserves windows that intentionally block the view and materials without a fake
reflection layer. Map-specific overrides are not required for the converted families.

### Validation

```sh
bash scripts/build-sp.sh
python3 scripts/test-glass-sp.py --package build/ready --msaa
python3 scripts/test-glass-sp.py --package build/ready --models
python3 scripts/test-glass-sp.py --package build/ready --campaign ja --mode combined
python3 scripts/test-glass-sp.py --package build/ready --campaign ja --probe-budget 1 --expect-fallback
python3 scripts/test-glass-sp.py --package build/ready --campaign ja --mode fallback
python3 scripts/test-rend2-sp.py --package build/ready --shadows 3 --buffer-storage
```

The capture checks passed on headless Intel P630 hardware at 960 by 720:

- Front and oblique tower views in `t1_sour` and `kejim_post`, plus an exterior
  view of the `t1_sour` tower.
- Security-pattern windows in both maps, including JO brush-model glass.
- Rotated and scaled MD3 windows in `vjun2`.
- Original, thin, zero-reflection, restored, reflection-only, roughness, assignment,
  and attenuation-only captures from each camera.
- Automatic probes with 4x MSAA, coexistence with an authored probe, a one-probe
  budget, and the disabled automatic-probe path.
- Renderer restart, save/load, and map transition with projected shadows and
  persistent buffers. The package's vanilla and Rend2 smoke checks also passed.
- The shared MP Rend2 target compiled successfully. MP runtime was not tested.

| Map | Generated probes | Assigned sides | Invalid positions | Budget fallbacks |
| --- | ---: | ---: | ---: | ---: |
| `t1_sour` | 16 | 24 | 0 | 0 |
| `kejim_post` | 48 | 151 | 13 | 4 |
| `vjun2` | 18 | 37 | 7 | 0 |

Results are in `build/smoke/glass.lrfrz9ac` (JA/JO), `glass.iqh5q8s4` (models),
`glass.q4gqp_8d` (authored coexistence), `glass.e7g7n1rz` (budget),
`glass.y9qey191` (disabled probes), and `build/smoke/rend2.tkm0o1xx` (lifecycle).
Logged capture-phase times were about 0.6 seconds for each tower map on P630.
Model loading and placement are separate. These are not GPU frame-time benchmarks.

The earlier fallback-only test could pass for almost invisible glass. The new
check isolates assigned window pixels. It requires reflected image detail, a
roughness response in both diagnostic and normal rendering, and added reflected
light relative to attenuation alone. Live restoration must remain within 0.2 mean
RGB levels. Reflection-only captures show local ceilings, lights, and wall panels.
Full campaign visual review and player approval of the default strength remain open.

## Research

- [Epic: Fresnel in materials](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-fresnel-in-your-unreal-engine-materials)
  shows glass that is clear when viewed straight on and more reflective from the
  side. Fresnel controls reflection strength, not the colour of reflected light.
- [Filament: physically based rendering](https://google.github.io/filament/main/filament.html)
  explains energy conservation, Fresnel, reflection probes, and roughness.
  Its material table gives glass a normal-incidence reflectance of about 4–5%.
  This is an interface value, not the opacity of a complete window.
- [Epic: improved shading models](https://www.unrealengine.com/en-US/tech-blog/improved-shading-models-in-unreal-engine-4-25-and-beyond)
  describes thin translucent materials that combine specular highlights with
  background tint. The page returned HTTP 403. A Kagi-generated summary supplied
  this finding; the original page text was not directly inspected.
- [Epic: refraction](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-refraction-in-unreal-engine)
  gives glass an index of refraction of 1.52. It also describes depth bias to
  reduce foreground objects appearing in a distorted background. Its curved
  object examples are not a suitable visual target for thin, flat windows.

These sources support separate transmission, reflection, and surface detail.
The visibility cues proposed below are art choices for OpenJK, not a documented
recipe from a particular shipped game.

A flat, parallel-sided pane bends light at both interfaces. The outgoing ray is
parallel to the incoming ray, with a thickness-dependent offset. Strong lens-like
distortion is therefore a poor default for thin windows. Visible colour separation
is also unnecessary for this target.

## Why the Existing Glass Can Look White

### Stock assets

The local JA archives contain 23 shader definitions with `q3map_material Glass`.
Most window definitions are in `assets1.pk3`, under `shaders/common.shader`.

| Material | Relevant stages |
| --- | --- |
| `textures/common/env_glass` and `env_glass_breakable` | Add `etest4` with `GL_ONE GL_ONE` and `tcGen environment`. |
| `textures/common/glass` and `glass_no_tess` | Attenuate the background with `glass2`, then add an environment-mapped `glass2`. |
| Security variants | Often add `etest4`, then alpha-blend a security pattern. |
| Frosted, opaque, crystal, and effect variants | Use different stages despite sharing the Glass material flag. |

`tcGen environment` supplies reflection-like texture coordinates. It does not
capture the room. An additive stage gives `output = background + texture`.
A bright texture raises dark values and can clip highlights. This supports the
reported fake-reflection explanation. It does not prove which stage dominates
the user's specific scene; that requires a fixed-camera comparison.

`qer_trans` is an editor setting, not a runtime transparency control.

### The `glass_test` experiment

Commit `6c2ee035` has additional problems:

1. `ShaderUsesRefraction()` classifies every Glass material as refractive. This
   includes opaque glass, crystal, security layers, and `gfx/misc/test_crackle`.
2. `RB_IterateStagesGeneric()` selects refraction for each stage but retains the
   stage blend state. The refraction path samples the captured screen instead of
   the stage texture. An additive stage can thus add the background to itself.
   A multiplicative stage can also apply the wrong operation to that background.
3. The new Fresnel expression mixes the sampled background toward `var_Color.rgb`
   and increases alpha. For a white stage colour, this adds a white veil. It does
   not sample reflected room lighting.
4. The view direction uses clip-space X/Y without division by W. Normalization
   does not correct that projection error. Fresnel is calculated per vertex,
   which can also make large panes depend on their triangulation.
5. The pass retains separate RGB refraction offsets. It declares a depth sampler
   but does not use it to reject foreground samples.

Setting `r_glassRefraction 0` does not repair the blend operations or remove the
white Fresnel contribution.

Fresnel is not a pane-border mask. On a flat pane, it changes with viewing angle;
it does not identify the rectangular frame or the edges of individual triangles.

## Material Model

The optical stage uses an approximate two-interface thin sheet. Texture masks,
entity fades, and `r_glassReflection` supply additional weights:

```text
F = 0.04 + 0.96 * (1 - abs(dot(N, V)))^5
sheetF = 2 * F / (1 + F)
output = background * (1 - sheetF) + reflection * sheetF * 2^exposure
```

Legacy constant reflection alpha is replaced by Fresnel, rather than multiplied
into it again. This prevents a 5% model reflection from becoming almost invisible.
The exposure adjustment deliberately exceeds the physical response. The blend state is
`ONE, ONE_MINUS_SRC_ALPHA`. The shader premultiplies after fog and haze, and the
normal scene pipeline applies exposure and tone mapping. It does not light the
glass as an opaque diffuse surface or generate normals from reflection art.

Classification is in `codemp/rd-rend2/tr_shader.cpp`. Draw setup is in
`tr_shade.cpp`. `tr_glsl.cpp` builds dedicated glass variants from
`glsl/generic.glsl`, which reuses the existing vertex, fog, and animation paths.

## Further Work

- Review grazing views, movement, overlapping panes, sabers, and effects during
  campaign play. Measure GPU time and probe load cost before adding more passes.
- Add a generated-probe cache and a capture scheduler. Improve coverage for invalid
  positions and budget fallbacks. Runtime-spawned windows and moved models need
  a later placement/update path. Static captures do not follow changes in the room.
- Improve parallax bounds for irregular rooms and curved windows. A six-direction
  box is an approximation, not a reconstruction of the room.
- Keep breakable-glass cues deferred. If resumed, use authored detail masks or
  explicit pane coordinates. Tiled UVs and triangle edges cannot supply reliable
  frame borders. Use gameplay state: `func_glass` can be invincible or script-only.
- Add refraction only after visual approval of the undistorted version. Use a
  small thickness-based offset with one sample position for all colour channels.
  Reject foreground and off-screen samples. A single screen capture cannot
  correctly represent every overlapping transparent layer.
