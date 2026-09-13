#!/usr/bin/env python3
"""Inspect original JA map entities without extracting or changing game assets."""

import argparse
import collections
import json
import pathlib
import re
import shlex
import struct
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("map", help="Map name without extension")
    parser.add_argument("--assets", type=pathlib.Path, default=pathlib.Path("GameData/base"))
    parser.add_argument("--classes", default="", help="Class-name regular expression; omit for counts only")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_]+", args.map):
        parser.error("Invalid map name")

    # Restrict this tool to the original archive set, not mod filesystem emulation.
    found = {}
    for index in reversed(range(4)):
        path = args.assets / f"assets{index}.pk3"
        with zipfile.ZipFile(path) as archive:
            names = {name.lower(): name for name in archive.namelist()}
            for extension in ("bsp", "ent"):
                name = f"maps/{args.map}.{extension}".lower()
                if extension not in found and name in names:
                    found[extension] = (str(path), archive.read(names[name]))
    if "bsp" not in found:
        parser.error("Map not found in assets0.pk3 through assets3.pk3")
    archive, bsp = found["bsp"]
    if len(bsp) < 152 or struct.unpack_from("<4si", bsp) != (b"RBSP", 1):
        parser.error("Expected a JA RBSP version 1 header")
    offset, length = struct.unpack_from("<ii", bsp, 8)
    if offset < 152 or length < 0 or offset + length > len(bsp):
        parser.error("Invalid entity lump range")
    source, data = found.get("ent", (archive, bsp[offset:offset + length]))
    entities = []
    try:
        tokens = iter(shlex.split(data.rstrip(b"\0").decode("latin-1"), comments=False))
        for token in tokens:
            if token != "{":
                raise ValueError("Expected entity opening brace")
            entity = {}
            key = next(tokens)
            while key != "}":
                entity[key] = next(tokens)
                key = next(tokens)
            entities.append(entity)
    except (StopIteration, ValueError) as error:
        parser.error(f"Invalid entity text: {error}")
    result = {
        "map": args.map,
        "bsp_archive": archive,
        "entity_source": source + (":external .ent" if "ent" in found else ":BSP lump 0"),
        "classes": dict(sorted(collections.Counter(e.get("classname", "") for e in entities).items())),
    }
    if args.classes:
        pattern = re.compile(args.classes, re.IGNORECASE)
        result["entities"] = [dict(bsp_entity=i, **e) for i, e in enumerate(entities)
                              if pattern.search(e.get("classname", ""))]
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
