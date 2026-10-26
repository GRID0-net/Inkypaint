#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the inkypaint contributors
#
# Builds the inkypaint launcher (src/launcher, SwiftUI, macOS 26 and later) into a self-contained app:
#
#   build/launcher/inkypaint.app/Contents/
#     MacOS/Inkypaint        the launcher
#     MacOS/scarletnx        the runtime: extracts, recompiles and, copied next to the game, runs it
#     Frameworks/            MoltenVK, and Homebrew's LLVM with what it links, all found through @rpath
#     Resources/             the icon and the ink splats
#
# The runtime must be built first (./inkypaint.sh build, which `./inkypaint.sh launcher` runs). Nothing of the game
# goes in the app. Needs Xcode 26 (Liquid Glass is in the macOS 26 SDK). The app is signed ad hoc, for this Mac.
#
# Tunables: INKY_RUNTIME (default build/macos/scarletnx), INKY_APP_OUT (default build/launcher), INKY_VERSION.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LAUNCHER="$ROOT/src/launcher"
OUT="${INKY_APP_OUT:-$ROOT/build/launcher}"
APP="$OUT/inkypaint.app"
RUNTIME="${INKY_RUNTIME:-$ROOT/build/macos/scarletnx}"
VERSION="${INKY_VERSION:-0.1.0}"

say() { printf '\033[1;35m[launcher]\033[0m %s\n' "$*"; }
die() { printf '\033[1;31m[launcher]\033[0m %s\n' "$*" >&2; exit 1; }

[ "$(uname -s)" = Darwin ] || die "the launcher is a macOS app: build it on the Mac"
xcrun --find swift > /dev/null 2>&1 || die "no Swift toolchain: install Xcode 26 and run xcode-select -s /Applications/Xcode.app"
[ -x "$RUNTIME" ] || die "no runtime at $RUNTIME: ./inkypaint.sh build"
MVK="$(find "$ROOT/local/moltenvk" -path '*/dynamic/*' -name libMoltenVK.dylib 2>/dev/null | head -1)"
[ -n "$MVK" ] || die "no MoltenVK in local/moltenvk: ./inkypaint.sh setup"

# ---------------------------------------------------------------------------------------------------- the launcher
say "building the launcher (Swift, release)"
xcrun swift build -c release --arch arm64 --package-path "$LAUNCHER"
BIN="$(xcrun swift build -c release --arch arm64 --package-path "$LAUNCHER" --show-bin-path)"

rm -rf "$APP"
C="$APP/Contents"
mkdir -p "$C/MacOS" "$C/Resources" "$C/Frameworks"
cp "$BIN/Inkypaint" "$C/MacOS/Inkypaint"
# SwiftPM's resource bundle (Bundle.module looks in Contents/Resources), and the splats where Bundle.main finds them
[ -d "$BIN/Inkypaint_Inkypaint.bundle" ] && cp -R "$BIN/Inkypaint_Inkypaint.bundle" "$C/Resources/"
cp "$LAUNCHER"/Sources/Inkypaint/Resources/*.png "$C/Resources/"

build_number="$(git -C "$ROOT" rev-list --count HEAD 2>/dev/null || echo 1)"
sed -e "s/@VERSION@/$VERSION/" -e "s/@BUILD@/$build_number/" "$LAUNCHER/Packaging/Info.plist" > "$C/Info.plist"
printf 'APPL????' > "$C/PkgInfo"

say "making the icon"
iconset="$(mktemp -d)/AppIcon.iconset"
mkdir -p "$iconset"
for s in 16 32 128 256 512; do
    sips -z $s $s "$LAUNCHER/Packaging/AppIcon-1024.png" --out "$iconset/icon_${s}x${s}.png" > /dev/null
    sips -z $((s * 2)) $((s * 2)) "$LAUNCHER/Packaging/AppIcon-1024.png" --out "$iconset/icon_${s}x${s}@2x.png" > /dev/null
done
iconutil -c icns "$iconset" -o "$C/Resources/AppIcon.icns"

# ---------------------------------------------------------------------------------------------------- the runtime
say "bundling the runtime, MoltenVK and LLVM"
cp -L "$RUNTIME" "$C/MacOS/scarletnx"
chmod u+w,+x "$C/MacOS/scarletnx"
cp -L "$MVK" "$C/Frameworks/libMoltenVK.dylib"
chmod u+w "$C/Frameworks/libMoltenVK.dylib"

FW="$C/Frameworks"
is_system() { case "$1" in /usr/lib/*|/System/*) return 0 ;; *) return 1 ;; esac; }
rpaths_of() { otool -l "$1" | awk '/cmd LC_RPATH/{r=1} r && /path /{print $2; r=0}'; }

# A dependency's install name made a file: absolute, or through @rpath/@loader_path and the original's rpaths
resolve() { # dep, original file
    local dep="$1" orig="$2" name d
    case "$dep" in
        @rpath/*|@loader_path/*|@executable_path/*)
            name="${dep#*/}"
            for d in $(rpaths_of "$orig" | sed "s|@loader_path|$(dirname "$orig")|") "$(dirname "$orig")" \
                     /opt/homebrew/lib /opt/homebrew/opt/llvm/lib; do
                [ -f "$d/$name" ] && { echo "$d/$name"; return 0; }
            done
            return 1 ;;
        *) [ -f "$dep" ] && echo "$dep" ;;
    esac
}

