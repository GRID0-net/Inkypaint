#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
# The macOS app or the iOS one (docs/APPLE.md) from what `cmake --install` put in PREFIX. Run on a Mac with Xcode.
#
#   scripts/apple-app.sh macos PREFIX OUT [MOLTENVK]   OUT/ScarletNX.app, signed ad hoc
#   scripts/apple-app.sh ios PREFIX OUT [MOLTENVK]     OUT/ScarletNX.ipa, unsigned, for a sideloading tool to sign
#
# MOLTENVK is a MoltenVK release's MoltenVK-macos.tar or MoltenVK-ios.tar, extracted: Ryujinx's (V380-Ori/
# Ryujinx.MoltenVK, which .github/workflows/build.yml pins), whose patches emulators' pipelines need. Its dynamic
# library goes in the app's Frameworks, where the runtime has SDL load it (backend/sdl3.c). Without it the macOS app
# uses the system's Vulkan loader (Homebrew's molten-vk), and the iOS app has none.
set -eu
[ $# -ge 3 ] || { sed -n '4,13p' "$0"; exit 2; }
kind=$1 prefix=$2 out=$3 moltenvk=${4:-}
src=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$out"
out=$(cd "$out" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
# the file or folder in the MoltenVK release that ends with $1, and its license beside the app's other ones ($2)
moltenvk_part() {
    found=$(find "$moltenvk" -path "*/dynamic/*$1" | head -1)
    [ -n "$found" ] || { echo "no $1 in $moltenvk" >&2; exit 1; }
    license=$(find "$moltenvk" -name LICENSE | head -1)
    if [ -n "$license" ]; then
        mkdir -p "$2/licenses/scarletnx/MoltenVK"
        cp "$license" "$2/licenses/scarletnx/MoltenVK/LICENSE"
    fi
}
case $kind in
macos)
    app=$out/ScarletNX.app
    rm -rf "$app"
    mkdir -p "$app/Contents/Resources"
    # the programs in MacOS/, beside each other as installed; share/ beside it, where they look (../share/scarletnx)
    cp -R "$prefix/bin" "$app/Contents/MacOS"
    cp -R "$prefix/share" "$app/Contents/share"
    cp "$src/dist/apple/Info-macOS.plist" "$app/Contents/Info.plist"
    set=$work/ScarletNX.iconset
    mkdir "$set"
    for size in 16 32 128 256; do
        sips -z $size $size "$src/dist/icons/hicolor/256x256/apps/wimsy.png" --out "$set/icon_${size}x${size}.png" >/dev/null
    done
    iconutil -c icns "$set" -o "$app/Contents/Resources/ScarletNX.icns"
    if [ -n "$moltenvk" ]; then
        moltenvk_part dylib/macOS/libMoltenVK.dylib "$app/Contents/share"
        mkdir -p "$app/Contents/Frameworks"
        cp "$found" "$app/Contents/Frameworks/"
    fi
    # Apple silicon runs no unsigned code: an ad hoc signature, for the Mac it is copied to
    codesign --force --deep --sign - "$app"
    echo "$app"
    ;;
ios)
    [ -d "$prefix/scarletnx.app" ] || { echo "no scarletnx.app in $prefix: install the ios preset's build there" >&2; exit 1; }
    app=$work/Payload/ScarletNX.app
    mkdir -p "$work/Payload"
    cp -R "$prefix/scarletnx.app" "$app"
    cp "$src/dist/icons/hicolor/256x256/apps/wimsy.png" "$app/AppIcon.png"
    # the licenses go with the binaries, as everywhere (CMakeLists.txt)
    cp -R "$prefix/share" "$app/share"
    if [ -n "$moltenvk" ]; then
        moltenvk_part ios-arm64/MoltenVK.framework "$app/share"
        mkdir -p "$app/Frameworks"
        cp -R "$found" "$app/Frameworks/"
    fi
    if [ -n "${INKY_IOS_ASSETS:-}" ]; then
        python3 "$src/scripts/stage-ios-assets.py" "$INKY_IOS_ASSETS" "$app/GameAssets"
    fi
    plutil -lint "$app/Info.plist" >/dev/null
    rm -f "$out/ScarletNX.ipa"
    (cd "$work" && zip -qry "$out/ScarletNX.ipa" Payload)
    echo "$out/ScarletNX.ipa"
    ;;
*)
    echo "macos or ios, not $kind" >&2
    exit 2
    ;;
esac
