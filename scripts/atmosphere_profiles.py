#!/usr/bin/env python3
"""Prepare shared, editable map atmospheres in a campaign or review profile."""

import argparse
from collections import defaultdict
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import re
import shutil
import tempfile


def read_profile(path, expected_sky=None):
    text = re.sub(r"//[^\n]*|/\*.*?\*/", "", path.read_text(), flags=re.S).split()
    if len(text) < 4 or [s.lower() for s in text[:3]] != ["atmosphere", "1", "sky"]:
        raise ValueError(f"Wrong profile header: {path}")
    sky = text[3].lower()
    if expected_sky is not None and sky != expected_sky.lower():
        raise ValueError(f"Wrong sky target: {path}")
    ranges = {"radius": (1000, 10000), "thickness": (20, 200), "observerHeight": (.001, 10),
              "rayHeight": (1, 20), "mieHeight": (.1, 10), "anisotropy": (0, .9), "illuminance": (.1, 64),
              "rayleigh": (.0001, .1), "mie": (.0001, .1), "absorption": (0, .1), "groundAlbedo": (0, 1),
              "sunDirection": (-1, 1), "sunRadius": (.001, .03), "sunDisk": (0, 32),
              "cloudStrength": (0, 1), "cloudColor": (0, 2), "skyBlend": (0, 1)}
    names = {name.lower(): name for name in ranges}
    vectors = {"rayleigh", "mie", "absorption", "groundAlbedo", "sunDirection", "cloudColor"}
    fields, pos = {}, 4
    while pos < len(text):
        key = names.get(text[pos].lower())
        if key is None or key in fields:
            raise ValueError(f"Unknown or duplicate field in {path}: {text[pos]}")
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
    fields.setdefault("skyBlend", [1.0])
    return sky, fields


def profile_key(path):
    try:
        sky, fields = read_profile(path)
        return sky, tuple((k, tuple(v)) for k, v in sorted(fields.items()))
    except (OSError, ValueError):
        return None


def replace_file(destination, *, data=None, link=None):
    # Replace the directory entry, rather than writing through a symlink.
    with tempfile.TemporaryDirectory(prefix=".atmosphere-", dir=destination.parent) as folder:
        temporary = Path(folder) / "profile"
        if link is not None:
            temporary.symlink_to(link)
        else:
            assert data is not None
            temporary.write_bytes(data)
        os.replace(temporary, destination)


def write_edit_paths(home, entries):
    lines = ["// Generated atmosphere edit paths. Do not edit."]
    for entry in entries:
        name = entry["map"]
        path = home / (name + ".atmosphere")
        try:
            target = path.resolve().relative_to(home.resolve()).as_posix()
        except ValueError:
            target = name + ".atmosphere (custom link; inspect its target)"
        target = re.sub(r'[";\r\n]', ' ', target)
        lines.append(f'set ar_edit_{name} "echo Atmosphere file: maps/{target}"')
    replace_file(home.parent / "atmosphere-edit-paths.cfg", data=("\n".join(lines) + "\n").encode())


def prepare_profiles(package, profile, campaign):
    source = package / "OpenJK/maps"
    home = profile / "OpenJK/maps"
    home.mkdir(parents=True, exist_ok=True)
    catalogue = json.loads((package / "OpenJK/atmosphere-review/catalogue.json").read_text())
    entries = [m for m in catalogue["maps"] if m["campaign"] == campaign and m["palette"]]
    private_file = home / "shared/private-maps.json"
    private = set(json.loads(private_file.read_text())) if private_file.exists() else set()
    groups = defaultdict(list)
    for entry in entries:
        name = entry["map"] + ".atmosphere"
        target = (source / name).resolve().relative_to(source.resolve())
        if target == Path(name):
            if not (home / name).exists() and not (home / name).is_symlink():
                shutil.copyfile(source / name, home / name)
        else:
            groups[target].append(entry["map"])
    for relative, members in groups.items():
        target = home / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists() and not target.is_symlink():
            existing = [home / (m + ".atmosphere") for m in members
                        if m not in private and (home / (m + ".atmosphere")).is_file()
                        and not (home / (m + ".atmosphere")).is_symlink()]
            keys = [profile_key(p) for p in existing]
            # Carry forward a common local edit. Conflicting or invalid overrides
            # remain per-map files; do not choose one user's edit over another.
            origin = existing[0] if keys and keys[0] is not None and len(set(keys)) == 1 else source / relative
            shutil.copyfile(origin, target)
        key = profile_key(target)
        linked = []
        for name in members:
            destination = home / (name + ".atmosphere")
            if name in private:
                if not destination.exists() and not destination.is_symlink():
                    destination.write_bytes(target.read_bytes())
                continue
            if destination.is_symlink():
                if destination.resolve() == target.resolve():
                    linked.append(name)
                else:
                    print(f"Keep custom atmosphere link: {destination}")
                continue
            if destination.exists():
                if key is None or profile_key(destination) != key:
                    print(f"Keep private atmosphere edit: {destination}")
                    continue
                backups = home / "atmosphere-backups"
                backups.mkdir(exist_ok=True)
                stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
                shutil.copyfile(destination, backups / f"{name}.{stamp}.atmosphere")
            replace_file(destination, link=os.path.relpath(target, destination.parent))
            linked.append(name)
        print(f"Shared atmosphere: {target} ({', '.join(linked) or 'private map overrides retained'})")
    write_edit_paths(home, entries)
    return entries


def detach_profile(profile, name, entries):
    if name not in {m["map"] for m in entries}:
        raise ValueError(f"No atmosphere for map: {name}")
    home = profile / "OpenJK/maps"
    destination = home / (name + ".atmosphere")
    marker = home / "shared/private-maps.json"
    marker.parent.mkdir(parents=True, exist_ok=True)
    private = set(json.loads(marker.read_text())) if marker.exists() else set()
    private.add(name)
    replace_file(marker, data=(json.dumps(sorted(private), indent=2) + "\n").encode())
    if destination.is_symlink():
        replace_file(destination, data=destination.read_bytes())
    write_edit_paths(home, entries)
    print(f"Private atmosphere: {destination}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", type=Path, help="Final campaign-specific home directory")
    parser.add_argument("campaign", choices=("ja", "jo"))
    parser.add_argument("--package", type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument("--detach", metavar="MAP", help="Keep one map as an independent editable copy")
    args = parser.parse_args()
    entries = prepare_profiles(args.package, args.profile, args.campaign)
    if args.detach:
        detach_profile(args.profile, args.detach, entries)
    print(f"Atmosphere overrides: {args.profile / 'OpenJK/maps'}")


if __name__ == "__main__":
    main()
