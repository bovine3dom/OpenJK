# Local Squad Tactics

## Scope

Squad membership and local reports are available across NPC species and weapons
in Jedi Academy single-player. This includes snipers, Sith, and combat droids.
Team, target, local contact, and explicit script restrictions still apply.

Membership does not assign a movement controller. Supported gun users can take
cover, flank, and cover a retreat. Sith and other melee actors keep their own
combat behaviour while sharing contact reports. Snipers retain their firing
controller and use the common movement path for pressure retreats.

Jedi Academy saves use format version 3. Supported project v1 and v2 saves migrate
on load. The original file is not rewritten. See `save-migration.md`.
Jedi Outcast is unchanged.

## Local Reports

- A group evaluates recruitment at most once per second and makes at most two assignment attempts per pass.
- A caller must have a confirmed personal sight record that matches the group's latest record, no older than 1500 ms. Eligible observers take turns as callers.
- The caller searches at most 128 nearby entities, with a strict 512-unit radius.
- A common observation hook confirms sight for controllers outside trooper AI. A report source must have a matching confirmed-sight stamp; a controller's private sight timer alone is not sufficient.
- Both NPCs need a usable navigation connection. Beyond 256 units, they also need LOS to each other. Within 256 units, a short neighboring-node connection can substitute for LOS. This is a limited approximation of local hearing.
- The recipient need not see the Jedi. Delivery assigns awareness and joins the source group without changing the recipient's personal sight time or position.
- Receiving a report does not refresh its observation time. A recipient without personal sight cannot become a sight-report source.
- Ordinary team-alert recipients no longer trigger recursive anger alerts. This prevents an initial alert from propagating through an unrestricted chain of newly alerted NPCs.
- Group merges require the same team and local contact. Dead, confused, charmed, captured, frozen, ignored, locked-target, no-group, and scripted-navigation cases retain their restrictions.

There is no map-wide radio channel. Reports use shared sight memory, not the
hidden target's current coordinates. Existing damage, death, and sound awareness
remain separate from this new report path.

## Tactical Choices

New flank and exposure assignments require recent shared sight. Emergency cover
can use an older valid sight record. The policy uses these roles:

| Value | Role |
| --- | --- |
| 0 | No tactical assignment |
| 1 | Move to regroup cover |
| 2 | Hold after regrouping, or hold when no safe point is available |
| 3 | Move along a flank |
| 4 | Hold the reached flank position |
| 5 | Support a flank or a retreat |

A flank needs a healthy, armed, stationary supporter with a firing opportunity.
Only one flanker is assigned per group. The destination must be lateral to the
supporter's direction, reachable, clear of occupied geometry, and at least 128
units from the reported threat. Authored combat points are preferred. A bounded
search of up to 64 nearby ground waypoints supplies a fallback where maps have
few suitable combat points.

Flank and retreat pathfinding uses the fixed reported threat. Direct shortcuts
and path trimming cannot cut through its 128-unit danger radius when starting
outside it. An NPC already inside that radius can escape. The policy does not
read the player's camera to decide whether a flank is visible.

A badly wounded NPC prefers retreat cover or a hold instead of a new flank.
A healthy NPC without support uses the firing-and-cover cycle below.
A nearby retreating or badly wounded teammate does not
count as ready combat support. Movement has a six-second limit, followed by a
three-second hold on arrival and a retry cooldown. Stale sight does not renew
regroup holds indefinitely; ordinary lost-contact behaviour can resume.

Damage starts a three-second `underFire` timer. For a new assignment, recent
damage takes priority over a new flank, even above half health. The NPC searches
for retreat cover within 512 units. The search requires a route and blocked LOS
to the recorded threat. It does not remove the cover requirement if no point is
found. Without a suitable point, the NPC holds its current position.

At an assigned cover point, role 2 keeps the NPC crouched for the three-second
hold. The point is then released. The existing movement deadline, retry delay,
and script restrictions apply to normal regrouping. Damage does not supply a
new sight position. The hold-order override below also responds to damage.

