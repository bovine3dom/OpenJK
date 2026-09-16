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

- [x] Retain and sample physics poses across complete server intervals.
- [x] Compare pose-history sampling at 60, 120, and 144 Hz with a 20 Hz server.
- [ ] Review motion at these display rates on a GPU desktop.
- [x] Track a standing animation with a motor-driven full-body rig.
- [x] Show a clear small-hit response without a fall or launch in the test scene.
- [x] Take a corrective step after a moderate disturbance on clear ground.
- [x] Permit a failed recovery to become a fall without a new impulse.
- [x] Preserve moving-rig velocity through that transition.
- [x] Test ground contacts, recovery, slow time, and lifecycle resets on the test map.

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

At this stage, successful step recovery was unverified. The leg test started
an attempt but ended in a fall.

## Controller Changes: 16 September 2026

The new tests use a captured stock weapon-idle pose, including its full bone
matrices and staggered feet. This exposed problems that the symmetric test
rig did not show:

- The knee bend direction could reverse as the foot moved forward. The solver
  now retains the reference bend direction and leg twist.
- A landed foot could trigger another step before load transfer finished.
  The controller now fits one balanced stance and blends toward it.
- Brief contact loss could start an unnecessary step. Contact tolerance is
  separate from the actual contact needed for root assistance.
- Early spawn poses could differ greatly from the engagement pose. The shadow
  rig now fits body transforms to current landmarks. Engagement fits joint
  anchors and limit frames without replacing the bodies or their velocities.
- Foot boxes were centred at the ankle. They now extend down to the sole.
- Leg weakness now reduces motor stiffness and damping as well as torque.
  A withdrawal target makes the local response visible when the other leg can
  keep the actor upright. A stronger hit can produce a knee collapse.
- A moving knockdown could add the native root velocity twice. The handoff now
  consumes that velocity once.

The controller uses at most 220 N of horizontal root force and 80 N m of root
torque while the feet have contact. No upward root force is supplied. All root
assistance stops during a fall. These limits are reported and tested. Fully
muscle-driven balance remains a later goal.

The captured-pose solver test now requires a landing and sustained balance
after pushes at three different times. It also checks velocity and pose
continuity at release. The display-history test compares every bone matrix,
including rotation, at 60, 120, and 144 Hz.

The game demonstration has separate idle, hit, step, leg, run, and fall cases.
It uses a clear area of `t1_sour`, a fixed starting frame for standing cases,
and normal NPC movement for the running case. See
[Jolt Reactive Animation](jolt-animation.md) for launch commands. Broader
locomotion, terrain, collision, and visual validation remain necessary.

The headless tests pass on vanilla and Rend2 for damage, projectile reactions,
get-ups, return to animation, slow time, save/load, renderer restart, removal,
and shutdown. The packaged demonstration launcher and its separate profile
also pass. The desktop updater suite contains 19 passing tests.

Frame sequences from the recordings show a foot placement followed by a
supported stance, a knee collapse after a stronger leg hit, and a running hit
followed by a grounded get-up. The running camera was moved back and forward
along the test lane to keep the fall in view. These checks do not establish
motion quality across the campaign maps.

## Sources

- [NaturalMotion interview, 2006](https://www.psu.com/news/psu-interviews-naturalmotion/)
- [GTA IV Euphoria announcement, 2007](https://www.gamedeveloper.com/game-platforms/product-i-grand-theft-auto-iv-i-using-naturalmotion-s-euphoria)
- [SIMBICON, SIGGRAPH 2007](https://www.cs.ubc.ca/~van/papers/simbicon.htm)
- [Watch Your Step, 2022](https://arxiv.org/html/2210.14730v1)
- [Jolt ragdoll interface](https://jrouwe.github.io/JoltPhysics/class_ragdoll.html)
- [Fix Your Timestep](https://gafferongames.com/post/fix_your_timestep/)
