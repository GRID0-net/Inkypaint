#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the inkypaint contributors
#
# inkypaint on macOS (Apple silicon): set up, build, prepare Splatoon 3 and run it, each step's output kept in logs/.
#
#   ./inkypaint.sh doctor     what this Mac has (chip, memory, Xcode, Homebrew, disk)
#   ./inkypaint.sh setup      Homebrew packages and Ryujinx's MoltenVK (asks before installing anything)
#   ./inkypaint.sh build      configure and build the runtime and driver into build/macos
#   ./inkypaint.sh prepare    extract the base game with the 11.3.0 update, then recompile it
#   ./inkypaint.sh run [secs] [--KEY=VALUE...]  run the game (default: until you close it), screenshot at the end
#   ./inkypaint.sh perf [secs] [--KEY=VALUE...] a measured run: frame rate, GPU time, memory, threads, call stacks
#   ./inkypaint.sh diag [secs]  three measured runs that narrow down the rendering bugs (about 6 minutes)
#   ./inkypaint.sh gpudiag [secs] three measured runs that split the GPU's time: fast math, half resolution
#   ./inkypaint.sh shaderprof [secs] a measured run with every draw timed alone: the costliest draws by their
#                             programs, whose SPIR-V lands in local/spv (the game's shaders: they stay here)
#   ./inkypaint.sh texdiag [secs] two measured runs for the missing skin and eye colours: indexed textures as now
#                             (each reads its table's first texture), then through texture arrays
#   ./inkypaint.sh rtdump [frame] one frame's rendering passes as pictures (frame 1500 by default, about 25 s in),
#                             into local/rtdump-<time> (the game's frame: it stays here)
#   ./inkypaint.sh capture    the game with Metal's frame capture on, for Xcode's GPU profiler
#   ./inkypaint.sh all        doctor, setup, build, prepare, run
#
# Everything the game is made of stays in this folder: local/ (extraction, the recompiled game, its saves and logs)
# and logs/ are ignored by git, as are "the rom" and "decryption keys".
#
# Tunables (environment): INKY_JOBS (parallel compiles, default from memory), INKY_BUILD_JOBS (build parallelism),
# INKY_OPT (recompile optimization 0-3, default 2), INKY_ROM_DIR, INKY_KEYS_DIR.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROM_DIR="${INKY_ROM_DIR:-$ROOT/the rom}"
KEYS_DIR="${INKY_KEYS_DIR:-$ROOT/decryption keys}"
LOCAL="$ROOT/local"
DATA="$LOCAL/data"                     # scarletnx's data folder (--data): recompiled/, work/, units cache
WORK="$DATA/work/splatoon3"            # the extraction
GAME_ID=0100C2500FC20000
GAME_DIR="$DATA/recompiled/$GAME_ID"
BUILD="$ROOT/build/macos"
MVK_RELEASE=v1.4.1-ryujinx
MVK_SHA256=ea1c633808d0a61e86ee35af4bdc280f5f07f95633b95935f3e695dc5226e640
MVK_DIR="$LOCAL/moltenvk"
LOGS="$ROOT/logs"
mkdir -p "$LOGS" "$LOCAL"

stamp() { date +%Y%m%d-%H%M%S; }
say() { printf '\033[1;35m[inkypaint]\033[0m %s\n' "$*"; }
die() { printf '\033[1;31m[inkypaint] %s\033[0m\n' "$*" >&2; exit 1; }

# run a step with its output on screen and in logs/<step>-<time>.log, logs/<step>-latest.log pointing at it
logged() {
    local step=$1; shift
    local log="$LOGS/$step-$(stamp).log"
    say "$step: logging to logs/$(basename "$log")"
    ln -sf "$(basename "$log")" "$LOGS/$step-latest.log"
    { echo "# inkypaint $step, $(date), commit $(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo none)"; } > "$log"
    ( "$@" ) 2>&1 | tee -a "$log"
    local rc=${PIPESTATUS[0]}
    echo "# exit $rc" >> "$log"
    [ "$rc" -eq 0 ] || die "$step failed (exit $rc). Send me logs/$(basename "$log") or just tell me it failed: I can read logs/ myself."
    return 0
}

