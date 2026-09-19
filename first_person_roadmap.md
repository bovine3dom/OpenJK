# First-Person Body Roadmap

## Research question

How do games show a body in first person? Do they reuse the third-person animation, or do they use a separate presentation?

## Short answer

Games usually reuse the gameplay state and much of the animation data. They do not usually reuse the complete third-person render without changes.

Even games with strong body awareness use some visual cheats:

- A first-person mesh or lower-body mesh.
- A separate first-person field of view.
- A different render layer or depth range.
- Hidden head and upper-body surfaces.
- Separate weapon and hand alignment.
- First-person-only aim and camera corrections.
- First-person suppression of animations that would harm aiming or cause clipping.

The main split is:

- **Shared truth:** movement state, animation state, skeleton, weapon state, and gameplay position.
- **Separate presentation:** camera, visible mesh, FOV, depth, weapon pose, and camera motion.

This means OpenJK does not need to choose between a fully shared body and a fully fake body. A hybrid system is the best fit.

## Findings from other games and engines

### Arma

Arma is the closest comparison for this project.

The available Bohemia community material indicates that Arma uses the same general character animation system for first-person and third-person characters. A Bohemia forum discussion states that Arma 3 has one animation set for both views. Another discussion explains that first person suppresses or simplifies some complex character animation to keep aiming stable, while third person shows the full character animation.

This suggests the following Arma model:

- The physical character state is shared.
- The body animation is mostly shared.
- First-person rendering filters or replaces parts of the result.
- Weapon clipping and aiming use different first-person rules.
- The camera does not simply become an unmodified third-person camera inside the head.

Arma also has first-person-specific model presentation. Bohemia documentation and issue material refer to View-Pilot model LODs. This is evidence that first-person geometry can differ from normal world geometry, although it does not prove that the complete player body uses a separate animation set.

**Conclusion:** Arma appears to reuse the animation truth, but not every animation layer or every render decision.

Sources:

