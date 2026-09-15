#!/usr/bin/env python3
"""Report JO content references and possible Academy compatibility gaps."""

import argparse
from collections import Counter
from contextlib import ExitStack
import importlib.util
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("import_jo", Path(__file__).with_name("import-jo.py"))
assert spec is not None and spec.loader is not None
jo = importlib.util.module_from_spec(spec)
spec.loader.exec_module(jo)


def blocks(data):
    if data[:8] != b"IBI\0" + struct.pack("<f", 1.57):
        raise ValueError("Unsupported ICARUS header")
    pos = 8
    while pos < len(data):
        start = pos
        op, count, flags = struct.unpack_from("<iiB", data, pos)
        pos += 9
        if count < 0:
            raise ValueError("Invalid ICARUS member count")
        values = []
        for _ in range(count):
            kind, size = struct.unpack_from("<ii", data, pos)
            pos += 8
            if size < 0 or pos + size > len(data):
                raise ValueError("Invalid ICARUS member size")
            value = data[pos:pos + size]
            pos += size
            if kind in (4, 7):
                value = value.rstrip(b"\0").decode("cp1252")
            elif kind in (5, 6) and size == 4:
                value = struct.unpack("<f", value)[0]
            else:
                value = {"kind": kind, "hex": value.hex()}
            values.append(value)
        yield {"offset": start, "op": op, "flags": flags, "values": values}


def uncomment(text):
    return re.sub(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*.*?\*/',
                  lambda m: m[0] if m[0].startswith('"') else " " * len(m[0]), text, flags=re.S)


def source_tables(folder):
    functions, flags = {}, {}
    for path in sorted(folder.glob("*.cpp")):
        original = path.read_text(errors="replace")
        text = uncomment(original)
        for match in re.finditer(r'\bvoid\s+(SP_\w+)\s*\([^;{}]*\)\s*\{', text):
            pos, depth = match.end(), 1
            while depth and pos < len(text):
                depth += (text[pos] == "{") - (text[pos] == "}")
                pos += 1
            body = text[match.end():pos - 1].strip()
            functions[match[1]] = {"source": f"{path.relative_to(ROOT)}:{original.count(chr(10), 0, match.start()) + 1}",
                                   "empty": body in ("", "return;"), "body": body}
        for match in re.finditer(r'/\*QUAKED\s+(\S+)[^\n]*', original):
            header = match[0].rsplit(")", 1)[-1].replace("?", "").split()
            flags[match[1].lower()] = header
    spawn = (folder / "g_spawn.cpp").read_text()
    handlers = {name.lower(): handler for name, handler in
                re.findall(r'\{\s*"([^"]+)"\s*,\s*(SP_\w+)\s*\}', spawn)}
    return handlers, functions, flags


def script_path(value):
    return "scripts/" + value.lower().replace("\\", "/").removeprefix("scripts/").removesuffix(".ibi") + ".ibi"