brew_prefix() { command -v brew >/dev/null && brew --prefix || echo /opt/homebrew; }
mem_gb() { echo $(( $(sysctl -n hw.memsize) / 1073741824 )); }
default_jobs() { # each LLVM compile of a big code unit can take 1-2 GB: keep two free for macOS
    local m; m=$(mem_gb)
    if [ "$m" -le 8 ]; then echo 2; elif [ "$m" -le 16 ]; then echo 4; else sysctl -n hw.perflevel0.physicalcpu 2>/dev/null || echo 6; fi
}

find_one() { # the file in dir $1 whose name matches the glob $2
    local f
    for f in "$1"/$2; do [ -f "$f" ] && { echo "$f"; return 0; }; done
    return 1
}

do_doctor() {
    echo "== machine"; sw_vers; uname -m; sysctl -n machdep.cpu.brand_string
    echo "memory: $(mem_gb) GB, cores: $(sysctl -n hw.ncpu) ($(sysctl -n hw.perflevel0.physicalcpu 2>/dev/null) performance)"
    echo "== disk"; df -h "$ROOT" | tail -1
    echo "== Xcode"; xcode-select -p 2>&1; xcrun --sdk macosx --show-sdk-path 2>&1; clang --version 2>&1 | head -1
    echo "== Homebrew"; command -v brew && brew --version | head -1 || echo "no Homebrew (https://brew.sh)"
    for t in cmake ninja pkg-config python3 git; do printf '%-12s %s\n' "$t" "$(command -v $t || echo MISSING)"; done
    local llvm; llvm="$(brew_prefix)/opt/llvm/bin/llvm-config"
    [ -x "$llvm" ] && echo "llvm: $("$llvm" --version)" || echo "llvm: MISSING (brew install llvm)"
    [ -f "$MVK_DIR/.ok" ] && echo "MoltenVK: $MVK_RELEASE" || echo "MoltenVK: not downloaded (./inkypaint.sh setup)"
    echo "== game files"
    ls -l "$ROM_DIR" 2>&1; ls -l "$KEYS_DIR" 2>&1 | sed 's/ [^ ]*keys$/ (keys present)/'
    echo "== inkypaint state"
    [ -x "$BUILD/scarletnx" ] && echo "built: $BUILD/scarletnx" || echo "not built"
    [ -f "$WORK/exefs/main" ] && echo "extracted: $WORK" || echo "not extracted"
    ls "$GAME_DIR"/*_code.* 2>/dev/null && echo "recompiled: $GAME_DIR" || echo "not recompiled"
}

do_setup() {
    xcode-select -p >/dev/null 2>&1 || die "Install Apple's command line tools first: xcode-select --install"
    command -v brew >/dev/null || die "Install Homebrew first (https://brew.sh), then run this again"
    local pkgs="cmake ninja pkg-config llvm"
    local missing=""
    for p in $pkgs; do brew list --formula "$p" >/dev/null 2>&1 || missing="$missing $p"; done
    if [ -n "$missing" ]; then
        say "Homebrew will install:$missing"
        brew install $missing || die "brew install failed"
    else
        say "Homebrew packages: all there"
    fi
    if [ ! -f "$MVK_DIR/.ok" ]; then
        say "Downloading Ryujinx's MoltenVK $MVK_RELEASE (the Vulkan driver ScarletNX is tested with on macOS)"
        mkdir -p "$MVK_DIR"
        curl -fL -o "$MVK_DIR/MoltenVK-macos.tar" \
            "https://github.com/V380-Ori/Ryujinx.MoltenVK/releases/download/$MVK_RELEASE/MoltenVK-macos.tar" || die "download failed"
        echo "$MVK_SHA256  $MVK_DIR/MoltenVK-macos.tar" | shasum -a 256 -c || die "MoltenVK checksum mismatch"
        tar -xf "$MVK_DIR/MoltenVK-macos.tar" -C "$MVK_DIR" && touch "$MVK_DIR/.ok"
    fi
    say "MoltenVK: $(mvk_dylib)"
}

mvk_dylib() { find "$MVK_DIR" -path '*/dynamic/*' -name 'libMoltenVK.dylib' | head -1; }

do_build() {
    local llvm; llvm="$(brew_prefix)/opt/llvm/bin/llvm-config"
    [ -x "$llvm" ] || die "no Homebrew LLVM: ./inkypaint.sh setup"
    local jobs=${INKY_BUILD_JOBS:-$(( $(mem_gb) <= 8 ? 4 : $(sysctl -n hw.ncpu) ))}
    # Xcode's (or the command line tools') clang builds the runtime; Homebrew's LLVM is only the recompiler's library.
    # No menu (SNX_BUILD_GUI=OFF): it needs Swift and the full Xcode, and inkypaint is driven from here for now
    cmake --preset macos -B "$BUILD" \
        -DSNX_LLVM_CONFIG="$llvm" \
        -DSNX_BUILD_GUI=OFF \
        -DBUILD_TESTING=OFF \
        -DSNX_RECOMPILED_DIR="$DATA/recompiled" || return 1
    cmake --build "$BUILD" --parallel "$jobs"
}

do_prepare() {
    local snx="$BUILD/scarletnx"
    [ -x "$snx" ] || die "not built yet: ./inkypaint.sh build"
    local base update keys tkeys
    base=$(find_one "$ROM_DIR" '*0100C2500FC20000*BASE*.nsp') || base=$(find_one "$ROM_DIR" '*0100C2500FC20000*.nsp') \
        || die "no Splatoon 3 base NSP (title ID 0100C2500FC20000) in $ROM_DIR"
    update=$(find_one "$ROM_DIR" '*0100C2500FC20800*.nsp') || die "no Splatoon 3 update NSP (0100C2500FC20800) in $ROM_DIR"
    keys="$KEYS_DIR/prod.keys"; tkeys="$KEYS_DIR/title.keys"
    [ -f "$keys" ] || die "no prod.keys in $KEYS_DIR"
    mkdir -p "$DATA"
    if [ -f "$WORK/exefs/main" ] && [ -d "$WORK/romfs" ]; then
        say "already extracted: $WORK (delete it to extract again)"
    else
        say "extracting (about 10 GB is written into local/)"
        "$snx" --data "$DATA" -x "$base" --update "$update" --keys "$keys" --title-keys "$tkeys" --out "$WORK" || return 1
    fi
    local jobs=${INKY_JOBS:-$(default_jobs)}
    say "recompiling with $jobs parallel compiles at -O${INKY_OPT:-2} ($(mem_gb) GB of memory). Interrupted? Run prepare again: finished units are kept"
    "$snx" --data "$DATA" -r "$WORK" --outdir "$DATA/recompiled" --jobs "$jobs" -O "${INKY_OPT:-2}"
}

do_run() {
    local secs=${1:-0}; shift || true   # the rest: --KEY=VALUE settings for this run
    [ -d "$GAME_DIR" ] || die "not prepared yet: ./inkypaint.sh prepare"
    local exe="$GAME_DIR/$GAME_ID"
    [ -x "$exe" ] || die "no runtime copy at $exe (the build deploys it: ./inkypaint.sh build)"
    local mvk; mvk=$(mvk_dylib)
    [ -n "$mvk" ] || die "no MoltenVK: ./inkypaint.sh setup"
    local shots="$LOGS/shots"; mkdir -p "$shots"
    local args=(--shot="$shots/run-$(stamp)")
    [ "$secs" -gt 0 ] && args+=(--seconds="$secs")
    [ $# -gt 0 ] && args+=("$@")
    local how="until you close the window"; [ "$secs" -gt 0 ] && how="for $secs seconds"
    say "running Splatoon 3 with MoltenVK $MVK_RELEASE, $how"
    ( cd "$GAME_DIR" && SDL_VULKAN_LIBRARY="$mvk" MVK_CONFIG_LOG_LEVEL=2 "./$GAME_ID" "${args[@]}" )
    local rc=$?
    [ -f "$GAME_DIR/$GAME_ID.log" ] && cp "$GAME_DIR/$GAME_ID.log" "$LOGS/game-$(stamp).log" && say "game log copied to logs/"
    return $rc
}

# A measured run: the game for $1 seconds (default 90) with the GPU's time per pass logged, while the Mac's memory
# (vm_stat: swapping), the game's threads' CPU (ps -M) and a 10-second call-stack sample (sample) are written beside
# its log in logs/perf-<time>/. Extra --KEY=VALUE settings go to the game, to compare one change against another
do_perf() {
    local secs=${1:-70}; shift || true
    [ -x "$GAME_DIR/$GAME_ID" ] || die "not prepared yet: ./inkypaint.sh prepare"
    local out="$LOGS/perf-$(stamp)"; mkdir -p "$out"
    local mvk; mvk=$(mvk_dylib)
    say "measured run for $secs s into logs/$(basename "$out") (settings: ${*:-none}; $(env | grep '^MVK_' | tr '\n' ' '))"
    { echo "settings: ${*:-none}"; env | grep '^MVK_' || echo "MVK: defaults"; env | grep '^SNX_'; } > "$out/settings.txt"
    ( cd "$GAME_DIR" && SDL_VULKAN_LIBRARY="$mvk" MVK_CONFIG_LOG_LEVEL="${MVK_CONFIG_LOG_LEVEL:-2}" \
        "./$GAME_ID" --seconds="$secs" --vk_gpu_profile=1 --shot="$out/last" "$@" ) > "$out/console.txt" 2>&1 &
    local game=$!
    sleep 3
    local pid; pid=$(pgrep -n -x "$GAME_ID" || echo "$game")
    echo "pid $pid" >> "$out/settings.txt"
    vm_stat 2 > "$out/vm_stat.txt" 2>&1 &
    local vms=$!
    (
        local t=0
        while kill -0 "$pid" 2>/dev/null; do
            { echo "== t=$t s"; ps -M -p "$pid" -o pid,%cpu,rss 2>/dev/null; } >> "$out/threads.txt"
            memory_pressure 2>/dev/null | tail -1 >> "$out/pressure.txt"
            sleep 5; t=$((t + 5))
        done
    ) &
    local poll=$!
    # the call stacks once the shaders of earlier runs are loaded and the game is past its start
    local at=$(( secs > 60 ? secs - 20 : 30 ))
    sleep "$at"
    kill -0 "$pid" 2>/dev/null && sample "$pid" 10 -mayDie -file "$out/sample.txt" > /dev/null 2>&1
    wait "$game"; local rc=$?
    kill "$vms" "$poll" 2>/dev/null
    cp "$GAME_DIR/$GAME_ID.log" "$out/game.log" 2>/dev/null
    cp "$GAME_DIR/threads.txt" "$out/game-threads.txt" 2>/dev/null
    [ -f "$out/last.ppm" ] && sips -s format png "$out/last.ppm" --out "$out/last.png" > /dev/null 2>&1 && rm -f "$out/last.ppm"
    grep "perf:" "$out/game.log" | tail -20
    say "done: logs/$(basename "$out")"
    return $rc
}

# The rendering bugs (invisible limbs, missing eye colour): the same scene three ways, each a measured run whose
# last frame and log land in its own logs/perf-<time>/, to tell a macro, a MoltenVK setting or the translation apart
do_diag() {
    local secs=${1:-70}
    say "diag 1/3: GPU macros checked against the interpreter (mme_verify), MoltenVK logging its setup"
    MVK_CONFIG_LOG_LEVEL=3 do_perf "$secs" --mme_verify=1 || return 1
    say "diag 2/3: every GPU macro interpreted (no native or compiled macros)"
    do_perf "$secs" --mme_hle=0 --mme_jit=0 || return 1
    say "diag 3/3: MoltenVK without Metal argument buffers"
    MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS=0 do_perf "$secs" || return 1
}

# Where the GPU's time goes once the CPU keeps up: the same scene three ways, each a measured run. Fast math shows
# whether Metal's precise math (MoltenVK turns fast math off for any shader that asks to preserve infinities and NaNs,
# which every translated one does) is the cost; half resolution whether the pixels are
do_gpudiag() {
    local secs=${1:-60}
    say "gpudiag 1/3: as it is"
    do_perf "$secs" || return 1
    say "gpudiag 2/3: Metal fast math"
    MVK_CONFIG_FAST_MATH_ENABLED=1 do_perf "$secs" || return 1
    say "gpudiag 3/3: half the 3D resolution"
    do_perf "$secs" --resolution=0.5 || return 1
    for d in $(ls -d "$LOGS"/perf-*/ | tail -3); do
        echo "== $(basename "$d"): $(head -1 "$d/settings.txt") $(grep MVK_ "$d/settings.txt" | tr '\n' ' ')"
        grep "GPU over" "$d/game.log" | tail -1
        grep "perf:" "$d/game.log" | tail -3
    done
}

