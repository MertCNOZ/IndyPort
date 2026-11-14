"""Map addresses of one release of an XBE to another release of the same game (USA -> PAL).

The two releases are the same code linked a little differently, so a function's bytes are the same at a shifted
address apart from the addresses inside them. Code: the bytes at the address, with every 4 bytes that look like an
address in the image and every call / jump displacement left out, are searched near the same address in the other
image; the match must be the only one. Data: code that loads the address is matched, and the other image's
instruction gives the other address; several references must agree.

Usage:
  python tools/map_release.py <from.xbe> <to.xbe> code 0x323160 0x10B3F9 ...
  python tools/map_release.py <from.xbe> <to.xbe> data 0x6D75F0 ...
  python tools/map_release.py <from.xbe> <to.xbe> config config/game.toml config/game_eu.toml <xbe path> <out> <library>
"""
import re
import struct
import sys


class Xbe:
    def __init__(self, path):
        self.data = open(path, "rb").read()
        d = self.data
        assert d[:4] == b"XBEH", path
        self.base = struct.unpack_from("<I", d, 0x104)[0]
        self.entry_enc = struct.unpack_from("<I", d, 0x128)[0]
        count, headers = struct.unpack_from("<II", d, 0x11C)
        self.sections = []
        for i in range(count):
            off = headers - self.base + i * 56
            flags, va, vsize, raw, rsize, name_addr = struct.unpack_from("<IIIIII", d, off)
            name = d[name_addr - self.base:].split(b"\0", 1)[0].decode("latin-1")
            self.sections.append((name, va, vsize, raw, rsize, flags))

    def section_of(self, va):
        for s in self.sections:
            if s[1] <= va < s[1] + s[4]:
                return s
        return None

    def read(self, va, n):
        s = self.section_of(va)
        if not s or va + n > s[1] + s[4]:
            return None
        off = s[3] + (va - s[1])
        return self.data[off:off + n]

    def code_sections(self):
        return [s for s in self.sections if s[5] & 4]  # executable


def mask_of(b):
    """Positions to compare: 4 byte runs that look like image addresses and rel32 operands are left out."""
    keep = [True] * len(b)
    i = 0
    while i < len(b):
        if b[i] in (0xE8, 0xE9) and i + 5 <= len(b):
            for j in range(i + 1, i + 5):
                keep[j] = False
        if b[i] == 0x0F and i + 1 < len(b) and 0x80 <= b[i + 1] <= 0x8F and i + 6 <= len(b):
            for j in range(i + 2, i + 6):
                keep[j] = False
        i += 1
    for i in range(len(b) - 3):
        v = struct.unpack_from("<I", b, i)[0]
        if 0x10000 <= v < 0x900000:
            for j in range(i, i + 4):
                keep[j] = False
    return keep


def matches(src, dst, va, n, lo, hi):
    pat = src.read(va, n)
    if not pat:
        return None
    keep = mask_of(pat)
    if sum(keep) < n // 2:
        return None
    sel = [i for i in range(n) if keep[i]]
    first = sel[0]
    found = []
    for name, sva, vsize, raw, rsize, flags in dst.sections:
        start, end = max(lo, sva), min(hi, sva + rsize - n)
        if start >= end:
            continue
        blob = dst.data[raw + (start - sva): raw + (end - sva) + n]
        needle = pat[first]
        pos = blob.find(bytes([needle]), first)
        while pos != -1:
            p = pos - first
            if p >= 0 and p + n <= len(blob) and all(blob[p + i] == pat[i] for i in sel):
                found.append(start + p)
            pos = blob.find(bytes([needle]), pos + 1)
    return found


def map_code(src, dst, va, radius=0x3000):
    for n in (48, 96, 160, 256, 32, 24):
        found = matches(src, dst, va, n, va - radius, va + radius)
        if found is None:
            continue
        found = sorted(set(found))
        if len(found) == 1:
            return found[0]
    # past the end of a function the window may run into the next one: match from a little before instead
    for back in (16, 32, 64):
        for n in (64, 128):
            found = matches(src, dst, va - back, n, va - back - radius, va - back + radius)
            if found and len(set(found)) == 1:
                return found[0] + back
    return None


