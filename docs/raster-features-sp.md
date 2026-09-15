# Additional Raster Features

These features use the stock game assets. GTAO, capsule shadows with wall
occlusion, SMAA, skin diffusion, and soft particles start enabled in Rend2.

## Controls

| Setting | Default | Effect |
| --- | ---: | --- |
| `r_capsuleShadows` | `1` | Skeletal capsule occlusion. Requires `r_ssao 1` and `r_depthPrepass 1`. |
| `r_capsuleShadowStrength` | `0.25` | Capsule shadow strength, from `0` to `1`. |
| `r_capsuleShadowSoftness` | `1` | Penumbra multiplier, from `0` to `2`. Try `0.25` for sharper shadows. |
| `r_capsuleShadowRadius` | `1` | Capsule thickness multiplier, from `0.25` to `2`. |
| `r_capsuleShadowRange` | `64` | Maximum occlusion distance in world units, from `8` to `128`. |
| `r_capsuleShadowWalls` | `1` | Surface-normal proximity occlusion, including walls. Set to `0` for ground receivers only. |
| `r_capsuleShadowDebug` | `0` | Set to `1` to report model, root surface, and capsule count for one frame. |
| `r_sss` | `1` | Skin diffusion strength, from `0` to `4`. Values above `1` exaggerate the correction for testing. |
| `r_sssRadius` | `0.5` | Diffusion radius in world units, from `0` to `8`. Zero disables diffusion. |
| `r_sssDebug` | `0` | `1` mask, `2` raw irradiance, `3` filtered irradiance, `4` difference, `5` split scene. |
| `r_sssDebugGain` | `16` | Difference-view gain, from `1` to `128`. Does not change scene lighting. |
| `r_softParticles` | `1` | Enable depth fading for blended sprite particles. Requires `vid_restart` after a change. |
| `r_softParticleDistance` | `8` | Intersection fade distance in world units, from `0` to `64`. Zero disables fading live. |
| `r_smaa` | `1` | SMAA 1x High. Set to `0` to disable it. |
| `r_smaaDebug` | `0` | `1` shows edges; `2` shows blend weights. |

Except for `r_softParticles`, these controls change live. Debug controls are not
archived. Other controls are saved in the profile.
Existing profiles keep their saved values when defaults change.

## Graphics Default Audit

New profiles select Rend2 when the build includes it. GTAO uses medium quality,
half-resolution calculation, denoising, and separate weapon AO. Normal and
specular mapping are enabled. Generated normals use strength `0.25`, and their
local cache is enabled. HDR rendering, tone mapping, and automatic exposure
retain their enabled defaults.

MSAA (`r_ext_multisample 0`) and per-sample shading (`r_sampleShading 0`) remain
off by request. SMAA supplies the default anti-aliasing pass.

Other off settings have separate purposes:

- `r_generatedNormalBrighten 0`: the old diffuse-colour compensation was
  deliberately disabled during material calibration.
- `r_parallaxMapping 0` and `r_cubeMapping 0`: inherited optional material
  features. They were not approved as stock-asset defaults. The parallax,
  specular, and roughness calibration did not show a clear desktop benefit.
- `r_dynamicGlow 0`, `r_drawSunRays 0`, and `r_flares 0`: inherited optional
  effects, not the new approved raster passes.
- `r_arb_buffer_storage 0`: an optional buffer-upload path, not a visual effect.
- Debug views and forced lighting remain off. Legacy `cg_shadows` remains `1`.

Vulkan, volumetric fog, new light shafts, and indirect-lighting work are deferred.
Ray tracing is outside the project scope. A persistent driver shader-binary
cache is not implemented; linked programs are reused across soft map resets.

To apply the approved settings to an existing JA or JO profile, enter:

```text
exec rend2-defaults.cfg
```

This packaged preset also disables MSAA and per-sample shading, clears the
graphics debug views, and restarts the renderer. Apply it separately in each
campaign profile. Other settings retain their current values.

## Capsule Contact Shadows

The renderer reads existing humanoid bone positions and constructs up to twelve
capsules for the torso, head, arms, legs, and hands. Temporary Ghoul2 bolt references
are released after each query. No physics library or animation controller is
required.

The effect approximates local ambient occlusion. It uses the nearest eight eligible submitted characters
within 1024 world units. Screen bounds limit pixel work. The effect has a short
range and uses stock-humanoid radii.

