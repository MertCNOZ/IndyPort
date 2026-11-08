"""Search the Ghidra export (out/ghidra/export/<section>.c) for functions whose assembly matches patterns.

Usage: python tools/find_in_export.py <section.c> [<regex> ...] [--name=<part of name>] [--show]
Lists functions whose asm matches every regex (case-insensitive), with size and callers; --show prints the asm.
"""
import re
import sys


def functions(path):
    name, head, asm, in_asm = None, "", [], False
    for line in open(path, encoding="utf-8", errors="replace"):
        if line.startswith("// ==== "):
            if name:
                yield name, head, asm
            name, head, asm, in_asm = line[8:].strip(), "", [], False
        elif line.startswith("// callers:"):
            head = line.strip()
        elif line.startswith("/* asm"):
            in_asm = True
        elif line.startswith("*/"):
            in_asm = False
        elif in_asm:
            asm.append(line.rstrip())
    if name:
        yield name, head, asm


def main():
    show = "--show" in sys.argv
    only = next((a[7:] for a in sys.argv if a.startswith("--name=")), None)
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    pats = [re.compile(p, re.I) for p in args[1:]]
    for name, head, asm in functions(args[0]):
        if only and only.lower() not in name.lower():
            continue
        text = "\n".join(asm)
        if all(p.search(text) for p in pats):
            callers = head.replace("// callers: ", "")
            n = callers.count("@")
            print(f"{name}  ({len(asm)} insns, {n} callers)")
            if show:
                print(text)


if __name__ == "__main__":
    main()
