#!/usr/bin/env python3
"""Check live material controls and generated-normal cache recovery on hardware."""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=root / "build/ready")
    args = parser.parse_args()
    package = args.package.resolve()
    fixture = "rend2-materials.cfg"
    if (package / "OpenJK" / fixture).read_bytes() != (root / "scripts" / fixture).read_bytes():
        parser.error("Build a package with the current material fixture")
    output = root / "build/smoke"
    output.mkdir(parents=True, exist_ok=True)
    suite = Path(tempfile.mkdtemp(prefix="materials.", dir=output))
    profile = suite / "profile/OpenJK"
    profile.mkdir(parents=True)
    env = dict(os.environ, OJK_PROFILE=str(profile.parent), SDL_VIDEODRIVER="offscreen",
               EGL_PLATFORM="surfaceless", SDL_AUDIODRIVER="dummy")
    env.pop("LIBGL_ALWAYS_SOFTWARE", None)
    captures = {}
    results = {}
    print(f"Material results: {suite}", flush=True)
    for label in ("disabled", "cold", "warm", "corrupt", "bright_disabled", "bright_cached"):
        if label == "corrupt":
            # Some cache entries belong to startup-only models. Corrupt all entries
            # so this map must exercise payload validation and regeneration.
            for entry in (profile / "cache/rd2n1").glob("*.bin"):
                with entry.open("r+b") as stream:
                    stream.seek(32)
                    value = stream.read(1)
                    stream.seek(32)
                    stream.write(bytes([value[0] ^ 128]))
        settings = dict(cl_renderer="rdsp-rend2", r_genNormalMaps=1, r_normalMapping=1,
                        r_specularMapping=1, r_normalMapCache=int(label not in ("disabled", "bright_disabled")),
                        r_generatedNormalBrighten=int(label.startswith("bright_")), r_normalStrength=1, r_generatedNormalStrength=0.25,
                        r_specularStrength=1, r_roughnessScale=1, r_roughnessFloor=0,
                        r_ssao=0, r_mode=-1, r_customwidth=640, r_customheight=480,
                        r_fullscreen=0, r_debugContext=1, r_ignoreGLErrors=0,
                        com_maxfps=30, s_initsound=0)
        (profile / "openjk_sp.cfg").write_text("".join(f'set {k} "{v}"\n' for k, v in settings.items()))
        (profile / "autoexec_sp.cfg").write_text("// Controlled material test.\n")
        start = time.monotonic()
        with (suite / f"{label}.log").open("w") as log:
            subprocess.run(["timeout", "--kill-after=5s", "600s", "bash", str(package / "launch-sp.sh"),
                            os.environ.get("OJK_ASSETS", str(root / "GameData")),
                            "+devmap", "t2_wedge", "+exec", fixture],
                           env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
        text = (suite / f"{label}.log").read_text(errors="replace")
        if ("OJK_MATERIALS_DONE" not in text or "----- rdsp-rend2 -----" not in text or
                re.search(r"llvmpipe|softpipe|trying to load fallback|GL_INVALID_|GL_OUT_OF_MEMORY|"
                          r"OpenGL -> [^\n]*\[(?:Error|Undefined)\]", text, re.I)):
            raise RuntimeError(f"Renderer or GL failure: {label}")
        counts = re.findall(r"Normal maps: (\d+) generated, (\d+) cache hits, (\d+) ms generation", text)
        if not counts:
            raise RuntimeError("No normal-generation statistics")
        generated, hits, generation_ms = map(int, counts[-1])
        results[label] = dict(generated=generated, hits=hits, generation_ms=generation_ms,
                              process_seconds=time.monotonic() - start)
        if label in ("disabled", "cold", "corrupt", "bright_disabled") and generated == 0:
            raise RuntimeError(f"No generation in {label} run")
        if label in ("warm", "bright_cached") and (generated != 0 or hits == 0):
            raise RuntimeError("Warm cache did not replace generation")
        if label == "disabled" and (hits or (profile / "cache/rd2n1").exists()):
            raise RuntimeError("Disabled cache was used")
        images = suite / label
        (profile / "screenshots").rename(images)
        captures[label] = {}
        for name in ("flat", "gentle", "strong", "equivalent", "restored", "matte", "glossy", "rough"):
            data = subprocess.run(["ffmpeg", "-v", "error", "-xerror", "-i",
                str(images / f"material_{name}.png"), "-vf", "scale=160:120", "-frames:v", "1",
                "-pix_fmt", "gray", "-f", "rawvideo", "-"], capture_output=True, check=True).stdout
            if len(data) != 160 * 120:
                raise RuntimeError("Invalid image dimensions")
            # Static wall/floor region excludes sky and animated effects.
            captures[label][name] = bytes(data[y * 160 + x] for y in range(56, 112) for x in range(56, 104))
        print(f"{label}: {results[label]}", flush=True)

    def difference(a, b):
        return sum(abs(x - y) for x, y in zip(a, b)) / len(a)

    c = captures["warm"]
    metrics = {"gentle_effect": difference(c["flat"], c["gentle"]),
               "strong_effect": difference(c["flat"], c["strong"]),
               "equivalent_error": difference(c["gentle"], c["equivalent"]),
               "restore_error": difference(c["gentle"], c["restored"]),
               "specular_effect": difference(c["gentle"], c["matte"]),
               "roughness_effect": difference(c["glossy"], c["rough"])}
    print(metrics, flush=True)
    if not 0.01 < metrics["gentle_effect"] < metrics["strong_effect"]:
        raise RuntimeError("Normal strength did not give a milder result")
    if max(metrics["equivalent_error"], metrics["restore_error"]) > 0.2:
        raise RuntimeError("Live normal controls did not restore their result")
    if min(metrics["specular_effect"], metrics["roughness_effect"]) <= 0.01:
        raise RuntimeError("Specular or roughness controls had no measurable effect")
    for label in ("cold", "warm", "corrupt"):
        for name in c:
            if difference(captures["disabled"][name], captures[label][name]) > 0.2:
                raise RuntimeError(f"Cache changed rendering: {label}/{name}")
    for name in c:
        if difference(captures["bright_disabled"][name], captures["bright_cached"][name]) > 0.2:
            raise RuntimeError(f"Cache changed diffuse compensation: {name}")
    metrics["diffuse_brightening_effect"] = difference(captures["disabled"]["flat"], captures["bright_cached"]["flat"])
    if metrics["diffuse_brightening_effect"] <= 0.01:
        raise RuntimeError("Diffuse-brightening control had no measurable effect")
    (suite / "results.json").write_text(json.dumps(dict(runs=results, metrics=metrics), indent=2) + "\n")
    print("PASS: live material controls, cache reuse, identical cached output, and corrupt-cache recovery")


if __name__ == "__main__":
    main()
