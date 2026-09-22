# GTAO Bent Normal Roadmap

## Purpose

This document records the current GTAO state and the options for bent normals.
It applies to the JA single-player Rend2 renderer.
It does not apply to the JK2 binary.

## Current State

The renderer has GTAO bent normals for the single-player Rend2 binary.
The `r_gtaoBentNormals` control enables them. The default value is `1`.
The disabled path keeps the scalar R8 GTAO path.

The enabled path stores scalar visibility in R and the encoded view-space bent
normal in GBA. It uses RGBA8 images. The filter, optional upsample pass, world
AO path, and weapon AO path process the packed value. Debug modes 5 and 6 show
the world and weapon bent normals.

The relevant code is in these files:

- `codemp/rd-rend2/glsl/ssao.glsl`
- `codemp/rd-rend2/glsl/depthblur.glsl`
- `codemp/rd-rend2/glsl/lightall.glsl`
- `codemp/rd-rend2/tr_backend.cpp`
- `codemp/rd-rend2/tr_image.cpp`

The lighting shader supports direction-aware specular occlusion, dynamic-model
diffuse environment light, and dynamic-model light-grid visibility. These
features do not replace the material normal. The diffuse and light-grid options
do not change baked world surfaces.

## Bent Normal Definition

A bent normal points toward the average visible part of the hemisphere.
The surface normal describes the material surface.
The bent normal describes indirect visibility.

Do not use the bent normal as the material normal.
Do not use it for the direct-light BRDF.
Use it only for indirect light and directional visibility.

A bent normal cannot find data that is not on the screen.
It will retain the normal limits of a screen-space effect.
Off-screen objects can still cause light leaks.

## Data Output

### Recommended Format

Use `RGBA8` when bent normals are active.
Keep scalar visibility in the red channel.
Store the encoded view-space bent-normal XYZ value in GBA.

This layout keeps the current red-channel AO interface.
It also keeps the current AO debug and fallback paths simple.

Use this channel layout:

| Channel | Value |
| --- | --- |
| R | GTAO visibility |
| G | Encoded bent-normal X |
| B | Encoded bent-normal Y |
| A | Encoded bent-normal Z |

Encode each normal component from `[-1, 1]` to `[0, 1]`.
Decode and normalize the vector before use.

Do not use octahedral encoding in the first version.
Its seam needs special filter rules.
The extra complexity has little value at half resolution.

### Storage Cost

`RGBA8` uses four times the storage of `R8`.
The measured format cost was small at 1280 x 720 on the Intel P630.
The `r_compactAO 1` test used `R8` and gave 93.22 FPS.
The `r_compactAO 0` test used `RGBA8` and gave 93.04 FPS.
This difference is within normal measurement variation.

This test measured only the image format change.
It did not measure the bent-normal calculation or lighting work.

## GTAO Calculation

Use the directional GTAO calculation from Algorithm 2 of the GTAO paper.
Calculate a visible direction for each horizon slice.
Add the slice directions to one view-space vector.
Normalize the final vector after all slices are complete.

Keep the current scalar visibility calculation.
The visibility value supplies the cone width.
The bent normal supplies the cone axis.

Return a safe surface direction when the depth is invalid.
Return a safe direction when the calculated vector is too small.
Never write a NaN or an infinite value.

The current GTAO normal comes from depth reconstruction.
It does not include a material normal map.
The lighting shader must keep the decoded bent normal in the hemisphere of the
material normal.
It can move the bent normal toward the material normal when the two vectors
have a large difference.

## Filter and Upsample Work

The current filter reads and writes `vec4` values.
This design is a useful base for bent normals.

Update both filter passes as follows:

1. Filter visibility and the encoded bent-normal components together.
2. Keep the current depth rejection rules.
3. Supply a valid packed value for sky pixels.
4. Normalize the bent normal after the final texture sample.
5. Reject a vector that has a length below the selected epsilon.

Apply the same rules to the optional full-resolution upsample pass.
Test the default half-resolution output first.

The world and weapon AO paths use the same filter code.
Keep their depth layers separate.
Do not let the weapon bent normal affect the world.

