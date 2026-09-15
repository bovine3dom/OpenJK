# Physics-Driven Animation Plan

## Status and Scope

The first Jolt reaction prototype is implemented for one selected stock
stormtrooper. It uses the existing mesh, skeleton, skin weights, and animation
clips. See [Jolt Animation Prototype](docs/jolt-animation.md) for controls,
rig parameters, tests, and limits. Full-body physical falls remain pending.

### First Prototype

- [x] Pin Jolt 5.3.0 and package its MIT license.
- [x] Add an opt-in pelvis anchor with powered torso and head capsules.
- [x] Estimate dimensions from stock bone positions and actor bounds.
- [x] Apply localized blaster impulses through the existing damage path.
- [x] Add reaction rotations to the shared Ghoul2 skeleton.
- [x] Use fixed game-time steps and interpolate display poses.
- [x] Release pose control during native knockdowns and get-ups.
- [x] Clear transient state during loading, restart, removal, and shutdown.
- [ ] Add world collision and extend the rig to full-body falls and recovery.

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

- [ ] Define one low-detail physics rig: capsule dimensions, masses, joint limits,
  motor strengths, and the mapping to stock humanoid bones.
- [ ] Generate initial dimensions from the skeleton and mesh bounds, then inspect
  them. Bone positions alone do not establish correct physical parameters.
- [ ] Build static collision from BSP collision data, including invisible clip
  brushes and patches. Represent doors and platforms as kinematic bodies.
- [ ] Keep existing navigation and locomotion. Drive the physical rig toward the
  current animation, with explicit ownership of the root transform.
- [ ] Add localised blaster-hit and upper-body reactions. Keep weapon aiming and
  saber timing under game control.
- [ ] Blend into falls after strong Force pushes or loss of support.
- [ ] Select and blend into existing get-up clips after settling.
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
