#!/usr/bin/env python3
"""Validate atmosphere profiles and build campaign-specific review playlists."""

import argparse
import json
import math
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
CATALOGUE = ROOT / "scripts/atmosphere-catalogue.json"


def profile_text(entry, policy):
    lines = ["atmosphere 1", "sky " + entry["sky"],
             f"// Initial palette: {entry['palette']}. Edit this map profile for tuning.",
             "// " + entry["sun_basis"]]
    parameters = dict(policy["base"], **policy["palettes"][entry["palette"]])
    parameters["sunDirection"] = entry["sun_direction"]
    for key, value in parameters.items():
        values = value if isinstance(value, list) else [value]
        lines.append(key + " " + " ".join(f"{x:g}" for x in values))
    return "\n".join(lines) + "\n"


def validate_profile(path, entry):
    text = re.sub(r"//[^\n]*|/\*.*?\*/", "", path.read_text(), flags=re.S).split()
    if text[:4] != ["atmosphere", "1", "sky", entry["sky"]]:
        raise ValueError(f"Wrong version or sky target: {path}")
    ranges = {"radius": (1000, 10000), "thickness": (20, 200), "observerHeight": (.001, 10),
              "rayHeight": (1, 20), "mieHeight": (.1, 10), "anisotropy": (0, .9), "illuminance": (.1, 64),
              "rayleigh": (.0001, .1), "mie": (.0001, .1), "absorption": (0, .1), "groundAlbedo": (0, 1),
              "sunDirection": (-1, 1), "sunRadius": (.001, .03), "sunDisk": (0, 32),
              "cloudStrength": (0, 1), "cloudColor": (0, 2), "skyBlend": (0, 1)}
    vectors = {"rayleigh", "mie", "absorption", "groundAlbedo", "sunDirection", "cloudColor"}
    fields, pos = {}, 4
    while pos < len(text):
        key = text[pos]
        if key not in ranges or key in fields:
            raise ValueError(f"Unknown or duplicate field in {path}: {key}")
        count = 3 if key in vectors else 1
        values = list(map(float, text[pos+1:pos+1+count]))
        if len(values) != count or any(not math.isfinite(x) or not ranges[key][0] <= x <= ranges[key][1] for x in values):
            raise ValueError(f"Invalid {key} in {path}")
        fields[key] = values
        pos += count + 1
    if set(ranges) - {"skyBlend"} - set(fields):
        raise ValueError(f"Missing fields in {path}")
    sun = fields["sunDirection"]
    if sun[2] <= 0 or sum(x*x for x in sun) <= .25:
        raise ValueError(f"Invalid sun direction in {path}")
    return fields


def message(text):
    return re.sub(r'[";\r\n]', " ", text)


def write_playlist(output, campaign, mode, maps):
    folder = output / "atmosphere-review" / campaign / mode
    folder.mkdir(parents=True, exist_ok=True)
    entry = f"atmosphere-review-{campaign}" + ("-all" if mode == "all" else "") + ".cfg"
    controls = ['set ' + value for value in ('r_atmosphere 1', 'r_compareEnhancements 0', 'r_autoExposure 0', 'logfile 2',
        'r_mapHaze 0', 'r_localFog 0', 'r_highResSkies 0', 'r_seamlessSky 1',
        'cg_draw2D 0', 'cg_drawGun 0', 'con_notifytime -1', 'cg_bobup 0', 'cg_bobpitch 0', 'cg_bobroll 0')]
    controls += [
        'bind PGDN "vstr ar_next"', 'bind PGUP "vstr ar_prev"', 'bind HOME "vstr ar_current"',
        'bind F4 "exitview"', 'bind F5 "r_compareEnhancements 0; toggle r_atmosphere"',
        'bind F6 "r_atmosphereReload"', 'bind F7 "vstr ar_stock"', 'bind F8 "vstr ar_atmosphere"',
        'bind F9 "vstr ar_flag"', 'bind F10 "vstr ar_status; mapname; r_atmosphere; viewpos"',
        'bind SPACE "+moveup"', 'bind CTRL "+movedown"']
    (output / entry).write_text("\n".join(controls + [f"exec atmosphere-review/{campaign}/{mode}/{maps[0]['map']}.cfg"]) + "\n")
    for i, item in enumerate(maps):
        name = item["map"]
        here = f"atmosphere-review/{campaign}/{mode}/{name}.cfg"
        state = item["palette"] or "STOCK - no atmosphere profile"
        label = message(f"ATMO REVIEW {campaign.upper()} {i+1}/{len(maps)} {name}: {state}")
        lines = [f'set ar_current "exec {here}"',
            f'set ar_next "exec atmosphere-review/{campaign}/{mode}/{maps[(i+1)%len(maps)]["map"]}.cfg"',
            f'set ar_prev "exec atmosphere-review/{campaign}/{mode}/{maps[(i-1)%len(maps)]["map"]}.cfg"',
            f'set ar_status "echo {label}; echo {message(item["reason"])}"',
            f'set ar_flag "echo ATMO_TWEAK {campaign}/{name}; viewpos; screenshot_png"',
            f'set ar_stock "r_compareEnhancements 0; r_atmosphere 0; wait 10; viewpos; screenshot_png atmo_{campaign}_{name}_stock"',
            f'set ar_atmosphere "r_compareEnhancements 0; r_atmosphere 1; wait 10; viewpos; screenshot_png atmo_{campaign}_{name}_profile"',
            'set com_maxfps 30', 'set d_npcfreeze 0', f'devmap {name}', 'wait 100', 'exitview', 'wait 100',
            'god', 'give all', 'wait 40', 'weapon 3', 'wait 80', 'noclip', 'd_npcfreeze 1', 'cg_thirdPerson 0',
            'r_compareEnhancements 0', 'r_atmosphere 1']
        if item.get("view"):
            lines.append("setviewpos " + " ".join(f"{x:g}" for x in item["view"]))
        lines += ['com_maxfps 60', 'vstr ar_status', 'echo PgDn/PgUp: next/previous. F5: toggle. F6: reload. F7/F8: captures. F9: flag. F10: status.']
        (folder / f"{name}.cfg").write_text("\n".join(lines) + "\n")


