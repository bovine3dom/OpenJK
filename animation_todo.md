# Physics-Driven Animation Plan

## Status and Scope

The next work follows [Reactive Character Control](docs/reactive-animation-research.md).
The previous threshold-and-launch design does not meet the motion target.
Pose history, motor-driven balance, and corrective steps are now implemented.

Jolt projectile reactions are enabled for validated stock stormtroopers without
manual selection. Each actor has an independent reaction record; one actor can
use full-body physics at a time. The system uses the existing mesh, skeleton, skin weights, and animation
clips. See [Jolt Animation Prototype](docs/jolt-animation.md) for controls,
rig parameters, tests, and limits. The prototype uses thirteen bodies and
includes foot contacts, corrective steps, protective arm targets, retained
running momentum, and a checked get-up blend. Use
`openjk-play --worktree rmlui --desktop --jolt-demo` for the test sequence.
Balance still uses bounded root assistance. General locomotion and obstacle
handling need further work.

### First Prototype

- [x] Pin Jolt 5.3.0 and package its MIT license.
- [x] Add an opt-in pelvis anchor with powered torso and head capsules.
- [x] Estimate dimensions from stock bone positions and actor bounds.
- [x] Apply localized blaster impulses through the existing damage path.
- [x] Add reaction rotations to the shared Ghoul2 skeleton.
- [x] Use fixed game-time steps and interpolate display poses.
- [x] Replace eligible knockdowns with physical falls and blend into a get-up.
- [x] Clear transient state during loading, restart, removal, and shutdown.
- [x] Add brush and patch collision, kinematic brush models, and full-body falls.
- [x] Add a non-damaging impulse, pose comparison, and debug joint drawing.
- [x] Replace the old instability threshold with continuous motor control and foot support checks.
- [x] Replace handled projectile pain clips while retaining normal damage and combat callbacks.
- [x] Match a grounded first-frame get-up pose before playing the authored rise.
- [x] Add corrective step attempts and confirmed landings on clear ground.
- [x] Add protective arm targets during a controlled fall.
- [ ] Extend recovery and stepping to obstacles and uneven ground.

Euphoria combines physical simulation with motor control, balance, stepping,
bracing, and recovery. A ragdoll solver alone does not supply those behaviours.
We can build a smaller system with an open-source physics library and our own
controllers. No replacement character art is required for the first prototype.

## Existing Code

- `code/game/g_main.cpp`: `G_RagDoll` currently excludes living characters.
- `code/game/bg_pangles.cpp`: bone IK and arm IK.
- `code/rd-vanilla/G2_API.cpp` and `tr_ghoul2.cpp`: native Ghoul2 pose and ragdoll
  operations, also used by the SP Rend2 module.
- Existing interfaces provide effector goals, impulses, joint constraints, bone
  transforms, and animation playback. Existing clips include falls and get-ups.

## Library Assessment

| Library | Assessment |
| --- | --- |
| [Jolt Physics](https://github.com/jrouwe/JoltPhysics) | Preferred candidate. MIT licence, C++17, Linux support, motors, powered ragdolls, and skeleton mapping. |
| [Bullet](https://github.com/bulletphysics/bullet3) | Mature alternative under the zlib licence. Controllers and game integration are still required. |
| [PhysX](https://github.com/NVIDIA-Omniverse/PhysX) | Capable articulations and rigid bodies. Check the pinned SDK and component licences; the core SDK uses BSD-style terms. |
| [MuJoCo](https://github.com/google-deepmind/mujoco) | Apache 2.0; useful for articulated-body control research. Less direct as an incremental game integration. |
| [MimicKit](https://github.com/xbpeng/MimicKit) | Motion-imitation research framework. Requires motion conversion, training, and runtime integration. Defer learned controllers. |

Jolt's [samples](https://github.com/jrouwe/JoltPhysics/blob/master/Docs/Samples.md)
demonstrate driving a ragdoll toward an animation pose and mapping between
detailed animation skeletons and simpler physics skeletons. These samples do
not provide an autonomous balance or recovery controller.

## Stormtrooper MVP

- [x] Define one low-detail physics rig: capsule dimensions, masses, joint limits,
  motor strengths, and the mapping to stock humanoid bones.
- [ ] Generate initial dimensions from the skeleton and mesh bounds, then inspect
  them. Bone positions alone do not establish correct physical parameters.
- [x] Build static collision from standard BSP collision data, including invisible clip
  brushes and patches. Represent doors and platforms as kinematic bodies.
- [ ] Keep existing navigation and locomotion. Drive the physical rig toward the
  current animation, with explicit ownership of the root transform.
- [x] Add localised blaster-hit and upper-body reactions. Keep weapon aiming and
  saber timing under game control.
- [ ] Blend into falls after strong Force pushes or loss of support.
- [x] Blend into an existing get-up clip after settling and a clearance check.
- [x] Select the get-up clip from the settled orientation and align its start pose.
- [ ] Add balance steps and bracing only after the first reaction/fall loop works.

## Integration Requirements

- [x] Use a fixed physics step in game time and interpolate display poses for reactions.
- [ ] Keep collision and damage queries consistent with the visible pose.
- [ ] Define transitions between navigation, animation, physics, and cinematics.
- [x] Preserve previous project-save loading. The reaction rig has no serialized state.
- [ ] Test Force powers, dismemberment, doors, slopes, stairs, and ledges.
- [ ] Restrict the first implementation to the selected humanoid. Other species,
  droids, vehicles, and large creatures need separate rig validation.
- [ ] Measure one actor first, then a squad. Add distance-based simulation detail
  and sleeping only after correctness checks pass.

Acceptance: the stormtrooper reacts to impacts, remains controllable during
ordinary movement, falls without unstable joints, and returns to a valid
animation without changing mission scripts or damage rules unexpectedly.
