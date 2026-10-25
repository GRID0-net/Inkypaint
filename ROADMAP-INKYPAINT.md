# inkypaint roadmap

## Milestone 0: the fork (done)

- ScarletNX forked with history, `upstream` remote kept for merges.
- The 3DS, PS Vita, GBA and Linux ABI drivers, the multicall program and their tests, docs and dependencies removed.
- The driver accepts Splatoon 3 with its 11.3.0 update alone, at extraction and at recompilation
  (`INKYPAINT_ANY_TITLE=1` lets the test suite's synthetic programs through).
- `inkypaint.sh`: doctor, setup, build, prepare and run on macOS, each step logged to `logs/`.

## Milestone 1: Splatoon 3 boots on macOS

1. Build on the Mac with Homebrew's LLVM, no menu (`SNX_BUILD_GUI=OFF`).
2. Extract with the update, record the main executable's build ID and pin it in `inkypaint.c`, so a recompile of an
   extraction from another version is refused too.
3. Recompile on an 8 GB Mac: measure peak memory per compile, tune `--jobs` and `--chunk`.
4. Run: find where it stops. Upstream's macOS state is "one game reaches its title background", with the cause on
   MoltenVK unknown, so expect renderer work first.

## Milestone 2: playable

- Title screen, the plaza, a Recon map, then a Turf War against bots in the Recon/practice modes.
- Audio (`audren`), controller mappings, saves.
- Performance on Apple silicon: the reason for this port. Measure frame time per scene against the Switch's 30/60 FPS.

## Milestone 3: Windows and Linux

- Upstream's Windows cross build (llvm-mingw) and Linux build, with the same Splatoon-only driver.
- A small inkypaint launcher in place of ScarletNX's multi-console menu (`src/gui`, which still lists the removed
  drivers' entries and is not built on macOS yet).
- CI: replace upstream's workflow (it builds the removed drivers) with macOS, Windows and Linux jobs.

## Milestone 4: online play

Splatoon 3's local play (LAN battles over local wireless, ldn) can travel over the internet through ScarletNX's Eden
room support (`net_room.c`, `docs/NETWORKING.md`), which also lets inkypaint meet Eden players. That is the path for
online multiplayer. The console's official online servers are out of scope: they need a console's credentials, and
connecting a modified client to them breaks the service's terms and gets accounts banned.

## Later

- Higher internal resolution, uncapped frame rate where the game's logic allows it.
- Side Order DLC (the driver already accepts Splatoon 3's DLC dumps).
