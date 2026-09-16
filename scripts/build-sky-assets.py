#!/usr/bin/env python3
"""Build source-derived 2048-pixel skies with cross-face reconstruction."""

import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import zipfile

FACES = ("rt", "lf", "bk", "ft", "up", "dn")
SKIES = ("wedge", "yavin")


def direction(face, s, t):
    return ((1, -s, t), (-1, s, t), (s, 1, t), (-s, -1, t), (-t, -s, 1), (t, -s, -1))[face]


def sample(images, size, d):
    x, y, z = d
    axis = max(range(3), key=lambda i: abs(d[i]))
    scale = abs(d[axis])
    face = axis * 2 + (d[axis] < 0)
    s, t = ((-y, z), (y, z), (x, z), (-x, z), (-y, -x), (-y, x))[face]
    u, v = (s / scale + 1) * size / 2 - .5, (1 - t / scale) * size / 2 - .5
    ix, iy = math.floor(u), math.floor(v)
    fx, fy = u - ix, v - iy
    image = images[face]
    result = []
    for c in range(3):
        value = 0
        for dx, dy, weight in ((0, 0, (1-fx)*(1-fy)), (1, 0, fx*(1-fy)),
                               (0, 1, (1-fx)*fy), (1, 1, fx*fy)):
            px, py = max(0, min(size-1, ix+dx)), max(0, min(size-1, iy+dy))
            value += image[(py*size+px)*3+c] * weight
        result.append(round(value))
    return bytes(result)


def ffmpeg(data, *args):
    return subprocess.run(["ffmpeg", "-v", "error", "-threads", "1", *args], input=data,
                          capture_output=True, check=True).stdout


def build(assets, output):
    sources = {}
    for path in sorted((assets / "base").glob("*.pk3")):
        with zipfile.ZipFile(path) as archive:
            for name in archive.namelist():
                if any(name.lower() == f"textures/skies/{sky}_{face}.{ext}"
                       for sky in SKIES for face in FACES for ext in ("jpg", "tga", "png")):
                    sources[name.lower()] = archive.read(name)
    records, generated = {}, {}
    for sky in SKIES:
        images, sizes, hashes = [], [], {}
        for face in FACES:
            name = next((f"textures/skies/{sky}_{face}.{ext}" for ext in ("png", "tga", "jpg")
                         if f"textures/skies/{sky}_{face}.{ext}" in sources), None)
            if name is None:
                break
            data = sources[name]
            info = json.loads(subprocess.run(["ffprobe", "-v", "error", "-show_entries",
                "stream=width,height", "-of", "json", "-i", "pipe:0"], input=data,
                capture_output=True, check=True).stdout)["streams"][0]
            sizes.append((info["width"], info["height"]))
            images.append(ffmpeg(data, "-i", "pipe:0", "-frames:v", "1", "-pix_fmt", "rgb24", "-f", "rawvideo", "-"))
            hashes[name] = hashlib.sha256(data).hexdigest()
        if len(images) != 6 or len(set(sizes)) != 1 or sizes[0][0] != sizes[0][1]:
            print(f"Skip {sky}: need six equal square faces")
            continue
        size = sizes[0][0]
        if size >= 2048 or 2048 % size:
            print(f"Skip {sky}: native resolution {size}")
            continue
        pad, factor = 4, 2048 // size
        padded_size = size + 2*pad
        for face, suffix in enumerate(FACES):
            padded = bytearray(padded_size*padded_size*3)
            for y in range(-pad, size+pad):
                for x in range(-pad, size+pad):
                    dest = ((y+pad)*padded_size+x+pad)*3
                    if 0 <= x < size and 0 <= y < size:
                        src = (y*size+x)*3
                        padded[dest:dest+3] = images[face][src:src+3]
                    else:
                        padded[dest:dest+3] = sample(images, size,
                            direction(face, 2*(x+.5)/size-1, 1-2*(y+.5)/size))
            png = ffmpeg(padded, "-f", "rawvideo", "-pixel_format", "rgb24", "-video_size",
                f"{padded_size}x{padded_size}", "-i", "pipe:0", "-vf",
                f"scale={padded_size*factor}:{padded_size*factor}:flags=lanczos,crop=2048:2048:{pad*factor}:{pad*factor}",
                "-frames:v", "1", "-c:v", "png", "-f", "image2pipe", "-")
            generated[f"textures/sky_hd/{sky}_{suffix}.png"] = png
        records[sky] = dict(source_size=size, output_size=2048, sources=hashes)
        print(f"Reconstructed {sky}: {size} -> 2048, six padded faces")
    if not records:
        raise RuntimeError("No selected source skies found")
    generated["sky-assets.json"] = (json.dumps(dict(method="cross-face Lanczos reconstruction",
        skies=records), indent=2) + "\n").encode()
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(generated.items()):
            archive.writestr(zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0)), data,
                             compress_type=zipfile.ZIP_DEFLATED)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("assets", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    build(args.assets, args.output)
