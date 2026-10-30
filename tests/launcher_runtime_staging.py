#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the inkypaint contributors
"""Check the launcher CMake runtime staging with a synthetic executable only."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="inky runtime staging ") as tmp:
    root = Path(tmp)
    runtime = root / "scarletnx-fixture"
    runtime.write_text("synthetic runtime v1\n")
    runtime.chmod(0o755)
    source, build, installed = root / "source", root / "build", root / "installed"
    source.mkdir()
    (source / "main.c").write_text("int main(void) { return 0; }\n")
    (source / "CMakeLists.txt").write_text('cmake_minimum_required(VERSION 3.21)\nproject(runtime_fixture C)\n'
        'add_executable(inkypaint main.c)\ninclude("' + str(ROOT / "src/launcher-qt/runtime.cmake") + '")\n')
    def run(*args):
        p = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if p.returncode:
            raise RuntimeError(p.stdout)
    run("cmake", "-S", str(source), "-B", str(build), "-DINKYPAINT_RUNTIME_FILE=" + str(runtime))
    run("cmake", "--build", str(build), "--parallel", "2")
    copied = build / "runtime/scarletnx"
    assert copied.read_bytes() == runtime.read_bytes()
    assert copied.stat().st_mode & 0o100
    runtime.write_text("synthetic runtime v2\n")
    run("cmake", "--build", str(build), "--parallel", "2")
    assert copied.read_bytes() == runtime.read_bytes()
    run("cmake", "--install", str(build), "--prefix", str(installed))
    assert (installed / "runtime/scarletnx").read_bytes() == runtime.read_bytes()
print("PASS launcher runtime staging, refresh and install")
