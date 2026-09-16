#!/usr/bin/env python3
"""Check local fog, torch scattering, immediate light removal, and map reset."""

import argparse
import json
import os
from pathlib import Path
import re
import runpy
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=root / "build/ready")
    parser.add_argument("--msaa", type=int, choices=(0, 4), default=0)
    parser.add_argument("--invalid", action="store_true", help="Check rejection of a non-finite profile")
    args = parser.parse_args()
    suite = Path(tempfile.mkdtemp(prefix="local-fog.", dir=root / "build/smoke"))
    profile = suite / "profile/OpenJK"
    (profile / "maps").mkdir(parents=True)
    (profile / "maps/t2_wedge.volfog").write_text(
        ("nan" if args.invalid else "0.20") + " 0.24 0.28 0.0015 2300 200 -160 3500 1200 240\n")
    settings = dict(cl_renderer="rdsp-rend2", r_mode=-1, r_customwidth=640, r_customheight=480,
                    r_fullscreen=0, com_maxfps=30, developer=1, s_initsound=0, r_ignoreGLErrors=0,
                    r_debugContext=1, r_ext_multisample=args.msaa, r_autoExposure=0, r_mapHaze=0,
                    cg_torch=0, r_localFog=0)
    (profile / "openjk_sp.cfg").write_text("".join(f'set {k} "{v}"\n' for k, v in settings.items()))
    (profile / "autoexec_sp.cfg").write_text("// Isolated local fog fixture.\n")
    commands = ["exec krildor-traverse.cfg", "noclip", "d_npcfreeze 1", "cg_draw2D 0", "cg_drawGun 0",
                "cg_thirdPerson 0", "con_notifytime -1", "r_drawentities 0", "cg_bobup 0",
                "cg_bobpitch 0", "cg_bobroll 0", "setviewpos 2640 688 -40 315", "wait 50"]
    phases = (("base", 0, 0), ("fog", 1, 0), ("torch_base", 0, 1), ("torch_fog", 1, 1),
              ("grid_lit", 2, 1), ("grid_dark", 2, 0), ("grid_lit_again", 2, 1),
              ("grid_dark_again", 2, 0), ("fog_restored", 1, 0), ("base_restored", 0, 0))
    for name, fog, torch in phases:
        commands += [f"r_localFog {fog}", f"cg_torch {torch}", "wait 10", f"screenshot_png {name}", "wait 2"]
    commands += ["r_localFog 1", "devmap t1_sour", "wait 100", "echo OJK_LOCAL_FOG_DONE", "quit"]
    (profile / "local-fog-test.cfg").write_text("\n".join(commands) + "\n")
    env = dict(os.environ, OJK_PROFILE=str(profile.parent), SDL_VIDEODRIVER="offscreen",
               EGL_PLATFORM="surfaceless", SDL_AUDIODRIVER="dummy")
    print(f"Local fog results: {suite}", flush=True)
    with (suite / "console.log").open("w") as stream:
        subprocess.run(["timeout", "--kill-after=5s", "600s", "bash", str(args.package.resolve() / "launch-sp.sh"),
                        os.environ.get("OJK_ASSETS", str(root / "GameData")), "+devmap", "t2_wedge",
                        "+exec", "local-fog-test.cfg"], env=env, stdout=stream, stderr=subprocess.STDOUT, check=True)
    text = (suite / "console.log").read_text(errors="replace")
    expected = "Local fog profile: t2_wedge (0 volumes, invalid)" if args.invalid else "Local fog grid: 64x36x33"
    if "OJK_LOCAL_FOG_DONE" not in text or expected not in text or re.search(
            r"ERROR:|Unknown command|GL_INVALID_|GL_OUT_OF_MEMORY|trying to load fallback|"
            r"OpenGL -> [^\n]*\[(?:Error|Undefined)\]", text):
        raise RuntimeError(f"Local fog fixture failed: {suite}")
    pixels = runpy.run_path(str(root / "scripts/test-sky-fog.py"))["pixels"]
    images = {name: pixels(profile / "screenshots" / f"{name}.png") for name, _, _ in phases}
    def difference(a, b):
        return sum(abs(x-y) for x, y in zip(images[a], images[b])) / len(images[a])
    results = dict(fog_delta=difference("base", "fog"), torch_grid_delta=difference("grid_lit", "grid_dark"),
                   torch_repeat_error=difference("grid_lit", "grid_lit_again"),
                   light_removal_error=difference("grid_dark", "grid_dark_again"),
                   fog_restore_error=difference("fog", "fog_restored"),
                   base_restore_error=difference("base", "base_restored"))
    if args.invalid:
        if results["fog_delta"] > 1 or "Local fog grid:" in text:
            raise RuntimeError("Invalid profile enabled local fog")
    elif results["fog_delta"] < 1 or results["torch_grid_delta"] < 0.1 or any(
            value > 1 for key, value in results.items() if key.endswith("error")):
        raise RuntimeError(f"Local fog comparison failed: {results}")
    (suite / "result.json").write_text(json.dumps(results, indent=2) + "\n")
    print(json.dumps(results, indent=2))
    print("PASS: invalid profile rejected" if args.invalid else
          "PASS: local fog, shadowed torch contribution, live restoration, and map change")


if __name__ == "__main__":
    main()
