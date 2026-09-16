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
                  lambda m: m[0] if m[0].startswith('"') else re.sub(r'[^\r\n]', ' ', m[0]), text, flags=re.S)


def braced_body(text, start):
    depth = 0
    for token in re.finditer(r'"(?:\\.|[^"\\])*"|[{}]', text[start:]):
        if token[0] == "{": depth += 1
        elif token[0] == "}": depth -= 1
        if depth == 0:
            return text[start + 1:start + token.start()]
    raise ValueError("Unclosed source body")


def script_interface(path, legacy=False):
    text = uncomment(path.read_text(errors="replace"))
    registered = set(re.findall(r'ENUM2STRING\((SET_\w+)\)', text))
    operations = {}
    for operation, method in (("set", "Set"), ("get-float", "GetFloat"), ("get-string", "GetString"), ("get-vector", "GetVector")):
        symbol = ("Q3_" if legacy else "CQuake3GameInterface::") + method
        match = re.search(r'\b' + symbol + r'\s*\([^{};]*\)\s*\{', text)
        if not match:
            raise ValueError(f"Missing script dispatcher: {symbol}")
        body = braced_body(text, match.end() - 1)
        code = re.sub(r'"(?:\\.|[^"\\])*"', lambda m: " " * len(m[0]), body)
        cases = list(re.finditer(r'\b(?:case\s+(SET_\w+)|default)\s*:', code))
        handlers = {}
        end, section = len(body), ""
        for case in reversed(cases):
            # Empty fall-through labels share the following implementation.
            section = body[case.end():end].strip() or section
            end = case.start()
            if case[1] is None: continue
            handlers[case[1]] = {"source": f"{path.relative_to(ROOT)}:{text.count(chr(10), 0, match.end() + case.start()) + 1}",
                                 "stub_review": section in ("break;", "return 0;", "return qfalse;") or "not implemented" in section.lower()}
        operations[operation] = handlers
    return registered, operations


def source_tables(folder):
    functions, flags = {}, {}
    for path in sorted(folder.glob("*.cpp")):
        original = path.read_text(errors="replace")
        text = uncomment(original)
        for match in re.finditer(r'\bvoid\s+(SP_\w+)\s*\([^;{}]*\)\s*\{', text):
            body = braced_body(text, match.end() - 1).strip()
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
    old_properties, old_dispatch = script_interface(ROOT / "codeJK2/game/Q3_Interface.cpp", legacy=True)
    properties, dispatch = script_interface(ROOT / "code/game/Q3_Interface.cpp")
    reviews = json.loads((ROOT / "scripts/jo-behavior-reviews.json").read_text())
    behaviors = {}

    def property_reference(operation, name, source):
        key = operation + ":" + name
        if key not in behaviors:
            registered, handled = name in properties, name in dispatch[operation]
            old_handled = name in old_properties and name in old_dispatch[operation]
            status = "implemented" if registered and handled else "missing" if old_handled else "retail-reference-review"
            entry = {"operation": operation, "property": name, "status": status,
                     "registered": registered, "dispatched": handled, "jo_dispatched": old_handled,
                     "jo_stub_review": old_dispatch[operation].get(name, {}).get("stub_review", False),
                     "references": [], **dispatch[operation].get(name, {})}
            if entry.get("stub_review"): entry["status"] = "handler-review"
            if key in reviews and registered and handled:
                entry.update(reviews[key])
            behaviors[key] = entry
        if source is not None:
            behaviors[key]["references"].append(source)

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
        translated = set(re.findall(r'\b(?:BOTH|TORSO|LEGS)_\w+', jo.read(assets, "models/players/_humanoid/animation.cfg").decode()))
        translated.update(name.rstrip(b"\0").decode() for name in jo.SCRIPT_ANIMATION_REPLACEMENTS)
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
                    if key.startswith("SET_"):
                        property_reference("set", key, source)
                    if key.endswith("SCRIPT") and len(values) > 1 and isinstance(values[1], str) and values[1].lower() not in ("null", "none", ""):
                        dependencies.add(script_path(values[1]))
                    if any(word in key for word in ("FORCE", "SABER", "WEAPON", "OBJECTIVE", "INVENTORY", "VIDEO", "LOADGAME", "MISSION")):
                        progression.append(block)
                for i, value in enumerate(values[:-2]):
                    if isinstance(value, dict) and value.get("kind") == 36 and isinstance(values[i + 2], str):
                        operation = {4: "get-string", 6: "get-float", 14: "get-vector"}.get(values[i + 1])
                        if operation and values[i + 2].startswith("SET_"):
                            property_reference(operation, values[i + 2], source)
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
                        ref = f"{cls}:{1 << bit}"
                        issue("spawnflag-review", source, ref, f"JO label {old}; JA label {new}; compare handler behavior")
                        findings[-1].update(reviews.get("spawnflag:" + ref, {"status": "review-required"}))
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
    for operation, cases in old_dispatch.items():
        for name in sorted(old_properties & cases.keys()):
            property_reference(operation, name, None)
    for key, behavior in behaviors.items():
        if behavior["status"] in ("missing", "handler-review", "retail-reference-review"):
            for source in behavior["references"]:
                issue("script-" + behavior["status"], source, key, "Inspect registration and operation-specific dispatch")
    return {"limits": ["Static references do not establish runtime behavior.",
                       "A registered property or dispatcher case does not establish equivalent behavior.",
                       "Source inspection does not evaluate preprocessor branches or execute handlers.",
                       "Script branches and variable references require runtime checks.",
                       "Animation findings require actor-specific review; cinematic aliases require cinematic actors.",
                       "Spawn-flag labels are evidence for review, not proof of a defect.",
                       "Asset presence does not establish importer selection, material rendering, or model compatibility."],
            "counts": dict(Counter(f["kind"] for f in findings)), "findings": findings,
            "maps": maps, "npcs": npcs, "scripts": scripts, "behaviors": behaviors,
            "behavior_status_counts": dict(Counter(b["status"] for b in behaviors.values())),
            "used_behavior_status_counts": dict(Counter(b["status"] for b in behaviors.values() if b["references"]))}


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
    print("Referenced script behavior:", json.dumps(report["used_behavior_status_counts"], sort_keys=True))


if __name__ == "__main__":
    main()
