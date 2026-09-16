#!/usr/bin/env python3
"""Check sky orientation, live fallback, and renderer restart on headless EGL."""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile


def pixels(path):
    return subprocess.run(["ffmpeg", "-v", "error", "-i", str(path), "-vf", "scale=160:120",
                           "-frames:v", "1", "-pix_fmt", "rgb24", "-f", "rawvideo", "-"],
                          capture_output=True, check=True).stdout


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=root / "build/ready")
    parser.add_argument("--msaa", type=int, choices=(0, 4), default=0)
    parser.add_argument("--glow", type=int, choices=(0, 1), default=1, help="Enable bloom (default: 1)")
    parser.add_argument("--missing-glow-output", action="store_true",
                        help="Check that the renderer rejects the original incomplete sky shader")
    args = parser.parse_args()
    suite = Path(tempfile.mkdtemp(prefix="sky-fog.", dir=root / "build/smoke"))
    profile = suite / "profile/OpenJK"
    profile.mkdir(parents=True)
    settings = dict(cl_renderer="rdsp-rend2", r_mode=-1, r_customwidth=640, r_customheight=480,
                    r_fullscreen=0, com_maxfps=30, developer=1, s_initsound=0, r_ignoreGLErrors=0,
                    r_debugContext=1, r_ext_multisample=args.msaa, r_autoExposure=0, r_highResSkies=0,
                    r_mapHaze=0, r_dynamicGlow=args.glow)
    if args.missing_glow_output:
        settings["r_externalGLSL"] = 1
        (profile / "glsl").mkdir()
        shader = (root / "codemp/rd-rend2/glsl/skycube.glsl").read_text()
        (profile / "glsl/skycube.glsl").write_text(
            "\n".join(line for line in shader.splitlines() if "out_Glow" not in line) + "\n")
    (profile / "openjk_sp.cfg").write_text("".join(f'set {k} "{v}"\n' for k, v in settings.items()))
    (profile / "autoexec_sp.cfg").write_text("// Isolated sky fixture.\n")
    commands = ["exec krildor-traverse.cfg", "noclip", "d_npcfreeze 1", "cg_draw2D 0", "cg_drawGun 0",
                "cg_thirdPerson 0", "con_notifytime -1", "r_drawentities 0", "cg_bobup 0",
                "cg_bobpitch 0", "cg_bobroll 0", "cl_pitchspeed 80"]
    names = []
    for yaw, pitch in ((0, 0), (90, 0), (180, 0), (270, 0), (0, -80), (0, 80)):
        name = f"view_{yaw}_{pitch}"
        names.append(name)
        commands += [f"setviewpos 2688 640 512 {yaw}", "wait 40"]
        if pitch:
            look = "lookup" if pitch < 0 else "lookdown"
            commands += [f"+{look}", "wait 30", f"-{look}", "wait 40"]
        for mode in (0, 1):
            commands += [f"r_seamlessSky {mode}", "wait 5", f"screenshot_png {name}_{mode}", "wait 2"]
        if pitch == -80:
            for highres in (0, 1):
                commands += [f"r_highResSkies {highres}", "r_dynamicGlow 2", "wait 5",
                             f"screenshot_png glow_sky_{highres}", "wait 2"]
            commands += ["r_highResSkies 0", f"r_dynamicGlow {args.glow}"]
    commands += ["fixedtime 1", "wait 30", "screenshot_png baseline", "wait 2",
                 "vid_restart", "wait 200", "screenshot_png restarted", "wait 2",
                 "r_highResSkies 1", "wait 5", "screenshot_png highres", "wait 2",
                 "r_seamlessSky 0", "wait 5", "screenshot_png highres_clamped", "wait 2",
                 "r_seamlessSky 1",
                 "r_highResSkies 0", "r_mapHaze 1", "wait 5", "screenshot_png haze", "wait 2",
                 "r_seamlessSky 0", "wait 5", "screenshot_png haze_clamped", "wait 2",
                 "vid_restart", "wait 200", "screenshot_png haze_restart", "wait 2",
                 "r_mapHaze 0", "wait 5", "screenshot_png haze_off", "wait 2",
                 "r_localFog 1", "wait 5", "screenshot_png local_fog", "wait 2",
                 "r_seamlessSky 1", "wait 5", "screenshot_png local_seamless", "wait 2",
                 "vid_restart", "wait 200", "screenshot_png local_restart", "wait 2",
                 "r_localFog 0", "wait 5", "screenshot_png local_off", "wait 2", "echo OJK_SKY_DONE", "quit"]
    (profile / "sky-test.cfg").write_text("\n".join(commands) + "\n")
    env = dict(os.environ, OJK_PROFILE=str(profile.parent), SDL_VIDEODRIVER="offscreen",
               EGL_PLATFORM="surfaceless", SDL_AUDIODRIVER="dummy")
    print(f"Sky results: {suite}", flush=True)
    with (suite / "console.log").open("w") as stream:
        startup = ["+quit"] if args.missing_glow_output else ["+devmap", "t2_wedge", "+exec", "sky-test.cfg"]
        process = subprocess.run(["timeout", "--kill-after=5s", "600s", "bash",
                                  str(args.package.resolve() / "launch-sp.sh"),
                                  os.environ.get("OJK_ASSETS", str(root / "GameData")), *startup],
                                 env=env, stdout=stream, stderr=subprocess.STDOUT,
                                 check=not args.missing_glow_output)
    text = (suite / "console.log").read_text(errors="replace")
    if args.missing_glow_output:
        if process.returncode == 0 or "Sky shader must write scene color and glow outputs" not in text:
            raise RuntimeError("Incomplete sky shader was not rejected")
        print("PASS: incomplete sky shader rejected before rendering")
        return
    if "OJK_SKY_DONE" not in text or "Sky cube: textures/skies/wedge" not in text or re.search(
            r"ERROR:|Unknown command|usage: setviewpos|GL_INVALID_|GL_OUT_OF_MEMORY|trying to load fallback|"
            r"OpenGL -> [^\n]*\[(?:Error|Undefined)\]", text):
        raise RuntimeError(f"Sky fixture failed: {suite}")
    results = {}
    for highres in (0, 1):
        image = pixels(profile / "screenshots" / f"glow_sky_{highres}.png")
        # This patch is clear sky in the upward view, away from foreground lights.
        patch = [image[(y*160+x)*3+c] for y in range(20, 80) for x in range(100, 150) for c in range(3)]
        results[f"sky_glow_{highres}_max"] = max(patch)
        if max(patch) > 2:
            raise RuntimeError("Sky contaminated the bloom buffer")
    for name in names:
        a, b = (pixels(profile / "screenshots" / f"{name}_{mode}.png") for mode in (0, 1))
        if len(a) != 160*120*3 or len(b) != len(a) or max(a)-min(a) < 16:
            raise RuntimeError(f"Invalid capture: {name}")
        # Reorientation or color-space errors change the face interiors, not just seams.
        results[name] = sum(abs(x-y) for x, y in zip(a, b)) / len(a)
        if results[name] > 3:
            raise RuntimeError(f"Sky orientation/color changed: {name}: {results[name]}")
    a = pixels(profile / "screenshots/baseline.png")
    b = pixels(profile / "screenshots/restarted.png")
    results["restart_error"] = sum(abs(x-y) for x, y in zip(a, b)) / len(a)
    if results["restart_error"] > 1:
        raise RuntimeError("Sky changed after restart")
    b = pixels(profile / "screenshots/highres.png")
    results["highres_delta"] = sum(abs(x-y) for x, y in zip(a, b)) / len(a)
    if "Sky cube: textures/sky_hd/wedge (2048)" not in text or not 0 < results["highres_delta"] < 3:
        raise RuntimeError("High-resolution sky missing or changed orientation/color")
    clamped = pixels(profile / "screenshots/highres_clamped.png")
    results["highres_clamped_error"] = sum(abs(x-y) for x, y in zip(b, clamped)) / len(b)
    if results["highres_clamped_error"] > results["highres_delta"] * 0.5 + 0.005:
        raise RuntimeError("Filtering toggle changed the high-resolution sky")
    haze = pixels(profile / "screenshots/haze.png")
    results["haze_delta"] = sum(abs(x-y) for x, y in zip(a, haze)) / len(a)
    if "Map haze: t2_wedge" not in text or results["haze_delta"] < 0.2:
        raise RuntimeError("Map haze did not affect the bounded cloud layer")
    for name, reference in (("haze_clamped", haze), ("haze_restart", haze), ("haze_off", a)):
        image = pixels(profile / "screenshots" / f"{name}.png")
        results[name + "_error"] = sum(abs(x-y) for x, y in zip(reference, image)) / len(image)
        if results[name + "_error"] > 1:
            raise RuntimeError(f"Haze lifecycle mismatch: {name}")
        if name == "haze_clamped" and sum(abs(x-y) for x, y in zip(a, image)) / len(a) < 0.2:
            raise RuntimeError("Filtering toggle disabled sky haze")
    fog = pixels(profile / "screenshots/local_fog.png")
    results["local_fog_delta"] = sum(abs(x-y) for x, y in zip(a, fog)) / len(a)
    results["local_fog_changed_channels"] = sum(abs(x-y) > 2 for x, y in zip(a, fog))
    if "Local fog grid: 64x36x33" not in text or results["local_fog_changed_channels"] < 500:
        raise RuntimeError("Local fog grid missing or invisible")
    for name, reference in (("local_seamless", fog), ("local_restart", fog), ("local_off", a)):
        image = pixels(profile / "screenshots" / f"{name}.png")
        results[name + "_error"] = sum(abs(x-y) for x, y in zip(reference, image)) / len(image)
        if results[name + "_error"] > 1:
            raise RuntimeError(f"Local fog lifecycle mismatch: {name}")
    restarted = pixels(profile / "screenshots/local_restart.png")
    off = pixels(profile / "screenshots/local_off.png")
    results["restarted_fog_changed_channels"] = sum(abs(x-y) > 2 for x, y in zip(restarted, off))
    if results["restarted_fog_changed_channels"] < 500:
        raise RuntimeError("Local fog disappeared after renderer restart")
    (suite / "result.json").write_text(json.dumps(results, indent=2) + "\n")
    print(json.dumps(results, indent=2))
    print("PASS: sky orientation, live fallback, and restart")


if __name__ == "__main__":
    main()
