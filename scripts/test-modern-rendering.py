#!/usr/bin/env python3
"""Check spatial GTAO presets, live sample shading, and camera return captures."""

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
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--msaa", type=int, choices=(0, 4), default=4)
    parser.add_argument("--half-res", type=int, choices=(0, 1), default=0)
    args = parser.parse_args()
    if not (64 <= args.width <= 16384 and 64 <= args.height <= 16384):
        parser.error("Invalid capture dimensions")
    package = args.package.resolve()
    fixture = "rend2-modern.cfg"
    if (package / "OpenJK" / fixture).read_bytes() != (root / "scripts" / fixture).read_bytes():
        parser.error("Build a package with the current fixture")
    output = root / "build/smoke"
    output.mkdir(parents=True, exist_ok=True)
    case = Path(tempfile.mkdtemp(prefix="modern.", dir=output))
    profile = case / "profile/OpenJK"
    profile.mkdir(parents=True, exist_ok=True)
    settings = dict(cl_renderer="rdsp-rend2", r_ssao=1, r_ssaoMethod=1, r_gtaoHalfRes=args.half_res,
                    r_normalMapping=1, r_specularMapping=1, r_ext_multisample=args.msaa,
                    r_mode=-1, r_customwidth=args.width, r_customheight=args.height,
                    r_fullscreen=0, r_debugContext=1, r_ignoreGLErrors=0,
                    com_maxfps=30, s_initsound=0)
    env = dict(os.environ, OJK_PROFILE=str(profile.parent), SDL_VIDEODRIVER="offscreen",
               EGL_PLATFORM="surfaceless", SDL_AUDIODRIVER="dummy")
    env.pop("LIBGL_ALWAYS_SOFTWARE", None)
    print(f"Modern renderer results: {case}", flush=True)
    (profile / "openjk_sp.cfg").write_text("".join(f'set {k} "{v}"\n' for k, v in settings.items()))
    (profile / "autoexec_sp.cfg").write_text("// Controlled capture profile.\n")
    with (case / "console.log").open("w") as log:
        subprocess.run(["timeout", "--kill-after=5s", "600s", "bash", str(package / "launch-sp.sh"),
                        os.environ.get("OJK_ASSETS", str(root / "GameData")),
                        "+devmap", "t2_wedge", "+exec", fixture],
                       env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    text = re.sub(r"\^[0-9]", "", (case / "console.log").read_text(errors="replace"))
    if ("OJK_MODERN_DONE" not in text or "----- rdsp-rend2 -----" not in text
            or "sample shading available" not in text
            or re.search(r"trying to load fallback|llvmpipe|softpipe|GL_INVALID_|GL_OUT_OF_MEMORY|"
                         r"OpenGL -> [^\n]*\[(?:Error|Undefined)\]|Cheats are not enabled", text, re.I)):
        raise RuntimeError(f"Renderer, capability, or fixture failure: {case}")
    if not re.search(rf'Cvar r_ext_multisample = "{args.msaa}"', text):
        raise RuntimeError("Requested MSAA count was not confirmed")
    if not re.search(r'Cvar r_ssaoMethod = "1"', text):
        raise RuntimeError("GTAO selection was not confirmed")
    ao_width = (args.width + 1) // 2 if args.half_res else args.width
    ao_height = (args.height + 1) // 2 if args.half_res else args.height
    if f"GTAO: {ao_width}x{ao_height} -> {args.width}x{args.height}" not in text:
        raise RuntimeError("Unexpected AO buffer dimensions")
    for name, value in (("r_gtaoQuality", 1), ("r_sampleShading", 0),
                        ("r_ssaoRadius", 1), ("r_ssaoViewModelRadius", "0.05")):
        if not re.search(rf'Cvar {name} = "{value}"', text):
            raise RuntimeError(f"Unexpected default: {name}")

    def pixels(name):
        path = profile / "screenshots" / f"{name}.png"
        header = path.read_bytes()[:24]
        if header[:16] != b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR" or struct.unpack(">II", header[16:24]) != (args.width, args.height):
            raise RuntimeError(f"Invalid dimensions: {path}")
        return subprocess.run(["ffmpeg", "-v", "error", "-xerror", "-i", str(path),
                               "-frames:v", "1", "-pix_fmt", "gray", "-f", "rawvideo", "-"],
                              capture_output=True, check=True).stdout

    def difference(a, b):
        return sum(abs(x - y) for x, y in zip(a, b)) / len(a)

    results = {"package": (package / "build-id.txt").read_text().strip(), "msaa": args.msaa,
               "half_res": args.half_res, "ao_dimensions": [ao_width, ao_height]}
    presets = [pixels(f"quality{i}") for i in range(4)]
    for i, image in enumerate(presets):
        if max(image) - min(image) < 20 or sum(p < 245 for p in image) < len(image) * 0.001:
            raise RuntimeError(f"GTAO preset {i} has no useful AO")
    results["preset_delta"] = [difference(presets[i], presets[i + 1]) for i in range(3)]
    if min(results["preset_delta"]) < 0.01:
        raise RuntimeError("Quality presets did not change the AO")
    narrow, wide, restored_ao = (pixels(name) for name in ("denoise_off", "denoise_on", "denoise_restored"))
    region = [y * args.width + x for y in range(args.height * 3 // 10, args.height * 8 // 10)
              for x in range(args.width // 20, args.width // 4)]
    def variation(image):
        return sum(abs(2 * image[i] - image[i - 1] - image[i + 1]) +
                   abs(2 * image[i] - image[i - args.width] - image[i + args.width])
                   for i in region) / len(region)
    results["flat_surface_variation"] = [variation(narrow), variation(wide)]
    print(f"Flat-surface variation (narrow, wide): {results['flat_surface_variation']}", flush=True)
    results["denoise_restore_error"] = sum(abs(narrow[i] - restored_ao[i]) for i in region) / len(region)
    if results["denoise_restore_error"] > 0.2:
        raise RuntimeError("Denoising did not restore the static AO result")
    if args.half_res and results["flat_surface_variation"][1] >= results["flat_surface_variation"][0] * 0.9:
        raise RuntimeError("Denoising did not reduce flat-surface high-frequency variation")
    off, on, restored = (pixels(name) for name in ("aa_off", "aa_on", "aa_restored"))
    results["sample_shading_delta"] = difference(off, on)
    results["sample_shading_restore_error"] = difference(off, restored)
    noise = results["sample_shading_restore_error"]
    if noise > 0.2:
        raise RuntimeError("Sample shading did not restore the baseline")
    if args.msaa and results["sample_shading_delta"] <= max(0.01, noise * 2):
        raise RuntimeError("Sample shading did not change the image above fixture noise")
    if not args.msaa and results["sample_shading_delta"] > max(0.02, noise * 2):
        raise RuntimeError("Sample shading changed a single-sample image")
    start = pixels("pan_start")
    # Compare the static wall. Sky textures and character animation still advance.
    def wall(image):
        return bytes(image[y * args.width + x]
                     for y in range(args.height // 10, args.height * 7 // 10)
                     for x in range(args.width * 28 // 100, args.width * 42 // 100))
    results["camera_return_error"] = difference(wall(start), wall(pixels("pan_return")))
    print(json.dumps(results, indent=2), flush=True)
    if results["camera_return_error"] > 0.2:
        raise RuntimeError("Camera return did not restore the spatial rendering result")
    for i in range(1, 6):
        pixels(f"pan{i}")
    (case / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    print("PASS: quality presets, sample shading, and camera return. Pan images require visual review.")


if __name__ == "__main__":
    main()
