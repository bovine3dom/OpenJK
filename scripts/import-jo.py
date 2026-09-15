#!/usr/bin/env python3
"""Build a local JO campaign overlay for the Academy runtime."""

import argparse
from contextlib import ExitStack
import hashlib
import json
from pathlib import Path
import re
import struct
import tempfile
import zipfile


def index_assets(root, stack):
    index = {}
    for path in sorted((root / "base").glob("assets*.pk3")):
        archive = stack.enter_context(zipfile.ZipFile(path))
        for entry in archive.infolist():
            if not entry.is_dir():
                index[entry.filename.lower()] = (archive, entry)
    return index


def read(index, name):
    archive, entry = index[name]
    return archive.read(entry)


def strings(data):
    """Read the English text and references from a retail STRIP package."""
    text = data.decode("cp1252", errors="replace")
    return re.findall(r'\bREFERENCE\s+(\w+)\s+TEXT_LANGUAGE1\s+"((?:\\.|[^"\\])*)"', text)


def stringed(entries):
    lines = ['VERSION "1"', 'CONFIG ""', 'FILENOTES "JO import"']
    for name, text in entries:
        lines.extend((f"REFERENCE {name}", f'LANG_ENGLISH "{text}"'))
    return ("\n".join(lines) + "\nENDMARKER\n").encode("cp1252")


def convert_script(data, aliases):
    """Change animation names without changing ICARUS block or member IDs."""
    if data[:8] != b"IBI\0" + struct.pack("<f", 1.57):
        raise ValueError("Unsupported ICARUS version")
    output = bytearray(data[:8])
    pos = 8
    while pos < len(data):
        _, count, _ = struct.unpack_from("<iiB", data, pos)
        output.extend(data[pos:pos + 9])
        pos += 9
        if count < 0:
            raise ValueError("Invalid ICARUS member count")
        for _ in range(count):
            kind, size = struct.unpack_from("<ii", data, pos)
            pos += 8
            if size < 0 or pos + size > len(data):
                raise ValueError("Invalid ICARUS member size")
            value = data[pos:pos + size]
            pos += size
            value = aliases.get(value, value)
            output.extend(struct.pack("<ii", kind, len(value)))
            output.extend(value)
    return output


