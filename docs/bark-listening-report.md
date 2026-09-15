# Squad Bark Listening Report

## Commands

Close the game before using the desktop updater for an audition. List the original
clips, then play one clip at a time:

```bash
openjk-play --worktree squad_ai --barks --voice st1
openjk-play --worktree squad_ai --barks --voice st1 --play cover1
```

Playback uses `ffplay` from FFmpeg. It does not change the game profile. The tool
reads the original PK3 archives and removes its temporary audio file after playback.
If `ffplay` is unavailable, the listing gives the exact asset path. In the game
console, run `play` followed by that path.

Please listen to this short first set:

```bash
openjk-play --worktree squad_ai --barks --voice st1 --play cover1
openjk-play --worktree squad_ai --barks --voice st1 --play cover2
openjk-play --worktree squad_ai --barks --voice st1 --play outflank1
openjk-play --worktree squad_ai --barks --voice st1 --play outflank2
openjk-play --worktree squad_ai --barks --voice st1 --play escaping1
openjk-play --worktree squad_ai --barks --voice st1 --play lost1
openjk-play --worktree squad_ai --barks --voice st1 --play look1
openjk-play --worktree squad_ai --barks --voice st1 --play detected1
```

## Fill In

Use: cover / flank / rally / danger / lost contact / acknowledgement / none.
The filenames are candidates, not confirmed meanings. Mark unclear words with `?`.

| Clip | Words heard | Suitable event | Clear in combat? | Notes |
| --- | --- | --- | --- | --- |
| st1/cover1 | | | | |
| st1/cover2 | | | | |
| st1/outflank1 | | | | |
| st1/outflank2 | | | | |
| st1/escaping1 | | | | |
| st1/lost1 | | | | |
| st1/look1 | | | | |
| st1/detected1 | | | | |

Which clip becomes annoying when repeated? ___

Which important event has no suitable clip? ___

For a later voice comparison, use `--voice humanmerc1`, `humanmerc2`, `rodian2`,
`trandoshan1`, or `weequay`. The tool marks missing clips as unavailable.
