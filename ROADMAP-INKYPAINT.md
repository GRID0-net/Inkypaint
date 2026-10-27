# what's next for inkypaint!

it's already running, so this isn't a list of things that need to happen before it boots anymore!
this is what i'm working towards, no release dates attached.

## What's already here

- Splatoon 3 11.3.0, recompiled with ScarletNX as the base.
- a SwiftUI launcher on macOS and a Qt launcher on Windows and Linux.
- game setup, settings, mods, and GRID0+ sign-in in the launcher.
- LAN play, including choosing a ZeroTier adapter for GRID0.
- GRID0+ as the NSO replacement, with its account, friend, certificate and game patch support.
- the game world following your display's aspect ratio, with the UI kept in 16:9.
- rendering fixes for player limbs and HDR issues on Apple silicon.

having a feature implemented doesn't mean it's perfect on every machine! macOS has had the most playtesting,
Windows has started getting real PC tests, and Linux still needs more reports.

## The big things right now

- **faster recompiles!!** half an hour to an hour for setup is too much. i want to cut that down without breaking
  the generated game code.
- **stable matches**: better frame times in busy battles, less memory use, and no long stretches of stuck input.
- **async shaders**: keep compilation in the background where it's safe, and make newly compiled shaders show up
  without needing to leave and reload the plaza. some one-time resource setup still has to finish before it's used.
- **Windows compatibility**: track down the startup hang reported on an Intel Iris Xe PC. another PC with a GTX 1080
  starts fine, but we still need the diagnostic log before calling this an Intel driver bug.
- **online reliability**: keep testing actual matches and transitions with friends, on both LAN and GRID0+.

## After that

- smoother rendering above 60 FPS, with the game's logic kept at its normal speed. the interpolation work is still
  experimental and has had flickering, so normal performance comes first.
- a usable 30 FPS option for lower end machines, also without changing game speed.
- more DLC testing, including Side Order. accepting a DLC dump isn't the same as verifying the whole thing works.

if you're testing, [here's what to include in a report](docs/TESTING.md)! the machine, game version, scene, and log
help a lot more than just saying it works or doesn't.