# Which draws the GPU's time goes to: each draw ends its pass so it is timed alone (the frame rate drops, as every
# draw then stores and loads its targets), and the log lists the costliest draws a frame with their vertex and
# fragment programs. Each program's SPIR-V is written to local/spv/<hash>_<stage>_<words>.spv to read what it became
do_shaderprof() {
    local secs=${1:-60}; shift || true
    mkdir -p "$LOCAL/spv"
    SNX_DRAW_SPLIT=1 SNX_SPV_DUMP="$LOCAL/spv" do_perf "$secs" "$@" || return 1
    local d; d=$(ls -td "$LOGS"/perf-*/ | head -1)
    grep "GPU   draw" "$d/game.log" | tail -40
}

# The player model's missing skin and eye colours: the game's indexed texture tables (a texture picked by a register
# in the shader) read their table's first texture unless the shaders get texture arrays, which MoltenVK's reported
# limits turn off. The same scene twice: as it is, then with the arrays forced on and MoltenVK logging what it does
# with them (its compile errors show the Metal it made), each translated program's SPIR-V in local/spv-arrays. Each
# run translates and makes the pipelines again (the arrays are in the shader cache's key), so they load slowly
do_texdiag() {
    local secs=${1:-60}
    say "texdiag 1/2: indexed textures as now"
    do_perf "$secs" || return 1
    say "texdiag 2/2: indexed textures through texture arrays"
    mkdir -p "$LOCAL/spv-arrays"
    SNX_FORCE_TEXTURE_ARRAYS=1 SNX_SPV_DUMP="$LOCAL/spv-arrays" MVK_CONFIG_LOG_LEVEL=3 do_perf "$secs" || return 1
    for d in $(ls -d "$LOGS"/perf-*/ | tail -2); do
        echo "== $(basename "$d"): $(grep -o 'SNX_FORCE[^ ]*' "$d/settings.txt")"
        grep "index texture tables\|indexed textures" "$d/game.log" | tail -2
        echo "Metal compile errors: $(grep -c 'Shader library compile failed' "$d/console.txt")"
        grep "perf:" "$d/game.log" | tail -2
    done
}