## Coordinate Conversion

GTAO produces a view-space bent normal.
The lighting shader uses world-space directions for cubemaps and lights.
Transform the bent normal with the camera forward, left, and up axes.

Verify the axis signs with a debug view.
Test the result after camera yaw, pitch, and roll changes.
A wrong sign can make the environment sample the blocked direction.

## Cubemap Options

### Option 1: Direction-Aware Specular Occlusion

Keep the normal reflection vector:

```glsl
vec3 reflection = reflect(-viewDirection, materialNormal);
```

Do not calculate the reflection from the bent normal.
That change would bend and distort visible reflections.

Use the bent normal and visibility as a visibility cone.
Compare this cone with the reflection lobe.
Reduce the cubemap result when the reflection points into the blocked region.
Use material roughness to set the reflection-lobe width.

This option needs no additional cubemap sample.
It can replace the current scalar attenuation of cubemap specular light.
It should reduce bright reflection leaks near walls and corners.

Complexity: medium.
Risk: low to medium.
Recommended: yes.

### Option 2: Diffuse Cubemap Light

Sample diffuse environment light in the bent-normal direction:

```glsl
vec3 environment = textureLod(cubeMap, bentNormal, diffuseMip).rgb;
vec3 indirectDiffuse = environment * albedo * visibility;
```

A diffuse irradiance cubemap is the correct input.
The highest-roughness mip of the current cubemap can be an initial
approximation.

This option adds one cubemap sample.
It also adds new ambient energy to the scene.
It can change the authored appearance of a level.
Do not add it on top of baked lightmap energy without calibration.

Use it first on dynamic models.
Do not use it on baked world surfaces in the first version.

Complexity: medium to high.
Risk: medium.
Recommended: after Option 1.

## Directional Ambient Options

### Option 3: Existing Light-Grid Direction

Dynamic entities already receive these values:

- `u_AmbientLight`
- `u_DirectedLight`
- A dominant light direction

Keep `u_AmbientLight` as the constant ambient term.
Use the GTAO cone to calculate visibility toward the dominant direction.
Apply this visibility to the directional term.

Do not replace the material normal in the diffuse BRDF.
The material normal must still calculate the surface response.
The GTAO cone must only calculate indirect visibility.

This option uses data that the renderer already supplies.
It does not need a new map format.
The existing directed term can behave like direct light.
Tune the result carefully to prevent double shadowing.

Use this option on dynamic models first.
Keep it off for baked lightmap surfaces.

Complexity: medium.
Risk: medium.
Recommended: as an optional second stage.

### Option 4: Directional Ambient Probes

Replace the single ambient RGB value with directional irradiance data.
Possible representations are:

- A diffuse irradiance cubemap
- Low-order spherical harmonics
- A small set of directional light lobes

Evaluate the representation around the bent normal.
Multiply the result by GTAO visibility.
This method can select the color of a visible doorway or window.

This option gives the best directional ambient result.
It also changes probe generation, renderer data, shader uniforms, and lighting
calibration.
It can require new map or asset data.

Complexity: high.
Risk: high.
Recommended: only after the smaller options show sufficient value.

## Controls

Use this latched control:

```text
r_gtaoBentNormals 1
```

Use these values:

- `0`: Scalar GTAO only
- `1`: Calculate and store bent normals

Bent normals are enabled by default after visual and performance tests passed.
The default directional-light blend is `0.25`. Directional irradiance probes
use a full blend when a valid map file is available.

Extend `r_ssaoDebug` with bent-normal views.
Provide separate world and weapon views.
Show the decoded vector as RGB.
Also show invalid or backward vectors with a clear error color.

## Recommended Implementation Order

### Phase 1: Output and Debug

1. Add the optional `RGBA8` AO images.
2. Calculate the view-space bent normal.
3. Pack visibility and the bent normal.
4. Update the filter and upsample passes.
5. Add world and weapon debug views.
6. Do not change scene lighting in this phase.

This phase proves the calculation and coordinate system.

### Phase 2: Specular Cubemap Occlusion

