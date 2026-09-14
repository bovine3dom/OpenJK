#!/usr/bin/env python3
"""Test the reticle in an existing package. Run all smoke tests in sequence."""

import argparse
import json
import math
import os
import re
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


def measure(pixels, background, width, height):
    bright = set()
    for y in range(-56, 56):
        for x in range(-56, 56):
            index = ((height // 2 + y) * width + width // 2 + x) * 3
            if all(pixels[index + c] - background[index + c] > 25 for c in range(3)):
                bright.add((x, y))
    bbox = None
    if bright:
        xs, ys = zip(*bright)
        bbox = [min(xs), min(ys), max(xs), max(ys)]
    return bright, bbox


def check_hud(captures, width, height, check):
    baseline = captures["hud_idle"]

    def arc_pixels(state, radius, blue, angle=None):
        count = 0
        for y in range(-24, 24):
            for x in range(-24, 24):
                r = math.hypot(x + 0.5, y + 0.5)
                if not radius - 3 <= r <= radius + 1:
                    continue
                if angle is not None and abs(math.degrees(math.atan2(y + 0.5, x + 0.5)) - angle) > 25:
                    continue
                i = ((height // 2 + y) * width + width // 2 + x) * 3
                red = captures[state][i] - baseline[i]
                green = captures[state][i + 1] - baseline[i + 1]
                blue_delta = captures[state][i + 2] - baseline[i + 2]
                # Separate the strong stance's red from yellow saber illumination.
                if angle == -30:
                    count += red > 15 and red - green > 20 and red - blue_delta > 10
                else:
                    count += (blue_delta > 15 and blue_delta - red > 10) if blue else (red > 15 and red - blue_delta > 10)
        return count

    check("Force: inner blue arc while recharging", arc_pixels("hud_resources", 11.25, True) >= 4)
    check("ammo: outer yellow arc after pickup", arc_pixels("hud_resources", 15.75, False) >= 4)
    for state, angle, blue in (("hud_fast", -150, True), ("hud_medium", -90, False), ("hud_strong", -30, False)):
        check(f"{state}: selected stance position", arc_pixels(state, 15.75, blue, angle) >= 4)
    check("stance: visible while attacking", arc_pixels("hud_saber_active", 15.75, False, -30) >= 4)
    check("ammo: visible while firing", arc_pixels("hud_ammo_active", 15.75, False) >= 4)
    check("stance: fades at idle", arc_pixels("hud_stance_idle", 15.75, False, -30) < 4)
    check("ammo: fades at idle", arc_pixels("hud_ammo_idle", 15.75, False) < 4)
    check("Force: fades after recharge", arc_pixels("hud_ammo_idle", 11.25, True) < 4)

    def panel_pixels(state):
        return sum(captures[state][i + 2] - captures[state][i] > 40
                   for y in range(height * 3 // 4, height)
                   for x in range(width * 3 // 4, width)
                   for i in [(y * width + x) * 3])

    check("legacy panel: hidden with contextual HUD", panel_pixels("hud_legacy") > panel_pixels("hud_idle") + 50)
    for state in ("hud_noreticle", "hud_legacyreticle"):
        check(f"{state}: restores the panel", panel_pixels(state) > panel_pixels("hud_idle") + 50)


def check_vitals(captures, width, height, check):
    baseline = captures["vitals_idle"]

    def colored_pixels(state, health):
        count = 0
        background = captures["vitals_noreticle"] if state == "vitals_full_resources" else baseline
        color = (240, 115, 115) if health else (135, 215, 155)
        for y in range(0, 23):
            for x in range(-23, 23):
                if (x < 0) != health or not 18.5 <= math.hypot(x + 0.5, y + 0.5) <= 21:
                    continue
                i = ((height // 2 + y) * width + width // 2 + x) * 3
                delta = [captures[state][i + c] - background[i + c] for c in range(3)]
                # Fit the translucent color to the background, including channels it darkens.
                blend = [color[c] - background[i + c] for c in range(3)]
                alpha = sum(d * b for d, b in zip(delta, blend)) / max(1, sum(b * b for b in blend))
                count += 0.2 < alpha < 0.75 and max(abs(d - alpha * b) for d, b in zip(delta, blend)) < 4
        return count

    for state in ("vitals_damage", "vitals_pickup", "vitals_shield_hit", "vitals_full_resources"):
        check(f"{state}: health at lower left", colored_pixels(state, True) >= 3)
        # A 10% shield arc covers only one or two pixels at this scale.
        check(f"{state}: shields at lower right", colored_pixels(state, False) >= (1 if state == "vitals_shield_hit" else 3))
    for state in ("vitals_damage_idle", "vitals_pickup_idle", "vitals_recovered"):
        check(f"{state}: no persistent health arc", colored_pixels(state, True) < 3)
        check(f"{state}: no persistent shield arc", colored_pixels(state, False) < 3)
    for state in ("vitals_critical", "vitals_critical_restart"):
        check(f"{state}: persistent critical-health arc", colored_pixels(state, True) >= 3)
        check(f"{state}: empty shields do not persist", colored_pixels(state, False) < 3)

    def panel_pixels(state):
        return sum(captures[state][i + 2] - captures[state][i] > 40
                   for y in range(height * 3 // 4, height)
                   for x in range(width // 4)
                   for i in [(y * width + x) * 3])

    for state in ("vitals_legacy", "vitals_noreticle"):
        check(f"{state}: restores the left panel", panel_pixels(state) > panel_pixels("vitals_idle") + 50)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--hud", action="store_true", help="Test contextual resource rings and stance indicators")
    mode.add_argument("--vitals", action="store_true", help="Test contextual health and shield arcs")
    parser.add_argument("--output", type=Path, default=ROOT / "build" / "reticle-tests")
    parser.add_argument("--modern", action="store_true", help="Use GTAO and sample-shaded 4x MSAA in Rend2")
    parser.add_argument("--hardware", action="store_true", help="Use headless hardware EGL for Rend2")
    args = parser.parse_args()
    package = args.package.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="reticle.", dir=args.output.resolve()))
    assets = Path(os.environ.get("OJK_ASSETS", ROOT / "GameData")).resolve()
    overlay = output / "assets"
    fixture = "rmlui-vitals.cfg" if args.vitals else "rmlui-hud.cfg" if args.hud else "rmlui-reticle.cfg"
    overlay.mkdir()
    (overlay / "base").symlink_to(assets / "base", target_is_directory=True)
    (overlay / "OpenJK").mkdir()
    if (assets / "OpenJK").is_dir():
        for item in (assets / "OpenJK").iterdir():
            if item.name != fixture:
                (overlay / "OpenJK" / item.name).symlink_to(item)
    (overlay / "OpenJK" / fixture).symlink_to(ROOT / "scripts" / fixture)
    report = {"package": str(package), "runs": [], "failures": []}
    print(f"Reticle-test output: {output}", flush=True)
    for renderer in ("rdsp-vanilla", "rdsp-rend2"):
        hardware = args.hardware and renderer == "rdsp-rend2"
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
                        "+set", "r_customheight", str(height), "+exec", fixture]
            if args.modern and renderer == "rdsp-rend2":
                for key, value in dict(r_ssao=1, r_ssaoMethod=1, r_sampleShading=1,
                                       r_ext_multisample=4, r_normalMapping=1, r_specularMapping=1).items():
                    command += ["+set", key, str(value)]
            states = ("default", "normal", "scaled", "hidden", "legacy", "legacy-hidden", "restored", "restarted")
            if args.hud:
                states = ("hud_idle", "hud_legacy", "hud_resources", "hud_fast", "hud_medium", "hud_strong",
                          "hud_stance_idle", "hud_saber_active", "hud_ammo_active", "hud_ammo_idle",
                          "hud_noreticle", "hud_legacyreticle")
            elif args.vitals:
                states = ("vitals_idle", "vitals_legacy", "vitals_damage", "vitals_damage_idle",
                          "vitals_pickup", "vitals_pickup_idle", "vitals_critical", "vitals_critical_restart",
                          "vitals_recovered", "vitals_shield_hit", "vitals_full_resources", "vitals_noreticle")
            env = dict(os.environ, OJK_SMOKE_DISPLAY=f"{width}x{height}",
                       OJK_SMOKE_RENDERER=renderer, OJK_SMOKE_ROOT=str(run),
                       OJK_SMOKE_TIMEOUT="600", OJK_ASSETS=str(overlay))
            if hardware:
                command = ["timeout", "--kill-after=5s", "600s", "bash", str(package / "launch-sp.sh"),
                           str(overlay), "+safe", "+set", "cl_renderer", renderer,
                           "+set", "r_fullscreen", "0", "+set", "s_initsound", "0",
                           "+devmap", "t1_sour", *command[4:], "+wait", "10", "+quit"]
                env.update(SDL_VIDEODRIVER="offscreen", EGL_PLATFORM="surfaceless",
                           SDL_AUDIODRIVER="dummy", OJK_PROFILE=str(run / "profile"))
                env.pop("LIBGL_ALWAYS_SOFTWARE", None)
            print(f"RUN: {name}", flush=True)
            try:
                result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=620)
                (run / "smoke.log").write_text(result.stdout + result.stderr)
                check("smoke runner", result.returncode == 0)
                if result.returncode:
                    continue
                smoke = run if hardware else next(run.glob("t1_sour.*"))
                if hardware:
                    (smoke / "console.log").write_text(result.stdout + result.stderr)
                log = (smoke / "console.log").read_text(errors="replace")
                if hardware:
                    check("hardware renderer", "GL_RENDERER:" in log and not re.search(r"llvmpipe|softpipe", log, re.I))
                check("RmlUi initialized", "RmlUi: reticle ready" in log)
                check("no RmlUi initialization failure", "reticle initialization failed" not in log)
                check("no RmlUi resource leaks", "Leaking " not in log)
                check("no rejected geometry", "DrawUiGeometry:" not in log and "geometry exceeds" not in log)
                samples = {}
                captures = {}
                for state in states:
                    image = smoke / "profile" / "OpenJK" / "screenshots" / f"{state}.png"
                    captures[state] = image_pixels(image, width, height)
                if args.hud:
                    check_hud(captures, width, height, check)
                    continue
                if args.vitals:
                    check_vitals(captures, width, height, check)
                    continue
                for state in states:
                    bright, bbox = measure(captures[state], captures["legacy-hidden"], width, height)
                    samples[state] = bright
                    entry["images"][state] = {"bright_bbox": bbox, "bright_pixels": len(bright)}
                    if state in ("default", "normal", "scaled", "restored", "restarted"):
                        size = 12 if state == "scaled" else 4 if state == "default" else 6
                        half = size // 2
                        expected = [-half + 1, -half + 1, half - 2, half - 2]
                        check(f"{state}: centered {size}px dot",
                              bbox is not None and all(abs(a - b) <= 1 for a, b in zip(bbox, expected)))
                        check(f"{state}: filled center", (0, 0) in bright)
                        corners = [(-half, -half), (half - 1, -half),
                                   (-half, half - 1), (half - 1, half - 1)]
                        check(f"{state}: rounded corners", all(point not in bright for point in corners))
                        center = (height // 2 * width + width // 2) * 3
                        alpha = [(captures[state][center + c] - captures["legacy-hidden"][center + c]) /
                                 max(1, 255 - captures["legacy-hidden"][center + c]) for c in range(3)]
                        check(f"{state}: translucent center", all(0.5 < value < 0.8 for value in alpha))
                for state in ("hidden", "legacy-hidden"):
                    check(f"{state}: no bright reticle pixels", not samples[state])
                # The legacy texture is translucent. Test its change, not opaque white.
                changed = 0
                for y in range(height // 2 - 24, height // 2 + 24):
                    for x in range(width // 2 - 32, width // 2 + 32):
                        i = (y * width + x) * 3
                        changed += all(captures["legacy"][i + c] - captures["legacy-hidden"][i + c] > 8
                                       for c in range(3))
                check("legacy: visible center artwork", changed >= 16)
                check("legacy: not the RmlUi artwork", samples["legacy"] != samples["normal"])
                check("vid_restart: RmlUi initialized again", log.count("RmlUi: reticle ready") >= 2)
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
