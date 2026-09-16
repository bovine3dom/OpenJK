# JA and JO Atmosphere Map Audit

## Scope and Method

The September 16, 2026 audit covers every BSP in the installed retail JA and JO
archives. It checks compiled sky flags, rendered surface counts, shader sky
parameters, sun declarations, sky portals, and the six source images. JO world
shader and image overrides take precedence over JA, as in the importer.

Source-art contact sheets were reviewed. These are initial art decisions, not
full campaign playthrough results. Only the t1_sour appearance has user approval.

Profiles with painted planets, forests, or strong cloud art use a low `skyBlend`.
They retain most of the source image. Check feature contrast during manual review.
Night, space, and fog-only scenes keep stock rendering. No volumetric clouds are added.

See [the review guide](atmosphere-review.md) for controls and profile editing.

## JA Single Player: 23 Profiles, 11 Stock

| Map | Decision / palette | Sky target or source | Reason |
| --- | --- | --- | --- |
| `academy1` | yavin-ja | `textures/skies/yavin` | Add restrained jungle daylight. Retain most of the painted planet and canopy; review their contrast. |
| `academy2` | yavin-ja | `textures/skies/yavin` | Add restrained jungle daylight. Retain most of the painted planet and canopy; review their contrast. |
| `academy3` | yavin-ja | `textures/skies/yavin` | Add restrained jungle daylight. Retain most of the painted planet and canopy; review their contrast. |
| `academy4` | yavin-ja | `textures/skies/yavin` | Add restrained jungle daylight. Retain most of the painted planet and canopy; review their contrast. |
| `academy5` | yavin-ja | `textures/skies/yavin` | Add restrained jungle daylight. Retain most of the painted planet and canopy; review their contrast. |
| `academy6` | yavin-ja | `textures/skies/yavin` | Add restrained jungle daylight. Retain most of the painted planet and canopy; review their contrast. |
| `hoth2` | Stock | `-` | The sky has no six-face outerbox. Keep its authored geometry, layers, and fog. |
| `hoth3` | Stock | `none` | No rendered cube sky; retain the authored interior or fog background. |
| `kor1` | korriban | `textures/skies/korriban` | Use a muted warm-dust blend for the tomb landscape. |
| `kor2` | korriban | `textures/skies/korriban` | Profile only the Korriban material. Keep the ending's Yavin and space materials stock. |
| `t1_danger` | blenjeel | `textures/skies/dune` | Add a mild dry-air gradient. Keep the two painted planets prominent; use a low blend. |
| `t1_fatal` | bakura | `textures/skies/t1_fatal` | Add a restrained teal-green atmosphere. Keep the painted moon and water composition prominent. |
| `t1_inter` | yavin-ja | `textures/skies/yavin` | Add restrained jungle daylight. Retain most of the painted planet and canopy; review their contrast. |
| `t1_rail` | Stock | `-` | The sky has no six-face outerbox. Keep its authored geometry, layers, and fog. |
| `t1_sour` | tatooine | `textures/skies/desert` | Daylight cube with a low mountain horizon; suitable for the established desert atmosphere. |
| `t1_surprise` | tatooine | `textures/skies/desert` | Reuse the Tatooine daylight palette for the droid-recovery landscape. |
| `t2_dpred` | dosuun | `textures/skies/dosuun` | Use muted overcast daylight; retain most of the cloud and terrain artwork. |
| `t2_rancor` | amber-overcast | `textures/skies/narkreeta` | Use a low amber blend to retain the bright cloud deck and rocky horizon. |
| `t2_rogue` | Stock | `textures/skies/cs1` | Keep the night sky, stars, and painted celestial disc. |
| `t2_trip` | Stock | `-` | The sky has no six-face outerbox. Keep its authored geometry, layers, and fog. |
| `t2_wedge` | krildor | `textures/skies/wedge_sky` | Use a restrained golden atmosphere above the authored cloud sea. Volumetric clouds remain deferred. |
| `t3_bounty` | Stock | `textures/skies/b` | Keep the night sky and stars. The current optical profile is a daylight model. |
| `t3_byss` | Stock | `textures/skies/byss` | Keep the space backdrop and nebulae. |
| `t3_hevil` | storm-grey | `textures/skies/hevil_2` | Use a low cool-grey blend. Preserve the storm lighting and cloud contrast. |
| `t3_rift` | chandrila | `textures/skies/artus_light` | Use restrained canyon daylight. Keep the painted sun; add no second disc. |
| `t3_stamp` | Stock | `none` | No rendered cube sky; retain the authored interior or fog background. |
| `taspir1` | Stock | `-` | The sky has no six-face outerbox. Keep its authored geometry, layers, and fog. |
| `taspir2` | amber-overcast | `textures/skies/narkreeta` | Use a low amber blend to retain the bright cloud deck and rocky horizon. |
| `vjun1` | Stock | `-` | The sky has no six-face outerbox. Keep its authored geometry, layers, and fog. |
| `vjun2` | bespin | `textures/skies/bespin` | Use a low peach-haze blend for the authored cloud backdrop; inspect its limited sky openings. |
| `vjun3` | Stock | `textures/skies/vu` | Keep the acid-haze background and its authored fog match. |
| `yavin1` | yavin-ja | `textures/skies/yavin` | Add restrained jungle daylight. Retain most of the painted planet and canopy; review their contrast. |
| `yavin1b` | yavin-ja | `textures/skies/yavin` | Add restrained jungle daylight. Retain most of the painted planet and canopy; review their contrast. |
| `yavin2` | yavin-ja | `textures/skies/yavin` | Profile the Yavin cube. Keep the more numerous no-outerbox helper sky surfaces stock. |

