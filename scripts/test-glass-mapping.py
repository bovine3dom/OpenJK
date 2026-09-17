#!/usr/bin/env python3
"""Check that small pane geometry errors do not change planar reflection mapping."""

import argparse
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location("glass", ROOT / "scripts/test-glass-sp.py")
assert SPEC is not None and SPEC.loader is not None
GLASS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GLASS)


def fixture(assets, warped):
    original = None
    for archive in sorted((assets / "base").glob("assets*.pk3")):
        with zipfile.ZipFile(archive) as package:
            if "maps/t1_sour.bsp" in package.namelist():
                original = package.read("maps/t1_sour.bsp")
    if original is None or original[:4] != b"RBSP":
        raise RuntimeError("Missing retail t1_sour RBSP")
    data = bytearray(original)
    lumps = [struct.unpack_from("<ii", data, 8 + i * 8) for i in range(18)]
    start, length = lumps[1]
    names = [data[i:i + 64].split(b"\0")[0] for i in range(start, start + length, 72)]
    start, length = lumps[13]
    count = 0
    for surface in range(start, start + length, 148):
        shader, _, kind, first, vertices = struct.unpack_from("<5i", data, surface)
        if names[shader] != b"textures/common/glass" or kind != 1:
            continue
        normal = struct.unpack_from("<3f", data, surface + 128)
        scale = math.sqrt(sum(v * v for v in normal))
        normal = [v / scale for v in normal]
        origin = struct.unpack_from("<3f", data, lumps[10][0] + first * 80)
        distance = sum(x * y for x, y in zip(origin, normal))
        # Keep the first vertex (the renderer's reference point) fixed. Only
        # the render vertices change; collision, scripts, and topology stay intact.
        for vertex in range(1, vertices):
            offset = lumps[10][0] + (first + vertex) * 80
            point = struct.unpack_from("<3f", data, offset)
            error = sum(x * y for x, y in zip(point, normal)) - distance
            noise = (1.25 if vertex % 2 else -1.25) if warped else 0.0
            struct.pack_into("<3f", data, offset, *(point[i] + normal[i] * (noise - error) for i in range(3)))
        count += 1
    if count != 16:
        raise RuntimeError(f"Unexpected retail glass surfaces: {count}")
    return data


