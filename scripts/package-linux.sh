#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the inkypaint contributors
#
# inkypaint for Linux x86_64: the runtime (scarletnx, which extracts, recompiles and runs the game) and the Qt launcher
# (inkypaint), with the Qt libraries it loads, into
#   build/linux-package/inkypaint/                    the files: run ./inkypaint
#   build/inkypaint-<version>-linux-x86_64.tar.gz     the portable archive
#
# Built on the oldest system it should run on (the release builds in Ubuntu 22.04, glibc 2.35): the runtime links LLVM,
# libstdc++ and its other libraries statically and needs only glibc, so the copy a recompile puts beside the game runs
# on its own; the launcher carries Qt, found through $ORIGIN.
#
# Needs: clang and lld (any of clang-25 to clang-20 on PATH, or clang itself), CMake 3.25+, Ninja, NASM, Python 3 with
# venv, make, perl (OpenSSL's build), patch (FFmpeg's), pkg-config, patchelf, and the development headers SDL's video
# and audio backends build against (X11, Wayland, xkbcommon, ALSA, PulseAudio, PipeWire, D-Bus, udev). LLVM's libraries are
# built once by scripts/linux-llvm.sh into .cache/linux/llvm (about an hour on two cores), unless LLVM_DIR names a
# static LLVM's lib/cmake/llvm. Qt comes from Qt's own download server (aqtinstall in a virtual environment), the same
# version as the Windows package's. Nothing is installed system-wide, and nothing of the game goes into the package.
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
cd "$here"
lin="$here/.cache/linux"
jobs=${INKY_JOBS:-$(getconf _NPROCESSORS_ONLN)}
say() { printf '\033[1;35m[package-linux]\033[0m %s\n' "$*"; }
die() { printf '\033[1;31m[package-linux] %s\033[0m\n' "$*" >&2; exit 1; }
[ "$(uname -s)-$(uname -m)" = Linux-x86_64 ] || die "builds on Linux x86_64"
for t in cmake ninja python3 nasm make perl patch pkg-config patchelf; do
    command -v "$t" > /dev/null || die "needs $t"
done

# ---- the compiler: clang and lld under their plain names, which the presets and linux-llvm.sh call
mkdir -p "$lin/bin"
pick() { local n v; for v in 25 24 23 22 21 20; do n=$(command -v "$1-$v" 2> /dev/null) && { echo "$n"; return; }; done; command -v "$1" 2> /dev/null; }
for t in clang clang++ ld.lld llvm-ar llvm-ranlib llvm-nm llvm-strip; do
    p=$(pick "$t") || die "needs $t (clang and lld 20 or later)"
    ln -sf "$p" "$lin/bin/$t"
done
export PATH="$lin/bin:$PATH" CC=clang CXX=clang++
say "compiler: $(clang --version | head -1)"

# ---- LLVM's libraries, static, with the host's code generator alone
if [ -z "${LLVM_DIR:-}" ]; then
    LLVM_DIR="$lin/llvm/lib/cmake/llvm"
    if [ ! -f "$LLVM_DIR/LLVMConfig.cmake" ]; then
        say "LLVM's libraries into .cache/linux/llvm (scripts/linux-llvm.sh, once)"
        scripts/linux-llvm.sh x86_64 "$lin/llvm" > "$lin/llvm.log" 2>&1 || die "LLVM's build failed: .cache/linux/llvm.log"
        rm -rf "$lin/llvm.build" # the sources and the build tree: gigabytes the package doesn't need
    fi
fi
[ -f "$LLVM_DIR/LLVMConfig.cmake" ] || die "no LLVM in $LLVM_DIR"

# ---- Qt, from Qt's server: the version of the Windows package's
if [ ! -x "$lin/venv/bin/python" ]; then
    say "a Python environment for aqtinstall in .cache/linux/venv"
    python3 -m venv "$lin/venv"
    "$lin/venv/bin/pip" install -q aqtinstall
