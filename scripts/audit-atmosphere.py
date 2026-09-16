#!/usr/bin/env python3
"""Inventory retail JA/JO map skies and produce source-art contact sheets."""

import argparse
from contextlib import ExitStack
import importlib.util
import json
import math
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("import_jo", ROOT / "scripts/import-jo.py")
assert spec is not None and spec.loader is not None
jo = importlib.util.module_from_spec(spec)
spec.loader.exec_module(jo)
FACES = ("rt", "lf", "bk", "ft", "up", "dn")


def shaders(index):
    result = {}
    for path in sorted(index):
        if path.startswith("shaders/") and path.endswith(".shader"):
            for name, body in jo.shader_definitions(jo.read(index, path)):
                result[name] = dict(source=path, body=body.decode("latin1"))
    return result


def entities(data):
    offset, size = struct.unpack_from("<ii", data, 8)
    return [dict((k.lower(), v) for k, v in re.findall(r'"([^"\n]+)"\s*"([^"\n]*)"', body))
            for body in re.findall(r"\{([^{}]*)\}", data[offset:offset+size].decode("latin1"))]


def sky_info(name, definitions, assets):
    definition = definitions.get(name, {})
    text = re.sub(r"//[^\n]*|/\*.*?\*/", "", definition.get("body", ""), flags=re.S)
    match = re.search(r"\bskyparms\s+(\S+)\s+(\S+)\s+(\S+)", text, re.I)
    outer = match[1].strip('"').lower() if match else None
    suns = re.findall(r"(?im)^\s*(?:sun|q3map_sun(?:ext)?|q3gl2_sun)\s+([^\r\n]+)", text)
    faces = []
    if outer and outer != "-":
        for suffix in FACES:
            path = next((f"{outer}_{suffix}.{ext}" for ext in ("jpg", "png", "tga")
                         if f"{outer}_{suffix}.{ext}" in assets), None)
            faces.append(path)
    return dict(shader=name, source=definition.get("source"), outer=outer, suns=suns,
                layers=re.findall(r"(?im)^\s*(?:map|clampmap)\s+(\S+)", text), faces=faces)


