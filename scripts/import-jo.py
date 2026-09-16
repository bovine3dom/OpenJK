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

UI_PREFIXES = ("gfx/menus/", "gfx/hud/", "gfx/2d/")
NPC_CLASSES = {"GALAK_MECH": "GALAKMECH", "MORGAN": "MORGANKATARN"}
CINEMATIC_GESTURES = (b"BOTH_TALKGESTURE11START", b"BOTH_TALKGESTURE11STOP", b"BOTH_TALKGESTURE2")
# Slots 45-50 are shared with the Galak controller in codeJK2/game/AI_GalakMech.cpp.
GALAK_ANIMATIONS = (b"BOTH_ALERT1", b"TORSO_RAISEWEAP2", b"TORSO_DROPWEAP2",
                    b"BOTH_TRIUMPHANT1START", b"BOTH_TRIUMPHANT1STARTGESTURE", b"BOTH_TRIUMPHANT1STOP")
# These two retail script names have no clips in the supplied animation sets.
SCRIPT_ANIMATION_REPLACEMENTS = {b"BOTH_SCARED1\0": b"BOTH_CROUCH3\0", b"BOTH_DEADFORWARD1\0": b"BOTH_DEAD1\0"}


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


def convert_npcs(text):
    def npc_class(match):
        name = match[2].upper().removeprefix("CLASS_")
        return match[1] + "CLASS_" + NPC_CLASSES.get(name, name)
    text = re.sub(r'(?im)^([ \t]*class[ \t]+)"?(\w+)"?', npc_class, text)
    return re.sub(r'(?im)^(\s*surf(?:On|Off)\s+)([^\r\n]+)',
                  lambda m: m[1] + surface_names(m[2]), text)


def surface_names(text):
    # JA strips _off from mesh and skin names. JO uses it for distinct surfaces.
    return re.sub(r'\b(head(?:_face|_eyes_mouth)?)_off\b', r'\1_alt', text)


def convert_model(data):
    model = bytearray(data)
    count, pos = struct.unpack_from("<ii", model, 152)
    for _ in range(count):
        name = model[pos:pos + 64].split(b"\0", 1)[0].decode("ascii")
        model[pos:pos + 64] = surface_names(name).encode().ljust(64, b"\0")
        children, = struct.unpack_from("<i", model, pos + 140)
        pos += 144 + children * 4
    return model


def convert_skin(data):
    return re.sub(rb'(?m)^([^,\r\n]+),',
                  lambda m: surface_names(m[1].decode("ascii")).encode() + b",", data)


def script_aliases(jo_anims):
    cockpit = sorted(set(re.findall(rb"BOTH_COCKPIT_\w+", jo_anims)))
    cinematic = cockpit + list(CINEMATIC_GESTURES)
    if len(cinematic) > 44:
        raise ValueError("Too many legacy cinematic aliases")
    aliases = {name + b"\0": f"BOTH_CIN_{i + 1}".encode() + b"\0" for i, name in enumerate(cinematic)}
    aliases.update({name + b"\0": f"BOTH_CIN_{i + 45}".encode() + b"\0" for i, name in enumerate(GALAK_ANIMATIONS)})
    aliases.update(SCRIPT_ANIMATION_REPLACEMENTS)
    return aliases


def cinematic_animation_config(data, aliases):
    # Keep every original name, plus aliases used by earlier imports and the boss controller.
    extra = []
    for line in data.splitlines():
        fields = line.split(maxsplit=1)
        if len(fields) == 2 and fields[0] + b"\0" in aliases:
            extra.append(aliases[fields[0] + b"\0"][:-1] + b" " + fields[1])
    return data.rstrip() + b"\n" + b"\n".join(extra) + (b"\n" if extra else b"")