Healthy stationary shooters also seek cover without damage. A firing opportunity
starts a 2.5-to-4-second exposure timer. Normal pauses between shots do not reset
it. Movement, loss of sight, or an incompatible order clears pending exposure.
After exposure, the NPC runs to checked cover within 384 units, crouches for
three seconds without firing, then steps out beside that cover. The firing step
is 48 to 144 units from the cover anchor. It needs a clear standing muzzle line,
a clear body route, and ground support. The NPC fires for one to 1.8 seconds,
then runs back to the same anchor. Incoming fire, low health, or a shot blocked
for 500 ms can end the firing interval early. Pressure delays the next peek.

A cover pair has a 15-second budget. Each movement has a six-second limit. When
the pair ends, the NPC can select a new firing position through the existing
cycle. Every 500 ms, the pair checks cover against the latest valid sight record.
An enemy within 128 units of the anchor, exposed cover, a changed threat during
movement, or a blocked return route requests a new cover search. Two blocked
firing intervals also request a new position.
The decisions use recorded threat positions. They do not track a hidden target.

The cycle checks an authored combat point, then up to 64 nearby graph positions
and 128 local floor samples. Each cover candidate must have a usable firing step.
Cover needs fixed world geometry at crouched and standing heights. Doors do not
qualify. Candidates must have a safe route and must not overlap reserved combat
points or active tactical destinations. A failed search has a four-second retry
delay. It does not force a cover hold in the open. Active flank and support roles
are not interrupted by exposure. Timers store the cycle phase. Format 3 also
stores the cover anchor. Other actors avoid the anchor while its owner peeks.

## Incoming Fire

Non-explosive linear missiles apply pressure along their actual movement segment.
An eligible NPC must already have an enemy. A hostile shot must pass within
112 units of its body center, with a clear local line between the segment and the
NPC. This includes nearby impacts. Walls can block the pressure signal. Friendly
shots, stationary missiles, and explosive missiles do not use this path.

Use `g_squadPressureRadius` to adjust the radius. Its default is `112`. The code
limits it to 0 through 256 units. Zero disables new pressure detection.

Pressure lasts 1.5 seconds and requests cover through a two-second under-fire
timer. Updates are limited to one per NPC per 300 ms. Pressure does not assign
an enemy or update sight times and positions. Existing grenade avoidance remains
separate.
Actual damage also enters the immediate pressure path through the general NPC
pain handler, including during a support role or movement cooldown.

Fresh pressure is checked on the actor's own combat update. It bypasses the
general movement and exposure retry delays. Exposed actors search authored
points, graph positions, and local floor samples for cover. An emergency retreat
does not require a future firing step. If cover is unavailable, a checked direct
move can gain distance instead. Failed searches wait 750 ms before retry.
Actors already retreating keep moving. Concealed actors crouch and delay their
next peek. Active scripted movement retains control.

`g_squadPressureOverrides` defaults to `1`. Pressure from shots, damage, or a
nearby saber can temporarily override no-chase, no-retreat, crouch, and walk orders. The NPC
can finish its bounded retreat after the pressure signal expires. It does not
start an offensive peek cycle while held. On arrival or cancellation, the
temporary permission is cleared. During movement, steering and command generation
request a standing run. The original script flags are never changed.
The NPC remains at its new position until another order or pressure event.

Set `g_squadPressureOverrides 0` to retain the original hold and gait orders.
Cinematics, pending scripted navigation, forced marches, explicit ICARUS freezes,
item goals, and incompatible NPC controllers retain their existing protections.
The override uses saved timers and does not change the save format.

At trace level 3, `pressure_cover` records a new destination. `pressure_response`
explains a hold, an existing retreat, a restriction, or a failed search. Level 4
also records `pressure_ignored reason=wall` for a blocked pressure signal.

## Close Saber Pressure

A ranged NPC also receives pressure when its visible enemy approaches with an
active, held lightsaber. `g_squadSaberPressureRadius` defaults to 192 units. The
code limits it to 0 through 512 units; zero disables new saber-pressure detection.
An inactive or thrown saber does not trigger this response. Unarmed actors and
actors with melee weapons are excluded. The existing squad eligibility and
script restrictions still apply.

