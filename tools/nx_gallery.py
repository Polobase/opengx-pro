#!/usr/bin/env python3
"""nx engine gallery: render the sheets of src/nx/nxg_sheets.c and the screens of
src/nx/nxg_screens.c, compare them with the approved baselines in tests/gallery/nx/, approve new
ones, and report.

  run --host                 host renderer (tests/host/build/nxg_host, < 1 s) -> build/gallery/host/
  run --host-dev             the screens alone, built with the image's engine switches
                             (src/nx/nx_config.mk; tests/host/build/nxg_host_dev) -> build/gallery/host-dev/
  run --emu [--variant V]    APP=nxgallery on the emulator (tools/emu.py --shot-hook nxg_shot:DIR)
                             -> build/gallery/emu/ (V = ds8: 8-bit SPI + REV16, polled: no DMA,
                             each in its own build directory -> build/gallery/emu-V/)
  compare [--source S]       S (host, host-dev, emu, emu-ds8, emu-polled) against the baselines,
                             pixel-exact; host-dev has no sheets: only its shots are compared
  crosscheck [A B]           two sources against each other (default host emu: driver bugs)
  approve [--source S] (--all | NAME...)   copy renders of S into tests/gallery/nx/
  report                     build/gallery/index.html: baseline, renders, differences at 2x

Baselines contain only open assets (Inter, Material Symbols, own drawings): they are committed.
Exit status 1 when a comparison finds a difference, a missing or an unapproved shot.
"""

import argparse
import html
import os
import shutil
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/gallery"
BASE = ROOT / "tests/gallery/nx"
PY = str(ROOT / ".venv/bin/python")
VARIANTS = {"ds16": ("", "build/nxgallery", "emu"),
            "ds8": ("-DNX_LCD_DS16=0", "build/nxgallery-ds8", "emu-ds8"),
            "polled": ("-DNX_LCD_DMA=0", "build/nxgallery-polled", "emu-polled")}
# host renderers by source: their target in tests/host/Makefile and its binary
HOSTS = {"host": ("gallery", "nxg_host"), "host-dev": ("gallery-dev", "nxg_host_dev")}
SCREENS_ONLY = {"host-dev"}   # sources that render the screens but not the sheets


def sh(cmd, **kw):
    return subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True, **kw)


def fresh(d):
    if d.exists():
        shutil.rmtree(d)
    d.mkdir(parents=True)


def run_host(source):
    target, binary = HOSTS[source]
    r = sh(["make", "-C", "tests/host", target])
    if r.returncode:
        print(r.stdout + r.stderr)
        return 1
    ppm, out = OUT / f"{source}-ppm", OUT / source
    fresh(ppm)
    fresh(out)
    r = sh([str(ROOT / "tests/host/build" / binary), str(ppm)])
    print(r.stdout.strip())
    if r.returncode:
        print(r.stderr)
        return 1
    for p in sorted(ppm.glob("*.ppm")):
        Image.open(p).convert("RGB").save(out / f"{p.stem}.png")
    print(f"{len(list(out.glob('*.png')))} renders in {out.relative_to(ROOT)}")
    return 0