1. Decode and normalize the bent normal in `lightall.glsl`.
2. Transform it to world space.
3. Estimate the visibility-cone width from scalar visibility.
4. Apply cone overlap to cubemap specular light.
5. Keep the normal reflection vector.
6. Test rough and smooth materials.

This phase is the recommended first visual feature.

### Phase 3: Dynamic Directional Ambient

1. Use the existing light-grid dominant direction.
2. Apply directional visibility to dynamic models.
3. Keep constant ambient light separate.
4. Exclude baked world surfaces.
5. Compare the result with scalar AO.

### Phase 4: Diffuse Environment Light

1. Test the highest-roughness cubemap mip as diffuse irradiance.
2. Add a separate irradiance convolution if the approximation is not stable.
3. Calibrate energy against the current ambient term.
4. Prevent duplicate light on baked surfaces.

### Phase 5: Probe Upgrade

Evaluate first-order spherical harmonics on dynamic models. Use pre-baked
spatial probe files for all 60 JA and JO campaign maps. Do not index this data
by unique BSP light-grid records because those records can be reused at
unrelated positions.

Keep the probe work optional and separate from the Phase 1 through 4 controls.
See [Directional Irradiance Probes](docs/irradiance-probes.md) for the format,
distribution decision, bake tradeoffs, and evaluation plan.

## Validation

The disabled path must match the current output.
It must not add measurable work when `r_gtaoBentNormals` is `0`.

The enabled path must meet these requirements:

- All bent normals are finite.
- All final bent normals have unit length.
- Bent normals point toward visible space in a controlled corner scene.
- Camera rotation does not change their world-space meaning.
- Depth edges do not create large color seams.
- Half-resolution output does not flicker during movement.
- Full-resolution upsampling remains optional.
- Weapon AO does not affect world lighting.
- World AO does not affect the weapon.
- Direct lights retain the material normal.
- Baked lightmaps do not receive duplicate occlusion.
- Renderer restart and save/load remain valid.

Add deterministic scenes with a wall, a corner, and two different environment
colors.
Capture scalar AO, bent-normal debug, and final lighting images.

Extend these tests:

- `scripts/test-ssao-sp.py`
- `scripts/test-ssao-weapons.py`
- `scripts/test-rend2-sp.py`

Run the hardware AO tests on the Intel P630.
Run the renderer lifecycle tests with software rendering.
Check for OpenGL errors in both paths.

## Performance Targets

Measure GPU AO time with `r_speeds 100`.
Measure total frame throughput without timer reporting.
Use the same camera, resolution, and warmup for each comparison.

Use these initial targets at 1280 x 720 on the Intel P630:

- Less than 0.5 ms additional median world-AO time
- Less than 5 percent loss in total scene throughput
- No cost when the feature is disabled

The XeGTAO project reports approximately 25 percent additional AO cost for its
directional component.
This value is a reference only.
Our shader, filter, resolution, and hardware are different.

## Risks

- Screen-space data cannot include off-screen occluders.
- Depth normals do not include material normal maps.
- Bent-normal filtering can produce a short or invalid vector.
- Wrong coordinate signs can reverse the result.
- Cubemap probes can have seams or incorrect local parallax.
- Diffuse cubemap light can add too much energy.
- Baked lightmaps can receive duplicate occlusion.
- Direct-light attenuation can create false shadows.
- The extra calculation can remove part of the GTAO performance gain.

## Recommendation

The implementation includes Phases 1 through 4. Keep the master control off by
default. Keep direction-aware specular occlusion as the first enabled lighting
option. Enable diffuse environment light and light-grid visibility only after
level-specific calibration.

Do not apply the diffuse or light-grid options to baked world surfaces. Keep the
probe-system work in Phase 5 separate and optional.

## References

- Jimenez et al., *Practical Real-Time Strategies for Accurate Indirect
  Occlusion*: <https://www.activision.com/cdn/research/Practical_Real_Time_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf>
- Intel GameTechDev, *XeGTAO*: <https://github.com/GameTechDev/XeGTAO>
