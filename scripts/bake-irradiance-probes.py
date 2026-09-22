#!/usr/bin/env python3
"""Bake and collect directional irradiance grids for campaign maps."""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile


def bake(args, root, package, assets, jo_assets, map_name, campaign):
    destination = args.output / f"{map_name}.irrprobe"
    if args.skip_existing and destination.is_file():
        print(f"Skip existing {destination}", flush=True)
        return 0, 0.0

    work = root / "build/probe-bakes"
    work.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix=f"{map_name}.", dir=work))
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
               "+devmap", map_name, "+wait", "100", "+r_bakeIrradianceProbes",
               str(args.horizontal_stride), str(args.vertical_stride), str(args.face_size),
               "+wait", "5", "+quit"]
    print(f"Probe bake output for {campaign}/{map_name}: {run}", flush=True)
    with log.open("w") as stream:
        subprocess.run(command, env=env, stdout=stream, stderr=subprocess.STDOUT,
                       check=True, timeout=args.timeout, start_new_session=True)

    text = log.read_text(errors="replace")
    match = re.search(rf"Irradiance probe bake: wrote maps/{re.escape(map_name)}\.irrprobe, "
                      r"(\d+) of (\d+) positions, (\d+) ms", text)
    if not match or re.search(r"ERROR:|GL_INVALID_|Unknown command|trying to load fallback renderer", text):
        raise RuntimeError(f"Probe bake failed: {log}")
    game = profile / ("campaigns/jo/OpenJK" if campaign == "jo" else "OpenJK")
    source = game / f"maps/{map_name}.irrprobe"
    data = source.read_bytes()
    if len(data) < 56 or struct.unpack_from("<4sI", data) != (b"OIP1", 1):
        raise RuntimeError(f"Invalid probe file: {source}")
    args.output.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)
    seconds = int(match[3]) / 1000
    print(f"Wrote {destination}: {len(data)} bytes, {match[1]} valid positions, {seconds:.1f} seconds",
          flush=True)
    return len(data), seconds


def main():
    root = Path(__file__).resolve().parent.parent
    entries = [entry for entry in json.loads((root / "scripts/atmosphere-catalogue.json").read_text())["maps"]
               if entry["kind"] == "sp"]
    campaign_for = {entry["map"]: entry["campaign"] for entry in entries}
    if len(campaign_for) != len(entries):
        raise RuntimeError("Campaign map names are not unique")

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("map", nargs="*", help="Campaign map name; specify more than one to run a batch")
    parser.add_argument("--all", action="store_true", help="Bake all campaign maps")
    parser.add_argument("--campaign", choices=("ja", "jo"), help="Limit a batch to one campaign")
    parser.add_argument("--package", type=Path, default=root / "build/ready")
    parser.add_argument("--output", type=Path, default=root / "probe-data/maps")
    parser.add_argument("--horizontal-stride", type=int, choices=range(1, 9), default=2)
    parser.add_argument("--vertical-stride", type=int, choices=range(1, 9), default=1)
    parser.add_argument("--face-size", type=int, choices=range(4, 33), default=16)
    parser.add_argument("--timeout", type=int, default=900, help="Timeout in seconds for each map")
    parser.add_argument("--skip-existing", action="store_true", help="Resume without replacing output files")
    args = parser.parse_args()
    if args.all == bool(args.map):
        parser.error("Specify map names or --all")
    unknown = sorted(set(args.map) - set(campaign_for))
    if unknown:
        parser.error("Unknown campaign map: " + ", ".join(unknown))
    selected = ([entry["map"] for entry in entries if not args.campaign or entry["campaign"] == args.campaign]
                if args.all else args.map)
    if args.campaign and any(campaign_for[name] != args.campaign for name in selected):
        parser.error(f"A selected map is not in the {args.campaign.upper()} campaign")

    package = args.package.resolve()
    assets = Path(os.environ.get("OJK_ASSETS", root / "GameData")).resolve()
    jo_assets = Path(os.environ.get("OJK_JO_ASSETS", root / "GameData_JO")).resolve()
    if not (package / "launch-sp.sh").is_file():
        parser.error(f"Package has no launch-sp.sh: {package}")
    if not (assets / "base/assets0.pk3").is_file():
        parser.error(f"Jedi Academy assets are missing: {assets}")
    if any(campaign_for[name] == "jo" for name in selected) and not (jo_assets / "base/assets0.pk3").is_file():
        parser.error(f"Jedi Outcast assets are missing: {jo_assets}")

    total_size = 0
    total_seconds = 0.0
    failures = []
    for map_name in selected:
        try:
            size, seconds = bake(args, root, package, assets, jo_assets, map_name, campaign_for[map_name])
            total_size += size
            total_seconds += seconds
        except (OSError, RuntimeError, subprocess.SubprocessError) as error:
            failures.append(f"{campaign_for[map_name]}/{map_name}: {error}")
            print(f"FAILED: {failures[-1]}", flush=True)
    print(f"Completed {len(selected) - len(failures)} of {len(selected)} maps: "
          f"{total_size} bytes, {total_seconds:.1f} bake seconds", flush=True)
    if failures:
        raise RuntimeError("Probe bake failures:\n" + "\n".join(failures))


if __name__ == "__main__":
    main()
