<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the ScarletNX contributors -->
# contributing!

want to help out? rendering fixes, performance work, launcher improvements, and useful bug reports are all welcome!
this fork is specifically for Splatoon 3 11.3.0.

[build instructions](docs/BUILD.md) · [development](docs/DEVELOPMENT.md) · [architecture](docs/ARCHITECTURE.md) ·
[agent rules](CLAUDE.md)

## Game data stays out

please keep game data out of git and reports! no keys, game dumps, extracted files, generated game code, or shader
and texture dumps. use your own local files for testing. [CLAUDE.md](CLAUDE.md) has the full rule.

## Build and check

[BUILD.md](docs/BUILD.md) covers the toolchains. Before a pull request, from the checkout root:

```sh
cmake -DCMAKE_BUILD_TYPE=Release -Bbuild -S.
cmake --build build
ctest --test-dir build --output-on-failure
scripts/format.sh --check
python3 tests/run_filesystem_tree.py --cc clang --sanitize
```

`scripts/format.sh` without `--check` formats in place (clang-format 23 and the pinned Ruff, through `uvx`). CI
doesn't check formatting, so run it locally. Configuring with `-DSNX_TEST_SANITIZERS=ON` builds the native
regression tests, and the programs and libraries they run or link, with ASan and UBSan. That build is for tests only and
is not deployed.

if a change builds, that's a start, but please test the scene it actually affects too! say what you ran and what
happened. if you can't test on the affected platform, say that instead of calling it fixed. [DEVELOPMENT.md](docs/DEVELOPMENT.md) has the run and diagnose commands, and
[TESTING.md](docs/TESTING.md) the form a test report takes: the build, the machine, the scene, the runs and the result.

## Commits

The subject is a bracketed area followed by a lowercase imperative sentence. The body says why, and how it was
verified. A `Signed-off-by` line (`git commit -s`) is encouraged but not required. For example:

```
[tests] avoid the Windows near macro in the motion test

Rename the local float comparison helper so the Windows header macro does not corrupt its
declaration and calls. Verified with x86_64-w64-mingw32-clang using the CI compile flags and
the hid_motion CTest.

Signed-off-by: Your Name <you@example.org>
```

Common areas, from the history: `[gui]`, `[shader]`, `[ci]`, `[runtime]`, `[gpu]`, `[audio]`, `[cmake]`, `[docs]`,
`[cpu]`, `[hid]`, `[hle]`, `[test]`, `[build]`, `[kernel]`, `[driver]`, `[cli]`, `[android]`, `[vk]`, `[dist]`,
`[agents]`. Two areas share one bracket, as in `[gui, android]`.

Commit messages and user-facing docs read as plain prose: no em-dash splicing clauses, no semicolons joining
sentences, no spaced hyphen as a dash.

## Pull requests

keep the PR focused on what changed, why, and what you tested. a rendering fix should say which scene you checked,
and a performance change should include measurements from comparable runs. no need to claim a speedup you haven't
measured!

the workflows in `.github/workflows/` check the tree and build packages. local game testing still matters,
especially for online battles and hardware-specific bugs.

## C code

New C follows the rules of [RFC #142](https://github.com/scarletnx/ScarletNX/issues/142): strict ISO C99, no state
another value already says, indices instead of pointer chains, one allocation per owner. The
`.claude/skills/snx-code-quality/SKILL.md` skill is the reference, with the exceptions to each rule.

## License

ScarletNX is GPL-3.0-or-later (`LICENSE`). A new file starts with the SPDX line and the project's copyright line,
in the file's comment syntax:

```c
/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
```

Code ported from another project keeps its copyright lines, names where it came from and gets a row in
[docs/CREDITS.md](docs/CREDITS.md). Code under GPL-3.0, GPL-2.0-or-later (yuzu, suyu, sudachi, Citron, Eden), LGPL,
MIT (Ryujinx, LibHac), BSD, ISC, zlib or Apache 2.0 can be ported. GPL-2.0-only code can't be. Atmosphère is
GPL-2.0-only, so it is read as a reference for how Horizon behaves and never ported.
