#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the inkypaint contributors
"""The Windows build's prebuilt parts, MSYS2's clang64 packages (https://repo.msys2.org/mingw/clang64/): the MinGW-w64
headers and import libraries, libc++, compiler-rt and libunwind (the sysroot a host clang cross-builds with, as
llvm-mingw's), Qt 6 (the launcher), LLVM (the recompiler's backend) and the DLLs those need. Each is pinned in
windows-deps.lock.json by file name and SHA-256, checked after the download.

  windows-deps.py [--dest DIR]   fetch what the lock names into DIR (.cache/windows), extracted into DIR/clang64
  windows-deps.py --update       resolve the newest packages and their dependencies from MSYS2's database again and
                                 rewrite the lock (then rebuild and test before committing it)

Needs Python 3 and zstd (the zstd program, or Python's zstandard module). Nothing of the game is involved."""
import hashlib, io, json, os, re, shutil, subprocess, sys, tarfile, urllib.request

REPO = "https://repo.msys2.org/mingw/clang64/"
HERE = os.path.dirname(os.path.abspath(__file__))
LOCK = os.path.join(HERE, "windows-deps.lock.json")
PREFIX = "mingw-w64-clang-x86_64-"
# what the build asks for (llvm-tools: the import libraries LLVM's CMake package checks for); their dependencies come along, but for those only MSYS2's own tools need (LLVM's Python
# scripts, Tcl): the build links none of them and the package ships none
ROOTS = ["headers-git", "crt-git", "winpthreads", "libwinpthread", "libc++", "compiler-rt", "libunwind",
         "qt6-base", "qt6-declarative", "qt6-svg", "llvm-libs", "llvm", "llvm-tools", "vulkan-headers", "mimalloc"]
SKIP = ["python", "python-packaging", "tcl", "tk", "sqlite3", "mpdecimal", "ncurses", "lld",
        "wineditline", "tzdata", "clang", "clang-libs"]


def unzstd(data):
    try:
        import zstandard
        return zstandard.ZstdDecompressor().stream_reader(io.BytesIO(data)).read()
    except ImportError:
        tool = shutil.which("zstd")
        if not tool:
            sys.exit("windows-deps: needs zstd (brew install zstd, apt install zstd) or Python's zstandard module")
        return subprocess.run([tool, "-dc"], input=data, stdout=subprocess.PIPE, check=True).stdout


def fetch(url):
    with urllib.request.urlopen(url, timeout=120) as r:
        return r.read()


def update():
    db = tarfile.open(fileobj=io.BytesIO(unzstd(fetch(REPO + "clang64.db"))))
    pkgs, provides = {}, {}
    for m in db.getmembers():
        if not m.name.endswith("/desc"):
            continue
        fields, cur = {}, None
        for line in db.extractfile(m).read().decode().split("\n"):
            if line.startswith("%") and line.endswith("%"):
                cur = line.strip("%")
                fields[cur] = []
            elif line and cur:
                fields[cur].append(line)
        pkgs[fields["NAME"][0]] = fields
        for p in fields.get("PROVIDES", []):
            provides[re.split("[<>=]", p)[0]] = fields["NAME"][0]
    skip = {PREFIX + s for s in SKIP}
    chosen, todo = {}, [PREFIX + r for r in ROOTS]
    while todo:
        name = re.split("[<>=]", todo.pop())[0]
        name = name if name in pkgs else provides.get(name, name)
        if name in chosen or name in skip:
            continue
        if name not in pkgs:
            sys.exit("windows-deps: MSYS2 has no package " + name)
        chosen[name] = pkgs[name]
        todo += pkgs[name].get("DEPENDS", [])
    lock = {n[len(PREFIX):]: {"file": f["FILENAME"][0], "sha256": f["SHA256SUM"][0]} for n, f in sorted(chosen.items())}
    with open(LOCK, "w") as f:
        json.dump(lock, f, indent=1)
        f.write("\n")
    print("windows-deps: %d packages locked in %s" % (len(lock), LOCK))


def install(dest):
    with open(LOCK) as f:
        lock = json.load(f)
    pkgdir, root = os.path.join(dest, "packages"), os.path.join(dest, "clang64")
    stamp = os.path.join(dest, "installed.json")
    os.makedirs(pkgdir, exist_ok=True)
    if os.path.exists(stamp) and os.path.isdir(root):
        with open(stamp) as f:
            if json.load(f) == lock:
                print("windows-deps: up to date in " + root)
                return
    if os.path.isdir(root):
        shutil.rmtree(root)
    for name, p in lock.items():
        path = os.path.join(pkgdir, p["file"])
        data = open(path, "rb").read() if os.path.exists(path) else None
        if data is None or hashlib.sha256(data).hexdigest() != p["sha256"]:
            print("windows-deps: downloading " + p["file"])
            data = fetch(REPO + p["file"])
            if hashlib.sha256(data).hexdigest() != p["sha256"]:
                sys.exit("windows-deps: %s doesn't match its SHA-256 in the lock" % p["file"])
            with open(path + ".part", "wb") as f:
                f.write(data)
            os.replace(path + ".part", path)
        with tarfile.open(fileobj=io.BytesIO(unzstd(data))) as t:
            members = [m for m in t.getmembers() if m.name.startswith("clang64/") and (m.isfile() or m.isdir() or m.issym() or m.islnk())]
            if hasattr(tarfile, "tar_filter"):
                t.extractall(dest, members=members, filter="tar")
            else:
                t.extractall(dest, members=members)
    with open(stamp, "w") as f:
        json.dump(lock, f)
    print("windows-deps: %d packages in %s" % (len(lock), root))


if __name__ == "__main__":
    args = sys.argv[1:]
    if args[:1] == ["--update"]:
        update()
    else:
        dest = args[args.index("--dest") + 1] if "--dest" in args else os.path.join(HERE, "..", ".cache", "windows")
        install(os.path.abspath(dest))
