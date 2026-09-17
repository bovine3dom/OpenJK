# Jolt Reactive Animation

Reactions are enabled by default (`g_joltReactions 1`). Automatic diagnostic
messages and skeleton lines are disabled by default (`g_joltDebug 0`).
Use `g_joltDebug 1` to enable diagnostics. Explicit status commands still print
their results. See [Performance Check](jolt-performance.md) for measurements.

## Force Effects

Grip uses the native targeting, resistance, damage, and release rules.
Level 1 restrains the neck near its initial position with a bounded spring.
It does not raise the target or cancel gravity. Levels 2 and 3 suspend the body through a bounded
force at the upper torso. The hands follow a choking pose. Suspended legs keep
the passive joint damping, friction, and limits of a free ragdoll. Living hips
and knees also have gentle muscle tone toward a near-vertical posture with a
small knee bend. The added torque is limited to 5 N m at each hip and 3 N m at
each knee. It damps swinging without locking axial twist. Ankles stay passive.
Small, intermittent thigh impulses make the legs struggle. The added muscle
tone ends on release or death. Level 3 follows the native carry
target. Release preserves physical velocity and applies the native speed limit.
The controller waits for the native recovery delay before a get-up.

Lifted targets turn toward the caster through a bounded yaw torque. Angular
damping removes spin, and the target turn rate is limited. The equilibrium
follows the caster's position. Release removes this turning torque. The facing
reference uses shoulder and spine positions, including for restored corpses.

Lightning reactions start only after accepted health damage. Repeated hits
refresh one exposure state. Contractions become stronger at higher power levels
and retain motor strength during a fall. Contractions fade over approximately
one second after the last hit. Native damage remains in use. Physical targets
receive continuous horizontal acceleration, without repeated upward kicks.
The default `g_joltLightningPushScale 0.5` applies the nominal horizontal Force
Push velocity change over two seconds of exposure at the same level, distance,
and target mass. Contacts and drag affect the resulting motion. Weak or
partially resisted hits reduce the acceleration. Values from zero to one allow
further tuning. Push stops when the caster's power ends. A contact grace period
of 0.3 seconds covers slow server updates and ends push when the beam misses.
Push does not continue during the contraction after-effect. Unsupported targets retain native knockback.
Death ends the contractions and
muscle control. A Grip attachment remains active while the native power holds
the corpse. Release then lets the passive body fall.

Free falls and corpses have modest damping between connected bones. It slows
rapid folding without a rest-pose target. The torso has the most resistance;
the ankles have the least. Equal and opposite torques preserve total angular
momentum. Grip uses this resistance in the suspended legs and in held corpses.
Its active upper body, Lightning contractions, and catching arms retain their
own control. Sleeping bodies do not receive these torques.

The controller has separate strength settings for torso, head, arms, legs, and
feet. Strength changes are gradual. Grip and Lightning use the same rig and the
existing active-body budget. Unsupported actors use native behaviour.

Run `scripts/test-jolt-sp.py --force-effects --record` to check the native powers
and record their motion. The test covers Grip levels, carrying, release,
disable/re-enable, save/load, death, Lightning expiry, and protected targets.

Saber reactions remain deferred. See [Physical Melee Reactions](jolt-melee-plan.md).

## Start the Demonstration

On the configured desktop, run:

```sh
openjk-play --worktree rmlui --desktop --jolt-demo
```

This command downloads the published package. It starts `t1_sour` with a separate
`jolt-demo` profile. The demonstration finds a clear area, places a stormtrooper,
and sets a side view. It repeats five cases: a small hit, a corrective step,
a leg hit, a running fall, and a standing fall with a get-up.

From the source worktree, use the local package:

```sh
bash scripts/jolt-demo.sh --desktop
```

Set `OJK_ASSETS` if the Academy assets are not in `GameData`. Set `OJK_PACKAGE`
to use a package other than `build/ready`. The package also contains
`jolt-demo.sh`. The script uses the same separate demonstration profile.

| Key | Action |
| --- | --- |
| F5 | Repeat the small-hit case |
| F6 | Repeat the step case |
| F7 | Repeat the leg-hit case |
| F8 | Repeat the running-fall case |
| F9 | Repeat the standing-fall case |
| F10 | Reset to a standing actor |
| F11 | Switch between normal speed and quarter speed |
| F12 | Stop the sequence and release movement control |

