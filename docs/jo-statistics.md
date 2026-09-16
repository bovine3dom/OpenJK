# JO Mission Statistics

JO now shows completion statistics on its loading screen. The panel uses the
shared statistics counters, fonts, and renderer interface.
The panel waits for a click or Enter. At a true mission boundary, statistics and
mission preparation appear before the next map loads. Other displayed transitions
hold the next map paused after loading. Holding a movement or attack button does
not dismiss the panel. See `jo-mission-preparation.md` for mission groups.

## Display Rules

- A normal `target_level_change` enables the panel.
- JO's `HIDEINFO` flag, bit 2, hides the panel. JA retains its story-audio rule
  for the same bit.
- `SET_MISSION_STATUS_SCREEN` enables the panel for a scripted transition.
- A new game, direct map command, or save load clears the display flag.
- A new game's first loading screen does not show completion statistics.

The game copies the completed mission's counters to UI variables before the
transition. It refreshes this snapshot during game shutdown when the display
is enabled. The next map can then reset its live counters without changing the
displayed results. The `clearstats` map setting retains its existing carry/reset
behavior. No new save format is required.

The panel shows secrets, kills, favorite weapon, shots, hits, and accuracy.
Force and saber rows appear after saber acquisition or recorded saber use.
The panel also shows the additional JA powers after they are acquired or used.
Favorite weapon measures use time, not shots fired. Zero shots therefore do not
necessarily mean that there is no favorite weapon. An unarmed mission clears a
stale favorite label, and zero shots produce `0.00%` accuracy.

The text importer now accepts `NOTES` metadata before an English STRIP entry.
This restores five previously omitted `sp_ingame` entries, including the word
used in `1 of 3` secrets. Objective ordering remains unchanged.

## Checks

```bash
python3 scripts/test-jo-stats.py --package build/ready --renderer rdsp-vanilla
python3 scripts/test-jo-stats.py --package build/ready --renderer rdsp-rend2
```

Both checks pass. Each run creates an isolated profile and a small local fixture.
It fires real shots, kills a test NPC, finds a secret through `target_secret`,
picks up a saber, throws it, and uses Force push. It compares the live counters
with the displayed snapshot across a retail exit and a script-requested
transition. A local map override sets `clearstats=0` for the carry check.

Further cases check counter reset, save loading, zero shots, an unarmed map,
direct map loading, and the retail Valley `HIDEINFO` exit. Captures come from
the actual loading screen. Logs, captures, and result data are in `build/jo-stats/`.
The check waits on the completed panel and confirms that the next map's clock stops.
It checks both Enter and mouse-click continuation.
The test does not complete the missions through normal play.

Use `missionstats_status` to inspect live counters and the last UI snapshot.
Set `d_missionStats 1` to log loading-screen draws. Desktop visual checks remain
in `human_todo.md`.
