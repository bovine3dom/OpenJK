# Passcode Overlay

## Behavior

The passcode overlay is available in the JO campaign. It uses RmlUi.

When the player finds a passcode, a `NEW PASSCODE FOUND` message appears for four seconds. The message uses the same position, font, size, and color as the new-objective message. If both messages are active, only the new-objective message appears.

The original passcode texture also appears in the upper-left corner. The texture has a small gap from the screen edges. It has no label, background, or border. The importer creates an additive UI shader, so black image pixels are transparent. The world texture does not change. More than one texture can be visible at the same time.

A texture stays visible while its puzzle objective is pending. The texture disappears when the game marks the objective as complete. The overlay does not appear during a cinematic, a remote camera view, an intermission, or a level screenshot.

The system gets the Kejim and Doomgiver passcodes from objective events. Nar Shaddaa does not send an event when the player sees a fuel symbol. Therefore, the JO importer adds two checked sight markers to `ns_starpad`. A marker records the symbol when these conditions are true:

- The symbol is not more than 256 game units from the view.
- The symbol is within 15 degrees of the center of the view.
- The player looks at the front of the symbol.
- Solid map geometry does not block the view.

The marker state is part of the saved entity state. A loaded save retains a symbol that the player found.

## Supported Passcodes

The overlay supports these JO passcodes:

| Map | Passcode | Game image |
| --- | --- | --- |
| Kejim Post | Red clearance code | `textures/system/securitycode_red` |
| Kejim Post | Green clearance code | `textures/system/securitycode_green` |
| Kejim Post | Blue clearance code | `textures/system/securitycode_blue` |
| Nar Shaddaa Starpad | Red fuel-line symbol | `textures/narshaddaa/fuelpump4` |
| Nar Shaddaa Starpad | Blue fuel-line symbol | `textures/narshaddaa/fuelpump3` |
| Doomgiver Communications Array | Communications-array code | `textures/system/securitycode` |

The audit searched the public JO mission scripts and checked the complete JO walkthrough. It also checked the complete JA walkthrough and the JA objective code. No JA puzzle requires the player to find a code and enter it later.

The Nar Shaddaa Hideout password is not in this list. Dialogue applies that password automatically. The Yavin trial symbols are also not in this list. Those symbols are part of a local matching puzzle and do not require later code entry.

## Diagnostics

Enter this command in the console:

```text
passcode_status
```

The command prints the active-code mask, the remaining notification time, and each map marker. A marker has `seen=1` after the player finds its symbol.

Use these checks after a code change:

```bash
python3 scripts/test-import-jo.py
OPENJK_IMPORT_JO=build/sp/openjedvibe-import-jo python3 scripts/test-import-jo.py
c++ -std=c++11 -Ishared tests/passcode_overlay.cpp -o /tmp/passcode-overlay-test
/tmp/passcode-overlay-test
```

A manual test must use a new JO import. Import format 4 includes the Nar Shaddaa markers and the additive passcode shaders. Test all six passcodes with both renderers. Also test a save before and after each Nar Shaddaa symbol is found.
