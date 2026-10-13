#!/usr/bin/env python3
"""Self-test of tools/diff.py's comparison rules on synthetic runs (no emulator, a few seconds):
--serial-only and the SPI-flash areas, resets, --timing, --baseline.

    .venv/bin/python tools/diff_selftest.py

The last line reads "diff_selftest: N checks, F failed".
"""

import argparse
import sys
import tempfile
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))

import diff as D  # noqa: E402

TESTS = []


def test(fn):
    TESTS.append(fn)
    return fn


def args(**kw):
    """diff.py's options with their defaults, overridden by kw."""
    a = dict(timing_tol_ms=None, tolerance_px=0, serial_only=False, timing="two-sided", baseline=None,
             update_baseline=False)
    a.update(kw)
    return argparse.Namespace(**a)


def run(uart=(), pb2=(), cps=(), ops=(), sectors=None, stop="time limit", stopped=10000.0, inputs=(), resets=()):
    """A result.json-like run. uart: (port, t_ms, text) transmitted lines; ops: (op, addr, len);
    sectors: {"0x019000": sha1}; inputs: times of input events; resets: reset causes."""
    return {"checkpoints": list(cps),
            "uart": [{"port": p, "dir": "tx", "t_ms": t, "text": x} for p, t, x in uart],
            "pb2": list(pb2),
            "spi_flash": {"ops": [{"t_ms": 0.0, "op": o, "addr": a, "len": n} for o, a, n in ops],
                          "changed": [], "sectors": dict(sectors or {})},
            "stop_reason": stop, "stopped_at_ms": stopped, "violations": [], "warnings": [],
            "inputs": [{"t_ms": t, "what": "input"} for t in inputs],
            "resets": [{"t_ms": 1.0, "cause": c} for c in resets]}


def screen(d, name, color=(0, 0, 0), backlight=0.5, dot=None):
    """A checkpoint entry and its 640x480 screenshot in directory d; dot: one white pixel there (odd
    coordinates: the 2:1 reduction to 320x240 samples those)."""
    Path(d).mkdir(parents=True, exist_ok=True)
    im = Image.new("RGB", (640, 480), color)
    if dot:
        im.putpixel(dot, (255, 255, 255))
    im.save(Path(d) / f"{name}.png")
    return {"name": name, "png": f"{name}.png", "backlight": backlight, "display_on": True, "sleep": False,
            "inversion": True}


def scenario(*checkpoints, **keys):
    return {"name": "synthetic", **keys,
            "timeline": [{"at": 1000 * (i + 1), "checkpoint": c} for i, c in enumerate(checkpoints)]}


def problems(sc, ref, cand, ref_dir=None, cand_dir=None, **kw):
    return D.compare(sc, ref, cand, ref_dir, cand_dir, args(**kw))[0]


@test
def serial_only_screens():
    with tempfile.TemporaryDirectory() as d:
        rd, cd = Path(d, "ref"), Path(d, "cand")
        sc = scenario("home")
        ref = run(cps=[screen(rd, "home")])
        cand = run(cps=[screen(cd, "home", dot=(21, 21))])
        strict = problems(sc, ref, cand, rd, cd)
        p, i = D.compare(sc, ref, cand, rd, cd, args(serial_only=True))
        dark = run(cps=[screen(cd, "home", backlight=0.0)])
        bl = problems(sc, ref, dark, rd, cd, serial_only=True)
    return [("a differing screen fails normally", any("screen home" in x for x in strict)),
            ("--serial-only: the pixels are informational", not p and any("--serial-only" in x for x in i)),
            ("--serial-only: backlight on/off stays strict", any("backlight on/off" in x for x in bl))]


@test
def serial_only_uart_and_flash():
    sc = scenario()
    legacy = ("erase4k", 0x19000, 4096)
    ref = run(uart=[("U1", 6000.0, ":ok\r\n")], ops=[legacy], sectors={"0x019000": "a"})
    wrong_reply = run(uart=[("U1", 6000.0, ":err\r\n")], ops=[legacy], sectors={"0x019000": "a"})
    nx_write = run(uart=[("U1", 6000.0, ":ok\r\n")], ops=[legacy, ("erase4k", 0x90000, 4096)],
                   sectors={"0x019000": "a", "0x090000": "n"})
    stray = run(uart=[("U1", 6000.0, ":ok\r\n")], ops=[legacy, ("erase4k", 0x40000, 4096)],
                sectors={"0x019000": "a", "0x040000": "x"})
    legacy_diff = run(uart=[("U1", 6000.0, ":ok\r\n")], ops=[legacy], sectors={"0x019000": "b"})
    p_nx, i_nx = D.compare(sc, ref, nx_write, None, None, args(serial_only=True))
    return [("--serial-only: UART lines stay strict",
             any("U1 tx" in x for x in problems(sc, ref, wrong_reply, serial_only=True))),
            ("--serial-only: changes in the nx area are informational",
             not p_nx and any("nx area" in x for x in i_nx)),
            ("without --serial-only the extra erase fails", bool(problems(sc, ref, nx_write))),
            ("--serial-only: a write outside the legacy and nx areas fails",
             any("outside the legacy and nx areas" in x for x in problems(sc, ref, stray, serial_only=True))),
            ("--serial-only: other content in a legacy sector fails",
             any("legacy sector 0x019000" in x for x in problems(sc, ref, legacy_diff, serial_only=True)))]


