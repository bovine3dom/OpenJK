#!/usr/bin/env python3
"""Check shared atmosphere edits, migration, private exceptions, and backups."""

from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import re
import shutil
import tempfile
import unittest

from atmosphere_profiles import detach_profile, prepare_profiles, profile_key

ROOT = Path(__file__).resolve().parents[1]


class AtmosphereSharingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="atmosphere-sharing.")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.package = self.root / "package"
        self.source = self.package / "OpenJK/maps"
        shutil.copytree(ROOT / "scripts/maps", self.source, symlinks=True)
        catalogue = self.package / "OpenJK/atmosphere-review/catalogue.json"
        catalogue.parent.mkdir()
        shutil.copyfile(ROOT / "scripts/atmosphere-catalogue.json", catalogue)
        self.profile = self.root / "profile"
        self.home = self.profile / "OpenJK/maps"
        self.home.mkdir(parents=True)

    def prepare(self):
        with redirect_stdout(io.StringIO()):
            return prepare_profiles(self.package, self.profile, "ja")

    def edited(self, name, value):
        return re.sub(r"(?m)^illuminance\s+\S+", f"illuminance {value}",
                      (self.source / (name + ".atmosphere")).read_text())

    def test_shared_edits_and_explicit_private_copy(self):
        entries = self.prepare()
        first, second = self.home / "kor1.atmosphere", self.home / "kor2.atmosphere"
        self.assertTrue(first.is_symlink())
        self.assertTrue(second.is_symlink())
        self.assertEqual(first.resolve(), second.resolve())
        first.resolve().write_text(self.edited("kor1", 9))
        self.assertEqual(first.read_bytes(), second.read_bytes())
        with redirect_stdout(io.StringIO()):
            detach_profile(self.profile, "kor2", entries)
        private = second.read_bytes()
        self.prepare()  # Equal values must not undo an explicit detach.
        self.assertFalse(second.is_symlink())
        first.resolve().write_text(self.edited("kor1", 10))
        self.assertEqual(second.read_bytes(), private)
        self.assertNotEqual(first.read_bytes(), private)
        paths = (self.home.parent / "atmosphere-edit-paths.cfg").read_text()
        self.assertIn('ar_edit_kor2 "echo Atmosphere file: maps/kor2.atmosphere"', paths)
        self.assertIn('ar_edit_kor1 "echo Atmosphere file: maps/shared/ja-korriban.atmosphere"', paths)

    def test_common_local_tuning_survives_migration(self):
        custom = self.edited("kor1", 9)
        for name in ("kor1", "kor2"):
            (self.home / (name + ".atmosphere")).write_text(custom + f"// Note for {name}.\n")
        self.prepare()
        target = self.home / "shared/ja-korriban.atmosphere"
        self.assertIn("illuminance 9", target.read_text())
        for name in ("kor1", "kor2"):
            alias = self.home / (name + ".atmosphere")
            self.assertTrue(alias.is_symlink())
            backups = list((self.home / "atmosphere-backups").glob(name + ".*.atmosphere"))
            self.assertEqual(len(backups), 1)
            self.assertEqual(backups[0].read_text(), custom + f"// Note for {name}.\n")
        self.prepare()
        self.assertEqual(len(list((self.home / "atmosphere-backups").iterdir())), 2)

    def test_conflicting_edits_are_not_merged(self):
        first, second = self.edited("kor1", 9), self.edited("kor2", 11)
        (self.home / "kor1.atmosphere").write_text(first)
        (self.home / "kor2.atmosphere").write_text(second)
        self.prepare()
        for name, text in (("kor1", first), ("kor2", second)):
            self.assertFalse((self.home / (name + ".atmosphere")).is_symlink())
            self.assertEqual((self.home / (name + ".atmosphere")).read_text(), text)
        self.assertEqual((self.home / "shared/ja-korriban.atmosphere").read_bytes(),
                         (self.source / "kor1.atmosphere").read_bytes())

    def test_separate_yavin_tuning_and_invalid_override_survive(self):
        unique = self.edited("yavin1b", 8)
        invalid = "// Deliberately incomplete local edit.\natmosphere 1\n"
        (self.home / "yavin1b.atmosphere").write_text(unique)
        (self.home / "academy2.atmosphere").write_text(invalid)
        self.prepare()
        self.assertEqual((self.home / "yavin1b.atmosphere").read_text(), unique)
        self.assertFalse((self.home / "yavin1b.atmosphere").is_symlink())
        self.assertEqual((self.home / "academy2.atmosphere").read_text(), invalid)
        self.assertTrue((self.home / "academy1.atmosphere").is_symlink())

    def test_group_defaults_and_optional_blend(self):
        maps = json.loads((ROOT / "scripts/atmosphere-catalogue.json").read_text())["maps"]
        selected = [m for m in maps if m["palette"]]
        self.assertLess(len({(self.source / (m["map"] + ".atmosphere")).resolve() for m in selected}), len(selected))
        self.assertNotEqual(profile_key(self.source / "yavin1b.atmosphere"), profile_key(self.source / "yavin1.atmosphere"))
        first = self.home / "one.atmosphere"
        second = self.home / "two.atmosphere"
        text = re.sub(r"(?m)^skyBlend\s+[^\n]*\n?", "", (self.source / "t1_sour.atmosphere").read_text())
        first.write_text(text)
        second.write_text(text + "\n// Explicit default.\nskyBlend 1\n")
        self.assertEqual(profile_key(first), profile_key(second))


if __name__ == "__main__":
    unittest.main()