def run_emu(variant, until):
    opts, build, source = VARIANTS[variant]
    cmd = ["make", "APP=nxgallery"] + ([f"NX_LCD_OPTS={opts}", f"BUILD={build}"] if opts else [])
    r = sh(cmd)
    if r.returncode:
        print(r.stdout + r.stderr)
        return 1
    raw, out = OUT / f"{source}-raw", OUT / source
    fresh(raw)
    fresh(out)
    bench = out / "bench.txt"
    r = sh([PY, "tools/emu.py", f"{build}/gxpro.elf", "--shot-hook", f"nxg_shot:{raw}", "--until", str(until),
            "--allow-tx", "--tx-log", str(bench)])
    bad = [ln for ln in r.stdout.splitlines() if ln.startswith("warning:") or "VIOLATIONS" in ln]
    if "power_off" not in r.stdout:
        bad.append(f"the gallery did not finish within {until} ms")
    for p in sorted(raw.glob("*.png")):
        name = p.stem.split("_", 1)[1]
        im = Image.open(p).convert("RGB")
        im.resize((im.width // 2, im.height // 2), Image.NEAREST).save(out / f"{name}.png")
    n = len(list(out.glob("*.png")))
    stop = [ln for ln in r.stdout.splitlines() if ln.startswith("stopped at")]
    print(f"{n} screenshots in {out.relative_to(ROOT)} ({variant}); {stop[0] if stop else 'no output'}")
    if bench.exists():
        for ln in bench.read_text().splitlines():
            print("  " + ln.split(": ", 1)[-1])
    for ln in bad:
        print("  " + ln)
    return 1 if bad or r.returncode else 0


def same(a, b):
    ia, ib = Image.open(a).convert("RGB"), Image.open(b).convert("RGB")
    if ia.size != ib.size:
        return False, None
    d = ImageChops.difference(ia, ib)
    return d.getbbox() is None, d


def heat(base, diff, path):
    """The baseline dimmed, every differing pixel red."""
    b = Image.open(base).convert("RGB").point(lambda v: v // 3)
    mask = diff.convert("L").point(lambda v: 255 if v else 0)
    b.paste((255, 0, 0), mask=mask)
    b.save(path)


def compare(source):
    cur = OUT / source
    diffdir = OUT / f"diff-{source}"
    fresh(diffdir)
    base = {p.stem: p for p in BASE.glob("*.png")}
    have = {p.stem: p for p in cur.glob("*.png")}
    if source in SCREENS_ONLY:
        base = {n: p for n, p in base.items() if n in have}
    problems, identical = [], 0
    for name in sorted(base):
        if name not in have:
            problems.append(f"{name}: missing in {source}")
            continue
        ok, d = same(base[name], have[name])
        if ok:
            identical += 1
        elif d is not None:
            heat(base[name], d, diffdir / f"{name}.png")
            gray = d.convert("L")
            n = gray.width * gray.height - gray.histogram()[0]
            problems.append(f"{name}: {n} pixels differ (build/gallery/diff-{source}/{name}.png)")
        else:
            problems.append(f"{name}: size differs")
    for name in sorted(set(have) - set(base)):
        problems.append(f"{name}: not approved (nx_gallery.py approve --source {source} {name})")
    for p in problems:
        print("  " + p)
    print(f"{identical}/{len(base)} identical ({source} vs tests/gallery/nx)")
    return 1 if problems or not base else 0


def crosscheck(a, b):
    da, db = OUT / a, OUT / b
    names = sorted(p.stem for p in da.glob("*.png"))
    other = {p.stem for p in db.glob("*.png")}
    bad = [n for n in names if n not in other or not same(da / f"{n}.png", db / f"{n}.png")[0]]
    bad += sorted(other - set(names))
    for n in bad:
        print(f"  {n}: differs or missing")
    print(f"{len(names) - len(bad)}/{len(names)} identical ({a} vs {b})")
    return 1 if bad or not names else 0


def approve(source, names, everything):
    cur = OUT / source
    BASE.mkdir(parents=True, exist_ok=True)
    pick = sorted(cur.glob("*.png")) if everything else [cur / f"{n}.png" for n in names]
    for p in pick:
        if not p.exists():
            print(f"  {p.stem}: no render in {source}")
            return 1
        shutil.copyfile(p, BASE / p.name)
    print(f"approved {len(pick)} from {source} into {BASE.relative_to(ROOT)}")
    return 0


def report():
    names = sorted({p.stem for d in [BASE, OUT / "host", OUT / "emu"] if d.exists() for p in d.glob("*.png")})
    cols = [("baseline", BASE), ("host", OUT / "host"), ("emu", OUT / "emu"), ("diff host", OUT / "diff-host"),
            ("diff emu", OUT / "diff-emu")]
    rows = []
    for n in names:
        cells = []
        for _, d in cols:
            p = d / f"{n}.png"
            src = html.escape(os.path.relpath(p, OUT))
            cells.append(f'<td><img src="{src}"></td>' if p.exists() else "<td></td>")
        rows.append(f"<tr><th>{html.escape(n)}</th>{''.join(cells)}</tr>")
    head = "".join(f"<th>{c}</th>" for c, _ in cols)
    page = f"""<!doctype html><meta charset="utf-8"><title>nx gallery</title>
<style>body{{background:#222;color:#ddd;font:13px sans-serif}}img{{width:640px;image-rendering:pixelated}}
td,th{{padding:4px;vertical-align:top}}</style>
<table><tr><th></th>{head}</tr>{''.join(rows)}</table>"""
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "index.html").write_text(page)
    print(f"{len(names)} shots: {(OUT / 'index.html').relative_to(ROOT)}")
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("--host", action="store_true")
    r.add_argument("--host-dev", action="store_true")
    r.add_argument("--emu", action="store_true")
    r.add_argument("--variant", choices=sorted(VARIANTS), default="ds16")
    r.add_argument("--until", type=int, default=120000, help="emulated ms at most (the app stops itself, after about 90 s)")
    c = sub.add_parser("compare")
    c.add_argument("--source", default="host")
    x = sub.add_parser("crosscheck")
    x.add_argument("a", nargs="?", default="host")
    x.add_argument("b", nargs="?", default="emu")
    a = sub.add_parser("approve")
    a.add_argument("--source", default="host")
    a.add_argument("--all", action="store_true")
    a.add_argument("names", nargs="*")
    sub.add_parser("report")
    args = ap.parse_args(argv)
    if args.cmd == "run":
        rc = 0
        if args.host or not (args.emu or args.host_dev):
            rc |= run_host("host")
        if args.host_dev:
            rc |= run_host("host-dev")
        if args.emu:
            rc |= run_emu(args.variant, args.until)
        return rc
    if args.cmd == "compare":
        return compare(args.source)
    if args.cmd == "crosscheck":
        return crosscheck(args.a, args.b)
    if args.cmd == "approve":
        if not args.all and not args.names:
            ap.error("approve needs --all or names")
        return approve(args.source, args.names, args.all)
    return report()


if __name__ == "__main__":
    sys.exit(main())