@test
def resets():
    sc = scenario()
    ref = run(resets=["software", "pin"])
    return [("the same resets pass", not problems(sc, ref, run(resets=["software", "pin"]))),
            ("different resets fail", any("resets differ" in x for x in problems(sc, ref, run(resets=["software"]))))]


@test
def timing_modes():
    sc = scenario(two_sided_before_ms=5000)
    boot = ("U1", 4500.0, ":w11=0,0,\r\n")
    ref = run(uart=[boot, ("U1", 6000.0, ":ok\r\n")], inputs=[5990.0])
    faster = run(uart=[boot, ("U1", 5900.0, ":ok\r\n")], inputs=[5990.0])
    slower = run(uart=[boot, ("U1", 6080.0, ":ok\r\n")], inputs=[5990.0])
    early_boot = run(uart=[("U1", 4400.0, ":w11=0,0,\r\n"), ("U1", 6000.0, ":ok\r\n")], inputs=[5990.0])
    other_keys = run(uart=[boot, ("U1", 6305.0, ":ok\r\n")], inputs=[6300.0])
    other_keys_slow = run(uart=[boot, ("U1", 6400.0, ":ok\r\n")], inputs=[6300.0])

    def bad(cand, **kw):
        return bool(problems(sc, ref, cand, **kw))
    return [("two-sided: 100 ms faster fails", bad(faster)),
            ("one-sided: 100 ms faster passes", not bad(faster, timing="one-sided")),
            ("one-sided: 80 ms slower fails", bad(slower, timing="one-sided")),
            ("one-sided: a line before two_sided_before_ms stays two-sided", bad(early_boot, timing="one-sided")),
            ("anchor: the same latency after a later input passes", not bad(other_keys, timing="anchor")),
            ("one-sided without anchors: that run fails", bad(other_keys, timing="one-sided")),
            ("anchor: 90 ms more latency than the reference fails", bad(other_keys_slow, timing="anchor"))]


@test
def baseline():
    with tempfile.TemporaryDirectory() as d:
        rd, cd, bd = Path(d, "ref"), Path(d, "cand"), Path(d, "base")
        sc = scenario("home")
        ref = run(cps=[screen(rd, "home", color=(255, 0, 0))])     # the reference's screen does not matter
        cand = run(cps=[screen(cd, "home")])
        missing = problems(sc, ref, cand, rd, cd, baseline=str(bd))
        p, info = D.compare(sc, ref, cand, rd, cd, args(baseline=str(bd), update_baseline=True))
        stored = bd / "synthetic" / "home.png"
        size = Image.open(stored).size if stored.exists() else None
        same = problems(sc, ref, cand, rd, cd, baseline=str(bd))
        changed = problems(sc, ref, run(cps=[screen(cd, "home", dot=(21, 21))]), rd, cd, baseline=str(bd))
        # same pixels as the approved baseline, but the backlight state disagrees with the reference
        dark = run(cps=[screen(cd, "home", backlight=0.0)])
        backlight_mismatch = problems(sc, ref, dark, rd, cd, baseline=str(bd), serial_only=True)
    return [("no approved baseline: fails and says how to approve", any("--update-baseline" in x for x in missing)),
            (f"--update-baseline stores the screen at 320x240 ({size})",
             not p and size == (320, 240) and any("baseline updated" in x for x in info)),
            ("the approved screen passes; the reference's screen is not compared", not same),
            ("one pixel off the baseline fails", any("vs baseline" in x for x in changed)),
            ("--serial-only --baseline: backlight on/off still strict against the reference",
             any("backlight on/off" in x for x in backlight_mismatch)),
            ("only apps without vendor graphics may store baselines",
             not D.baseline_allowed("build/fw/gxpro.elf") and D.baseline_allowed("build/nx/gxpro.elf"))]


def main():
    total = failed = 0
    for fn in TESTS:
        print(f"== {fn.__name__}")
        for text, ok in fn():
            print(f"{'ok  ' if ok else 'FAIL'} {text}")
            total += 1
            failed += not ok
    print(f"diff_selftest: {total} checks, {failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
