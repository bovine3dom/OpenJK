#!/usr/bin/env python3
"""Capture JA and JO windows. Check the optical response and live glass controls."""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parent.parent
SCENES = {
    "ja": ("t1_sour", [(7460, -2780, 556, 45), (7380, -2710, 556, 15),
                       (9150, -2710, 340, 0), (7580, -2580, 556, 225)],
           (7168, -3072, 556)),
    "jo": ("kejim_post", [(100, 160, 524, 180), (70, 260, 524, 150),
                          (1232, -430, -550, 90)],
           (100, 160, 524)),
}


def pixels(path):
    return subprocess.run([
        "ffmpeg", "-v", "error", "-xerror", "-i", str(path),
        "-frames:v", "1", "-pix_fmt", "rgb24", "-f", "rawvideo", "-",
    ], capture_output=True, check=True).stdout


def difference(a, b):
    return sum(abs(x - y) for x, y in zip(a, b)) / len(a)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=ROOT / "build/ready")
    parser.add_argument("--campaign", choices=("ja", "jo", "both"), default="both")
    parser.add_argument("--mode", choices=("automatic", "manual", "combined", "fallback"), default="automatic")
    parser.add_argument("--manual-count", type=int, default=1, help="Pad authored probes to exercise high automatic probe indices")
    parser.add_argument("--probe-budget", type=int, default=48)
    parser.add_argument("--expect-fallback", action="store_true", help="Check a deliberately insufficient probe budget")
    parser.add_argument("--models", action="store_true", help="Check rotated and scaled MD3 windows in vjun2")
    parser.add_argument("--msaa", action="store_true", help="Check glass with 4x MSAA")
    args = parser.parse_args()
    automatic = args.mode in ("automatic", "combined")
    manual = args.mode in ("manual", "combined")
    if not 1 <= args.probe_budget <= 255 or not 1 <= args.manual_count <= 255 or (args.expect_fallback and not automatic):
        parser.error("Use probe counts from 1 to 255; budget fallback requires automatic probes")
    package = args.package.resolve()
    output = ROOT / "build/smoke"
    output.mkdir(parents=True, exist_ok=True)
    suite = Path(tempfile.mkdtemp(prefix="glass.", dir=output))
    print(f"Glass results: {suite}", flush=True)
    results = {}
    campaigns = ("ja",) if args.models else ("ja", "jo") if args.campaign == "both" else (args.campaign,)
    for campaign in campaigns:
        mapname, views, probe = (("vjun2", [(835, 1500, 245, 250), (758, 1528, 245, 250)], (835, 1500, 245))
                                 if args.models else SCENES[campaign])
        home = suite / campaign
        profile = home / ("campaigns/jo/OpenJK" if campaign == "jo" else "OpenJK")
        profile.mkdir(parents=True)
        settings = dict(cl_renderer="rdsp-rend2", r_mode=-1, r_customwidth=960,
                        r_customheight=720, r_fullscreen=0, r_debugContext=1,
                        r_ignoreGLErrors=0, r_cubeMapping=int(manual),
                        r_glassProbes=int(automatic), r_glassProbeBudget=args.probe_budget,
                        r_glassExposure=2, r_glassDebug=0,
                        r_glassPlanar=1,
                        r_ext_multisample=4 if args.msaa else 0,
                        r_glass=1, r_glassReflection=1, r_glassRoughness=0.12,
                        r_ssao=0, r_autoExposure=0, r_dynamicGlow=0,
                        com_maxfps=60, s_initsound=0, developer=1, fx_freeze=2)
        (profile / "openjk_sp.cfg").write_text("".join(f'set {k} "{v}"\n' for k, v in settings.items()))
        (profile / "autoexec_sp.cfg").write_text("// Controlled window captures.\n")
        if manual:
            probes = profile / "cubemaps" / mapname
            probes.mkdir(parents=True)
            (probes / "env.json").write_text(json.dumps({"Cubemaps": [
                {"Name": f"window-test-{i}", "Position": probe, "Radius": 512}
                for i in range(args.manual_count)]}))
        commands = ["wait 150", "exitview", "god", "notarget", "noclip", "d_npcfreeze 1",
                    "con_notifytime -1", "cg_draw2D 0", "cg_drawGun 0", "cg_thirdPerson 0",
                    "give weaponnum 3", "wait 90", "weapon 3", "wait 420", "r_glassReflection 1"]
        for index, view in enumerate(views):
            commands += ["fixedtime 0", "setviewpos " + " ".join(map(str, view)), "wait 360", "r_we clear", "fixedtime 1"]
            for label, controls in (("stock", ["r_glass 0"]),
                                    ("thin", ["r_glass 1"]),
                                    ("open", ["r_glassReflection 0"]),
                                    ("restored", ["r_glassReflection 1"]),
                                    ("reflection", ["r_glassDebug 1"]),
                                    ("reflection_mesh", ["r_glassPlanar 0"]),
                                    ("reflection_restored", ["r_glassPlanar 1"]),
                                    ("rough", ["r_glassRoughness 0.8"]),
                                    ("rough_normal", ["r_glassDebug 0"]),
                                    ("assignment", ["r_glassDebug 2"]),
                                    ("attenuation", ["r_glassDebug 3", "r_glassRoughness 0.12"])):
                commands += controls + ["wait 10", f"screenshot_png glass_{index}_{label}", "wait 5"]
            commands += ["r_glassDebug 0", "r_glassRoughness 0.12"]
        commands += ["echo OJK_GLASS_DONE", "wait 5", "quit"]
        (profile / "glass-test.cfg").write_text("\n".join(commands) + "\n")
        env = dict(os.environ, OJK_PROFILE=str(home), SDL_VIDEODRIVER="offscreen",
                   EGL_PLATFORM="surfaceless", SDL_AUDIODRIVER="dummy",
                   OJK_JO_ASSETS=os.environ.get("OJK_JO_ASSETS", str(ROOT / "GameData_JO")))
        env.pop("LIBGL_ALWAYS_SOFTWARE", None)
        with (suite / f"{campaign}.log").open("w") as log:
            subprocess.run(["timeout", "--kill-after=5s", "300s", "bash", str(package / "launch-sp.sh"),
                            os.environ.get("OJK_ASSETS", str(ROOT / "GameData")),
                            "--campaign", campaign, "+devmap", mapname, "+exec", "glass-test.cfg"],
                           env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
        text = (suite / f"{campaign}.log").read_text(errors="replace")
        saved = (profile / "openjk_sp.cfg").read_text()
        if not re.search(rf'^seta r_glassProbeBudget "{args.probe_budget}"$', saved, re.M):
            raise RuntimeError("The renderer clamped or failed to save the requested probe budget")
        if ("OJK_GLASS_DONE" not in text or "----- rdsp-rend2 -----" not in text or
                re.search(r"trying to load fallback|GL_INVALID_|GL_OUT_OF_MEMORY|Couldn't compile|"
                          r"OpenGL -> [^\n]*\[(?:Error|Undefined)\]", text, re.I)):
            raise RuntimeError(f"Renderer failure: {campaign}")
        if args.models and automatic and "Glass model pane: models/map_objects/vjun/window.md3" not in text:
            raise RuntimeError("The model fixture did not discover MD3 windows")
        if automatic:
            stats = re.findall(r"Glass probes: (\d+) generated, (\d+) pane sides assigned \((\d+) shared\), (\d+) invalid, (\d+) budget fallback", text)
            if not stats or not 0 < int(stats[-1][0]) <= min(args.probe_budget, 255 - (args.manual_count if manual else 0)):
                raise RuntimeError("Invalid automatic probe count")
            if args.expect_fallback and int(stats[-1][4]) == 0:
                raise RuntimeError("The budget test did not exercise fallback")
            results[campaign + "_probes"] = dict(zip(("generated", "assigned", "shared", "invalid", "budget_fallback"), map(int, stats[-1])))
            indices = [int(value) for value in re.findall(r"Glass probe (\d+):", text)]
            if not indices or max(indices) > 255 or (manual and min(indices) <= args.manual_count):
                raise RuntimeError("Automatic probe indices overlap authored slots or exceed the sort key")
            results[campaign + "_probes"].update(first_index=min(indices), last_index=max(indices))
        for index in range(len(views)):
            images = {label: pixels(profile / "screenshots" / f"glass_{index}_{label}.png")
                      for label in ("stock", "thin", "open", "restored", "reflection", "reflection_mesh", "reflection_restored",
                                    "rough", "rough_normal", "assignment", "attenuation")}
            if any(len(image) != 960 * 720 * 3 for image in images.values()):
                raise RuntimeError("Unexpected capture dimensions")
            metrics = {"stock_to_open": difference(images["stock"], images["open"]),
                       "thin_to_open": difference(images["thin"], images["open"]),
                       "restore_error": difference(images["thin"], images["restored"])}
            mask = [p for p in range(0, len(images["assignment"]), 3)
                    if images["assignment"][p + 1] > max(32, images["assignment"][p] * 2, images["assignment"][p + 2] * 2)
                    and sum(abs(images["assignment"][p + c] - images["thin"][p + c]) for c in range(3)) > 24]
            if args.models:
                # The player can occlude this close model fixture. Exclude its
                # moving silhouette and compare only stable window interiors.
                interior = set(mask)
                mask = [p for p in mask if 8 <= p // 3 % 960 < 952 and 8 <= p // 2880 < 712
                        and all(p + (dy * 960 + dx) * 3 in interior for dy in (-8, 0, 8) for dx in (-8, 0, 8))]
            if mask:
                def masked(label):
                    return bytes(images[label][p + c] for p in mask for c in range(3))
                reflection = sorted(sum(images["reflection"][p:p + 3]) / 3 for p in mask)
                metrics.update(probe_pixels=len(mask), reflection_range=reflection[int(len(mask) * 0.99)] - reflection[int(len(mask) * 0.01)],
                               roughness_effect=difference(masked("reflection"), masked("rough")),
                               normal_roughness_effect=difference(masked("thin"), masked("rough_normal")),
                               reflected_light=(sum(masked("thin")) - sum(masked("attenuation"))) / (3 * len(mask)))
                metrics["planar_restore_error"] = difference(masked("reflection"), masked("reflection_restored"))
                if args.models:
                    metrics["curved_mapping_error"] = difference(masked("reflection"), masked("reflection_mesh"))
                    if metrics["curved_mapping_error"] > 0.2:
                        raise RuntimeError("Planar correction changed curved model reflections")
            print(campaign, index, metrics, flush=True)
            results[f"{campaign}_{index}"] = metrics
            if metrics["thin_to_open"] <= 0.01:
                raise RuntimeError("No glass response")
            if automatic and not args.expect_fallback and (len(mask) < 1000 or metrics["reflection_range"] < 15 or
                    metrics["roughness_effect"] < 0.25 or metrics["normal_roughness_effect"] < 0.25 or metrics["reflected_light"] < 1):
                raise RuntimeError("Missing local probe, reflected detail, or roughness response")
            if metrics["restore_error"] > 0.2:
                raise RuntimeError("Live glass controls did not restore the image")
            if metrics.get("planar_restore_error", 0) > 0.2:
                raise RuntimeError("Live planar control did not restore its reflection")
    (suite / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    print("PASS: window reflection captures and live control restoration")


if __name__ == "__main__":
    main()
