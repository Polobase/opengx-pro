#!/usr/bin/env python3
"""Measure response times over the GX Pro's USB serial channels, to compare display firmwares.

Run it once per firmware with the device in PC mode (USB connected), then compare:

  gx_latency.py --save build/latency-ours.json
  gx_latency.py --save build/latency-original.json
  gx_latency.py compare build/latency-ours.json build/latency-original.json

Tests per channel (outputs stay off, nothing is stored on the device):
  display   ":n00=$"  -> ":Rev201"           answered by the display firmware
  set_freq  ":w24=<f>," -> ":ok"              answered by the generator board
  read_r11  ":r11=,"  -> ":r11=<value>."     answered by the generator board
  read_r12  ":r12=,"  -> ":r12=<value>."     answered by the generator board
  scan_step w24 + r11 + r12 back to back     time of one scan step without any pause

Only plain protocol commands are sent. A test that gets no reply is reported as such and
skipped after a few timeouts.
"""

from __future__ import annotations

import argparse
import json
import statistics as st
import sys
import time
from pathlib import Path

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial is missing: run this with .venv/bin/python")

WCH_VID = 0x1A86
TIMEOUT_S = 0.3
GIVE_UP_AFTER = 3          # consecutive timeouts before a test is skipped


def find_channels(requested):
    if requested:
        return requested
    ports = [p for p in list_ports.comports() if p.vid == WCH_VID]
    found = sorted(p.device for p in ports if "wchusbserial" in p.device) or sorted(p.device for p in ports)
    if not found:
        sys.exit("no WCH USB-serial channel found; is the GX Pro connected by USB?")
    return found


def encode_hz(hz: float) -> int:
    """Frequency encoding of the protocol with two decimals: m*10^(6-8) Hz."""
    return round(hz * 100) * 10 + 6


class Channel:
    def __init__(self, device: str):
        self.device = device
        s = serial.Serial()
        s.port, s.baudrate, s.timeout = device, 115200, 0
        s.dtr = s.rts = False
        s.open()
        self.ser = s
        self.buf = b""

    def close(self):
        self.ser.close()

    def request(self, line: str, expect: str) -> float | None:
        """Send line + CRLF, return seconds until a reply line starting with `expect`, or None."""
        self.ser.reset_input_buffer()
        self.buf = b""
        t0 = time.perf_counter()
        self.ser.write(line.encode("ascii") + b"\r\n")
        deadline = t0 + TIMEOUT_S
        while time.perf_counter() < deadline:
            chunk = self.ser.read(self.ser.in_waiting or 1)
            if not chunk:
                continue
            self.buf += chunk
            while b"\n" in self.buf:
                raw, self.buf = self.buf.split(b"\n", 1)
                if raw.decode("latin-1").strip().startswith(expect):
                    return time.perf_counter() - t0
        return None


def run_test(ch: Channel, name: str, n: int, action) -> dict:
    times, timeouts, streak = [], 0, 0
    for i in range(n):
        t = action(i)
        if t is None:
            timeouts += 1
            streak += 1
            if streak >= GIVE_UP_AFTER and not times:
                break
        else:
            streak = 0
            times.append(t)
    result = {"n": len(times), "timeouts": timeouts}
    if times:
        s = sorted(times)
        result.update(median_ms=st.median(s) * 1e3, p90_ms=s[int(len(s) * 0.9)] * 1e3,
                      min_ms=s[0] * 1e3, max_ms=s[-1] * 1e3)
    status = (f"median {result['median_ms']:6.2f} ms  p90 {result['p90_ms']:6.2f}  min {result['min_ms']:6.2f}  "
              f"max {result['max_ms']:6.2f}" if times else "no reply")
    print(f"  {name:10s} {status}   ({len(times)} ok, {timeouts} timeouts)")
    return result


def measure(devices, n: int) -> dict:
    results = {}
    for dev in devices:
        print(f"{dev}")
        ch = Channel(dev)
        try:
            base = 41000.0
            r = {}
            r["display"] = run_test(ch, "display", n, lambda i: ch.request(":n00=$", ":Rev"))
            r["set_freq"] = run_test(ch, "set_freq", n, lambda i: ch.request(f":w24={encode_hz(base + 10 * i)},", ":ok"))
            r["read_r11"] = run_test(ch, "read_r11", n, lambda i: ch.request(":r11=,", ":r11="))
            r["read_r12"] = run_test(ch, "read_r12", n, lambda i: ch.request(":r12=,", ":r12="))

            def step(i):
                t0 = time.perf_counter()
                for line, expect in ((f":w24={encode_hz(base + 10 * i)},", ":ok"), (":r11=,", ":r11="), (":r12=,", ":r12=")):
                    if ch.request(line, expect) is None:
                        return None
                return time.perf_counter() - t0

            if r["set_freq"]["n"] and r["read_r11"]["n"] and r["read_r12"]["n"]:
                r["scan_step"] = run_test(ch, "scan_step", n, step)
            results[dev] = r
        finally:
            ch.close()
    return results


def compare(a_path: Path, b_path: Path) -> int:
    a, b = json.loads(a_path.read_text()), json.loads(b_path.read_text())
    print(f"median response time: {a['label']} vs {b['label']}")
    for dev in sorted(set(a["results"]) | set(b["results"])):
        print(dev)
        ra, rb = a["results"].get(dev, {}), b["results"].get(dev, {})
        for test in ("display", "set_freq", "read_r11", "read_r12", "scan_step"):
            va, vb = ra.get(test, {}).get("median_ms"), rb.get(test, {}).get("median_ms")
            fa = f"{va:7.2f} ms" if va is not None else "   no reply"
            fb = f"{vb:7.2f} ms" if vb is not None else "   no reply"
            delta = f"{va - vb:+7.2f} ms" if va is not None and vb is not None else ""
            print(f"  {test:10s} {fa}   {fb}   {delta}")
    return 0


def main() -> int:
    if len(sys.argv) > 1 and sys.argv[1] == "compare":
        ap = argparse.ArgumentParser(prog="gx_latency.py compare")
        ap.add_argument("a", type=Path)
        ap.add_argument("b", type=Path)
        args = ap.parse_args(sys.argv[2:])
        return compare(args.a, args.b)
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", action="append", help="serial device, repeatable (default: all WCH channels)")
    ap.add_argument("-n", type=int, default=50, help="repetitions per test (default 50)")
    ap.add_argument("--save", type=Path, help="write the results as JSON")
    ap.add_argument("--label", help="name of this run (default: file name of --save)")
    args = ap.parse_args()
    results = measure(find_channels(args.port), args.n)
    if args.save:
        args.save.parent.mkdir(parents=True, exist_ok=True)
        label = args.label or args.save.stem
        args.save.write_text(json.dumps({"label": label, "time": time.strftime("%Y-%m-%d %H:%M:%S"),
                                         "results": results}, indent=1))
        print(f"saved {args.save}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
