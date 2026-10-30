#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the inkypaint contributors
# LOCAL ONLY: generated framework contains the user's recompiled game code.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
[ $# -eq 1 ] || { echo "Usage: $0 EXTRACTED_GAME_FOLDER" >&2; exit 2; }
RUNTIME="${INKY_RUNTIME:-$ROOT/build/macos/scarletnx}"
OUT="$ROOT/local/ios"
[ -x "$RUNTIME" ] || { echo "Build the macOS runtime first." >&2; exit 1; }
[ -d "$1/exefs" ] || { echo "Use the extracted, updated game folder containing exefs/." >&2; exit 1; }
mkdir -p "$OUT/recompiled"
"$RUNTIME" -r "$1" --target ios-aot --outdir "$OUT/recompiled" --jobs "${INKY_JOBS:-2}"
CODE="$OUT/recompiled/0100C2500FC20000/0100C2500FC20000_code.dylib"
[ -f "$CODE" ] || { echo "Expected the supported Splatoon 3 title ID, not another game." >&2; exit 1; }
xcrun vtool -show-build "$CODE" | rg -q 'platform IOS$'
FW="$OUT/InkypaintGame.framework"
mkdir -p "$FW"
cp "$CODE" "$FW/InkypaintGame"
chmod 755 "$FW/InkypaintGame"
python3 - "$FW/Info.plist" <<'PY'
import plistlib,sys
with open(sys.argv[1],'wb') as f:
 plistlib.dump(dict(CFBundleIdentifier='org.inkypaint.game',CFBundleName='InkypaintGame',
   CFBundleExecutable='InkypaintGame',CFBundlePackageType='FMWK',CFBundleVersion='1',
   CFBundleShortVersionString='0.1.2',MinimumOSVersion='16.0',CFBundleSupportedPlatforms=['iPhoneOS']),f)
PY
printf 'Prepared locally: %s\nGame assets will be bundled by scripts/build-ios.sh.\n' "$FW"
