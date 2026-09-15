# Jolt Animation Prototype

## Scope

This first prototype adds physical torso and head reactions to one selected
stock stormtrooper. Jolt 5.3.0 supplies rigid bodies, joint limits, and position
motors. Existing animation clips remain the target pose. The motors return
the additive rotation to zero after an impact.

This is the first stage of `animation_todo.md`. Full-body physical falls,
balance steps, bracing, and physical recovery are not implemented. The game
still controls knockdown and get-up clips. The reaction rig stops during
these clips and resumes from zero when the actor can stand again.

## Use

Load a map with cheats enabled, for example `devmap t1_sour`. Wait for the
opening scene to finish. Stand on clear ground, then enter:

```text
set g_joltReactions 1
npc spawn stormtrooper jolt_demo
wait 20
jolt_select nearest
jolt_status
```

`jolt_select` without an argument selects the actor under the crosshair.
`jolt_select nearest` selects the nearest eligible actor within 512 game units.
Only the `stormtrooper` NPC type with the stock stormtrooper model is eligible.
Selection replaces the previous rig.

Shoot the selected actor with a blaster or Bryar pistol. For repeatable tests:

```text
jolt_hit front
jolt_hit left
jolt_hit head
jolt_knockdown
```

`jolt_hit` accepts `front`, `back`, `left`, `right`, or `head`. Directions use
the actor's local axes. Each command applies five points of blaster damage
through `G_Damage`. Existing location multipliers apply; head hits can do more
damage. These commands can kill the actor. `jolt_knockdown` tests the existing
game knockdown and get-up path; Jolt does not control that fall.

Use `set g_joltReactions 0` to remove the rig. The setting defaults to zero and
requires cheats. It is not archived. No selection is stored in a save.

## Rig and Ownership

The rig has three capsules: a fixed pelvis anchor, a 28 kg torso, and a 5 kg
head. Physics runs in local space with X forward, Y left, and Z up. One game
unit is treated as 0.0254 metres. Gravity and body contacts are disabled for
this reaction-only rig.

Initial lengths come from the `lower_lumbar`, `cervical`, and `cranium` bone
positions. Torso radius comes from the actor's collision bounds. The console
prints the generated dimensions when an actor is selected. These are initial
estimates, not measured anatomical data.

| Parameter | Torso | Head |
| --- | --- | --- |
| Swing limit | 16 degrees | 12 degrees |
| Twist limit | ±8 degrees | ±8 degrees |
| Motor frequency | 3.5 Hz | 3.5 Hz |
| Damping ratio | 1 | 1 |
| Maximum motor torque | 120 N m | 25 N m |

Impact direction and position set the impulse and lever arm. Impulses are
limited to 8 N s. Torso rotation is added at `lower_lumbar`; relative head
rotation is added at `cervical`. The existing Ghoul2 bone-angle path writes
both changes to the shared model. Skeletal hit tests use that same model.
The actor's navigation hull, health rules, weapon state, and root position
remain under game control.

The solver uses fixed 1/120-second steps in game time. Display poses use
quaternion interpolation between steps. A time jump above 250 ms resets the
rig. Slow time changes simulation time through the normal game clock.

The rig yields during cinematics, scripted animation tasks, airborne motion,
knockdowns, get-ups, IK, held-character states, and vehicle use. Saber and
emplaced-weapon use also suspend reactions. Death, dismemberment, entity
removal, loading, map changes, and client initialization remove the rig.
Teleportation resets its rotation and velocity.

## Build and Tests

The default SP build includes the prototype. `-DUseJoltReactions=OFF` removes
the dependency and game hooks. Jolt and its adapter use C++17 and static
linking. Jolt uses the MIT license. Its license is included in the package.
The dependency archive and SHA-256 are pinned in
`cmake/Modules/JoltDependencies.cmake`.

Build all targets with one job:

```sh
bash scripts/build-sp.sh
cmake --build build/sp --target jolt-reaction-test --parallel 1
build/sp/jolt-reaction-test
python3 scripts/test-jolt-sp.py --package build/ready --renderer rdsp-vanilla
python3 scripts/test-jolt-sp.py --package build/ready --renderer rdsp-rend2
```

The solver test checks impact direction, joint limits, return to the neutral
pose, pause, reset, invalid input, and different frame intervals. The game test
checks actual damage, bone updates, return from reactions, native knockdown,
slow time, disable, save loading, renderer restart, actor removal, and shutdown.
Captures and logs are stored under `build/jolt-tests/`.

`jolt_status` reports the selected entity, active state, hit count, bone-update
count, fixed-step count, peak angle, current angles, health, and mean solver
microseconds per step. The timing excludes normal Ghoul2 animation and drawing.

Both renderer tests passed in `rdsp-vanilla.a5ixuduu` and
`rdsp-rend2.z47pkyy0`. The single-actor solver averaged approximately 6–9
microseconds per step on this test host. Larger rigs and world contacts will
require new measurements.

## Next Stage

Before a root can move under Jolt control, add BSP collision for solid and
clip brushes and patches. Add kinematic bodies for doors and platforms.
Then extend the rig to the limbs, define root ownership during a fall, and
align existing get-up clips with the physical pose. Validate slopes, stairs,
ledges, Force powers, and dismemberment before increasing the actor count.

## Sources

- [Jolt 5.3.0 source](https://github.com/jrouwe/JoltPhysics/tree/v5.3.0)
- [Swing-twist constraints and motors](https://github.com/jrouwe/JoltPhysics/blob/v5.3.0/Jolt/Physics/Constraints/SwingTwistConstraint.h)
- [Jolt ragdoll interface](https://jrouwe.github.io/JoltPhysics/class_ragdoll.html)