# Where a frame goes wrong: every pass of one frame, each color target as a picture at the pass's end, with
# local/rtdump-<time>/index.txt naming each pass's targets and programs. The run ends a while after that frame
do_rtdump() {
    local at=${1:-1500}
    local out="$LOCAL/rtdump-$(stamp)"; mkdir -p "$out"
    local secs=$(( at / 60 + 20 ))
    say "frame $at's passes into local/$(basename "$out"), a $secs s run"
    SNX_RT_DUMP="$out" SNX_RT_DUMP_AT="$at" do_run "$secs" || return 1
    local n=0
    for f in "$out"/*.ppm; do
        [ -f "$f" ] || continue
        sips -s format png "$f" --out "${f%.ppm}.png" > /dev/null 2>&1 && rm -f "$f" && n=$((n + 1))
    done
    say "$n pictures, $(wc -l < "$out/index.txt" 2>/dev/null || echo 0) passes listed in local/$(basename "$out")/index.txt"
}

# For Xcode: the game with Metal's capture layer on and no time limit. In Xcode, Debug > Attach to Process picks the
# game (its name is its title ID), then the debug bar's Metal button captures a frame: its Performance view times
# every draw and the shader profiler each line of the translated MSL. Instruments' Metal System Trace template
# attaches the same way, without the capture
do_capture() {
    say "attach Xcode (Debug > Attach to Process > 0100C2500FC20000), then capture a frame with the Metal button"
    METAL_CAPTURE_ENABLED=1 do_run 0 "$@"
}

cmd=${1:-help}; shift || true
case "$cmd" in
    doctor) logged doctor do_doctor ;;
    setup) logged setup do_setup ;;
    build) logged build do_build ;;
    prepare) logged prepare do_prepare ;;
    run) logged run do_run "$@" ;;
    perf) logged perf do_perf "$@" ;;
    diag) logged diag do_diag "$@" ;;
    gpudiag) logged gpudiag do_gpudiag "$@" ;;
    shaderprof) logged shaderprof do_shaderprof "$@" ;;
    texdiag) logged texdiag do_texdiag "$@" ;;
    rtdump) logged rtdump do_rtdump "$@" ;;
    capture) logged capture do_capture "$@" ;;
    all) logged doctor do_doctor; logged setup do_setup; logged build do_build; logged prepare do_prepare; logged run do_run "$@" ;;
    *) sed -n '4,23p' "$0" | sed 's/^# \{0,1\}//' ;;
esac
