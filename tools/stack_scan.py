"""Lists, per thread of a minidump, the recompiled game functions found on its stack.

The recompiled guest code runs as ordinary host calls, so each host thread's stack holds return addresses into
the game code DLL. They are mapped to the DLL's exported sub_XXXXXXXX names (the guest address of the function).
Usage: python tools/stack_scan.py <dump.dmp> <game code dll> [max frames per thread]
"""
import bisect
import struct
import sys


def read_exports(dll_path):
    d = open(dll_path, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    opt = pe + 24
    magic = struct.unpack_from("<H", d, opt)[0]
    dd = opt + (112 if magic == 0x20B else 96)
    exp_rva, _ = struct.unpack_from("<II", d, dd)
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    sec_off = opt + struct.unpack_from("<H", d, pe + 20)[0]
    secs = [struct.unpack_from("<8sIIII", d, sec_off + i * 40) for i in range(nsec)]

    def off(rva):
        for _, vsize, va, rsize, raw in secs:
            if va <= rva < va + max(vsize, rsize):
                return raw + rva - va
        raise ValueError(hex(rva))

    o = off(exp_rva)
    n_funcs, n_names, a_funcs, a_names, a_ords = struct.unpack_from("<IIIII", d, o + 20)
    funcs = struct.unpack_from(f"<{n_funcs}I", d, off(a_funcs))
    names = struct.unpack_from(f"<{n_names}I", d, off(a_names))
    ords = struct.unpack_from(f"<{n_names}H", d, off(a_ords))
    out = []
    for nrva, ordinal in zip(names, ords):
        name = d[off(nrva):d.index(b"\0", off(nrva))].decode()
        out.append((funcs[ordinal], name))
    out.sort()
    return out


def parse_dump(path):
    d = open(path, "rb").read()
    if d[:4] != b"MDMP":
        sys.exit("not a minidump")
    nstreams, dir_rva = struct.unpack_from("<II", d, 8)
    streams = {}
    for i in range(nstreams):
        t, size, rva = struct.unpack_from("<III", d, dir_rva + i * 12)
        streams[t] = (size, rva)

    modules = []
    size, rva = streams[4]  # ModuleListStream
    n = struct.unpack_from("<I", d, rva)[0]
    for i in range(n):
        m = rva + 4 + i * 108
        base, msize = struct.unpack_from("<QI", d, m)
        name_rva = struct.unpack_from("<I", d, m + 20)[0]
        ln = struct.unpack_from("<I", d, name_rva)[0]
        name = d[name_rva + 4:name_rva + 4 + ln].decode("utf-16-le")
        modules.append((base, msize, name))

    threads = []
    size, rva = streams[3]  # ThreadListStream
    n = struct.unpack_from("<I", d, rva)[0]
    for i in range(n):
        t = rva + 4 + i * 48
        tid = struct.unpack_from("<I", d, t)[0]
        start, mem_size, mem_rva = struct.unpack_from("<QII", d, t + 24)
        ctx_size, ctx_rva = struct.unpack_from("<II", d, t + 40)
        rip = struct.unpack_from("<Q", d, ctx_rva + 0xF8)[0] if ctx_size >= 0x100 else 0
        rsp = struct.unpack_from("<Q", d, ctx_rva + 0x98)[0] if ctx_size >= 0x100 else start
        stack = d[mem_rva:mem_rva + mem_size]
        threads.append((tid, rip, rsp, start, stack))
    return modules, threads


def main():
    dump, dll = sys.argv[1], sys.argv[2]
    max_frames = int(sys.argv[3]) if len(sys.argv) > 3 else 25
    exports = read_exports(dll)
    rvas = [r for r, _ in exports]
    modules, threads = parse_dump(dump)
    dll_name = dll.replace("/", "\\").split("\\")[-1].lower()
    game = next(((b, s) for b, s, n in modules if n.lower().endswith(dll_name)), None)
    if not game:
        sys.exit(f"{dll_name} is not in the dump's module list")
    gbase, gsize = game

    def where(addr):
        for b, s, n in modules:
            if b <= addr < b + s:
                short = n.split("\\")[-1]
                if b == gbase:
                    i = bisect.bisect_right(rvas, addr - b) - 1
                    return f"{exports[i][1]}+{addr - b - exports[i][0]:X}" if i >= 0 else f"{short}+{addr - b:X}"
                return f"{short}+{addr - b:X}"
        return None

    for tid, rip, rsp, start, stack in threads:
        frames = []
        skip = max(0, rsp - start) if start <= rsp < start + len(stack) else 0
        for o in range(skip - skip % 8, len(stack) - 7, 8):
            v = struct.unpack_from("<Q", stack, o)[0]
            if gbase <= v < gbase + gsize:
                w = where(v)
                if not frames or frames[-1] != w:
                    frames.append(w)
            if len(frames) >= max_frames:
                break
        if not frames:
            continue
        print(f"thread {tid}: rip {where(rip) or hex(rip)}")
        for f in frames:
            print(f"    {f}")


if __name__ == "__main__":
    main()