- [Arma 3 animations](https://community.bistudio.com/wiki/Arma_3:_Animations)
- [Arma 3 LOD documentation](https://community.bistudio.com/wiki/LOD)
- [Bohemia View Pilot LOD issue](https://feedback.bistudio.com/T180184)
- [Bohemia forum: first-person and third-person animations](https://forums.bohemia.net/forums/topic/61440-1st-person-animations-vs-3rd-person/)
- [Bohemia forum: better animations](https://forums.bohemia.net/forums/topic/139062-better-animations/?do=findComment&comment=2268896)

The forum sources are community discussions. They are useful evidence, but they are not engine documentation.

### Mirror's Edge

DICE presented Mirror's Edge as a full-body first-person movement problem. Its GDC presentation describes a custom animation and tool pipeline for believable first-person parkour.

This is important because parkour exposes the whole body. A simple arms-only view model cannot show a vault, wall run, kick, or landing correctly. A full-body system therefore needs more than a third-person mesh placed around the camera.

The likely lesson is:

- Use the full gameplay body for large movement actions.
- Drive the camera from a controlled body or camera bone.
- Author or adjust animations for first-person comfort.
- Limit head motion and camera distortion.
- Add special handling for actions that move the whole body.

The GDC page does not expose enough technical detail to reproduce the system directly.

Source:

- [GDC: Creating First Person Movement for Mirror's Edge](https://gdcvault.com/play/1012171/Creating-First-Person-Movement-for)

### Unreal Engine default pattern

Epic's first-person C++ tutorial uses separate first-person and third-person meshes. The first-person mesh is visible only to the owning player. The third-person mesh is visible to other players.

The tutorial also uses different visibility and shadow rules for the two meshes.

This is the standard shooter pattern:

- First person gets a camera-optimized arms or body mesh.
- Third person gets the normal character mesh.
- The two meshes can share a skeleton and animation state.
- Their transforms and visibility rules can differ.

Source:

- [Epic: Add a first-person camera, mesh, and animation](https://dev.epicgames.com/documentation/en-us/unreal-engine/coder-04-adding-a-firstperson-camera-mesh-and-animation)

### Bevy example

The official Bevy example separates a view model from the world model. It uses separate render layers, render order, and FOV rules.

This confirms that a first-person model is often treated as a special render pass, even when it represents the same player.

The important techniques are:

- A view model camera renders the local body or weapon.
- A world camera renders the world and normal player models.
- The view model can use a fixed FOV.
- The view model can render after the world model.
- Lighting can be supplied to both layers.

Source:

- [Bevy: First person view model](https://bevy.org/examples/camera/first-person-view-model/)

### Full-body middleware patterns

NeoFPS documents three useful configurations:

1. A full body with synced weapon arms.
2. A torso and legs body with camera-aligned weapon arms.
3. Separate weapon and arm rigs.

Its full-body configuration uses a head camera constraint, a steering hierarchy, and weapon alignment against the upper chest instead of directly against the camera. This prevents the weapon from drifting away from the body during vertical aiming.

This is a useful middle ground for OpenJK. The body and legs can use the normal animation state while the weapon hands remain a controlled first-person view model.

Source:

- [NeoFPS: First Person Body](https://docs.neofps.com/manual/fpcharacters-firstpersonbody.html)

## Reuse versus cheating

### Reuse directly

These parts should remain shared between first and third person:

- Player origin and collision.
- Movement state.
- Ground and air state.
- Torso and legs animation numbers.
- Animation timers.
- Weapon state.
- Kick timing.
- Foot bolt positions used for the server trace.
- The skeleton and animation files where possible.

For the kick, the server should still use the world player model and its foot bolts. The first-person boot must never control hit detection.

### Cheat or separate deliberately

These parts should be allowed to differ:

- First-person model origin and scale.
- First-person FOV.
- Depth handling.
- Head and upper-body visibility.
- First-person weapon and hand pose.
- Camera pitch and roll.
- Aim stabilization.
- Camera bob and impact movement.
- Animation layers that would place geometry through the camera.

This is not a failure of synchronization. It is the normal solution to the fact that a camera inside a body has different visual needs from a camera outside it.

## Implications for OpenJK

OpenJK already has a hybrid foundation:

- `CG_Player()` renders the Ghoul2 player model for world and mirror views.
- Normal first-person weapon views use separate hands and weapon models.
- The saber path uses special player-model rendering.
- `PM_SetAnim()` has separate torso and legs channels.
- The renderer already has `RF_DEPTHHACK` and `RF_FIRST_PERSON` flags.
- Kick traces use server-side animation and foot bolt positions.

The main missing feature is a local first-person body render that can show the kicking leg without exposing the full player model.

## Recommended design

Use a gameplay-synced full-body view model.

### Shared animation

Keep one gameplay animation state. For a rifle kick, play the existing kick animation on the legs only:

```cpp
PM_SetAnim(pm, SETANIM_LEGS, BOTH_A7_KICK_F,
    SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD, 100);
```

The torso can keep the rifle-ready animation. The client view model should read the predicted legs animation and timer. The kick must use the same full-body view model as other actions. Do not create a kick-only model.

### Separate local presentation

Render the local full body when the first-person body mode is active:

- Show the torso, pelvis, legs, and boots when the player looks down.
- Keep the head surface hidden to prevent near-camera clipping.
- Keep the normal rifle hands and weapon model.
- Render the local body with first-person depth handling.
- Keep the world player model hidden from the main first-person view.
- Keep the world player model available for mirrors and other players.

Keep the torso visible unless it causes a clear clipping or aiming problem. Hide more surfaces only as a presentation fix, not as a separate kick model.

### Do not attach the camera to the head yet

A true full-body camera attached to the animated head can provide strong body awareness. It also creates new problems:

- Head animation can shake the camera.
- Neck and spine poses can distort the view.
- Existing OpenJK weapon alignment assumes a normal first-person camera.
- Camera pitch and body pitch can fight each other.
- Some existing animations can place the face or geometry inside the near plane.

Keep the current camera system. Add a controlled kick offset first. Consider a head or camera bone only after the lower-body prototype works.

### Do not use the first-person render for gameplay

The first-person body is cosmetic. The server must continue to use:

- The player collision shape.
- The server animation state.
- The server foot bolts.
- `G_KickTrace()`.

This keeps prediction and multiplayer behavior stable.

## Implementation plan

### Phase 0: Rendering experiment

Goal: prove that the current Ghoul2 model can render as a local view model.

- Add a developer-only test path.
- Render a duplicate local player model near the first-person camera.
- Apply `RF_DEPTHHACK` and `RF_FIRST_PERSON`.
- Confirm that the model does not appear in mirrors twice.
- Test camera pitch, yaw, crouching, and weapon changes.

Do not change gameplay in this phase.

### Phase 1: Kick gameplay

Use the plan in `duke_boot_roadmap.md`:

- Add a dedicated kick button.
- Add a held-button flag.
- Start a forward kick in shared movement code.
- Apply the kick to `SETANIM_LEGS`.
- Stop movement and normal firing during the kick.
- Reuse `G_KickTrace()`.

### Phase 2: Static full-body view model

- Render the existing full player model in the first-person view.
- Use fixed offsets first.
- Keep the gameplay origin and orientation authoritative.
- Hide the head surface.
- Confirm that the body is visible when looking down.

Do not create a boot-only or kick-only model.

### Phase 3: Animate the view model

- Drive the full body from the predicted legs and torso animation state.
- Reuse the existing Ghoul2 skeleton.
- Preserve the rifle torso pose.
- Compare the visible boot position with the server foot bolt.
- Correct only visual offsets. Do not change hit detection to match the view model.

### Phase 4: Isolate the first-person body

Use a cloned Ghoul2 player model with the head surface disabled.

- Keep the torso, pelvis, legs, and boots available.
- Keep surface changes off the live world model.
- Hide additional surfaces only when they clip the camera or weapon.
- Keep one view model for normal movement and kicks.

Use a separate lower-body model only if the cloned full-body model cannot meet the clipping and aiming requirements.

### Phase 5: Weapon and camera polish

- Keep the rifle hands aligned with the camera.
- Replace the current third-person hip-fire pose with a view-aligned aim animation.
- Keep first-person weapon alignment separate from the third-person body animation.
- Verify that projectile and hitscan origins use the intended aim point.
- Add a small pitch and roll response.
- Add kick start and impact sounds.
- Tune view-model scale and offsets for different FOV settings.
- Test crouching, jumping, slopes, stairs, mirrors, and water.
- Test all player models and skins.

### Phase 6: Optional true full-body mode

Only consider this after the hybrid system works.

- Add a camera or eye bone.
- Blend body aim toward the view angles.
- Stabilize the camera during head and torso animation.
- Add animation-specific camera rules.
- Decide whether the rifle should use body-driven hands or remain a separate view model.

This is a larger feature. It is not required for a convincing kick.

## Decisions to make

### 1. What is the target view?

Choose one:

- **Full-body awareness:** show the complete body when looking down. Recommended.
- **Lower-body awareness:** use this only if the torso causes repeated clipping or aiming problems.
- **Saber-style body view:** use the existing special body path for all melee actions. Do not use this as the general weapon solution.

### 2. Should the torso be visible?

Yes. Keep the torso visible in first person. Hide the head first. Hide more torso surfaces only if testing shows a clear clipping or aiming problem.

### 3. Should the first-person model use the same Ghoul2 instance?

Recommendation: test a duplicate or isolated instance first. Shared surface changes may affect mirror rendering and other passes.

### 4. Should first person use a separate FOV?

Recommendation: start with the current FOV and depth handling. Add a separate view-model FOV only if the boot clips or scales badly. A separate FOV requires more renderer work.

### 5. Should first person use a separate animation?

Recommendation: share the kick animation and legs timer first. Add first-person-only animation corrections only when the shared animation causes clipping or poor readability.

### 6. Should the camera follow the animated body?

Recommendation: no for the first version. Keep the existing camera and add controlled camera offsets. A body-driven camera is a later option.

### 7. Should the first-person body affect collision or damage?

Recommendation: never. Use the server player state and existing kick trace.

## Success criteria

The first production version is successful when:

- A rifle remains correctly held during a kick.
- The kick animation is visible in first person.
- The visible boot does not block the crosshair.
- The boot does not appear in mirrors as a second player.
- The server hit trace matches the kick timing.
- Other players see the normal full kick animation.
- Prediction does not cause visible kick delay or duplicate kicks.
- The full body is visible when looking down.
- The head does not clip into the first-person camera.
- The same body presentation works during normal movement and kicks.

## Final recommendation

Implement a shared gameplay animation with a separate first-person full-body presentation. This follows the common industry split:

- Reuse the animation and gameplay truth.
- Cheat the camera, FOV, depth, and visible geometry.
- Hide the head while keeping the torso and legs visible.
- Use the same body presentation for movement and kicks.
- Keep the third-person model authoritative for other players and hit detection.

This gives OpenJK the useful part of Arma-style body awareness without requiring a complete true-first-person camera system.

## Source and research notes

The web pages above were researched with Kagi summaries. The source links are included so the implementation team can inspect the original material. GDC pages provide high-level session descriptions, not the full presentation contents. The Arma forum findings are community reports, not official engine specifications.
