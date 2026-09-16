#!/usr/bin/env python3
"""Test the SP Rend2 renderer through restart, save load, and map change."""

import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=root / "build/ready")
    parser.add_argument("--shadows", type=int, choices=(1, 2, 3), default=1)
    parser.add_argument("--buffer-storage", action="store_true")
    parser.add_argument("--geometry-validate", action="store_true")
    parser.add_argument("--gpu-skinning", action="store_true", help="Validate GPU positions through restart, load, and map change")
    args = parser.parse_args()
    package = args.package.resolve()
    if args.geometry_validate and args.gpu_skinning:
        parser.error("Choose CPU cache validation or GPU skinning validation")
    fixture_name = "rend2-gpu-validate.cfg" if args.gpu_skinning else "rend2-geometry-validate.cfg" if args.geometry_validate else "rend2-smoke.cfg"
    for name in ("rend2-smoke.cfg", fixture_name):
        fixture = package / "OpenJK" / name
        if not fixture.is_file() or fixture.read_bytes() != (root / "scripts" / name).read_bytes():
            parser.error(f"Build the current {name}")
    output = root / "build/smoke"
    output.mkdir(parents=True, exist_ok=True)
    suite = Path(tempfile.mkdtemp(prefix="rend2.", dir=output))
    print(f"Rend2 results: {suite}", flush=True)
    subprocess.run(["bash", str(root / "scripts/smoke-sp.sh"), str(package), "t2_wedge",
                    "+set", "r_debugContext", "1",
                    "+set", "cg_shadows", str(args.shadows), "+set", "r_patchStitching", "0",
                    "+set", "r_arb_buffer_storage", str(int(args.buffer_storage)),
                    "+set", "r_ignoreGLErrors", "0", "+exec", fixture_name],
                   env=dict(os.environ, OJK_SMOKE_ROOT=str(suite), OJK_SMOKE_RENDERER="rdsp-rend2",
                            OJK_SMOKE_TIMEOUT="600", OJK_SMOKE_WAIT="10", OJK_SMOKE_DISPLAY="640x480"),
                   check=True)
    logs = list(suite.glob("t2_wedge.*/console.log"))
    if len(logs) != 1:
        raise RuntimeError(f"Missing unique log: {suite}")
    text = logs[0].read_text(errors="replace")
    if args.gpu_skinning and not re.search(r"Ghoul2 GPU validated: vertices=[1-9]\d*", text):
        raise RuntimeError(f"No GPU position readback: {logs[0]}")
    if args.geometry_validate and not re.search(r"Ghoul2 cache validated: vertices=[1-9]\d* tangents=[1-9]\d*", text):
        raise RuntimeError(f"No geometry/tangent reference checks: {logs[0]}")
    if args.buffer_storage and "...using GL_ARB_buffer_storage" not in text:
        raise RuntimeError(f"Buffer storage was not enabled: {logs[0]}")
    if re.search(r"OpenGL -> [^\n]*\[(?:Error|Undefined)\]|GL_INVALID_\w+|GL_OUT_OF_MEMORY|"
                 r"aimemory event=rejected|Cheats are not enabled", text):
        raise RuntimeError(f"GL error or fixture failure: {logs[0]}")
    if text.count("----- finished R_Init -----") < 3:
        raise RuntimeError(f"Fewer than three completed renderer initializations: {logs[0]}")

    phases = {"before": "CM_LoadMap( maps/t2_wedge.bsp, 1 )",
              "restart": "----- finished R_Init -----",
              "loaded": "Loaded saved game format 3",
              "transition": "CM_LoadMap( maps/t1_sour.bsp, 1 )"}
    images = {}
    start = 0
    for phase, required in phases.items():
        marker = f"OJK_REND2_{phase.upper()}"
        end = text.find(marker, start)
        section = text[start:end]
        if end < 0 or text.count(marker) != 1 or required not in section:
            raise RuntimeError(f"Missing or out-of-order {phase} stage: {logs[0]}")
        if f"Wrote screenshots/rend2_{phase}.png" not in section:
            raise RuntimeError(f"Missing {phase} capture before its marker: {logs[0]}")
        if phase != "transition":
            samples = re.findall(r"aimemory event=sample name=_memory_a (.*)", section)
            sample = dict(word.split("=", 1) for word in samples[-1].split()) if samples else {}
            if (sample.get("enemy") != "0" or sample.get("los") != "1" or sample.get("pvs") != "1"
                    or int(sample.get("health", "0")) <= 0):
                raise RuntimeError(f"No live stormtrooper in clear view at {phase}: {logs[0]}")
        image = logs[0].parent / f"profile/OpenJK/screenshots/rend2_{phase}.png"
        pixels = subprocess.run(["ffmpeg", "-v", "error", "-xerror", "-i", str(image),
                                 "-vf", "scale=64:48", "-frames:v", "1", "-pix_fmt", "gray",
                                 "-f", "rawvideo", "-"], stdout=subprocess.PIPE, check=True).stdout
        if (len(pixels) != 64 * 48 or sum(value > 8 for value in pixels) < len(pixels) * 0.01
                or max(pixels) - min(pixels) < 16):
            raise RuntimeError(f"Black or uniform scene: {image}")
        images[phase] = pixels
        start = end + len(marker)

    # Compare different maps, not animation frames from the same scene.
    changed = sum(abs(a - b) > 16 for a, b in zip(images["before"], images["transition"]))
    if changed < len(images["before"]) * 0.1:
        raise RuntimeError(f"Map transition did not change the scene: {suite}")
    print(f"PASS: Rend2 restart, format-3 load, NPC state, and map transition. Results: {suite}")


if __name__ == "__main__":
    main()
