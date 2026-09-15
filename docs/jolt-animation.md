# Jolt Animation Prototype

## Scope

The prototype supports one selected stock stormtrooper. Its goal is a visible
impact, loss of balance, physical fall, and return to animation.

Small hits drive torso and head motors. The selected actor does not play a
normal pain clip over these reactions. Running speed, leg hits, and repeated
impacts increase an instability value. When this value reaches its limit,
eleven bodies take control of the pose. They retain the actor's velocity and
fall under gravity. Eligible game knockdown requests also start a physical
fall. After the body settles, a clear-space check permits a get-up blend.

The instability rule is a first controller. It does not yet include balance
steps, bracing, or autonomous muscle control during the fall. Jolt 5.3.0
supplies the rigid-body solver; it does not supply those behaviours.

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

Use `jolt_knockdown` first. The arms, legs, torso, and pelvis should move as
physical bodies. This command now starts a Jolt fall, not a normal knockdown
clip. Use `set g_joltDebug 1` to show the selected-actor marker and green
physical-joint lines.

Starting a fall constructs its collision meshes and can cause a brief pause.
Caching these meshes is a later step.

`jolt_select` without an argument selects the actor under the crosshair.
`jolt_select nearest` selects the nearest eligible actor within 512 game units.
Only the `stormtrooper` NPC type with the stock stormtrooper model is eligible.
Selection replaces the previous rig.

Shoot the selected actor with a blaster or Bryar pistol. For repeatable tests:

```text
jolt_impulse left
jolt_impulse right
jolt_knockdown
```

`jolt_impulse` applies an impulse without health damage. It accepts `left` or
`right`; its default direction is forward. Repeated impulses can cause a fall.

`jolt_hit` applies actual damage and accepts `front`, `back`, `left`, `right`,
or `head`. Directions use
the actor's local axes. Each command applies five points of blaster damage
through `G_Damage`. Existing location multipliers apply; head hits can do more
damage. These commands can kill the actor.

For a moving test, bind an unused key to `jolt_impulse left`, then enter
`jolt_control`. The normal movement keys control the selected actor for up to
30 seconds. Press the impulse key while moving across clear ground. Control
returns to the player when the actor falls. `exitview` also releases control.

For an animation-only comparison, set `g_joltReactionPose 0`, then use a
single `jolt_impulse left` while the actor stands still. Repeat with
`g_joltReactionPose 1` after the first impulse settles. This switch affects
the small additive reaction only; full-body falls always show their physical
pose.

Use `set g_joltReactions 0` to remove the rig. The setting defaults to zero and
requires cheats. It is not archived. No selection is stored in a save.

## Rig and Ownership

The small-reaction rig has a fixed pelvis anchor, a 28 kg torso, and a 5 kg
head. It runs in local space with X forward, Y left, and Z up. Its motors
return additive rotation toward zero. One game unit is 0.0254 metres.

Initial lengths come from the `lower_lumbar`, `cervical`, and `cranium` bone
positions. Torso radius comes from the actor's collision bounds. The console
prints the generated dimensions when an actor is selected. These are initial
estimates, not measured anatomical data.

| Parameter | Torso | Head |
| --- | --- | --- |
| Swing limit | 16 degrees | 12 degrees |
| Twist limit | ±8 degrees | ±8 degrees |
| Motor frequency | 1.8 Hz | 1.8 Hz |
| Damping ratio | 0.8 | 0.8 |
| Maximum motor torque | 120 N m | 25 N m |

Impact direction and position set the impulse and lever arm. Impulses are
limited to 8 N s. Torso rotation is added at `lower_lumbar`; relative head
rotation is added at `cervical`. The existing Ghoul2 bone-angle path writes
both changes to the shared model. Skeletal hit tests use that same model.
The actor's navigation hull, health rules, weapon state, and root position
remain under game control.

The fall rig replaces that local-space rig with eleven world-space capsules:
pelvis, torso, head, upper arms, forearms, thighs, and lower legs. Their masses
are 12, 24, 5, 3, 2, 8, and 4 kg respectively; limb masses apply per side.
Joint limits are relative to the captured pose: 35-degree torso/head swing,
65-degree limb swing, and ±25-degree twist. These limits still need anatomical
calibration. Limb-to-limb self-collision is disabled.

