#!/usr/bin/env python3
"""Check Steam Audio mixing, local caches, and sound restart with a dummy audio device."""
import argparse
import array
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import tempfile
import time
import wave


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=root / "build/ready")
    parser.add_argument("--campaign", choices=("ja", "jo"), default="ja")
    parser.add_argument("--map")
    parser.add_argument("--bake", action="store_true")
    args = parser.parse_args()
    run = Path(tempfile.mkdtemp(prefix="steam-audio.", dir=root / "build/smoke"))
    home = run / "profile"
    profile = home / ("campaigns/jo/OpenJK" if args.campaign == "jo" else "OpenJK")
    profile.mkdir(parents=True)
    settings = dict(cl_renderer="rdsp-vanilla", r_mode=-1, r_customwidth=640, r_customheight=480,
                    r_fullscreen=0, s_initsound=1, s_musicvolume=0, com_maxfps=60,
                    r_ignoreGLErrors=1, developer=1)
    (profile / "openjk_sp.cfg").write_text("".join(f'set {k} "{v}"\n' for k, v in settings.items()))
    (profile / "autoexec_sp.cfg").write_text("")
    env = dict(os.environ, OJK_PROFILE=str(home), OJK_JO_ASSETS=str(root / "GameData_JO"),
               SDL_AUDIODRIVER="dummy", SDL_VIDEODRIVER="offscreen", EGL_PLATFORM="surfaceless")
    log = run / "console.log"
    records = {}
    print(f"Steam Audio results: {run}", flush=True)
    with log.open("w") as stream:
        process = subprocess.Popen(["bash", str(args.package.resolve() / "launch-sp.sh"), str(root / "GameData"),
                                    "--campaign", args.campaign, "+devmap", args.map or ("kejim_base" if args.campaign == "jo" else "t1_sour"),
                                    "+wait", "100", "+echo", "AUDIO_READY"], env=env, stdin=subprocess.PIPE,
                                   stdout=stream, stderr=subprocess.STDOUT, text=True, start_new_session=True)
        assert process.stdin
        stdin = process.stdin
        def text():
            return re.sub(r"\^[0-9]", "", log.read_text(errors="replace"))
        def wait(marker, start=0):
            deadline = time.monotonic() + 300
            while time.monotonic() < deadline:
                output = text()[start:]
                if marker in output:
                    return output
                if process.poll() is not None:
                    raise RuntimeError(f"Game exited: {log}")
                time.sleep(0.05)
            raise TimeoutError(f"Missing {marker}: {log}")
        serial = 0
        def cmd(value):
            nonlocal serial
            serial += 1
            marker = f"AUDIO_CMD_{serial}_DONE"
            start = len(text())
            line = f"{value}; wait 10; echo {marker}\n"
            assert len(line) < 256
            stdin.write(line); stdin.flush()
            return wait(marker, start)
        def status(name):
            result = cmd("s_steam_status")
            match = re.search(r"steam_audio active=[^\n]+", result)
            assert match, result
            value = dict(word.split("=", 1) for word in match[0].split()[1:])
            records[name] = value
            return value
        def capture():
            start = len(text())
            cmd("s_steam_record 2; s_steam_emit sound/weapons/blaster/fire.wav")
            result = wait("Steam Audio capture:", start)
            match = re.search(r"Steam Audio capture: (\S+)", result)
            assert match
            with wave.open(str(profile / match[1])) as wav:
                assert wav.getnchannels() == 2 and wav.getsampwidth() == 2
                assert wav.getnframes() == 2 * wav.getframerate()
                samples = array.array("h", wav.readframes(wav.getnframes()))
            assert max(map(abs, samples)) > 100, "Recorded audio is silent"
        try:
            wait("AUDIO_READY")
            cmd("exitview; wait 200; helpusobi 1; god; notarget; d_npcfreeze 1; con_notifytime -1")
            initial = status("initial")
            assert initial["active"] == "1" and int(initial["triangles"]) > 100, initial
            capture()
            assert int(status("mixed")["mixed_blocks"]) > int(initial["mixed_blocks"])
            if args.bake:
                cmd("s_steam_bake")
                assert int(status("baked")["probes"]) > 0
            cmd("save steam_audio_test; load steam_audio_test; wait 100")
            loaded = status("loaded")
            assert loaded["active"] == "1" and loaded["scene_cache"] == "1", loaded
            if args.bake:
                assert loaded["probe_cache"] == "1", loaded
                capture()
            cmd("set s_steamAudio 0")
            assert status("disabled")["active"] == "0"
            capture()
            cmd("set s_steamAudio 1; snd_restart; wait 100")
            assert status("restarted")["active"] == "1"
            capture()
            stdin.write("quit\n"); stdin.flush()
            assert process.wait(timeout=30) == 0
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM); process.wait(timeout=15)
            (run / "results.json").write_text(json.dumps(records, indent=2))
    assert "Steam Audio: initialization failed" not in text()
    print("PASS: Steam Audio mix, cache, disable, save/load, and restart")


if __name__ == "__main__":
    main()
