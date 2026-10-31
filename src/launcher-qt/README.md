# the Windows and Linux launcher!

the Qt 6 Quick launcher! same idea as the [Mac app](../launcher/README.md), just for Windows and Linux.
it handles setup, settings, mods, and GRID0+ sign-in:

- **Play** starts the game and stops it, and shows the resolution, frame rate, shader and online settings.
- **Online** is online play on GRID0+: sign-in, your friend code and friends, friend requests, and GRID0+'s patches
  for the game's code, which it downloads and recompiles the game with.
- **Settings** edits the game's settings file, `recompiled\0100C2500FC20000\0100C2500FC20000.ini` in the data folder.
  It includes the LAN adapter for LAN play (ZeroTier and the like).
- **Game Files** sets the game up from the player's own files: the base game's NSP, the 11.3.0 update's NSP and
  `prod.keys`. It extracts and recompiles the game with the bundled runtime and shows the progress.
- **Mods** adds mods from a folder or an archive, turns each on or off and removes it. Files (`romfs/`) apply the next
  time the game starts; code (`exefs/`) takes a recompile, which the page offers.

## Building

On macOS or Linux, `./inkypaint.sh windows` (or `scripts/package-windows.sh`) cross-builds the runtime and the
launcher and packages them:

- `build/windows-package/inkypaint/` holds the files as they are installed.
- `build/inkypaint-<version>-windows-x86_64.zip` is the portable zip.
- `build/inkypaint-<version>-windows-x86_64-setup.exe` is the installer, made when `makensis` is on PATH
  (`brew install makensis`, `apt install nsis`).

It needs CMake, Ninja, Python 3 and zstd, and clang with lld from LLVM 22 or later. On a Mac without them it fetches
llvm-mingw's clang. The prebuilt parts come pinned from MSYS2 (`scripts/windows-deps.py`, `scripts/windows-deps.lock.json`):
the MinGW-w64 sysroot, libc++, Qt and LLVM. The Qt tools for the build machine come from Qt's server through aqtinstall.
Everything lands in `.cache/windows`, and nothing is installed system-wide.

On Linux x86_64, `scripts/package-linux.sh` builds the runtime and the launcher and packages them as
`build/inkypaint-<version>-linux-x86_64.tar.gz` (the files are in `build/linux-package/inkypaint/`). It needs clang
and lld, CMake, Ninja, NASM, patchelf and the headers SDL builds against: `scripts/linux-build-deps.sh` installs them
on Ubuntu 22.04, which the release builds in so the package runs on glibc 2.35 and newer. LLVM's libraries are built
once into `.cache/linux/llvm`, and Qt comes from Qt's server, the same version as the Windows package's. The runtime
links everything but glibc statically, so the copy a recompile puts beside the game runs on its own.

For a Linux source build, build the runtime and launcher. The launcher is a
separate CMake project; building only it does not compile `scarletnx`:

```sh
cmake --preset default
cmake --build build/default --target scarletnx --parallel 2
cmake -S src/launcher-qt -B build/launcher-qt -G Ninja \
  -DINKYPAINT_RUNTIME_FILE="$PWD/build/default/scarletnx"
cmake --build build/launcher-qt --parallel 2
./build/launcher-qt/inkypaint
```

The launcher's build stages the selected executable in `runtime/` beside it, and
its install includes that executable too. Standard native build folders are
detected automatically. Custom runtime locations can be supplied with
`INKYPAINT_RUNTIME_FILE`; when running, `INKYPAINT_RUNTIME` overrides discovery:

```sh
INKYPAINT_RUNTIME="$PWD/build/default/scarletnx" ./build/launcher-qt/inkypaint
```

For UI work on a Mac, use the Mac runtime build path instead. Native launcher
builds need Qt6.5 or later (`CMAKE_PREFIX_PATH` can select its installation).
Source checkout runs also recognize a runtime beside the launcher or in the
checkout's standard build folders. A runtime must be a file with execute permission.

`INKYPAINT_PANE=online` opens a pane, and `INKYPAINT_SCREENSHOT=out.png` saves the window and quits.

## Where things go

