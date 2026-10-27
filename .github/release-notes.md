inkypaint @VERSION@: Splatoon 3 (11.3.0), recompiled to run natively on your PC or Mac, with online play on GRID0+.

**You need your own copy of the game and your own console's keys.** None of the game is in these downloads: the
launcher turns your copy into the app on your machine (about 13 GB of disk, from half an hour to an hour once).

You need:

- Splatoon 3's base game NSP (`0100C2500FC20000`)
- Its 11.3.0 update NSP (`0100C2500FC20800`, v2752512)
- `prod.keys` dumped from your console (`title.keys` is optional)

## Downloads

| System | File | |
| --- | --- | --- |
| Windows 10/11 x86_64 | `inkypaint-@VERSION@-windows-x86_64-setup.exe` | installer |
| | `inkypaint-@VERSION@-windows-x86_64.zip` | portable: unzip, run `inkypaint.exe` |
| macOS 26 on Apple silicon | `inkypaint-@VERSION@-macos-arm64.zip` | unzip, move `inkypaint.app` to Applications |
| Linux x86_64 (glibc 2.35+, e.g. Ubuntu 22.04, Fedora 36, Debian 12 or newer) | `inkypaint-@VERSION@-linux-x86_64.tar.gz` | unpack, run `./inkypaint` |

`SHA256SUMS` lists every file's checksum.

## Getting started

1. Open inkypaint. The **Game Files** page asks for the two NSPs and your keys (or a folder that holds them).
2. It asks whether you want to play online. Sign in to GRID0+ there and its patches are compiled in on the first go.
   No account yet? Use `/register` with the GRID0+ bot on the GRID0 Discord server.
3. Press **Prepare Splatoon 3** and leave it running. When it's done, press **Play**.

Mods: the **Mods** page adds RomFS mods (folders or archives) that apply the next time the game starts, no recompile
needed. Mods that change the game's code (exefs) need one, which the page offers.

## Notes for each system

- **Windows**: SmartScreen may warn that the installer is from an unknown publisher (it isn't signed): choose
  *More info*, then *Run anyway*. You need a GPU with Vulkan 1.3.
- **macOS**: the app isn't notarized. The first time, open it, then go to *System Settings > Privacy & Security* and
  choose *Open Anyway* (or run `xattr -dr com.apple.quarantine /Applications/inkypaint.app`).
- **Linux**: you need your GPU's Vulkan driver (Mesa's RADV or ANV, or NVIDIA's) and the Vulkan loader
  (`libvulkan1` / `vulkan-icd-loader`). The launcher uses the system's OpenGL dispatch library: if it says
  `libOpenGL.so.0` is missing, install `libopengl0` (Debian, Ubuntu) or `libglvnd-opengl` (Fedora).
  `./install-desktop-entry.sh` adds inkypaint to your applications.

inkypaint is GPL-3.0-or-later, built on [ScarletNX](https://github.com/scarletnx/ScarletNX). Splatoon is a trademark of
its owner. inkypaint is not affiliated with or endorsed by the game's publisher.
