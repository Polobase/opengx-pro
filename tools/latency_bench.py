#!/usr/bin/env python3
"""Latency benchmark for the GX Pro display firmware over USB serial (115200 baud).

Measures the round trip from sending a command line to receiving its reply line, 1000 times per
command by default, and writes every single measurement to a CSV file. Run it once per firmware
on the same unit, the same USB port and the same computer, then summarise all runs together:

  latency_bench.py run --firmware original-v7789
  latency_bench.py run --firmware opengx-pro-0.9.0 --image build/release/opengx-pro-0.9.0.bin
  latency_bench.py summarize build/latency/*.csv

Both firmwares answer the version query with the same text (":Rev201"; nx keeps the original's
replies byte for byte), so the device cannot tell which firmware it runs: --firmware is required,
and --image adds the SHA-256 of the image that was flashed. Every CSV row carries both, together
with the revision the device reported at the start of the run.

Commands (outputs stay off; nothing is stored on the device):
  display    ":n00=$"     -> ":Rev..."   answered by the display firmware (the comparison)
  set_freq   ":w24=<f>,"  -> ":ok"       answered by the generator board
  read_r11   ":r11=,"     -> ":r11=..."  answered by the generator board
  read_r12   ":r12=,"     -> ":r12=..."  answered by the generator board
The generator boards answer only inside a session of the PC software; without one these commands
time out and are skipped after three timeouts in a row. Run the benchmark on each firmware under
the same conditions so the generator rows are comparable too.

Between two requests the tool waits a random time (0..--gap-ms, seeded, logged) so the requests
do not lock onto the firmware's main-loop rhythm.

  burst      k copies of ":n00=$" in one write; counts how many are answered
             (commands sent back to back without waiting for the reply)

The measured time includes the USB-serial adapter and the host; it is the same for both firmwares
when both runs use the same setup.

  latency_bench.py selftest   runs the whole tool against a simulated device on a pseudo
                              terminal (temporary files only)
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import math
import os
import random
import statistics
import sys
import tempfile
import threading
import time
from datetime import datetime, timezone
from pathlib import Path

try:
    import serial
    from serial.tools import list_ports
except ImportError:              # summarize works without pyserial
    serial = list_ports = None


def need_serial():
    if serial is None:
        sys.exit("pyserial is missing: pip install pyserial (or use the project's .venv/bin/python)")

BAUD = 115200
WCH_VID = 0x1A86                    # the GX Pro's USB-serial bridge
TIMEOUT_S = 0.3
GIVE_UP_AFTER = 3                   # consecutive timeouts without any reply: skip the command
DEFAULT_N = 1000
DEFAULT_OUT = Path(__file__).resolve().parent.parent / "build" / "latency"
FIELDS = ["utc", "firmware", "device_revision", "image_sha256", "port", "test", "seq", "sent",
          "expect", "status", "latency_ms", "reply", "replies", "gap_ms", "seed"]
SUMMARY_FIELDS = ["firmware", "device_revision", "image_sha256", "port", "test", "runs", "ok",
                  "timeouts", "other", "p50_ms", "p95_ms", "p99_ms", "mean_ms", "jitter_sd_ms",
                  "spread_p99_p50_ms", "min_ms", "max_ms", "answered_pct", "first_utc", "last_utc",
                  "files"]


def encode_hz(hz: float) -> int:
    """The protocol's frequency encoding with two decimals (same as tools/gx_latency.py)."""
    return round(hz * 100) * 10 + 6


def now_utc() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def find_ports(requested: list[str] | None) -> list[str]:
    if requested:
        return requested
    ports = [p for p in list_ports.comports() if p.vid == WCH_VID]
    found = sorted(p.device for p in ports if "wchusbserial" in p.device) or sorted(p.device for p in ports)
    return found


