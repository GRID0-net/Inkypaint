#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the inkypaint contributors
"""Package synthetic assets only; no compiler, game, GPU or device is started."""
import importlib.util
import os
from pathlib import Path
import plistlib
import subprocess
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("ios_assets", ROOT / "scripts/stage-ios-assets.py")
assets = importlib.util.module_from_spec(spec)
spec.loader.exec_module(assets)

class Packaging(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.source = self.root / "source"
        for name, data in {"exefs/main.npdm": b"synthetic npdm", "romfs/fixture.bin": b"synthetic romfs",
                           "save/user/private": b"save", "keys/prod.keys": b"key", "mods/config": b"mod",
                           "account.json": b"credential", "game.source": b"Mac path"}.items():
            p = self.source / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(data)

    def test_only_immutable_assets(self):
        dst = self.root / "stage"
        dst.mkdir()
        (dst / "stale").write_text("old")
        assets.stage(self.source, dst)
        self.assertEqual(sorted(str(p.relative_to(dst)) for p in dst.rglob("*") if p.is_file()),
                         ["exefs/main.npdm", "romfs/fixture.bin"])
        self.assertTrue((self.source / "save/user/private").exists())

    def test_rejects_symlink_before_replacing_staging(self):
        dst = self.root / "stage"
        dst.mkdir()
        (dst / "keep").write_text("existing package")
        (self.source / "romfs/linked").symlink_to(self.source / "account.json")
        with self.assertRaises(ValueError):
            assets.stage(self.source, dst)
        self.assertTrue((dst / "keep").exists())

    def test_rejects_overlap(self):
        with self.assertRaises(ValueError):
            assets.stage(self.source, self.source / "staging")

    @unittest.skipUnless(os.uname().sysname == "Darwin", "Apple IPA packager uses plutil")
    def test_ipa_contains_assets_and_both_frameworks(self):
        stage = self.root / "prefix"
        app = stage / "scarletnx.app"
        fw = app / "Frameworks/InkypaintGame.framework"
        fw.mkdir(parents=True)
        (fw / "InkypaintGame").write_bytes(b"synthetic game code")
        (app / "scarletnx").write_bytes(b"synthetic runtime")
        (app / "Info.plist").write_bytes(plistlib.dumps({"CFBundleExecutable": "scarletnx"}))
        (stage / "share").mkdir()
        mvk = self.root / "mvk"
        mf = mvk / "dynamic/MoltenVK.xcframework/ios-arm64/MoltenVK.framework"
        mf.mkdir(parents=True)
        (mf / "MoltenVK").write_bytes(b"synthetic GPU framework")
        out = self.root / "out"
        env = dict(os.environ, INKY_IOS_ASSETS=str(self.source))
        subprocess.run(["bash", str(ROOT / "scripts/apple-app.sh"), "ios", str(stage), str(out), str(mvk)],
                       env=env, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        with zipfile.ZipFile(out / "ScarletNX.ipa") as z:
            self.assertIsNone(z.testzip())
            prefix = "Payload/ScarletNX.app/"
            self.assertEqual(z.read(prefix + "GameAssets/romfs/fixture.bin"), b"synthetic romfs")
            self.assertEqual(z.read(prefix + "GameAssets/exefs/main.npdm"), b"synthetic npdm")
            self.assertIn(prefix + "Frameworks/InkypaintGame.framework/InkypaintGame", z.namelist())
            self.assertIn(prefix + "Frameworks/MoltenVK.framework/MoltenVK", z.namelist())
            self.assertFalse(any("/GameAssets/save/" in n or "/GameAssets/keys/" in n or "account.json" in n
                                 for n in z.namelist()))

if __name__ == "__main__":
    unittest.main()
