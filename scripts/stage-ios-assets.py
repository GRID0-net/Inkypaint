#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the inkypaint contributors
"""Stage immutable game assets only, never saves, credentials, keys or host paths."""
import argparse
from pathlib import Path
import shutil
import os
import re

ASSETS = ("exefs", "romfs", "romfs.bin", "control.nacp", "icon.jpg")

def installed_dlcs(dlc_source: Path):
    """Only installed, enabled images; never carry dump paths or desktop INIs."""
    dlc_source = dlc_source.resolve()
    game = "0100C2500FC20000"
    if dlc_source.name != game + "_data":
        raise ValueError("Optional DLC needs an installed game's _data folder")
    folder = dlc_source.parent
    ini = folder / (game + ".ini")
    if ini.is_symlink():
        raise ValueError("DLC settings must not be a symlink")
    text = ini.read_text() if ini.is_file() else ""
    matches = re.findall(r"^addons[ \t]*=[ \t]*([^\r\n]*)", text, re.MULTILINE)
    dlcs = []
    for entry in (matches[-1].split(",") if matches else []):
        entry = entry.strip().upper()
        if not entry:
            continue
        if re.fullmatch("0100C2500FC20800(V[0-9]+)?", entry):
            continue  # updates are already part of the base extraction
        if not re.fullmatch("[0-9A-F]{16}", entry) or entry in (game, "0000000000000000"):
            raise ValueError("Invalid DLC entry in the installed game's settings")
        image = folder / f"{game}_{entry}_data/system/archives/{entry}.romfs"
        if any(p.is_symlink() for p in [image, *image.parents]):
            raise ValueError(f"Symlink in optional DLC: {image}")
        if not image.is_file() or not image.stat().st_size:
            raise ValueError(f"Extract this DLC in the desktop launcher first: {entry}")
        if entry not in [title for title, _ in dlcs]:
            dlcs.append((entry, image))
    return dlcs

def stage(source: Path, destination: Path, dlc_source: Path | None = None):
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
    dlcs = installed_dlcs(dlc_source) if dlc_source is not None else []
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
    if dlcs:
        archives = destination / "system/archives"
        archives.mkdir(parents=True)
        for title, image in dlcs:
            shutil.copy2(image, archives / (title + ".romfs"))
        (destination / "dlc.ini").write_text("addons=" + ",".join(title for title, _ in dlcs) + "\n")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path, nargs="?")
    parser.add_argument("--check-dlc", action="store_true", help="Validate optional DLC without staging assets")
    args = parser.parse_args()
    try:
        dlc_source = Path(os.environ["INKY_IOS_DLC_SOURCE"]) if os.environ.get("INKY_IOS_DLC_SOURCE") else None
        if args.check_dlc:
            print(f"Optional DLC: {len(installed_dlcs(args.source))} installed package(s)")
        else:
            if args.destination is None:
                raise ValueError("Choose an asset staging destination")
            stage(args.source, args.destination, dlc_source)
    except (OSError, ValueError) as error:
        parser.exit(1, f"iOS assets: {error}\n")
