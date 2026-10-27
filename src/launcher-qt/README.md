# inkypaint launcher for Windows and Linux

The app that sets up and starts Splatoon 3 on Windows and Linux, written with Qt 6 Quick. It has the same panes as
the Mac app (`src/launcher`):

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

To work on the UI, build it natively against any Qt 6.5 or later:

```sh
cmake -S src/launcher-qt -B build/launcher-qt -G Ninja -DCMAKE_PREFIX_PATH=<Qt>/lib/cmake
cmake --build build/launcher-qt
INKYPAINT_RUNTIME=build/macos/scarletnx build/launcher-qt/inkypaint
```

`INKYPAINT_PANE=online` opens a pane, and `INKYPAINT_SCREENSHOT=out.png` saves the window and quits.

## Where things go

| What | Where |
| --- | --- |
| The app (the installer's choice) | `%LOCALAPPDATA%\Programs\inkypaint`, the launcher with the runtime in `runtime\` |
| The extraction and the recompiled game | `%LOCALAPPDATA%\inkypaint\data` (or the data folder you chose) |
| Game output (one log per launch) | `%LOCALAPPDATA%\inkypaint\logs` |
| The GRID0+ password | `%LOCALAPPDATA%\inkypaint\Credentials\grid0.json`, protected with Windows' data protection for your account |
| On Linux | the folder you unpacked; `~/.local/share/inkypaint/data`, `logs` and `Credentials/grid0.json` (readable by you alone) |

Uninstalling leaves the data folder alone, because the prepared game and its saves are yours. The runtime sits in a
folder of its own with only the DLLs it needs. A recompile copies those DLLs beside the game's copy of the runtime, and
the launcher updates them when a newer version brings a newer runtime.

## Online play

The launcher talks to GRID0+ over TLS with Qt's OpenSSL backend. It trusts the server only when its certificate
chains to the SwitchNet Local CA and names the host it answers for. The game reads the password from its standard
input when it starts, so the password never appears on its command line or in its settings. The game's own TLS finds
Windows' trusted root certificates (`CertOpenSystemStore`) and the SwitchNet CA where a console has its firmware's.