class Link:
    def __init__(self, device: str):
        s = serial.serial_for_url(device, do_not_open=True)
        s.baudrate, s.timeout = BAUD, 0
        s.dtr = s.rts = False
        s.open()
        self.ser, self.buf = s, b""

    def close(self):
        self.ser.close()

    def _lines_until(self, deadline: float, want: int | None):
        """Collect reply lines until the deadline, or until `want` lines arrived."""
        got = []
        while time.perf_counter() < deadline and (want is None or len(got) < want):
            chunk = self.ser.read(self.ser.in_waiting or 1)
            if not chunk:
                continue
            self.buf += chunk
            while b"\n" in self.buf:
                raw, self.buf = self.buf.split(b"\n", 1)
                line = raw.decode("latin-1").strip()
                if line:
                    got.append((time.perf_counter(), line))
        return got

    def request(self, line: str) -> tuple[float | None, str]:
        """Send one line; seconds until the first reply line and that line, or (None, "")."""
        self.ser.reset_input_buffer()
        self.buf = b""
        t0 = time.perf_counter()
        self.ser.write(line.encode("ascii") + b"\r\n")
        got = self._lines_until(t0 + TIMEOUT_S, 1)
        return (got[0][0] - t0, got[0][1]) if got else (None, "")

    def burst(self, line: str, k: int) -> list[tuple[float, str]]:
        self.ser.reset_input_buffer()
        self.buf = b""
        t0 = time.perf_counter()
        self.ser.write((line.encode("ascii") + b"\r\n") * k)
        return [(t - t0, s) for t, s in self._lines_until(t0 + TIMEOUT_S, k)]


def sha256(path: Path | None) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest() if path else ""


def tests():
    base = 41000.0
    return [
        ("display", lambda _: ":n00=$", ":Rev"),
        ("set_freq", lambda i: f":w24={encode_hz(base + 10 * (i % 100))},", ":ok"),
        ("read_r11", lambda _: ":r11=,", ":r11="),
        ("read_r12", lambda _: ":r12=,", ":r12="),
    ]


def run(args) -> int:
    need_serial()
    ports = find_ports(args.port)
    if not ports:
        print("no GX Pro found (no WCH USB-serial port): nothing measured.")
        return 3
    if args.n < DEFAULT_N:
        print(f"note: -n {args.n} is below {DEFAULT_N}; fine for a smoke test, not for a comparison")
    image_hash = sha256(args.image)
    rng = random.Random(args.seed)
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    out = args.out or DEFAULT_OUT / f"{args.firmware}_{stamp}.csv"
    out.parent.mkdir(parents=True, exist_ok=True)
    selected = set(args.tests.split(",")) if args.tests else None
    with out.open("w", newline="") as f:
        w = csv.DictWriter(f, FIELDS)
        w.writeheader()
        for port in ports:
            link = Link(port)
            try:
                _, rev = link.request(":n00=$")
                rev = rev if rev.startswith(":Rev") else ""
                print(f"{port}: device revision {rev or '(no reply)'}, firmware label {args.firmware}")
                row0 = dict(firmware=args.firmware, device_revision=rev, image_sha256=image_hash, port=port,
                            seed=args.seed)
                for name, make, expect in tests():
                    if selected and name not in selected:
                        continue
                    ok = streak = 0
                    lat = []
                    for i in range(args.n):
                        gap = rng.uniform(0, args.gap_ms)
                        time.sleep(gap / 1e3)
                        sent = make(i)
                        t, reply = link.request(sent)
                        status = "timeout" if t is None else ("ok" if reply.startswith(expect) else "other")
                        w.writerow(dict(row0, utc=now_utc(), test=name, seq=i, sent=sent, expect=expect,
                                        status=status, latency_ms="" if t is None else f"{t * 1e3:.4f}",
                                        reply=reply, replies=0 if t is None else 1, gap_ms=f"{gap:.3f}"))
                        if status == "ok":
                            ok += 1
                            lat.append(t * 1e3)
                        streak = streak + 1 if t is None else 0
                        if streak >= GIVE_UP_AFTER and not ok:
                            print(f"  {name:9s} no reply, skipped")
                            break
                    else:
                        if lat:
                            q = percentiles(lat)
                            print(f"  {name:9s} n={len(lat):5d}  p50 {q[50]:7.2f}  p95 {q[95]:7.2f}  "
                                  f"p99 {q[99]:7.2f} ms  sd {statistics.pstdev(lat):6.2f}")
                        else:
                            print(f"  {name:9s} no expected reply in {args.n} runs")
                if args.burst:
                    for i in range(args.burst_n):
                        time.sleep(rng.uniform(0, args.gap_ms) / 1e3)
                        got = link.burst(":n00=$", args.burst)
                        answered = sum(1 for _, s in got if s.startswith(":Rev"))
                        w.writerow(dict(row0, utc=now_utc(), test=f"burst_k{args.burst}", seq=i,
                                        sent=f":n00=$ x{args.burst}", expect=":Rev",
                                        status="ok" if answered == args.burst else "lost",
                                        latency_ms=f"{got[-1][0] * 1e3:.4f}" if got else "",
                                        reply="|".join(s for _, s in got), replies=answered, gap_ms=""))
                    print(f"  burst_k{args.burst} written ({args.burst_n} bursts)")
            finally:
                link.close()
    write_settings(out, args, ports)
    print(f"saved {out}")
    return 0


