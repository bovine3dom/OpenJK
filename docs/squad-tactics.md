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

An unsupported or badly wounded NPC prefers retreat cover or a hold instead of
a new offensive flank. A nearby retreating or badly wounded teammate does not
count as ready combat support. Movement has a six-second limit, followed by a
three-second hold on arrival and a retry cooldown. Stale sight does not renew
regroup holds indefinitely; ordinary lost-contact behaviour can resume.

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

Sixteen cases pass: hidden-ally recruitment with unchanged personal sight, ignore
and no-group protections, completed concealed flanks in both commander modes,
wounded retreat, unsupported regrouping, support-loss cancellation, and tactical
save/load, plus death, timeout, cinematic interruption, contested reservation,
and save-reservation checks. The `cp-low` and `cp-high` cases check non-tactical
release, reuse, failed replacement, and save/load in both entity orders.
The tests check roles, positions, route separation from the threat, arrival, and
cleanup, not just assignment messages. The timeout case forces a deadline; it
does not physically block a route. The cinematic case simulates `BS_CINEMATIC`
and an external goal, not a full pending ICARUS script. The contested case uses
the reservation API, not a real encounter with multiple squads.

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
test invulnerability; `wound` reduces health; and `ignore`, `nogroups`, and
`dontflee` set the corresponding restrictions. These are test controls.

## Remaining Checks

Manual campaign tests must assess difficulty, retreat pacing, and bark clarity.
These checks remain deferred. Use the root `human_todo.md` Rend2 checklist.
Dedicated tests are still needed for larger recruitment chains, radius boundaries,
mixed teams, grenade preemption, named/locked-door permissions, and all authored
anger-script combinations. ST hidden-target decisions now use known positions,
and `NPC_StartFlee` retries keep the supplied danger point. Other NPC controllers,
generic callers, and full FOV and attention checks remain outside this scope.
The separate `SCF_NO_GROUPS` legacy formation controller still needs hearing,
steering, and chase corrections. This is not an engine-wide perception rewrite.
