# inkypaint

A static recompilation PC port of **Splatoon 3**, built on [ScarletNX](https://github.com/scarletnx/ScarletNX)'s ARM64
recompiler and Switch runtime. inkypaint supports one game and one build of it: Splatoon 3 (`0100C2500FC20000`) with
its 11.3.0 update (`v2752512`). Everything it adds (hooks, fixes, settings, later online play) is written against that
executable.

inkypaint runs on Windows x86_64, Linux x86_64 and macOS on Apple silicon, with a launcher on each that sets the game
up from your own files, edits its settings, adds mods and signs in to GRID0+, the community's servers, for online play.

**You need your own copy of the game and your own console's keys.** Nothing of the game is in this repository or in
its releases, and nothing extracted from it may be committed (`.gitignore` covers the usual paths, and
`tests/tracked_material.py` checks every push).

## Download

The [releases](https://github.com/GRID0-net/Inkypaint/releases) have each system's package:

| System | Package |
| --- | --- |
| Windows 10/11 x86_64 | the installer (`-setup.exe`), or the portable `.zip` |
| macOS 26 on Apple silicon | `inkypaint.app`, zipped |
| Linux x86_64 (glibc 2.35 or newer) | a portable `.tar.gz`: unpack it and run `./inkypaint` |

Open the launcher, give the **Game Files** page Splatoon 3's base NSP (`0100C2500FC20000`), its 11.3.0 update NSP
(`0100C2500FC20800`, `v2752512`) and your `prod.keys`, and prepare the game: it is extracted and recompiled for your
machine once (about 13 GB of disk, from half an hour to an hour). Before it starts, the launcher asks whether you
want to play online: signed in, GRID0+'s patches are compiled in the first time.

## Building

Each package builds from a checkout with one script. `.github/workflows/release.yml` runs them for a release.

| Package | Command | Builds on |
| --- | --- | --- |
| macOS app | `./inkypaint.sh setup`, then `./inkypaint.sh launcher` (into `build/launcher`) | macOS 26 with Xcode 26 and Homebrew |
| Windows | `./inkypaint.sh windows`, or `scripts/package-windows.sh` | macOS or Linux: LLVM 22+ with lld, CMake, Ninja, NASM (`makensis` for the installer) |
| Linux | `scripts/package-linux.sh` (`scripts/linux-build-deps.sh` installs what it needs on Ubuntu 22.04) | Linux x86_64 |

The Windows and Linux launchers are the Qt one in `src/launcher-qt`, and the Mac's is the SwiftUI one in
`src/launcher`.

## Playing from the checkout on a Mac

For working on inkypaint, `inkypaint.sh` drives everything from the project folder. Put your dumps and keys there,
the way the scripts expect them:

```
inkypaint/
  the rom/            Splatoon 3 [0100C2500FC20000][v65536][BASE].nsp
                      Splatoon 3 11.3.0 [0100C2500FC20800][v2752512][UPD].nsp
  decryption keys/    prod.keys, title.keys
```

Then, in Terminal:

```sh
./inkypaint.sh doctor     # what your Mac has; tells you what is missing
./inkypaint.sh setup      # Homebrew packages (cmake, ninja, pkg-config, llvm) and Ryujinx's MoltenVK
./inkypaint.sh build      # the runtime and the driver, into build/macos
./inkypaint.sh prepare    # extract the game with the update (about 10 GB) and recompile it
./inkypaint.sh run 120    # play for 120 seconds (no number: until you close the window)
```

Each step writes `logs/<step>-<time>.log`. You need Apple's command line tools (`xcode-select --install`) and
[Homebrew](https://brew.sh). Recompiling is memory hungry: on an 8 GB Mac it runs two compiles at a time
(`INKY_JOBS=1 ./inkypaint.sh prepare` if macOS starts swapping hard). An interrupted `prepare` picks up where it
stopped.

The world follows the window or fullscreen display's aspect ratio, including taller MacBook displays and
ultrawide monitors. The UI stays centered in a 16:9 area. F11 or Alt+Enter toggles fullscreen. Rendering quality
still uses the existing `resolution` scale; adapting the aspect ratio does not automatically raise the pixel count.
Use `--adapt_aspect=0` to restore the original 16:9 presentation, or `--aspect_ratio=2.333333` to force 21:9.
`--window_width=1280 --window_height=800` sets the initial window size; resizing it updates the aspect while playing.

## How it works

ScarletNX translates the game's ARM64 executables to LLVM IR ahead of time and compiles them into a native module.
Its runtime emulates the Switch's OS (kernel, IPC services, filesystem) and its GPU (Maxwell command buffers and shaders,
drawn with Vulkan, through MoltenVK on macOS). See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

inkypaint keeps ScarletNX's Switch runtime and drops its 3DS, PS Vita, GBA and Linux ABI drivers. The driver refuses
any dump that is not Splatoon 3 with the 11.3.0 update (`src/nx/runtime/driver/inkypaint.c`). `upstream` is
ScarletNX's repository, merged from to pick up its runtime and GPU work.

## License

GPL-3.0-or-later, as ScarletNX (`LICENSE`). Credits for the code ported into ScarletNX are in
[docs/CREDITS.md](docs/CREDITS.md). Splatoon is a trademark of its owner. inkypaint is not affiliated with or endorsed by
the game's publisher.
