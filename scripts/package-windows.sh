#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the inkypaint contributors
#
# inkypaint for Windows x86_64, cross-built on macOS or Linux: the runtime (scarletnx.exe, which extracts, recompiles
# and runs the game) and the Qt launcher (inkypaint.exe), with the Qt and MSYS2 DLLs they load, into
#   build/windows-package/inkypaint/                     the files, as installed
#   build/inkypaint-<version>-windows-x86_64.zip         the portable zip
#   build/inkypaint-<version>-windows-x86_64-setup.exe   the installer, when makensis is on PATH (brew install makensis)
#
# Needs: clang and lld 22 or later (LLVM_BIN=its bin folder when they aren't on PATH, Homebrew's
# /opt/homebrew/opt/llvm/bin), CMake, Ninja and NASM, Python 3 with zstd (or Python's zstandard module), glslangValidator only
# where the build doesn't fetch its own. The prebuilt parts come from MSYS2 (scripts/windows-deps.py) and Qt's own
# download server (the host Qt, through aqtinstall in a virtual environment): nothing is installed system-wide.
# Nothing of the game goes into the package: players prepare their own copy with the launcher.
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
cd "$here"
win="$here/.cache/windows"
jobs=${INKY_JOBS:-2}
say() { printf '\033[1;35m[package-windows]\033[0m %s\n' "$*"; }
die() { printf '\033[1;31m[package-windows] %s\033[0m\n' "$*" >&2; exit 1; }
command -v cmake > /dev/null || die "needs CMake"
command -v ninja > /dev/null || die "needs Ninja"
command -v python3 > /dev/null || die "needs Python 3"
command -v nasm > /dev/null || die "needs NASM for FFmpeg's x86-64 assembly (brew install nasm)"

# ---- the prebuilt parts and the toolchain
mkdir -p "$win"
if [ ! -x "$win/venv/bin/python" ]; then
    say "a Python environment for aqtinstall and zstandard in .cache/windows/venv"
    python3 -m venv "$win/venv"
    "$win/venv/bin/pip" install -q aqtinstall zstandard
fi
py="$win/venv/bin/python"
"$py" scripts/windows-deps.py --dest "$win"
scripts/windows-toolchain.sh "$win" > /dev/null
export PATH="$win/bin:$PATH"
qtver=$(sed -n 's/.*"file": "mingw-w64-clang-x86_64-qt6-base-\([0-9.]*\)-.*/\1/p' scripts/windows-deps.lock.json)
[ -n "$qtver" ] || die "the lock names no qt6-base"
case "$(uname -s)-$(uname -m)" in
    Darwin-*) hostos=mac; hostarch=clang_64 ;;
    Linux-x86_64) hostos=linux; hostarch=linux_gcc_64 ;;
    Linux-aarch64) hostos=linux_arm64; hostarch=linux_gcc_arm64 ;;
    *) die "no host Qt for $(uname -s) $(uname -m)" ;;
esac
hostqt="$win/hostqt/$qtver"
if [ ! -d "$hostqt" ]; then
    say "Qt $qtver for this machine (its moc, rcc and QML tools) into .cache/windows/hostqt"
    # from .cache/windows: aqtinstall writes its own aqtinstall.log into the folder it runs in
    (cd "$win" && "$py" -m aqt install-qt "$hostos" desktop "$qtver" "$hostarch" -O "$win/hostqt") > "$win/aqt.log" 2>&1 \
        || die "aqtinstall failed: see .cache/windows/aqt.log"
fi
config=$(find "$hostqt" -maxdepth 5 -path '*/lib/cmake/Qt6/Qt6Config.cmake' | head -1)
[ -n "$config" ] || die "no host Qt in $hostqt"
qthost=${config%/lib/cmake/Qt6/Qt6Config.cmake} # its folder (gcc_64, macos)

# ---- the runtime
say "the runtime (scarletnx.exe) into build/windows"
cmake --preset windows ${SNX_GLSLANG:+-DSNX_GLSLANG="$SNX_GLSLANG"} > "$win/configure.log" 2>&1 || die "configure failed: .cache/windows/configure.log"
cmake --build build/windows --target scarletnx -j "$jobs" > "$win/build.log" 2>&1 || die "build failed: .cache/windows/build.log"

# ---- the launcher
say "the launcher (inkypaint.exe) into build/windows-launcher"
cmake -S src/launcher-qt -B build/windows-launcher -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_SYSTEM_PROCESSOR=x86_64 \
    -DCMAKE_C_COMPILER=x86_64-w64-mingw32-clang -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-clang++ \
    -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres -DCMAKE_FIND_ROOT_PATH="$win/clang64" \
    -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
    -DCMAKE_PREFIX_PATH="$win/clang64" -DQT_HOST_PATH="$qthost" > "$win/launcher-configure.log" 2>&1 \
    || die "the launcher's configure failed: .cache/windows/launcher-configure.log"
cmake --build build/windows-launcher -j "$jobs" > "$win/launcher-build.log" 2>&1 \
    || die "the launcher's build failed: .cache/windows/launcher-build.log"

