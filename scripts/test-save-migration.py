#!/usr/bin/env python3
"""Verify real project-v1 saves load without rewriting their source files."""

import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile


def chunks(path):
    data = path.read_bytes()
    result = []
    offset = 0
    while offset < len(data):
        tag, size = struct.unpack_from("<Ii", data, offset)
        if size < 0 or offset + size + 12 > len(data):
            raise RuntimeError("Expected complete, uncompressed test save")
        result.append((tag.to_bytes(4, "big").decode("ascii"), data[offset+8:offset+8+size]))
        offset += size + 12
    return result


def samples(text):
    result = {}
    for line in text.splitlines():
        if "aimemory event=sample " in line:
            sample = dict(word.split("=", 1) for word in line.split("aimemory ", 1)[1].split())
            result[sample["name"]] = sample
    return result


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--old-package", type=Path, required=True)
    parser.add_argument("--package", type=Path, default=root / "build/ready")
    parser.add_argument("--renderer", choices=("rdsp-vanilla", "rdsp-rend2"), default="rdsp-vanilla",
                        help="Renderer for the new package. The old package uses vanilla.")
    args = parser.parse_args()
    output = root / "build/smoke"
    output.mkdir(parents=True, exist_ok=True)
    suite = Path(tempfile.mkdtemp(prefix="migration.", dir=output))
    print(f"Migration results: {suite}", flush=True)
    old_profile, new_profile = suite / "old-profile", suite / "new-profile"

    def run(name, package, profile, commands, renderer=args.renderer):
        module = package.resolve() / f"{renderer}_x86_64.so"
        if not module.is_file() or not module.stat().st_size:
            raise RuntimeError(f"Missing renderer module: {module}")
        profile.mkdir(parents=True, exist_ok=True)
        command = ["timeout", "--kill-after=5s", "600s" if renderer == "rdsp-rend2" else "180s",
                   "xvfb-run", "-a", "-s", "-screen 0 640x480x24",
                   "bash", str(package.resolve() / "launch-sp.sh"), str(root / "GameData"),
                   "+safe", "+set", "cl_renderer", renderer,
                   "+set", "r_fullscreen", "0", "+set", "r_mode", "3", "+set", "s_initsound", "0",
                   "+set", "developer", "1", "+set", "com_maxfps", "10", "+set", "sv_compress_saved_games", "0",
                   *commands, "+wait", "4", "+quit"]
        with (suite / f"{name}.log").open("w") as log:
            result = subprocess.run(command, env=dict(os.environ, OJK_PROFILE=str(profile), LIBGL_ALWAYS_SOFTWARE="1",
                                                     LP_NUM_THREADS="1", SDL_AUDIODRIVER="dummy"), stdout=log, stderr=subprocess.STDOUT)
        text = (suite / f"{name}.log").read_text(errors="replace")
        if result.returncode or re.search(r"ERROR:|Error:|couldn't exec|Unknown command", text):
            raise RuntimeError(f"Failed {name}: {suite / (name + '.log')}")
        if ("failed: trying to load fallback renderer" in text
                or set(re.findall(r'Trying to load "(rdsp-[^"]+)"', text)) != {module.name}
                or (renderer == "rdsp-rend2" and "----- rdsp-rend2 -----" not in text)):
            raise RuntimeError(f"Wrong renderer in {name}: {suite / (name + '.log')}")
        return text

    old = run("v1-create", args.old_package, old_profile,
              ["+devmap", "t2_wedge", "+exec", "ai-memory-switch.cfg", "+d_npcfreeze", "1",
               "+give", "health", "77", "+give", "armor", "33", "+give", "weaponnum", "4",
               "+give", "ammo", "23", "+setForceJump", "3", "+wait", "4", "+save", "migration_v1",
               "+nav", "memory", "_memory_a", "+nav", "memory", "_memory_b", "+nav", "memory", "_memory_c"],
              renderer="rdsp-vanilla")
    source = old_profile / "OpenJK/saves/migration_v1.sav"
    old_chunks = chunks(source)
    if struct.unpack("<i", old_chunks[0][1])[0] != 1:
        raise RuntimeError("The supplied old package did not write v1")
    source_hash = hashlib.sha256(source.read_bytes()).digest()
    destination = new_profile / "OpenJK/saves"
    destination.mkdir(parents=True)
    copied = destination / source.name
    shutil.copy2(source, copied)
    checks = ["+wait", "40", "+nav", "player", "+nav", "memory", "_memory_a",
              "+nav", "memory", "_memory_b", "+nav", "memory", "_memory_c"]
    loaded = run("v1-load-v2-save", args.package, new_profile,
                 ["+set", "d_npcfreeze", "1", "+load", "migration_v1", "+helpusobi", "1", "+d_npcfreeze", "1", *checks, "+save", "migration_v2"])
    if "Loaded saved game format 1" not in loaded:
        raise RuntimeError("Legacy importer did not finish")
    before, after = samples(old), samples(loaded)
    for name in ("_memory_a", "_memory_b", "_memory_c"):
        for key in ("ent", "enemy", "pos", "seen_time", "seen", "group", "group_enemy", "group_time", "shared", "members", "goal", "goal_pos"):
            if before[name][key] != after[name][key]:
                raise RuntimeError(f"Migration changed {name}.{key}: {before[name][key]} -> {after[name][key]}")
        if after[name]["role"] != "0" or after[name]["cp"] != "-1" or after[name]["deadline"] != "0":
            raise RuntimeError(f"Bad tactical defaults: {after[name]}")
    player = re.search(r"playerstate health=(\d+) armor=(\d+) weapons=(\d+) ammo_blaster=(\d+) jump=(\d+)", loaded)
    if not player or tuple(map(int, (player[1], player[2], player[4], player[5]))) != (77, 33, 23, 3) or not int(player[3]) & (1 << 4):
        raise RuntimeError("Player state was not preserved")
    converted = destination / "migration_v2.sav"
    new_chunks = chunks(converted)
    if struct.unpack("<i", new_chunks[0][1])[0] != 2:
        raise RuntimeError("Writer did not produce v2")
    if [p for tag, p in old_chunks if tag == "OBJT"] != [p for tag, p in new_chunks if tag == "OBJT"]:
        raise RuntimeError("Mission objectives changed")
    reloaded = run("v2-reload", args.package, new_profile, ["+set", "d_npcfreeze", "1", "+load", "migration_v2", "+helpusobi", "1", "+d_npcfreeze", "1", *checks])
    if "Loaded saved game format 2" not in reloaded or set(samples(reloaded)) != set(after):
        raise RuntimeError("Converted save did not reload")
    for name, expected in after.items():
        actual = samples(reloaded)[name]
        for key in ("enemy", "pos", "seen_time", "seen", "group", "shared", "role", "cp"):
            if actual[key] != expected[key]:
                raise RuntimeError(f"V2 round trip changed {name}.{key}")
    # _VER is a four-byte uncompressed chunk; update its normal MD4 XOR checksum.
    for version in (0, 3):
        invalid = bytearray(source.read_bytes())
        payload = struct.pack("<i", version)
        digest = subprocess.run(["openssl", "dgst", "-provider", "legacy", "-md4", "-binary"],
                                input=payload, capture_output=True, check=True).stdout
        words = struct.unpack("<4I", digest)
        invalid[8:12] = payload
        invalid[12:16] = struct.pack("<I", words[0] ^ words[1] ^ words[2] ^ words[3])
        (destination / f"invalid_{version}.sav").write_bytes(invalid)
        rejected = run(f"reject-{version}", args.package, new_profile,
                       ["+load", f"invalid_{version}", "+load", "migration_v2", "+helpusobi", "1", "+d_npcfreeze", "1", *checks])
        if f"version # {version}" not in rejected or "Loaded saved game format 2" not in rejected:
            raise RuntimeError("Unsupported version was not rejected or poisoned the next load")
    for path in (source, copied):
        if hashlib.sha256(path.read_bytes()).digest() != source_hash:
            raise RuntimeError("Original v1 save was modified")
    print(f"PASS: v1 migration, v2 round trip, state preservation, unchanged originals. Results: {suite}")


if __name__ == "__main__":
    main()
