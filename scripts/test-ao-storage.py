#!/usr/bin/env python3
"""Compare scalar AO storage with the RGBA reference through renderer restarts."""

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
    parser.add_argument("--method", type=int, choices=(0, 1), default=1)
    args = parser.parse_args()
    package = args.package.resolve()
    fixture = "rend2-ao-storage.cfg"
    if (package / "OpenJK" / fixture).read_bytes() != (root / "scripts" / fixture).read_bytes():
        parser.error("Build the current AO storage fixture")
    suite = Path(tempfile.mkdtemp(prefix="ao-storage.", dir=root / "build/smoke"))
    subprocess.run(["bash", str(root / "scripts/smoke-sp.sh"), str(package), "t2_wedge",
                    "+set", "r_compactAO", "0", "+set", "r_ssaoMethod", str(args.method),
                    "+set", "r_ignoreGLErrors", "0", "+set", "r_debugContext", "1", "+exec", fixture],
                   env=dict(os.environ, OJK_SMOKE_ROOT=str(suite), OJK_SMOKE_RENDERER="rdsp-rend2",
                            OJK_SMOKE_TIMEOUT="600"), check=True)
    log, = suite.glob("t2_wedge.*/console.log")
    text = log.read_text(errors="replace")
    if re.search(r"GL_INVALID_|GL_OUT_OF_MEMORY|trying to load fallback|OpenGL -> [^\n]*\[(?:Error|Undefined)\]", text):
        raise RuntimeError(f"GL or renderer failure: {log}")
    images, start = {}, 0
    for phase, storage in (("rgba", "RGBA8"), ("compact", "R8" if args.method else "RGBA8"), ("restored", "RGBA8")):
        end = text.find(f"OJK_AO_{phase.upper()}", start)
        if end < 0 or f"AO storage: {storage}" not in text[start:end]:
            raise RuntimeError(f"Missing storage transition: {phase}")
        start = end+1
        image = log.parent / f"profile/OpenJK/screenshots/ao_{phase}.png"
        pixels = subprocess.run(["ffmpeg", "-v", "error", "-xerror", "-i", str(image), "-frames:v", "1",
                                 "-pix_fmt", "rgb24", "-f", "rawvideo", "-"], capture_output=True, check=True).stdout
        if len(pixels) != 640*480*3 or max(pixels)-min(pixels) < 4:
            raise RuntimeError("Missing or uniform AO")
        if any(max(pixels[i:i+3])-min(pixels[i:i+3]) > 2 for i in range(0, len(pixels), 3)):
            raise RuntimeError("AO channel replication failed")
        images[phase] = pixels
    errors = {name: sum(abs(a-b) for a, b in zip(images["rgba"], image)) / len(image)
              for name, image in images.items() if name != "rgba"}
    print(errors)
    if max(errors.values()) > 0.1:
        raise RuntimeError(f"AO storage changed the reference image: {suite}")
    print(f"PASS: AO storage and restart round trip. Results: {suite}")


if __name__ == "__main__":
    main()
