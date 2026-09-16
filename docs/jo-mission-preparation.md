# JO Mission Preparation

At the end of a combat mission, continue from the statistics screen to mission
preparation. Select two main weapons and one explosive type. Select optional
Force upgrades, then choose **Begin mission**. The next map loads after this step.

The screen uses the shared JA menu system. Click an item to select it. Use Up,
Down, or Tab to move keyboard focus, and Enter to activate the focused item.
To replace a selected main weapon, clear its selection first.

## Mission Boundaries

The preparation screen appears only when the campaign advances to another
combat mission. Connected maps use the same mission group:

| Group | Maps | Total optional Force points |
| --- | --- | ---: |
| Kejim | `kejim_post`, `kejim_base` | 0 |
| Artus Prime | `artus_mine`, `artus_detention`, `artus_topside` | 0 |
| Nar Shaddaa | `ns_streets`, `ns_hideout`, `ns_starpad` | 2 |
| Bespin | `bespin_undercity`, `bespin_streets`, `bespin_platform` | 3 |
| Cairn Installation | `cairn_bay`, `cairn_assembly`, `cairn_reactor`, `cairn_dock1` | 4 |
| Doomgiver | `doom_comm`, `doom_detention`, `doom_shields` | 5 |
| Return to Yavin | `yavin_swamp`, `yavin_canyon`, `yavin_courtyard`, `yavin_final` | 7 |

A new game starts with the normal JO equipment. The first preparation screen is
between Kejim Base and Artus Mine. Valley and Jedi training retain their story
equipment and have no preparation screen. Internal map transitions do not open
weapon or Force selection. Their existing statistics display rules still apply.

Direct map commands and save loads do not open preparation. The bonus `pit` map
is outside the campaign groups.

## Weapons

Choose two from:

- Blaster rifle
- Disruptor
- Bowcaster
- Repeater
- DEMP 2
- Flechette
- Rocket launcher
- JA concussion rifle

Choose thermal detonators, trip mines, or detonation packs as the explosive type.
The selected weapons receive full ammunition. Kyle keeps his Bryar pistol and
his saber after its story unlock. An owned stun baton is also retained.
Other inventory items are preserved.

You can collect more weapons during a mission. At the next preparation screen,
select two main weapons again. Story weapon locks, including the bar sequence,
continue to apply.

## Force Points

All original JO powers remain controlled by the story. This includes Heal,
Mind Trick, Grip, and Lightning, as well as Jump, Speed, Push, Pull, and saber skills.

Points buy only Absorb, Protect, Rage, Drain, and Sense. Each rank costs one point.
Each power has three ranks, for 15 possible ranks. The campaign permits seven
purchased ranks by the final mission: just under half of the total.

The total budget rises to 2, 3, 4, 5, and 7 at the post-training mission boundaries.
Points are unavailable before Force training. Unspent points remain available
at later preparation screens. The minus buttons undo changes on the current
screen; they do not refund purchases from earlier missions.

The budget is a story limit, not a reward added on every transition. Returning
to an earlier save or repeating a transition does not increase it.

## Saves and Implementation

Choices take effect before the map transition. The normal player-transfer and
save systems carry weapons, ammunition, and Force ranks. A saved `jo_loadoutMap`
marker preserves a chosen loadout through the Nar Shaddaa entry reset, including
entry autosaves. A direct map command clears that marker. No save-format change
is required.

Finish preparation before saving. A pending selection is temporary and cannot
be saved as a completed mission state. Renderer restarts preserve the draft.

Mission groups, budgets, and allowed choices are in `code/game/g_jo_missions.cpp`.
The menu is `ui/jo_preparation.menu`. Use `jo_prepare status` to inspect a pending
selection and the player's current optional ranks.

## Verification

```bash
python3 scripts/test-jo-preparation.py --package build/ready
python3 scripts/test-jo-preparation.py --package build/ready --renderer rdsp-rend2
```

Both renderer checks pass. They cover mission boundaries, loadout limits, mouse
and keyboard input, rank limits, unavailable story-power purchases, and the
seven-point budget. They also check full saves, entry autosaves, earlier-save
restoration, concussion firing, Sense activation, and automatic Jump upgrades.

The test adds local exit entities to retail maps. These isolate transitions
without completing every intervening objective. The test does not establish
full normal-play campaign completion.