def check_indices(package, suite):
    runs = []
    for count in (1, 239):
        run = subprocess.run(["python3", str(ROOT / "scripts/test-glass-sp.py"), "--package", str(package),
                              "--campaign", "ja", "--mode", "combined", "--manual-count", str(count),
                              "--probe-budget", "192"], capture_output=True, text=True)
        print(run.stdout, flush=True)
        if run.returncode:
            raise RuntimeError(run.stderr)
        match = re.search(r"^Glass results: (.+)$", run.stdout, re.M)
        if not match:
            raise RuntimeError("Missing probe test output")
        runs.append(Path(match[1]))
    stats = json.loads((runs[1] / "results.json").read_text())["ja_probes"]
    if stats["first_index"] != 240 or stats["last_index"] != 255:
        raise RuntimeError("The test did not use the highest probe slots")
    errors = {}
    for view in range(4):
        images = []
        masks = []
        for run in runs:
            screenshots = run / "ja/OpenJK/screenshots"
            images.append(GLASS.pixels(screenshots / f"glass_{view}_reflection.png"))
            masks.append(GLASS.pixels(screenshots / f"glass_{view}_assignment.png"))
        region = [p for p in range(0, len(masks[0]), 3)
                  if all(mask[p + 1] > max(32, mask[p] * 2, mask[p + 2] * 2) for mask in masks)]
        if len(region) < 1000:
            raise RuntimeError("Missing reflection pixels in probe-index test")
        samples = [bytes(image[p + c] for p in region for c in range(3)) for image in images]
        errors[view] = GLASS.difference(*samples)
        if errors[view] > 0.2:
            raise RuntimeError(f"High probe indices changed the reflected image: {view}, {errors[view]}")
    (suite / "results.json").write_text(json.dumps(dict(runs=list(map(str, runs)), errors=errors), indent=2) + "\n")
    print(f"PASS: probe slots 240–255 match the low-index reference: {errors}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=ROOT / "build/ready")
    parser.add_argument("--high-indices", action="store_true", help="Check a 192 budget and compare probe slots 240–255 with low slots")
    args = parser.parse_args()
    package = args.package.resolve()
    assets = Path(os.environ.get("OJK_ASSETS", ROOT / "GameData")).resolve()
    output = ROOT / "build/smoke"
    output.mkdir(parents=True, exist_ok=True)
    suite = Path(tempfile.mkdtemp(prefix="glass-mapping.", dir=output))
    print(f"Mapping results: {suite}", flush=True)
    if args.high_indices:
        check_indices(package, suite)
        return
    captures = {}
    for variant in ("flat", "warped"):
        home = suite / variant
        profile = home / "OpenJK"
        (profile / "maps").mkdir(parents=True)
        (profile / "maps/t1_sour.bsp").write_bytes(fixture(assets, variant == "warped"))
        probes = profile / "cubemaps/t1_sour"
        probes.mkdir(parents=True)
        (probes / "env.json").write_text(json.dumps({"Cubemaps": [
            {"Name": "fixed-reference", "Position": [7168, -3072, 556], "Radius": 512}]}))
        settings = dict(cl_renderer="rdsp-rend2", r_mode=-1, r_customwidth=960,
                        r_customheight=720, r_fullscreen=0, r_debugContext=1, r_ignoreGLErrors=0,
                        r_glass=1, r_glassProbes=0, r_cubeMapping=1, r_glassPlanar=1,
                        r_glassReflection=0, r_glassExposure=2, r_glassRoughness=0,
                        r_glassDebug=0, r_autoExposure=0, r_ssao=0, r_dynamicGlow=0,
                        s_initsound=0, com_maxfps=60, developer=1)
        (profile / "openjk_sp.cfg").write_text("".join(f'set {key} "{value}"\n' for key, value in settings.items()))
        (profile / "autoexec_sp.cfg").write_text("// Controlled reflection geometry test.\n")
        # Reflection is off during capture, so edited glass cannot change the
        # reference cubemap. Both runs sample the same room from the same point.
        commands = ["wait 150", "exitview", "god", "notarget", "noclip", "d_npcfreeze 1",
                    "cg_draw2D 0", "cg_drawGun 0", "cg_thirdPerson 0", "r_drawentities 0",
                    "con_notifytime -1", "wait 420", "setviewpos 7460 -2780 556 45", "wait 360",
                    "fixedtime 1", "r_glassReflection 1", "r_glassDebug 1"]
        for label, controls in (("planar", ["r_glassPlanar 1"]), ("mesh", ["r_glassPlanar 0"]),
                                ("restored", ["r_glassPlanar 1"]), ("mask", ["r_glassDebug 2"])):
            commands += controls + ["wait 10", f"screenshot_png mapping_{label}", "wait 5"]
        commands += ["echo OJK_MAPPING_DONE", "wait 5", "quit"]
        (profile / "mapping.cfg").write_text("\n".join(commands) + "\n")
        env = dict(os.environ, OJK_PROFILE=str(home), SDL_VIDEODRIVER="offscreen",
                   EGL_PLATFORM="surfaceless", SDL_AUDIODRIVER="dummy")
        env.pop("LIBGL_ALWAYS_SOFTWARE", None)
        with (suite / f"{variant}.log").open("w") as log:
            subprocess.run(["timeout", "--kill-after=5s", "300s", "bash", str(package / "launch-sp.sh"),
                            str(assets), "+devmap", "t1_sour", "+exec", "mapping.cfg"],
                           env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
        text = (suite / f"{variant}.log").read_text(errors="replace")
        if "OJK_MAPPING_DONE" not in text or "----- rdsp-rend2 -----" not in text or re.search(
                r"trying to load fallback|GL_INVALID_|GL_OUT_OF_MEMORY|Couldn't compile|OpenGL -> [^\n]*\[(?:Error|Undefined)\]", text, re.I):
            raise RuntimeError(f"Renderer failure: {variant}")
        captures[variant] = {label: GLASS.pixels(profile / "screenshots" / f"mapping_{label}.png")
                             for label in ("planar", "mesh", "restored", "mask")}
    masks = [captures[variant]["mask"] for variant in captures]
    if any(len(image) != 960 * 720 * 3 for run in captures.values() for image in run.values()):
        raise RuntimeError("Unexpected image dimensions")
    valid = [all(image[p + 1] > max(32, image[p] * 2, image[p + 2] * 2) for image in masks)
             for p in range(0, len(masks[0]), 3)]
    # Exclude silhouettes, where the fixture intentionally changes mesh coverage.
    region = [y * 960 + x for y in range(4, 716) for x in range(4, 956)
              if all(valid[(y + dy) * 960 + x + dx] for dy in (-4, 0, 4) for dx in (-4, 0, 4))]
    if len(region) < 10000:
        raise RuntimeError("No common window interior")

    def sample(variant, label):
        image = captures[variant][label]
        return bytes(image[p * 3 + c] for p in region for c in range(3))

    metrics = dict(pixels=len(region),
                   flat_reference_error=GLASS.difference(sample("flat", "planar"), sample("flat", "mesh")),
                   planar_error=GLASS.difference(sample("flat", "planar"), sample("warped", "planar")),
                   mesh_error=GLASS.difference(sample("flat", "mesh"), sample("warped", "mesh")),
                   restore_error=max(GLASS.difference(sample(v, "planar"), sample(v, "restored")) for v in captures))
    (suite / "results.json").write_text(json.dumps(metrics, indent=2) + "\n")
    print(metrics, flush=True)
    # Full-precision plane normals differ slightly from packed vertex normals.
    # A large shift on already-flat panes would indicate incorrect plane/batch state.
    if metrics["flat_reference_error"] > 3:
        raise RuntimeError("Planar mapping changed the reference reflection on flat panes")
    if metrics["planar_error"] > 0.2 or metrics["mesh_error"] < max(0.02, metrics["planar_error"] * 3):
        raise RuntimeError("Planar mapping did not reject the triangle geometry error")
    if metrics["restore_error"] > 0.2:
        raise RuntimeError("Planar control did not restore its image")
    print("PASS: reflection mapping is stable across perturbed pane triangles")


if __name__ == "__main__":
    main()
