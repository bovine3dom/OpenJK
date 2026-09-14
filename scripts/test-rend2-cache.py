#!/usr/bin/env python3
"""Test Rend2 shader reuse, source changes, context reset, and compile failure."""

import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time


STATS = re.compile(r"GLSL programs: (\d+) linked, (\d+) reused, (\d+) unused released")
GL_ERRORS = re.compile(r"OpenGL -> [^\n]*\[(?:Error|Undefined)\]|GL_INVALID_\w+|GL_OUT_OF_MEMORY")


def stats(text):
    return [tuple(map(int, match)) for match in STATS.findall(text)]


def section(text, begin, end):
    if text.count(begin) != 1 or text.count(end) != 1:
        raise RuntimeError(f"Missing or repeated marker: {begin}, {end}")
    start = text.index(begin) + len(begin)
    stop = text.index(end)
    if stop < start:
        raise RuntimeError(f"Markers are out of order: {begin}, {end}")
    return text[start:stop]


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=root / "build/ready")
    args = parser.parse_args()
    package = args.package.resolve()
    for name in ("rend2-cache.cfg", "rend2-cache-failure.cfg"):
        fixture = package / "OpenJK" / name
        if not fixture.is_file() or fixture.read_bytes() != (root / "scripts" / name).read_bytes():
            parser.error(f"Stage scripts/{name} in PACKAGE/OpenJK before this test")
    output = root / "build/smoke"
    output.mkdir(parents=True, exist_ok=True)
    suite = Path(tempfile.mkdtemp(prefix="rend2-cache.", dir=output))
    print(f"Rend2 cache results: {suite}", flush=True)

    for shader_name in (None, "generic", "lightall"):
        failure = shader_name is not None
        case = suite / (f"{shader_name}-failure" if failure else "reuse")
        fixture = "rend2-cache-failure.cfg" if failure else "rend2-cache.cfg"
        command = ["bash", str(root / "scripts/smoke-sp.sh"), str(package), "t2_wedge",
                   "+set", "com_maxfps", "10", "+set", "r_debugContext", "1",
                   "+set", "r_ignoreGLErrors", "0", "+set", "r_ssao", "1",
                   "+set", "r_externalGLSL", "0", "+exec", fixture]
        env = dict(os.environ, OJK_SMOKE_ROOT=str(case), OJK_SMOKE_RENDERER="rdsp-rend2",
                   OJK_SMOKE_TIMEOUT="600", OJK_SMOKE_WAIT="10", OJK_SMOKE_DISPLAY="640x480")
        injected = False
        with subprocess.Popen(command, env=env) as process:
            while process.poll() is None:
                if failure and not injected:
                    logs = list(case.glob("t2_wedge.*/console.log"))
                    if len(logs) == 1 and "OJK_CACHE_OVERRIDE_READY" in logs[0].read_text(errors="replace"):
                        profile = logs[0].parent / "profile/OpenJK"
                        suffix = "vp" if shader_name == "generic" else "fp"
                        shader = profile / f"glsl/{shader_name}_{suffix}.glsl"
                        shader.parent.mkdir(parents=True, exist_ok=True)
                        shader.write_text("#error OJK_CACHE_MALFORMED_OVERRIDE\n", encoding="ascii")
                        # Publish the gate file only after both writes are complete.
                        ready = profile / "rend2-cache-ready.tmp"
                        ready.write_text('set rend2_cache_gate ""\n', encoding="ascii")
                        ready.replace(profile / "rend2-cache-ready.cfg")
                        injected = True
                time.sleep(0.05)
        logs = list(case.glob("t2_wedge.*/console.log"))
        if len(logs) != 1:
            raise RuntimeError(f"Missing unique log: {case}")
        log = logs[0]
        text = log.read_text(errors="replace")
        if ("----- rdsp-rend2 -----" not in text or "failed: trying to load fallback renderer" in text
                or 'Trying to load "rdsp-vanilla_' in text
                or re.search(r"Segmentation fault|recursive error|core dumped", text, re.IGNORECASE)):
            raise RuntimeError(f"Renderer failure or crash: {log}")

        first_marker = "OJK_CACHE_OVERRIDE_READY" if failure else "OJK_CACHE_BEGIN_CHANGED"
        baseline = text.split(first_marker, 1)[0]
        counts = stats(baseline)
        if (not counts or counts[0][0] <= 0 or counts[0][1:] != (0, 0)
                or not any(linked == 0 and reused > 0 and unused == 0 for linked, reused, unused in counts)
                or "CM_LoadMap( maps/t2_wedge.bsp, 1 )" not in baseline or GL_ERRORS.search(baseline)):
            raise RuntimeError(f"Missing initial link or initial map cache hit: {log}")

        if failure:
            section(text, "OJK_CACHE_OVERRIDE_READY", "OJK_CACHE_BEGIN_FAILURE")
            failed = text.split("OJK_CACHE_BEGIN_FAILURE", 1)[1]
            error = failed.find(f"Couldn't compile shader '{shader_name}'")
            diagnostic = failed.find("OJK_CACHE_MALFORMED_OVERRIDE")
            if (process.returncode == 0 or not injected or error < 0
                    or not 0 <= diagnostic < error
                    or "RE_Shutdown( 1 )" not in failed[diagnostic:]
                    or "RE_Shutdown( 0 )" not in failed[:diagnostic]
                    or "OJK_CACHE_UNEXPECTED_SUCCESS" in text or stats(failed)
                    or "----- finished R_Init -----" in failed):
                raise RuntimeError(f"Compile failure did not stop and shut down the renderer: {log}")
            print(f"PASS: {shader_name} compile failure and renderer shutdown. Log: {log}", flush=True)
            continue

        if process.returncode != 0 or GL_ERRORS.search(text):
            raise RuntimeError(f"Smoke test or GL check failed: {log}")
        previous = first_marker
        for phase in ("changed", "reused", "restarted"):
            begin, end = f"OJK_CACHE_BEGIN_{phase.upper()}", f"OJK_CACHE_{phase.upper()}"
            if phase != "changed":
                section(text, previous, begin)
            segment = section(text, begin, end)
            counts = stats(segment)
            if len(counts) != 1:
                raise RuntimeError(f"Expected one cache report in {phase}: {log}")
            linked, reused, unused = counts[0]
            # SSAO changes the common header of every shader stage.
            valid = {"changed": linked > 0 and reused == 0 and unused > 0,
                     "reused": linked == 0 and reused > 0 and unused == 0,
                     "restarted": linked > 0 and reused == 0 and unused == 0}[phase]
            required = "RE_Shutdown( 1 )" if phase == "restarted" else "CM_LoadMap( maps/t2_wedge.bsp, 1 )"
            if not valid or required not in segment or "----- finished R_Init -----" not in segment:
                raise RuntimeError(f"Invalid {phase} cache report {counts}: {log}")
            name = f"rend2_cache_{phase}.png"
            if f"Wrote screenshots/{name}" not in segment:
                raise RuntimeError(f"Missing {phase} capture: {log}")
            image = log.parent / "profile/OpenJK/screenshots" / name
            pixels = subprocess.run(["ffmpeg", "-v", "error", "-xerror", "-i", str(image),
                                     "-vf", "scale=64:48", "-frames:v", "1", "-pix_fmt", "gray",
                                     "-f", "rawvideo", "-"], stdout=subprocess.PIPE, check=True).stdout
            if (len(pixels) != 64 * 48 or sum(value > 8 for value in pixels) < len(pixels) * 0.01
                    or max(pixels) - min(pixels) < 16):
                raise RuntimeError(f"Black or uniform scene: {image}")
            previous = end
        print(f"PASS: Source change, map reuse, and context reset. Log: {log}", flush=True)


if __name__ == "__main__":
    main()
