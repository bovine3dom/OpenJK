#!/usr/bin/env python3
"""Inspect JO script animations, actor models, and cinematic props."""

import argparse
from contextlib import ExitStack
import importlib.util
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
# These names occur only in this retail script, not in any map or spawn script.
RETAIL_ABSENT_ACTORS = {("scripts/cinematics/cinematic24.ibi", "cinematic24_stormtrooper1"),
                       ("scripts/cinematics/cinematic24.ibi", "cinematic24_stormtrooper2")}


def module(name):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(name + ".py"))
    assert spec and spec.loader
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


campaign = module("audit-jo")
jo = campaign.jo


def animations(data):
    return {m[1].upper(): tuple(map(float, m.groups()[1:])) for m in re.finditer(
        r"(?m)^\s*((?:BOTH|TORSO|LEGS|FACE)_\w+)\s+(-?\d+)\s+(\d+)\s+(-?\d+)\s+(-?[\d.]+)",
        campaign.uncomment(data.decode("cp1252")))}


def engine_names(folder):
    names = set(re.findall(r"ENUM2STRING\(((?:BOTH|TORSO|LEGS|FACE)_\w+)\)", (folder / "cgame/animtable.h").read_text()))
    extra = folder / "game/jo_anims.h"
    if extra.exists():
        names.update(re.findall(r"JO_ANIM\((\w+)\)", extra.read_text()))
    return names


def references(data):
    scopes = ["self"]
    for block in campaign.blocks(data):
        op, values = block["op"], block["values"]
        if op in (19, 27, 38, 39, 41):
            actor = values[0] if op == 19 and values and isinstance(values[0], str) else scopes[-1]
            scopes.append(actor.lower())
        elif op == 25 and len(scopes) > 1:
            scopes.pop()
        elif op == 26 and len(values) >= 2 and isinstance(values[0], str):
            key, value = values[:2]
            if key in ("SET_ANIM_BOTH", "SET_ANIM_UPPER", "SET_ANIM_LOWER"):
                if isinstance(value, str):
                    yield {"kind": "animation", "actor": scopes[-1], "name": value.upper(), "offset": block["offset"]}
                else:
                    yield {"kind": "expression", "actor": scopes[-1], "values": values[1:], "offset": block["offset"]}
            elif key in ("SET_ADDRHANDBOLT_MODEL", "SET_ADDLHANDBOLT_MODEL") and isinstance(value, str):
                yield {"kind": "prop", "actor": scopes[-1], "name": value.lower(), "offset": block["offset"]}