Softness `1` preserves the original penumbra. Lower values sharpen its edge;
radius changes the geometric thickness. Range controls the distance falloff.
The wall mode projects occlusion along the receiving surface normal.
It can show contact occlusion on walls and ceilings. This is a proximity
approximation, not a shadow from a particular lamp. Check wall leakage and cost.

Capsule selection follows the active Ghoul2 surface tree, including the selected
root and hidden descendants. A detached hand gets one small capsule. A detached
arm gets its arm and hand capsules. A detached head gets one sphere. The remaining
body no longer casts capsules for removed parts. Cut caps do not add body segments.
Screen bounds use only the selected capsule endpoints.

For a model test, use `testG2Model models/players/kyle/model.glm`, then
`testsurface r_hand root`. Use `r_capsuleShadowDebug 1` to check the count.
`testsurface hips root` selects the body tree; `testsurface r_arm 256` hides the
arm and its descendants. These commands affect the test model.

Capsule visibility multiplies the filtered world AO image. The existing
`r_ssaoAmbientOnly` and strength controls therefore also affect its application.
Use `r_ssaoAmbientOnly 0` for the broader baked-lighting comparison. First-person
weapon AO remains separate. Compare with `cg_shadows 0` to isolate capsules from
the existing character-shadow modes.

This is a bounded contact-shadow approximation, not a replacement for all direct
light shadows. It does not trace occluders between the capsule and receiver.
Check walls between characters and receivers, unusual poses, and dismemberment.
Non-humanoid rigs and weapon models are excluded. Physical animation remains
deferred in `animation_todo.md`.

## Humanoid Skin Eligibility and Diffusion

Stock skin profiles now cover the selectable player species and campaign
humanoids with exposed skin. These include Kyle, Luke, Jan, Rosh, Alora, Tavion,
Jedi, officers, civilians, mercenaries, and humanoid aliens. Characters fully
covered by armour, clothing, or fur do not receive a blanket skin effect.

`codemp/rd-rend2/tr_skin_profiles.h` contains the material names and UV bounds.
Profiles are assigned when shaders load. Separate mouth, eye, tooth, hair,
armour, and cut-cap surfaces are excluded. UV bounds restrict mixed textures,
such as a cultist face with a mask, to selected skin regions. They are
conservative regions, not precise painted tissue masks. Some skin near clothing
or atlas boundaries can remain outside the effect. Stock textures are unchanged.

Named skin surfaces on custom Jedi outfits and Alora's second outfit also
qualify. This permits skin and clothing surfaces to share one texture without
applying diffusion to both surfaces. The original Twi'lek profile remains
available, including the head, lekku, arms, hands, and exposed torso.

The renderer captures diffuse irradiance and albedo for eligible opaque
surfaces. A manual depth comparison accounts for MSAA sample displacement.
Two separable, depth-aware filters spread red light farther than green or blue.
The composite applies the irradiance change through the original albedo and
keeps the original specular contribution. Painted texture detail is not blurred
directly.

This is screen-space diffusion. It does not estimate transmission through ears
or tissue thickness. Only eligible stock skin regions qualify. Fogged and blended
skin stages are excluded. Test intersections with foreground transparent effects;
the diffuse correction is composited after the main scene colour pass.
Menu portraits and partial or portal views are not processed.

