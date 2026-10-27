<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the ScarletNX contributors -->
# inkypaint docs!

just want to play? start with the [README](../README.md)! the launcher handles setup from your own game and keys.
these pages are for building, troubleshooting, or digging into how it works.

## Getting started

- [building](BUILD.md): the package scripts and the toolchains underneath them
- [usage](USAGE.md): launcher setup, runtime commands, settings and folders
- [multiplayer](NETWORKING.md): LAN / GRID0 ZeroTier, GRID0+, and the networking underneath them
- [what's next](../ROADMAP-INKYPAINT.md)
- [contributing](../CONTRIBUTING.md)
- [credits and licenses](CREDITS.md)

## Working on it

- [development](DEVELOPMENT.md): rebuilds, diagnostics and profiling
- [testing and reports](TESTING.md): what ran, on what machine, and what happened
- [architecture](ARCHITECTURE.md): the recompiler and runtime
- [CPU differences](DIVERGENCES.md)
- [recompiler library](LIBRARY.md)
- [older layouts and deprecations](DEPRECATIONS.md)

## Runtime and rendering

these are mostly technical references inherited from ScarletNX. some examples describe upstream features or
platforms rather than what inkypaint ships. this fork still only supports Splatoon 3 11.3.0!

- [IPC](IPC.md), [filesystem](FILESYSTEM.md), [memory accounting](MEMORY_ACCOUNTING.md)
- [NRO files](NRO.md) and [ldr:ro](LDR_RO.md)
- [GPU ownership](GPU_OWNERSHIP.md) and [buffers on unified memory](UMA_MEMORY.md)
- [block linear textures and conversions](BLOCKLINEAR.md), [ASTC uploads](ASTC.md)
- [VIC composition](VIC.md), [NVDEC](NVDEC.md)
- [NVN investigation](NVN_HLE.md) and [shader fixes](shader-audit/fixes-001.md)
- [framerate research](FRAMERATE-RESEARCH.md)
- [Apple platform notes](APPLE.md)
