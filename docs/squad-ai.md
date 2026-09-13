# Squad AI Diagnostics

This first increment adds diagnostics to Jedi Academy single-player only.
It does not change gameplay, add roles, or enable `ST_GetCPFlags`.

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
- `lost_track` shows the action, contact age in milliseconds, position, and
  position source. `enemy_current` is the live enemy position, not a stored
  last-seen position. Repeated track actions use DETAIL; dissolution uses INFO.
- `cp_request` shows decimal flags before the search. `cp_result` shows flags
  after the retry search can change them. `cp=-1` means no point was found.
  `source=self_current` identifies all three position arguments to this search.
- `cp_reserve` and `cp_release` mark changes to the occupied flag, not attempts.
  They do not identify an owner. These functions have no owner argument.
  `failed` is the release argument. Existing bounds checks are unchanged.
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
No new random calls, navigation searches, timers, or persistent fields are added.

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
