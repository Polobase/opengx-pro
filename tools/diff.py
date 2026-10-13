#!/usr/bin/env python3
"""Differential test: run scenarios on the original firmware (reference) and on a candidate ELF
on the same emulated board, and compare what the LCD shows and what goes out on the UARTs.

Compared per scenario (tools/scenarios/NAME.json, format in tools/sim/scenario.py):
  screens   every checkpoint, as seen on the glass (320x240 logical pixels: GRAM, display on/off,
            inversion vs. panel, backlight on/off). Differing pixels outside the checkpoint's
            masks must not exceed its tolerance_px (default 0 = pixel-exact). A diff image
            (reference | candidate | differences in magenta) is written for each mismatch.
  uart      the lines the display transmits, per port (U1, U2, U3): same text in the same order
            (exact), and each line's start time within timing_tol_ms of the reference's
            (default 50 ms; scenario key "timing_tol_ms" or --timing-tol-ms).
  pb2       PB2 level changes (generator-link ownership, power-off square wave): same sequence,
            same timing tolerance.
  spi flash erase operations (sector, in order) and the final content of every changed
            sector; a different split into page programs is only reported.
  stop      how the run ended (time limit, power_off, reset, fault) and when (timing tolerance).
  faults    any violation of the candidate (fault, internal-flash write, ...) fails the scenario.
  resets    the causes of the resets a run went through, in order (runs with "on_reset":
            "continue", "reboot" or "power_cut" events).
A checkpoint marked "informational": true (known, documented deviation) is reported but does
not fail the scenario.

--serial-only is for a candidate with its own UI (APP=nx): screen pixels and display states are
only reported; the UART lines, PB2, how the run ended, the resets, the backlight on/off state at
every checkpoint and the SPI flash in the legacy areas (names 0x005000, program slots
0x010000-0x02EFFF, calibration 0x074000-0x075FFF) stay strict; changes in the nx area
0x080000-0x1FFFFF are reported, writes anywhere else fail.

--timing chooses how times are judged (tolerance: timing_tol_ms):
  two-sided  |candidate - reference| within the tolerance (default; the classic firmware)
  one-sided  the candidate may be earlier by any amount, later only within the tolerance (faster
             is allowed, never slower); reference entries before the scenario's
             "two_sided_before_ms" stay two-sided (the boot sequence must not start earlier)
  anchor     one-sided on times measured from the latest input event (key press or release, PB1
             change, received line, reset) before each entry in its own run: for runs whose key
             presses differ (--keymap)

--baseline DIR compares the candidate's screens with its own approved screenshots
DIR/<scenario>/<checkpoint>.png (320x240) instead of the reference's, pixel-exact; the serial
comparison against the reference is unchanged. --update-baseline stores the candidate's screens
as the approved ones (review them first). Only apps without vendor graphics may store baselines
(tools/scenarios/baseline/<app>/; not fw, uitest, bringup, probe, linktest); none is committed today.

--keymap NAME expands the candidate's intent events (tools/scenarios/keymaps/NAME.json); the
reference always runs with the classic keymap. With a keymap other than classic use --timing anchor.

Exit code: 0 = all scenarios match, 1 = a mismatch beyond tolerance, 2 = harness problem
(reference run with violations, candidate missing).

Examples:
  tools/diff.py                                 # all scenarios, candidate build/fw/gxpro.elf
  tools/diff.py boot_home list_nav --candidate build/probe/gxpro.elf
  tools/diff.py --fresh-reference cal_boot
  tools/diff.py --all --jobs 6 --timing-tol-ms 100
  tools/diff.py --candidate build/nx/gxpro.elf --serial-only queries serial_storage
  tools/diff.py --candidate build/nx/gxpro.elf --serial-only --timing one-sided pc_mode
  tools/diff.py --candidate build/nx/gxpro.elf --serial-only --baseline tools/scenarios/baseline/nx boot_home
  tools/diff.py --candidate build/nx/gxpro.elf --serial-only --keymap nx --timing anchor ch1_program
Outputs: build/sim/diff/<scenario>/ (candidate run, *_diff.png, report.txt). The reference run
is stored in tools/scenarios/reference/<scenario>/ and reused while its fingerprint (scenario,
original image, tools/sim/names, SPI image, tools/sim sources) is unchanged (--fresh-reference forces).
"""