The check runs after confirmed sight on the actor's combat update. It refreshes
the same short-lived pressure used for incoming shots. It does not obtain a new
position from a hidden enemy. Trace level 3 records `saber_pressure` events.

Within 128 units, the NPC first tries to gain distance directly. It also tries
this escape when no cover is available. The destination must gain at least
64 units of distance. The direct route must move away from the recorded threat,
fit the standing actor, and have ground support. This avoids a detour toward the
saber through a nearby navigation node. Movement retains the six-second limit.
An escape point is not treated as concealed cover; `pressure_cover escape=1`
identifies this fallback. The NPC can seek cover after gaining distance.

## Covering a Retreat

A nearby, healthy, stationary teammate can cover a retreat instead of starting
its own exposure move. The supporter needs a clear muzzle line and must not be
under pressure. Support ends when the retreat ends, support becomes unavailable,
or its deadline expires. Its deadline cannot exceed the mover's six-second limit.

A pressured flank supporter can withdraw. A ready teammate takes over if one is
available. Otherwise, the flank is cancelled. A direct pressure response can
also interrupt the flanker. These decisions use the existing role and timer
fields; the save format remains 3. `retreat_support` and `support_handoff` record
the assignments. Actors under pressure can all retreat if no suitable supporter
remains.

Autonomous role-1 movement requests running. Explicit script-walk orders remain
in control unless a script-run order has priority. Navigation can still slow an
NPC near obstacles or its destination. NPC speed settings are not increased.

Support loss, target changes, death, script interruption, and timeouts clear the
assignment. Tactic cancellation and group removal also clear `movementSpeech`
and its chance value. Combat-point release clears all NPC ownership claims, not
only tactical claims, before reuse. A failed replacement clears the NPC's point
ID. Full-save load restores combat-point occupancy from NPC claims; autosave load
does not. Scripted goals are not cleared as if they belonged to a tactic. Existing
grenade danger handling runs before tactical decisions can suppress it.

## Barks

Successful recruitment attempts a detected/contact call. The recipient queues a
single delayed acknowledgement attempt. A real flank assignment can request an
outflank bark; a real retreat movement can request a cover bark. The unrelated
legacy random outflank call is removed.

Normal speech cooldowns and script restrictions still apply. Missing or suppressed
audio does not prevent movement. Calls are not guaranteed for every manoeuvre,
and the headless tests do not establish audible quality or suitable clip wording.

## Testing

```bash
python3 scripts/test-squad-tactics.py
python3 scripts/test-squad-tactics.py --case flank-async
```

The existing cases pass: hidden-ally recruitment with unchanged personal sight, ignore
and no-group protections, completed concealed flanks in both commander modes,
wounded retreat, solo cover movement, support-loss cancellation, and tactical
save/load, plus death, timeout, cinematic interruption, contested reservation,
and save-reservation checks. The `cp-low` and `cp-high` cases check non-tactical
release, reuse, failed replacement, and save/load in both entity orders.
The tests check roles, positions, route separation from the threat, arrival, and
cleanup, not just assignment messages. The timeout case forces a deadline; it
does not physically block a route. The cinematic case simulates `BS_CINEMATIC`
and an external goal, not a full pending ICARUS script. The contested case uses
the reservation API, not a real encounter with multiple squads.

The suite has 57 cases. `contact-async` and
`contact-sync` apply damage through `G_Damage` while health stays above half.
They check movement into cover, physical arrival, crouched holds, and timed
reservation release. `contact-hold` checks the no-chase opt-out with overrides
disabled. These cases use normal maximum health and protect the actors
from other damage. They do not prove firing recovery or campaign combat quality.

`cycle-async` and `cycle-sync` check repeated local peeks without damage, quiet
crouched holds, returns to the same anchor, and attack commands. `cycle-cancel`
checks cleanup when tactics are disabled. Contact tests check the running
command and measured speed above walking speed. Attack commands do not prove
that projectiles hit their targets. Campaign combat quality needs manual tests.

