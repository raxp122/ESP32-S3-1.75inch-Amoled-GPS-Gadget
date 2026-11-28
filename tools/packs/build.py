#!/usr/bin/env python3
"""Builds the asset packs of the apps (files that live on the microSD, see main/packs.h):
each generator writes a folder, which becomes <name>.amp (format in main/core.h).

    python3 tools/packs/build.py [OUT_DIR]        default build/packs

Needs Pillow. The CI attaches the .amp files to every release; the pendant downloads them.
"""
import os, struct, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
PACKS = ["tarot"]   # one generator tools/packs/<name>.py each


def pack(folder, out):
    files = []
    for root, _, names in os.walk(folder):
        for n in sorted(names):
            full = os.path.join(root, n)
            files.append((os.path.relpath(full, folder).replace(os.sep, "/"), full))
    files.sort()
    with open(out, "wb") as f:
        f.write(b"AMP1")
        for rel, full in files:
            name = rel.encode()
            assert len(name) <= 63 and not rel.startswith(".") and ".." not in rel, rel
            data = open(full, "rb").read()
            f.write(struct.pack("<H", len(name)) + name + struct.pack("<I", len(data)) + data)
        f.write(struct.pack("<H", 0))
    return len(files)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "..", "build", "packs")
    os.makedirs(out, exist_ok=True)
    for name in PACKS:
        folder = os.path.join(out, name)
        subprocess.check_call([sys.executable, os.path.join(HERE, name + ".py"), folder])
        n = pack(folder, os.path.join(out, name + ".amp"))
        print("%s.amp: %d files, %d KB" % (name, n, os.path.getsize(os.path.join(out, name + ".amp")) // 1024))


if __name__ == "__main__":
    main()
