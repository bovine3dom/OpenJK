# Duke Boot Roadmap

## Summary

A melee kick is now a working first prototype in the OpenJK code. The shared
movement path selects the kick, plays the leg animation, raises the player, and
emits the kick event. The existing server trace remains authoritative.

The first-person body path now shows the full body during the kick. It hides the
head, keeps the torso and legs available, and uses the same presentation for
normal movement. It does not use a special kick model.

The feature is not blocked by torso animation. The kick plays on the legs while
the weapon torso pose remains active. The remaining work is balance, camera
polish, aim-pose work, and broader playtesting.

## Existing support

Relevant code now provides:

- Kick animations such as `BOTH_A7_KICK_F` and `BOTH_A7_KICK_F_AIR`.
- Separate torso and legs animation channels.
- Animation blending through `PM_SetAnim()`.
- Kick state checks through `PM_KickingAnim()`.
- A separate `+kick` command, `BUTTON_KICK`, and `PMF_KICK_HELD`.
- Foot bolt positions for hit detection.
- Kick damage, knockdown, push, and impact effects through `G_KickTrace()`.
- A kick event and melee kick sound.
- A small upward impulse that increases with Force Push level.
- A first-person body view that follows the shared kick animation.

The kick can run with the saber equipped. The first-person path does not create
a kick-specific model.

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

The first forward kick is implemented. Its shared movement path uses logic
similar to:

```cpp
PM_SetAnim(pm, SETANIM_LEGS, BOTH_A7_KICK_F,
    SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD, 100);
```

The implementation:

1. Keeps the rifle pose on the torso.
2. Plays the kick on the pelvis and legs.
3. Uses a held-button flag to prevent repeated kicks.
4. Reuses the existing kick trace because the trace checks `legsAnim`.
5. Stops normal weapon firing during the kick.
6. Rejects crouching, knockdown, vehicle, and weapon-change states.

Open animation work includes air, side, and backward kicks; animation timing;
and any first-person-only pose correction. Do not add a special kick model.

## First-person rendering findings

Normal rifle first person does not render the local player body. The current body
path uses a cloned Ghoul2 model with `RF_FIRST_PERSON`; it keeps the normal world
model available for mirrors and hides the head surface in the local clone.

The body camera follows the animated neck area with separate neck-axis and height
controls. The current controls are:

- `cg_firstPersonBodyNeckOffset`: neck-axis offset.
- `cg_firstPersonBodyHeightOffset`: world height offset.

The high-detail weapon remains visible in body mode. The code first anchors it to
the animated hand, then aligns its muzzle and barrels to the animated world
weapon muzzle. This prevents the model-origin offset from moving the weapon away
from the hand. Projectile and reticle muzzle data use the same live world muzzle.

The saber still uses its special player-model path. A general first-person saber
presentation remains open.

Do not create a temporary lower-body or kick-only model. Use the same cloned full
body for normal movement and kicks.

## Implementation phases

### Phase 1: Gameplay prototype — complete for the first prototype

- [x] Add `BUTTON_KICK` and a `+kick` binding.
- [x] Add shared kick selection in `bg_pmove.cpp`.
- [x] Play `BOTH_A7_KICK_F` on `SETANIM_LEGS`.
- [x] Reuse `G_KickTrace()`.
- [x] Add kick sound and Force Push-scaled lift.

Damage, range, knockdown, prediction, and weapon interruption still need focused
playtesting.

### Phase 2: First-person proof of concept — complete

- [x] Render a cloned local player model in first person.
- [x] Hide the head without hiding the legs.
- [x] Confirm that the body follows the shared kick animation.
- [x] Keep the model available for normal body mode, not only kicks.

### Phase 3: Full-body view model — replaces the lower-body-only plan

- [x] Keep the normal world player hidden from the main first-person view.
- [x] Keep the world player available for mirrors.
- [x] Avoid changing shared surface state by using a clone.
- [x] Tune the neck anchor, height, depth, and weapon muzzle placement.
- [ ] Test all player models, skins, weapons, slopes, stairs, water, mirrors,
  and weapon changes.

### Phase 4: Polish and balance

- [x] Add the kick event and start sound.
- [x] Add Force Push-scaled upward impulse.
- [ ] Fix bug: force push does _not_ appear to scale the impulse ingame.
- [x] Remove the obsolete kick camera pitch and roll settings.
- [ ] Make upwards impulse tuneable ingame, and the backwards one too, separately.
- [ ] Test demos, save games, prediction, and multiplayer behavior.
- [ ] Tune damage, push, range, cooldown, and animation timing.
- [ ] Add air and directional kicks if wanted.

## Main risks

1. **Body and camera clipping**
   - The full body must show the kick without placing the head in the camera.
2. **Weapon pose**
   - The current body-driven weapon placement works, but a first-person aim or
     ready pose is still needed.
3. **Animation suitability**
   - The existing kick animations were made for whole-body saber moves. Test
     the rifle torso pose and lower-body result.
4. **Shared model state**
   - Keep surface changes on the cloned model so mirrors remain correct.
5. **Gameplay and view timing**
   - The kick must use shared prediction and server traces. The view must not
     change damage, range, or hit timing.

## Difficulty assessment

- Gameplay kick: low to moderate difficulty.
- Legs-only rifle blending: low difficulty because the animation split already exists.
- Basic first-person proof of concept: moderate difficulty.
- A polished, model-independent boot view model: the largest part of the work.

The first-person version is no longer blocked by body rendering. A cloned full-body
view model works for normal movement and kicks. The remaining work is weapon aim
presentation, camera polish, balance, and broad asset testing.
