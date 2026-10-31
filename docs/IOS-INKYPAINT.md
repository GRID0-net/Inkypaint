<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the inkypaint contributors -->
# inkypaint on iOS: prototype

The first target is iPhone 17. This is port work, not a tested mobile release.
The offline iOS linker and input math have CPU tests; Splatoon has not been run
on an iPhone through this path yet.

## Two ways to prepare the game

**Default: prepare on a Mac.** `--target ios-aot` emits an arm64 iOS Mach-O library.
It is bundled as `InkypaintGame.framework` and signed with the app before install.
The phone loads it normally through dyld. The signed AOT app refuses executable
memory loading, disables CPU fallback compilation and the optional MME macro JIT,
and looks up original precompiled functions for runtime hooks. Known GPU command
macros retain native implementations; others use the existing macro interpreter.
Metal shader compilation remains available.

**Optional: prepare on the device.** The upstream `--target ios` path emits an
object pack which the app relocates into executable memory. This mode needs the
existing debugger/JIT setup to execute its output. It is an explicit alternative
build (`local-recompile`), not a fallback used by the signed AOT build.

The macOS `_code.so` is not an iOS game library, despite both devices being arm64.
Missing game functions or code changes without a precompiled variant cannot be
compiled at runtime in signed AOT mode: the game reports the missing code. Offline
coverage of all Splatoon scenes and mods still needs validation. The strict mode
is not a claim that every existing game/mod workflow already works without JIT.

## Mac IPA Builder

`scripts/package-ipa-builder.sh` builds the separate Mac helper app at
`build/ipa-builder/Inkypaint IPA Builder.app`. It does not start the iOS build.
The helper needs the source checkout, Xcode and the build dependencies; select
that checkout as the Project folder. Eight workers are selected by default.

Select an existing inkypaint data folder, installed title folder, or extracted
game folder. The helper reads the Mac launcher's saved data-folder preference
and also recognizes this checkout's `local/data` installation. **Check** runs the
read-only preflight; **Build IPA** starts the combined pipeline and displays its
output; **Stop** cancels the owned build session. **Show IPA** reveals the output
when it finishes. Quitting the helper cancels a running job.

**Include installed DLC** is optional and off by default. Install the DLC packages
through the desktop launcher's **Game Files → DLC (optional)** first, then select
that existing installation in the builder. Only enabled, extracted DLC images are
bundled; desktop settings, dump paths and keys stay on the Mac. The base game can
be packaged without DLC. DLC gameplay on desktop and iOS remains untested.

For the combined CLI pipeline, opt in with
`INKY_IOS_INCLUDE_DLC=1 scripts/package-ios-all.sh /path/to/installed/data`.

The CLI also accepts those installed-game locations now. Existing decrypted
ExeFS/RomFS are reused without extracting dumps again or changing the Mac
installation. The macOS `_code.so` cannot serve as the iOS game framework:
iOS code is still recompiled. Saves, credentials and keys are not imported.

## Combined local build

With the LLVM23.1.4 source archive in `local/ios/` and the iPhoneOS MoltenVK
release extracted into `local/moltenvk-ios/`, use:

```sh
cd /Users/lizzy/projects/inkypaint
bash scripts/package-ios-all.sh --check
INKY_JOBS=8 bash scripts/package-ios-all.sh
```

The check does not compile anything. The build command installs/resumes iOS LLVM
when needed, builds an isolated Mac compiler in `build/ios-host/`, recompiles the
extracted updated game from `local/data/work/splatoon3/`, builds the iPhone app,
and creates `build/ios-aot/package/inkypaint-ios-aot.ipa`. A different extracted
folder can be passed as the final argument. `INKY_JOBS` also sets
`CMAKE_BUILD_PARALLEL_LEVEL`, used by the FFmpeg/OpenSSL dependency builds.
The steps run sequentially; the all-in-one command defaults to eight workers.
The smaller individual scripts retain their two-worker defaults.

The log is `logs/ios-all-<timestamp>.log`. This is a full iOS-target game recompile,
not a copy of the desktop binary. The native compiler does not deploy over the
existing desktop game. To select other dependency locations, set
`INKY_IOS_LLVM_DIR`, `INKY_IOS_LLVM_SOURCE` or `INKY_IOS_MOLTENVK`.

The IPA contains the runtime, `InkypaintGame.framework`, MoltenVK and extracted
ExeFS/RomFS game assets. It needs no separate game-data copy on first launch.
Sign the app and **all nested frameworks** for the device before installation.
The existing `scripts/apple-resign.sh` describes the developer signing route in
[APPLE.md](APPLE.md#installing-from-a-mac). This build command leaves signing and
installation to the user.

This personal IPA contains the user's game. Keep it and generated assets/code
local; do not add them to Git or public release assets. The package only takes
immutable game assets: it excludes saves, mods, keys, account credentials and Mac
folder/source records.

## Game storage and first launch

The signed prototype starts the supported title directly. Game assets are read
from `GameAssets/` in the signed bundle through a read-only filesystem overlay.
Saves and other mutable game files stay in Documents, with settings/log/cache
using the runtime's writable iOS locations. The app makes its writable game folder
on first launch. It does not copy or expand the multi-GB game there.

This first prototype does not yet provide the desktop launcher's import/login
UI. Signing, address-space and memory limits still need verification on the
phone; Apple silicon compatibility alone does not establish playability.

For the optional device-compilation app:

```sh
INKY_IOS_MOLTENVK=/absolute/path/to/extracted/MoltenVK-ios scripts/build-ios.sh local-recompile
```

That build retains the upstream import/preparation UI and JIT object-pack loader.
It requires its JIT setup before playing; the signed AOT build does not.

## Input

The existing on-screen controller is enabled by default on iOS, with
multitouch, two sticks, A/B/X/Y, D-pad, L/R, ZL/ZR, stick clicks, Plus/Minus and
Home/Capture. Its editor supports moving/resizing controls in the safe area.
Gameplay-specific touch layouts are future work.

`device_motion=1` supplies player 1's motion from the phone's accelerometer and
gyroscope. Both landscape orientations are mapped to controller axes; readings
use Horizon's g and revolutions/second units. A physical controller takes motion
priority. Disabling device motion or backgrounding closes the phone sensors.
The game's own motion controls must be enabled. Rotation signs and aim feel need
physical-device testing; CPU tests cover conversion and invalid readings only.

Smooth motion is unchanged by this work and is not part of mobile validation.