# The non-system libraries file links, copied into Frameworks (with theirs, recursively) and linked through @rpath.
# orig is where file came from, for its @rpath dependencies
bundle_deps() { # file, original file
    local file="$1" orig="$2" own dep path name
    own="$(otool -D "$file" 2>/dev/null | sed -n 2p)"
    for dep in $(otool -L "$file" | tail -n +2 | awk '{print $1}'); do
        [ "$dep" = "$own" ] && continue
        is_system "$dep" && continue
        name="$(basename "$dep")"
        [ "$dep" = "@rpath/$name" ] && [ -f "$FW/$name" ] && continue
        path="$(resolve "$dep" "$orig")" || die "can't find $dep (linked by $(basename "$file"))"
        if [ ! -f "$FW/$name" ]; then
            cp -L "$path" "$FW/$name"
            chmod u+w "$FW/$name"
            install_name_tool -id "@rpath/$name" "$FW/$name" 2> /dev/null
            bundle_deps "$FW/$name" "$path"
        fi
        install_name_tool -change "$dep" "@rpath/$name" "$file" 2> /dev/null
    done
    for r in $(rpaths_of "$file"); do install_name_tool -delete_rpath "$r" "$file" 2> /dev/null || true; done
}

bundle_deps "$C/MacOS/scarletnx" "$RUNTIME"
install_name_tool -add_rpath "@executable_path/../Frameworks" "$C/MacOS/scarletnx"
for lib in "$FW"/*.dylib; do
    install_name_tool -add_rpath "@loader_path" "$lib" 2> /dev/null || true
done

# ---------------------------------------------------------------------------------------------------- signing
say "signing (ad hoc)"
for lib in "$FW"/*.dylib; do codesign --force --sign - --timestamp=none "$lib"; done
codesign --force --sign - --timestamp=none "$C/MacOS/scarletnx"
codesign --force --sign - --timestamp=none "$C/MacOS/Inkypaint"
codesign --force --sign - --timestamp=none "$APP"

# the runtime loads from the app alone (no Homebrew path left), or this says what it looked for
if otool -L "$C/MacOS/scarletnx" | grep -q /opt/homebrew; then die "the runtime still links Homebrew: $(otool -L "$C/MacOS/scarletnx" | grep /opt/homebrew)"; fi
if "$C/MacOS/scarletnx" --help 2>&1 | grep -q "Library not loaded"; then
    "$C/MacOS/scarletnx" --help 2>&1 | grep -A3 "Library not loaded" >&2
    die "the bundled runtime can't load its libraries"
fi

say "done: $APP ($(du -sh "$APP" | cut -f1))"
say "frameworks: $(ls "$FW" | tr '\n' ' ')"
