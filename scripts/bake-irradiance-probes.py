#!/usr/bin/env python3
"""Bake and collect a directional irradiance grid for one campaign map."""

import argparse
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("map", choices=("t1_sour", "kejim_post"))
    parser.add_argument("--package", type=Path, default=root / "build/ready")
    parser.add_argument("--output", type=Path, default=root / "probe-data/maps")
    parser.add_argument("--horizontal-stride", type=int, choices=range(1, 9), default=2)
    parser.add_argument("--vertical-stride", type=int, choices=range(1, 9), default=1)
    parser.add_argument("--face-size", type=int, choices=range(4, 33), default=16)
    parser.add_argument("--timeout", type=int, default=900)
    args = parser.parse_args()

    package = args.package.resolve()
    assets = Path(os.environ.get("OJK_ASSETS", root / "GameData")).resolve()
    campaign = "jo" if args.map == "kejim_post" else "ja"
    jo_assets = Path(os.environ.get("OJK_JO_ASSETS", root / "GameData_JO")).resolve()
    if not (package / "launch-sp.sh").is_file():
        parser.error(f"Package has no launch-sp.sh: {package}")
    if not (assets / "base/assets0.pk3").is_file():
        parser.error(f"Jedi Academy assets are missing: {assets}")
    if campaign == "jo" and not (jo_assets / "base/assets0.pk3").is_file():
        parser.error(f"Jedi Outcast assets are missing: {jo_assets}")

    work = root / "build/probe-bakes"
    work.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix=f"{args.map}.", dir=work))
    profile = run / "profile"
    log = run / "console.log"
    env = dict(os.environ, OJK_PROFILE=str(profile), OJK_JO_ASSETS=str(jo_assets),
               SDL_VIDEODRIVER="offscreen", EGL_PLATFORM="surfaceless", SDL_AUDIODRIVER="dummy")
    env.pop("LIBGL_ALWAYS_SOFTWARE", None)
    command = ["bash", str(package / "launch-sp.sh"), str(assets), "--campaign", campaign,
               "+set", "cl_renderer", "rdsp-rend2", "+set", "r_fullscreen", "0",
               "+set", "r_mode", "3", "+set", "s_initsound", "0",
               "+set", "r_glassProbes", "1", "+set", "r_cubeMapping", "0",
               "+set", "r_atmosphere", "1", "+set", "developer", "1",
               "+devmap", args.map, "+wait", "100", "+r_bakeIrradianceProbes",
               str(args.horizontal_stride), str(args.vertical_stride), str(args.face_size),
               "+wait", "5", "+quit"]
    print(f"Probe bake output: {run}", flush=True)
    with log.open("w") as stream:
        subprocess.run(command, env=env, stdout=stream, stderr=subprocess.STDOUT,
                       check=True, timeout=args.timeout, start_new_session=True)

    text = log.read_text(errors="replace")
    match = re.search(rf"Irradiance probe bake: wrote maps/{args.map}\.irrprobe, "
                      r"(\d+) of (\d+) positions, (\d+) ms", text)
    if not match or re.search(r"ERROR:|GL_INVALID_|Unknown command|trying to load fallback renderer", text):
        raise RuntimeError(f"Probe bake failed: {log}")
    game = profile / ("campaigns/jo/OpenJK" if campaign == "jo" else "OpenJK")
    source = game / f"maps/{args.map}.irrprobe"
    data = source.read_bytes()
    if len(data) < 56 or struct.unpack_from("<4sI", data) != (b"OIP1", 1):
        raise RuntimeError(f"Invalid probe file: {source}")
    args.output.mkdir(parents=True, exist_ok=True)
    destination = args.output / source.name
    shutil.copyfile(source, destination)
    print(f"Wrote {destination}: {len(data)} bytes, {match[1]} valid positions, {int(match[3]) / 1000:.1f} seconds")


if __name__ == "__main__":
    main()