## JO Single Player: 10 Profiles, 16 Stock

| Map | Decision / palette | Sky target or source | Reason |
| --- | --- | --- | --- |
| `artus_detention` | Stock | `textures/skies/desert2` | Keep the moonlit mountain and cloud composition; a daylight replacement would alter the scene. |
| `artus_mine` | artus | `textures/skies/test2` | Use JO's dark amber-overcast palette. This asset differs from JA's blue desert sky. |
| `artus_topside` | artus | `textures/skies/test2` | Use JO's dark amber-overcast palette. This asset differs from JA's blue desert sky. |
| `bespin_platform` | bespin | `textures/bespin/sky_platform` | Use a restrained peach-haze blend while retaining the cloud-city backdrop. |
| `bespin_streets` | bespin | `textures/bespin/sky` | Use a restrained peach-haze blend while retaining the cloud-city backdrop. |
| `bespin_undercity` | bespin | `textures/skies/bespin` | Profile the 29-surface sky material. One secondary sky surface remains stock; inspect its transition. |
| `cairn_assembly` | Stock | `none` | No rendered cube sky; retain the authored interior or fog background. |
| `cairn_bay` | Stock | `textures/skies/stars` | Keep the stars and dark space/night background. |
| `cairn_dock1` | Stock | `none` | No rendered cube sky; retain the authored interior or fog background. |
| `cairn_reactor` | Stock | `none` | No rendered cube sky; retain the authored interior or fog background. |
| `doom_comm` | Stock | `textures/skies/nebula2` | Keep the painted space backdrop and planets. |
| `doom_detention` | Stock | `textures/skies/nebula2` | Keep the painted space backdrop and planets. |
| `doom_shields` | Stock | `none` | No rendered cube sky; retain the authored interior or fog background. |
| `kejim_base` | Stock | `textures/skies/stars` | Keep the stars and dark space/night background. |
| `kejim_post` | Stock | `textures/skies/desert2, textures/skies/nebula2` | Keep the moonlit mountain and cloud composition; a daylight replacement would alter the scene. |
| `ns_hideout` | Stock | `none` | No rendered cube sky; retain the authored interior or fog background. |
| `ns_starpad` | bespin | `textures/skies/bespin` | Use the authored peach cloud backdrop as the palette reference, with a low blend. |
| `ns_streets` | Stock | `none` | No rendered cube sky; retain the authored interior or fog background. |
| `pit` | Stock | `none` | No rendered cube sky; retain the authored interior or fog background. |
| `valley` | Stock | `textures/skies/nebula2` | Keep the painted space backdrop and planets. |
| `yavin_canyon` | Stock | `none` | No rendered cube sky; retain the authored interior or fog background. |
| `yavin_courtyard` | yavin-jo | `textures/skies/yavin` | Use a restrained bright jungle profile. Retain most of JO's painted Yavin Prime and canopy. |
| `yavin_final` | yavin-jo | `textures/skies/yavin` | Use a restrained bright jungle profile. Retain most of JO's painted Yavin Prime and canopy. |
| `yavin_swamp` | Stock | `none` | No rendered cube sky; retain the authored interior or fog background. |
| `yavin_temple` | yavin-jo | `textures/skies/yavin` | Use a restrained bright jungle profile. Retain most of JO's painted Yavin Prime and canopy. |
| `yavin_trial` | yavin-jo | `textures/skies/yavin` | Use a restrained bright jungle profile. Retain most of JO's painted Yavin Prime and canopy. |

