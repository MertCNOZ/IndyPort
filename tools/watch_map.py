"""Maps the writers INDY_WATCH logged (IndyET.dll+<offset>) to the recompiled functions they are in.

Usage: python tools/watch_map.py out/build/release/logs/indyet.log out/build/release/IndyET.dll
"""
import bisect
import re
import sys

sys.path.insert(0, __file__.rsplit("\\", 1)[0].rsplit("/", 1)[0])
from stack_scan import read_exports  # noqa: E402


def main():
    log, dll = sys.argv[1], sys.argv[2]
    exports = read_exports(dll)
    rvas = [r for r, _ in exports]
    dll_name = dll.replace("/", "\\").split("\\")[-1].lower()
    seen = {}
    for line in open(log, encoding="utf-8", errors="replace"):
        m = re.search(r"watch: (?:new writer )?(\S+)\+([0-9A-F]+) (?:writes ([0-9A-F]+)|(\d+) writes, first ([0-9A-F]+))", line)
        if not m or m.group(1).lower() != dll_name:
            continue
        off = int(m.group(2), 16)
        count = int(m.group(4)) if m.group(4) else seen.get(off, (0, ""))[0]
        first = m.group(3) or m.group(5)
        seen[off] = (max(count, seen.get(off, (0, ""))[0]), first)
    for off, (count, first) in sorted(seen.items(), key=lambda kv: -kv[1][0]):
        i = bisect.bisect_right(rvas, off) - 1
        name = f"{exports[i][1]}+{off - exports[i][0]:X}" if i >= 0 else "?"
        print(f"{count:8d}  {name:40s}  first write {first}")


if __name__ == "__main__":
    main()
