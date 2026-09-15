# Reactive Character Control

## Research Findings

The target is a character that tries to keep its balance after an impact.
Balance control is part of the main system. It is not a final visual effect.

NaturalMotion described Euphoria as a simulation of the body and its motor
control. In a 2006 interview, Torsten Reil said that engine integration was
complete and the team was spending most of its time on adaptive behaviours.
These behaviours use information about the body and its environment.
The exact GTA IV controllers are proprietary. Public descriptions do not
establish their source code, algorithms, or tuning values.

Published work provides useful implementation methods:

- SIMBICON uses feedback control to produce locomotion that responds to pushes
  and terrain changes. Motion capture can provide the reference movement.
- Watch Your Step combines foot support, ankle feedback, variable leg length,
  and corrective steps. A small disturbance can produce a sway; a larger one
  can require a step.
- Jolt supplies bodies, contacts, joint limits, and motors. Our code must supply
  the control goals and transitions.
- Fixed-step simulation and smooth display are separate requirements. Display
  interpolation must use simulation samples that surround the requested time.

These papers are implementation references. They are not Euphoria internals.

## Problems Found in the Previous Prototype

1. The small-reaction rig has a fixed anchor and only moves the torso and head.
   It cannot shift foot support or take a corrective step.
2. A damage/speed counter starts a separate passive full-body rig. This makes
   loss of control a hard switch instead of a failed balance response.
3. The transition adds another impulse. This can launch an actor even when the
   original hit should only disturb its balance.
4. The fall rig starts with root velocity but lacks the motion of individual
   limbs. Its joints have limits and friction, but no active pose control.
5. The server normally updates at 20 Hz. Jolt performs six 120 Hz steps per
   update, but the old display path keeps only the last two samples. Those
   samples cover about 8.3 ms, not the 50 ms server interval. Clamping that pair
   can produce visible pauses followed by jumps.

## Revised Design

### Pose Time

Store a bounded history of timestamped physics poses. Sample this history at
the requested game display time. Use the same clock for the root and bones.
Do not advance physics from a renderer callback. Shadow and colour passes
must see the same pose. Keep the existing server update rate.

### Continuous Physical Control

Use the same bodies through the response and fall. Joint motors follow an
animation reference while control is available. Preserve position, linear
velocity, and angular velocity when control changes. Change motor strength
over time instead of removing all control at once.

Navigation supplies a desired movement. The physical controller supplies the
actual pose. Any temporary root assistance must be bounded and reported. It
must not lift the pelvis into place during a fall.

### Readable Hits and Balance

Separate impact momentum from the visible injury response. Use a short target
pose change and temporary local weakness for readable recoil. A leg hit can
reduce support without adding a large horizontal impulse.

Measure body position and velocity relative to foot support. Try a reachable
corrective step on clear ground. A failed step, loss of support, or insufficient
strength can lead to a fall. Keep some body tension and protective motion
during that fall.

### Validation Order

- [ ] Retain and sample physics poses across complete server intervals.
- [ ] Verify display sampling at 60, 120, and 144 Hz with a 20 Hz server.
- [ ] Track a standing animation with a motor-driven full-body rig.
- [ ] Show a clear small-hit response without a fall or launch.
- [ ] Take a corrective step after a moderate disturbance.
- [ ] Permit a failed recovery to become a fall without a new impulse.
- [ ] Preserve running momentum and limb motion through that transition.
- [ ] Test ground contacts, recovery, slow time, and lifecycle resets.

Start with one controlled full-body actor. Keep the existing damage and script
rules. Increase the actor budget after correctness and cost checks pass.

## Test Results: 15 September 2026

The solver tests pass for standing support, small-hit recoil, and a rig with
stormtrooper body dimensions. The pose history tests compare 60, 120, and 144 Hz
samples with a 120 Hz reference while the server updates at 20 Hz.

The head and arm motor targets now use the changed torso target when they
calculate relative joint rotation. The head uses a smaller recoil target.
The controller starts a step when the balance error exceeds 0.14 m.

The game control test passes with both vanilla and Rend2. It checks standing,
small-hit recoil, an explicit fall, and reset. The projectile tests also pass
with both renderers. These checks cover automatic reactions on multiple actors,
damage, protected targets, unsupported targets, and the disable control.

Successful step recovery is still unverified. The solver starts a corrective
step after the leg disturbance, but the actor falls. The test checks that an
attempt starts; it does not establish successful recovery. Visual checks at
multiple display rates and moving transitions are also incomplete.

## Sources

- [NaturalMotion interview, 2006](https://www.psu.com/news/psu-interviews-naturalmotion/)
- [GTA IV Euphoria announcement, 2007](https://www.gamedeveloper.com/game-platforms/product-i-grand-theft-auto-iv-i-using-naturalmotion-s-euphoria)
- [SIMBICON, SIGGRAPH 2007](https://www.cs.ubc.ca/~van/papers/simbicon.htm)
- [Watch Your Step, 2022](https://arxiv.org/html/2210.14730v1)
- [Jolt ragdoll interface](https://jrouwe.github.io/JoltPhysics/class_ragdoll.html)
- [Fix Your Timestep](https://gafferongames.com/post/fix_your_timestep/)