The implementation uses the two-pass diffusion approach discussed in
[Separable Subsurface Scattering](https://www.iryoku.com/separable-sss/), with a
small fixed RGB kernel and a separate irradiance/albedo capture.

Use `r_sssDebug 1` to check eligibility before judging the appearance. Bright
regions qualify; other regions must be black. The raw and filtered irradiance
views use `RGB / (1 + RGB)` for display. The difference view shows the absolute
applied HDR correction multiplied by `r_sssDebugGain`. The split view shows the
original scene on the left and scattering on the right, in the same frame.
Diagnostic masks and difference values bypass tone mapping and scene overlays.
The split view retains normal tone mapping.

For a deliberately exaggerated check:

```text
r_ssaoDebug 0
r_sss 4
r_sssRadius 4
r_sssDebug 4
r_sssDebugGain 32
```

Use debug `5` for the split comparison, or `0` for the complete scene. Then
return to the approved defaults: strength `1` and radius `0.5`.
High test values can produce strong colour shifts. Larger radii use 17 taps per
filter pass instead of 9. Skin diffusion has little effect under uniform
lighting, and painted texture wrinkles remain because albedo is preserved.

## Soft Particles

The depth prepass supplies an independent depth copy. Eligible non-depth-writing
sprite and oriented-quad stages fade as they approach that surface. Standard
alpha blending fades alpha. Additive blending also fades RGB. The glow output
uses the faded result.

UI, viewmodels, no-depth sprites, alpha-tested stages, refractive stages, and
depth-writing stages are excluded. The effect is disabled when current-view
depth is unavailable. It does not reuse depth from an earlier view or frame.
Distance is measured along the camera depth axis.

The developer command `testparticle <shader> [radius] [offset]` places a static
sprite near the first solid surface ahead of the camera. `testparticle` with no
arguments clears it. This is a renderer test tool and requires `developer 1`.

## SMAA and Build Dependency

SMAA runs after scene tone mapping, glow, and refraction, and before UI drawing.
It uses the upstream luma-edge, blend-weight, and neighbourhood passes with the
High spatial preset. It has no motion-vector or frame-history dependency. It
can be combined with ordinary MSAA; sample shading is not required.

A pass-definition lifetime bug previously caused all three programs to perform
edge detection, producing red/green edges over the scene. Shader compilation
now copies pass definitions before header generation. Missing SMAA pass defines
produce a compile error. The regression also checks that final SMAA colours stay
within the nearby input colour range. Use `r_smaaDebug 0` for the normal result.

SMAA source and lookup tables are pinned to
`iryoku/smaa` commit `71c806a838bdd7d517df19192a20f0c61b3ca29d` through CMake
FetchContent. The archive has a SHA-256 check. Shader code and lookup data are
embedded in the renderer. `licenses/SMAA.txt` is installed with the package.
For an offline build, set `FETCHCONTENT_SOURCE_DIR_SMAA` to a matching checkout.
No new runtime library or replacement game texture pack is required.

SMAA and SSS targets are allocated on first use and retained until renderer
shutdown. First enablement can allocate GPU memory. SSS uses five full-size
RGBA16F targets; SMAA uses three full-size RGBA8 targets and small lookup textures.

## Tests and Measurements

```sh
python3 scripts/test-raster-features.py --package build/ready
python3 scripts/test-raster-features.py --package build/ready --msaa 4
python3 scripts/test-debrief-sp.py --package build/ready --renderer rdsp-rend2 --msaa 4 --raster
python3 scripts/test-rmlui-reticle.py build/ready --modern --raster --hardware
```

Feature tests use headless hardware rendering, a fixed campaign scene, and
profile-local white-particle test shaders. They compare effects and restoration,
check SMAA edge/weight output, validate the skin mask, and exercise restart,
save/load, humanoid skin profiles, and a switch to an armoured player model.
They also check capsule counts for a whole model, detached parts, and a body
with a removed arm. Original assets are not
modified.

Initial 960 x 720 checks passed with MSAA 0 and 4. The SSS comparison used only
the interior of its skin mask, so moving effects outside that mask did not
contaminate the result. Alpha and additive particle tests used static test
materials to avoid animated-texture differences.

Expanded humanoid and detached-part checks passed with MSAA off in
`build/smoke/raster-features._8mhuosx` and at 4x in
`build/smoke/raster-features.rox88363`. Kyle now has a positive skin test.
The stormtrooper negative test has no eligible skin pixels.

In two 8-second P630 runs per setting at 1280 x 720 with MSAA off, the normal
Kril'dor scene measured 42.24 FPS with capsules/SMAA off and 39.13 FPS with
capsules. The original SMAA timing is invalid because of the pass-selection bug.
A separate front-facing player view measured 62.73 FPS
with SSS off and 53.66 FPS at strength `0.5`. These are approximate throughput
measurements, not universal GPU costs or GTX 1080 Ti predictions.

Benchmark results are under `build/benchmark-sp/rdsp-rend2.togyz76b`, `.3s2zgri8`,
`.ikkmq28v`, `.xsrnn4fg`, and `.evb1_wra`. The benchmark now accepts `--viewpos`
and `--noclip` for controlled views. See `human_todo.md` for desktop checks.
