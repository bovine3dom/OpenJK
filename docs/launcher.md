# OpenJedvibe Launcher

## Requirements

You must own and install Jedi Academy to use this package. You must also own
and install Jedi Outcast to play the Jedi Outcast campaign.

The OpenJedvibe package does not contain game data. The launcher reads your game
data from the folders that you select. It does not change these source files.

## Start The Launcher

On Windows, use the **OpenJedvibe** Start Menu shortcut. On macOS, open
`OpenJedvibe.app`. On Linux, open `openjedvibe.desktop`. You can also start
`openjedvibe-launcher` directly.

Select the folder that contains `base/assets0.pk3`. You can select a game
folder, a `GameData` folder, or a `base` folder. Jedi Academy data is necessary
for both campaigns.

The launcher lists Jedi Outcast first. Its colours and controls match the
in-game selection menus. The launcher checks the files before each launch.

- Select **Continue** to load the most recent save in that campaign. This control
  is disabled if there is no save. Temporary and empty save files are ignored.
  The engine checks the save data when it loads the file.
- Select **New Game** to start the first map.
- Select **Main Menu** to open the game menu without loading a save.
- Select **Music: Off** to start the original chiptune. Select **Music: On** to
  stop it. Music is off at startup. It does not contain Star Wars recordings or
  copied score data.

New profiles use the desktop resolution, full-screen mode, and aspect-correct
field of view. Existing `OpenJK/openjk_sp.cfg` settings are not overridden.
Use the game settings menu to change the display settings.

For desktop development builds, run `scripts/play-sp.sh --launcher`. You can
add `--worktree NAME` before or after `--launcher`. The script updates the build
and opens the launcher with the configured game folders and worktree profile.
Select the campaign in the launcher. Do not add engine arguments to this command.

Use `openjedvibe-launcher --ui --profile PATH --ja-path PATH --jo-path PATH`
to open the window with explicit paths. The JO path is optional. For command-line
launches, use `--continue --campaign ja` or `--continue --campaign jo`.

## Verification

The native import and launcher suite checks launch arguments, desktop defaults,
existing settings, latest-save selection, excluded save files, and campaign
separation. The desktop update suite checks update and profile behaviour.
Linux window checks use Xvfb and software OpenGL. Windows, macOS, hardware audio,
and a full campaign load from Continue still need manual checks.

## User Data

The launcher stores user data in these locations:

- Windows: `Documents/My Games/OpenJK`
- macOS: `Library/Application Support/OpenJK` in your home folder
- Linux: `$XDG_DATA_HOME/openjk`, or `$HOME/.local/share/openjk`

The `bootstrap.ini` file stores the selected game folders and the last selected
campaign. Jedi Academy settings and saves use the profile root. Jedi Outcast
settings and saves use `campaigns/jo` below the profile root.

The launcher creates the Jedi Outcast import at
`campaigns/jo/OpenJK/zz_jo_campaign.pk3`. It creates this file from your local
game installations. This file is not part of the downloaded package.

The launcher shows setup and import errors in its window. It does not create a
persistent launcher log. Engine logs, when enabled, use the active campaign's
`OpenJK` profile directory.
