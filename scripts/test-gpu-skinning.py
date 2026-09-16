#!/usr/bin/env python3
"""Validate GPU deformation and compare the CPU/GPU character images."""

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
    parser.add_argument("--shadows", type=int, choices=(1, 2, 3), default=1)
    parser.add_argument("--software", action="store_true")
    args = parser.parse_args()
    package = args.package.resolve()
    fixture = "rend2-gpu-skinning.cfg"
    if (package / "OpenJK" / fixture).read_bytes() != (root / "scripts" / fixture).read_bytes():
        parser.error("Build the current GPU skinning fixture")
    suite = Path(tempfile.mkdtemp(prefix="gpu-skinning.", dir=root / "build/smoke"))
    profile = suite / "profile/OpenJK"
    profile.mkdir(parents=True)
    settings = dict(cl_renderer="rdsp-rend2", r_mode=-1, r_customwidth=960, r_customheight=720,
                    r_fullscreen=0, com_maxfps=20, developer=1, s_initsound=0,
                    r_ignoreGLErrors=0, r_debugContext=1, r_ext_multisample=args.msaa, cg_shadows=args.shadows)
    (profile / "openjk_sp.cfg").write_text("".join(f'set {k} "{v}"\n' for k, v in settings.items()))
    (profile / "autoexec_sp.cfg").write_text("// Controlled GPU skinning fixture.\n")
    env = dict(os.environ, OJK_PROFILE=str(profile.parent), SDL_AUDIODRIVER="dummy")
    command = ["timeout", "--kill-after=5s", "600s"]
    if args.software:
        env.update(SDL_VIDEODRIVER="x11", LIBGL_ALWAYS_SOFTWARE="1", LP_NUM_THREADS="1")
        env.pop("EGL_PLATFORM", None)
        command += ["xvfb-run", "-a", "-s", "-screen 0 960x720x24"]
    else:
        env.update(SDL_VIDEODRIVER="offscreen", EGL_PLATFORM="surfaceless")
        env.pop("LIBGL_ALWAYS_SOFTWARE", None)
    command += ["bash", str(package / "launch-sp.sh"), os.environ.get("OJK_ASSETS", str(root / "GameData")),
                "+devmap", "t2_wedge", "+exec", fixture, "+wait", "10", "+quit"]
    print(f"GPU skinning results: {suite}", flush=True)
    with (suite / "console.log").open("w") as stream:
        subprocess.run(command, env=env, stdout=stream, stderr=subprocess.STDOUT, check=True)
    text = (suite / "console.log").read_text(errors="replace")
    if ("OJK_GPU_DONE" not in text or "----- rdsp-rend2 -----" not in text or
            re.search(r"ERROR:|Unknown command|mismatch|GL_INVALID_|GL_OUT_OF_MEMORY|trying to load fallback|"
                      r"OpenGL -> [^\n]*\[(?:Error|Undefined)\]", text)):
        raise RuntimeError("GPU skinning or GL failure")
    gpu = re.findall(r"GL_RENDERER: (.*)", text)
    if not gpu or (not args.software and re.search(r"llvmpipe|softpipe", gpu[-1], re.I)):
        raise RuntimeError("Wrong renderer device")
    sections = text.split("OJK_GPU_RESTORED", 1)
    if len(sections) != 2 or not all(re.search(r"Ghoul2 GPU validated: vertices=[1-9]\d*", s) for s in sections):
        raise RuntimeError("Static and animated GPU readback were not both exercised")
    if args.shadows == 2 and not re.search(r"Ghoul2 GPU: .*fallbacks=[1-9]\d*", text):
        raise RuntimeError("Stencil CPU fallback was not exercised")
    gore = text.split("OJK_GPU_GORE_BEGIN", 1)[-1]
    if not re.search(r"Ghoul2 GPU: .*gore=[1-9]\d*", gore):
        raise RuntimeError("Real projectile impacts did not exercise the gore fallback")
    hit = re.search(r"aimemory event=sample name=_memory_a .*health=(\d+)", gore)
    if not hit or not 0 < int(hit[1]) < 30:
        raise RuntimeError("The gore probe did not hit the intended NPC")
    images = {}
    for name in ("cpu", "on", "restored", "split", "gore"):
        image = profile / "screenshots" / f"gpu_{name}.png"
        if struct.unpack(">II", image.read_bytes()[16:24]) != (960, 720):
            raise RuntimeError("Unexpected GPU comparison dimensions")
        images[name] = subprocess.run(["ffmpeg", "-v", "error", "-xerror", "-i", str(image),
            "-frames:v", "1", "-pix_fmt", "rgb24", "-f", "rawvideo", "-"], capture_output=True, check=True).stdout
        if len(images[name]) != 960*720*3 or max(images[name])-min(images[name]) < 16:
            raise RuntimeError("Missing or uniform GPU comparison image")
    # This fixed region contains the character; the round trip bounds animation drift.
    region = [i*3+c for y in range(360, 720) for x in range(280, 680) for i in (y*960+x,) for c in range(3)]
    delta = lambda a, b: sum(abs(images[a][i]-images[b][i]) for i in region) / len(region)
    result = dict(gpu=gpu[-1], msaa=args.msaa, shadows=args.shadows,
                  image_delta=delta("cpu", "on"), restore_delta=delta("cpu", "restored"),
                  max_position_error=max(map(float, re.findall(r"Ghoul2 GPU validated: vertices=\d+ max_error=([\d.e+-]+)", text))))
    (suite / "result.json").write_text(json.dumps(result, indent=2)+"\n")
    print(result)
    if result["restore_delta"] > 1 or result["image_delta"] > result["restore_delta"] + 1:
        raise RuntimeError("GPU shading differs too much from the CPU reference")
    print("PASS: GPU readback, static/animated geometry, CPU image comparison, and live split")


if __name__ == "__main__":
    main()