Each case starts with a new actor. The test actor has 500 health. Standing
cases use a fixed weapon-idle frame so that you can compare repeated runs.
The running case uses normal NPC movement and a nonfatal leg hit before the
physical fall.

To start the same test from the game console, enter `exec jolt-demo.cfg`.
This loads the map and sets the keys above in the current profile. Use the
launcher option for a separate profile. On the loaded map, use
`jolt_demo all`, or select `idle`, `hit`, `step`, `leg`, `run`, `fall`, `reset`,
or `stop`.

### What to Check

- The idle actor must remain on its feet.
- The small hit must produce clear recoil without a launch.
- The step case must lift a foot, place it on the floor, and settle into a
  supported stance. A step attempt alone is not a successful recovery.
- The leg case must show leg withdrawal or a support correction. A failed
  correction can become a fall.
- The running actor must carry its movement into the fall.
- During a fall, reachable surfaces must produce separate arm reaches. Hand
  contact must bend the arms as the body settles. If no surface is reachable,
  the arms use a protective pose.
- The get-up must start from the grounded pose. The body must not move to a
  distant standing point or rise before the get-up clip starts.

Use `jolt_status` for measurements. `corrections` counts step attempts;
`landings` counts confirmed foot placements. `phase` is 0 for animation
tracking, 1 for active balance, 2 for a step, and 3 for a fall. `engaged` shows
whether physics controls the pose. Phase 4 is physical get-up preparation;
phase 5 is a passive corpse.
`brace_mask` and `hand_contacts` use 1 for the left hand, 2 for the right hand,
and 3 for both hands. `preparing` reports the preparation stage; `blend_ms` reports the
duration of the final handoff. `recovery_lift` is the initial get-up
correction in game units. Its limit is eight units.
`handoff_error` measures the first native get-up frame against the prepared
pose, in game units. `corpse`, `sleeping`, `active_bodies`, and `body_limit`
report physical ownership and the simulation budget.

## Normal Play and Manual Controls

For normal JO gameplay, use the imported campaign profile:

```sh
openjk-play --worktree rmlui --campaign jo --desktop +set g_joltReactions 1
```

The JO combat stormtroopers use the validated stormtrooper model path. The
campaign tests check a real projectile hit and a physical fall on Kejim's
original `st_guard2`, plus save/load and the transition to `kejim_base`.

`g_joltReactions` defaults to `1` and is saved in the configuration. Reactions
start automatically on validated humanoid NPC rigs in JA and JO. This includes
troopers, Imperials, Jedi, Reborn, human allies, and humanoid aliens. Bone
aliases also support armoured troopers and humanoid droids. The rig must have
the required landmarks and valid dimensions. Vehicles and unrelated creature
skeletons retain their native handling. The first-person player is not driven
by this NPC controller.

Set `g_joltReactions 0` to stop new reactions and release living actors.
Existing physical corpses remain passive until normal corpse removal.

Supported projectiles are blaster, Bryar, bowcaster, repeater primary,
flechette primary, emplaced, and seeker bolts. Thermal detonators, rockets,
detpacks, trip mines, explosive alternate fire, and world explosions can cause
physical falls. Native damage supplies blast distance, cover, armour, team
protection, and knockback. The controller consumes that knockback once.

Damage, death scripts, kill counts, sounds, and item drops use the game rules.
A lethal supported hit can start a physical corpse. An upright actor loses
motor strength over 0.45 seconds. The targets lower the hips and bend the torso
during this interval. Torso contact ends motor control early. An actor already
on the floor becomes passive immediately. Death preserves the initial pose and
velocity. Native disintegration retains its special handling.
Low health reduces baseline support by at most 15 percent. Recent hits add
up to 12 percent temporary weakness. This weakness decreases with time.
Grounded physical NPCs qualify for the saber floor attack. The recovery
controller obeys the attack's get-up delay. Movement traces ignore grounded
physical bodies, but weapon traces keep their full hit bounds. Recovery checks
for an occupied standing hull before it releases physical control.