def write_cinematic_npcs(dest, outcast, npcs):
    models = set()
    clones = []
    for definition in re.finditer(r'(?im)^\s*(\w+)\s*\{([^}]+)\}', npcs):
        actor, body = definition.groups()
        field = re.search(r'(?im)^\s*playerModel\s+"?(\w+)', body)
        if not field:
            continue
        model = field[1].lower()
        path = f"models/players/{model}/model.glm"
        if path not in outcast:
            continue
        original = read(outcast, path)
        if original[72:136].split(b"\0", 1)[0] != b"models/players/_humanoid/_humanoid":
            continue
        clone = "jo_cinematic_" + model
        if model not in models:
            models.add(model)
            mesh = convert_model(original)
            mesh[72:136] = b"models/players/jo_cinematic/jo_cinematic".ljust(64, b"\0")
            dest.writestr(f"models/players/{clone}/model.glm", mesh)
            for skin in sorted(outcast):
                if skin.startswith(f"models/players/{model}/") and skin.endswith(".skin"):
                    dest.writestr(skin.replace(f"/{model}/", f"/{clone}/", 1), convert_skin(read(outcast, skin)))
        body = re.sub(r'(?im)^(\s*playerModel\s+)"?' + re.escape(field[1]) + r'"?',
                      lambda m: m[1] + clone, body)
        clones.append(f"\njo_cinematic_{actor.lower()}\n{{{body}}}\n")
    return npcs + "".join(clones)


def shader_definitions(data):
    text = data.decode("latin1")
    tokens = [m for m in re.finditer(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|[{}]|[^\s{}"]+', text, re.S)
              if not m[0].startswith(("//", "/*"))]
    i = 0
    while i < len(tokens):
        name = tokens[i]
        i += 1
        if i == len(tokens) or tokens[i][0] != "{":
            raise ValueError(f"Missing shader body: {name[0]}")
        depth = 0
        while i < len(tokens):
            token = tokens[i]
            i += 1
            if token[0] == "{": depth += 1
            if token[0] == "}": depth -= 1
            if depth == 0: break
        if depth:
            raise ValueError(f"Unclosed shader: {name[0]}")
        yield name[0].strip('"').lower(), text[name.start():tokens[i - 1].end()].encode("latin1")


def write_shaders(dest, academy, outcast):
    definitions = {}
    paths = set()
    for assets in (academy, outcast):
        for path in sorted(assets):
            if not path.startswith("shaders/") or not path.endswith(".shader"):
                continue
            paths.add(path)
            for name, body in shader_definitions(read(assets, path)):
                # Shared UI keeps JA blending; JO world materials take precedence.
                if assets is outcast and name.startswith(UI_PREFIXES) and name in definitions:
                    continue
                definitions[name] = body
    # Shadow the source files so duplicate names in other files cannot win by load order.
    for path in sorted(paths):
        dest.writestr(path, b"// Definitions merged into jo_campaign.shader\n")
    dest.writestr("shaders/jo_campaign.shader", b"\n\n".join(definitions.values()) + b"\n")


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
        aliases = script_aliases(jo_anims)

        with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED, compresslevel=1) as dest:
            # Keep JA UI, weapon definitions, and humanoid gameplay animations.
            # JO supplies world content, dialogue, and its character appearances.
            roots = ("maps/", "scripts/", "textures/", "sound/", "music/",
                     "video/", "effects/", "models/", "gfx/", "menu/", "levelshots/")
            for name in sorted(outcast):
                if not name.startswith(roots) or name.startswith((humanoid, "models/weapons2/")):
                    continue
                if name in academy and name.startswith(UI_PREFIXES):
                    continue
                data = read(outcast, name)
                if name.endswith(".ibi"):
                    data = convert_script(data, aliases)
                elif name.endswith(".glm"):
                    data = convert_model(data)
                elif name.endswith(".skin"):
                    data = convert_skin(data)
                elif name.endswith("/animation.cfg"):
                    data = cinematic_animation_config(data, aliases)
                dest.writestr(name, data)

            write_shaders(dest, academy, outcast)
            dest.writestr("ext_data/dms.dat", read(outcast, "ext_data/dms.dat"))

            npcs = convert_npcs(read(outcast, "ext_data/npcs.cfg").decode("cp1252"))
            npcs = write_cinematic_npcs(dest, outcast, npcs)
            dest.writestr("ext_data/jo/npcs.cfg", npcs.encode("cp1252"))
            cinematic_gla = bytearray(read(outcast, humanoid + "_humanoid.gla"))
            cinematic_gla[8:72] = b"models/players/jo_cinematic/jo_cinematic.gla".ljust(64, b"\0")
            dest.writestr("models/players/jo_cinematic/jo_cinematic.gla", cinematic_gla)
            dest.writestr("models/players/jo_cinematic/animation.cfg", cinematic_animation_config(jo_anims, aliases))

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