`pressure` checks a real nearby missile, friendly and distant controls, decay,
and unchanged hidden-target memory. `peek-pressure` checks quick withdrawal
without damage. `peek-save` checks the anchor and active phase across save/load.
The new cases check:

- Immediate local cover through a forced ten-second cooldown in both commander modes. All authored points are reserved in this fixture.
- Detection with a 112-unit radius but not a 72-unit radius, plus shielding by map geometry with a 256-unit test radius.
- Firing support during a retreat, support replacement, and flank cancellation without a replacement.
- Distinct local destinations and arrivals for two pressured actors.
- Cover replacement after a confirmed approach, with no hidden-position update.
- Save/load during both the outward peek and the withdrawal. The fixture freezes each actor after the movement starts.
- Close-saber retreats through a forced cooldown in both commander modes, including a direct escape from inside 128 units.
- Gun, inactive-saber, distant-saber, disabled-radius, melee-actor, no-chase, and hidden-saber controls. The hidden control uses a 512-unit radius to check LOS independently of distance.
- Held actors retreat after a shot, damage, or saber pressure. A no-retreat order also yields. Tests check unchanged script flags and cleanup of temporary movement permission.
- Cinematic control remains protected. An active hold-order override survives save/load and clears after arrival.
- Native `t1_sour` mercenaries, Rodians, Trandoshans, Weequays, and a sniper move under pressure. These tests use the original spawners and scripts. See `encounter-tatooine.md`.
- Mixed trooper/Sith, Sith-only, trooper/sniper, and trooper/droid squads share reports. Membership does not assign trooper movement roles to Sith or droids.

General squad fixtures equip the player with a blaster. Saber-specific fixtures
select and activate the saber explicitly.
The isolated pressure-detection and saber-exclusion fixtures disable hold overrides
to keep their actors stationary.

For conflicting claims in older saves, reconstruction keeps the first valid
living owner in entity order.
The save does not identify which conflicting claimant was the original owner.

The fixtures clear native NPCs and use protected test actors. They run at a
controlled frame cap; do not execute them at arbitrary frame rates and treat the
results as a benchmark. Normal campaign play does not protect NPCs or increase
their health, damage, or movement speed.

`d_squadTactics` defaults to `1`. In a cheat-enabled session, set it to `0` to
disable tactical movement while retaining reports. The older memory and trace
suites do this to test those systems independently. Use `d_npcai 3` for report
deliveries, acknowledgement attempts, assignments, movement samples, and finishes.

`nav memory <name>` includes tactical role, CP, deadline, goal, and threat.
`nav contact <source-name> <recipient-name>` checks local report eligibility.
For `_memory_` test actors, `fight` permits movement and firing; `protect` enables
test invulnerability; `hit` applies five damage through the damage handler;
`wound` reduces health; and `ignore`, `nogroups`, and
`dontflee` set the corresponding restrictions. These are test controls.

## Remaining Checks

Contact tests are still needed for repeated hits, expired damage records, no
available cover, and damage during active flank or support roles. Add
pending-exposure interruption, larger multi-squad encounters, and explicit
script-walk tests. Animated leaning is not implemented. Blocked-shot
posture recovery and friendly-shot rejection need dedicated runtime tests.

Manual campaign tests must assess difficulty, retreat pacing, and bark clarity.
These checks remain deferred. Use the root `human_todo.md` Rend2 checklist.
Dedicated tests are still needed for larger recruitment chains, radius boundaries,
mixed teams, grenade preemption, named/locked-door permissions, and all authored
anger-script combinations. ST hidden-target decisions now use known positions,
and `NPC_StartFlee` retries keep the supplied danger point. Other NPC controllers,
generic callers, and full FOV and attention checks remain outside this scope.
The separate `SCF_NO_GROUPS` legacy formation controller still needs hearing,
steering, and chase corrections. This is not an engine-wide perception rewrite.
