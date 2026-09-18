# Sound Routing

## Routes

The mixer selects one route for each active source. Steam Audio must be enabled.
With `s_steamAudio 0`, all sources use the existing legacy mixer.

| Route | Direct sound | Indirect sound |
| --- | --- | --- |
| `full` | Steam Audio obstruction, transmission, and air absorption | Reflections and available propagation paths |
| `protected` | Game volume and distance falloff; no acoustic obstruction | Steam Audio reflections and room response |
| `legacy` | Existing legacy sample mixer | No added Steam Audio effects |

With `s_steamHeadphones 1` (the default), full and protected sources use HRTF.
This replaces direct stereo panning and decodes reflections for headphones.
Legacy sources do not use HRTF. With `s_steamHeadphones 0`, protected direct
sound uses the existing legacy sample mixer and stereo gains. The exact direct
sample comparisons below apply to speaker mode.

The protected direct signal bypasses acoustic obstruction, transmission, and
air-absorption filters. The SDK does not add a second direct signal or a diffracted
direct path. Its reflection input still receives the original source signal.
Protected sources use a normal reflection send of 1. `s_steamReverb` controls
wet gain. Full-route transient effects still use `s_steamTransientReverb`.

The final mix limiter remains active for all three routes while Steam Audio is
active. It can reduce direct level during loud combined events. Source distance,
output mode, channel limits, missing assets, and game activation rules still
apply. Protected routing cannot restore a source that the game did not submit.

## Classification

`shared/sound/audio_routes.inc` is the shared policy table. The engine and static
triage tool both read this table. Context rules take priority over asset names:

1. Local, global-voice, announcer, and music channels use `legacy`.
2. World-space voice channels use `protected`.
3. Weapon-channel sources use `full`, including weapons on brush-model entities.
4. Other brush-model sources, including moving doors and lifts, use `protected`.
5. Other sources use the first matching asset prefix.

Global non-voice sound events use a new automatic broadcast channel. It retains
legacy channel allocation so overlapping events do not replace each other.
Global ambient beds use the existing local-sound channel. Their direct playback
remains listener-attached; they do not acquire a room response. Radio dialogue is legacy-only when the game submits it
on a broadcast channel. A recording's filename alone cannot establish its role.

The initial asset rules are:

| Family | Route |
| --- | --- |
| Weapons, footsteps, body impacts, identified explosions and impacts, TIE pass effects | `full` |
| Character cues, dialogue, movers, switches, vehicles, world ambience | `protected` |
| Other environmental effects, including alarms, forcefields, steam, and machinery loops | `protected` |
| Interface and music assets | `legacy` |
| Unclassified assets | `protected`, with `unclassified-review` in diagnostics |

This policy is deliberately conservative for environmental sounds. It does not
claim that every ambient effect needs protected direct audio. Narrower rules can
be added after a controlled comparison. Rebuild the game after you edit the table.
Rules do not change cache geometry, so this policy change does not require a
probe bake or cache-version change.

The previous Kejim alarm transmission multiplier was removed. The alarm and its
existing relays now use protected direct audio. Relay placement was not changed.
The user's reverb and fly-by settings were not changed by this implementation.

## Diagnostics

Use `s_steam_status sources` to inspect `route` and `rule` beside the source's
entity, sound asset, position, channel, and gains. Reported occlusion values can
still be low for a protected source; those values do not filter its direct sound.

For a diagnostic comparison in a cheat-enabled map:

```text
s_steamRoute -1  // Automatic policy: normal playback
s_steamRoute 0   // Legacy-only world sources, with the Steam mixer clock retained
s_steamRoute 1   // Protected direct world sources plus reflections
s_steamRoute 2   // Full acoustic world sources
```

Local and broadcast channels always remain legacy-only. The override is not
archived. Changing it resets acoustic effect history. Return it to `-1` after a
test. For a complete legacy-backend comparison, use `s_steamAudio 0` instead.
`s_steam_emit SOUND global` tests the automatic broadcast channel.

## Inventory Triage

The previous inventories cover 34 JA maps and 26 JO maps. Reuse them with:

```sh
python3 scripts/triage-audio.py \
  --inventory build/audio-audit/ja-final-inventory.json \
  --output build/audio-routing/ja-triage.json
python3 scripts/triage-audio.py \
  --inventory build/audio-audit/jo-final-inventory.json \
  --output build/audio-routing/jo-triage.json
```

| Campaign | Full | Protected | Legacy | References for review |
| --- | ---: | ---: | ---: | ---: |
| JA | 56 | 5,708 | 1,403 | 20 |
| JO | 119 | 8,195 | 1,036 | 22 |

These are reference counts, not unique assets or active voices. Repeated map and
script references retain their context. Stop commands are excluded. Reports
contain the source archive, owner, inferred context, route, rule, and input hashes.
The review counts include missing and dynamic references. All resolved references
in these inventories have an explicit context or family rule.

Static scripts do not always identify the runtime speaker or channel. Dynamic
names and sounds generated by game code remain runtime work. The small full-route
counts do not represent combat sound usage: most weapon and footstep playback
comes from code, not static map entities.

## Tests and Limits

```sh
python3 scripts/test-triage-audio.py
python3 scripts/test-steam-audio-sp.py --routing --audit-freeze --flyby --burst
python3 scripts/test-steam-audio-sp.py --campaign jo --map kejim_post --alarm --burst
python3 scripts/test-audio-routing-movers.py
```

`tests/audio_routing.cpp` checks classification. `tests/steam_audio.cpp` checks
that protected processing leaves caller-supplied direct samples unchanged,
retains reflections, and adds no diffracted direct signal.

The mover fixture uses actual `kejim_post` entities: the large `t2` lift and the
vertical `tower_door`. It restores the same saved state for each route. It checks
source submission, direct level, clipping, listener motion, and stopped loops.
The baseline is legacy-only routing on the continuous Steam mixer clock.

In `build/audio-routing/movers.4loelaku`, full processing lost 10.55 dB on the
lift and 5.23 dB on the door relative to that baseline. Protected direct differed
by -0.06 dB and +0.08 dB. The wet mixes had no clipped samples, and both loops
stopped. This confirms those fixtures, not every lift, door, or mission state.

The baked 11-position alarm comparison in `build/audio-audit/measure.yejoo9x4`
had no strong-legacy loss candidate, no clipped samples, and a silent stopped
capture. With relays enabled, the two tested exterior positions gained about
6.8 dB over legacy. The panel gained 9.0 dB and still needs a balance check.

Selected losses from the previous audit were tested again:

| Case | Previous gain relative to legacy | Protected route, with reflections |
| --- | ---: | ---: |
| Taspir 2, entity 48, `offset-1-1` | -4.65 dB | +2.86 dB |
| Cairn Dock 1, entity 4, `offset-0--1` | -10.89 dB | +0.56 dB |
| Doom Shields, entity 25, `offset-1-1` | -12.51 dB | -0.51 dB |

These repeat samples are in `build/audio-audit/measure.32klt74u` and
`build/audio-audit/measure.v2lik_8w`. The positions are audit samples, not proof
of normal player-route coverage. Other emitters and mission states remain open.

Headless PCM tests do not establish desktop balance. Listen to dialogue through
walls, radio dialogue, the moving lift, doors, alarm coverage, and combat effects.
Check especially for excessive alarm level, unwanted room response on radio
speech, and voice masking during dense fire. Record the source's `route` and
`rule` when a category needs adjustment.

The SDK supports separate direct and reflection processing. See the
[Steam Audio integration guide](https://valvesoftware.github.io/steam-audio/doc/capi/integration.html).
