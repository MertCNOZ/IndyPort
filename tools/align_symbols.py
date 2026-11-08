"""Guess missing XDK symbol addresses by aligning a reference game's symbol order with ours.

Library code is linked in the same order in every title, so between two symbols both games know, a missing
symbol sits at about the same relative position. Each guess snaps to the nearest function start Ghidra found.

Usage: python tools/align_symbols.py <reference game.toml> config/game.toml out/ghidra/export/functions.tsv
       [--test]   leave-one-out: hide each known symbol and check whether it is predicted back
"""
import bisect
import re
import sys


def load_symbols(path):
    syms, section = {}, None
    for line in open(path, encoding="utf-8"):
        m = re.match(r"^\[(\w+)\]", line)
        if m:
            section = m.group(1)
            continue
        m = re.match(r"^(\w+)\s*=\s*(0x[0-9A-Fa-f]+|0)\b", line)
        if m and section == "symbols":
            syms[m.group(1)] = int(m.group(2), 16)
    return syms


def load_functions(path):
    starts = []
    for line in open(path, encoding="utf-8").readlines()[1:]:
        starts.append(int(line.split("\t", 1)[0], 16))
    return sorted(starts)


def anchors_in_order(ref, ours):
    """Symbols both know, in reference address order, dropping any that break the order in ours."""
    common = sorted((ref[k], ours[k], k) for k in ref if ref[k] and ours.get(k))
    # longest increasing subsequence on our addresses keeps a consistent ordering
    tails, prev, idx = [], [-1] * len(common), []
    for i, (_, b, _) in enumerate(common):
        j = bisect.bisect_left(tails, b)
        if j == len(tails):
            tails.append(b)
            idx.append(i)
        else:
            tails[j] = b
            idx[j] = i
        prev[i] = idx[j - 1] if j else -1
    out, i = [], idx[-1] if idx else -1
    while i >= 0:
        out.append(common[i])
        i = prev[i]
    return out[::-1]


def predict(ref_addr, anchors, starts, max_gap=0x4000):
    ra = [a for a, _, _ in anchors]
    j = bisect.bisect_right(ra, ref_addr)
    if j == 0 or j == len(anchors):
        return None
    (a0, b0, _), (a1, b1, _) = anchors[j - 1], anchors[j]
    if a1 - a0 > max_gap or b1 <= b0:
        return None  # anchors too far apart to interpolate, likely a different library
    guess = b0 + (ref_addr - a0) * (b1 - b0) / (a1 - a0)
    k = bisect.bisect_left(starts, guess)
    cands = [s for s in starts[max(0, k - 1):k + 1] if b0 < s < b1]
    return min(cands, key=lambda s: abs(s - guess)) if cands else None


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    ref, ours, starts = load_symbols(args[0]), load_symbols(args[1]), load_functions(args[2])
    anchors = anchors_in_order(ref, ours)
    print(f"{len(anchors)} ordered anchors shared with the reference")

    if "--test" in sys.argv:
        ok = wrong = none = 0
        for i, (a, b, k) in enumerate(anchors):
            p = predict(a, anchors[:i] + anchors[i + 1:], starts)
            if p is None:
                none += 1
            elif p == b:
                ok += 1
            else:
                wrong += 1
                print(f"  wrong  {k}: predicted 0x{p:X}, actual 0x{b:X}")
        print(f"leave-one-out: {ok} right, {wrong} wrong, {none} no prediction")
        return

    for k in sorted(ref, key=ref.get):
        if ref[k] and not ours.get(k, 0) and k in ours:
            p = predict(ref[k], anchors, starts)
            if p:
                print(f"{k:<56} = 0x{p:06X}")


if __name__ == "__main__":
    main()
