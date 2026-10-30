#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the inkypaint contributors
"""Stage immutable game assets only, never saves, credentials, keys or host paths."""
import argparse
from pathlib import Path
import shutil

ASSETS = ("exefs", "romfs", "romfs.bin", "control.nacp", "icon.jpg")

def stage(source: Path, destination: Path):
    source, destination = source.resolve(), destination.absolute()
    if destination.is_symlink():
        raise ValueError("Asset destination must not be a symlink")
    destination = destination.resolve()
    if source == destination or source in destination.parents or destination in source.parents:
        raise ValueError("Asset source and staging destination must be separate trees")
    if not (source / "exefs" / "main.npdm").is_file():
        raise ValueError("Missing extracted exefs/main.npdm")
    if not (source / "romfs").is_dir() and not (source / "romfs.bin").is_file():
        raise ValueError("Missing extracted RomFS")
    # Check before replacing staging. Following a mod's symlink could include
    # unrelated user files in an otherwise correctly named asset directory.
    for name in ASSETS:
        path = source / name
        for entry in [path, *(path.rglob("*") if path.is_dir() and not path.is_symlink() else [])]:
            if entry.is_symlink():
                raise ValueError(f"Symlink in bundled assets: {entry}")
            if entry.exists() and not entry.is_dir() and not entry.is_file():
                raise ValueError(f"Unsupported asset type: {entry}")
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True)
    for name in ASSETS:
        path = source / name
        if path.is_dir():
            shutil.copytree(path, destination / name)
        elif path.is_file():
            shutil.copy2(path, destination / name)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    try:
        stage(args.source, args.destination)
    except (OSError, ValueError) as error:
        parser.exit(1, f"iOS assets: {error}\n")
