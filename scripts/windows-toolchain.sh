#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the inkypaint contributors
#
# A Windows x86_64 cross toolchain the way llvm-mingw makes one, from this machine's clang and lld (LLVM 22 or later,
# for MSYS2's libc++ 23 headers) and the MinGW-w64 sysroot scripts/windows-deps.py fetched: wrappers named
# x86_64-w64-mingw32-clang, -clang++, -windres, -ar, -ranlib, -nm, -strip, -objdump and -dlltool in DEST/bin, which
# the windows-cross preset and FFmpeg's and OpenSSL's builds find on PATH. clang's own builtin headers stay its own;
# compiler-rt comes from the sysroot, through a resource folder made of both.
#   scripts/windows-toolchain.sh [DEST]     (DEST: .cache/windows, where windows-deps.py put clang64/)
#   LLVM_BIN=/opt/homebrew/opt/llvm/bin scripts/windows-toolchain.sh   another LLVM's tools
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
dest=$(cd "${1:-$here/../.cache/windows}" && pwd)
sysroot="$dest/clang64"
[ -d "$sysroot/include" ] || { echo "windows-toolchain: no sysroot in $sysroot: run scripts/windows-deps.py first" >&2; exit 1; }
find_tool() { # the first of the names that runs, in LLVM_BIN first
    local n
    for n in "$@"; do
        if [ -n "${LLVM_BIN:-}" ] && [ -x "$LLVM_BIN/$n" ]; then echo "$LLVM_BIN/$n"; return; fi
        if command -v "$n" > /dev/null 2>&1; then command -v "$n"; return; fi
    done
    echo "windows-toolchain: none of $* found (LLVM 22 or later with lld: LLVM_BIN=its bin folder)" >&2
    exit 1
}
# LLVM 22 or later: LLVM_BIN's, a versioned one on PATH (Ubuntu's clang-23), Homebrew's, or the plain clang on PATH
pick_llvm() {
    local x c
    if [ -n "${LLVM_BIN:-}" ] && [ -x "$LLVM_BIN/clang" ]; then echo "$LLVM_BIN"; return; fi
    for x in 25 24 23 22; do
        c=$(command -v "clang-$x" 2> /dev/null) && { echo "versioned:$x"; return; }
    done
    for c in /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin "$dest/llvm-mingw/bin"; do
        if [ -x "$c/clang" ] && [ -x "$c/ld.lld" ] && [ "$("$c/clang" -dumpversion | cut -d. -f1)" -ge 22 ]; then echo "$c"; return; fi
    done
    c=$(command -v clang 2> /dev/null) && [ "$("$c" -dumpversion | cut -d. -f1)" -ge 22 ] && command -v ld.lld > /dev/null &&
        { dirname "$c"; return; }
    echo ""
}
llvm=$(pick_llvm)
if [ -z "$llvm" ] && [ "$(uname -s)" = Darwin ]; then
    # none here: llvm-mingw's newest macOS release, its clang and lld alone (the sysroot stays MSYS2's)
    echo "windows-toolchain: no LLVM 22 or later with lld here: fetching llvm-mingw's macOS release into $dest/llvm-mingw" >&2
    url=$(curl -fsSL https://api.github.com/repos/mstorsjo/llvm-mingw/releases/latest |
          sed -n 's/.*"browser_download_url": "\([^"]*ucrt-macos-universal\.tar\.xz\)".*/\1/p' | head -1)
    [ -n "$url" ] || { echo "windows-toolchain: couldn't find llvm-mingw's macOS release" >&2; exit 1; }
    rm -rf "$dest/llvm-mingw" "$dest/llvm-mingw.tar.xz"
    curl -fL --progress-bar -o "$dest/llvm-mingw.tar.xz" "$url"
    mkdir -p "$dest/llvm-mingw"
    tar -xJf "$dest/llvm-mingw.tar.xz" -C "$dest/llvm-mingw" --strip-components=1
    rm -f "$dest/llvm-mingw.tar.xz"
    llvm=$(pick_llvm)
fi
[ -n "$llvm" ] || { echo "windows-toolchain: needs clang and lld of LLVM 22 or later (MSYS2's libc++ 23): install them, or LLVM_BIN=their bin folder" >&2; exit 1; }
v=""
case "$llvm" in
    versioned:*) v="-${llvm#versioned:}" ;;
    *) LLVM_BIN="$llvm" ;;
esac
clang=$(find_tool "clang$v" clang)
major=$("$clang" -dumpversion | cut -d. -f1)
lld=$(find_tool "ld.lld$v" ld.lld)
ar=$(find_tool "llvm-ar$v" llvm-ar)
ranlib=$(find_tool "llvm-ranlib$v" llvm-ranlib)
nm=$(find_tool "llvm-nm$v" llvm-nm)
strip=$(find_tool "llvm-strip$v" llvm-strip)
objdump=$(find_tool "llvm-objdump$v" llvm-objdump)
dlltool=$(find_tool "llvm-dlltool$v" llvm-dlltool)
rc=$(find_tool "llvm-windres$v" llvm-windres llvm-rc "llvm-rc$v")
# the resource folder: clang's headers, the sysroot's compiler-rt for Windows
res="$dest/resource"
rm -rf "$res"
mkdir -p "$res/lib"
ln -s "$("$clang" -print-resource-dir)/include" "$res/include"
cp -R "$sysroot/lib/clang/"*/lib/windows "$res/lib/windows"
mkdir -p "$dest/bin"
t=x86_64-w64-mingw32
# the target's options in a configuration file, as llvm-mingw's: options from one aren't reported as unused when a
# step compiles without linking, which -Werror would make an error
cat > "$dest/$t.cfg" <<C
--target=$t
--sysroot=$sysroot
-resource-dir=$res
-rtlib=compiler-rt
-unwindlib=libunwind
-fuse-ld=lld
--ld-path=$lld
C
for lang in clang clang++; do
    cat > "$dest/bin/$t-$lang" <<W
#!/bin/sh
exec "$clang" --driver-mode=$([ $lang = clang ] && echo gcc || echo g++) --config="$dest/$t.cfg" $([ $lang = clang++ ] && echo -stdlib=libc++) "\$@"
W
done
cp "$dest/bin/$t-clang" "$dest/bin/$t-gcc"
cp "$dest/bin/$t-clang++" "$dest/bin/$t-g++"
for pair in "ar:$ar" "ranlib:$ranlib" "nm:$nm" "strip:$strip" "objdump:$objdump" "dlltool:$dlltool"; do
    printf '#!/bin/sh\nexec "%s" "$@"\n' "${pair#*:}" > "$dest/bin/$t-${pair%%:*}"
done
case "$rc" in
    *windres*) printf '#!/bin/sh\nexec "%s" --target=pe-x86-64 -I"%s/include" "$@"\n' "$rc" "$sysroot" > "$dest/bin/$t-windres" ;;
    *) echo "windows-toolchain: no llvm-windres, only llvm-rc: install a newer LLVM" >&2; exit 1 ;;
esac
chmod +x "$dest/bin/"*
echo "windows-toolchain: $dest/bin (LLVM $major): PATH=\"$dest/bin:\$PATH\""