## Multiplayer Inventory

These maps were included in the asset audit. They retain stock rendering in
this SP rollout and are not in the campaign review playlists.

| Campaign | Map | Sky source |
| --- | --- | --- |
| JA | `mp/ctf1` | `textures/skies/cs1` |
| JA | `mp/ctf2` | `-` |
| JA | `mp/ctf3` | `textures/skies/fatal` |
| JA | `mp/ctf4` | `none` |
| JA | `mp/ctf5` | `textures/skies/korb` |
| JA | `mp/duel1` | `textures/skies/bespin` |
| JA | `mp/duel10` | `none` |
| JA | `mp/duel2` | `none` |
| JA | `mp/duel3` | `none` |
| JA | `mp/duel4` | `textures/skies/space` |
| JA | `mp/duel5` | `textures/skies/korb` |
| JA | `mp/duel6` | `textures/skies/yavin` |
| JA | `mp/duel7` | `textures/skies/korb` |
| JA | `mp/duel8` | `textures/skies/korb` |
| JA | `mp/duel9` | `textures/skies/hevil` |
| JA | `mp/ffa1` | `textures/skies/cs1` |
| JA | `mp/ffa2` | `textures/skies/b` |
| JA | `mp/ffa3` | `textures/skies/desert` |
| JA | `mp/ffa4` | `layered` |
| JA | `mp/ffa5` | `textures/skies/korb` |
| JA | `mp/siege_desert` | `textures/skies/desert` |
| JA | `mp/siege_hoth` | `textures/skies/hevil` |
| JA | `mp/siege_korriban` | `textures/skies/desert` |
| JO | `ctf_bespin` | `none` |
| JO | `ctf_imperial` | `textures/skies/bespin` |
| JO | `ctf_ns_streets` | `textures/skies/stars` |
| JO | `ctf_yavin` | `textures/skies/yavin` |
| JO | `duel_bay` | `none` |
| JO | `duel_bespin` | `none` |
| JO | `duel_carbon` | `none` |
| JO | `duel_hangar` | `textures/skies/space` |
| JO | `duel_jedi` | `textures/skies/desert` |
| JO | `duel_pit` | `none` |
| JO | `duel_temple` | `none` |
| JO | `duel_training` | `textures/skies/yavin` |
| JO | `ffa_bespin` | `textures/skies/bespin` |
| JO | `ffa_deathstar` | `textures/skies/space` |
| JO | `ffa_imperial` | `textures/skies/space` |
| JO | `ffa_ns_hideout` | `textures/skies/bespin` |
| JO | `ffa_ns_streets` | `textures/skies/stars` |
| JO | `ffa_raven` | `textures/skies/space` |
| JO | `ffa_yavin` | `textures/skies/yavin` |

## Reproduce the Audit

```sh
python3 scripts/audit-atmosphere.py --sheets --catalogue scripts/atmosphere-catalogue.json
python3 scripts/build-atmosphere-review.py --report docs/atmosphere-map-audit.md
```

Contact sheets and the detailed inventory are written under `build/atmosphere-audit/`.
Face order is right, left, back, front, up, down. Profiles are the editable source
of truth. `--seed-profiles` creates missing profiles and never overwrites edits.
