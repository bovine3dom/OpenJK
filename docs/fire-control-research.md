# Fire Control Research

## Sources

- [FM 23-14, chapter 6](https://www.globalsecurity.org/military/library/policy/army/fm/23-14/fm231_7.htm), inspected directly through the GlobalSecurity mirror. The manual distinguishes sustained, rapid, and cyclic fire. It describes controlled bursts, selected aiming points, observation, and changes in fire as the situation changes. Cyclic speed is not a normal sustained firing schedule.
- [Army M110A1 role description](https://cpeground.army.mil/Equipment/Equipment-Portfolio/PM-SL-Portfolio/M110A1-Squad-Designated-Marksman-Rifle/). Direct access returned HTTP 403. A Kagi-generated summary describes a semi-automatic weapon for direct-line-of-sight precision fire. This is secondary evidence from the summary, not a directly inspected quotation.

## Game Design

Use the differences between roles. The sources do not establish a universal
rounds-per-minute value for every trooper, sharpshooter, or automatic-weapon user.
Jedi Academy also has different projectiles, encounter distances, and weapon
delays. The firing intervals in this project are game settings. They are not
real-world firing instructions or reproduced weapon specifications.

- Standard gun users: short bursts with a clear pause to observe and aim again.
- Sharpshooters: deliberate shots at visible targets. Do not assign blind area fire.
- Automatic-weapon users: longer bursts with shorter pauses while supporting movement.
- Suppression: select a bounded area from recent contact. Use the last-seen point and nearby visible navigation points as possible exits. These are hypotheses, not new sightings.
- End suppression when the contact record expires, the firing line is blocked, or an ally crosses it. Do not infer target position from the player's camera or hidden current coordinates.

## Code Findings

`ShootThink` counts attack requests as burst shots. The weapon code can reject a
request while its mechanical delay is active. Thus the old burst counter does
not reliably count projectiles or hitscan shots.

The old trooper suppression path also requires a moving squad member and a random
per-update check. Its desired aim can then be replaced by the movement-facing
code. These conditions can make suppression rare and inconsistent.

Native hold scripts prevent offensive movement. Existing pressure overrides only
react after damage, nearby shots, or a close saber. A separate, bounded response
to visible contact can make defensive movement start earlier while preserving
active script navigation and cinematic control.

## Implemented Game Intervals

`g_squadFireControl` defaults to `1`. Eligible blaster and pistol users have the
trooper profile. Disruptor users have the sharpshooter profile. Primary-fire
repeater users have the automatic-weapon profile. Other controllers and
script-forced fire retain their existing schedules.

| Profile | Shots per burst | Minimum interval within a burst | Pause after a burst |
| --- | --- | --- | --- |
| Trooper | 3 | 250 ms | 1000–1150 ms |
| Sharpshooter | 1 | Not applicable | 1800–1950 ms |
| Automatic weapon | 6 | 140 ms | 750–900 ms |

An active trooper or automatic-weapon supporter reduces the pause by 200 ms.
The code counts actual weapon releases. Native weapon delays, aiming, pain, and
blocked shots can make intervals longer. These are minimum intervals, not a
guaranteed rounds-per-minute output. Player weapon rates are unchanged.

Troopers can suppress for up to 2500 ms after the latest sight record. Automatic
weapon users have a 4500 ms limit. Sharpshooters do not use this new suppression
path. Candidate points stay within 160 game units of the record. A raised aiming
point and a clear muzzle trace are required. The aim must settle before firing.
The weapon release code also checks for teammates in the firing line.

The three role tests count real releases, burst pauses, and minimum intervals.
They also check suppression, expired contact, and unchanged hidden-target memory.
Manual play must still assess difficulty and whether the pauses are clear.
