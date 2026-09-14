#!/usr/bin/env python3
"""Measure SP engine work with an external console controller."""

import argparse
import json
import math
import os
import platform
from pathlib import Path
import queue
import re
import signal
import statistics
import struct
import subprocess
import tempfile
import threading
import time


FRAME = re.compile(r"^fr:(\d+)\s+all:\s*(-?\d+).*\brf:\s*(-?\d+)\s+bk:\s*(-?\d+)")
SHADERS = re.compile(r"loaded (\d+) GLSL shaders \((\d+) gen (\d+) light (\d+) etc\) in\s*([\d.]+) seconds")
CACHE_NOTE = "Controlled warmups and fresh profiles; shared cache; OS/driver cache state uncontrolled. Not cold runs."


def percentiles(values):
    values = sorted(values)
    return {f"p{p}": values[math.ceil(len(values) * p / 100) - 1] for p in (50, 95, 99)}


def run(args, suite, index, settings):
    directory = suite / f"run-{index}"
    profile = directory / "profile"
    (profile / "OpenJK").mkdir(parents=True)
    # Avoid the engine's bounded startup command list and asset-side autoexec files.
    (profile / "OpenJK/openjk_sp.cfg").write_text("".join(
        f'set {name} "{value}"\n' for name, value in settings.items()))
    (profile / "OpenJK/autoexec_sp.cfg").write_text("// Controlled benchmark profile.\n")
    env = dict(os.environ, OJK_PROFILE=str(profile), SDL_VIDEODRIVER=args.video_driver,
               EGL_PLATFORM="surfaceless", SDL_AUDIODRIVER="dummy",
               XDG_CACHE_HOME=str(suite / "cache"), MESA_SHADER_CACHE_DIR=str(suite / "cache/mesa"))
    env.pop("LIBGL_ALWAYS_SOFTWARE", None)
    if args.video_driver != "offscreen":
        env.pop("EGL_PLATFORM", None)
    command = ["bash", str(args.package / "launch-sp.sh"), str(args.assets)]
    command += ["+set", "activeAction", "echo OJK_BENCH_ACTIVE", "+devmap", args.map]
    result = {"run": index, "settings_requested": settings, "command": command,
              "package_build_id": (args.package / "build-id.txt").read_text().strip(),
              "system": platform.platform(),
              "cache_note": CACHE_NOTE, "scene": "t2_wedge fixed natural view; native NPCs active",
              "environment": {k: env.get(k) for k in ("SDL_VIDEODRIVER", "EGL_PLATFORM",
                  "SDL_AUDIODRIVER", "LIBGL_ALWAYS_SOFTWARE", "XDG_CACHE_HOME", "MESA_SHADER_CACHE_DIR")}}
    lines = []
    events = queue.Queue()
    started = time.monotonic()
    process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, env=env, start_new_session=True,
                               text=True, encoding="utf-8", errors="replace", bufsize=1)
    stdin, stdout = process.stdin, process.stdout
    assert stdin is not None and stdout is not None

    def read_output():
        try:
            for line in stdout:
                received = time.monotonic()
                lines.append(line)
                events.put((received, line.strip()))
        finally:
            events.put((time.monotonic(), None))

    reader = threading.Thread(target=read_output, daemon=True)
    reader.start()

    def send(command):
        stdin.write(command + "\n")
        stdin.flush()

    def receive(deadline):
        try:
            event = events.get(timeout=max(0, deadline - time.monotonic()))
        except queue.Empty:
            return None
        if event[1] is None:
            raise RuntimeError("Unexpected console EOF")
        return event

    def wait_for(marker):
        deadline = time.monotonic() + args.timeout
        while time.monotonic() < deadline:
            event = receive(deadline)
            if event and event[1] == marker:
                return event[0]
        raise RuntimeError(f"Timeout waiting for {marker}")

    try:
        active = wait_for("OJK_BENCH_ACTIVE")
        result["load_total_receipt_seconds"] = active - started
        text = "".join(lines)
        identity = dict(re.findall(r"^(GL_VENDOR|GL_RENDERER|GL_VERSION):\s*(.+)$", text, re.M))
        gpu = identity.get("GL_RENDERER", "")
        if args.gpu.lower() not in gpu.lower() or re.search(r"llvmpipe|softpipe", gpu, re.I):
            raise RuntimeError(f"Hardware check failed: GL_RENDERER={gpu!r}")
        modes = re.findall(r"MODE:\s*-?\d+,\s*(\d+) x (\d+)", text)
        if not modes or tuple(map(int, modes[-1])) != (args.width, args.height):
            raise RuntimeError(f"Actual MODE does not match requested dimensions: {modes}")
        if "failed: trying to load fallback renderer" in text or (
                args.renderer == "rdsp-rend2" and "----- rdsp-rend2 -----" not in text):
            raise RuntimeError("Renderer selection failed")
        result["gpu"] = identity
        result["actual_mode"] = [int(v) for v in modes[-1]]
        result["glsl_summary"] = [dict(zip(("total", "generic", "light", "other", "seconds"),
            [int(v) for v in m[:4]] + [float(m[4])])) for m in SHADERS.findall(text)]
        send("exitview; god; setviewpos 2688 640 -60 315; set cg_thirdPerson 1; "
             "set cg_draw2D 0; set d_npcfreeze 0; com_speeds 0; echo OJK_BENCH_SCENE")
        scene = wait_for("OJK_BENCH_SCENE")
        deadline = scene + args.warmup
        while time.monotonic() < deadline:
            receive(deadline)
        send("echo OJK_BENCH_BEGIN; com_speeds 1")
        begin = wait_for("OJK_BENCH_BEGIN")
        samples = []
        deadline = begin + args.seconds
        stopping = False
        while True:
            if time.monotonic() >= deadline:
                if stopping:
                    raise RuntimeError("Timeout waiting for OJK_BENCH_END")
                send("com_speeds 0; echo OJK_BENCH_END")
                stopping = True
                deadline = time.monotonic() + args.timeout
            event = receive(deadline)
            if event is None:
                continue
            received, line = event
            if line == "OJK_BENCH_END":
                end = received
                break
            match = FRAME.match(line)
            if match:
                samples.append(tuple(map(int, match.groups())))
        if not samples or any(b[0] != a[0] + 1 for a, b in zip(samples, samples[1:])):
            raise RuntimeError("Missing or discontinuous engine work samples")
        result.update(measured_receipt_seconds=end - begin, frame_count=len(samples),
                      approximate_throughput_fps=len(samples) / (end - begin),
                      engine_work_ms=percentiles([s[1] for s in samples]),
                      renderer_frontend_work_ms=percentiles([s[2] for s in samples]),
                      renderer_backend_work_ms=percentiles([s[3] for s in samples]))
        send("screenshot_png benchmark_end")
        wait_for("Wrote screenshots/benchmark_end.png")
        result["reload_receipt_seconds"] = []
        for index in range(args.reloads):
            send(f'set activeAction "echo OJK_RELOAD_READY_{index}"; '
                 f'echo OJK_RELOAD_BEGIN_{index}; devmap {args.map}')
            load_begin = wait_for(f"OJK_RELOAD_BEGIN_{index}")
            load_end = wait_for(f"OJK_RELOAD_READY_{index}")
            result["reload_receipt_seconds"].append(load_end - load_begin)
        send("quit")
        if process.wait(timeout=args.timeout) != 0:
            raise RuntimeError("Game exited with a nonzero status")
        reader.join(timeout=args.timeout)
        result["program_cache"] = [dict(zip(("linked", "reused", "unused_released"), map(int, values)))
            for values in re.findall(r"GLSL programs: (\d+) linked, (\d+) reused, (\d+) unused released", "".join(lines))]
        image = profile / "OpenJK/screenshots/benchmark_end.png"
        with image.open("rb") as stream:
            header = stream.read(24)
        if header[:16] != b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR" or len(header) != 24:
            raise RuntimeError("Invalid screenshot PNG header")
        dimensions = struct.unpack(">II", header[16:24])
        if dimensions != (args.width, args.height):
            raise RuntimeError(f"Screenshot dimensions do not match: {dimensions}")
        result["screenshot"] = str(image)
        if args.ffprobe:
            probe = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0",
                "-count_frames", "-show_entries", "stream=width,height,nb_read_frames",
                "-of", "json", str(image)], capture_output=True, text=True,
                check=True, timeout=args.timeout)
            streams = json.loads(probe.stdout)["streams"]
            if (probe.stderr.strip() or len(streams) != 1 or streams[0].get("nb_read_frames") != "1"
                    or (streams[0]["width"], streams[0]["height"]) != dimensions):
                raise RuntimeError("Screenshot decode probe failed")
            result["ffprobe"] = streams[0]
        result["status"] = "ok"
        return result
    except BaseException as error:
        result.update(status="failed", error=str(error))
        raise
    finally:
        # Kill the session even if the launcher has exited but a child holds the pipe.
        if process.poll() is None or reader.is_alive():
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        process.wait()
        reader.join(timeout=5)
        stdin.close()
        stdout.close()
        (directory / "console.log").write_text("".join(lines))
        (directory / "result.json").write_text(json.dumps(result, indent=2) + "\n")


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=root / "build/ready")
    parser.add_argument("--renderer", choices=("rdsp-vanilla", "rdsp-rend2"), default="rdsp-rend2")
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--seconds", type=float, default=15)
    parser.add_argument("--warmup", type=float, default=5)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--reloads", type=int, default=0, help="Measure same-process map reloads after the frame test")
    parser.add_argument("--ssao", type=int, choices=(0, 1), default=1)
    parser.add_argument("--shadows", type=int, choices=(1, 2, 3), default=3)
    parser.add_argument("--map", choices=("t2_wedge",), default="t2_wedge")
    parser.add_argument("--video-driver", choices=("offscreen", "x11", "wayland"), default="offscreen")
    parser.add_argument("--gpu", default="Intel", help="Required GL_RENDERER substring")
    parser.add_argument("--timeout", type=float, default=180, help="Startup/command/exit timeout in seconds")
    parser.add_argument("--ffprobe", action="store_true", help="Decode the final PNG with ffprobe")
    parser.add_argument("--cvar", nargs=2, action="append", default=[], metavar=("NAME", "VALUE"),
                        help="Override a numeric or single-word setting for an A/B test")
    args = parser.parse_args()
    for name, value in args.cvar:
        if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name) or not re.fullmatch(r"[A-Za-z0-9_.+-]+", value):
            parser.error("Cvar overrides require a name and a numeric or single-word value")
    if (not 64 <= args.width <= 16384 or not 64 <= args.height <= 16384 or args.runs < 1 or args.reloads < 0
            or not args.gpu.strip() or any(not math.isfinite(v) for v in
                (args.seconds, args.warmup, args.timeout))
            or args.seconds <= 0 or args.warmup < 0 or args.timeout <= 0):
        parser.error("Invalid dimensions, count, duration, timeout, or GPU string")
    args.package = args.package.resolve()
    args.assets = Path(os.environ.get("OJK_ASSETS", root / "GameData")).resolve()
    if not (args.package / "launch-sp.sh").is_file():
        parser.error("Package has no launch-sp.sh")
    output = root / "build/benchmark-sp"
    output.mkdir(parents=True, exist_ok=True)
    suite = Path(tempfile.mkdtemp(prefix=args.renderer + ".", dir=output))
    (suite / "cache").mkdir()
    print(f"Results: {suite}", flush=True)
    settings = dict(cl_renderer=args.renderer, developer=0, logfile=0, com_speeds=0,
        speedslog=0, com_timestamps=0, com_ansiColor=0, cl_avidemo=0,
        com_maxfps=0, com_maxfpsUnfocused=0, com_maxfpsMinimized=0,
        r_mode=-1, r_customwidth=args.width, r_customheight=args.height, r_fullscreen=0,
        r_swapInterval=0, s_initsound=0, cg_fov=80, cg_fovAspectAdjust=1,
        cg_thirdPerson=1, cg_draw2D=0, cg_shadows=args.shadows, r_ssao=args.ssao,
        r_normalMapping=1, r_specularMapping=1, r_parallaxMapping=0, r_picmip=0,
        r_dynamiclight=1, r_hdr=1, r_toneMap=1, r_autoExposure=1,
        r_dynamicGlow=0, r_speeds=0, r_debugContext=0, r_arb_buffer_storage=0,
        r_ext_multisample=0)
    settings.update(args.cvar)
    results = []
    try:
        for index in range(1, args.runs + 1):
            results.append(run(args, suite, index, settings))
    finally:
        aggregate = dict(arguments={k: str(v) if isinstance(v, Path) else v
                                   for k, v in vars(args).items()}, cache_note=CACHE_NOTE,
                         completed_runs=len(results), requested_runs=args.runs, runs=results)
        if results:
            aggregate["median_approximate_throughput_fps"] = statistics.median(
                r["approximate_throughput_fps"] for r in results)
        (suite / "result.json").write_text(json.dumps(aggregate, indent=2) + "\n")
    print(f"Median approximate throughput: {aggregate['median_approximate_throughput_fps']:.2f} fps")


if __name__ == "__main__":
    main()
