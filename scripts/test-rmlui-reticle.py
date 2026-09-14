#!/usr/bin/env python3
"""Test the reticle in an existing package. Run all smoke tests in sequence."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def image_pixels(path, width, height):
    result = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", str(path), "-frames:v", "1",
         "-f", "rawvideo", "-pix_fmt", "rgb24", "-threads", "1", "-"],
        check=True, capture_output=True,
    )
    if len(result.stdout) != width * height * 3:
        raise ValueError(f"Unexpected image size: {path}")
    return result.stdout


def measure(pixels, width, height):
    white = set()
    black = set()
    for y in range(-56, 56):
        for x in range(-56, 56):
            index = ((height // 2 + y) * width + width // 2 + x) * 3
            rgb = pixels[index:index + 3]
            if min(rgb) >= 240:
                white.add((x, y))
            if max(rgb) <= 16:
                black.add((x, y))
    bbox = None
    if white:
        xs, ys = zip(*white)
        bbox = [min(xs), min(ys), max(xs), max(ys)]
    return white, black, bbox


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "build" / "reticle-tests")
    args = parser.parse_args()
    package = args.package.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="reticle.", dir=args.output.resolve()))
    assets = Path(os.environ.get("OJK_ASSETS", ROOT / "GameData")).resolve()
    overlay = output / "assets"
    overlay.mkdir()
    (overlay / "base").symlink_to(assets / "base", target_is_directory=True)
    (overlay / "OpenJK").mkdir()
    if (assets / "OpenJK").is_dir():
        for item in (assets / "OpenJK").iterdir():
            if item.name != "rmlui-reticle.cfg":
                (overlay / "OpenJK" / item.name).symlink_to(item)
    (overlay / "OpenJK" / "rmlui-reticle.cfg").symlink_to(ROOT / "scripts" / "rmlui-reticle.cfg")
    report = {"package": str(package), "runs": [], "failures": []}
    print(f"Reticle-test output: {output}", flush=True)
    for renderer in ("rdsp-vanilla", "rdsp-rend2"):
        for width, height in ((960, 720), (1280, 720)):
            name = f"{renderer}-{width}x{height}"
            run = output / name
            run.mkdir()
            entry = {"name": name, "checks": [], "images": {}}
            report["runs"].append(entry)

            def check(label, condition):
                entry["checks"].append({"name": label, "passed": bool(condition)})
                if not condition:
                    report["failures"].append(f"{name}: {label}")

            command = ["bash", str(ROOT / "scripts" / "smoke-sp.sh"), str(package),
                       "t1_sour", "+set", "r_mode", "-1", "+set", "r_customwidth", str(width),
                       "+set", "r_customheight", str(height), "+exec", "rmlui-reticle.cfg"]
            states = ("normal", "scaled", "hidden", "legacy", "legacy-hidden", "restored", "restarted")
            env = dict(os.environ, OJK_SMOKE_DISPLAY=f"{width}x{height}",
                       OJK_SMOKE_RENDERER=renderer, OJK_SMOKE_ROOT=str(run),
                       OJK_SMOKE_TIMEOUT="240", OJK_ASSETS=str(overlay))
            print(f"RUN: {name}", flush=True)
            try:
                result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=260)
                (run / "smoke.log").write_text(result.stdout + result.stderr)
                check("smoke runner", result.returncode == 0)
                if result.returncode:
                    continue
                smoke = next(run.glob("t1_sour.*"))
                samples = {}
                captures = {}
                for state in states:
                    image = smoke / "profile" / "OpenJK" / "screenshots" / f"{state}.png"
                    captures[state] = image_pixels(image, width, height)
                    white, black, bbox = measure(captures[state], width, height)
                    samples[state] = white
                    entry["images"][state] = {"path": str(image), "white_bbox": bbox,
                                               "white_pixels": len(white)}
                    if state in ("normal", "scaled", "restored", "restarted"):
                        size = 96 if state == "scaled" else 48
                        half = size // 2
                        expected = [-half + 1, -half + 1, half - 2, half - 2]
                        check(f"{state}: centered square {size}px outline",
                              bbox is not None and all(abs(a - b) <= 1 for a, b in zip(bbox, expected)))
                        # Test the white arm interiors and the black outer edges.
                        arms = [(0, -half + 3), (0, half - 4), (-half + 3, 0), (half - 4, 0)]
                        edges = [(0, -half), (0, half - 1), (-half, 0), (half - 1, 0)]
                        check(f"{state}: four white arms", all(point in white for point in arms))
                        check(f"{state}: four black edges", all(point in black for point in edges))
                for state in ("hidden", "legacy-hidden"):
                    check(f"{state}: no white reticle pixels", not samples[state])
                # The legacy texture is translucent. Test its change, not opaque white.
                changed = 0
                for y in range(height // 2 - 24, height // 2 + 24):
                    for x in range(width // 2 - 32, width // 2 + 32):
                        i = (y * width + x) * 3
                        changed += all(captures["legacy"][i + c] - captures["legacy-hidden"][i + c] > 8
                                       for c in range(3))
                check("legacy: visible center artwork", changed >= 16)
                check("legacy: not the RmlUi artwork", samples["legacy"] != samples["normal"])
                log = (smoke / "console.log").read_text(errors="replace")
                check("vid_restart: RmlUi initialized again", log.count("RmlUi: reticle ready") >= 2)
                check("no RmlUi initialization failure", "reticle initialization failed" not in log)
                check("no RmlUi resource leaks", "Leaking " not in log)
            except (OSError, ValueError, subprocess.SubprocessError, StopIteration) as error:
                report["failures"].append(f"{name}: {error}")
            finally:
                (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    for failure in report["failures"]:
        print(f"FAIL: {failure}")
    print(f"{'FAIL' if report['failures'] else 'PASS'}: {output / 'results.json'}")
    return bool(report["failures"])


if __name__ == "__main__":
    raise SystemExit(main())
