#!/usr/bin/env python3
"""Benchmark Spooky2 biofeedback scans from a serial capture.

Reads a Device Monitoring Studio capture (.dmslog8) of one GX Pro USB-serial channel, splits it
into scans (a scan ends with ":w24=0," or when the frequency drops), and reports for each scan:
the baseline phase (start frequency held while ":r11"/":r12" are polled), the sweep (one
frequency per step, one reading per step), time per step and where that time goes (generator
replies vs. waits on the PC side).

  bfb_bench.py capture.dmslog8                    report
  bfb_bench.py capture.dmslog8 --csv readings.csv every reading: scan, phase, t, Hz, r11, r12
  bfb_bench.py capture.dmslog8 --range 41000:1800000   also project the time of a full sweep

Protocol seen in the captures: ":w24=<f>," sets the frequency (encoding: Hz = (v/10)·10^(v%10−8)),
":r11=," / ":r12=," read two measurements (Spooky2 BFB settings suggest mA and phase angle),
each command is answered by the generator board.
"""

from __future__ import annotations

import argparse
import csv
import math
import re
import statistics as st
import struct
import sys
from pathlib import Path


def dmslog8_records(path: Path) -> list[tuple[float, str, bytes]]:
    """(seconds since the first record, 'TX'|'RX', bytes) for every write and every completed read
    of the capture, as the PC's serial driver saw them (a line may span several records).

    Record layout as found in Device Monitoring Studio 8 serial logs (decoder from
    github.com/Polobase/freqgen): 24-byte header {u32 id, 4 zero-ish bytes, u64 FILETIME, u32 size, u32 type}
    followed by a body whose u32 at offset 13 is the IRP major function (4 = write, 3 = read)
    and whose data starts at offset 17. Records are located by the timestamp's top bytes.
    """
    b = path.read_bytes()
    sig = b[0x1D:0x20]                     # top 3 bytes of the header FILETIME (~30 h window)
    recs = {}
    for m in re.finditer(re.escape(sig), b):
        p = m.start() - 13
        if p < 0 or p + 24 > len(b):
            continue
        size, typ = struct.unpack_from("<II", b, p + 16)
        if not (24 <= size <= 70000) or typ not in (1, 3, 0x80000001, 0x80000003):
            continue
        if b[p + 4:p + 7] != b"\0\0\0" or p + size > len(b):
            continue
        recs.setdefault(struct.unpack_from("<I", b, p)[0], (p, size, typ))
    ids = sorted(recs)
    if not ids or ids[-1] - ids[0] + 1 != len(ids):
        raise ValueError(f"{path}: could not locate a gap-free record sequence")
    t0, out = None, []
    for rid in ids:
        p, size, typ = recs[rid]
        ts = struct.unpack_from("<Q", b, p + 8)[0]
        t0 = t0 if t0 is not None else ts
        body = b[p + 24:p + size]
        major = struct.unpack_from("<I", body, 13)[0]
        t = (ts - t0) / 1e7
        if typ == 1 and major == 4:
            out.append((t, "TX", body[17:]))
        elif typ == 0x80000001 and major == 3:
            out.append((t, "RX", body[17:]))
    return out


def decode_dmslog8(path: Path) -> list[tuple[float, str, str]]:
    """(seconds since the first record, 'TX'|'RX', line) for every line of the capture: a write
    is taken as one line, the reads are split at CR LF."""
    rx, lines = "", []
    for t, d, data in dmslog8_records(path):
        if d == "TX":
            lines.append((t, "TX", data.decode("latin-1").strip()))
        else:
            rx += data.decode("latin-1")
            while "\r\n" in rx:
                line, rx = rx.split("\r\n", 1)
                lines.append((t, "RX", line))
    return lines


def hz(value: str) -> float:
    v = int(value.rstrip(",. "))
    m, e = divmod(v, 10)
    return m * 10 ** e / 1e8


def reading(line: str) -> int:
    return int(line.split("=", 1)[1].rstrip(",. "))


def split_steps(lines):
    """Steps start at each ':w24='; every step collects its (t, r11, r12) readings."""
    steps, cur = [], None
    pending_r11 = None
    for t, d, s in lines:
        if d == "TX" and s.startswith(":w24="):
            cur = {"t": t, "hz": hz(s[5:]), "readings": []}
            steps.append(cur)
        elif cur and d == "RX" and s.startswith(":r11="):
            pending_r11 = (t, reading(s))
        elif cur and d == "RX" and s.startswith(":r12=") and pending_r11:
            cur["readings"].append((pending_r11[0], pending_r11[1], reading(s)))
            pending_r11 = None
    return steps


def split_scans(steps):
    scans, cur = [], []
    for s in steps:
        if s["hz"] == 0 or (cur and s["hz"] < cur[-1]["hz"]):
            if cur:
                scans.append(cur)
            cur = [] if s["hz"] == 0 else [s]
            continue
        cur.append(s)
    if cur:
        scans.append(cur)
    return scans


