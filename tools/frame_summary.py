"""Groups the draws of a frame capture (frame.txt, XBE_CAPTURE_DIR + F3) by state.

Usage: python tools/frame_summary.py <capture dir>/frame.txt [--list <field>=<value> ...]
Without --list: one row per distinct (target, prim, vs, ps, blend, zwrite, textures) with draw count and indices.
With --list: the draws whose fields match, e.g. --list blend="1 1 1".
"""
import re
import sys
from collections import defaultdict

LINE = re.compile(r"^(\d{4}) draw (.*)$")


def parse(path):
    for line in open(path, encoding="utf-8", errors="replace"):
        m = LINE.match(line.rstrip("\n"))
        if not m:
            continue
        idx, rest = int(m.group(1)), m.group(2)
        state, _, tex = rest.partition(" | ")
        f = {}
        for key in ("rt", "ds", "prim", "count", "vs", "ps", "zwrite", "cull"):
            mm = re.search(rf"\b{key} (\S+)", state)
            f[key] = mm.group(1) if mm else ""
        mm = re.search(r"\bblend (\S+ \S+ \S+)", state)
        f["blend"] = mm.group(1) if mm else ""
        mm = re.search(r"\bzen (\S+ \S+)", state)
        f["zen"] = mm.group(1) if mm else ""
        f["tex"] = " | ".join(t.strip() for t in tex.split(" | ") if t.strip()) if tex else "-"
        f["tex_short"] = ", ".join(re.sub(r"^t(\d) \S+ fmt (\S+) (\S+)$", r"t\1:\2 \3", t.strip())
                                   for t in tex.split(" | ") if t.strip()) if tex else "-"
        yield idx, f


def main():
    path = sys.argv[1]
    want = {}
    if "--list" in sys.argv:
        for a in sys.argv[sys.argv.index("--list") + 1:]:
            k, _, v = a.partition("=")
            want[k] = v
    if want:
        for idx, f in parse(path):
            if all(f.get(k) == v for k, v in want.items()):
                print(f"{idx:04d} prim {f['prim']} count {f['count']} vs {f['vs']} ps {f['ps']} blend {f['blend']} "
                      f"zw {f['zwrite']} {f['tex_short']}")
        return
    groups = defaultdict(list)
    for idx, f in parse(path):
        key = (f["rt"], f["vs"], f["ps"], f["blend"], f["zwrite"], f["tex_short"].split(",")[0])
        groups[key].append(idx)
    for key, idxs in sorted(groups.items(), key=lambda kv: kv[1][0]):
        rt, vs, ps, blend, zw, t0 = key
        span = f"{idxs[0]:04d}-{idxs[-1]:04d}" if len(idxs) > 1 else f"{idxs[0]:04d}"
        print(f"{len(idxs):4d} draws {span:9s} rt {rt} vs {vs} ps {ps} blend {blend:11s} zw {zw} {t0}")


if __name__ == "__main__":
    main()
