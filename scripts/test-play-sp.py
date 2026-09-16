#!/usr/bin/env python3
"""Test desktop updates with real rsync, a local SSH stand-in, and a fake game."""

import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parent.parent
SSH = '''#!/usr/bin/env python3
import os, shlex, subprocess, sys
from pathlib import Path
args = sys.argv[1:]
if args[0] == "--":
    args.pop(0)
assert args.pop(0) == "fixture"
if os.environ.get("OJK_TEST_SSH_FAIL"):
    sys.exit(255)
if args[0] == "rsync" and os.environ.get("OJK_TEST_RSYNC_FAIL"):
    sys.exit(12)
if args[0] == "rsync" and os.environ.get("OJK_TEST_INTERRUPT"):
    server = subprocess.Popen(args, stdout=subprocess.PIPE)
    total = 0
    try:
        while block := os.read(server.stdout.fileno(), 65536):
            sys.stdout.buffer.write(block)
            sys.stdout.buffer.flush()
            total += len(block)
            if total > 200000:
                sys.exit(12)
        sys.exit(server.wait())
    finally:
        if server.poll() is None:
            server.kill()
        server.wait()
if args[0].startswith("sh -s") and os.environ.get("OJK_TEST_PUBLISH"):
    result = subprocess.run(["sh", "-c", " ".join(args)], stdout=subprocess.PIPE)
    parameters = shlex.split(args[0])
    root = Path(parameters[3]).resolve()
    if parameters[4]:
        root = root.parent / "worktrees" / ("openjk-" + parameters[4])
    ready = root / "build/ready"
    ready.unlink()
    ready.symlink_to(os.environ["OJK_TEST_PUBLISH"])
    sys.stdout.buffer.write(result.stdout)
    sys.exit(result.returncode)
os.execl("/bin/sh", "sh", "-c", " ".join(args))
'''
GAME = '''#!/usr/bin/env python3
import json, os, sys, time
from pathlib import Path
Path(os.environ["OJK_TEST_LAUNCH"]).write_text(json.dumps(sys.argv[1:]))
if os.environ.get("OJK_TEST_HOLD"):
    time.sleep(30)
'''


class DesktopUpdateTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="openjk-play-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.server = self.root / "project 'quoted' $path"
        self.packages = self.server / "build/packages"
        self.packages.mkdir(parents=True)
        self.assets = self.root / "Game Data"
        (self.assets / "base").mkdir(parents=True)
        for index in range(4):
            (self.assets / "base" / f"assets{index}.pk3").touch()
        self.jo_assets = self.root / "Outcast Game Data"
        (self.jo_assets / "base").mkdir(parents=True)
        for index in (0, 1, 2, 5):
            (self.jo_assets / "base" / f"assets{index}.pk3").touch()
        self.destination = self.root / "desktop/build"
        self.launch = self.root / "launch.json"
        self.bin = self.root / "bin"
        self.bin.mkdir()
        (self.bin / "ssh").write_text(SSH)
        (self.bin / "ssh").chmod(0o755)
        self.env = dict(os.environ, PATH=f"{self.bin}:{os.environ['PATH']}",
                        OJK_HOST="fixture", OJK_REMOTE_ROOT=str(self.server),
                        OJK_ASSETS=str(self.assets), OJK_DESKTOP_DIR=str(self.root / "desktop"),
                        OJK_PROFILE=str(self.root / "profile"),
                        OJK_DESKTOP_CONFIG=str(self.root / "desktop.conf"),
                        OJK_TEST_LAUNCH=str(self.launch))
        self.first = self.package("first")
        (self.server / "build/ready").symlink_to(self.first)

    def package(self, name, server=None):
        package = (server / "build/packages" if server else self.packages) / name
        (package / "OpenJK").mkdir(parents=True)
        (package / "launch-sp.sh").write_bytes((ROOT / "scripts/launch-sp.sh").read_bytes())
        (package / "import-jo.py").write_text(
            'import json, os, sys\nfrom pathlib import Path\n'
            'Path(os.environ["OJK_TEST_LAUNCH"] + ".import").write_text(json.dumps(sys.argv[1:]))\n')
        (package / "openjk_sp.x86_64").write_text(GAME)
        (package / "openjk_sp.x86_64").chmod(0o755)
        (package / "rdsp-vanilla_x86_64.so").write_bytes(bytes(range(256)) * 8192)
        (package / "rdsp-rend2_x86_64.so").write_text(f"rend2 module {name}")
        (package / "OpenJK/jagamex86_64.so").write_text("game module")
        (package / "build-id.txt").write_text(name + "\n")
        (package / "smoke-result.txt").write_text("PASS: t1_sour\n")
        (package / "smoke-rend2-result.txt").write_text("PASS: t1_sour\n")
        (package / "smoke-jo-result.txt").write_text("PASS: kejim_post\n")
        (package / "jo-mvp-result.txt").write_text("PASS: JO opening, wheels, weapon switching, save/load, and Kejim transition (rdsp-vanilla)\n")
        return package

    def worktree(self, name):
        server = self.server.parent / "worktrees" / f"openjk-{name}"
        package = self.package(name, server)
        (server / "build/ready").symlink_to(package)
        return server

    def run_play(self, *args, success=True, **env):
        result = subprocess.run(["bash", str(ROOT / "scripts/play-sp.sh"), *args],
                                env=dict(self.env, **env), capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        return result

    def test_repeated_delta_update_and_arguments(self):
        args = ["+set", "cl_renderer", "rdsp-rend2", "+devmap", "t2_wedge", "+exec", "a config.cfg"]
        self.run_play(*args)
        self.assertEqual(json.loads(self.launch.read_text())[-len(args):], args)
        second = self.package("second")
        blob = second / "rdsp-vanilla_x86_64.so"
        with blob.open("r+b") as file:
            file.seek(65536)
            file.write(b"changed" * 100)
        old_stat = (self.destination / blob.name).stat()
        os.utime(blob, ns=(old_stat.st_atime_ns, old_stat.st_mtime_ns + 1))
        (self.destination / "obsolete.so").write_text("stale module")
        profile = Path(self.env["OJK_PROFILE"])
        (profile / "keep.cfg").write_text("user configuration")
        ready = self.server / "build/ready"
        ready.unlink()
        ready.symlink_to(second)
        result = self.run_play()
        self.assertEqual((self.destination / blob.name).read_bytes(), blob.read_bytes())
        self.assertEqual((self.destination / "rdsp-rend2_x86_64.so").read_bytes(),
                         (second / "rdsp-rend2_x86_64.so").read_bytes())
        self.assertFalse((self.destination / "obsolete.so").exists())
        self.assertEqual((profile / "keep.cfg").read_text(), "user configuration")
        matched = re.search(r"Matched data: ([\d,]+) bytes", result.stdout)
        if matched is None:
            self.fail(result.stdout)
        self.assertGreater(int(matched[1].replace(",", "")), 1_000_000)

    def test_atmosphere_review_isolation(self):
        (self.first / "setup-atmosphere-review.py").write_bytes(
            (ROOT / "scripts/setup-atmosphere-review.py").read_bytes())
        maps = self.first / "OpenJK/maps"
        maps.mkdir()
        for source in (ROOT / "scripts/maps").glob("*.atmosphere"):
            (maps / source.name).write_bytes(source.read_bytes())
        subprocess.run(["python3", str(ROOT / "scripts/build-atmosphere-review.py"),
                        "--output", str(self.first / "OpenJK")], check=True, capture_output=True)
        home = Path(self.env["OJK_PROFILE"])
        home.mkdir()
        sentinel = home / "keep.cfg"
        sentinel.write_text("campaign configuration")
        self.run_play("--desktop", "--atmosphere-review")
        args = json.loads(self.launch.read_text())
        review = home / "atmosphere-review"
        self.assertEqual(args[args.index("fs_homepath") + 1], str(review))
        self.assertIn("atmosphere-review-ja.cfg", args)
        profile = review / "OpenJK/maps/t1_sour.atmosphere"
        edited = profile.read_text() + "// Local review edit.\n"
        profile.write_text(edited)
        notes = review / "atmosphere-review-notes.csv"
        notes.write_text("local notes\n")
        (review / "OpenJK/qconsole.log").write_text("ATMO_TWEAK ja/t1_sour\n")
        self.run_play("--atmosphere-review", "all")
        self.assertIn("atmosphere-review-ja-all.cfg", json.loads(self.launch.read_text()))
        self.assertEqual(profile.read_text(), edited)
        self.assertEqual(notes.read_text(), "local notes\n")
        self.assertEqual(next((review / "OpenJK/review-logs").glob("*.log")).read_text(), "ATMO_TWEAK ja/t1_sour\n")
        self.run_play("--campaign", "jo", "--atmosphere-review", "all", OJK_JO_ASSETS=str(self.jo_assets))
        args = json.loads(self.launch.read_text())
        jo_review = review / "campaigns/jo"
        self.assertEqual(args[args.index("fs_homepath") + 1], str(jo_review))
        self.assertIn("atmosphere-review-jo-all.cfg", args)
        self.assertTrue((jo_review / "OpenJK/maps/bespin_streets.atmosphere").exists())
        imported = json.loads(Path(str(self.launch) + ".import").read_text())
        self.assertEqual(imported[-1], str(jo_review))
        self.assertEqual(sentinel.read_text(), "campaign configuration")
        self.assertFalse((home / "OpenJK/maps").exists())

    def test_publication_does_not_change_transfer_source(self):
        second = self.package("second")
        self.run_play(OJK_TEST_PUBLISH=str(second))
        self.assertEqual((self.destination / "build-id.txt").read_text(), "first\n")
        self.run_play()
        self.assertEqual((self.destination / "build-id.txt").read_text(), "second\n")

    def test_failed_transfer_blocks_direct_launch_and_recovers(self):
        self.run_play()
        self.launch.unlink()
        self.run_play(success=False, OJK_TEST_RSYNC_FAIL="1")
        self.assertFalse(self.launch.exists())
        self.assertTrue((self.destination / ".update-incomplete").exists())
        direct = subprocess.run(["bash", str(self.destination / "launch-sp.sh"), str(self.assets)],
                                env=self.env, capture_output=True, timeout=10)
        self.assertNotEqual(direct.returncode, 0)
        self.assertFalse(self.launch.exists())
        self.run_play()
        self.assertTrue(self.launch.exists())
        self.assertFalse((self.destination / ".update-incomplete").exists())

    def test_running_game_blocks_updates_and_direct_launch(self):
        game = subprocess.Popen(["bash", str(ROOT / "scripts/play-sp.sh")],
                                env=dict(self.env, OJK_TEST_HOLD="1"), stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL)
        try:
            deadline = time.monotonic() + 10
            while not self.launch.exists() and time.monotonic() < deadline:
                time.sleep(0.05)
            self.assertTrue(self.launch.exists(), "Game did not start")
            result = self.run_play(success=False)
            self.assertIn("already running or updating", result.stderr)
            direct = subprocess.run(["bash", str(self.destination / "launch-sp.sh"), str(self.assets)],
                                    env=self.env, capture_output=True, timeout=10)
            self.assertNotEqual(direct.returncode, 0)
        finally:
            game.terminate()
            game.wait(timeout=10)

    def test_interrupted_data_transfer_recovers(self):
        profile = Path(self.env["OJK_PROFILE"])
        profile.mkdir()
        (profile / "keep.cfg").write_text("keep")
        self.run_play(success=False, OJK_TEST_INTERRUPT="1")
        self.assertFalse(self.launch.exists())
        self.assertTrue((self.destination / ".update-incomplete").exists())
        partials = list(self.destination.rglob(".rsync-partial/*"))
        self.assertTrue(any(p.is_file() and p.stat().st_size > 0 for p in partials))
        (self.destination / "obsolete.so").write_text("stale")
        self.run_play()
        self.assertEqual((self.destination / "rdsp-vanilla_x86_64.so").read_bytes(),
                         (self.first / "rdsp-vanilla_x86_64.so").read_bytes())
        self.assertFalse((self.destination / "obsolete.so").exists())
        self.assertFalse((self.destination / ".update-incomplete").exists())
        self.assertEqual((profile / "keep.cfg").read_text(), "keep")
        self.assertEqual(len(list((self.assets / "base").glob("assets*.pk3"))), 4)

    def test_refuses_unmanaged_destination_and_path_overlap(self):
        self.destination.mkdir(parents=True)
        sentinel = self.destination / "important.txt"
        sentinel.write_text("keep")
        self.run_play(success=False)
        self.assertEqual(sentinel.read_text(), "keep")
        self.run_play(success=False, OJK_PROFILE=str(self.destination / "profile"))
        self.assertFalse(self.launch.exists())

    def test_ssh_failure_and_invalid_publication_do_not_launch(self):
        self.run_play(success=False, OJK_TEST_SSH_FAIL="1")
        (self.first / "smoke-result.txt").write_text("FAIL\n")
        self.run_play(success=False)
        self.assertFalse(self.launch.exists())

    def test_configuration(self):
        self.run_play("--configure", "fixture", str(self.assets))
        self.env.pop("OJK_HOST")
        self.env.pop("OJK_ASSETS")
        self.run_play()
        self.assertTrue(self.launch.exists())

    def test_jo_configuration_worktree_and_profile_isolation(self):
        self.run_play("--configure", "fixture", str(self.assets))
        self.run_play("--configure-jo", str(self.jo_assets))
        self.worktree("jo-mvp")
        self.run_play("--campaign", "jo", "--worktree", "jo-mvp", "--new-game",
                      "--resolution", "1920x1080", "+set", "g_spskill", "2")
        args = json.loads(self.launch.read_text())
        profile = self.root / "profile/worktrees/openjk-jo-mvp/campaigns/jo"
        self.assertEqual(args[args.index("fs_homepath") + 1], str(profile))
        self.assertEqual(args[args.index("com_outcast") + 1], "1")
        self.assertEqual(args[args.index("+map") + 1], "kejim_post")
        self.assertEqual(args[-3:], ["+set", "g_spskill", "2"])
        self.assertEqual(json.loads(Path(str(self.launch) + ".import").read_text()),
                         [str(self.assets), str(self.jo_assets), str(profile)])
        (profile / "keep.cfg").write_text("JO save and settings")
        self.run_play("--worktree", "jo-mvp", "--campaign", "ja")
        args = json.loads(self.launch.read_text())
        self.assertEqual(args[args.index("com_outcast") + 1], "0")
        self.assertEqual(args[args.index("fs_homepath") + 1], str(profile.parent.parent))
        self.assertEqual((profile / "keep.cfg").read_text(), "JO save and settings")

    def test_jo_requires_assets_and_verified_package(self):
        self.run_play("--campaign", "jo", success=False)
        self.run_play("--configure", "fixture", str(self.assets), str(self.jo_assets))
        (self.first / "smoke-jo-result.txt").unlink()
        (self.first / "jo-mvp-result.txt").unlink()
        self.run_play("--campaign", "jo", "--desktop")
        self.launch.unlink()
        (self.first / "smoke-result.txt").unlink()
        self.run_play("--campaign", "jo", success=False)
        self.assertFalse(self.launch.exists())
        (self.first / "smoke-result.txt").write_text("PASS: t1_sour\n")
        self.run_play()
        self.launch.unlink()
        (self.jo_assets / "base/assets5.pk3").unlink()
        self.run_play("--campaign", "jo", success=False)
        self.assertFalse(self.launch.exists())

    def test_invalid_campaign_does_not_launch(self):
        for args in (("--campaign",), ("--campaign", "jk2")):
            self.run_play(*args, success=False)
        self.assertFalse(self.launch.exists())

    def test_worktree_configuration_isolation_and_arguments(self):
        self.run_play("--configure", "fixture", str(self.assets))
        configuration = Path(self.env["OJK_DESKTOP_CONFIG"]).read_bytes()
        main_profile = Path(self.env["OJK_PROFILE"])
        for key in ("OJK_REMOTE_ROOT", "OJK_DESKTOP_DIR", "OJK_PROFILE", "OJK_ASSETS"):
            self.env[key] = str(self.root / "unused environment" / key)
        self.run_play()
        self.worktree("ui-radial")
        self.worktree("rend2-perf")
        (main_profile / "keep.cfg").write_text("main settings")
        self.run_play("--worktree", "ui/radial", "--resolution", "1920x1080",
                      "+set", "cl_renderer", "rdsp-rend2")
        args = json.loads(self.launch.read_text())
        destination = self.root / "desktop/worktrees/openjk-ui-radial/build"
        profile = main_profile / "worktrees/openjk-ui-radial"
        self.assertEqual(args[args.index("fs_basepath") + 1], str(destination))
        self.assertEqual(args[args.index("fs_homepath") + 1], str(profile))
        self.assertEqual(args[args.index("fs_cdpath") + 1], str(self.assets))
        self.assertEqual(args[args.index("r_customwidth") + 1], "1920")
        self.assertEqual(args[-3:], ["+set", "cl_renderer", "rdsp-rend2"])
        self.assertNotIn("--worktree", args)
        (profile / "keep.cfg").write_text("worktree settings")
        self.run_play("--worktree", "rend2-perf", "--desktop")
        self.assertEqual((self.root / "desktop/worktrees/openjk-rend2-perf/build/build-id.txt").read_text(), "rend2-perf\n")
        self.run_play("--worktree", "ui/radial")
        self.assertEqual((destination / "build-id.txt").read_text(), "ui-radial\n")
        self.assertEqual((profile / "keep.cfg").read_text(), "worktree settings")
        self.run_play()
        self.assertEqual((self.destination / "build-id.txt").read_text(), "first\n")
        self.assertEqual((main_profile / "keep.cfg").read_text(), "main settings")
        self.assertEqual(Path(self.env["OJK_DESKTOP_CONFIG"]).read_bytes(), configuration)
        self.assertFalse((self.root / "unused environment").exists())

    def test_jolt_demo_has_a_separate_worktree_profile(self):
        self.worktree("rmlui")
        self.run_play("--worktree", "rmlui", "--desktop", "--jolt-demo")
        args = json.loads(self.launch.read_text())
        profile = Path(self.env["OJK_PROFILE"]) / "worktrees/openjk-rmlui/jolt-demo"
        self.assertEqual(args[args.index("fs_homepath") + 1], str(profile))
        self.assertEqual(args[args.index("+exec") + 1], "jolt-demo.cfg")
        self.assertEqual(args[args.index("r_fullscreen") + 1], "1")
        self.assertNotIn("--jolt-demo", args)

    def test_worktree_uses_resolved_remote_root(self):
        self.worktree("rend2-perf")
        alias = self.root / "aliases/main"
        alias.parent.mkdir()
        alias.symlink_to(self.server, target_is_directory=True)
        self.run_play("--worktree", "rend2-perf", OJK_REMOTE_ROOT=str(alias))
        destination = self.root / "desktop/worktrees/openjk-rend2-perf/build"
        self.assertEqual((destination / "build-id.txt").read_text(), "rend2-perf\n")

    def test_worktree_publication_is_pinned(self):
        server = self.worktree("rend2-perf")
        second = self.package("second", server)
        self.run_play("--worktree", "rend2-perf", OJK_TEST_PUBLISH=str(second))
        destination = self.root / "desktop/worktrees/openjk-rend2-perf/build"
        self.assertEqual((destination / "build-id.txt").read_text(), "rend2-perf\n")
        self.run_play("--worktree", "rend2-perf")
        self.assertEqual((destination / "build-id.txt").read_text(), "second\n")
        self.assertEqual((self.server / "build/ready").resolve(), self.first)

    def test_worktree_failure_does_not_fall_back_to_main(self):
        self.run_play("--worktree", "missing", success=False)
        self.assertFalse(self.launch.exists())
        server = self.worktree("bad")
        ready = server / "build/ready"
        ready.unlink()
        ready.symlink_to(self.first)
        self.run_play("--worktree", "bad", success=False)
        self.assertFalse(self.launch.exists())

    def test_invalid_worktree_names(self):
        self.run_play("--worktree", success=False)
        for name in ("", "../main", "/absolute", "a/../../b", "--configure", "a\nb", "a b", "a;id", "a/", "a//b"):
            with self.subTest(name=name):
                self.run_play("--worktree", name, success=False)
        self.assertFalse(self.launch.exists())
        self.assertFalse(self.destination.parent.exists())

    def test_worktree_locks_are_independent(self):
        self.worktree("held")
        game = subprocess.Popen(["bash", str(ROOT / "scripts/play-sp.sh"), "--worktree", "held"],
                                env=dict(self.env, OJK_TEST_HOLD="1"), stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL)
        try:
            deadline = time.monotonic() + 10
            while not self.launch.exists() and time.monotonic() < deadline:
                time.sleep(0.05)
            self.assertTrue(self.launch.exists(), "Worktree game did not start")
            self.run_play()
            result = self.run_play("--worktree", "held", success=False)
            self.assertIn("already running or updating", result.stderr)
        finally:
            game.terminate()
            game.wait(timeout=10)

    def test_display_defaults_and_explicit_modes(self):
        self.run_play()
        args = json.loads(self.launch.read_text())
        self.assertNotIn("cl_renderer", args)
        self.assertEqual(args[args.index("r_mode") + 1], "-2")
        self.assertEqual(args[args.index("cg_fovAspectAdjust") + 1], "1")
        profile = Path(self.env["OJK_PROFILE"]) / "OpenJK"
        profile.mkdir()
        (profile / "openjk_sp.cfg").write_text("saved preferences")
        self.run_play()
        self.assertNotIn("r_mode", json.loads(self.launch.read_text()))
        self.run_play("--resolution", "3840x2160", "+set", "r_fullscreen", "0")
        args = json.loads(self.launch.read_text())
        self.assertEqual(args[args.index("r_mode") + 1], "-1")
        self.assertEqual(args[args.index("r_customwidth") + 1], "3840")
        self.assertEqual(args[args.index("r_customheight") + 1], "2160")
        self.assertEqual(args[-3:], ["+set", "r_fullscreen", "0"])
        self.run_play("--desktop")
        args = json.loads(self.launch.read_text())
        self.assertEqual(args[args.index("r_mode") + 1], "-2")
        self.launch.unlink()
        self.run_play("--resolution", "0x2160", success=False)
        self.assertFalse(self.launch.exists())


if __name__ == "__main__":
    unittest.main()
