#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the inkypaint contributors
# Local personal package: includes recompiled game code and assets, never publish.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CHECK=0
if [ "${1:-}" = --check ]; then CHECK=1; shift; fi
[ $# -le 1 ] || { echo "Usage: $0 [--check] [EXTRACTED_UPDATED_GAME_FOLDER]" >&2; exit 2; }
GAME="${1:-$ROOT/local/data/work/splatoon3}"
export INKY_JOBS="${INKY_JOBS:-8}"
[[ "$INKY_JOBS" =~ ^[1-9][0-9]*$ ]] || { echo "INKY_JOBS must be a positive integer." >&2; exit 2; }
export CMAKE_BUILD_PARALLEL_LEVEL="$INKY_JOBS"
VERSION="${LLVM_VERSION:-23.1.4}"
LLVM_PREFIX="$ROOT/local/ios/llvm"
export INKY_IOS_LLVM_DIR="${INKY_IOS_LLVM_DIR:-$LLVM_PREFIX/lib/cmake/llvm}"
export INKY_IOS_MOLTENVK="${INKY_IOS_MOLTENVK:-$ROOT/local/moltenvk-ios}"
SOURCE="${INKY_IOS_LLVM_SOURCE:-$ROOT/local/ios/llvm-project-$VERSION.src.tar.xz}"
HOST_BUILD="$ROOT/build/ios-host"
export INKY_RUNTIME="$HOST_BUILD/scarletnx"
for tool in cmake ninja python3 xcrun tar rg; do
    command -v "$tool" >/dev/null || { echo "Missing build tool: $tool" >&2; exit 1; }
done
xcrun --sdk iphoneos --show-sdk-path >/dev/null
GAME="$(python3 "$ROOT/scripts/resolve-ios-install.py" "$GAME" --path-only)"
if [ "${INKY_IOS_INCLUDE_DLC:-0}" = 1 ]; then
    export INKY_IOS_DLC_SOURCE="$GAME"
    python3 "$ROOT/scripts/stage-ios-assets.py" "$GAME" --check-dlc
    echo "Optional installed DLC will be included (gameplay untested)."
else
    unset INKY_IOS_DLC_SOURCE
fi
echo "Using existing game files: $GAME"
MVK=$(find "$INKY_IOS_MOLTENVK" -path '*/dynamic/*ios-arm64/MoltenVK.framework/MoltenVK' -type f -print -quit)
[ -n "$MVK" ] || { echo "Missing iPhoneOS MoltenVK framework under $INKY_IOS_MOLTENVK" >&2; exit 1; }
if [ ! -f "$INKY_IOS_LLVM_DIR/LLVMConfig.cmake" ]; then
    [ -f "$SOURCE" ] || [ -d "$SOURCE/llvm" ] || { echo "Missing iOS LLVM source: $SOURCE" >&2; exit 1; }
    echo "iOS LLVM will be built/resumed from $SOURCE"
fi
NATIVE_LLVM="${SNX_LLVM_CONFIG:-}"
if [ -z "$NATIVE_LLVM" ] && command -v brew >/dev/null; then
    NATIVE_LLVM="$(brew --prefix llvm)/bin/llvm-config"
fi
[ -x "$NATIVE_LLVM" ] || { echo "Set SNX_LLVM_CONFIG to the Mac's llvm-config executable." >&2; exit 1; }
echo "Ready: $INKY_JOBS jobs, signed AOT game framework + bundled game assets."
[ "$CHECK" = 0 ] || exit 0
cd "$ROOT"
mkdir -p logs
LOG="$ROOT/logs/ios-all-$(date +%Y%m%d-%H%M%S).log"
exec > >(tee "$LOG") 2>&1
echo "Build log: $LOG"
if [ ! -f "$INKY_IOS_LLVM_DIR/LLVMConfig.cmake" ]; then
    # An alternate prefix must be built by its owner; don't populate a different
    # default while later configuring against the alternate path.
    [ "$INKY_IOS_LLVM_DIR" = "$LLVM_PREFIX/lib/cmake/llvm" ] || {
        echo "The selected INKY_IOS_LLVM_DIR is not installed."; exit 1; }
    echo "INKY_STAGE: Building iOS LLVM"
    LLVM_VERSION="$VERSION" bash scripts/apple-llvm.sh ios "$LLVM_PREFIX" "$SOURCE"
fi
echo "INKY_STAGE: Building the Mac game compiler"
cmake --preset macos -B "$HOST_BUILD" -DSNX_BUILD_GUI=OFF -DSNX_DEPLOY=OFF -DBUILD_TESTING=OFF \
    "-DSNX_LLVM_CONFIG=$NATIVE_LLVM" "-DCPM_SOURCE_CACHE=$ROOT/.cache/cpm"
cmake --build "$HOST_BUILD" --target scarletnx --parallel "$INKY_JOBS"
echo "INKY_STAGE: Preparing iOS game code"
bash scripts/prepare-ios-game.sh "$GAME"
echo "INKY_STAGE: Building the iPhone app"
bash scripts/build-ios.sh aot
echo "Ready to sign: $ROOT/build/ios-aot/package/inkypaint-ios-aot.ipa"