# ---- the files, as installed
out="$here/build/windows-package/inkypaint"
rm -rf "$out"
mkdir -p "$out/runtime"
cp build/windows-launcher/inkypaint.exe "$out/"
# the runtime in a folder of its own with only its DLLs: a recompile copies every DLL beside it into the game's folder
cp build/windows/scarletnx.exe "$out/runtime/"
x86_64-w64-mingw32-strip --strip-debug "$out/runtime/scarletnx.exe"
qt="$win/clang64/share/qt6"
mkdir -p "$out/plugins" "$out/qml"
for p in platforms/qwindows.dll tls/qopensslbackend.dll tls/qschannelbackend.dll tls/qcertonlybackend.dll \
         imageformats/qico.dll imageformats/qjpeg.dll imageformats/qsvg.dll iconengines \
         networkinformation; do
    [ -e "$qt/plugins/$p" ] || continue
    mkdir -p "$out/plugins/$(dirname "$p")"
    cp -R "$qt/plugins/$p" "$out/plugins/$(dirname "$p")/"
done
for m in QtQuick QtQml QtCore QtNetwork; do
    [ -d "$qt/qml/$m" ] && cp -R "$qt/qml/$m" "$out/qml/"
done
find "$out" \( -name '*.debug' -o -name '*.pdb' -o -name '*.a' \) -delete
# Qt Quick's fallback file/folder pickers import this module. Native dialogs
# aren't available on every desktop, so it must ship even if build-time scans
# only see our direct QtQuick.Dialogs import.
foldermodel="$qt/qml/Qt/labs/folderlistmodel"
[ -d "$foldermodel" ] || die "Qt's fallback file picker needs $foldermodel"
mkdir -p "$out/qml/Qt/labs"
cp -R "$foldermodel" "$out/qml/Qt/labs/"
printf '[Paths]\nPrefix = .\nPlugins = plugins\nQmlImports = qml\n' > "$out/qt.conf"
# every DLL any shipped program, plugin or QML module imports, from MSYS2's bin, until none is missing: beside the
# launcher for its files, in runtime/ for the runtime's. Windows' own (the api-ms-win sets, kernel32...) aren't in it
"$py" - "$out" "$win/clang64/bin" <<'PY'
import os, shutil, struct, sys
root, bin_ = sys.argv[1], sys.argv[2]
have = {f.lower(): f for f in os.listdir(bin_) if f.lower().endswith(".dll")}
def imports(path):
    d = open(path, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    opt = pe + 24
    magic = struct.unpack_from("<H", d, opt)[0]
    dirs = opt + (112 if magic == 0x20B else 96)
    rva = struct.unpack_from("<I", d, dirs + 8)[0]
    secs = [struct.unpack_from("<IIII", d, opt + struct.unpack_from("<H", d, pe + 20)[0] + 40 * i + 8) for i in range(nsec)]
    def off(r):
        for vsize, va, rsize, ptr in secs:
            if va <= r < va + max(vsize, rsize):
                return ptr + r - va
        return None
    names, at = [], off(rva) if rva else None
    while at is not None:
        name_rva = struct.unpack_from("<I", d, at + 12)[0]
        if not name_rva:
            break
        o = off(name_rva)
        names.append(d[o:d.index(b"\0", o)].decode())
        at += 20
    return names
def close(out, files):
    todo, copied = list(files), set()
    while todo:
        for name in imports(todo.pop()):
            n = name.lower()
            if n in have and n not in copied and not os.path.exists(os.path.join(out, have[n])):
                shutil.copy2(os.path.join(bin_, have[n]), os.path.join(out, have[n]))
                copied.add(n)
                todo.append(os.path.join(out, have[n]))
    return len(copied)
runtime = os.path.join(root, "runtime")
launcher = [os.path.join(r, f) for r, _, fs in os.walk(root) if not r.startswith(runtime) for f in fs
            if f.lower().endswith((".exe", ".dll"))]
print("package-windows: %d DLLs for the launcher, %d for the runtime" %
      (close(root, launcher), close(runtime, [os.path.join(runtime, "scarletnx.exe")])))
PY
# the licenses: inkypaint's (GPL-3.0-or-later), and those of what it carries
mkdir -p "$out/licenses"
cp LICENSE "$out/licenses/inkypaint-LICENSE.txt"
[ -f docs/CREDITS.md ] && cp docs/CREDITS.md "$out/licenses/CREDITS.md"
for d in "$win/clang64/share/licenses"/*; do
    [ -d "$d" ] && cp -R "$d" "$out/licenses/" 2>/dev/null || true
done
# the game's logo, when the checkout has one (nothing of the game is in git: local/branding is the player's own)
[ -f local/branding/splatoon3-logo.png ] && cp local/branding/splatoon3-logo.png "$out/"
cp dist/launcher/inkypaint.ico "$out/"

version=$(git describe --tags --always 2>/dev/null || echo dev)
zip="$here/build/inkypaint-$version-windows-x86_64.zip"
rm -f "$zip"
"$py" - "$here/build/windows-package" "$zip" <<'PY'
import os, sys, zipfile
root, zip_ = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(zip_, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for r, _, fs in os.walk(root):
        for f in sorted(fs):
            p = os.path.join(r, f)
            z.write(p, os.path.relpath(p, root))
PY
say "portable zip: build/$(basename "$zip") ($(du -h "$zip" | cut -f1))"
if command -v makensis > /dev/null; then
    makensis -V2 -DVERSION="$version" -DSOURCE="$out" -DOUTFILE="$here/build/inkypaint-$version-windows-x86_64-setup.exe" \
        dist/windows/inkypaint.nsi > "$win/nsis.log" 2>&1 || die "makensis failed: .cache/windows/nsis.log"
    say "installer: build/inkypaint-$version-windows-x86_64-setup.exe"
else
    say "no makensis on PATH (brew install makensis): the installer was left out"
fi