| What | Where |
| --- | --- |
| The app (the installer's choice) | `%LOCALAPPDATA%\Programs\inkypaint`, the launcher with the runtime in `runtime\` |
| The extraction and the recompiled game | `%LOCALAPPDATA%\inkypaint\data` (or the data folder you chose) |
| Game output (one log per launch) | `%LOCALAPPDATA%\inkypaint\logs` |
| The GRID0+ password | `%LOCALAPPDATA%\inkypaint\Credentials\grid0.json`, protected with Windows' data protection for your account |
| On Linux | the folder you unpacked; `~/.local/share/inkypaint/data`, `logs` and `Credentials/grid0.json` (readable by you alone) |

uninstalling leaves your data folder alone! your prepared game and saves stay there. The runtime sits in a
folder of its own with only the DLLs it needs. A recompile copies those DLLs beside the game's copy of the runtime, and
the launcher updates them when a newer version brings a newer runtime.

## Online play

The launcher talks to GRID0+ over TLS with Qt's OpenSSL backend. It trusts the server only when its certificate
chains to the SwitchNet Local CA and names the host it answers for. The game reads the password from its standard
input when it starts, so the password never appears on its command line or in its settings. The game's own TLS finds
Windows' trusted root certificates (`CertOpenSystemStore`) and the SwitchNet CA where a console has its firmware's.

## Launcher settings and file pickers

launcher preferences use Qt's `GRID0` / `inkypaint` identity. that gives Windows a valid registry store for the
chosen game folder, GRID0+ account name and other preferences. Linux uses its normal Qt config location.
settings from the old identity are imported once where available. credentials still live in the separate file
listed above, with DPAPI on Windows. if an older Windows build lost the saved account name, the launcher recovers
it from that file and checks the credential before restoring sign-in.

a custom data folder that the old build never saved needs to be selected once again. choose the existing folder,
you don't need to prepare or recompile the game again!

both packages include `Qt/labs/folderlistmodel`, which Qt's fallback file and folder pickers need. keep it in the
package even on systems where native dialogs work.

for launcher regression tests, configure this Qt project with `BUILD_TESTING=ON`, build `inkypaint-test-state`,
then run `ctest --test-dir <launcher-build> --output-on-failure`. it checks persistence across separate processes
with a temporary settings store and a synthetic prepared-game folder. it doesn't use a real account or game.

`inkypaint-test-dialogs` takes `tests/launcher_qt_dialogs.qml` and a staged package's QML directory as absolute
arguments. with `QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software`, it checks that fallback file and folder
pickers open using only the packaged QML modules. missing `Qt.labs.folderlistmodel` must fail that test.

## Closing the game with Escape

Started from the launcher, the game does not close when Escape is pressed in its window. The launcher comes to the front instead and asks whether to close the game, and the game keeps running until the player answers Close the game.

## Discord rich presence

While the launcher is open, Discord shows your friends what you are doing. In the launcher it names the page you are
on. While the game is set up it shows the extraction or the recompile with its progress. While the game runs it shows
where you are in Splatoon 3: Splatsville, a lobby, the rule and stage of a battle, the Salmon Run site, Hero Mode or
Side Order. The runtime writes what the game shows to `<title ID>.presence.json` beside the game's log about once a
second, and the launcher reads that file every 2 seconds. The runtime's `discord_presence_status` setting turns the
file off. The switch in Settings turns the whole feature off.

It uses inkypaint's Discord application, whose ID is the default of the CMake cache variable
`INKYPAINT_DISCORD_APP_ID` (a fork can use its own with `cmake -DINKYPAINT_DISCORD_APP_ID=123456789012345678 ...`).
The environment variable of the same name overrides it when the launcher starts. With an empty ID the launcher never
looks for Discord. When Discord is not running it tries
again every 15 seconds, so starting Discord later is enough.

To make the application, open the Developer Portal at discord.com/developers/applications, create an application
named the way the presence should read (Discord shows its name as the game, for example "Splatoon 3"), and copy its
Application ID. Under Rich Presence, Art Assets, upload a large image named `inkypaint` and the small images `plaza`,
`battle`, `salmon`, `hero` and `sideorder`. An image that is missing is left blank.