def write_settings(out: Path, args, ports: list[str], note: str = "") -> None:
    """The run's settings next to its CSV (<name>.settings.json)."""
    import json
    info = dict(csv=out.name, baud=BAUD, runs_per_command=args.n, gap_ms_max=args.gap_ms, seed=args.seed,
                tests=args.tests or "display,set_freq,read_r11,read_r12", burst_k=args.burst, bursts=args.burst_n,
                reply_timeout_s=TIMEOUT_S, firmware=args.firmware, image=str(args.image or ""), ports=ports,
                pyserial=getattr(serial, "__version__", ""), python=sys.version.split()[0], note=note)
    out.with_suffix(".settings.json").write_text(json.dumps(info, indent=1) + "\n")


def percentiles(values: list[float]) -> dict[int, float]:
    """Linear-interpolated percentiles (the 'linear' method of numpy / Excel PERCENTILE.INC)."""
    s = sorted(values)

    def q(p):
        if len(s) == 1:
            return s[0]
        x = (len(s) - 1) * p / 100
        lo = math.floor(x)
        hi = min(lo + 1, len(s) - 1)
        return s[lo] + (s[hi] - s[lo]) * (x - lo)

    return {p: q(p) for p in (50, 95, 99)}


def summarize_rows(paths: list[Path]) -> list[dict]:
    groups: dict[tuple, dict] = {}
    for path in paths:
        with path.open(newline="") as f:
            for r in csv.DictReader(f):
                key = (r["firmware"], r["port"], r["test"])
                g = groups.setdefault(key, dict(rows=[], files=set(), revs=set(), hashes=set()))
                g["rows"].append(r)
                g["files"].add(path.name)
                g["revs"].add(r["device_revision"])
                g["hashes"].add(r["image_sha256"])
    out = []
    for (fw, port, test), g in sorted(groups.items()):
        rows = g["rows"]
        base = dict(firmware=fw, device_revision="|".join(sorted(g["revs"])),
                    image_sha256="|".join(sorted(h for h in g["hashes"] if h)), port=port, test=test,
                    runs=len(rows), first_utc=min(r["utc"] for r in rows), last_utc=max(r["utc"] for r in rows),
                    files="|".join(sorted(g["files"])))
        if test.startswith("burst_k"):
            k = int(test[len("burst_k"):])
            answered = sum(int(r["replies"] or 0) for r in rows)
            base.update(ok=sum(r["status"] == "ok" for r in rows), timeouts="", other="",
                        answered_pct=f"{100 * answered / (k * len(rows)):.2f}")
            out.append(base)
            continue
        lat = [float(r["latency_ms"]) for r in rows if r["status"] == "ok"]
        base.update(ok=len(lat), timeouts=sum(r["status"] == "timeout" for r in rows),
                    other=sum(r["status"] == "other" for r in rows), answered_pct="")
        if lat:
            q = percentiles(lat)
            base.update(p50_ms=f"{q[50]:.3f}", p95_ms=f"{q[95]:.3f}", p99_ms=f"{q[99]:.3f}",
                        mean_ms=f"{statistics.fmean(lat):.3f}", jitter_sd_ms=f"{statistics.pstdev(lat):.3f}",
                        spread_p99_p50_ms=f"{q[99] - q[50]:.3f}", min_ms=f"{min(lat):.3f}", max_ms=f"{max(lat):.3f}")
        out.append(base)
    return out


def summarize(args) -> int:
    paths = [p for p in args.csv if p.suffix == ".csv"]
    if not paths:
        sys.exit("no CSV files given")
    rows = summarize_rows(paths)
    with args.out.open("w", newline="") as f:
        w = csv.DictWriter(f, SUMMARY_FIELDS)
        w.writeheader()
        w.writerows(rows)
    for r in rows:
        if r.get("p50_ms"):
            print(f"{r['firmware']:18s} {Path(r['port']).name:28s} {r['test']:9s} n={r['ok']:>5}  "
                  f"p50 {r['p50_ms']:>8}  p95 {r['p95_ms']:>8}  p99 {r['p99_ms']:>8}  sd {r['jitter_sd_ms']:>7} ms")
        elif r.get("answered_pct"):
            print(f"{r['firmware']:18s} {Path(r['port']).name:28s} {r['test']:9s} answered {r['answered_pct']} %")
    print(f"saved {args.out}")
    return 0


