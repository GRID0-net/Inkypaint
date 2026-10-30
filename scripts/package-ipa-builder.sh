#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the inkypaint contributors
# Build the Mac helper only. Does NOT start the game or iOS build pipeline.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/ipa-builder"
APP="$OUT/Inkypaint IPA Builder.app"
SCRATCH="$ROOT/build/ipa-builder-swift"
xcrun swift build --package-path "$ROOT/src/ipa-builder" --scratch-path "$SCRATCH" -c release --jobs 2
BIN="$(xcrun swift build --package-path "$ROOT/src/ipa-builder" --scratch-path "$SCRATCH" -c release --show-bin-path)"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN/InkypaintIPABuilder" "$APP/Contents/MacOS/InkypaintIPABuilder"
cp "$ROOT/scripts/run-ios-job.py" "$APP/Contents/Resources/"
cp "$ROOT/dist/launcher/splat-burst.png" "$APP/Contents/Resources/"
printf '%s\n' "$ROOT" > "$APP/Contents/Resources/project.txt"
python3 - "$APP/Contents/Info.plist" <<'PY'
import plistlib,sys
with open(sys.argv[1],'wb') as f:
 plistlib.dump(dict(CFBundleIdentifier='io.github.inkypaint.ipabuilder',CFBundleName='Inkypaint IPA Builder',
   CFBundleDisplayName='Inkypaint IPA Builder',CFBundleExecutable='InkypaintIPABuilder',CFBundlePackageType='APPL',
   CFBundleVersion='1',CFBundleShortVersionString='0.1',LSMinimumSystemVersion='14.0',
   NSHighResolutionCapable=True,NSPrincipalClass='NSApplication'),f)
PY
codesign --force --sign - "$APP"
ditto -c -k --sequesterRsrc --keepParent "$APP" "$OUT/Inkypaint-IPA-Builder-macos.zip"
printf 'Mac helper: %s\nNo iOS build has been started.\n' "$APP"
