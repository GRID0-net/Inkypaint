# inkypaint launcher

The macOS app that sets up and starts Splatoon 3. It is written in SwiftUI for macOS 26 and later, uses Liquid Glass,
and has three panes:

- **Play**: starts the game and stops it. It also shows the frame rate, resolution and shader settings.
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

The ink splats in `Sources/Inkypaint/Resources` are white masks, tinted where they are drawn.

The Play page shows the game's logo when `local/branding/splatoon3-logo.png` exists at build time (a transparent PNG;
the script copies it into the app). It is the game's artwork, so it stays in the git-ignored `local/` folder. Without
it, the page shows the title in type. From Xcode, `INKYPAINT_LOGO` points at the file.
