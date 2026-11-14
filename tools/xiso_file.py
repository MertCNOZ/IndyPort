"""Copy one file out of the root of an original Xbox disc image (XISO or full Redump dump).

Usage: python tools/xiso_file.py game.iso default.xbe out/default.xbe
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from xiso_extract import SECTOR, find_partition  # noqa: E402


def find_entry(f, part, sector, size, name):
    """The (sector, size) of a file in a directory's binary tree of entries, by name (case insensitive)."""
    f.seek(part + sector * SECTOR)
    table = f.read(size)
    stack, seen = [0], set()
    while stack:
        pos = stack.pop()
        if pos in seen or pos + 14 > len(table):
            continue
        seen.add(pos)
        left, right, ent_sector, ent_size, attr, name_len = struct.unpack_from("<HHIIBB", table, pos)
        if left == 0xFFFF:
            continue
        entry = table[pos + 14:pos + 14 + name_len].decode("latin-1")
        if entry.lower() == name.lower():
            return ent_sector, ent_size
        for child in (left, right):
            if child:
                stack.append(child * 4)
    return None


def main(iso, name, out):
    with open(iso, "rb") as f:
        part, (root_sector, root_size) = find_partition(f)
        found = find_entry(f, part, root_sector, root_size, name)
        if not found:
            sys.exit(f"{name} is not in the root of {iso}")
        sector, size = found
        f.seek(part + sector * SECTOR)
        data = f.read(size)
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, "wb") as o:
        o.write(data)
    print(f"{name}: {size} bytes -> {out}")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:])