The engine exports solid and clip-brush faces and its collision-patch facets.
Inline brush models use kinematic mesh bodies. Doors and platforms follow
their game transforms; removed or non-solid models stop colliding. Sub-BSP
terrain maps are not supported by this bridge. Other characters and dynamic
non-brush props are not yet part of the Jolt collision scene.

During a fall, Jolt owns the root and bone transforms. NPC movement and firing
pause. The game updates the actor's broad-phase bounds around the physical
pose. Both renderers use the complete bone transforms, including translation
and model scale. Extra Ghoul2 smoothing is disabled for these transforms.
Native mover pushes yield to the kinematic collision bodies during the fall.
Normal platform carry resumes during the get-up blend.

Recovery requires low body speed for 600 ms and at least 1.2 seconds of fall
time. The game searches the current position and eight nearby positions for
ground and standing clearance. If no position is clear, the actor stays down.
A quaternion blend connects the settled pose to `BOTH_GETUP1` over 900 ms.
This is an initial recovery blend; pose-specific get-up selection is pending.

The solver uses fixed 1/120-second steps in game time. Display poses use
quaternion interpolation between steps. A time jump above 250 ms resets the
rig. Slow time changes simulation time through the normal game clock. Fall
gravity uses the game gravity value at the start of the fall.

The standing reaction yields during cinematics, scripted animation tasks,
airborne motion, get-ups, IK, held-character states, and vehicle use. Saber and
emplaced-weapon use also suspend reactions. Death, dismemberment, entity
removal, loading, map changes, and client initialization remove the rig.
Death returns control to the existing death system. A reset during a live
fall restores the last navigation position and original hull. Saving during
a fall performs this reset before writing the save. No physical pose flags
or temporary hull are left in the save. Teleportation resets small reactions.
An external pose owner or teleport ends a physical fall without restoring the
old navigation position.

## Build and Tests

The default SP build includes the prototype. `-DUseJoltReactions=OFF` removes
the dependency and game hooks. Jolt and its adapter use C++17 and static
linking. Jolt uses the MIT license. Its license is included in the package.
The dependency archive and SHA-256 are pinned in
`cmake/Modules/JoltDependencies.cmake`.

Use matching binaries from one package: the game API is 11 and the SP
renderer API is 22.

Build all targets with one job:

```sh
bash scripts/build-sp.sh
cmake --build build/sp --target jolt-reaction-test --parallel 1
build/sp/jolt-reaction-test
python3 scripts/test-jolt-sp.py --package build/ready --renderer rdsp-vanilla
python3 scripts/test-jolt-sp.py --package build/ready --renderer rdsp-rend2
```

The solver test checks impact direction, joint limits, return to the neutral
pose, pause, reset, invalid input, and different frame intervals. It also checks
full-body falls and moving or disabled platform colliders. The game test
checks actual damage, pain-clip isolation, animation-only comparison,
physical falls, movement-triggered falls, recovery, slow time, disable,
saving during a fall, loading, renderer restart, actor removal, and shutdown.
Captures and logs are stored under `build/jolt-tests/`.

`jolt_status` reports the selected entity, active state, hit count, bone-update
count, fixed-step count, peak angle, current angles, health, and mean solver
microseconds per step for the small reaction. `fall_steps` and `fall_us` report
the full-body solver's step count and mean microseconds per step. Timings
exclude animation, drawing, and collision-mesh construction. `launch`
reports game velocity carried into a fall. `pose_error` is the largest
position error across the eleven controlled bones when debug drawing is on.

## Next Stage

Add balance steps and bracing before the instability limit releases the root.
Calibrate anatomical joint limits and add self-collision. Improve recovery
with face-up/face-down detection and aligned get-up clips. Cache collision
shapes before combat to remove construction pauses. Validate doors,
slopes, stairs, ledges, Force powers, and dismemberment across campaign maps
before increasing the actor count.

## Sources

- [Jolt 5.3.0 source](https://github.com/jrouwe/JoltPhysics/tree/v5.3.0)
- [Swing-twist constraints and motors](https://github.com/jrouwe/JoltPhysics/blob/v5.3.0/Jolt/Physics/Constraints/SwingTwistConstraint.h)
- [Jolt ragdoll interface](https://jrouwe.github.io/JoltPhysics/class_ragdoll.html)