Use `--floor-combat` with `scripts/test-jolt-sp.py` to check movement across
fallen NPCs, a lethal saber floor attack, and movement across a sleeping corpse.
Use `--collapse --record` to record a standing death and check the strength fade.
The projectile comparison found the same health loss with reactions on and off.
The thermal blast comparison also passed. These checks measure damage from
accepted hits; they do not measure how easy a moving target is to hit.
Ordinary saber attacks keep their authored movement; saber users can still
be knocked down by explosions or eligible knockdown requests.

Cheat-enabled maps also provide these commands:

```text
jolt_select nearest
jolt_balance
jolt_hit front
jolt_hit legleft
jolt_push front 1.3
jolt_knockdown
jolt_blast jolt_demo_actor
jolt_status all
```

- `jolt_select` selects the actor under the crosshair. It also accepts `nearest`
  or a unique NPC target name. Selection resets that actor's reaction record.
  Use `jolt_select none` to release the debug selection and permit a normal
  return to animation.
- `jolt_balance` starts active control without an impact.
- `jolt_hit` accepts `front`, `back`, `left`, `right`, `head`, `legleft`, or
  `legright`, followed by an optional damage value from 1 to 50. The default
  is five points of blaster damage through `G_Damage`. The leg demonstration
  uses 15. Location multipliers apply. These hits can kill a normal actor.
- `jolt_impulse left` starts a hit response without health damage. It also
  accepts `right`; the default direction is forward.
- `jolt_push` changes body velocity without health damage. It accepts `front`,
  `back`, `left`, or `right`, and a speed change from 0.1 to 2 metres per second.
  Set `g_joltDemoPush` to change the push used by the demonstration. Its default
  is 1.3. A stronger push can cause a fall.
- `jolt_control` lets the movement keys control the selected NPC for 30 seconds.
  Use `exitview` to release it. A fall also releases control.
- `jolt_shoot <targetname> [alt]` fires a real blaster missile toward the actor.
  Walls, other actors, and normal damage rules apply.
- `jolt_blast <targetname>` detonates a thermal near a named NPC. It uses the
  normal thermal explosion callback, including radius damage and cover checks.
- `g_joltDebug 1` shows physical joint lines. Value `2` also prints the captured
  rig. `g_joltReactionPose 0` suppresses hit presentation before full-body
  engagement for an animation-only comparison.

## Controller and Ownership

The full-body rig has eleven capsules and two foot boxes. It follows the
animation before engagement. The same bodies then perform recoil, balance
corrections, and falls. Engagement retains their velocities. The controller
fits joint anchors and limit frames to the current pose before it enables
motors. It does not add a second launch impulse when balance fails.

The step solver retains the knee bend direction and leg twist from the
reference pose. It updates the landing point during the swing. After foot
contact, it blends toward a balanced stance and permits time for load transfer.
Foot boxes sit below the ankle, at the sole. A short contact delay prevents a
single missed contact report from starting a new step.

The controller still uses bounded external assistance: up to **220 N of
horizontal root force** and **80 N m of root torque**. Actual foot contact is
required. The assistance stops during a fall and never supplies upward root
force. `assist_force` and `assist_torque` report the current values. This is a
controller aid, not a claim of fully muscle-driven balance.

The default budget is **10 active rigs**, with up to 64 reaction records.
Use `set g_joltMaxBodies 5` or `set g_joltMaxBodies 10` to change the budget;
the accepted range is 1–16. Sleeping corpses do not consume an active slot.
Unused animation-tracking rigs can yield their slots to new impacts.
Solver scratch memory and collision shapes are shared across the rigs.
If the active budget is full, a new actor can use the smaller torso/head
reaction. Selected actors retain active control for tests.

Jolt uses game gravity and 120 Hz simulation steps. A bounded pose history
retains complete server intervals. The display samples one server interval
behind the simulation. Gameplay restores the authoritative pose before its
queries. Both SP renderers use the same full bone transforms.

Collision includes BSP brushes, patches, and moving brush models. Collision
shapes are cached. Other characters, dynamic non-brush props, limb self-collision,
and Sub-BSP terrain are not included. Joint limits still need anatomical
calibration. General locomotion, obstacles, stairs, ledges, and contact-driven
get-ups need further work.

