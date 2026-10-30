#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the inkypaint contributors
import importlib.util
from pathlib import Path
import signal
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("ios_install", ROOT / "scripts/resolve-ios-install.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class BuilderTools(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="inky builder ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
    def make_game(self, folder, title=module.TITLE):
        (folder / "exefs").mkdir(parents=True)
        (folder / "romfs").mkdir()
        meta = bytearray(0x100)
        meta[:4] = b"META"
        struct.pack_into("<I", meta, 0x70, 0x80)
        meta[0x80:0x84] = b"ACI0"
        struct.pack_into("<Q", meta, 0x90, int(title,16))
        (folder / "exefs/main.npdm").write_bytes(meta)
        (folder / "exefs/main").write_bytes(b"synthetic code")
    def test_installed_data_title_and_runtime_selection(self):
        data = self.root / "data"
        title = data / "recompiled" / module.TITLE
        game = title / (module.TITLE + "_data")
        self.make_game(game)
        exe = title / module.TITLE
        exe.write_bytes(b"synthetic runtime")
        for selection in [data, title, game, exe]:
            self.assertEqual(module.resolve_install(selection), game.resolve())
    def test_repo_and_extracted_folder(self):
        game = self.root / "local/data/work/splatoon3"
        self.make_game(game)
        self.assertEqual(module.resolve_install(self.root), game.resolve())
        self.assertEqual(module.resolve_install(game), game.resolve())
    def test_rejects_wrong_title(self):
        self.make_game(self.root / "other", "0100000000000001")
        with self.assertRaises(ValueError):
            module.resolve_install(self.root / "other")
    def test_cancel_stops_owned_worker(self):
        ready, stopped = self.root / "ready", self.root / "stopped"
        code = "import signal,time,pathlib,sys; signal.signal(signal.SIGTERM,lambda *_:(pathlib.Path(sys.argv[2]).write_text('stopped'),sys.exit(0))); pathlib.Path(sys.argv[1]).write_text('ready'); print('ready',flush=True); time.sleep(30)"
        p = subprocess.Popen([sys.executable, str(ROOT / "scripts/run-ios-job.py"), sys.executable, "-c", code,
                              str(ready), str(stopped)], stdout=subprocess.PIPE, text=True)
        try:
            self.assertEqual(p.stdout.readline().strip(), "ready")
            p.terminate()
            self.assertEqual(p.wait(timeout=5), 130)
            self.assertTrue(stopped.exists())
        finally:
            if p.poll() is None: p.kill(); p.wait()
            p.stdout.close()

if __name__ == "__main__":
    unittest.main()
