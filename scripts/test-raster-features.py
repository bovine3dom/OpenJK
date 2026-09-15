#!/usr/bin/env python3
"""Capture and compare capsule shadows, skin diffusion, particles, and SMAA."""

import argparse
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=root / "build/ready")
    parser.add_argument("--msaa", type=int, choices=(0, 4), default=0)
    args = parser.parse_args()
    package = args.package.resolve()
    fixture = "rend2-features.cfg"
    if (package / "OpenJK" / fixture).read_bytes() != (root / "scripts" / fixture).read_bytes():
        parser.error("Build a package with the current fixture")
    output = root / "build/smoke"
    output.mkdir(parents=True, exist_ok=True)
    suite = Path(tempfile.mkdtemp(prefix="raster-features.", dir=output))
    profile = suite / "profile/OpenJK"
    profile.mkdir(parents=True)
    (profile / "shaders").mkdir()
    (profile / "shaders/raster_test.shader").write_text(
        "raster/soft_alpha { cull disable\n { map $whiteimage\n blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA\n rgbGen vertex\n alphaGen vertex\n } }\n"
        "raster/soft_add { cull disable\n { map $whiteimage\n blendFunc GL_ONE GL_ONE\n rgbGen vertex\n } }\n")
    settings = dict(cl_renderer="rdsp-rend2", r_mode=-1, r_customwidth=960, r_customheight=720,
                    r_fullscreen=0, r_ssao=1, r_ssaoMethod=1, r_softParticles=1,
                    r_ext_multisample=args.msaa, com_maxfps=60, developer=1,
                    r_ignoreGLErrors=0, r_debugContext=1, s_initsound=0)
    (profile / "openjk_sp.cfg").write_text("".join(f'set {k} "{v}"\n' for k, v in settings.items()))
    (profile / "autoexec_sp.cfg").write_text("// Controlled graphics fixture.\n")
    env = dict(os.environ, OJK_PROFILE=str(profile.parent), SDL_VIDEODRIVER="offscreen",
               EGL_PLATFORM="surfaceless", SDL_AUDIODRIVER="dummy")
    env.pop("LIBGL_ALWAYS_SOFTWARE", None)
    print(f"Raster feature results: {suite}", flush=True)
    with (suite / "console.log").open("w") as stream:
        subprocess.run(["timeout", "--kill-after=5s", "600s", "bash", str(package / "launch-sp.sh"),
                        os.environ.get("OJK_ASSETS", str(root / "GameData")), "+devmap", "t2_wedge", "+exec", fixture],
                       env=env, stdout=stream, stderr=subprocess.STDOUT, check=True)
    log = (suite / "console.log").read_text(errors="replace")
    if ("OJK_FEATURES_DONE" not in log or "----- rdsp-rend2 -----" not in log or
            re.search(r"llvmpipe|softpipe|trying to load fallback|GL_INVALID_|GL_OUT_OF_MEMORY|"
                      r"OpenGL -> [^\n]*\[(?:Error|Undefined)\]", log, re.I)):
        raise RuntimeError("Renderer or GL failure")
    def image(name):
        file = profile / "screenshots" / f"{name}.png"
        header = file.read_bytes()[:24]
        if struct.unpack(">II", header[16:24]) != (960, 720):
            raise RuntimeError("Unexpected capture dimensions")
        return subprocess.run(["ffmpeg", "-v", "error", "-i", str(file), "-frames:v", "1",
                               "-pix_fmt", "rgb24", "-f", "rawvideo", "-"], capture_output=True, check=True).stdout
    names = ("caps_base", "caps_on", "caps_restored", "smaa_base", "smaa_on", "smaa_restored",
             "smaa_edges", "smaa_weights", "skin_mask", "skin_on", "skin_off", "skin_restored",
             "particle_base", "particle_on", "particle_restored", "additive_base", "additive_on", "additive_restored",
             "skin_restarted", "skin_loaded", "skin_ineligible", "particle_no_depth_base", "particle_no_depth_on")
    data = {name: image(name) for name in names}
    def delta(a, b):
        return sum(abs(x-y) for x, y in zip(data[a], data[b])) / len(data[a])
    results = {"msaa": args.msaa}
    for effect in ("caps", "smaa", "particle", "additive"):
        results[effect] = dict(effect_delta=delta(effect + "_base", effect + "_on"),
                               restore_error=delta(effect + "_base", effect + "_restored"))
    mask = {i for i in range(960 * 720) if data["skin_mask"][3*i] > 128}
    interior = {i for i in mask if all(i + dy * 960 + dx in mask
                for dx in (-2, 0, 2) for dy in (-2, 0, 2))}
    def skin_delta(a, b):
        return sum(abs(data[a][i*3+c] - data[b][i*3+c]) for i in interior for c in range(3)) / max(1, len(interior)*3)
    results["skin"] = dict(effect_delta=skin_delta("skin_off", "skin_on"), restore_error=skin_delta("skin_on", "skin_restored"),
                           mask_pixels=len(mask))
    results["smaa"]["edge_pixels"] = sum(max(data["smaa_edges"][i:i+3]) > 32 for i in range(0, len(data["smaa_edges"]), 3))
    results["smaa"]["weight_pixels"] = sum(max(data["smaa_weights"][i:i+3]) > 8 for i in range(0, len(data["smaa_weights"]), 3))
    results["no_depth_fade_delta"] = delta("particle_no_depth_base", "particle_no_depth_on")
    results["lifecycle_masks"] = {name: sum(data[name][i] > 128 for i in range(0, len(data[name]), 3))
                                 for name in ("skin_restarted", "skin_loaded", "skin_ineligible")}
    (suite / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    print(json.dumps(results, indent=2), flush=True)
    for effect in ("caps", "smaa", "particle", "additive", "skin"):
        if results[effect]["effect_delta"] <= 0.001 or results[effect]["restore_error"] > 1:
            raise RuntimeError(f"No controlled effect or failed restoration: {effect}")
    for effect in ("particle", "additive"):
        if results[effect]["effect_delta"] <= 2 * results[effect]["restore_error"]:
            raise RuntimeError(f"Particle fading did not exceed scene variation: {effect}")
    if results["no_depth_fade_delta"] > 0.1:
        raise RuntimeError("Particle fading used stale depth with the prepass disabled")
    if not 10 < results["skin"]["mask_pixels"] < 960 * 720 // 5:
        raise RuntimeError("Skin eligibility mask is empty or excessive")
    if min(results["smaa"]["edge_pixels"], results["smaa"]["weight_pixels"]) < 100:
        raise RuntimeError("SMAA edge/weight passes are empty")
    for name in ("skin_restarted", "skin_loaded"):
        if sum(data[name][i] > 128 for i in range(0, len(data[name]), 3)) < 10:
            raise RuntimeError(f"Missing skin after lifecycle transition: {name}")
    if max(data["skin_ineligible"]) > 2:
        raise RuntimeError("Ineligible model retained a skin mask")
    print("PASS: feature captures and live restoration")


if __name__ == "__main__":
    main()