fi
qtver=$(sed -n 's/.*"file": "mingw-w64-clang-x86_64-qt6-base-\([0-9.]*\)-.*/\1/p' scripts/windows-deps.lock.json)
[ -n "$qtver" ] || die "scripts/windows-deps.lock.json names no qt6-base"
qtroot="$lin/qt/$qtver"
if [ ! -d "$qtroot" ]; then
    say "Qt $qtver into .cache/linux/qt"
    (cd "$lin" && "$lin/venv/bin/python" -m aqt install-qt linux desktop "$qtver" linux_gcc_64 -O "$lin/qt") \
        > "$lin/aqt.log" 2>&1 || die "aqtinstall failed: .cache/linux/aqt.log"
fi
config=$(find -L "$qtroot" -maxdepth 5 -path '*/lib/cmake/Qt6/Qt6Config.cmake' | head -1)
[ -n "$config" ] || die "no Qt in $qtroot"
qt=${config%/lib/cmake/Qt6/Qt6Config.cmake}

# ---- the runtime: libstdc++ and libgcc linked in, so it needs nothing beyond glibc wherever it is copied
say "the runtime (scarletnx) into build/linux"
cmake --preset default -B build/linux -DCMAKE_BUILD_TYPE=Release -DLLVM_DIR="$LLVM_DIR" -DSNX_DEPLOY=OFF \
    -DBUILD_TESTING=OFF -DSNX_BUILD_GUI=OFF -DSNX_A64_DISASM=OFF \
    -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld -static-libstdc++ -static-libgcc" \
    -DCMAKE_SHARED_LINKER_FLAGS="-fuse-ld=lld -static-libstdc++ -static-libgcc" > "$lin/configure.log" 2>&1 \
    || die "configure failed: .cache/linux/configure.log"
cmake --build build/linux --target scarletnx -j "$jobs" > "$lin/build.log" 2>&1 || die "build failed: .cache/linux/build.log"

# ---- the launcher
say "the launcher (inkypaint) into build/linux-launcher"
cmake -S src/launcher-qt -B build/linux-launcher -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$qt" \
    -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld" > "$lin/launcher-configure.log" 2>&1 \
    || die "the launcher's configure failed: .cache/linux/launcher-configure.log"
cmake --build build/linux-launcher -j "$jobs" > "$lin/launcher-build.log" 2>&1 \
    || die "the launcher's build failed: .cache/linux/launcher-build.log"

# ---- the files, as the archive holds them: Qt's layout (lib, plugins and qml side by side, as its RUNPATHs expect)
out="$here/build/linux-package/inkypaint"
rm -rf "$out"
mkdir -p "$out/runtime" "$out/lib" "$out/plugins" "$out/qml"
cp build/linux-launcher/inkypaint "$out/"
patchelf --set-rpath '$ORIGIN/lib' "$out/inkypaint"
llvm-strip --strip-unneeded "$out/inkypaint"
cp build/linux/scarletnx "$out/runtime/"
llvm-strip --strip-debug "$out/runtime/scarletnx"
for p in platforms/libqxcb.so platforms/libqwayland.so platforms/libqoffscreen.so xcbglintegrations \
         wayland-decoration-client wayland-graphics-integration-client wayland-shell-integration \
         platforminputcontexts tls iconengines imageformats/libqico.so imageformats/libqjpeg.so \
         imageformats/libqsvg.so networkinformation platformthemes/libqxdgdesktopportal.so; do
    [ -e "$qt/plugins/$p" ] || continue
    mkdir -p "$out/plugins/$(dirname "$p")"
    cp -R "$qt/plugins/$p" "$out/plugins/$(dirname "$p")/"
done
for m in QtQuick QtQml QtCore QtNetwork QtWayland; do
    [ -d "$qt/qml/$m" ] && cp -R "$qt/qml/$m" "$out/qml/"
done
printf '[Paths]\nPrefix = .\nPlugins = plugins\nQmlImports = qml\nLibraries = lib\n' > "$out/qt.conf"
# every Qt library (and Qt's ICU) the launcher, a plugin or a QML module needs, until none is missing. The system's own
# (libc, libGL, libxcb, Wayland's, fontconfig) stay the system's; the xcb utility libraries Qt's X11 plugin needs and
# few systems install (xcb-cursor most of all) come along from this one
"$lin/venv/bin/python" - "$out" "$qt/lib" <<'PY'
import os, shutil, subprocess, sys
root, qtlib = sys.argv[1], sys.argv[2]
extra = ("libxcb-cursor.so", "libxcb-icccm.so", "libxcb-image.so", "libxcb-keysyms.so", "libxcb-render-util.so",
         "libxcb-util.so", "libxkbcommon-x11.so")
