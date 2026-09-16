#!/usr/bin/env python3
"""Capture fixed t1_sour views and check the map atmosphere prototype."""

import argparse
import json
import os
from pathlib import Path
import re
import runpy
import subprocess
import tempfile
import zipfile


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=root / "build/ready")
    parser.add_argument("--stock-only", action="store_true")
    parser.add_argument("--msaa", type=int, choices=(0, 4), default=0)
    parser.add_argument("--profile", type=Path, help="Use an isolated profile override for art tuning")
    parser.add_argument("--invalid-profile", action="store_true", help="Check non-finite profile rejection")
    args = parser.parse_args()
    suite = Path(tempfile.mkdtemp(prefix="atmosphere.", dir=root / "build/smoke"))
    assets = Path(os.environ.get("OJK_ASSETS", root / "GameData"))
    images = {}
    for path in sorted((assets / "base").glob("*.pk3")):
        with zipfile.ZipFile(path) as archive:
            for name in archive.namelist():
                if re.fullmatch(r"textures/skies/desert_(rt|lf|bk|ft|up|dn)\.(jpg|png|tga)", name.lower()):
                    images[name.lower()] = archive.read(name)
    faces = []
    for suffix in ("rt", "lf", "bk", "ft", "up", "dn"):
        data = next(data for name, data in images.items() if f"desert_{suffix}." in name)
        faces.append(subprocess.run(["ffmpeg", "-v", "error", "-i", "pipe:0", "-vf", "scale=256:256",
            "-frames:v", "1", "-pix_fmt", "rgb24", "-f", "rawvideo", "-"],
            input=data, capture_output=True, check=True).stdout)
    subprocess.run(["ffmpeg", "-v", "error", "-f", "rawvideo", "-pixel_format", "rgb24", "-video_size",
                    "256x256", "-i", "pipe:0", "-vf", "tile=3x2", "-frames:v", "1", str(suite / "stock-sky.png")],
                   input=b"".join(faces), check=True)
    profile = suite / "profile/OpenJK"
    profile.mkdir(parents=True)
    if args.profile or args.invalid_profile:
        (profile / "maps").mkdir()
        text = (args.profile or (args.package / "OpenJK/maps/t1_sour.atmosphere")).read_text()
        if args.invalid_profile:
            text = re.sub(r"(?m)^radius\s+\S+", "radius nan", text)
        (profile / "maps/t1_sour.atmosphere").write_text(text)
    settings = dict(cl_renderer="rdsp-rend2", r_mode=-1, r_customwidth=960, r_customheight=720,
                    r_fullscreen=0, com_maxfps=30, developer=1, s_initsound=0, r_ignoreGLErrors=0,
                    r_debugContext=1, r_ext_multisample=args.msaa, r_autoExposure=0, r_dynamicGlow=1,
                    r_atmosphere=0, r_mapHaze=0, r_localFog=0)
    (profile / "openjk_sp.cfg").write_text("".join(f'set {k} "{v}"\n' for k, v in settings.items()))
    (profile / "autoexec_sp.cfg").write_text("// Isolated atmosphere fixture.\n")
    commands = ["wait 100", "exitview", "wait 100", "god", "give all", "wait 40", "weapon 3", "wait 80", "noclip", "d_npcfreeze 1",
                "cg_draw2D 0", "cg_drawGun 0", "cg_thirdPerson 0", "con_notifytime -1",
                "cg_bobup 0", "cg_bobpitch 0", "cg_bobroll 0", "r_drawentities 0"]
    views = {"interior": "4693 -1624 80 225", "street": "6328 -952 80 225",
             "interior_back": "7164 -3968 64 90",
             "roof": "4693 -1624 380 45", "sun": "4693 -1624 380 274"}
    for name, view in views.items():
        commands += [f"setviewpos {view}", "wait 60"]
        if name == "sun":
            commands += ["cl_pitchspeed 80", "+lookup", "wait 90", "-lookup", "wait 30"]
        commands += ["viewpos", "fixedtime 1"]
        for suffix, mode in (("stock", 0),) if args.stock_only else (("stock", 0), ("atmosphere", 1), ("restored", 0)):
            commands += [f"r_atmosphere {mode}", "wait 10", f"screenshot_png {name}_{suffix}", "wait 2"]
        if name == "roof" and not args.stock_only:
            commands += ["r_atmosphere 1"]
            for phase, mode in (("modern", 0), ("base", 1), ("split", 2)):
                commands += [f"r_compareEnhancements {mode}", "wait 10", f"screenshot_png compare_{phase}", "wait 2"]
                if mode == 1:
                    commands += ["r_atmosphere 0", "wait 10", "screenshot_png compare_base_stock", "wait 2", "r_atmosphere 1"]
            commands += ["r_compareEnhancements 0"]
        commands += ["fixedtime 0"]
    if not args.stock_only:
        commands += ["fixedtime 1", "r_atmosphere 1", "wait 10", "screenshot_png lifecycle", "wait 2",
                     "r_atmosphereReload", "r_atmosphereReload", "wait 10", "screenshot_png reloaded", "wait 2",
                     "imagelist", "vid_restart", "wait 200", "screenshot_png restarted", "wait 2",
                     "save atmosphere_test", "wait 5", "load atmosphere_test", "wait 200",
                     "screenshot_png loaded", "wait 2", "r_dynamicGlow 2", "wait 10",
                     "screenshot_png sky_glow", "wait 2", "r_dynamicGlow 1",
                     "fixedtime 0", "devmap t2_wedge", "wait 100", "exitview", "wait 100"]
    commands += ["echo OJK_ATMOSPHERE_DONE", "quit"]
    (profile / "atmosphere-test.cfg").write_text("\n".join(commands) + "\n")
    env = dict(os.environ, OJK_PROFILE=str(profile.parent), SDL_VIDEODRIVER="offscreen",
               EGL_PLATFORM="surfaceless", SDL_AUDIODRIVER="dummy")
    print(f"Atmosphere results: {suite}", flush=True)
    with (suite / "console.log").open("w") as stream:
        subprocess.run(["timeout", "--kill-after=5s", "600s", "bash", str(args.package.resolve() / "launch-sp.sh"),
                        str(assets), "+devmap", "t1_sour", "+exec", "atmosphere-test.cfg"], env=env,
                       stdout=stream, stderr=subprocess.STDOUT, check=True)
    text = (suite / "console.log").read_text(errors="replace")
    if "OJK_ATMOSPHERE_DONE" not in text or re.search(
            r"ERROR:|Unknown command|GL_INVALID_|GL_OUT_OF_MEMORY|trying to load fallback|"
            r"OpenGL -> [^\n]*\[(?:Error|Undefined)\]", text):
        raise RuntimeError(f"Atmosphere fixture failed: {suite}")
    if args.stock_only:
        return
    pixels = runpy.run_path(str(root / "scripts/test-sky-fog.py"))["pixels"]
    results = {}
    for name in views:
        images = [pixels(profile / "screenshots" / f"{name}_{mode}.png")
                  for mode in ("stock", "atmosphere", "restored")]
        results[name] = dict(delta=sum(abs(a-b) for a, b in zip(images[0], images[1])) / len(images[0]),
                             restore_error=sum(abs(a-b) for a, b in zip(images[0], images[2])) / len(images[0]))
        if name == "roof":
            results[name]["foreground_delta"] = sum(abs(a-b) for a, b in
                zip(images[0][80*160*3:], images[1][80*160*3:])) / (40*160*3)
    reference = pixels(profile / "screenshots/lifecycle.png")
    lifecycle = {}
    for name in ("reloaded", "restarted", "loaded"):
        image = pixels(profile / "screenshots" / f"{name}.png")
        lifecycle[name] = sum(abs(a-b) for a, b in zip(reference, image)) / len(reference)
    glow = pixels(profile / "screenshots/sky_glow.png")
    glow_max = max(glow[(y*160+x)*3+c] for y in range(20, 70) for x in range(50, 110) for c in range(3))
    if not args.invalid_profile and ("Atmosphere: t1_sour" not in text or text.count("*atmosphere:t1_sour") != 1):
        raise RuntimeError("Atmosphere table missing or duplicated during reload")
    if args.invalid_profile and ("Invalid atmosphere profile: t1_sour" not in text or
                                max(r["delta"] for r in results.values()) > 0.5):
        raise RuntimeError("Invalid profile did not select the stock sky")
    if (max(lifecycle.values()) > 1 or glow_max > 2 or results["roof"]["foreground_delta"] > 0.5 or
            any(results[name]["delta"] > 0.5 for name in ("interior", "interior_back"))):
        raise RuntimeError(f"Atmosphere lifecycle, bloom, or foreground mismatch: {lifecycle}, glow={glow_max}")
    comparisons = {name: pixels(profile / "screenshots" / f"compare_{name}.png") for name in ("base", "modern", "split")}
    stock = pixels(profile / "screenshots/compare_base_stock.png")
    results["roof"]["baseline_sky_error"] = sum(abs(stock[(y*160+x)*3+c] - comparisons["base"][(y*160+x)*3+c])
        for y in range(10, 25) for x in range(65, 95) for c in range(3)) / (15*30*3)
    if results["roof"]["baseline_sky_error"] > 1:
        raise RuntimeError("Base comparison retained the procedural sky")
    for name, left, right in (("base", 4, 76), ("modern", 84, 156)):
        error = sum(abs(comparisons[name][(y*160+x)*3+c] - comparisons["split"][(y*160+x)*3+c])
                    for y in range(4, 116) for x in range(left, right) for c in range(3)) / (112*(right-left)*3)
        if error > 1:
            raise RuntimeError(f"Sky portal split comparison mismatch: {name}: {error}")
        results["roof"][f"split_{name}_error"] = error
    report = dict(views=results, lifecycle=lifecycle, sky_glow_max=glow_max,
                  lut_build_ms=[int(t) for t in re.findall(r"256x128 LUT, (\d+) ms", text)])
    (suite / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(results, indent=2))
    print(dict(lifecycle=lifecycle, sky_glow_max=glow_max))
    if (not args.invalid_profile and max(r["delta"] for r in results.values()) < 5) or any(r["restore_error"] > 1 for r in results.values()):
        raise RuntimeError("Atmosphere has no clear effect or failed live restoration")
    subprocess.run(["ffmpeg", "-v", "error", "-i", str(profile / "screenshots/roof_stock.png"), "-i",
                    str(profile / "screenshots/roof_atmosphere.png"), "-filter_complex", "hstack", "-frames:v", "1",
                    str(suite / "roof-comparison.png")], check=True)
    print("PASS: atmosphere comparison, foreground preservation, reload, restart, load, and bloom")


if __name__ == "__main__":
    main()