import argparse
import bisect
import difflib
import json
import os
import sys
import time
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

from PIL import Image, ImageChops

sys.path.insert(0, str(Path(__file__).resolve().parent))

from sim import scenario as S  # noqa: E402

FW_DIR = Path(__file__).resolve().parents[1]
DEFAULT_CANDIDATE = FW_DIR / "build" / "fw" / "gxpro.elf"
DEFAULT_TIMING_TOL_MS = 50.0
LEGACY_AREAS = ((0x005000, 0x006000), (0x010000, 0x02F000), (0x074000, 0x076000))   # names, slots, calibration
NX_AREA = (0x080000, 0x200000)                    # nx storage v2; the original never touches it
# apps whose screens are drawn with the vendor's fonts/bitmaps (their screenshots must not be committed)
VENDOR_UI_APPS = {"fw", "uitest", "bringup", "probe", "linktest", "sizeprobe", "nxbring"}


def baseline_allowed(candidate):
    """Approved screenshots are committed: only for apps without vendor graphics."""
    return Path(candidate).parent.name not in VENDOR_UI_APPS


def _run(job):
    name, target, out, keymap = job
    try:
        return S.run(S.load_scenario(name), target, out, keymap=keymap)
    except Exception as e:  # noqa: BLE001 - reported as harness error
        import traceback
        return {"error": f"{type(e).__name__}: {e}", "trace": traceback.format_exc(), "scenario": name,
                "target": target}


def _visible(res_dir, cp):
    im = Image.open(Path(res_dir) / cp["png"]).convert("RGB")
    return im.resize((320, 240), Image.NEAREST)


def _masked(x, y, masks):
    return any(x0 <= x <= x1 and y0 <= y <= y1 for x0, y0, x1, y1 in masks)


