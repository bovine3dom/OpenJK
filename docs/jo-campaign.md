# Jedi Outcast Campaign MVP

## Scope

This prototype runs JO content in the JA single-player executable and game
module. It uses the shared controls, weapon wheel, Force wheel, and AI code.
Kejim Post and Kejim Base are the initial test maps.

The checks cover the opening scene, cinematic skipping, starting equipment,
weapon selection, save/load, and an explicit map transition. They do not prove
that the player can complete every puzzle or the full campaign. Complete a
manual Kejim playthrough before you use this prototype for a campaign run.

## Test on Another Machine

Install the new updater on the test machine. Replace `BUILD_SERVER` with your
SSH host alias:

```bash
scp BUILD_SERVER:/home/olie/projects/worktrees/openjk-jed-joi/scripts/play-sp.sh ~/.local/bin/openjk-play
chmod +x ~/.local/bin/openjk-play
```

For an existing desktop configuration, add the local JO asset path:

```bash
openjk-play --configure-jo /path/to/GameData_JO
```

For a new configuration, supply both local asset paths:

```bash
openjk-play --configure BUILD_SERVER /path/to/GameData /path/to/GameData_JO
```

Start the JO opening from this worktree:

```bash
openjk-play --worktree jed-joi --campaign jo --new-game --desktop
```

To open the menu and load a save, omit `--new-game`:

```bash
openjk-play --worktree jed-joi --campaign jo --resolution 1920x1080
```

Select Rend2 with an engine argument:

```bash
openjk-play --worktree jed-joi --campaign jo --new-game --desktop +set cl_renderer rdsp-rend2
```

Select JA with `--campaign ja`. The default campaign is JA.
Put worktree and campaign options before display and engine arguments.
`--new-game` starts a new game each time you use it.

Python 3.9 or later is required on the test machine. On the first JO launch,
the importer creates `OpenJK/zz_jo_campaign.pk3` in the JO profile. This file
contains selected and converted assets from your local installations. The
package transfer does not include these assets. Leave both installations in
place. Later launches reuse the import unless the importer or source archives
change. Allow approximately 1 GB of free space for the import and its temporary
file.

JA keeps its existing profile path. JO uses `campaigns/jo` below that path.
The worktree option also retains its existing profile separation. The launcher
prints the selected profile. JO imports, settings, and saves stay in that
profile. Do not use original JO saves or copy JA saves into it.

## Controls and Diagnostics

- Hold **H** to open the weapon wheel. Kejim starts with the Bryar pistol and
  stun baton.
- The Force wheel stays closed until the player has an available Force power.
- Use the normal cinematic-skip control to leave the opening scene.
- Use the shared save and load menus to continue a test.
- Enter `campaign_status` in the console to print the campaign, map, camera
  state, equipment, ammunition, position, and active objectives.
- Enter `cinematic_status NPC_NAME` to print a cinematic actor's position,
  movement, animations, and pending tasks. For example, use
  `cinematic_status cinematic4_kyle` during the Artus opening.
- Set `g_subtitles 2` to show subtitles for all voiceovers.
- Press **Keypad 4** to toggle acquired light-amplification goggles. **U/O** select
  inventory items and **I** uses the selected item. The first selection press
  opens the stock inventory display; the next advances the selection. These
  defaults are assigned only to unused keys and preserve existing command bindings.
- The mounted-gun bar shows health. An invulnerable gun shows the player's
  health; a vulnerable gun shows its own health. Firing does not consume health.

Rend2 skin diffusion uses the shared defaults: `r_sss 1` and `r_sssRadius 0.5`.
The importer preserves JO material paths, including those in the cinematic
Kyle and Jan skins. These paths use the existing stock skin profiles. The
JO-only Reborn acrobat and fencer faces also have profiles. Their armour and
hoods remain outside the skin effect. Use `r_sssDebug 1` to inspect skin coverage.

## Build and Automated Checks

From the repository root:

```bash
bash scripts/build-sp.sh
python3 scripts/test-import-jo.py
python3 scripts/test-play-sp.py
python3 scripts/test-jo-sp.py --package build/ready --renderer rdsp-rend2 --sss
python3 scripts/test-jo-sp.py --package build/ready --content --renderer rdsp-rend2
python3 scripts/test-jo-cinematics.py --package build/ready --renderer rdsp-rend2
```

The build script checks JA with both renderers. If `GameData_JO/base` is present,
it also runs the vanilla JO integration, AI, content, and cinematic checks before
publication. Set `OJK_JO_ASSETS` to select a different JO installation.
The desktop updater requires the JO check results when you select JO.

