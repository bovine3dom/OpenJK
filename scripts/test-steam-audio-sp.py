#!/usr/bin/env python3
"""Check Steam Audio continuity, effect modes, caches, and sound restart."""
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
    parser.add_argument("--ambient", action="store_true", help="Check emitters outside the visual snapshot on the default map")
    parser.add_argument("--first-use", action="store_true", help="Check that runtime file access preserves queued audio")
    parser.add_argument("--alarm", action="store_true", help="Check the Kejim Post perimeter alarm behind a wall")
    parser.add_argument("--acoustics", action="store_true", help="Capture indoor and outdoor indirect sound on Kejim Post")
    parser.add_argument("--burst", action="store_true", help="Check headroom with four simultaneous blaster shots")
    parser.add_argument("--rate", type=int, choices=(22, 44), default=44)
    parser.add_argument("--audio-driver", default="dummy")
    parser.add_argument("--device-samples", type=int, default=0)
    args = parser.parse_args()
    run = Path(tempfile.mkdtemp(prefix="steam-audio.", dir=root / "build/smoke"))
    home = run / "profile"
    profile = home / ("campaigns/jo/OpenJK" if args.campaign == "jo" else "OpenJK")
    profile.mkdir(parents=True)
    settings = dict(cl_renderer="rdsp-vanilla", r_mode=-1, r_customwidth=640, r_customheight=480,
                    r_fullscreen=0, s_initsound=1, s_musicvolume=0, com_maxfps=60,
                    r_ignoreGLErrors=1, developer=1, s_khz=args.rate,
                    s_sdlDevSamps=args.device_samples)
    (profile / "openjk_sp.cfg").write_text("".join(f'set {k} "{v}"\n' for k, v in settings.items()))
    (profile / "autoexec_sp.cfg").write_text("")
    (profile / "audio-file-test.cfg").write_text("echo AUDIO_FILE_READ\n")
    env = dict(os.environ, OJK_PROFILE=str(home), OJK_JO_ASSETS=str(root / "GameData_JO"),
               SDL_AUDIODRIVER=args.audio_driver, SDL_VIDEODRIVER="offscreen", EGL_PLATFORM="surfaceless")
    log = run / "console.log"
    records = {"settings": settings, "audio_driver": args.audio_driver,
               "campaign": args.campaign, "package": str(args.package.resolve())}
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
            timing = re.search(r"steam_audio timing ([^\n]+)", result)
            assert timing, result
            records[name + "_timing"] = dict(word.split("=", 1) for word in timing[1].split())
            limiter = re.search(r"steam_audio limiter ([^\n]+)", result)
            if limiter:
                records[name + "_limiter"] = dict(word.split("=", 1) for word in limiter[1].split())
            return value
        def capture(continuous=True, wet=False, signal=True):
            start = len(text())
            shots = "; ".join(["s_steam_emit sound/weapons/blaster/fire.wav"] * (4 if args.burst and not wet else 1))
            cmd(f"s_steam_record 2{' wet' if wet else ''}; {shots}")
            result = wait("Steam Audio capture continuity:", start)
            match = re.search(r"Steam Audio capture: (\S+)", result)
            assert match
            with wave.open(str(profile / match[1])) as wav:
                assert wav.getnchannels() == 2 and wav.getsampwidth() == 2
                assert wav.getnframes() == 2 * wav.getframerate()
                samples = array.array("h", wav.readframes(wav.getnframes()))
            peak = max(map(abs, samples))
            assert (peak > 100 if signal else peak == 0), f"Unexpected capture peak: {peak}"
            continuity = re.search(r"overlap_frames=(\d+) gap_frames=(\d+)", result)
            assert continuity, result
            records.setdefault("captures", []).append(dict(
                file=match[1], continuous=continuous, overlap_frames=int(continuity[1]), gap_frames=int(continuity[2]),
                peak=peak, wet=wet, clipped_samples=sum(s in (-32768, 32767) for s in samples)))
            if continuous:
                assert continuity.groups() == ("0", "0"), continuity[0]
                if args.burst and not wet:
                    assert records["captures"][-1]["clipped_samples"] == 0, records["captures"][-1]
        def ambient():
            def sources():
                return [dict(word.split("=", 1) for word in line.split())
                        for line in re.findall(r"steam_source ([^\n]+)", cmd("s_steam_status sources"))]
            centers = ((6848, -3136, 600), (4376, -1816, 96)) if args.campaign == "ja" else (
                (-4256, -288, -96), (-2992, -1728, 0))
            cmd("noclip; set cg_spatialAmbience 1")
            for center in centers:
                for offset in ((0, 0, -128), (128, 0, 0), (-128, 0, 0), (0, 128, 0), (0, -128, 0), (0, 0, 128)):
                    position = " ".join(str(a+b) for a, b in zip(center, offset))
                    cmd(f"setviewpos {position} 0; wait 40")
                    active = sources()
                    hidden = [s for s in active if s["loop"] == "1" and s["visible"] == "0" and 0 < int(s["entity"]) < 1022]
                    if not hidden:
                        continue
                    entity = hidden[0]["entity"]
                    assert sum(s["entity"] == entity and s["loop"] == "1" for s in active) == 1
                    cmd("set cg_spatialAmbience 0; wait 40")
                    assert not any(s["entity"] == entity and s["loop"] == "1" for s in sources())
                    cmd("set cg_spatialAmbience 1; wait 40")
                    assert any(s["entity"] == entity and s["loop"] == "1" and s["visible"] == "0" for s in sources())
                    records["ambient"] = dict(position=position, source=hidden[0])
                    return
            raise AssertionError("No audible emitter outside the visual snapshot was found")
        try:
            wait("AUDIO_READY")
            cmd("exitview; wait 200; helpusobi 1; god; notarget; d_npcfreeze 1; con_notifytime -1")
            initial = status("initial")
            if args.rate == 22:
                assert initial["active"] == "0", initial
                capture(continuous=False)
                cmd("snd_restart; wait 100")
                assert status("restarted")["active"] == "0"
                capture(continuous=False)
                stdin.write("quit\n"); stdin.flush()
                assert process.wait(timeout=30) == 0
                assert "Steam Audio: initialization failed" not in text()
                print("PASS: 22050 Hz legacy fallback and sound restart")
                return
            assert initial["active"] == "1" and int(initial["triangles"]) > 100, initial
            if args.first_use:
                cmd("s_steam_status reset")
                assert "AUDIO_FILE_READ" in cmd("exec audio-file-test.cfg")
                output = cmd("s_steam_status")
                clears = re.search(r"buffer_clears=(\d+)", output)
                records["fileio_buffer_clears"] = int(clears[1]) if clears else None
                assert clears and clears[1] == "0", output
            if args.alarm:
                assert args.campaign == "jo" and args.map == "kejim_post"
                cmd("set cg_thirdPerson 0; noclip; use defense_alarm_sound")
                cmd("setviewpos 56 0 400 0; wait 100")
                def alarm():
                    sources = [dict(word.split("=", 1) for word in line.split())
                               for line in re.findall(r"steam_source ([^\n]+)", cmd("s_steam_status sources"))]
                    return [s for s in sources if s["sound"].endswith("/alarm1") and s["loop"] == "1"]
                source = alarm()
                original = [s for s in source if s["pos"] == "56.0,160.0,472.0"]
                assert len(original) == 1 and float(original[0]["occlusion"]) < .1, source
                assert float(original[0]["transmission"].split(",")[1]) >= .119, source
                assert len(source) == 3 and len({s["entity"] for s in source}) == 3, source
                records["alarm"] = source
                cmd("set cg_alarmRelays 0; wait 40")
                assert len(alarm()) == 1
                cmd("set cg_alarmRelays 1; setviewpos -16 160 480 180; wait 100")
                assert any(float(s["occlusion"]) > .9 and s["pos"] != "56.0,160.0,472.0" for s in alarm())
                cmd("save audio_alarm_test; load audio_alarm_test; wait 100")
                assert len(alarm()) == 3, "Alarm relays did not survive save/load"
                cmd("use defense_alarm_sound; wait 40")
                assert not alarm(), "Scripted alarm stop was ignored"
                initial = status("after_alarm")
            if args.ambient:
                assert not args.map, "Ambient fixtures use the default map"
                ambient()
            capture()
            assert int(status("mixed")["mixed_blocks"]) > int(initial["mixed_blocks"])
            if args.bake:
                cmd("s_steam_bake")
                assert int(status("baked")["probes"]) > 0
            for name, reflections, pathing in (("direct", 0, 0), ("reflections", 1, 0), ("pathing", 0, 1)):
                cmd(f"set s_steamReflections {reflections}; set s_steamPathing {pathing}; wait 100; s_steam_status reset")
                capture()
                status(name)
            if args.acoustics:
                assert args.campaign == "jo" and args.map == "kejim_post"
                if not (args.alarm or args.ambient):
                    cmd("noclip")
                cmd("set cg_thirdPerson 0; set s_steamReflections 1; set s_steamPathing 0")
                for name, position in (("canyon", "1692 -1692 -16"), ("room", "528 200 8")):
                    cmd(f"setviewpos {position} 0; wait 200")
                    capture(wet=True)
                    records[name + "_wet"] = records["captures"][-1]
                    assert records[name + "_wet"]["clipped_samples"] == 0
                cmd("set s_steamReflections 0; wait 100")
                capture(wet=True, signal=False)
            cmd("set s_steamReflections 1; set s_steamPathing 1")
            cmd("save steam_audio_test; load steam_audio_test; wait 100")
            loaded = status("loaded")
            assert loaded["active"] == "1" and loaded["scene_cache"] == "1", loaded
            if args.bake:
                assert loaded["probe_cache"] == "1", loaded
                capture()
            cmd("set s_steamAudio 0")
            assert status("disabled")["active"] == "0"
            capture(continuous=False)
            cmd("set s_steamAudio 1; wait 100")
            assert status("reenabled")["active"] == "1"
            capture()
            cmd("snd_restart; wait 100")
            assert status("restarted")["active"] == "1"
            capture()
            stdin.write("quit\n"); stdin.flush()
            assert process.wait(timeout=30) == 0
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM); process.wait(timeout=15)
            (run / "results.json").write_text(json.dumps(records, indent=2))
    assert "Steam Audio: initialization failed" not in text()
    print("PASS: Steam Audio continuity, effect modes, cache, disable, save/load, and restart")


if __name__ == "__main__":
    main()