def fake_device(fd: int, stop: threading.Event, rng: random.Random, busy_ms: float, drop_pending: bool):
    """A stand-in on a pseudo terminal: answers after a delay; optionally drops lines while busy."""
    buf = b""
    os.set_blocking(fd, False)
    while not stop.is_set():
        try:
            chunk = os.read(fd, 256)
        except BlockingIOError:
            time.sleep(0.0002)
            continue
        except OSError:
            return
        buf += chunk
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            line = raw.decode("latin-1").strip()
            time.sleep(rng.uniform(0.2, busy_ms) / 1e3)
            if drop_pending:            # whatever arrived while this line was pending is lost
                buf = b""
                try:
                    os.read(fd, 4096)
                except BlockingIOError:
                    pass
            reply = {":n00=$": ":Rev201", ":r11=,": ":r11=40449.", ":r12=,": ":r12=5189."}.get(line)
            if reply is None and line.startswith(":w24="):
                reply = ":ok"
            if reply:
                os.write(fd, reply.encode() + b"\r\n")


def selftest(args) -> int:
    import pty
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        files = []
        for label, busy, drop in (("sim-slow", 30.0, True), ("sim-fast", 3.0, False)):
            master, slave = pty.openpty()
            stop = threading.Event()
            th = threading.Thread(target=fake_device, args=(master, stop, random.Random(1), busy, drop), daemon=True)
            th.start()
            out = tmp / f"{label}.csv"
            ns = argparse.Namespace(port=[os.ttyname(slave)], firmware=label, image=None, n=args.n, gap_ms=5.0,
                                    seed=7, out=out, tests="display,set_freq", burst=2, burst_n=20)
            rc = run(ns)
            stop.set()
            os.close(master)
            os.close(slave)
            if rc:
                return rc
            files.append(out)
        summary = tmp / "summary.csv"
        summarize(argparse.Namespace(csv=files, out=summary))
        rows = list(csv.DictReader(summary.open()))
        by = {(r["firmware"], r["test"]): r for r in rows}
        assert float(by[("sim-fast", "display")]["p50_ms"]) < float(by[("sim-slow", "display")]["p50_ms"])
        assert int(by[("sim-fast", "display")]["ok"]) == args.n
        assert float(by[("sim-fast", "burst_k2")]["answered_pct"]) == 100.0
        assert float(by[("sim-slow", "burst_k2")]["answered_pct"]) < 100.0
        print("selftest passed")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run", help="measure the connected device")
    r.add_argument("--firmware", required=True, help="label of the flashed firmware, e.g. original-v7789, opengx-pro-0.9.0")
    r.add_argument("--image", type=Path, help="the image file that was flashed (its SHA-256 goes into every row)")
    r.add_argument("--port", action="append", help="serial port, repeatable (default: every WCH port)")
    r.add_argument("-n", type=int, default=DEFAULT_N, help=f"runs per command (default {DEFAULT_N})")
    r.add_argument("--gap-ms", type=float, default=20.0, help="random pause 0..GAP ms between requests (default 20)")
    r.add_argument("--seed", type=int, default=1, help="seed of the random pauses (default 1)")
    r.add_argument("--tests", help="comma list of display,set_freq,read_r11,read_r12 (default all)")
    r.add_argument("--burst", type=int, default=0, metavar="K", help="also send K version queries back to back")
    r.add_argument("--burst-n", type=int, default=200, help="number of bursts (default 200)")
    r.add_argument("--out", type=Path, help=f"CSV file (default {DEFAULT_OUT}/<firmware>_<time>.csv)")
    s = sub.add_parser("summarize", help="p50/p95/p99/jitter per firmware, port and command")
    s.add_argument("csv", type=Path, nargs="+")
    s.add_argument("--out", type=Path, default=DEFAULT_OUT.parent / "latency_summary.csv")
    t = sub.add_parser("selftest", help="run against a simulated device (no hardware, temporary files)")
    t.add_argument("-n", type=int, default=200)
    args = ap.parse_args()
    return {"run": run, "summarize": summarize, "selftest": selftest}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
