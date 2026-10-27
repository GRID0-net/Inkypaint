#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the inkypaint contributors
#
# What scripts/package-linux.sh builds with, on Ubuntu 22.04 (the release's container, as root): the development
# headers SDL3's backends build against, NASM, make and perl, patchelf, Python, clang and lld from apt.llvm.org, and a
# CMake and Ninja newer than 22.04's (in /opt/inkypaint-tools, which this puts first on PATH through $GITHUB_PATH when
# it runs in GitHub Actions). Run it in a container or a throwaway machine: it installs system packages.
#   scripts/linux-build-deps.sh [CLANG_VERSION]   (default 23)
set -euo pipefail
clang=${1:-23}
export DEBIAN_FRONTEND=noninteractive
retry() { local n; for n in 1 2 3; do "$@" && return 0; sleep 10; done; return 1; }
retry apt-get update
retry apt-get install -y --no-install-recommends \
    ca-certificates curl wget gnupg lsb-release software-properties-common git xz-utils file \
    python3 python3-venv python3-pip make perl patch pkg-config nasm patchelf \
    libstdc++-12-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev \
    libxtst-dev libxkbcommon-dev libxkbcommon-x11-dev libwayland-dev wayland-protocols libdecor-0-dev libegl1-mesa-dev \
    libgl1-mesa-dev libgles2-mesa-dev libdrm-dev libgbm-dev libasound2-dev libpulse-dev libpipewire-0.3-dev \
    libdbus-1-dev libudev-dev libibus-1.0-dev libfontconfig1 libfreetype6 libxcb1 libxcb-cursor0 libxcb-icccm4 \
    libxcb-image0 libxcb-keysyms1 libxcb-render-util0 libxcb-util1 libxcb-xinerama0 libxcb-randr0 libxcb-shape0 \
    libxcb-xkb1 libxcb-sync1 libxcb-xfixes0 libxcb-shm0 libgl1 libegl1 libopengl0 libglib2.0-0 libdbus-1-3 \
    fonts-dejavu-core
if ! command -v "clang-$clang" > /dev/null; then
    curl -fsSL https://apt.llvm.org/llvm.sh -o /tmp/llvm.sh
    chmod +x /tmp/llvm.sh
    retry /tmp/llvm.sh "$clang"
fi
command -v "ld.lld-$clang" > /dev/null || retry apt-get install -y --no-install-recommends "lld-$clang"
python3 -m venv /opt/inkypaint-tools
/opt/inkypaint-tools/bin/pip install -q "cmake>=3.28" ninja
if [ -n "${GITHUB_PATH:-}" ]; then echo /opt/inkypaint-tools/bin >> "$GITHUB_PATH"; fi
echo "linux-build-deps: clang-$clang, $(/opt/inkypaint-tools/bin/cmake --version | head -1), ninja $(/opt/inkypaint-tools/bin/ninja --version)"