def references(xbe, value, limit=12):
    """Code addresses where the 4 bytes of value appear."""
    out = []
    pat = struct.pack("<I", value)
    for name, va, vsize, raw, rsize, flags in xbe.code_sections():
        blob = xbe.data[raw:raw + rsize]
        pos = blob.find(pat)
        while pos != -1 and len(out) < limit:
            out.append(va + pos)
            pos = blob.find(pat, pos + 1)
    return out


def map_data(src, dst, value):
    votes = {}
    for ref in references(src, value):
        for back in (12, 8, 4, 16, 24):
            site = map_code(src, dst, ref - back)
            if site is None:
                continue
            got = dst.read(site + back, 4)
            if got:
                v = struct.unpack("<I", got)[0]
                votes[v] = votes.get(v, 0) + 1
            break
    if not votes:
        return None, 0, 0
    best = max(votes, key=votes.get)
    return best, votes[best], sum(votes.values())


DATA_SECTIONS = (".rdata", ".data", "$$XTIMAGE", "$$XSIMAGE")


def map_any(src, dst, va):
    s = src.section_of(va)
    if s and s[0] not in DATA_SECTIONS and s[5] & 4:
        return map_code(src, dst, va), "code"
    v, agree, total = map_data(src, dst, va)
    return v, f"data {agree}/{total}"


def entry_of(xbe):
    retail = 0xA8FC57AB
    return xbe.entry_enc ^ retail


def map_config(src, dst, cfg_in, cfg_out, xbe_path, out_dir, library):
    lines = open(cfg_in, encoding="utf-8").read().split("\n")
    out, fails = [], []
    for line in lines:
        m = re.match(r"^(\s*(?:\w+\s*=|at\s*=)\s*)(0x[0-9A-Fa-f]+)(.*)$", line)
        key = line.split("=")[0].strip() if "=" in line else ""
        if key == "path":
            line = f'path = "{xbe_path}"'
        elif key == "out":
            line = f'out = "{out_dir}"'
        elif key == "library":
            line = f'library = "{library}"  # <exe>.dll, which the executable delay loads'
        elif key == "entry":
            line = f"entry = 0x{entry_of(dst):X}"
        elif m and key not in ("title",):
            va = int(m.group(2), 16)
            new, how = map_any(src, dst, va)
            if new is None:
                fails.append((key, va, how))
                line = m.group(1) + "0" + f"  # not found in this release (was 0x{va:X})"
            else:
                rest = m.group(3)
                rest = re.sub(r"0x[0-9A-Fa-f]{5,6}", lambda mm: mapped_or_same(src, dst, mm.group(0)), rest)
                line = m.group(1) + f"0x{new:X}" + rest
        out.append(line)
    open(cfg_out, "w", encoding="utf-8", newline="\n").write("\n".join(out))
    for key, va, how in fails:
        print(f"NOT FOUND {key} 0x{va:X} ({how})")
    print(f"{cfg_out}: {len(fails)} not found")


def mapped_or_same(src, dst, text):
    va = int(text, 16)
    new, _ = map_any(src, dst, va)
    return f"0x{new:X}" if new is not None else text


def main():
    a, b, mode = sys.argv[1:4]
    src, dst = Xbe(a), Xbe(b)
    if mode == "config":
        map_config(src, dst, *sys.argv[4:9])
        return
    for text in sys.argv[4:]:
        va = int(text, 16)
        if mode == "code":
            new, how = map_code(src, dst, va), "code"
        else:
            new, how = map_any(src, dst, va)
        print(f"0x{va:06X} -> " + (f"0x{new:06X} ({how}, {new - va:+#x})" if new is not None else f"NOT FOUND ({how})"))


if __name__ == "__main__":
    main()
