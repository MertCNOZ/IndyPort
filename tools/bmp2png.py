"""Converts an uncompressed 24 or 32-bit BMP (the frame captures) to PNG, without Pillow.

Usage: python tools/bmp2png.py in.bmp out.png [--alpha]   (--alpha writes the alpha channel as gray)
"""
import struct
import sys
import zlib


def main():
    src, dst = sys.argv[1], sys.argv[2]
    alpha = "--alpha" in sys.argv
    d = open(src, "rb").read()
    if d[:2] != b"BM":
        sys.exit("not a BMP")
    off = struct.unpack_from("<I", d, 10)[0]
    w, h = struct.unpack_from("<ii", d, 18)
    bpp = struct.unpack_from("<H", d, 28)[0]
    if bpp not in (24, 32):
        sys.exit(f"{bpp} bits per pixel not handled")
    bottom_up = h > 0
    h = abs(h)
    stride = (w * bpp // 8 + 3) & ~3
    n = bpp // 8
    rows = []
    for y in range(h):
        sy = h - 1 - y if bottom_up else y
        row = d[off + sy * stride:off + sy * stride + w * n]
        if alpha and n == 4:
            rows.append(b"\0" + bytes(row[3::4]))
        else:
            out = bytearray(w * 3)
            out[0::3], out[1::3], out[2::3] = row[2::n], row[1::n], row[0::n]
            rows.append(b"\0" + bytes(out))

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    color = 0 if alpha and n == 4 else 2
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, color, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(b"".join(rows))) + chunk(b"IEND", b"")
    open(dst, "wb").write(png)


if __name__ == "__main__":
    main()
