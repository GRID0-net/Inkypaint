#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
"""What the tracked files may not hold, as any of it would be grounds to take the repository down:

- keys: a line as prod.keys or title.keys has them (a key's name or a rights ID, then 16 bytes or more of hex);
- dumps: a game's or the system's files, by extension or by magic (NSO, NRO, NCA, NSP, XCI, KIP, INI1, 3DS);
- binaries outside dist/: a prebuilt library or a game's asset (dist/ holds the icons and the controls' atlas);
- the names of piracy sites and of the tools that install pirated games.

Patches are checked on what they add: their context and removed lines are the patched project's own text.

    tracked_material.py [--rev REV] [REPO]   check REPO's checkout (its tracked files as they are), or commit REV
    tracked_material.py --self-test          plant each kind in a throwaway repository: each must be caught

ctest runs the self-test, then the checkout.
"""

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# The agents' instructions mention key names by design, and this file names what it looks for
SKIP = ('.claude/', 'CLAUDE.md', 'tests/tracked_material.py')
BINARY_HOME = 'dist/'
KEY_LINE = re.compile(
    r'\b([A-Za-z0-9_]*(?:key|kek|source|seed)[A-Za-z0-9_]*)[ \t]*=[ \t]*[0-9a-fA-F]{32,}\b', re.IGNORECASE
)
TITLE_KEY_LINE = re.compile(r'\b[0-9a-fA-F]{32}[ \t]*=[ \t]*[0-9a-fA-F]{32}\b')
DUMP_EXT = re.compile(r'\.(nso|nro|nca|ncz|nsp|nsz|xci|xcz|kip|cia|3ds|3dsx|cci|cxi|keys|bfttf)$', re.IGNORECASE)
MAGIC = (
    (0x0, b'NSO0', 'NSO'),
    (0x10, b'NRO0', 'NRO'),
    (0x0, b'PFS0', 'NSP/PFS0'),
    (0x0, b'HFS0', 'HFS0'),
    (0x100, b'HEAD', 'XCI'),
    (0x200, b'NCA3', 'NCA'),
    (0x200, b'NCA2', 'NCA'),
    (0x0, b'KIP1', 'KIP'),
    (0x0, b'INI1', 'INI1'),
    (0x100, b'NCSD', '3DS NCSD'),
    (0x100, b'NCCH', '3DS NCCH'),
    (0x0, b'3DSX', '3DSX'),
)
HUNK = re.compile(r'@@ -\d+(?:,(\d+))? \+\d+(?:,(\d+))? @@')
PIRACY = re.compile(r'\b(nsw2u|nxbrew|romsfun|ziperto|hshop|nopaystation|tinfoil|sigpatch(es)?)\b', re.IGNORECASE)


def git(repo, *args, data=None):
    return subprocess.run(['git', '-C', str(repo), *args], input=data, capture_output=True, check=True).stdout


def checkout_files(repo):
    for name in git(repo, 'ls-files', '-z').decode().split('\0'):
        if name and (Path(repo) / name).is_file():
            yield name, (Path(repo) / name).read_bytes()


def commit_files(repo, rev):
    entries = [e.split('\t', 1) for e in git(repo, 'ls-tree', '-r', '-z', rev).decode().split('\0') if e]
    blobs = [(meta.split()[2], name) for meta, name in entries if meta.split()[1] == 'blob']
    out = git(repo, 'cat-file', '--batch', data=''.join(sha + '\n' for sha, _ in blobs).encode())
    at = 0
    for _, name in blobs:
        end = out.index(b'\n', at)
        size = int(out[at:end].split()[2])
        yield name, out[end + 1 : end + 1 + size]
        at = end + 2 + size


def own_lines(name, text):
    """(line number, line) of what this repository wrote: in a patch, its header and the lines it adds"""
    patch = name.endswith(('.patch', '.diff'))
    old = new = 0  # the hunk's lines still to come, by its header's counts
    for number, line in enumerate(text.splitlines(), 1):
        if old > 0 or new > 0:
            if line.startswith('+'):
                new -= 1
                yield number, line
            elif line.startswith('-'):
                old -= 1
            elif not line.startswith('\\'):  # "\ No newline at end of file" counts as neither
                old, new = old - 1, new - 1
            continue
        hunk = HUNK.match(line) if patch else None
        if hunk:  # what follows the second @@ is the patched file's own line
            old, new = int(hunk.group(1) or 1), int(hunk.group(2) or 1)
            continue
        yield number, line


