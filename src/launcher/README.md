# inkypaint launcher

The macOS app that sets up and starts Splatoon 3. It is written in SwiftUI for macOS 26 and later, uses Liquid Glass,
and has three panes:

- **Play**: starts the game and stops it. It also shows the frame rate, resolution and shader settings.
- **Online**: online play on GRID0+, the community's servers. You sign in with your GRID0+ account, see your friend
  code and your friends, add friends by code, and answer friend requests. It also downloads GRID0+'s patches for the
  game's code and recompiles the game with them.
- **Settings**: edits the game's settings file, `<data>/recompiled/0100C2500FC20000/0100C2500FC20000.ini`.
- **Game Files**: sets the game up. You pick the base NSP, the 11.3.0 update and `prod.keys` (drag them in, or point
  at a folder), and it extracts and recompiles the game with a progress view. It can also play a game that
  `inkypaint.sh prepare` already made: choose that data folder (`local/data` in the checkout).

## Building

```sh
./inkypaint.sh launcher        # builds the runtime, then build/launcher/inkypaint.app
```

`scripts/package-launcher.sh` builds the self-contained app:

- the launcher;
- the runtime (`scarletnx`), which extracts and recompiles the game, and runs it from a copy placed next to the game;
- MoltenVK;
- Homebrew's LLVM, with its libraries relinked through `@rpath`.

It signs the app ad hoc, so it runs on the Mac that built it. You need Xcode 26.

To work on the UI, open this folder in Xcode 26 (File › Open… › `src/launcher`). Run it from the scheme with these
environment variables:

- `INKYPAINT_RUNTIME`: the path to `build/macos/scarletnx`.
- `INKYPAINT_MOLTENVK`: the path to `libMoltenVK.dylib` in `local/moltenvk`.

## Where things go

| What | Where |
| --- | --- |
| The extraction and the recompiled game | `~/Library/Application Support/inkypaint/data` (or the data folder you chose) |
| Game output (one log per launch) | `~/Library/Logs/inkypaint` |

Game Files shows the data folder. A newer app brings a newer runtime: it is copied over the game's copy the next
time you press Play.

The ink splats in `dist/launcher` are white masks, tinted where they are drawn. The app's icon is there too. The
repository keeps its binaries in `dist/`, so the script copies them into the app, and a run from Xcode reads them
from the checkout.

## Online play

The launcher talks to GRID0+ with the Mac's own TLS (Network.framework). It trusts the server only when its
certificate chains to the SwitchNet Local CA and names the host it answers for. Your password stays in `~/Library/Application Support/inkypaint/Credentials/grid0.json`, with mode 0600
inside an owner-only directory. Rebuilds do not request login-Keychain authentication. An old Keychain entry is
migrated only if macOS permits reading it without a prompt; otherwise sign in to GRID0+ once. When **Play online** is on, the launcher sets `network=1`, `grid0=1`, `grid0_server` and `grid0_user` in
the game's settings, and hands the game the password on its standard input when it starts. The game then signs in
itself (`src/nx/hle/services/network/grid0.c`). The game's own TLS finds the Mac's trusted roots and the SwitchNet CA
where a console has its firmware's, so no firmware is needed.

The patches come from the newest `patches-<serial>` release of GRID0-net/GRID0plus-cfw. Each file is checked against
the release's manifest. They go in the game's data folder as mods, `mods/grid0plus-<category>/exefs/<build ID>.ips`,
and only count once the game is recompiled. The Online page offers that recompile, which keeps the extraction.

The Play page shows the game's logo when `local/branding/splatoon3-logo.png` exists at build time (a transparent PNG;
the script copies it into the app). It is the game's artwork, so it stays in the git-ignored `local/` folder. Without
it, the page shows the title in type. From Xcode, `INKYPAINT_LOGO` points at the file.