def compare_screen(ref_dir, cand_dir, rcp, ccp, masks, out_png):
    a, b = _visible(ref_dir, rcp), _visible(cand_dir, ccp)
    diff = ImageChops.difference(a, b).convert("L").point(lambda v: 255 if v else 0)
    bbox = diff.getbbox()
    pts = []
    if bbox:
        px = diff.load()
        pts = [(x, y) for y in range(bbox[1], bbox[3]) for x in range(bbox[0], bbox[2])
               if px[x, y] and not _masked(x, y, masks)]
    if pts:
        hl = a.convert("L").point(lambda v: v // 3).convert("RGB")
        hp = hl.load()
        for x, y in pts:
            hp[x, y] = (255, 0, 255)
        for x0, y0, x1, y1 in masks:
            for x in range(max(0, x0), min(319, x1) + 1):
                for y in (y0, y1):
                    if 0 <= y < 240:
                        hp[x, y] = (0, 160, 255)
        sheet = Image.new("RGB", (960 + 16, 240), (40, 40, 40))
        sheet.paste(a, (0, 0))
        sheet.paste(b, (328, 0))
        sheet.paste(hl, (656, 0))
        sheet.resize((sheet.width * 2, 480), Image.NEAREST).save(out_png)
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    where = f"X {min(xs)}..{max(xs)}, Y {min(ys)}..{max(ys)}" if pts else ""
    return len(pts), where


def compare_baseline(sc, ev, ccp, cand_dir, args):
    """One checkpoint against its approved screenshot: (problem or None, info or None)."""
    name = ev["checkpoint"]
    base_dir = Path(args.baseline) / sc["name"]
    base = base_dir / f"{name}.png"
    if args.update_baseline:
        base_dir.mkdir(parents=True, exist_ok=True)
        _visible(cand_dir, ccp).save(base)
        return None, f"screen {name}: baseline updated ({base})"
    if not base.exists():
        return (f"screen {name}: no approved baseline {base} (review {Path(cand_dir) / ccp['png']}, "
                f"then run with --update-baseline)"), None
    tol = ev.get("tolerance_px", sc.get("tolerance_px", args.tolerance_px))
    n, where = compare_screen(base_dir, cand_dir, {"png": base.name}, ccp, [],
                              Path(cand_dir) / f"{name}_baseline_diff.png")
    line = f"screen {name} @ {ev['at']} ms vs baseline: " + (f"{n} px differ ({where})" if n else "identical")
    if n > tol:
        return line + f" -> {name}_baseline_diff.png", None
    return None, line


class Timing:
    """How a candidate's times are judged against the reference's (see --timing in the docstring)."""

    MODES = ("two-sided", "one-sided", "anchor")

    def __init__(self, mode="two-sided", ref_inputs=(), cand_inputs=(), two_sided_before_ms=0):
        self.mode = mode
        self.two_sided_before = float(two_sided_before_ms or 0)
        self.inputs = {"ref": sorted(ref_inputs), "cand": sorted(cand_inputs)}

    def rel(self, side, t):
        """t in the compared frame: absolute, or since the latest input event of that run (anchor)."""
        if self.mode != "anchor":
            return t
        a = self.inputs[side]
        i = bisect.bisect_right(a, t)
        return t - (a[i - 1] if i else 0.0)

    def judge(self, t_ref, t_cand, tol):
        """(too far off, candidate - reference in the compared frame)."""
        d = self.rel("cand", t_cand) - self.rel("ref", t_ref)
        if self.mode == "two-sided" or t_ref < self.two_sided_before:
            return abs(d) > tol, d
        return d > tol, d


def compare_seq(ref, cand, key, tol, label, timing=None):
    """ref/cand: lists of (t_ms, text). Returns (problems, info) lines."""
    timing = timing or Timing()
    problems, info = [], []
    rt, ct = [key(x) for x in ref], [key(x) for x in cand]
    sm = difflib.SequenceMatcher(None, rt, ct, autojunk=False)
    worst = 0.0
    for op, i1, i2, j1, j2 in sm.get_opcodes():
        if op == "equal":
            for i, j in zip(range(i1, i2), range(j1, j2)):
                bad, d = timing.judge(ref[i][0], cand[j][0], tol)
                worst = max(worst, abs(d))
                if bad:
                    problems.append(f"{label} timing: {rt[i]!r} at {ref[i][0]:.1f} ms in the reference, "
                                    f"{cand[j][0]:.1f} ms in the candidate ({d:+.1f} ms, {timing.mode})")
        else:
            for i in range(i1, i2):
                problems.append(f"{label} - {ref[i][0]:9.1f} ms  {rt[i]!r}   (reference only)")
            for j in range(j1, j2):
                problems.append(f"{label} + {cand[j][0]:9.1f} ms  {ct[j]!r}   (candidate only)")
    if ref or cand:
        info.append(f"{label}: {len(ref)} reference / {len(cand)} candidate entries, "
                    f"max timing difference of matched entries {worst:.1f} ms ({timing.mode})")
    return problems, info


def flash_area(addr):
    """'legacy' (the original's names, program slots, calibration), 'nx' or 'other'."""
    if any(lo <= addr < hi for lo, hi in LEGACY_AREAS):
        return "legacy"
    return "nx" if NX_AREA[0] <= addr < NX_AREA[1] else "other"


def _op_lines(ops):
    return [f"{o[0]} 0x{o[1]:06X} {o[2]}" for o in ops]


def compare_flash(ref, cand, serial_only=False):
    """SPI flash: erase/status operations (which sectors, in which order) and the final content of
    every changed sector; how the data is split into page programs is only reported. serial_only:
    only the legacy areas are compared, changes in the nx area are reported, and a candidate that
    writes anywhere else fails."""
    problems, info = [], []
    rops = [(o["op"], o["addr"], o["len"]) for o in ref["spi_flash"]["ops"]]
    cops = [(o["op"], o["addr"], o["len"]) for o in cand["spi_flash"]["ops"]]
    rer = [o for o in rops if o[0] != "program"]
    cer = [o for o in cops if o[0] != "program"]
    if serial_only:
        for o in cer:
            if flash_area(o[1]) == "other":
                problems.append(f"SPI flash: candidate {o[0]} at 0x{o[1]:06X}, outside the legacy and nx areas")
        n_nx = sum(flash_area(o[1]) == "nx" for o in cer)
        if n_nx:
            info.append(f"SPI flash: {n_nx} candidate erase/status operation(s) in the nx area (informational)")
        rer = [o for o in rer if flash_area(o[1]) == "legacy"]
        cer = [o for o in cer if flash_area(o[1]) == "legacy"]
    for line in difflib.unified_diff(_op_lines(rer), _op_lines(cer), "reference", "candidate", lineterm="", n=1):
        problems.append(f"SPI flash erase/status ops {line}")
    rpg = [o for o in rops if o[0] == "program"]
    cpg = [o for o in cops if o[0] == "program"]
    if rpg != cpg:
        info.append(f"SPI flash page programs split differently: reference {len(rpg)} "
                    f"({sum(o[2] for o in rpg)} bytes), candidate {len(cpg)} ({sum(o[2] for o in cpg)} bytes)")
    if serial_only:
        rs, cs = ref["spi_flash"].get("sectors", {}), cand["spi_flash"].get("sectors", {})
        for key in sorted(set(rs) | set(cs)):
            if rs.get(key) == cs.get(key):
                continue
            area = flash_area(int(key, 16))
            if area == "legacy":
                problems.append(f"SPI flash content differs in legacy sector {key}")
            elif area == "nx":
                info.append(f"SPI flash sector {key} in the nx area changed (informational)")
            elif key in cs:
                problems.append(f"SPI flash sector {key} written by the candidate, outside the legacy and nx areas")
            else:
                info.append(f"SPI flash sector {key} changed by the reference only, outside the legacy areas")
    else:
        rch = {(x["start"], x["end"]): x["sha1"] for x in ref["spi_flash"]["changed"]}
        cch = {(x["start"], x["end"]): x["sha1"] for x in cand["spi_flash"]["changed"]}
        for rng in sorted(set(rch) | set(cch)):
            if rch.get(rng) != cch.get(rng):
                problems.append(f"SPI flash content differs in 0x{rng[0]:06X}..0x{rng[1] - 1:06X}")
    if rops or cops:
        info.append(f"SPI flash: {len(rops)} reference / {len(cops)} candidate program/erase operations")
    return problems, info


def compare(sc, ref, cand, ref_dir, cand_dir, args):
    tol_ms = args.timing_tol_ms if args.timing_tol_ms is not None else sc.get("timing_tol_ms", DEFAULT_TIMING_TOL_MS)
    timing = Timing(args.timing, [x["t_ms"] for x in ref.get("inputs", [])],
                    [x["t_ms"] for x in cand.get("inputs", [])], sc.get("two_sided_before_ms", 0))
    problems, info = [], []
    # screens
    rcps = {c["name"]: c for c in ref["checkpoints"]}
    ccps = {c["name"]: c for c in cand["checkpoints"]}
    for ev in [e for e in sc.get("timeline", []) if "checkpoint" in e]:
        name = ev["checkpoint"]
        rcp, ccp = rcps.get(name), ccps.get(name)
        if args.baseline:                               # the candidate's own approved screens
            if not ccp:
                problems.append(f"screen {name}: missing in the candidate")
                continue
            if rcp and (rcp["backlight"] > 0) != (ccp["backlight"] > 0):
                # --baseline only replaces the pixel comparison; the docstring promises the serial
                # comparison against the reference (backlight on/off included) is unchanged.
                problems.append(f"screen {name} @ {ev['at']} ms vs reference: state differs: backlight on/off")
            p, i = compare_baseline(sc, ev, ccp, cand_dir, args)
            problems += [p] if p else []
            info += [i] if i else []
            continue
        if not rcp or not ccp:
            problems.append(f"screen {name}: missing in {'reference' if not rcp else 'candidate'}")
            continue
        tol = ev.get("tolerance_px", sc.get("tolerance_px", args.tolerance_px))
        n, where = compare_screen(ref_dir, cand_dir, rcp, ccp, ev.get("mask", []),
                                  Path(cand_dir) / f"{name}_diff.png")
        states = [k for k in ("display_on", "sleep", "inversion") if rcp.get(k) != ccp.get(k)]
        backlight = (rcp["backlight"] > 0) != (ccp["backlight"] > 0)
        if backlight:
            states.append("backlight on/off")
        line = f"screen {name} @ {ev['at']} ms: " + (f"{n} px differ ({where})" if n else "identical")
        if states:
            line += f"; state differs: {', '.join(states)}"
        shown = line + (f" -> {name}_diff.png" if n else "")
        if (n > tol or states) and ev.get("informational"):
            info.append(shown + " [informational, see the scenario note]")
        elif (n > tol or states) and args.serial_only and not backlight:
            info.append(shown + " [informational: --serial-only]")
        elif n > tol or states:
            problems.append(shown)
        else:
            info.append(line + (f" (within tolerance {tol})" if n else ""))
    # uart lines per port
    for port in ("U1", "U2", "U3"):
        r = [(u["t_ms"], u["text"]) for u in ref["uart"] if u["port"] == port and u["dir"] == "tx"]
        c = [(u["t_ms"], u["text"]) for u in cand["uart"] if u["port"] == port and u["dir"] == "tx"]
        p, i = compare_seq(r, c, lambda x: x[1].rstrip("\r\n") + ("" if x[1].endswith("\n") else " <no newline>"),
                           tol_ms, f"{port} tx", timing)
        problems += p
        info += i
    # PB2
    p, i = compare_seq([(t, str(v)) for t, v in ref["pb2"]], [(t, str(v)) for t, v in cand["pb2"]],
                       lambda x: x[1], tol_ms, "PB2", timing)
    problems += p
    info += i
    # SPI flash
    p, i = compare_flash(ref, cand, args.serial_only)
    problems += p
    info += i
    # resets (runs that reboot and go on)
    rres = [x["cause"] for x in ref.get("resets", [])]
    cres = [x["cause"] for x in cand.get("resets", [])]
    if rres != cres:
        problems.append(f"resets differ: reference {rres}, candidate {cres}")
    # end of run
    if ref["stop_reason"] != cand["stop_reason"]:
        problems.append(f"run ended differently: reference {ref['stop_reason']} at {ref['stopped_at_ms']} ms, "
                        f"candidate {cand['stop_reason']} at {cand['stopped_at_ms']} ms")
    elif ref["stop_reason"] != "time limit" and timing.judge(ref["stopped_at_ms"], cand["stopped_at_ms"], tol_ms)[0]:
        problems.append(f"{ref['stop_reason']} at {ref['stopped_at_ms']} ms (reference) vs "
                        f"{cand['stopped_at_ms']} ms (candidate)")
    for v in cand["violations"]:
        problems.append(f"candidate violation: {v.strip()}")
    for w in cand["warnings"]:
        info.append(f"candidate warning: {w['text']} (first at {w['first_ms']} ms, {w['count']}x)")
    return problems, info


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("scenarios", nargs="*", help="scenario names (default: all in tools/scenarios/)")
    ap.add_argument("--all", action="store_true", help="all scenarios (the default without names)")
    ap.add_argument("--candidate", default=str(DEFAULT_CANDIDATE), help="candidate ELF (default: build/fw/gxpro.elf)")
    ap.add_argument("--jobs", type=int, default=min(8, os.cpu_count() or 2), help="parallel emulator runs")
    ap.add_argument("--out", default=str(FW_DIR / "build" / "sim" / "diff"), help="output directory")
    ap.add_argument("--timing-tol-ms", type=float, help=f"override the timing tolerance (default {DEFAULT_TIMING_TOL_MS:g} ms)")
    ap.add_argument("--tolerance-px", type=int, default=0, help="default pixel tolerance per checkpoint")
    ap.add_argument("--fresh-reference", action="store_true",
                    help="re-run the reference even if its stored result (tools/scenarios/reference/) is current")
    ap.add_argument("--serial-only", action="store_true",
                    help="candidate with its own UI (APP=nx): screens informational; UART, PB2, end of run, resets, "
                         "backlight on/off and the legacy SPI-flash areas strict")
    ap.add_argument("--timing", choices=Timing.MODES, default="two-sided",
                    help="how times are judged: two-sided (default), one-sided (earlier is fine), anchor "
                         "(one-sided, measured from the latest input event of each run)")
    ap.add_argument("--baseline", metavar="DIR",
                    help="compare the candidate's screens with approved PNGs DIR/<scenario>/<checkpoint>.png "
                         "(e.g. tools/scenarios/baseline/nx) instead of the reference's")
    ap.add_argument("--update-baseline", action="store_true",
                    help="with --baseline: store the candidate's screens as the approved ones")
    ap.add_argument("--keymap", default="classic",
                    help="key model for the candidate's intent events (tools/scenarios/keymaps/; the reference "
                         "always uses classic)")
    args = ap.parse_args()
    if args.update_baseline and not args.baseline:
        ap.error("--update-baseline needs --baseline DIR")
    if args.update_baseline and not baseline_allowed(args.candidate):
        print(f"refusing --update-baseline for {args.candidate}: its screens contain vendor graphics, "
              f"which must not be committed")
        return 2

    names = args.scenarios or sorted(p.stem for p in S.SCENARIO_DIR.glob("*.json"))
    cand = Path(args.candidate)
    if not cand.exists():
        print(f"candidate {cand} does not exist (build it, or pass --candidate)")
        return 2
    t0 = time.time()
    jobs = []
    for n in names:
        ref_dir = S.SCENARIO_DIR / "reference" / n
        stored = ref_dir / "result.json"
        current = (not args.fresh_reference and stored.exists() and
                   json.loads(stored.read_text()).get("fingerprint") == S.fingerprint(S.load_scenario(n), "reference"))
        if not current:
            jobs.append((n, "reference", str(ref_dir), "classic"))
        jobs.append((n, str(cand), str(Path(args.out) / n), args.keymap))
    cached = sum(1 for n in names) - sum(1 for j in jobs if j[1] == "reference")
    print(f"running {len(jobs)} emulator runs for {len(names)} scenario(s), {max(1, args.jobs)} in parallel "
          f"(original: {cached} stored result(s) reused); this takes a few minutes, reports follow at the end",
          flush=True)
    results = {}
    with ProcessPoolExecutor(max_workers=max(1, args.jobs)) as ex:
        futures = {ex.submit(_run, j): (j[0], j[1]) for j in jobs}
        for done, fut in enumerate(as_completed(futures), 1):
            name, target = futures[fut]
            results[(name, target)] = r = fut.result()
            label = "original" if target == "reference" else "ours"
            state = "error" if "error" in r else f"{r.get('wall_s', '?')} s"
            print(f"  [{done:2d}/{len(jobs)}] {name:16s} {label:8s} done ({state}, {time.time() - t0:.0f} s elapsed)",
                  flush=True)
    print(flush=True)

    worst = 0
    summary = []
    for n in names:
        sc = S.load_scenario(n)
        ref_dir, cand_dir = S.SCENARIO_DIR / "reference" / n, Path(args.out) / n
        ref = results.get((n, "reference")) or json.loads((ref_dir / "result.json").read_text())
        res = results[(n, str(cand))]
        lines = [f"== {n}: {sc.get('description', '')}"]
        if sc.get("note"):
            lines.append(f"  note: {sc['note']}")
        for r, label in ((ref, "reference"), (res, "candidate")):
            if "error" in r:
                lines.append(f"  {label} run failed: {r['error']}\n{r['trace']}")
        if any("error" in r for r in (ref, res)):
            worst = max(worst, 2)
            status = "ERROR"
        elif ref["violations"]:
            lines += [f"  reference violation: {v.strip()}" for v in ref["violations"]]
            worst, status = max(worst, 2), "ERROR"
        else:
            problems, info = compare(sc, ref, res, ref_dir, cand_dir, args)
            lines += [f"  {x}" for x in info]
            lines += [f"  MISMATCH {x}" for x in problems]
            status = "FAIL" if problems else "PASS"
            if problems:
                worst = max(worst, 1)
            lines.append(f"  wall time: reference {ref['wall_s']} s, candidate {res['wall_s']} s")
        lines.insert(1, f"  result: {status}")
        text = "\n".join(lines)
        print(text, flush=True)
        cand_dir.mkdir(parents=True, exist_ok=True)
        (cand_dir / "report.txt").write_text(text + "\n")
        summary.append((n, status))
    print(f"\n{sum(s == 'PASS' for _, s in summary)}/{len(summary)} scenarios pass "
          f"({', '.join(f'{n} {s}' for n, s in summary)}); {time.time() - t0:.0f} s; outputs in {args.out}")
    return worst


if __name__ == "__main__":
    sys.exit(main())
