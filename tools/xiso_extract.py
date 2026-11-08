"""Extract an original Xbox disc image (XISO or full Redump dump) to a folder.

Usage: python tools/xiso_extract.py game.iso assets
"""
import os
import struct
import sys

SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"
PARTITION_OFFSETS = (0, 0x18300000, 0xFD90000, 0x89D80000, 0x2080000)
ATTR_DIRECTORY = 0x10
CHUNK = 8 * 1024 * 1024


def find_partition(f):
    for part in PARTITION_OFFSETS:
        f.seek(part + 32 * SECTOR)
        vd = f.read(SECTOR)
        if vd[:20] == MAGIC:
            return part, struct.unpack_from("<II", vd, 20)
    sys.exit("No Xbox (XDVDFS) file system found in this image.")


def walk(f, part, sector, size, out_dir, stats):
    """Extract one directory (a binary tree of entries) into out_dir."""
    if size == 0:
        return
    f.seek(part + sector * SECTOR)
    table = f.read(size)
    os.makedirs(out_dir, exist_ok=True)
    stack, seen = [0], set()
    while stack:
        pos = stack.pop()
        if pos in seen or pos + 14 > len(table):
            continue
        seen.add(pos)
        left, right, ent_sector, ent_size, attr, name_len = struct.unpack_from("<HHIIBB", table, pos)
        if left == 0xFFFF:
            continue
        name = table[pos + 14:pos + 14 + name_len].decode("ascii", "replace")
        if name in ("", ".", "..") or "/" in name or "\\" in name:
            sys.exit(f"Unsafe entry name in image: {name!r}")
        path = os.path.join(out_dir, name)
        if attr & ATTR_DIRECTORY:
            walk(f, part, ent_sector, ent_size, path, stats)
        else:
            f.seek(part + ent_sector * SECTOR)
            with open(path, "wb") as out:
                left_bytes = ent_size
                while left_bytes:
                    chunk = f.read(min(CHUNK, left_bytes))
                    if not chunk:
                        sys.exit(f"Image ends inside {path}")
                    out.write(chunk)
                    left_bytes -= len(chunk)
            stats[0] += 1
            stats[1] += ent_size
            if stats[0] % 200 == 0:
                print(f"  {stats[0]} files, {stats[1] / 2**20:.0f} MiB ...", flush=True)
        for child in (left, right):
            if child:
                stack.append(child * 4)


def main(iso: str, out_dir: str) -> None:
    with open(iso, "rb") as f:
        part, (root_sector, root_size) = find_partition(f)
        stats = [0, 0]
        walk(f, part, root_sector, root_size, out_dir, stats)
    print(f"Extracted {stats[0]} files, {stats[1] / 2**20:.0f} MiB to {out_dir}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