def build_overlay(ja, jo, output):
    with ExitStack() as stack:
        academy = index_assets(ja, stack)
        outcast = index_assets(jo, stack)
        for key in ("maps/kejim_post.bsp", "maps/kejim_base.bsp", "ext_data/npcs.cfg"):
            if key not in outcast:
                raise ValueError(f"Missing JO asset: {key}")
        humanoid = "models/players/_humanoid/"
        ja_anims = read(academy, humanoid + "animation.cfg")
        jo_anims = read(outcast, humanoid + "animation.cfg")
        # Cockpit actors use JO's 72-bone skeleton. Gameplay actors use JA's
        # skeleton and the renderer's existing JO mesh conversion.
        cockpit = sorted(set(re.findall(rb"BOTH_COCKPIT_\w+", jo_anims)))
        if len(cockpit) > 50:
            raise ValueError("Too many cockpit animations")
        aliases = {name + b"\0": f"BOTH_CIN_{i + 1}".encode() + b"\0"
                   for i, name in enumerate(cockpit)}
        cinematic_cfg = jo_anims
        for old, new in aliases.items():
            cinematic_cfg = re.sub(rb"\b" + old[:-1] + rb"\b", new[:-1], cinematic_cfg)

        with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED, compresslevel=1) as dest:
            # Keep JA UI, weapon definitions, and humanoid gameplay animations.
            # JO supplies world content, dialogue, and its character appearances.
            roots = ("maps/", "scripts/", "textures/", "shaders/", "sound/", "music/",
                     "video/", "effects/", "models/", "gfx/", "menu/", "levelshots/")
            for name in sorted(outcast):
                if not name.startswith(roots) or name.startswith((humanoid, "models/weapons2/")):
                    continue
                data = read(outcast, name)
                if name.endswith(".ibi"):
                    data = convert_script(data, aliases)
                dest.writestr(name, data)

            dest.writestr("ext_data/dms.dat", read(outcast, "ext_data/dms.dat"))

            npcs = read(outcast, "ext_data/npcs.cfg").decode("cp1252")
            for actor in ("kyle", "jan"):
                model = bytearray(read(outcast, f"models/players/{actor}/model.glm"))
                animation = b"models/players/jo_cinematic/jo_cinematic"
                model[72:136] = animation.ljust(64, b"\0")
                dest.writestr(f"models/players/jo_cinematic_{actor}/model.glm", model)
                dest.writestr(f"models/players/jo_cinematic_{actor}/model_default.skin",
                              read(outcast, f"models/players/{actor}/model_default.skin"))
                definition = re.search(r"(?im)^\s*" + actor + r"\s*\{[^}]*\}", npcs)
                if not definition:
                    raise ValueError(f"Missing NPC definition: {actor}")
                clone = re.sub(r"(?i)\b" + actor + r"\b", f"jo_cinematic_{actor}",
                               definition[0], count=1)
                clone = re.sub(r"(?i)(playerModel\s+)" + actor + r"\b",
                               rf"\g<1>jo_cinematic_{actor}", clone)
                npcs += "\n" + clone
            dest.writestr("ext_data/jo/npcs.cfg", npcs.encode("cp1252"))
            cinematic_gla = bytearray(read(outcast, humanoid + "_humanoid.gla"))
            cinematic_gla[8:72] = b"models/players/jo_cinematic/jo_cinematic.gla".ljust(64, b"\0")
            dest.writestr("models/players/jo_cinematic/jo_cinematic.gla", cinematic_gla)
            dest.writestr("models/players/jo_cinematic/animation.cfg", cinematic_cfg)

            bsp = read(outcast, "maps/kejim_post.bsp")
            start, size = struct.unpack_from("<ii", bsp, 8)
            entities = bsp[start:start + size].rstrip(b"\0").decode("cp1252")
            def cinematic_actor(match):
                entity = match[0]
                actor = re.search(r'"NPC_targetname"\s+"cinematic1_(kyle|jan)"', entity)
                if actor:
                    entity = re.sub(r'"classname"\s+"NPC_[^"]+"',
                                    '"classname" "NPC_spawner"', entity)
                    entity = re.sub(r'"NPC_type"\s+"[^"]+"', "", entity, flags=re.I)
                    entity = entity[:-1] + f'"NPC_type" "jo_cinematic_{actor[1]}"\n}}'
                return entity
            entities = re.sub(r"\{[^}]*\}", cinematic_actor, entities)
            dest.writestr("maps/kejim_post.ent", entities.encode("cp1252"))

            for name in sorted(outcast):
                if not name.startswith("strip/") or not name.endswith(".sp"):
                    continue
                entries = strings(read(outcast, name))
                if not entries:
                    continue
                package = Path(name).stem
                string_path = f"strings/english/{package}.str"
                merged = {}
                if string_path in academy:
                    merged.update(re.findall(r'\bREFERENCE\s+(\w+)\s+LANG_ENGLISH\s+"((?:\\.|[^"\\])*)"',
                                             read(academy, string_path).decode("cp1252")))
                merged.update(entries)
                dest.writestr(string_path, stringed(merged.items()))
                if package == "objectives":
                    dest.writestr("ext_data/jo/objectives.dat",
                                  "\n".join(key for key, _ in entries) + "\n")
            # Use JA's menu flow and controls, but bypass character creation.
            for name in ("ui/newgame.menu", "ui/newgame2.menu"):
                if name in academy:
                    menu = read(academy, name)
                    menu = re.sub(rb"open\s+characterMenu\s*;", b"uiScript startgame ;", menu)
                    dest.writestr(name, menu)
            dest.writestr(humanoid + "animation.cfg", ja_anims)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("academy", type=Path)
    parser.add_argument("outcast", type=Path)
    parser.add_argument("profile", type=Path)
    args = parser.parse_args()
    sources = []
    for root, required in ((args.academy, (0, 1, 2, 3)), (args.outcast, (0, 1, 2, 5))):
        for number in required:
            if not (root / "base" / f"assets{number}.pk3").is_file():
                parser.error(f"Missing {root}/base/assets{number}.pk3")
        sources.extend(sorted((root / "base").glob("assets*.pk3")))
    for root in (args.academy, args.outcast):
        if args.profile.resolve().is_relative_to(root.resolve()):
            parser.error("The import profile must be outside the original game directories")
    signature = hashlib.sha256(Path(__file__).read_bytes() + json.dumps(
        [(str(p.resolve()), p.stat().st_size, p.stat().st_mtime_ns) for p in sources]).encode()).hexdigest()
    folder = args.profile / "OpenJK"
    folder.mkdir(parents=True, exist_ok=True)
    target = folder / "zz_jo_campaign.pk3"
    stamp = folder / "jo-import.json"
    if target.is_file() and stamp.is_file() and stamp.read_text() == signature:
        return
    print(f"Importing JO campaign assets into {target}", flush=True)
    with tempfile.TemporaryDirectory(prefix="jo-import-", dir=folder) as temp:
        output = Path(temp) / target.name
        build_overlay(args.academy, args.outcast, output)
        output.replace(target)
    stamp.write_text(signature)


if __name__ == "__main__":
    main()