def audit(academy, outcast):
    engine = engine_names(ROOT / "code")
    old_engine = engine_names(ROOT / "codeJK2")
    base = campaign.audit(academy, outcast)
    _, functions, _ = campaign.source_tables(ROOT / "code/game")
    with ExitStack() as stack:
        ja, retail = jo.index_assets(academy, stack), jo.index_assets(outcast, stack)
        aliases = jo.script_aliases(jo.read(retail, "models/players/_humanoid/animation.cfg"))
        configs = {name: animations(jo.cinematic_animation_config(jo.read(retail, name), aliases))
                   for name in retail if name.endswith("/animation.cfg")}
        humanoid = configs["models/players/_humanoid/animation.cfg"]
        shared = animations(jo.read(ja, "models/players/_humanoid/animation.cfg"))
        all_frames = {name for config in configs.values() for name, frames in config.items() if frames[1] > 0 and frames[3] != 0}
        model_sets = {}
        for name in retail:
            if name.endswith(".glm"):
                data = jo.read(retail, name)
                skeleton = data[72:136].split(b"\0", 1)[0].decode("ascii").lower()
                model_sets[name] = str(Path(skeleton).parent / "animation.cfg")

        actor_models, actor_profiles = {}, {}
        for mapname, info in base["maps"].items():
            actors = {"kyle": {"models/players/kyle/model.glm"}}
            profiles = {}
            for ent in info["entities"]:
                if not ent.get("classname", "").lower().startswith("npc_"):
                    continue
                types = [ent["npc_type"].lower()] if ent.get("npc_type") else re.findall(
                    r'NPC_type\s*=\s*"([^"]+)"', functions.get(ent.get("handler"), {}).get("body", ""))
                if ent["classname"].lower() == "npc_galak":
                    types = ["galak_mech" if int(ent.get("spawnflags", "0")) & 1 else "galak"]
                models = set()
                for kind in types:
                    model = base["npcs"].get(kind.lower().removeprefix("jo_cinematic_"), {}).get("playermodel")
                    if model:
                        models.add(f"models/players/{model.lower()}/model.glm")
                label = ent.get("npc_targetname", ent.get("targetname", "" )).lower()
                if label:
                    actors.setdefault(label, set()).update(models)
                    profiles[label] = bool(int(ent.get("spawnflags", "0")) & 32) or label.startswith("cinematic")
            actor_models[mapname] = actors
            actor_profiles[mapname] = profiles

        refs, props, expressions = [], [], []
        for path in sorted(retail):
            if not path.startswith("scripts/") or not path.endswith(".ibi"):
                continue
            for ref in references(jo.read(retail, path)):
                ref["script"] = path
                ref["maps"] = [name for name, info in base["maps"].items() if path in info["scripts"]]
                if ref["kind"] == "expression":
                    expressions.append(ref)
                    continue
                models = set().union(*(actor_models[name].get(ref["actor"], set()) for name in ref["maps"]))
                ref["models"] = sorted(models)
                if ref["kind"] == "prop":
                    ref["present"] = ref["name"] in retail or ref["name"] in ja
                    props.append(ref)
                    continue
                name = ref["name"]
                effective = aliases.get(name.encode() + b"\0", name.encode() + b"\0").rstrip(b"\0").decode()
                ref["effective_name"] = effective
                ref["control_value"] = name == "-1"
                ref["engine_name"] = effective in engine or ref["control_value"]
                ref["retail_engine_name"] = name in old_engine
                ref["retail_frames"] = effective in all_frames or ref["control_value"]
                ref["humanoid_frames"] = effective in humanoid
                ref["shared_gameplay_frames"] = effective in shared
                ref["cinematic_profile"] = {name: actor_profiles[name].get(ref["actor"], False) for name in ref["maps"]}
                ref["actor_frame_sets"] = {model: effective in configs.get(model_sets.get(model), {})
                                           and configs[model_sets[model]][effective][1] > 0 for model in sorted(models)}
                refs.append(ref)

        missing = sorted({r["name"] for r in refs if not r["engine_name"]})
        no_frames = sorted({r["name"] for r in refs if not r["retail_frames"]})
        jo_only = sorted(old_engine - engine)
        return {
            "counts": {"animation_references": len(refs), "unique_animations": len({r["name"] for r in refs}),
                       "missing_engine_names": len(missing), "names_without_retail_frames": len(no_frames),
                       "prop_references": len(props), "animation_expressions": len(expressions)},
            "missing_engine_names": missing, "names_without_retail_frames": no_frames,
            "cinematic_actors_without_profile": sorted({(r["script"], r["actor"]) for r in refs
                if r["script"].startswith("scripts/cinematics/") and r["models"]
                and any(not enabled for enabled in r["cinematic_profile"].values())}),
            "unresolved_cinematic_actors": sorted({(r["script"], r["actor"]) for r in refs
                if r["script"].startswith("scripts/cinematics/") and r["maps"] and not r["models"]
                and (r["script"], r["actor"]) not in RETAIL_ABSENT_ACTORS}),
            "retail_absent_actors": sorted(RETAIL_ABSENT_ACTORS),
            "actor_animation_gaps": sorted({(r["script"], r["actor"], model, r["name"]) for r in refs
                if not r["control_value"] for model, present in r["actor_frame_sets"].items() if not present}),
            "additional_retail_engine_names": jo_only,
            "shared_names_with_different_frame_ranges": sorted(name for name in humanoid.keys() & shared.keys()
                                                               if humanoid[name] != shared[name]),
            "references": refs, "props": props, "expressions": expressions,
            "limits": ["Frame ranges refer to different skeleton files; equal names do not prove equal poses.",
                       "Actor lookup is conservative and does not execute script branches.",
                       "Self references and dynamically created actors require runtime checks.",
                       "Prop presence does not prove a correct attachment or pose."],
        }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--academy", type=Path, default=ROOT / "GameData")
    parser.add_argument("--outcast", type=Path, default=ROOT / "GameData_JO")
    parser.add_argument("--output", type=Path, default=ROOT / "build/jo-cinematic-audit.json")
    parser.add_argument("--check", action="store_true", help="Fail for unresolved animation or prop references")
    args = parser.parse_args()
    report = audit(args.academy, args.outcast)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report["counts"], indent=2))
    print(f"Report: {args.output}")
    if args.check and (any(report[key] for key in ("missing_engine_names", "names_without_retail_frames",
                      "cinematic_actors_without_profile", "unresolved_cinematic_actors", "actor_animation_gaps",
                      "additional_retail_engine_names")) or any(not prop["present"] for prop in report["props"])):
        raise SystemExit("Unresolved JO cinematic references; inspect the report")


if __name__ == "__main__":
    main()
