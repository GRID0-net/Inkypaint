#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the inkypaint contributors
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MODE="${1:-aot}"
case "$MODE" in aot|local-recompile) ;; *) echo "Usage: $0 [aot|local-recompile]" >&2; exit 2 ;; esac
LLVM="${INKY_IOS_LLVM_DIR:-$ROOT/local/ios/llvm/lib/cmake/llvm}"
MVK="${INKY_IOS_MOLTENVK:-$ROOT/local/moltenvk-ios}"
[ -f "$LLVM/LLVMConfig.cmake" ] || {
 echo "Missing iOS LLVM libraries: run INKY_JOBS=2 scripts/apple-llvm.sh ios $ROOT/local/ios/llvm" >&2; exit 1; }
[ -d "$MVK" ] || { echo "Set INKY_IOS_MOLTENVK to the extracted iOS MoltenVK release." >&2; exit 1; }
ARGS=(-DSNX_IOS_AOT_ONLY=OFF)
if [ "$MODE" = aot ]; then
 FW="$ROOT/local/ios/InkypaintGame.framework"
 [ -f "$FW/InkypaintGame" ] || { echo "Run scripts/prepare-ios-game.sh first." >&2; exit 1; }
 ARGS=(-DSNX_IOS_AOT_ONLY=ON "-DSNX_IOS_GAME_FRAMEWORK=$FW")
fi
export CMAKE_BUILD_PARALLEL_LEVEL="${INKY_JOBS:-2}"
if [ "$MODE" = aot ]; then
    export INKY_IOS_ASSETS="${INKY_IOS_ASSETS:-$ROOT/local/ios/recompiled/0100C2500FC20000/0100C2500FC20000_data}"
    [ -d "$INKY_IOS_ASSETS/exefs" ] || { echo "Prepare the game's extracted assets first." >&2; exit 1; }
fi
cd "$ROOT"
BUILD="$ROOT/build/ios-$MODE"
cmake --preset ios -B "$BUILD" "-DLLVM_DIR=$LLVM" "-DCPM_SOURCE_CACHE=$ROOT/.cache/cpm" -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO "${ARGS[@]}"
cmake --build "$BUILD" --parallel "${INKY_JOBS:-2}"
cmake --install "$BUILD" --prefix "$BUILD/stage"
echo "INKY_STAGE: Packaging IPA"
bash scripts/apple-app.sh ios "$BUILD/stage" "$BUILD/package" "$MVK"
# Retain upstream packaging layout and executable name; label the distributable separately.
mv "$BUILD/package/ScarletNX.ipa" "$BUILD/package/inkypaint-ios-$MODE.ipa"
printf 'Unsigned IPA: %s\nSign all nested frameworks and the app for your device before installing.\n' "$BUILD/package/inkypaint-ios-$MODE.ipa"