def step_breakdown(lines, t_from, t_to):
    """Median durations of the 6 phases of a sweep step: TX w24, RX ok, TX r11, RX r11, TX r12, RX r12, next TX."""
    names = ["w24 -> :ok (generator)", ":ok -> r11 sent (PC wait)", "r11 -> reply (generator)",
             "reply -> r12 sent (PC)", "r12 -> reply (generator)", "reply -> next w24 (PC)"]
    parts = [[] for _ in names]
    for i, (t, d, s) in enumerate(lines):
        if not (t_from <= t < t_to and d == "TX" and s.startswith(":w24=")) or i + 6 >= len(lines):
            continue
        seq = lines[i:i + 7]
        kinds = [(x[1], x[2][:4]) for x in seq]
        if kinds[:6] != [("TX", ":w24"), ("RX", ":ok"), ("TX", ":r11"), ("RX", ":r11"), ("TX", ":r12"), ("RX", ":r12")] \
                or not seq[6][2].startswith(":w24="):
            continue
        for k in range(6):
            parts[k].append(seq[k + 1][0] - seq[k][0])
    return [(n, st.median(p) if p else float("nan"), len(p)) for n, p in zip(names, parts)]


def ms(x):
    return f"{x * 1e3:6.1f} ms"


def report(path: Path, csv_path: Path | None, full_range: tuple[float, float] | None) -> int:
    lines = decode_dmslog8(path)
    steps = split_steps(lines)
    scans = split_scans(steps)
    print(f"{path.name}: {len(lines)} lines over {lines[-1][0] - lines[0][0]:.1f} s, {len(scans)} scan(s)")
    rows = []
    for n, scan in enumerate(scans, 1):
        baseline = [s for s in scan if len(s["readings"]) > 20]
        sweep = [s for s in scan if len(s["readings"]) <= 20]
        print(f"\n== scan {n}: {scan[0]['hz']:g} -> {scan[-1]['hz']:g} Hz")
        for b in baseline:
            end = next((s["t"] for s in scan if s["t"] > b["t"]), b["readings"][-1][0])
            r11 = [r[1] for r in b["readings"]]
            r12 = [r[2] for r in b["readings"]]
            gap = st.median(b["readings"][i + 1][0] - b["readings"][i][0] for i in range(len(b["readings"]) - 1))
            print(f"   baseline at {b['hz']:g} Hz: {end - b['t']:.1f} s, {len(r11)} readings every {ms(gap).strip()}, "
                  f"r11 {st.median(r11):g} (±{st.pstdev(r11):.1f}), r12 {st.median(r12):g} (±{st.pstdev(r12):.1f})")
        if len(sweep) > 1:
            durs = [sweep[i + 1]["t"] - sweep[i]["t"] for i in range(len(sweep) - 1)]
            ratio = st.median(sweep[i + 1]["hz"] / sweep[i]["hz"] for i in range(len(sweep) - 1))
            per_step = st.median(durs)
            print(f"   sweep: {len(sweep)} steps in {sweep[-1]['t'] - sweep[0]['t']:.1f} s, step +{(ratio - 1) * 100:.4f} %, "
                  f"time per step median {ms(per_step).strip()} (mean {ms(st.mean(durs)).strip()}, "
                  f"p90 {ms(sorted(durs)[int(len(durs) * .9)]).strip()})")
            print("   one sweep step, median:")
            gen = pc = 0.0
            for name, value, count in step_breakdown(lines, sweep[0]["t"], sweep[-1]["t"] + 1e-9):
                print(f"     {name:28s} {ms(value)}")
                if "generator" in name:
                    gen += value
                else:
                    pc += value
            print(f"     -> generator {ms(gen).strip()}, PC side {ms(pc).strip()} per step")
            if full_range:
                lo, hi = full_range
                n_steps = math.log(hi / lo) / math.log(ratio)
                print(f"   full sweep {lo:g} -> {hi:g} Hz at this pace: {n_steps:,.0f} steps, "
                      f"{n_steps * per_step / 60:.1f} min (+ baseline)")
        for phase, group in (("baseline", baseline), ("sweep", sweep)):
            for s in group:
                for t, r11, r12 in s["readings"]:
                    rows.append((n, phase, f"{t:.4f}", f"{s['hz']:.4f}", r11, r12))
    if csv_path:
        with open(csv_path, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["scan", "phase", "t_s", "hz", "r11", "r12"])
            w.writerows(rows)
        print(f"\n{len(rows)} readings written to {csv_path}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("capture", type=Path)
    ap.add_argument("--csv", type=Path, help="write every reading to this CSV file")
    ap.add_argument("--range", help="LO:HI in Hz: project the duration of a full sweep")
    args = ap.parse_args()
    full = tuple(float(x) for x in args.range.split(":")) if args.range else None
    return report(args.capture, args.csv, full)


if __name__ == "__main__":
    sys.exit(main())