def check(files):
    errors = []
    for name, data in files:
        if name.startswith(SKIP):
            continue
        if DUMP_EXT.search(name):
            errors.append(f'{name}: a dump file by its name; tests build their programs when they run')
        for offset, magic, kind in MAGIC:
            if data[offset : offset + 4] == magic:
                errors.append(f'{name}: {kind} by its magic at {offset:#x}')
        if b'\0' in data[:8192]:
            if not name.startswith(BINARY_HOME):
                errors.append(f'{name}: a binary outside {BINARY_HOME}')
            continue
        for number, line in own_lines(name, data.decode('utf-8', 'replace')):
            where = f'{name}:{number}'
            if KEY_LINE.search(line) or TITLE_KEY_LINE.search(line):
                errors.append(f'{where}: a key as prod.keys or title.keys holds it')
            if PIRACY.search(line):
                errors.append(f'{where}: {PIRACY.search(line).group(0)}, a piracy site or tool')
    return errors


PLANTED = {
    'keys/prod.txt': b'header_key = 00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff\n',
    'keys/title.txt': b'"0100000000000000000000000000000a = 3a9c4e17b2d05f68e1c7a3940b6d2f85"\n',
    'game.nro': b'\0' * 0x10 + b'NRO0' + b'\0' * 0x30,
    'data/blob': b'\x7fELF\0\0\0',
    'nca.dat': b'x' * 0x200 + b'NCA3',
    'README.md': b"See nsw2u for games.\n",
    'cmake/x.patch': b'# header_key = 00112233445566778899aabbccddeeff\n'
    b'--- a/x.c\n+++ b/x.c\n@@ -1,3 +1,3 @@ Nintendo context\n'
    b'-header_key = 00112233445566778899aabbccddeeff\n'
    b' header_key = 00112233445566778899aabbccddeeff\n'
    b'+header_key = 00112233445566778899aabbccddeeff\n'
    b' context\n'
    b'\\ No newline at end of file\n',
}
WANT = [
    'README.md:1: nsw2u',
    'cmake/x.patch:1: a key',
    'cmake/x.patch:7: a key',
    'data/blob: a binary outside',
    'game.nro: NRO by its magic',
    'game.nro: a binary outside',
    'game.nro: a dump file',
    'keys/prod.txt:1: a key',
    'keys/title.txt:1: a key',
    'nca.dat: NCA by its magic',
]
CLEAN = {
    'docs/attribution.md': b"Splatoon belongs to Nintendo. inkypaint isn't affiliated with or endorsed by Nintendo.\n"
    b"GRID0+ uses its own servers, not Nintendo's.\n",
    'src/pad.c': b'case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO: /* FontNintendoExtended, NintendoSDK */\n',
    'src/hosts.inc': b'BLOCKED_HOST("nintendo.net") /* srv.nintendo.net, nintendo-europe.com, nintendowifi.net */\n',
    'docs/a.md': b'`source/board/nintendo/nx/kern.cpp` and https://www.nintendo.com/\n',
    'tests/keys.c': b'"header_key = 00112233\\n" "good = 0123456789abcdef0123456789abcdef\\n"\n',
    'dist/icon.png': b'\x89PNG\r\n\x1a\n\0\0',
    '.claude/x.md': b'Nintendo\n',
}


def self_test():
    with tempfile.TemporaryDirectory() as temporary:
        repo = Path(temporary)
        git(repo, 'init', '-q')
        for name, data in {**PLANTED, **CLEAN}.items():
            (repo / name).parent.mkdir(parents=True, exist_ok=True)
            (repo / name).write_bytes(data)
        git(repo, 'add', '-A')
        got = sorted(check(checkout_files(repo)))
        # a tree, not a commit: no identity, signing or hooks of the machine's git config come into it
        from_commit = sorted(check(commit_files(repo, git(repo, 'write-tree').decode().strip())))
    failed = [f'missed: {want}' for want in WANT if not any(e.startswith(want) for e in got)]
    failed += [f'not planted: {e}' for e in got if not any(e.startswith(want) for want in WANT)]
    failed += ['the commit read differently from the checkout'] if from_commit != got else []
    for line in failed:
        print(f'FAIL self-test {line}')
    if not failed:
        print(f'self-test: the {len(WANT)} planted ones caught, the identifiers, fixtures and dist/ let be')
    return not failed


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('repo', nargs='?', default=str(ROOT))
    parser.add_argument('--rev', help='check this commit (or tree) instead of the checkout')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if not shutil.which('git'):
        print('git is not on PATH: nothing tells the tracked files apart', file=sys.stderr)
        return 77
    if args.self_test:
        return 0 if self_test() else 1
    if subprocess.run(['git', '-C', args.repo, 'rev-parse'], check=False, capture_output=True).returncode:
        print(f'{args.repo} is not a git checkout: nothing tells its tracked files apart', file=sys.stderr)
        return 77
    files = list(commit_files(args.repo, args.rev) if args.rev else checkout_files(args.repo))
    errors = check(files)
    for error in errors:
        print(error)
    print(f'{len(files)} tracked files, {len(errors)} problems')
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
