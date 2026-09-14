#!/usr/bin/env python3
"""Check static SP SSAO captures with MSAA 0 and 4. Do not build the package."""

import argparse
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile


def check(label, values, valid):
    print(f"{'PASS' if valid else 'FAIL'}: {label}: {values}", flush=True)
    if not valid:
        raise RuntimeError(label)


def compare(label, first, second):
    delta = [abs(a - b) for a, b in zip(first, second)]
    mean = sum(delta) / len(delta)
    changed = sum(d > 8 for d in delta) / len(delta)
    check(label, dict(mean_error=mean, fraction_over_8=changed), mean <= 1 and changed <= 0.01)


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=root / "build/ready")
    args = parser.parse_args()
    package = args.package.resolve()
    fixture = package / "OpenJK/rend2-ssao.cfg"
    if not fixture.is_file() or fixture.read_bytes() != (root / "scripts/rend2-ssao.cfg").read_bytes():
        parser.error("Stage scripts/rend2-ssao.cfg in PACKAGE/OpenJK before this test")
    output = root / "build/smoke"
    output.mkdir(parents=True, exist_ok=True)
    suite = Path(tempfile.mkdtemp(prefix="ssao.", dir=output))
    print(f"SSAO results: {suite}", flush=True)
    masks = {}
    # Wall and floor: source pixels [224,416) x [224,448), away from windows and sky.
    x0, y0, x1, y1 = 56, 56, 104, 112
    print(f"Scene ROI at 160 x 120: {(x0, y0, x1, y1)}; AO masks use the full image", flush=True)
    phases = ("ambient", "broad", "strength_zero", "strength_high", "radius_small", "radius_large",
              "raw", "filtered", "restored", "prepass", "prepass_scene")
    for msaa in (0, 4):
        case = suite / f"msaa{msaa}"
        command = ["bash", str(root / "scripts/smoke-sp.sh"), str(package), "t2_wedge"]
        for name, value in dict(r_ssao=1, r_ext_multisample=msaa, r_normalMapping=1,
                                r_specularMapping=1, r_debugContext=1, r_ignoreGLErrors=0).items():
            command += ["+set", name, str(value)]
        command += ["+exec", "rend2-ssao.cfg"]
        env = dict(os.environ, OJK_SMOKE_ROOT=str(case), OJK_SMOKE_RENDERER="rdsp-rend2",
                   OJK_SMOKE_TIMEOUT="600", OJK_SMOKE_WAIT="10", OJK_SMOKE_DISPLAY="640x480")
        # Do not inherit the benchmark's offscreen SDL/EGL selection.
        env["SDL_VIDEODRIVER"] = "x11"
        env.pop("EGL_PLATFORM", None)
        subprocess.run(command, env=env, check=True)
        logs = list(case.glob("t2_wedge.*/console.log"))
        if len(logs) != 1:
            raise RuntimeError(f"Missing unique log: {case}")
        log = logs[0]
        text = re.sub(r"\^[0-9]", "", log.read_text(errors="replace"))
        text = re.sub(r"(?m)^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} ", "", text)
        if re.search(r"OpenGL -> [^\n]*\[(?:Error|Undefined)\]|GL_INVALID_\w+|GL_OUT_OF_MEMORY|"
                     r"Cheats are not enabled|Segmentation fault|recursive error|core dumped", text):
            raise RuntimeError(f"GL error or fixture failure: {log}")
        if not re.search(rf'Cvar r_ext_multisample = "{msaa}"', text):
            raise RuntimeError(f"MSAA {msaa} was not confirmed: {log}")
        images = {}
        start = 0
        for phase in phases:
            begin, end = f"OJK_SSAO_BEGIN_{phase.upper()}", f"OJK_SSAO_END_{phase.upper()}"
            # Match whole lines: PREPASS is also a prefix of PREPASS_SCENE.
            markers = [list(re.finditer(rf"(?m)^{marker}\s*$", text)) for marker in (begin, end)]
            if any(len(matches) != 1 for matches in markers):
                raise RuntimeError(f"Missing or repeated {phase} markers: {log}")
            left, right = markers[0][0], markers[1][0]
            name = f"ssao_{phase}.png"
            if (not start <= left.start() < right.start()
                    or f"Wrote screenshots/{name}" not in text[left.end():right.start()]):
                raise RuntimeError(f"Missing or out-of-order {phase} capture: {log}")
            start = right.end()
            image = log.parent / "profile/OpenJK/screenshots" / name
            header = image.read_bytes()[:24]
            if header[:16] != b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR" or struct.unpack(">II", header[16:24]) != (640, 480):
                raise RuntimeError(f"Expected a 640 x 480 PNG: {image}")
            pixels = {}
            for fmt, filters, size in (("rgb24", [], 640 * 480 * 3),
                                       ("gray", ["-vf", "scale=160:120"], 160 * 120)):
                pixels[fmt] = subprocess.run(
                    ["ffmpeg", "-v", "error", "-xerror", "-i", str(image), *filters,
                     "-frames:v", "1", "-pix_fmt", fmt, "-f", "rawvideo", "-"],
                    stdout=subprocess.PIPE, check=True).stdout
                if len(pixels[fmt]) != size:
                    raise RuntimeError(f"Invalid capture size: {image}")
            rgb, gray = pixels["rgb24"], pixels["gray"]
            colored = sum(max(rgb[i:i+3]) - min(rgb[i:i+3]) > 2
                          for i in range(0, len(rgb), 3)) / (640 * 480)
            span = max(gray) - min(gray)
            ao = phase in ("raw", "filtered", "radius_small", "radius_large")
            mask = {(i % 160, i // 160) for i, value in enumerate(gray) if value < 250}
            coverage = len(mask) / len(gray)
            check(f"MSAA {msaa} {phase}", dict(colored=colored, span=span, below_250=coverage),
                  (colored <= 0.0001 and span >= 4 and coverage >= 0.001) if ao
                  else (colored >= 0.01 and span >= 16))
            if ao:
                masks[msaa, phase] = mask
            else:
                images[phase] = bytes(gray[y * 160 + x] for y in range(y0, y1) for x in range(x0, x1))
        if "OJK_SSAO_DONE" not in text[start:] or "RE_Shutdown" in text[:start].split("OJK_SSAO_BEGIN_AMBIENT", 1)[-1]:
            raise RuntimeError(f"Incomplete fixture or unexpected restart: {log}")
        subprocess.run(["ffmpeg", "-v", "error", "-xerror", "-i",
                        str(log.parent / "profile/OpenJK/screenshots/ssao_before_restart.png"),
                        "-frames:v", "1", "-f", "null", "-"], check=True)
        compare(f"MSAA {msaa} round trip", images["ambient"], images["restored"])
        compare(f"MSAA {msaa} prepass off", images["prepass"], images["prepass_scene"])
        compare(f"MSAA {msaa} zero strength", images["strength_zero"], images["prepass_scene"])
        check(f"MSAA {msaa} strength monotonicity", "strength 2 is darker than strength 1",
              sum(images["strength_high"]) < sum(images["broad"]))
        check(f"MSAA {msaa} radius", "larger radius has greater AO coverage",
              len(masks[msaa, "radius_large"]) > len(masks[msaa, "radius_small"]))
        delta = [a - b for a, b in zip(images["ambient"], images["broad"])]
        darkened = sum(d > 2 for d in delta) / len(delta)
        noise = sum(abs(a - b) > 2 for a, b in zip(images["ambient"], images["restored"])) / len(delta)
        mean = sum(delta) / len(delta)
        check(f"MSAA {msaa} broader lighting", dict(darkened=darkened, noise=noise, mean_drop=mean),
              darkened >= 0.001 and darkened > 2 * noise and mean > 0.01)
    for phase in ("raw", "filtered"):
        a, b = masks[0, phase], masks[4, phase]
        # One reduced pixel permits a four-pixel edge shift in the source image.
        misses = []
        for first, second in ((a, b), (b, a)):
            expanded = {(x + dx, y + dy) for x, y in second
                        for dx in (-1, 0, 1) for dy in (-1, 0, 1)}
            misses.append(len(first - expanded) / len(first))
        bounds = [(min(x for x, y in mask), min(y for x, y in mask),
                   max(x for x, y in mask), max(y for x, y in mask)) for mask in (a, b)]
        shift = max(abs(x - y) for x, y in zip(*bounds))
        ratio = len(a) / len(b)
        check(f"MSAA mask {phase}", dict(unmatched=misses, bounds=bounds, area_ratio=ratio),
              max(misses) <= 0.15 and shift <= 4 and 0.5 <= ratio <= 2)
    print(f"PASS: SSAO static captures. Results: {suite}")


if __name__ == "__main__":
    main()