def audit(academy, outcast):
    handlers, functions, flags = source_tables(ROOT / "code/game")
    old_handlers, _, old_flags = source_tables(ROOT / "codeJK2/game")
    game_text = "\n".join(p.read_text(errors="replace") for p in (ROOT / "code/game").glob("*.cpp"))
    findings, maps, scripts = [], {}, {}

    def issue(kind, source, reference, detail):
        findings.append(dict(kind=kind, source=source, reference=reference, detail=detail))

    with ExitStack() as stack:
        ja, assets = jo.index_assets(academy, stack), jo.index_assets(outcast, stack)
        if "maps/kejim_post.bsp" not in assets:
            raise ValueError("Missing JO campaign assets")
        available = set(ja) | set(assets)
        item_names = set(re.findall(r'(?m)^\s*classname\s+"?(\w+)',
                                   uncomment(jo.read(ja, "ext_data/items.dat").decode("cp1252"))))
        old_items = set(re.findall(r'(?m)^\s*classname\s+"?(\w+)',
                                  uncomment(jo.read(assets, "ext_data/items.dat").decode("cp1252"))))
        anim_names = set(re.findall(r'\b(?:BOTH|TORSO|LEGS)_\w+', jo.read(ja, "models/players/_humanoid/animation.cfg").decode()))
        translated = set(re.findall(r'\bBOTH_COCKPIT_\w+', jo.read(assets, "models/players/_humanoid/animation.cfg").decode()))
        translated.update(name.decode() for name in jo.CINEMATIC_GESTURES)
        translated.update(name.decode() for name in jo.GALAK_ANIMATIONS)
        npc_text = jo.convert_npcs(jo.read(assets, "ext_data/npcs.cfg").decode("cp1252"))
        npcs = {}
        for match in re.finditer(r'(\w+)\s*\{([^}]+)\}', uncomment(npc_text)):
            fields = dict((m[1].lower(), m[2].strip('"')) for m in re.finditer(r'(?m)^\s*(\w+)\s+("[^"\n]*"|\S+)', match[2]))
            npcs[match[1].lower()] = fields
            cls = fields.get("class")
            if cls and not re.search(r'\b' + re.escape(cls) + r'\b', game_text):
                issue("npc-class-review", "ext_data/npcs.cfg:" + match[1], cls, "No class reference in Academy game sources")
            model = fields.get("playermodel")
            if model and f"models/players/{model.lower()}/model.glm" not in available:
                issue("missing-npc-model", "ext_data/npcs.cfg:" + match[1], model, "Model is absent from both asset sets")

        for path in sorted(assets):
            if not path.startswith("scripts/") or not path.endswith(".ibi"):
                continue
            entries = list(blocks(jo.read(assets, path)))
            dependencies, progression = set(), []
            for block in entries:
                values = block["values"]
                source = f"{path}@{block['offset']}"
                for value in values:
                    if not isinstance(value, str):
                        continue
                    if value.startswith(("BOTH_", "TORSO_", "LEGS_")) and value not in anim_names | translated:
                        issue("animation-review", source, value, "No shared humanoid animation; check the actor's animation set")
                if block["op"] == 32 and values and isinstance(values[0], str):
                    dependencies.add(script_path(values[0]))
                if block["op"] == 26 and values and isinstance(values[0], str):
                    key = values[0]
                    if key.endswith("SCRIPT") and len(values) > 1 and isinstance(values[1], str) and values[1].lower() not in ("null", "none", ""):
                        dependencies.add(script_path(values[1]))
                    if any(word in key for word in ("FORCE", "SABER", "WEAPON", "OBJECTIVE", "INVENTORY", "VIDEO", "LOADGAME")):
                        progression.append(block)
                if block["op"] == 20 and len(values) > 1 and isinstance(values[1], str):
                    sound = values[1].lower()
                    candidates = {sound, sound.removesuffix(".wav") + ".mp3", sound.removesuffix(".mp3") + ".wav"}
                    if sound.startswith("sound/") and not candidates & available:
                        issue("missing-sound", source, sound, "No WAV or MP3 in either asset set")
            for dep in sorted(dependencies - available):
                issue("script-dependency-review", path, dep, "Missing literal dependency; check variable expressions")
            scripts[path] = {"dependencies": sorted(dependencies), "progression": progression}

        for path in sorted(assets):
            if (not path.startswith("maps/") or not path.endswith(".bsp") or "/mp/" in path
                    or Path(path).stem.startswith(("ctf_", "duel_", "ffa_"))):
                continue
            data = jo.read(assets, path)
            offset, size = struct.unpack_from("<ii", data, 8)
            text = data[offset:offset + size].rstrip(b"\0").decode("cp1252")
            entities, dependencies, transitions = [], set(), []
            for i, body in enumerate(re.findall(r'\{[^}]*\}', text)):
                ent = {k.lower(): v for k, v in re.findall(r'"([^"]*)"\s*"([^"]*)"', body)}
                source = f"{path}:entity[{i}]"
                cls = ent.get("classname", "").lower()
                handler = handlers.get(cls)
                if not handler and cls not in item_names and cls not in ("worldspawn", "info_null", "info_notnull", "light", "misc_model"):
                    issue("missing-handler" if cls in old_handlers or cls in old_items else "retail-missing-handler",
                          source, cls, "No Academy spawn-table or item entry")
                if handler and functions.get(handler, {}).get("empty"):
                    issue("empty-handler", source, handler, functions[handler]["source"])
                if handler and re.search(r'if\s*\([^)]*spawnflags[^)]*\)\s*(?:\{\s*)?return\s*;', functions.get(handler, {}).get("body", "")):
                    issue("conditional-handler-review", source, handler, "Spawn-flag branch returns without spawning; " + functions[handler]["source"])
                selected = int(ent.get("spawnflags", "0"))
                for bit, (old, new) in enumerate(zip(old_flags.get(cls, []), flags.get(cls, []))):
                    if selected & (1 << bit) and old.lower() != new.lower():
                        issue("spawnflag-review", source, f"{cls}:{1 << bit}", f"JO label {old}; JA label {new}; compare handler behavior")
                for key, value in ent.items():
                    if key.endswith("script") and value:
                        dependencies.add(script_path(value))
                    if key in ("map", "mapname"):
                        transitions.append({"entity": i, "map": value})
                        if f"maps/{value.lower()}.bsp" not in available:
                            issue("missing-map", source, value, "Transition map is absent")
                    if key == "npc_type" and value.lower() not in npcs and value.lower() != "random":
                        issue("npc-definition-review", source, value, "No imported NPC definition; check shared or generated definitions")
                    if (cls != "misc_model" and value.lower().startswith(("models/", "effects/"))
                            and Path(value).suffix and value.lower() not in available):
                        issue("missing-asset", source + ":" + key, value, "Asset is absent from both sets")
                if cls.startswith("npc_") or cls in item_names or cls == "target_level_change" or any(k.endswith("script") for k in ent):
                    entities.append({"index": i, "handler": handler, **ent})
            for dep in sorted(dependencies - available):
                issue("missing-map-script", path, dep, "Map script is absent")
            reachable, pending = set(), list(dependencies)
            while pending:
                dep = pending.pop()
                if dep in reachable:
                    continue
                reachable.add(dep)
                pending.extend(scripts.get(dep, {}).get("dependencies", []))
            maps[path] = {"entities": entities, "scripts": sorted(reachable), "transitions": transitions,
                          "navigation": path.removesuffix(".bsp") + ".nav" in available}
    return {"limits": ["Static references do not establish runtime behavior.",
                       "Script branches and variable references require runtime checks.",
                       "Animation findings require actor-specific review; cinematic aliases require cinematic actors.",
                       "Spawn-flag labels are evidence for review, not proof of a defect.",
                       "Asset presence does not establish importer selection, material rendering, or model compatibility."],
            "counts": dict(Counter(f["kind"] for f in findings)), "findings": findings,
            "maps": maps, "npcs": npcs, "scripts": scripts}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--academy", type=Path, default=ROOT / "GameData")
    parser.add_argument("--outcast", type=Path, default=ROOT / "GameData_JO")
    parser.add_argument("--output", type=Path, default=ROOT / "build/jo-audit.json")
    args = parser.parse_args()
    report = audit(args.academy, args.outcast)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(f"Audited {len(report['maps'])} maps and {len(report['scripts'])} scripts: {args.output}")
    print(json.dumps(report["counts"], indent=2))


if __name__ == "__main__":
    main()
