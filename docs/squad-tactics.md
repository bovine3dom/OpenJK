# Local Squad Tactics

## Scope

This first policy adds local recruitment, supported flanking, and regrouping to
Jedi Academy single-player. It applies to eligible stormtroopers, swamptroopers,
imperials, rebels, commandos, and Bespin cops. Bosses, Force users, incompatible
weapons, and script-controlled actors are excluded. It is not a replacement for
every NPC behaviour.

Jedi Academy saves now use format version 2. Supported pre-tactics v1 saves migrate
on load, with the new tactical fields initialized. The original file is not
rewritten. See `save-migration.md`. Jedi Outcast is unchanged.

## Local Reports

- A group evaluates recruitment at most once per second and makes at most two assignment attempts per pass.
- A caller must have a confirmed personal sight record that matches the group's latest record, no older than 1500 ms. Eligible observers take turns as callers.
- The caller searches at most 128 nearby entities, with a strict 512-unit radius.
- Both NPCs need a usable navigation connection. Beyond 256 units, they also need LOS to each other. Within 256 units, a short neighboring-node connection can substitute for LOS. This is a limited approximation of local hearing.
- The recipient need not see the Jedi. Delivery assigns awareness and joins the source group without changing the recipient's personal sight time or position.
- Receiving a report does not refresh its observation time. A recipient without personal sight cannot become a sight-report source.
- Ordinary team-alert recipients no longer trigger recursive anger alerts. This prevents an initial alert from propagating through an unrestricted chain of newly alerted NPCs.
- Group merges require the same team and local contact. Dead, confused, charmed, captured, frozen, ignored, locked-target, no-group, and scripted-navigation cases retain their restrictions.

There is no map-wide radio channel. Reports use shared sight memory, not the
hidden target's current coordinates. Existing damage, death, and sound awareness
remain separate from this new report path.

## Tactical Choices

New assignments require recent shared sight. The policy uses these roles:

| Value | Role |
| --- | --- |
| 0 | No tactical assignment |
| 1 | Move to regroup cover |
| 2 | Hold after regrouping, or hold when no safe point is available |
| 3 | Move along a flank |
| 4 | Hold the reached flank position |
| 5 | Stay in support of a flanker |

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
script restrictions, and `SCF_DONT_FLEE` restriction still apply. Damage does not
supply a new sight position. It does not interrupt an active tactical role or
bypass its retry delay. No new save fields are required.

Healthy stationary shooters also seek cover without damage. A firing opportunity
starts a 2.5-to-4-second exposure timer. Normal pauses between shots do not reset
it. Movement, loss of sight, or an incompatible order clears pending exposure.
After exposure, the NPC runs to checked cover within 384 units, crouches for
three seconds without firing, then moves to a checked firing position. Each
movement has a six-second limit. The return uses the recorded threat, not a
hidden target's current position. Normal firing can resume after arrival.

The cycle checks an authored combat point, then up to 64 nearby graph positions.
Cover needs fixed world geometry at crouched and standing heights. Doors do not
qualify. Candidates must have a safe route and must not overlap reserved combat
points or active tactical destinations. A failed search has a four-second retry
delay. It does not force a cover hold in the open. Active flank and support roles
are not interrupted by exposure. Existing timers store the cycle phase in saves.

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

The suite has 22 cases. `contact-async` and
`contact-sync` apply damage through `G_Damage` while health stays above half.
They check movement into cover, physical arrival, crouched holds, and timed
reservation release. `contact-hold` checks that damage does not override a
no-chase order. These cases use normal maximum health and protect the actors
from other damage. They do not prove firing recovery or campaign combat quality.

`cycle-async` and `cycle-sync` check two complete cycles without damage, quiet
crouched holds, return arrivals, and renewed attack commands. `cycle-cancel`
checks cleanup when tactics are disabled. Contact tests check the running
command and measured speed above walking speed. Attack commands do not prove
that projectiles hit their targets. Campaign combat quality needs manual tests.

Rend2 v1-to-v2 save migration, renderer lifecycle, and an autosave load also pass.
These changes do not change the save layout. For conflicting claims in older
saves, reconstruction keeps the first valid living owner in entity order.
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
available cover, and damage during active flank or support roles. Add cycle
save/load, pending-exposure interruption, contested graph destinations, and
explicit script-walk tests. Wall-edge leaning is not implemented. Blocked-shot
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