def report(maps, path):
    lines = ["# JA and JO Atmosphere Map Audit", "", "## Scope and Method", "",
        "The September 16, 2026 audit covers every BSP in the installed retail JA and JO",
        "archives. It checks compiled sky flags, rendered surface counts, shader sky",
        "parameters, sun declarations, sky portals, and the six source images. JO world",
        "shader and image overrides take precedence over JA, as in the importer.", "",
        "Source-art contact sheets were reviewed. These are initial art decisions, not",
        "full campaign playthrough results. Only the t1_sour appearance has user approval.", "",
        "Profiles with painted planets, forests, or strong cloud art use a low `skyBlend`.",
        "They retain most of the source image. Check feature contrast during manual review.",
        "Night, space, and fog-only scenes keep stock rendering. No volumetric clouds are added.", "",
        "See [the review guide](atmosphere-review.md) for controls and profile editing.", ""]
    for campaign in ("ja", "jo"):
        subset = [m for m in maps if m["campaign"] == campaign and m["kind"] == "sp"]
        count = sum(bool(m["palette"]) for m in subset)
        lines += [f"## {campaign.upper()} Single Player: {count} Profiles, {len(subset)-count} Stock", "",
                  "| Map | Decision / palette | Sky target or source | Reason |", "| --- | --- | --- | --- |"]
        for m in subset:
            sky = m.get("sky") or ", ".join(s["outer"] or "layered" for s in m["skies"]) or "none"
            lines.append(f"| `{m['map']}` | {m['palette'] or 'Stock'} | `{sky}` | {m['reason']} |")
        lines.append("")
    lines += ["## Multiplayer Inventory", "", "These maps were included in the asset audit. They retain stock rendering in",
              "this SP rollout and are not in the campaign review playlists.", "",
              "| Campaign | Map | Sky source |", "| --- | --- | --- |"]
    for m in maps:
        if m["kind"] == "mp":
            sky = ", ".join(s["outer"] or "layered" for s in m["skies"]) or "none"
            lines.append(f"| {m['campaign'].upper()} | `{m['map']}` | `{sky}` |")
    lines += ["", "## Reproduce the Audit", "", "```sh",
        "python3 scripts/audit-atmosphere.py --sheets --catalogue scripts/atmosphere-catalogue.json",
        "python3 scripts/build-atmosphere-review.py --report docs/atmosphere-map-audit.md", "```", "",
        "Contact sheets and the detailed inventory are written under `build/atmosphere-audit/`.",
        "Face order is right, left, back, front, up, down. Profiles are the editable source",
        "of truth. `--seed-profiles` creates missing profiles and never overwrites edits.", ""]
    path.write_text("\n".join(lines))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="Package OpenJK directory for generated review files")
    parser.add_argument("--seed-profiles", action="store_true", help="Create missing source profiles from the reviewed palettes")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    maps = json.loads(CATALOGUE.read_text())["maps"]
    policy = json.loads((ROOT / "scripts/atmosphere-review.json").read_text())
    selected = [m for m in maps if m["palette"]]
    if len({m["map"] for m in selected}) != len(selected):
        raise ValueError("Profile basename collision between campaigns")
    for m in maps:
        if not re.fullmatch(r"[a-z0-9_/]+", m["map"]):
            raise ValueError("Invalid map identifier")
    for m in selected:
        path = ROOT / "scripts/maps" / (m["map"] + ".atmosphere")
        if args.seed_profiles and not path.exists():
            path.write_text(profile_text(m, policy))
        validate_profile(path, m)
    if args.output:
        args.output.mkdir(parents=True, exist_ok=True)
        folder = args.output / "atmosphere-review"
        folder.mkdir(exist_ok=True)
        (folder / "catalogue.json").write_text(json.dumps(dict(version=1, maps=maps), indent=2) + "\n")
        for campaign in ("ja", "jo"):
            lookup = {m["map"]: m for m in maps if m["campaign"] == campaign and m["kind"] == "sp"}
            order = policy["campaigns"][campaign]["order"]
            if len(order) != len(lookup) or set(order) != set(lookup):
                raise ValueError(f"Review order does not cover {campaign} exactly once")
            ordered = [lookup[name] for name in order]
            write_playlist(args.output, campaign, "all", ordered)
            write_playlist(args.output, campaign, "profiles", [m for m in ordered if m["palette"]])
    if args.report:
        report(maps, args.report)
    print(f"Validated {len(selected)} profiles; audited {len(maps)} retail maps")


if __name__ == "__main__":
    main()
