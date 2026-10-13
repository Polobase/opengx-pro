#!/usr/bin/env python3
"""Flash and RAM of a firmware image per module, from the GNU ld map file (what --gc-sections
kept), grouped for the nx budgets (docs/nx/fw2-design.md "Budgets"), with the image size checked
against NX_FLASH_LIMIT and the RAM left for the stack.

    python3 tools/nx_size.py build/nx/gxpro.map [--limit 112640] [--stack 3072]

Exit status 1 when the image exceeds the limit. Standard library only.
"""

import argparse
import re
import sys
from pathlib import Path

RAM_BYTES = 36 * 1024
ONE = re.compile(r"^ (\.\S+|COMMON)\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(\S+)\s*$")
NAME = re.compile(r"^ (\.\S+|COMMON)\s*$")
REST = re.compile(r"^\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(\S+)\s*$")
FILL = re.compile(r"^ \*fill\*\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)")

GROUPS = [
    ("ui engine", ("nx_lcd", "nx_lcd_port", "nx_gfx", "nx_render", "nx_anim", "nx_trans", "nx_hero", "nx_scope")),
    ("ui assets", ("nx_assets", "nx_font")),
    # nx_ui* covers the framework, nx_uiset, nx_uifmt and nx_uicore
    ("ui screens", ("nx_ui", "nx_comp", "nx_scr_")),
    ("nx platform", ("nx_",)),
    ("shared core", ("link", "storage", "factory", "calib", "spiflash", "fmt", "runner_seq", "core_", "state")),
    ("board", ("board", "startup", "lcd", "keys", "uart", "main_")),
    ("libgcc / libc", ("lib",)),
]


def module(obj):
    """build/nx/nx/nx_gfx.o -> nx_gfx; .../libgcc.a(_udivsi3.o) -> libgcc.a"""
    m = re.match(r".*/(lib[^/(]+\.a)\(", obj)
    if m:
        return m.group(1)
    return Path(obj).stem


def kind(section):
    if section.startswith((".text", ".rodata", ".isr_vector", ".ARM.exidx")):
        return "flash"
    if section.startswith(".data"):
        return "data"
    if section.startswith((".bss", "COMMON")):
        return "bss"
    if section.startswith(".noinit"):
        return "noinit"
    return None


def parse(text):
    body = text.split("Linker script and memory map", 1)[-1]
    sizes = {}
    pending = None
    for line in body.splitlines():
        f = FILL.match(line)
        if f:   # alignment padding between input sections
            addr, n = int(f.group(1), 16), int(f.group(2), 16)
            k = "flash" if 0x08000000 <= addr < 0x08020000 else "bss" if 0x20000000 <= addr < 0x20009000 else None
            if k and n:
                entry = sizes.setdefault("(alignment)", {"flash": 0, "data": 0, "bss": 0, "noinit": 0})
                entry[k] += n
            continue
        m = ONE.match(line)
        if m:
            sec, addr, size, obj = m.groups()
        else:
            m = NAME.match(line)
            if m:
                pending = m.group(1)
                continue
            m = REST.match(line) if pending else None
            if not m:
                pending = None
                continue
            sec, (addr, size, obj) = pending, m.groups()
            pending = None
        k, n = kind(sec), int(size, 16)
        if not k or not n or int(addr, 16) == 0:
            continue
        entry = sizes.setdefault(module(obj), {"flash": 0, "data": 0, "bss": 0, "noinit": 0})
        entry[k] += n
    return sizes


def group_of(name):
    for label, prefixes in GROUPS:
        if name.startswith(prefixes):
            return label
    return "other"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("map", help="linker map, e.g. build/nx/gxpro.map")
    ap.add_argument("--limit", type=int, default=112640,
                    help="NX_FLASH_LIMIT in bytes (default 112640, the build budget of ld/nx.ld; the flash limit is FLASH_PROVEN)")
    ap.add_argument("--stack", type=int, default=3072, help="stack reserve in bytes (default 3072)")
    ap.add_argument("--modules", action="store_true", help="list every module, not only the groups")
    args = ap.parse_args(argv)
    sizes = parse(Path(args.map).read_text())
    groups = {}
    for name, s in sizes.items():
        g = groups.setdefault(group_of(name), {"flash": 0, "data": 0, "bss": 0, "noinit": 0})
        for k in g:
            g[k] += s[k]
    flash = sum(s["flash"] + s["data"] for s in sizes.values())
    ram = sum(s["data"] + s["bss"] for s in sizes.values())
    noinit = sum(s["noinit"] for s in sizes.values())
    print(f"{'group':24} {'flash':>8} {'ram':>8}")
    order = [g for g, _ in GROUPS] + ["other"]
    for g in order:
        if g in groups:
            s = groups[g]
            print(f"{g:24} {s['flash'] + s['data']:8} {s['data'] + s['bss']:8}")
    if args.modules:
        print()
        for name, s in sorted(sizes.items(), key=lambda kv: -(kv[1]["flash"] + kv[1]["data"])):
            print(f"  {name:22} {s['flash'] + s['data']:8} {s['data'] + s['bss']:8}")
    print(f"{'total':24} {flash:8} {ram:8}")
    left = RAM_BYTES - ram - noinit - args.stack
    print(f"image {flash} of {args.limit} bytes (NX_FLASH_LIMIT): {args.limit - flash} left")
    print(f"ram {ram} + noinit {noinit} + stack {args.stack} of {RAM_BYTES} bytes: {left} left")
    if flash > args.limit:
        print("FAIL: the image is larger than NX_FLASH_LIMIT")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
