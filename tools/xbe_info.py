"""Print an original Xbox XBE's title, sections and XDK library versions.

Usage: python xbe_info.py path/to/default.xbe
       python xbe_info.py path/to/game.iso   (XISO or Redump image)

Tells whether XboxXBERecomp already has XDK signatures for the game's build
(currently 4831, 5659 and 5849).
"""
import struct
import sys

SUPPORTED_XDK = {4831, 5659, 5849}


SECTOR = 2048
XDVDFS_MAGIC = b"MICROSOFT*XBOX*MEDIA"
# Game partition offsets: extract-xiso output, Redump full dumps (several layouts).
PARTITION_OFFSETS = (0, 0x18300000, 0xFD90000, 0x89D80000, 0x2080000)


def read_xbe_from_iso(path: str) -> bytes:
    """Return default.xbe from an Xbox disc image (XISO or full Redump dump)."""
    with open(path, "rb") as f:
        for part in PARTITION_OFFSETS:
            f.seek(part + 32 * SECTOR)
            vd = f.read(SECTOR)
            if vd[:20] == XDVDFS_MAGIC:
                break
        else:
            sys.exit("No Xbox (XDVDFS) file system found in this image.")

        root_sector, root_size = struct.unpack_from("<II", vd, 20)
        f.seek(part + root_sector * SECTOR)
        root = f.read(root_size)

        # The root directory is a binary tree; offsets are in 4-byte units.
        stack = [0]
        while stack:
            pos = stack.pop()
            if pos + 14 > len(root):
                continue
            left, right, sector, size, _attr, name_len = struct.unpack_from("<HHIIBB", root, pos)
            if left == 0xFFFF:
                continue
            name = root[pos + 14:pos + 14 + name_len].decode("ascii", "replace")
            if name.lower() == "default.xbe":
                f.seek(part + sector * SECTOR)
                return f.read(size)
            for child in (left, right):
                if child:
                    stack.append(child * 4)
    sys.exit("default.xbe not found in the image's root folder.")


def main(path: str) -> None:
    if path.lower().endswith(".iso"):
        data = read_xbe_from_iso(path)
    else:
        data = open(path, "rb").read()
    if data[:4] != b"XBEH":
        sys.exit("Not an XBE file (missing XBEH magic).")

    u32 = lambda off: struct.unpack_from("<I", data, off)[0]
    base = u32(0x104)
    off = lambda va: va - base

    cert = off(u32(0x118))
    title_id = u32(cert + 0x08)
    title = data[cert + 0x0C:cert + 0x0C + 80].decode("utf-16-le").split("\0")[0]
    regions = u32(cert + 0xA0)
    entry_raw = u32(0x128)
    # Retail entry points are XORed with this key.
    entry = entry_raw ^ 0xA8FC57AB

    print(f"Title      : {title}")
    print(f"Title ID   : 0x{title_id:08X}")
    print(f"Base       : 0x{base:08X}")
    print(f"Entry      : 0x{entry:08X} (retail key)")
    region_names = [n for bit, n in ((1, "NA"), (2, "JP"), (4, "World/PAL")) if regions & bit]
    print(f"Regions    : {', '.join(region_names) or hex(regions)}")

    print("\nSections:")
    count, hdrs = u32(0x11C), off(u32(0x120))
    for i in range(count):
        h = hdrs + i * 0x38
        vaddr, vsize = u32(h + 4), u32(h + 8)
        name_off = off(u32(h + 0x14))
        name = data[name_off:data.index(b"\0", name_off)].decode("ascii", "replace")
        print(f"  {name:<10} 0x{vaddr:08X}  size 0x{vsize:X}")

    print("\nXDK libraries:")
    count, libs = u32(0x160), off(u32(0x164))
    builds = set()
    for i in range(count):
        p = libs + i * 16
        name = data[p:p + 8].split(b"\0")[0].decode("ascii", "replace")
        major, minor, build, _flags = struct.unpack_from("<4H", data, p + 8)
        builds.add(build)
        print(f"  {name:<8} {major}.{minor}.{build}")

    print()
    hits = builds & SUPPORTED_XDK
    if hits:
        print(f"OK: XboxXBERecomp has signatures for XDK {sorted(hits)}.")
    else:
        print(f"Warning: no signatures for XDK {sorted(builds)}; a new "
              "xdk/<build>/signatures.toml and layout.toml would be needed.")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