def inventory(index, definitions, assets):
    records = []
    for path in sorted(index):
        if not path.startswith("maps/") or not path.endswith(".bsp"):
            continue
        data = jo.read(index, path)
        if data[:8] != b"RBSP\x01\0\0\0":
            raise ValueError(f"Unsupported BSP: {path}")
        offset, size = struct.unpack_from("<ii", data, 16)
        surface_offset, surface_size = struct.unpack_from("<ii", data, 8 + 13*8)
        if size % 72 or surface_size % 148:
            raise ValueError(f"Unexpected BSP layout: {path}")
        counts = {}
        for pos in range(surface_offset, surface_offset + surface_size, 148):
            number = struct.unpack_from("<i", data, pos)[0]
            counts[number] = counts.get(number, 0) + 1
        names = []
        for pos in range(offset, offset + size, 72):
            name = data[pos:pos+64].split(b"\0", 1)[0].decode("latin1").lower()
            if struct.unpack_from("<I", data, pos+64)[0] & 0x2000:
                names.append((name, counts.get((pos-offset)//72, 0)))
        ents = entities(data)
        records.append(dict(map=path[5:-4], worldspawn=ents[0],
            starts=[e for e in ents if e.get("classname") == "info_player_start"],
            portals=[e for e in ents if e.get("classname") == "misc_skyportal"],
            skies=[dict(sky_info(n, definitions, assets), surfaces=count) for n, count in sorted(set(names))]))
    return records


def sheets(records, assets, output):
    seen = set()
    for record in records:
        for sky in record["skies"]:
            if not sky["faces"] or sky["outer"] in seen:
                continue
            seen.add(sky["outer"])
            frames, sizes = [], []
            for face in sky["faces"]:
                if not face:
                    frames.append(bytes(192*192*3))
                    sizes.append(None)
                    continue
                data = jo.read(assets, face)
                info = json.loads(subprocess.run(["ffprobe", "-v", "error", "-show_entries", "stream=width,height",
                    "-of", "json", "-i", "pipe:0"], input=data, capture_output=True, check=True).stdout)["streams"][0]
                sizes.append([info["width"], info["height"]])
                frames.append(subprocess.run(["ffmpeg", "-v", "error", "-i", "pipe:0", "-vf", "scale=192:192",
                    "-frames:v", "1", "-pix_fmt", "rgb24", "-f", "rawvideo", "-"],
                    input=data, capture_output=True, check=True).stdout)
            image = output / (sky["outer"].replace("/", "_") + ".png")
            subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "rawvideo", "-pixel_format", "rgb24",
                "-video_size", "192x192", "-i", "pipe:0", "-vf", "tile=3x2", "-frames:v", "1", str(image)],
                input=b"".join(frames), check=True)
            for item in records:
                for other in item["skies"]:
                    if other["outer"] == sky["outer"]:
                        other.update(face_sizes=sizes, sheet=image.name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--academy", type=Path, default=ROOT / "GameData")
    parser.add_argument("--outcast", type=Path, default=ROOT / "GameData_JO")
    parser.add_argument("--output", type=Path, default=ROOT / "build/atmosphere-audit")
    parser.add_argument("--sheets", action="store_true")
    parser.add_argument("--catalogue", type=Path, help="Write the reviewed map catalogue from the policy file")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    catalogue = []
    policy = json.loads((ROOT / "scripts/atmosphere-review.json").read_text())
    with ExitStack() as stack:
        ja, outcast = jo.index_assets(args.academy, stack), jo.index_assets(args.outcast, stack)
        ja_defs = shaders(ja)
        for campaign, maps, definitions, assets in (("ja", ja, ja_defs, ja),
                ("jo", outcast, dict(ja_defs, **shaders(outcast)), dict(ja, **outcast))):
            records = inventory(maps, definitions, assets)
            folder = args.output / campaign
            folder.mkdir(exist_ok=True)
            if args.sheets:
                sheets(records, assets, folder)
            elif (folder / "inventory.json").exists():
                previous = json.loads((folder / "inventory.json").read_text())
                sizes = {s["outer"]: s.get("face_sizes") for r in previous for s in r["skies"]}
                for record in records:
                    for sky in record["skies"]:
                        if sizes.get(sky["outer"]):
                            sky["face_sizes"] = sizes[sky["outer"]]
            (folder / "inventory.json").write_text(json.dumps(records, indent=2) + "\n")
            for record in records:
                print(campaign, record["map"], "; ".join(f'{s["shader"]} -> {s["outer"]} sun={s["suns"]}'
                      for s in record["skies"]) or "NO SKY", "portal=" + str(bool(record["portals"])))
            rules = policy["campaigns"][campaign]
            sp_maps = {r["map"] for r in records if not r["map"].startswith(("mp/", "ctf_", "ffa_", "duel_"))}
            if sp_maps != set(rules["order"]):
                raise ValueError(f"Unreviewed {campaign} maps: {sp_maps ^ set(rules['order'])}")
            for record in records:
                name = record["map"]
                kind = "sp" if name in sp_maps else "mp"
                override = rules["overrides"].get(name, {})
                active = [s for s in record["skies"] if s["surfaces"] > 0]
                if override.get("sky"):
                    active = [s for s in active if s["shader"] == override["sky"]]
                    if not active:
                        raise ValueError(f"Missing primary sky override: {campaign}/{name}")
                sky = max(active, key=lambda s: s["surfaces"]) if active else None
                selected = dict(rules["skies"].get(sky["outer"], {}) if sky else {})
                selected.update(override)
                palette = selected.get("palette") if kind == "sp" else None
                reason = selected.get("reason", "No rendered cube sky; retain the authored interior or fog background.")
                if sky and sky["outer"] in (None, "-"):
                    reason = "The sky has no six-face outerbox. Keep its authored geometry, layers, and fog."
                if kind == "mp":
                    reason = "Retail multiplayer map; retain stock rendering in this SP campaign rollout."
                entry = dict(campaign=campaign, map=name, kind=kind, palette=palette, reason=reason,
                             approved=bool(override.get("approved")),
                             skies=[{k: s[k] for k in ("shader", "outer", "source", "surfaces")} for s in record["skies"]],
                             sky_portal=bool(record["portals"]))
                if record["starts"]:
                    start = record["starts"][0]
                    origin = [float(x) for x in start.get("origin", "0 0 0").split()]
                    yaw = float(start.get("angle", start.get("angles", "0 0 0").split()[1]))
                    entry["view"] = [*origin[:2], origin[2] + 25, yaw]
                    entry["view_source"] = "BSP player start; use flight to reach a sky opening."
                if override.get("view"):
                    entry.update(view=override["view"], view_source="Verified rooftop capture point.")
                if palette:
                    if not sky or not all(sky["faces"]) or not sky.get("face_sizes") or any(
                            not s or s[0] != s[1] or max(s) > 1024 for s in sky["face_sizes"]):
                        raise ValueError(f"Profile has no supported cube: {campaign}/{name}; run --sheets")
                    entry["sky"] = sky["shader"]
                    parameters = dict(policy["base"], **policy["palettes"][palette])
                    entry["sun_basis"] = "Source-art lighting estimate; no new sun disc."
                    if palette == "tatooine":
                        entry["sun_basis"] = "Painted desert sun measured from the JA up face."
                    if sky["suns"] and record["worldspawn"].get("_noshadersun") != "1":
                        numbers = [float(v) for v in sky["suns"][0].split()[:6]]
                        if numbers[3] > 0 and 0 < numbers[5] <= 90:
                            yaw, elevation = map(math.radians, numbers[4:6])
                            parameters["sunDirection"] = [round(math.cos(yaw)*math.cos(elevation), 6),
                                round(math.sin(yaw)*math.cos(elevation), 6), round(math.sin(elevation), 6)]
                            entry["sun_basis"] = "Authored shader sun direction; no new sun disc."
                    entry["sun_direction"] = parameters["sunDirection"]
                catalogue.append(entry)
    if args.catalogue:
        args.catalogue.write_text(json.dumps(dict(version=1, maps=catalogue), indent=2) + "\n")


if __name__ == "__main__":
    main()
