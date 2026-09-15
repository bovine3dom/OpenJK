#!/usr/bin/env python3
"""List or play original squad voice clips without changing the game profile."""

import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile


VOICES = ("st1", "humanmerc1", "humanmerc2", "rodian2", "trandoshan1", "weequay")
CLIPS = ("cover1", "cover2", "outflank1", "outflank2", "escaping1", "lost1", "look1", "detected1")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets", type=Path, default=Path("GameData/base"))
    parser.add_argument("--voice", choices=VOICES, default="st1")
    parser.add_argument("--play", metavar="CLIP", choices=CLIPS)
    parser.add_argument("--check", action="store_true", help="Decode the selected voice set without audio output")
    args = parser.parse_args()
    found = {}
    for number in range(4):
        archive = args.assets / f"assets{number}.pk3"
        with zipfile.ZipFile(archive) as pk3:
            for path in pk3.namelist():
                name = Path(path.lower())
                if name.parent.as_posix() == f"sound/chars/{args.voice}/misc" and name.stem in CLIPS and name.suffix in (".wav", ".mp3"):
                    found[name.stem] = (archive, path)
    selected = (args.play,) if args.play else CLIPS
    for clip in selected:
        if clip not in found:
            if args.play:
                parser.error(f"No original clip: {args.voice}/{clip}")
            print(f"{args.voice}/{clip}: unavailable")
            continue
        archive, path = found[clip]
        print(f"{args.voice}/{clip}: {path}", flush=True)
        if not args.play and not args.check:
            continue
        program = "ffmpeg" if args.check else "ffplay"
        if not shutil.which(program):
            parser.error(f"{program} is required. In the game console, use: play {path}")
        with zipfile.ZipFile(archive) as pk3, tempfile.TemporaryDirectory(prefix="openjk-bark-") as directory:
            local = Path(directory) / Path(path).name
            local.write_bytes(pk3.read(path))
            command = ([program, "-v", "error", "-xerror", "-i", str(local), "-f", "null", "-"] if args.check else
                       [program, "-nodisp", "-autoexit", "-loglevel", "error", str(local)])
            subprocess.run(command, check=True)


if __name__ == "__main__":
    main()
