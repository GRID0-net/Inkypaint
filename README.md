# inkypaint

A static recompilation PC port of **Splatoon 3**, built on [ScarletNX](https://github.com/scarletnx/ScarletNX)'s ARM64
recompiler and Switch runtime. inkypaint supports one game and one build of it: Splatoon 3 (`0100C2500FC20000`) with
its 11.3.0 update (`v2752512`). Everything it adds (hooks, fixes, settings, later online play) is written against that
executable.

The first target is macOS on Apple silicon. Windows and Linux follow, and the code they need is still here.

**You need your own copy of the game and your own console's keys.** Nothing of the game is in this repository, and
nothing extracted from it may be committed (`.gitignore` covers the usual paths).

## Status

Early bring-up. ScarletNX does not run any game to its menu on macOS yet, so the first milestone is Splatoon 3 booting
past its title screen there. See [ROADMAP-INKYPAINT.md](ROADMAP-INKYPAINT.md).

## macOS quick start

Put your dumps and keys in the project folder, the way the scripts expect them:

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
