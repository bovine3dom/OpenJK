#!/usr/bin/env python3
"""Check live enhancement comparison, same-frame split output, and restoration."""

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
    parser.add_argument("--msaa", type=int, choices=(0, 4), default=0)
    parser.add_argument("--model", action="store_true", help="Include a static humanoid with skin and capsule effects")
    args = parser.parse_args()
    package = args.package.resolve()
    for name in ("rend2-compare.cfg", "rend2-compare-test.cfg"):
        if (package / "OpenJK" / name).read_bytes() != (root / "scripts" / name).read_bytes():
            parser.error(f"Build the current {name}")
    suite = Path(tempfile.mkdtemp(prefix="renderer-compare.", dir=root / "build/smoke"))
    subprocess.run(["bash", str(root / "scripts/smoke-sp.sh"), str(package), "t2_wedge",
                    "+set", "r_ignoreGLErrors", "0", "+set", "r_debugContext", "1",
                    "+set", "compare_actor", "r_drawentities 1; testG2Model models/players/kyle/model.glm; r_capsuleShadowDebug 1" if args.model else "",
                    "+set", "r_ext_multisample", str(args.msaa), "+exec", "rend2-compare-test.cfg"],
                   env=dict(os.environ, OJK_SMOKE_ROOT=str(suite), OJK_SMOKE_RENDERER="rdsp-rend2",
                            OJK_SMOKE_TIMEOUT="600"), check=True)
    log, = suite.glob("t2_wedge.*/console.log")
    text = re.sub(r"\^[0-9]", "", log.read_text(errors="replace"))
    if re.search(r"trying to load fallback|GL_INVALID_|GL_OUT_OF_MEMORY|couldn't exec|Unknown command|"
                 r"OpenGL -> [^\n]*\[(?:Error|Undefined)\]", text):
        raise RuntimeError(f"Renderer or fixture error: {log}")
    start, baseline, images = 0, None, {}
    for phase in ("modern", "base", "split", "restored", "loaded", "restarted"):
        marker = f"OJK_COMPARE_{phase.upper()}"
        end = text.find(marker, start)
        section = text[start:end]
        if end < 0 or (phase in ("base", "split", "restored") and "RE_Shutdown" in section):
            raise RuntimeError(f"Missing live comparison stage: {phase}")
        if args.model and phase in ("modern", "loaded", "restarted") and "Capsules: models/players/kyle/model.glm" not in section:
            raise RuntimeError(f"Humanoid model missing after {phase}")
        records = re.findall(r"playerstate (.*)", section)
        state = dict(word.split("=", 1) for word in records[-1].split()) if records else {}
        if not state or (baseline and state != baseline):
            raise RuntimeError(f"Game state changed during {phase}: {state}")
        baseline = state
        image = log.parent / f"profile/OpenJK/screenshots/compare_{phase}.png"
        pixels = subprocess.run(["ffmpeg", "-v", "error", "-xerror", "-i", str(image),
                                 "-vf", "scale=96:72", "-frames:v", "1", "-pix_fmt", "rgb24",
                                 "-f", "rawvideo", "-"], capture_output=True, check=True).stdout
        if len(pixels) != 96*72*3 or max(pixels)-min(pixels) < 16:
            raise RuntimeError(f"Missing or uniform scene: {image}")
        images[phase] = pixels
        if phase == "restored" and any(f'{name} = "{value}"' not in section for name, value in
                                      (("r_generatedNormalStrength", "0.375"), ("r_ssaoStrength", "0.8"))):
            raise RuntimeError("Comparison changed saved graphics settings")
        start = end + len(marker)
    def difference(a, b, left, right):
        return sum(abs(images[a][(y*96+x)*3+c] - images[b][(y*96+x)*3+c])
                   for y in range(24, 68) for x in range(left, right) for c in range(3)) / (44*(right-left)*3)
    results = {"left_base_error": difference("split", "base", 4, 46),
               "right_enhanced_error": difference("split", "modern", 50, 92),
               "restore_error": difference("modern", "restored", 4, 92),
               "effect_delta": difference("modern", "base", 4, 92)}
    # Debug models are recreated at the current camera after reload; their old pixels
    # are not a saved-state reference. The world-only fixture checks exact restoration.
    for phase in ("loaded", "restarted"):
        key = f"recreated_model_{phase}_delta" if args.model else f"{phase}_error"
        results[key] = difference("split", phase, 4, 92)
    print(results)
    if results["effect_delta"] < 0.5 or any(results[key] > 1 for key in results if key.endswith("error")):
        raise RuntimeError(f"Split or restoration mismatch: {suite}")
    print(f"PASS: instant comparison, split halves, game state, and custom settings. Results: {suite}")


if __name__ == "__main__":
    main()
