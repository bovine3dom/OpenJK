# Additional Raster Features

These features use the stock game assets. Capsule shadows, skin diffusion, and
SMAA start disabled for desktop comparison. Soft particles start enabled.

## Controls

| Setting | Default | Effect |
| --- | ---: | --- |
| `r_capsuleShadows` | `0` | Set to `1` for skeletal capsule ground occlusion. Requires `r_ssao 1` and `r_depthPrepass 1`. |
| `r_capsuleShadowStrength` | `0.25` | Capsule shadow strength, from `0` to `1`. |
| `r_sss` | `0` | Skin diffusion strength, from `0` to `1`. Try `0.5` first. |
| `r_sssRadius` | `0.3` | Diffusion radius in world units, from `0.01` to `2`. |
| `r_sssDebug` | `0` | Set to `1` to show the eligible skin mask. |
| `r_softParticles` | `1` | Enable depth fading for blended sprite particles. Requires `vid_restart` after a change. |
| `r_softParticleDistance` | `8` | Intersection fade distance in world units, from `0` to `64`. Zero disables fading live. |
| `r_smaa` | `0` | Set to `1` for SMAA 1x High. |
| `r_smaaDebug` | `0` | `1` shows edges; `2` shows blend weights. |

Except for `r_softParticles`, these controls change live. Debug controls are not
archived. Other controls are saved in the profile.

## Capsule Ground Shadows

The renderer reads existing humanoid bone positions and constructs ten capsule
segments for the torso, head, arms, and legs. Temporary Ghoul2 bolt references
are released after each query. No physics library or animation controller is
required.

The first implementation approximates soft overhead ambient occlusion on
upward-facing receivers. It uses the nearest eight eligible submitted characters
within 1024 world units. Screen bounds limit pixel work. The effect has a short
vertical range and uses conservative stock-humanoid radii.

Capsule visibility multiplies the filtered world AO image. The existing
`r_ssaoAmbientOnly` and strength controls therefore also affect its application.
Use `r_ssaoAmbientOnly 0` for the broader baked-lighting comparison. First-person
weapon AO remains separate. Compare with `cg_shadows 0` to isolate capsules from
the existing character-shadow modes.

This is a bounded ground-shadow approximation, not a replacement for all direct
light shadows. It does not trace occluders between the capsule and receiver.
Check walls between characters and receivers, unusual poses, and dismemberment.
Non-humanoid rigs and weapon models are excluded. Physical animation remains
deferred in `animation_todo.md`.

## Skin Eligibility Gate and Diffusion

Asset inspection found separate skin materials on the default Twi'lek player:

- `heada_face` and `headb_face` use the `face` texture family.
- Eyes and teeth use the separate `mouth_eyes` material.
- The first torso variants have distinct `_skin` and `_clothes` materials.

The exact initial material list under `models/players/jedi_tf/` is:
`face`, `face_01`, `face_02`, `face_03`, `torso_01_skin`, `torso_02_skin`, and
`torso_03_skin`. Rendered masks confirmed the face and exposed chest, with eye,
mouth-interior, and clothing regions excluded. This passed the requested gate
before broader character support was attempted.

The renderer captures diffuse irradiance and albedo for eligible opaque
surfaces. A manual depth comparison accounts for MSAA sample displacement.
Two separable, depth-aware filters spread red light farther than green or blue.
The composite applies the irradiance change through the original albedo and
keeps the original specular contribution. Painted texture detail is not blurred
directly.

This is screen-space diffusion. It does not estimate transmission through ears
or tissue thickness. Only the listed stock materials qualify. Fogged and blended
skin stages are excluded. Test intersections with foreground transparent effects;
the diffuse correction is composited after the main scene colour pass.
Menu portraits and partial or portal views are not processed.

The implementation uses the two-pass diffusion approach discussed in
[Separable Subsurface Scattering](https://www.iryoku.com/separable-sss/), with a
small fixed RGB kernel and a separate irradiance/albedo capture.

Use `r_sssDebug 1` to check eligibility before judging the appearance. Bright
regions qualify; other regions must be black. Use radius `1` or `2` briefly for
a clear comparison, then return toward `0.3`. Skin diffusion has little effect
under spatially uniform lighting.

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
save/load, and a switch to an ineligible player model. Original assets are not
modified.

Initial 960 x 720 checks passed with MSAA 0 and 4. The SSS comparison used only
the interior of its skin mask, so moving effects outside that mask did not
contaminate the result. Alpha and additive particle tests used static test
materials to avoid animated-texture differences.

The extended 4x MSAA suite passed in `build/smoke/raster-features.si31he4c`.
It confirmed that particle fading stops when the depth prepass is disabled,
that skin masks return after restart and save/load, and that switching to Kyle
leaves no skin mask. The debrief mouse sequence passed with the new features
enabled in `build/smoke/debrief.jhxyc1x5`. The reticle checks passed for both
renderers and both aspect ratios in `build/reticle-tests/reticle.1s5hhhlb`.

In two 8-second P630 runs per setting at 1280 x 720 with MSAA off, the normal
Kril'dor scene measured 42.24 FPS with capsules/SMAA off, 39.13 FPS with capsules,
and 42.14 FPS with SMAA. A separate front-facing player view measured 62.73 FPS
with SSS off and 53.66 FPS at strength `0.5`. These are approximate throughput
measurements, not universal GPU costs or GTX 1080 Ti predictions.

Benchmark results are under `build/benchmark-sp/rdsp-rend2.togyz76b`, `.3s2zgri8`,
`.ikkmq28v`, `.xsrnn4fg`, and `.evb1_wra`. The benchmark now accepts `--viewpos`
and `--noclip` for controlled views. See `human_todo.md` for desktop checks.
