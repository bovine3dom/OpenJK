# Squad AI and Memory

These changes apply to Jedi Academy single-player. Squad diagnostics are joined
by a bounded lost-contact memory fix. No tactical roles are added, and
`ST_GetCPFlags` remains disabled.

## Sight Memory

- Confirmed geometric line of sight records time and position together. A blocked shot does not prevent this sight update.
- PVS membership alone does not refresh sight time. PVS is a visibility-region test, not proof that the NPC can see the player.
- New combat groups require a confirmed sight record. Assigning an unseen enemy does not invent one.
- Existing members retain their group after losing individual sight. A member that changes targets leaves the old group before regrouping.
- A sight update from one member updates shared group memory, not the other members' personal sight records. Same-target group merges preserve the newer paired record.
- After seven seconds without group sight, eligible commander-controlled members track the recorded position. Invalid or unreachable records cause a hold rather than a live-target fallback.
- After three minutes, the commander starts search from the recorded location's navigation node, or a member-local fallback. It does not write the target's waypoint in this transition.
- Scripted navigation, no-chase orders, and locked enemies retain their protections.

The fix also clears a released combat-point assignment during tracking and rejects
out-of-range combat-point IDs. This prevents repeated tracking from releasing a
point that another member has since reserved.

## Controls

- Set `d_npcai 0` to disable these records. This is the default.
- Set `d_npcai 3` for `DEBUG_LEVEL_INFO`: group changes, state changes,
  combat-point (CP) requests and results, CP reservations and releases,
  movement speech, failed moves, completed goals, and voice dispatch.
- Set `d_npcai 4` for `DEBUG_LEVEL_DETAIL`. This also shows commander passes,
  skip reasons, lost-contact tracking, speech suppression, and voice requests.
- `d_npcai` is a cheat cvar. Use a cheat-enabled test session.
- `d_asynchronousGroupAI` is an existing cheat cvar with a default of `1`.
  It selects one group member per commander pass. A value of `0` selects all
  members. Keep this value fixed when you compare traces.

## Trace Records

`Debug_Printf` adds color and the level time in milliseconds. Each new record
starts with `squad event=`. Other diagnostics can use the same cvar.

- `group` is the index in `level.groups`; `-1` means no group. `ent`, `enemy`,
  `speaker`, and `goal` are entity numbers. A missing goal is `-1`.
  Group and entity slots can be reused. They are not permanent identities.
- `commander_pass` separates the calling entity from the ranked commander and
  shows the active member index before selection. Passes and
  skips can repeat. There is no record for each successful movement tick.
- `group_insert` and `group_delete` mark membership changes. `state_change`
  marks a changed value in `AI_GroupUpdateSquadstates`, not each call.
  Direct state assignments elsewhere are not covered.
- `lost_track` shows the action, contact age in milliseconds, and position source.
  `group_last_seen` identifies shared sight memory. Tracking and hold records use
  DETAIL; dissolution uses INFO.
- `cp_request` shows decimal flags before the search. `cp_result` shows flags
  after the retry search can change them. `cp=-1` means no point was found.
  `source=self_current` identifies all three position arguments to this search.
- `cp_reserve` and `cp_release` mark changes to the occupied flag, not attempts.
  They do not identify an owner. These functions have no owner argument.
  `failed` is the release argument. Invalid IDs are rejected before array access.
- `movement_speech_store` shows the previous pending speech value. Consumption
  selects a speaker and clears the pending request, even after a failed move.
  `speech` is an ST speech type; `voice` is a voice event number.
  `chance` is the existing failure probability, not the success probability.
- Speech can stop before a voice request. DETAIL shows these suppression paths.
  `voice_request` means entry to `G_AddVoiceEvent`. `voice_suppress` gives the
  reason for a rejected request. `voice_dispatch` means `G_SpeechEvent` returned.
  Dispatch does not prove that a sound was found, played, or audible.
- `goal_complete` covers arrival or the existing stop-at-line-of-sight condition.
  It does not prove arrival at the exact goal position.

## Existing Limits

Squad states are existing movement and combat states, not tactical roles.
The dormant `ST_GetCPFlags` policy is unsafe to enable without a separate review.
These records do not validate that policy or add a squad plan.
Shared group, CP, and voice hooks can report NPCs outside stormtrooper groups.
The diagnostic records do not add random calls. Memory uses the existing saved
fields; no new serialized fields are added. Start fresh when comparing behaviour,
because older saves can contain inconsistent sight records.

This is not an engine-wide removal of hidden-target knowledge. Existing PVS-facing
and aim logic, short-loss combat distance decisions, `SCF_NO_GROUPS` pursuit,
group path-cost sorting, and generic flee behaviour still use live positions.
Geometric LOS is not a complete model of FOV, attention, or reaction time. Those
remain separate work items.

## Headless Check

After packaging, run `bash scripts/test-squad-sp.sh` from the repository root.
This checks trace levels 0, 3, and 4 in separate sessions. The fixture loads
`t1_sour`, skips the opening scene, enables player invulnerability, and spawns
three stormtroopers. Existing allies can kill these enemies. This is a diagnostic
fixture, not a balanced encounter or proof of coordinated flanking.

The check requires group and voice events, with additional commander and
combat-point search records at level 4. It does not require exact event counts or
timing. It does not yet test every suppression, movement, or reservation path.
Audio is disabled, so clip selection and audibility still need a later test.

## Memory Tests

```bash
python3 scripts/test-ai-memory.py
python3 scripts/test-ai-memory.py --case shared-async
```

Seven cases pass: single-member loss/reacquisition in both commander modes,
unseen enemy assignment, search expiry, shared observations in both modes, and
target switching. The tests check simulation timestamps, positions, group
identity, and movement goals, not just event presence.

The `ai-memory*.cfg` fixtures use the cleared Kril'dor room. They spawn named
stormtroopers, suppress firing, and hold their movement while normal combat AI
continues to evaluate sight. NPCs are paused briefly during player teleports so
that position and model-pose updates settle before visibility is sampled. The
expiry case advances simulation time with NPCs paused and rendering disabled,
then restores both before testing the three-minute transition. It is not a
three-minute campaign endurance test. Each run uses a fresh profile.

The cheat command below prints a read-only snapshot of a uniquely named NPC:

```text
nav memory <targetname>
```

Snapshots include personal and shared sight records, current target position,
geometric LOS, PVS, clear-shot time, and current goal. The live target position is
diagnostic evidence; printing it does not update sight memory.

For test NPC names beginning with `_memory_`, optional controls are available:

- `hold`: stop autonomous chasing, clear movement goals, and suppress firing without freezing perception.
- `chase`: permit chasing again; firing remains suppressed.
- `enemy [targetname]`: use the ordinary enemy-assignment function. Without a name, the target is the player. This does not force a sight record.

Controls refuse pending scripted movement. Do not use these fixtures as ordinary
campaign sessions. Rejected alert acquisition, blocked-shot sight, no-route holds,
merge ordering, and competing combat-point reuse still need dedicated runtime
cases. The broader campaign also needs manual regression testing.