def needed(path):
    out = subprocess.run(["patchelf", "--print-needed", path], capture_output=True, text=True)
    return out.stdout.split() if out.returncode == 0 else []
def system_lib(name):
    for d in ("/usr/lib/x86_64-linux-gnu", "/lib/x86_64-linux-gnu", "/usr/lib64", "/usr/lib"):
        if os.path.exists(os.path.join(d, name)):
            return os.path.realpath(os.path.join(d, name))
    return None
lib = os.path.join(root, "lib")
todo = [os.path.join(r, f) for r, _, fs in os.walk(root) for f in fs
        if f.endswith(".so") or ".so." in f or f == "inkypaint"]
todo = [p for p in todo if not p.startswith(os.path.join(root, "runtime"))]
copied = 0
while todo:
    for name in needed(todo.pop()):
        dest = os.path.join(lib, name)
        if os.path.exists(dest):
            continue
        source = os.path.join(qtlib, name) if os.path.exists(os.path.join(qtlib, name)) else (
            system_lib(name) if name.startswith(extra) else None)
        if source:
            shutil.copy2(os.path.realpath(source), dest)
            subprocess.run(["patchelf", "--set-rpath", "$ORIGIN", dest], check=True)
            copied += 1
            todo.append(dest)
print("package-linux: %d libraries" % copied)
PY
# what the launcher still needs that the system has to give: said now, not when a player starts it
missing=$(LD_LIBRARY_PATH="$out/lib" ldd "$out/inkypaint" "$out/plugins/platforms/"*.so 2>/dev/null | grep 'not found' | sort -u || true)
[ -z "$missing" ] || die "the launcher has unresolved libraries:
$missing"
runtime_libs=$(ldd "$out/runtime/scarletnx" | awk '{print $1}' | grep -vE '^(linux-vdso|/lib64/ld-linux)' | sort -u | tr '\n' ' ')
say "the runtime links: $runtime_libs"
for l in $runtime_libs; do
    case "$l" in libc.so.*|libm.so.*|libdl.so.*|libpthread.so.*|librt.so.*) ;; *) die "the runtime links $l, which a player may not have" ;; esac
done

# the icon, a desktop entry the player can install, and the licenses
cp dist/launcher/AppIcon-1024.png "$out/inkypaint.png"
cat > "$out/install-desktop-entry.sh" <<'SH'
#!/bin/sh
# Adds inkypaint to the desktop's applications, run from where this folder is now (run it again after moving it)
here=$(cd "$(dirname "$0")" && pwd)
apps=${XDG_DATA_HOME:-$HOME/.local/share}/applications
mkdir -p "$apps"
cat > "$apps/inkypaint.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=inkypaint
Comment=Splatoon 3, recompiled for this PC from your own copy
Exec="$here/inkypaint"
Icon=$here/inkypaint.png
Categories=Game;
Terminal=false
EOF
echo "inkypaint is in your applications now ($apps/inkypaint.desktop)"
SH
chmod +x "$out/install-desktop-entry.sh"
mkdir -p "$out/licenses"
cp LICENSE "$out/licenses/inkypaint-LICENSE.txt"
[ -f docs/CREDITS.md ] && cp docs/CREDITS.md "$out/licenses/CREDITS.md"
[ -d "$qt/../../Licenses" ] && cp -R "$qt/../../Licenses" "$out/licenses/Qt" 2>/dev/null || true

version=${INKY_VERSION:-$(git describe --tags --always 2>/dev/null || echo dev)}
archive="$here/build/inkypaint-$version-linux-x86_64.tar.gz"
rm -f "$archive"
tar -C "$here/build/linux-package" -czf "$archive" inkypaint
say "archive: build/$(basename "$archive") ($(du -h "$archive" | cut -f1))"
