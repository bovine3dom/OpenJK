#!/usr/bin/env python3
"""Validate and package the campaign irradiance probes."""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import zipfile


HEADER = struct.Struct("<4sII3I6fII")
RECORD_SIZE = 26


def archive_info(name):
    info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o100644 << 16
    return info


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=root / "probe-data/maps")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    catalogue = json.loads((root / "scripts/atmosphere-catalogue.json").read_text())["maps"]
    expected = {entry["map"]: entry["campaign"] for entry in catalogue if entry["kind"] == "sp"}
    provided = {path.stem: path for path in args.source.glob("*.irrprobe")}
    if set(provided) != set(expected):
        missing = sorted(set(expected) - set(provided))
        extra = sorted(set(provided) - set(expected))
        raise RuntimeError(f"Probe map mismatch; missing={missing}, extra={extra}")

    files = []
    manifest = []
    for name in sorted(expected):
        data = provided[name].read_bytes()
        if len(data) < HEADER.size:
            raise RuntimeError(f"Truncated probe file: {provided[name]}")
        magic, version, checksum, x, y, z, *values = HEADER.unpack_from(data)
        origin, spacing = values[:3], values[3:6]
        count, valid = values[6:]
        if (magic, version) != (b"OIP1", 1) or x * y * z != count or valid > count or not valid or \
                len(data) != HEADER.size + count * RECORD_SIZE or \
                not all(math.isfinite(value) for value in origin + spacing) or min(spacing) <= 0:
            raise RuntimeError(f"Invalid probe file: {provided[name]}")
        files.append((f"maps/{name}.irrprobe", data))
        manifest.append(dict(campaign=expected[name], map=name, bsp_checksum=checksum,
                             bounds=[x, y, z], origin=origin, spacing=spacing,
                             positions=count, valid_positions=valid, bytes=len(data),
                             sha256=hashlib.sha256(data).hexdigest()))

    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_name(args.output.name + ".tmp")
    with zipfile.ZipFile(temporary, "w", allowZip64=True) as archive:
        for name, data in files:
            archive.writestr(archive_info(name), data, compresslevel=9)
        data = (json.dumps(dict(version=1, maps=manifest), indent=2) + "\n").encode()
        archive.writestr(archive_info("irradiance-probes.json"), data, compresslevel=9)
    os.replace(temporary, args.output)
    print(f"Packaged {len(files)} probe maps: {sum(len(data) for _, data in files)} bytes raw, "
          f"{args.output.stat().st_size} bytes compressed")


if __name__ == "__main__":
    main()
