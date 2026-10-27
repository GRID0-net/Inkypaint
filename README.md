# inkypaint

a splatoon 3 pc port!! a proper one (or as proper as it can be) with [ScarletNX](https://github.com/scarletnx/ScarletNX)
as a base, and online multiplayer via either LAN! (GRID0 ZeroTier) or a proper NSO replacement! (GRID0+)

Windows, Linux, and Apple silicon Macs! the launcher handles extracting your game, recompiling it, settings,
mods, and signing into GRID0+.

this is specifically for **Splatoon 3 11.3.0**. other games and other updates aren't supported.
you'll need your own game dump and your own console's keys, none of that comes with inkypaint.

## Download

check [releases](https://github.com/GRID0-net/Inkypaint/releases) for the downloads!

| System | Package |
| --- | --- |
| Windows 10/11 x86_64 | the installer (`-setup.exe`), or the portable `.zip` |
| macOS 26 on Apple silicon | `inkypaint.app`, zipped |
| Linux x86_64 (glibc 2.35 or newer) | a portable `.tar.gz`: unpack it and run `./inkypaint` |

open the launcher and head to **Game Files**. you'll need:

- the base game NSP (`0100C2500FC20000`)
- the **11.3.0** update NSP (`0100C2500FC20800`, `v2752512`)
- your `prod.keys`

then prepare the game! extraction and recompilation need about 13 GB of disk space. the first recompile can take
half an hour to an hour depending on your machine, which is way too long, and something i want to improve.
after that you can just press **Play**, you don't need to recompile every time you open it.

if you're going to use GRID0+, sign in when the launcher asks about online play so its patches can be included
in that first recompile.

## Multiplayer!

there are two ways to play with friends:

- **LAN / GRID0 ZeroTier**: use the game's LAN play, with everyone on the same LAN or ZeroTier network.
  enable networking and pick your ZeroTier adapter in **Settings > LAN play** if you're using it.
- **GRID0+**: the NSO replacement! sign in on the launcher's **Online** page and enable online play.
  this uses GRID0+'s servers, not Nintendo's, and needs GRID0+'s game patches compiled in.

LAN and GRID0+ are separate options, you don't need a GRID0+ account just to use LAN.
[networking details here](docs/NETWORKING.md).

## Where it's at

it's playable, but there's still work to do! performance varies by machine and scene, and intense matches can
struggle on lower end devices (i need to apply more optimization runs for other devices)

higher framerates are still experimental. 
(as in you can enable it, but theres a flickering bug that happens which im not sure how to fix yet, regardless the game should work fine provided ur device can hold the framerates.)

see the [roadmap](ROADMAP-INKYPAINT.md) for what i'm working on.

## Building

want to build it yourself? these are the package scripts. `.github/workflows/release.yml` uses them too.

| Package | Command | Builds on |
| --- | --- | --- |
| macOS app | `./inkypaint.sh setup`, then `./inkypaint.sh launcher` (into `build/launcher`) | macOS 26 with Xcode 26 and Homebrew |
| Windows | `./inkypaint.sh windows`, or `scripts/package-windows.sh` | macOS or Linux: LLVM 22+ with lld, CMake, Ninja, NASM (`makensis` for the installer) |
| Linux | `scripts/package-linux.sh` (`scripts/linux-build-deps.sh` installs what it needs on Ubuntu 22.04) | Linux x86_64 |

Windows and Linux use the [Qt launcher](src/launcher-qt/README.md), macOS uses the
[SwiftUI launcher](src/launcher/README.md). more build details are in [docs/BUILD.md](docs/BUILD.md).

## Playing from the checkout on a Mac

if you're working from the checkout on a Mac, `inkypaint.sh` handles the steps below.
put your own dumps and keys in these folders:

```
inkypaint/
  the rom/            Splatoon 3 [0100C2500FC20000][v65536][BASE].nsp
                      Splatoon 3 11.3.0 [0100C2500FC20800][v2752512][UPD].nsp
  decryption keys/    prod.keys, title.keys
```

then, in Terminal:

```sh
./inkypaint.sh doctor     # what your Mac has; tells you what is missing
./inkypaint.sh setup      # Homebrew packages (cmake, ninja, pkg-config, llvm) and Ryujinx's MoltenVK
./inkypaint.sh build      # the runtime and the driver, into build/macos
./inkypaint.sh prepare    # extract the game with the update (about 10 GB) and recompile it
./inkypaint.sh run 120    # play for 120 seconds (no number: until you close the window)
```

each step writes to `logs/<step>-<time>.log`, so if something goes wrong there's a log to check!
you'll need Apple's command line tools (`xcode-select --install`) and [Homebrew](https://brew.sh).
recompiling uses a lot of memory, so an 8 GB Mac runs two compiles at a time. use
`INKY_JOBS=1 ./inkypaint.sh prepare` if it's swapping too much. an interrupted `prepare` picks up where it stopped.

the game world fills your window, including taller MacBook screens and ultrawide monitors! the UI stays centered
in 16:9 so it doesn't get stretched. F11 or Alt+Enter toggles fullscreen. the `resolution` scale still controls
rendering quality, changing the aspect ratio doesn't automatically increase the pixel count.
Use `--adapt_aspect=0` to restore the original 16:9 presentation, or `--aspect_ratio=2.333333` to force 21:9.
`--window_width=1280 --window_height=800` sets the initial window size; resizing it updates the aspect while playing.

## How it works

ScarletNX does the heavy lifting here! it translates the game's ARM64 code into LLVM IR ahead of time, then
compiles that into a native module for your machine. its runtime handles the Switch OS services and translates
the GPU's commands and shaders to Vulkan (through MoltenVK on macOS).

inkypaint builds on that for Splatoon 3 specifically, with the rendering fixes, launcher, display support, and
GRID0+ integration. the other console and Linux ABI drivers from upstream aren't part of this fork.
there's more on the internals in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## License

GPL-3.0-or-later, same as ScarletNX! see [LICENSE](LICENSE) and [credits](docs/CREDITS.md).

Splatoon belongs to Nintendo. inkypaint isn't affiliated with or endorsed by Nintendo.