The integration test uses Xvfb, software rendering, and SDL's dummy audio output.
It checks active level music and captures the loading screen and console.
It records logs and screenshots under `build/jo-tests/`. It uses the real opening
scripts and the shared game module. The transition check calls `maptransition`;
it does not complete the mission's final puzzle. Audio and hardware rendering
still need a manual test.
The optional `--sss` check tests imported skin masks on Kyle, Jan, both cinematic
clones, the acrobat and fencer Reborn, a prisoner, Gran, and Ugnaught. It also
checks that stormtrooper armour has no eligible skin pixels.
The Python JO tests retain logs, captures, and saves, but remove each run's large
generated asset archive after the game exits. The launcher can regenerate it.

## Implementation and Limits

- `com_outcast` selects the content profile at process startup. JA gameplay
  remains the common implementation.
- The importer retains JA weapon definitions and humanoid gameplay animations.
  The renderer converts JO humanoid meshes to the JA skeleton where required.
- JO's dynamic music table, menu images, and level previews are imported with
  the music tracks. An updater launch rebuilds an older import automatically.
- Shader definitions merge by material name. Shared HUD, datapad, and 2D images
  use JA definitions; JO world materials take precedence. Source shader files
  are shadowed in the import so duplicate names cannot depend on file order.
- Weapon cycling and the weapon wheel share one order. Both cycling directions
  include the Bryar pistol and stun baton when owned and usable.
- The JO loading screen uses the supplied title artwork and the shared progress
  bar. Some retail level previews are empty placeholders.
- The cockpit actors and cinematic Galak use a separate JO skeleton. Their
  script animation names map to JA's existing cinematic animation slots. New
  gesture aliases follow the existing cockpit slots to preserve their indices.
  Other JO-specific cinematic animations still need review.
- JO NPC definitions and objective names load as campaign data. Objective slot
  zero stays reserved for JA's light-side state. The save layout is unchanged.
- JO NPC class names convert to JA names. Earlier MVP saves repair invalid NPC
  classes on load. See `encounter-kejim.md` for pressure tests and remaining limits.
- English STRIP text converts to StringEd text. Other JO text languages are not
  imported yet.
- JO's small script navgoals retain their sizes. Blocked points are omitted
  from JA's generated route graph and reported in the log. Jan's routes and
  squad behavior need a full mission test.
- Later maps are available for investigation. Their bosses, progression,
  cinematics, and map-specific rules are not validated by this MVP.

## Manual Acceptance Checks

1. Watch the opening with sound. Check dialogue timing, subtitles, and actor poses.
2. Skip the opening in a new session. Check that player control returns.
3. Select and fire both starting weapons. Check the wheel and direct bindings.
4. Fight the first guards. Check Jan's movement, friendly fire, and enemy reactions.
5. Read the datapad. Check the objectives and the three code objectives.
6. Complete Kejim Post through normal play and enter Kejim Base.
7. Save during play, quit, restart the updater, and load the save.
8. Repeat the scene checks with Rend2 on the test machine.

The content test checks weapon cycling, datapad text, mounted health changes,
goggle pickup and activation, panel textures, and the generator pipe material's
on/off states. It uses original entities with diagnostic activation and camera
positions. It does not complete the route to those entities through normal play.
The reported missing flame jets still need an exact save or screenshot. The
generator's glowing pipe material and sampled walkway flame effects rendered
during investigation; this does not establish that the reported scene is fixed.

## Kejim CCTV and Artus Opening

The Kejim Base CCTV sequence needs Galak on the bridge. JA's `NPC_Galak`
spawner was empty, so the actor never appeared and the sequence could not
complete. The unarmoured spawner now creates the actor. Three JO gesture
animations are included for his dialogue. The armoured Galak boss still needs
its separate JO controller.

The Artus opening gives Kyle the `DROPTOFLOOR` spawn flag. JA interpreted the
same bit as a Jedi ceiling ambush and enabled noclip. JO Kyle now retains ground
collision and uses his walking animation. Cinematic music also retains its full
filename when a track loops.

The cinematic tests run the original scripts without player-requested skipping.
They check that Galak speaks and performs a gesture, the CCTV actors are removed,
the following cinematic starts, and the Artus opening returns player control.
Movement samples check Kyle's walking animation, ground contact, and disabled
noclip. Screenshots are stored under `build/jo-cinematics/`.

Load a save from before the CCTV sequence to replay it after updating. Earlier
MVP saves repair the inert Galak spawner on load. The named Artus cinematic Kyle
also has the erroneous noclip state cleared when loaded. A save already stalled
inside the CCTV script cannot recover the tasks that ran without Galak; use an
earlier save for that case.

To check an earlier save in a separate test profile:

```bash
python3 scripts/test-jo-cinematics.py --package build/ready --case cctv --save /path/to/before_cctv.sav
```