Recovery uses torso stability and ground contact instead of waiting for every
limb to stop. A supported body can start preparation after 180 ms of stability,
at least 650 ms after the fall starts. Standing clearance is still required.
The system
fits the first frame of one of five get-up clips to the settled pelvis, with
extra weight on arm alignment. The arms first move toward their preparation
positions under motor control. Gravity and collision remain active, and no
root assistance is used. A new hit can interrupt this stage.

The system then fits the clip again and checks the swept arm landmarks against
world collision. An eased transform blend connects the prepared pose to the
fixed first frame. Its duration limits peak bone translation to 0.75 m/s and
rotation to 150 degrees/s. It lasts at least 350 ms. The authored rise starts
after this blend. If clearance or alignment is not suitable, the actor stays
down and retries. The final blend and rise remain animation transitions.
A normal standing return still uses 180 ms and reports `recovery_clip=-1`.
The handoff clears old animation tracks and Ghoul2 smoothing history before
the authored rise. This prevents a cached standing pose from appearing briefly.

Fall bracing uses shoulder motion, gravity, and surface probes to estimate when
an arm can reach an impact surface. Each arm has a separate target ahead of
the head's projected impact point. Stiffer elbow motors help absorb the fall.
The reach
moves with the falling body until contact. Small palm collision proxies are
enabled for falls and preparation, with the existing forearm mass and inertia.
Joint torques and compliant elbows supply the response; no new root impulse
is added. Bracing can reduce an impact, but does not guarantee that the actor
will catch itself.

Removal and external scripted pose control release the transient rig. A save
during a living actor's fall restores a normal navigation pose and hull.
Corpse bone poses use the existing Ghoul2 save data. Loading restores them as
passive physical bodies; it does not restart a death animation. Native corpse
cleanup and scripted death callbacks continue to run.

## Build and Automated Tests

Use one build job and matching engine, game, and renderer modules:

```sh
bash scripts/build-sp.sh
cmake --build build/sp --target jolt-reaction-test --parallel 1
build/sp/jolt-reaction-test
python3 scripts/test-jolt-sp.py --renderer rdsp-vanilla --control
python3 scripts/test-jolt-sp.py --renderer rdsp-rend2 --control
python3 scripts/test-jolt-sp.py --renderer rdsp-vanilla --demo
python3 scripts/test-jolt-sp.py --renderer rdsp-rend2 --demo --fps 120
python3 scripts/test-jolt-sp.py --renderer rdsp-vanilla --demo --fps 144
python3 scripts/test-jolt-sp.py --renderer rdsp-vanilla --projectiles
python3 scripts/test-jolt-sp.py --renderer rdsp-rend2 --projectiles
python3 scripts/test-jolt-sp.py --renderer rdsp-vanilla --gameplay
python3 scripts/test-jolt-sp.py --renderer rdsp-rend2 --gameplay
python3 scripts/test-jolt-sp.py --renderer rdsp-vanilla
python3 scripts/test-jolt-sp.py --renderer rdsp-rend2
python3 scripts/test-jo-sp.py --jolt --renderer rdsp-vanilla
python3 scripts/test-jo-sp.py --jolt --renderer rdsp-rend2
```

The game tests use an isolated profile and Xvfb. Logs, screenshots, and demo
measurements are under `build/jolt-tests/`. Use `--demo-case step` to test one
case, `--push-speed 1.3` to set its push, or `--debug` to show the skeleton.
Add `--launcher` to test the packaged launcher and configuration. Add `--record`
to save an Xvfb recording as `motion.mp4`; this option requires `ffmpeg`.
The frame-rate options set display limits; software rendering can run below
those limits. Solver tests compare all bone transforms at 60, 120, and 144 Hz
with a 120 Hz reference and a 20 Hz server update schedule.
The gameplay test covers ten simultaneous rigs, several humanoid families,
thermal damage parity, death continuity, and corpse save/load. Use `--rig-types`
with that test to supply another list of NPC types.

Jolt 5.3.0 is pinned, linked statically, and packaged with its MIT licence.
`-DUseJoltReactions=OFF` removes the dependency and game hooks. The SP game API
is 13 and the renderer API is 23. See
[Reactive Character Control](reactive-animation-research.md) for the research
and the remaining validation work.
