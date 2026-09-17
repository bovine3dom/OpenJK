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

The launcher checks the selected files before each launch. Select **Play** to
continue a saved game. Select **New Game** to start the first map.

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
