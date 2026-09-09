#!/usr/bin/env python3
"""Emit sh_src.h from sh.asm: a C string holding the shell's source."""

import sys

def main():
    if len(sys.argv) != 2:
        sys.stderr.write("usage: mk_sh_src.py <sh.asm> > sh_src.h\n")
        return 1
    with open(sys.argv[1], "r", encoding="utf-8") as f:
        lines = f.read().split("\n")
    out = []
    out.append("/* generated from sh.asm by tools/mk_sh_src.py — do not edit */")
    out.append("static const char sh_src[] =")
    for line in lines:
        esc = line.replace("\\", "\\\\").replace('"', '\\"')
        out.append('    "%s\\n"' % esc)
    out.append("    \"\\n\"")
    out.append(";")
    print("\n".join(out))
    return 0

if __name__ == "__main__":
    sys.exit(main())