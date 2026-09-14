#!/usr/bin/env python3
"""Test the worktree helper in temporary repositories."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class WorktreeTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="openjk-worktree-")
        self.addCleanup(self.temp.cleanup)
        self.parent = Path(self.temp.name)
        self.repo = self.parent / "main checkout"
        self.repo.mkdir()
        (self.repo / "scripts").mkdir()
        self.script = self.repo / "scripts/add-worktree.sh"
        shutil.copyfile(Path(__file__).with_name("add-worktree.sh"), self.script)
        (self.repo / "GameData/base").mkdir(parents=True)
        (self.repo / ".gitignore").write_text("GameData\nbuild/\n")
        self.git("init", "-q", "--initial-branch=main")
        self.git("add", ".")
        self.git("-c", "user.name=Worktree Test", "-c", "user.email=test@example.invalid",
                 "commit", "-qm", "Test fixture")

    def git(self, *args, root=None):
        return subprocess.check_output(["git", "-C", str(root or self.repo), *args], text=True).strip()

    def invoke(self, branch, script=None, assets=None, succeeds=True):
        env = dict(os.environ)
        env.pop("OJK_ASSETS", None)
        if assets is not None:
            env["OJK_ASSETS"] = str(assets)
        result = subprocess.run(["bash", str(script or self.script), branch],
                                env=env, capture_output=True, text=True)
        self.assertEqual(result.returncode == 0, succeeds, result.stdout + result.stderr)

    def destination(self, name):
        return self.parent / "worktrees" / f"openjk-{name}"

    def test_new_branch(self):
        self.invoke("rend2-perf")
        target = self.destination("rend2-perf")
        self.assertEqual(self.git("branch", "--show-current", root=target), "rend2-perf")
        self.assertTrue((target / "GameData").is_symlink())
        self.assertEqual((target / "GameData").resolve(), self.repo / "GameData")
        self.assertEqual(self.git("status", "--porcelain", root=target), "")

    def test_existing_branch(self):
        self.git("branch", "ui/radial")
        before = self.git("rev-parse", "ui/radial")
        self.invoke("ui/radial")
        target = self.destination("ui-radial")
        self.assertEqual(self.git("branch", "--show-current", root=target), "ui/radial")
        self.assertEqual(self.git("rev-parse", "HEAD", root=target), before)

    def test_invocation_from_linked_worktree(self):
        self.invoke("first")
        first = self.destination("first")
        self.invoke("second", script=first / "scripts/add-worktree.sh")
        self.assertTrue(self.destination("second").is_dir())
        self.assertEqual((self.destination("second") / "GameData").resolve(), self.repo / "GameData")

    def test_path_collision_is_preserved(self):
        self.invoke("ui/radial")
        self.invoke("ui-radial", succeeds=False)
        self.assertEqual(self.git("branch", "--show-current", root=self.destination("ui-radial")), "ui/radial")
        self.assertNotIn("ui-radial", self.git("branch", "--format=%(refname:short)").splitlines())

    def test_checked_out_branch_is_rejected(self):
        self.invoke("main", succeeds=False)
        self.assertFalse(self.destination("main").exists())

    def test_missing_assets_do_not_create_branch(self):
        self.invoke("missing", assets=self.parent / "absent", succeeds=False)
        self.assertEqual(self.git("branch", "--format=%(refname:short)"), "main")

    def test_custom_assets(self):
        assets = self.parent / "shared assets"
        (assets / "base").mkdir(parents=True)
        self.invoke("custom", assets=assets)
        self.assertEqual((self.destination("custom") / "GameData").resolve(), assets)

    def test_invalid_branch_is_rejected(self):
        self.invoke("../../escape", succeeds=False)
        self.assertFalse((self.parent / "worktrees").exists())


if __name__ == "__main__":
    unittest.main()
