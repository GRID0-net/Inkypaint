#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the inkypaint contributors
"""Find decrypted game files in an existing inkypaint install, without changing it."""
import argparse
import json
from pathlib import Path
import struct

TITLE = "0100C2500FC20000"

def resolve_install(selection: Path) -> Path:
    root = selection.expanduser().resolve()
    if root.is_file():
        root = root.parent
    candidates = [root, root / f"{TITLE}_data", root / "recompiled" / TITLE / f"{TITLE}_data",
                  root / TITLE / f"{TITLE}_data", root / "local/data/recompiled" / TITLE / f"{TITLE}_data",
                  root / "work/splatoon3", root / "local/data/work/splatoon3"]
    for folder in candidates:
        exefs = folder / "exefs"
        if not (exefs / "main").is_file() or not (exefs / "main.npdm").is_file() or not (folder / "romfs").is_dir():
            continue
        # Don't silently prepare a different title when a broad data folder is selected.
        meta = (exefs / "main.npdm").read_bytes()
        if len(meta) < 0x80 or meta[:4] != b"META":
            continue
        aci = struct.unpack_from("<I", meta, 0x70)[0]
        if aci > len(meta) - 0x18 or meta[aci:aci+4] != b"ACI0":
            continue
        title = struct.unpack_from("<Q", meta, aci+0x10)[0]
        if title == int(TITLE, 16):
            return folder.resolve()
    raise ValueError("No supported Splatoon 3 ExeFS/RomFS found. Choose inkypaint's data folder, title folder or extracted game folder.")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("selection", type=Path)
    parser.add_argument("--path-only", action="store_true")
    args = parser.parse_args()
    try:
        folder = resolve_install(args.selection)
    except (OSError, ValueError) as error:
        parser.exit(1, f"iOS game files: {error}\n")
    print(folder if args.path_only else json.dumps({"source": str(folder), "title_id": TITLE}))
