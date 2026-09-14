#!/usr/bin/env python3
"""Check weapon self-occlusion and separation from world AO."""

import argparse
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile


def pixels(path, width, height):
    header = path.read_bytes()[:24]
    if header[:16] != b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR" or struct.unpack(">II", header[16:24]) != (width, height):
        raise RuntimeError(f"Unexpected capture dimensions: {path}")
    data = subprocess.run(["ffmpeg", "-v", "error", "-xerror", "-i", str(path),
                           "-vf", "scale=160:120", "-frames:v", "1", "-pix_fmt", "gray",
                           "-f", "rawvideo", "-"], capture_output=True, check=True).stdout
    if len(data) != 160 * 120:
        raise RuntimeError(f"Invalid image: {path}")
    return data


def check(label, condition, detail):
    print(f"{'PASS' if condition else 'FAIL'}: {label}: {detail}", flush=True)
    if not condition:
        raise RuntimeError(label)


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=root / "build/ready")
    parser.add_argument("--msaa", type=int, choices=(0, 4))
    parser.add_argument("--hardware", action="store_true", help="Use offscreen hardware EGL rather than Xvfb")
    parser.add_argument("--width", type=int, default=640)
    parser.add_argument("--height", type=int, default=480)
    parser.add_argument("--fov", type=int, default=80)
    parser.add_argument("--method", type=int, choices=(0, 1), default=0)
    parser.add_argument("--half-res", type=int, choices=(0, 1), default=0)
    parser.add_argument("--sample-shading", type=float, choices=(0, 1), default=0)
    args = parser.parse_args()
    if not (64 <= args.width <= 16384 and 64 <= args.height <= 16384):
        parser.error("Invalid dimensions")
    if not 20 <= args.fov <= 140:
        parser.error("FOV must be between 20 and 140")
    package = args.package.resolve()
    fixture = "rend2-ssao-weapons.cfg"
    if (package / "OpenJK" / fixture).read_bytes() != (root / "scripts" / fixture).read_bytes():
        parser.error("Build a package with the current weapon fixture")
    output = root / "build/smoke"
    output.mkdir(parents=True, exist_ok=True)
    suite = Path(tempfile.mkdtemp(prefix="ssao-weapons.", dir=output))
    print(f"Weapon AO results: {suite}", flush=True)
    for msaa in ((args.msaa,) if args.msaa is not None else (0, 4)):
        case = suite / str(msaa)
        env = dict(os.environ, OJK_SMOKE_ROOT=str(case), OJK_SMOKE_RENDERER="rdsp-rend2",
                   OJK_SMOKE_TIMEOUT="600", OJK_SMOKE_DISPLAY=f"{args.width}x{args.height}")
        if args.hardware:
            case.mkdir(parents=True)
            env.update(SDL_VIDEODRIVER="offscreen", EGL_PLATFORM="surfaceless",
                       SDL_AUDIODRIVER="dummy", OJK_PROFILE=str(case / "profile"))
            env.pop("LIBGL_ALWAYS_SOFTWARE", None)
            command = ["timeout", "--kill-after=5s", "600s", "bash", str(package / "launch-sp.sh"),
                       os.environ.get("OJK_ASSETS", str(root / "GameData")), "+safe",
                       "+set", "cl_renderer", "rdsp-rend2", "+set", "r_fullscreen", "0",
                       "+set", "s_initsound", "0", "+devmap", "t2_wedge"]
        else:
            env["SDL_VIDEODRIVER"] = "x11"
            env.pop("EGL_PLATFORM", None)
            command = ["bash", str(root / "scripts/smoke-sp.sh"), str(package), "t2_wedge"]
        for name, value in dict(r_ssao=1, r_ssaoMethod=args.method, r_gtaoHalfRes=args.half_res, r_sampleShading=args.sample_shading,
                                r_ext_multisample=msaa, r_normalMapping=1,
                                r_specularMapping=1, r_debugContext=1, r_ignoreGLErrors=0,
                                com_maxfps=10, r_mode=-1, r_customwidth=args.width,
                                r_customheight=args.height, cg_fov=args.fov).items():
            command += ["+set", name, str(value)]
        command += ["+exec", fixture]
        if args.hardware:
            command += ["+wait", "10", "+quit"]
            log = case / "console.log"
            with log.open("w") as stream:
                subprocess.run(command, env=env, stdout=stream, stderr=subprocess.STDOUT, check=True)
        else:
            subprocess.run(command, env=env, check=True)
            log, = case.glob("t2_wedge.*/console.log")
        text = log.read_text(errors="replace")
        check("renderer identity", "----- rdsp-rend2 -----" in text and
              "trying to load fallback renderer" not in text, "Rend2 without fallback")
        if args.hardware:
            gpu = re.findall(r"GL_RENDERER: (.*)", text)
            check("hardware renderer", bool(gpu) and not re.search(r"llvmpipe|softpipe", gpu[-1], re.I), gpu)
        check("fixture and GL", "OJK_WEAPON_AO_DONE" in text and not re.search(
            r"OpenGL -> [^\n]*\[(?:Error|Undefined)\]|GL_INVALID_|Cheats are not enabled", text), log)
        images = log.parent / "profile/OpenJK/screenshots"
        data = {name: pixels(images / f"{name}.png", args.width, args.height) for name in (
            "weapon_mask", "weapon_ao", "weapon_plain", "weapon_shaded", "world_with_weapon",
            "world_without_weapon", "weapon_hidden", "weapon_near_wall", "pistol_mask", "pistol_ao", "repeater_ao",
            "weapon_radius_small", "weapon_radius_large", "weapon_disabled", "weapon_disabled_ao",
            "weapon_restarted", "weapon_loaded")}
        mask = {i for i, p in enumerate(data["weapon_mask"]) if p < 128}
        check("weapon mask", 30 < len(mask) < 8000, len(mask))
        occluded = {i for i, p in enumerate(data["weapon_ao"]) if p < 245}
        check("weapon self-occlusion", len(occluded) > 10, len(occluded))
        expanded = {i + dy * 160 + dx for i in mask for dx in (-2, -1, 0, 1, 2) for dy in (-2, -1, 0, 1, 2)}
        check("AO stays on weapon", len(occluded - expanded) <= 5, len(occluded - expanded))
        interior = {i for i in mask if all(i + dy * 160 + dx in mask
                    for dx in (-1, 0, 1) for dy in (-1, 0, 1))}
        darkened = sum(data["weapon_plain"][i] - data["weapon_shaded"][i] > 2 for i in interior)
        check("strength changes weapon lighting", darkened > 5, darkened)
        check("weapon radius changes AO", sum(abs(a - b) > 2 for a, b in zip(
            data["weapon_radius_small"], data["weapon_radius_large"])) > 10, "radius 0.5 versus 2")
        check("disabled weapon AO is white", min(data["weapon_disabled_ao"]) >= 254, min(data["weapon_disabled_ao"]))
        difference = sum(abs(data["weapon_plain"][i] - data["weapon_disabled"][i]) for i in interior) / len(interior)
        check("disabled matches zero strength", difference < 2, difference)
        difference = [abs(a - b) for a, b in zip(data["world_with_weapon"], data["world_without_weapon"])]
        check("weapon does not occlude world", sum(difference) / len(difference) < 1,
              sum(difference) / len(difference))
        check("hidden weapon has no stale AO", min(data["weapon_hidden"]) >= 254, min(data["weapon_hidden"]))
        difference = [abs(a - b) for a, b in zip(data["weapon_ao"], data["weapon_near_wall"])]
        check("wall does not affect weapon AO", sum(difference) / len(difference) < 1,
              sum(difference) / len(difference))
        check("pistol after firing", sum(v < 245 for v in data["pistol_ao"]) > 10 and
              sum(v < 128 for v in data["pistol_mask"]) > 30, "mask and AO present")
        check("repeater after firing", sum(v < 245 for v in data["repeater_ao"]) > 10, "AO present")
        check("restart and load", "Loaded saved game format" in text and
              sum(v < 245 for v in data["weapon_restarted"]) > 10 and
              sum(v < 245 for v in data["weapon_loaded"]) > 10, "weapon AO recreated")
    print(f"PASS: weapon AO isolation. Results: {suite}")


if __name__ == "__main__":
    main()
