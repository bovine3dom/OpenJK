# Duke Boot Roadmap

## Summary

A melee kick is practical in the OpenJK code. The gameplay code already has most of the required support.

The first-person view is the main task. The player body is hidden in normal first-person weapon views. A kick needs a visible leg or boot. This is a presentation problem, not a movement or damage problem.

The feature is not blocked by torso animation. The animation system can play the kick on the legs while it keeps the rifle pose on the torso.

## Existing support

Relevant code already provides:

- Kick animations such as `BOTH_A7_KICK_F` and `BOTH_A7_KICK_F_AIR`.
- Separate torso and legs animation channels.
- Animation blending through `PM_SetAnim()`.
- Kick state checks through `PM_KickingAnim()`.
- Foot bolt positions for hit detection.
- Kick damage, knockdown, push, and impact effects through `G_KickTrace()`.

Important locations:

- `code/game/bg_pmove.cpp`
  - Saber kick input and kick selection.
  - `PM_SetSaberMove()`.
  - Weapon processing.
- `code/game/bg_panimate.cpp`
  - Kick animation tables and `PM_KickingAnim()`.
- `code/game/g_active.cpp`
  - Kick timing and `G_KickTrace()`.
- `code/client/cl_input.cpp`
  - Button command registration.
- `code/qcommon/q_shared.h`
  - User command button definitions.
- `code/cgame/cg_players.cpp`
  - Player model rendering and first-person hiding.
- `code/cgame/cg_weapons.cpp`
  - First-person hand and weapon rendering.
- `code/cgame/cg_view.cpp`
  - First-person and saber camera handling.

## Animation plan

Add a separate kick button. Do not reuse the saber-specific alt-attack path.

A normal ground kick can use logic similar to:

```cpp
PM_SetAnim(pm, SETANIM_LEGS, BOTH_A7_KICK_F,
    SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD, 100);
```

This should:

1. Keep the rifle pose on the torso.
2. Play the kick on the pelvis and legs.
3. Blend into and out of the current lower-body animation.
4. Reuse the existing kick trace because the trace checks `legsAnim`.

The first version should use one forward kick. Later versions can add air, side, and backward kicks.

The kick should also:

- Use a new `BUTTON_KICK` bit.
- Use an edge or held-button flag such as `PMF_KICK_HELD`.
- Stop movement during the kick.
- Prevent normal weapon firing during the kick.
- Reject crouching, knockdown, vehicle, and weapon-change states.

## First-person rendering findings

Normal rifle first person does not render the local player body. `CG_Player()` marks the local body with `RF_THIRD_PERSON`, so the body appears in mirrors but not in the main view. `CG_AddViewWeapon()` renders separate first-person hands and weapon models.

The saber uses a special path. The normal view weapon path skips the saber, and the player Ghoul2 model draws the saber. The camera and player rendering contain special saber handling. This gives us a useful precedent, but it does not directly solve a rifle kick.

Do not enable the full player body for all first-person views. That would expose the head and torso and could obstruct the camera. It would also duplicate the rifle hands and weapon view model.

The preferred solution is a temporary lower-body view model:

- Render only while a kick is active.
- Use the predicted player animation.
- Show the pelvis, thighs, and kicking boot.
- Keep the normal rifle hands and weapon view model.
- Use `RF_DEPTHHACK` and first-person placement.
- Add a small camera tilt or view kick if needed.

The lower-body model may need a separate Ghoul2 instance or a model/surface setup that hides the upper body. Surface changes on the live player model could affect mirrors and other render passes, so a separate instance is safer.

## Implementation phases

### Phase 1: Gameplay prototype

- Add `BUTTON_KICK` and a `+kick` binding.
- Add shared kick selection in `bg_pmove.cpp`.
- Play `BOTH_A7_KICK_F` on `SETANIM_LEGS`.
- Reuse `G_KickTrace()`.
- Test damage, range, knockdown, prediction, and weapon interruption.

This phase does not need a visible boot. It confirms that the server and client use the same kick timing.

### Phase 2: First-person proof of concept

- Add a temporary local player model render during a kick.
- Place it near the first-person camera.
- Hide the head and upper body if possible.
- Confirm that the boot follows the predicted kick animation.

A full temporary body is acceptable for this test. It is not the final presentation.

### Phase 3: Lower-body view model

- Create or configure a lower-body-only Ghoul2 view model.
- Keep the normal world player hidden in first person.
- Avoid changing shared surface state during rendering.
- Tune position, scale, field of view, lighting, and depth handling.
- Test all player models and skins.

### Phase 4: Polish

- Add kick start and impact sounds.
- Add camera movement and landing response.
- Tune damage, push, range, and cooldown.
- Add air and directional kicks if wanted.
- Test mirrors, demos, save games, prediction, and multiplayer behavior.

## Main risks

1. **Partial Ghoul2 rendering**
   - The model must show the boot without showing the head and torso.
2. **View-model placement**
   - The boot must align with the camera and still look attached to the player.
3. **Animation suitability**
   - The existing kick animations were made for whole-body saber moves. The lower-body result may need new animation work.
4. **Shared model state**
   - Surface changes on the live player model may affect mirror rendering or later frames.
5. **Weapon timing**
   - The kick must stop rifle firing without changing the rifle torso pose.

## Difficulty assessment

- Gameplay kick: low to moderate difficulty.
- Legs-only rifle blending: low difficulty because the animation split already exists.
- Basic first-person proof of concept: moderate difficulty.
- A polished, model-independent boot view model: the largest part of the work.

The first-person version is therefore semi-blocked by player-body rendering only in the visual sense. It does not require a visible torso at all times. It requires a reliable way to render a temporary lower body or boot in first person.
