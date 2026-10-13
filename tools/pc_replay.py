#!/usr/bin/env python3
"""Replay a Spooky2 serial capture on the emulated nx firmware.

  pc_replay.py replay CAPTURE.dmslog8 [--elf build/nx/gxpro.elf] [--out build/replay/NAME]
      What does the display show while Spooky2 runs a preset? The capture's PC -> device lines
      (Device Monitoring Studio, one GX Pro serial channel) go into the emulated board in PC mode
      with their timing (idle gaps shortened), the LCD is saved every --every seconds, and each
      shot is listed with what the PC had set by then: the wire frequency (:w24), the cycles of
      the wave table the PC uploaded (:aNN) and selected (:w20), their product (the frequency
      Spooky2 shows), and the frequency the firmware holds for its PC monitor.

      Default window: from the preset's name line before its first run to 45 s into that run
      (the emulator is several times slower than real time); --from / --to in capture seconds.

  pc_replay.py standalone CAPTURE.dmslog8 [--elf build/nx/gxpro.elf] [--out build/replay/NAME]
      Does the firmware's built-in preset send what Spooky2 sent? The image boots standalone, the
      keys open menu > Chains and start the preset (--keys), and what the firmware sends to the
      generator is checked against the capture of Spooky2 playing the same preset:
        - the two wave tables (":a11=", ":a25=") byte for byte
        - the generator's settings once the outputs are on (:w11 :w12 :w14 :w17 :w20 :w21 :w28
          :w29 :w32 :w33), whatever the order the two sent them in
        - the first :w24 of every step byte for byte, and the steps' length
        - every :w24 of a sweep on the line Spooky2's sweep draws (within 0.2 s of its slope)
      LCD shots of the list and of the dashboard in a sweep and in a fixed step go to --out.

Only the PC's lines are replayed: the generator's replies (":ok") mean nothing to the display in
PC mode. The capture is the user's own and is read at run time; nothing of it is stored here.
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import bfb_bench  # noqa: E402
from sim import scenario as S  # noqa: E402
from sim.firmware import load  # noqa: E402
from sim.machine import Board, BoardConfig, Inputs  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
PC_READY_MS = 5000.0     # nx enters PC mode at NX_GEN_READY_MS (4440 ms) when PB1 is high at power-on
CH_SIZE, PC_FREQ_OFF = 0x9B0, 0x9A0   # struct channel (src/state.h): its size, and pc_freq[2] in it
TABLE_N = 1024
# struct nx_pcmon (src/nx/nx_diag.h): cycles[2][16] after wave, ampl_cv, gate and seen
PCMON_TAB0, PCMON_TABS, PCMON_CYCLES_OFF = 11, 16, 15
DIAG_ISR_MAX_OFF = 104   # struct nx_diag (src/nx/nx_diag.h): isr_max_us after the 26 counters of 4 bytes


def hz(code: int) -> float:
    """Frequency code v = m * 10 + e: Hz = m * 10^(e - 8) (docs/serial-commands.md)."""
    m, e = divmod(code, 10)
    return m * 10 ** e / 1e8


def table_cycles(v: list[int]) -> int:
    """Wave cycles in a 1024-sample table, as src/nx/nx_diag.c counts them from the upward
    crossings of 512 (circular): 2 to 180 crossings whose gaps (the one around the table's end
    too) are within 2 samples of each other are that many cycles; 3 to 64 crossings that all lie
    on one grid of c steps are c cycles (Spooky2's H-Bomb tables: 8 cycles in 16 slots); else 1."""
    if len(v) != TABLE_N:
        return 1
    cross = [i for i in range(TABLE_N) if v[i - 1] < 512 <= v[i]]
    n = len(cross)
    if n < 2:
        return 1
    gaps = [(cross[(j + 1) % n] - cross[j]) % TABLE_N for j in range(n)]
    if n <= 180 and max(gaps) - min(gaps) <= 2:
        return n
    if not 3 <= n <= 64:
        return 1
    c0 = (TABLE_N + min(gaps) // 2) // min(gaps)
    for c in (c0, c0 - 1, c0 + 1):
        if not 2 <= c <= 180:
            continue
        ks = [(g * c + TABLE_N // 2) // TABLE_N for g in gaps]
        if all(abs(g * c - k * TABLE_N) <= 2 * c for g, k in zip(gaps, ks)) and sum(ks) == c:
            return c
    return 1


def values(line: str) -> list[int]:
    return [int(x) for x in line.split("=", 1)[1].replace(".", ",").split(",") if x.strip().isdigit()]


class PcState:
    """What the PC has set, from its lines in order."""

    def __init__(self):
        self.cycles = {}                 # table number -> cycles
        self.wave = [0, 0]               # :w20 / :w21
        self.code = [0, 0]               # :w24 / :w25
        self.sync = 0
        self.last = ""

    def feed(self, line: str):
        head = line[:5]
        if head[:2] == ":a" and head[2:4].isdigit():
            self.cycles[int(head[2:4])] = table_cycles(values(line))
            return
        if head[:2] != ":w" or not head[2:4].isdigit():
            return
        n, v = int(head[2:4]), values(line)
        if not v:
            return
        if n in (20, 21):
            self.wave[n - 20] = v[0]
        elif n in (24, 25):
            self.code[n - 24] = v[0]
            if n == 24:
                self.last = line
        elif n == 14:
            self.sync = v[0]

    def mult(self, out: int = 0) -> int:
        return self.cycles.get(self.wave[out], 1)


def pc_lines(capture: Path) -> list[tuple[float, bytes]]:
    """(capture seconds, bytes) of every write of the PC."""
    return [(t, data) for t, d, data in bfb_bench.dmslog8_records(capture) if d == "TX"]


def default_window(tx: list[tuple[float, bytes]]) -> tuple[float, float]:
    """From the name line (:n00=...) before the first run (:w11=1...) to 45 s into that run."""
    run = next((i for i, (_, d) in enumerate(tx) if d.startswith(b":w11=1")), None)
    if run is None:
        return tx[0][0], tx[-1][0]
    start = run
    for i in range(run, -1, -1):
        if tx[i][1].startswith(b":n00=") and not tx[i][1].startswith(b":n00=$"):
            start = i
            break
    return tx[start][0], tx[run][0] + 45.0


def new_board(elf: Path, pb1: int, keys=()):
    """The board with the firmware, the newest SPI-flash backup (else an erased chip), generators
    that answer, PB1 (the PC) and key presses (ms, KEY), each held 150 ms."""
    fw = load(str(elf))
    try:
        spi = S.newest_backup().read_bytes()
    except FileNotFoundError:
        spi = b"\xff" * 0x200000
    cfg = BoardConfig(reference=fw.reference, spi_image=spi, allow_tx=True, allow_spi_write=True,
                      generators={"U1": 1.0, "U2": 1.0}, stop_on_power_off=True)
    inputs = Inputs(pb1=pb1)
    for t, k in keys:
        inputs.add_key(t, t + 150.0, k)
    return Board(fw, cfg, inputs)


def shot(b, path: Path):
    path.parent.mkdir(parents=True, exist_ok=True)
    b.lcd.image(backlight=b.backlight(), full=b.cfg.bl_full).save(path)


def fw_pc_freq(b, ch: int, out: int) -> int:
    addr = b.fw.syms["g_ch"] + ch * CH_SIZE + PC_FREQ_OFF + out * 8
    return struct.unpack("<Q", bytes(b.uc.mem_read(addr, 8)))[0]


def fw_cycles(b, ch: int, table: int) -> int:
    """The cycles the firmware's receive tap found for a table (struct nx_pcmon, src/nx/nx_diag.h)."""
    if not 0 <= table - PCMON_TAB0 < PCMON_TABS:
        return 0
    return b.uc.mem_read(b.fw.syms["g_nx_pcmon"] + PCMON_CYCLES_OFF + ch * PCMON_TABS + table - PCMON_TAB0, 1)[0]


def finish(b, t_wall: float) -> int:
    b.flush_lines()
    for w, (first, n) in b.warnings.items():
        print(f"warning: {w} (first at {first} ms, {n}x)")
    print(f"stopped at {b.ms:.0f} ms ({b.stop_reason}), {time.time() - t_wall:.0f} s wall")
    for v in b.violations[:20]:
        print("VIOLATION " + v)
    return 1 if b.violations else 0


def replay(args) -> int:
    tx = pc_lines(args.capture)
    if not tx:
        print(f"{args.capture}: no PC lines found")
        return 2
    t_from, t_to = default_window(tx)
    t_from = args.t_from if args.t_from is not None else t_from
    t_to = args.t_to if args.t_to is not None else t_to
    st = PcState()
    for t, d in tx:                       # what the PC had set before the window
        if t < t_from:
            st.feed(d.decode("latin-1").strip())
    win = [(t, d) for t, d in tx if t_from <= t <= t_to]
    if not win:
        print(f"no PC lines between {t_from:.1f} s and {t_to:.1f} s")
        return 2

    # capture time -> replay time: idle gaps longer than --max-gap are shortened to it
    sched, r, prev = [], PC_READY_MS, win[0][0]
    for t, d in win:
        r += min(t - prev, args.max_gap) * 1000.0
        prev = t
        sched.append((r, t, d))
    end_ms = sched[-1][0] + 500.0

    b = new_board(args.elf, pb1=1)
    port, ch = args.port, 0 if args.port == "U1" else 1
    out = args.out
    rows, state = [], {"i": 0}

    def inject(k):
        b.inject(port, b.ms, sched[k][2])
        st.feed(sched[k][2].decode("latin-1").strip())
        state["i"] = k

    for k, (r, _, _) in enumerate(sched):
        b.at(r, lambda k=k: inject(k))

    def take(n, r):
        k = state["i"]
        t_cap = min(sched[k][1] + (r - sched[k][0]) / 1000.0, sched[k + 1][1] if k + 1 < len(sched) else t_to)
        name = f"shot_{n:03d}_{t_cap:07.2f}s.png"
        shot(b, out / name)
        wire, mult = hz(st.code[0]), st.mult(0)
        fw = fw_pc_freq(b, ch, 0)
        rows.append(f"{name}  {st.last or '(no :w24 yet)':24s} wire {wire:14.8f} Hz  table {st.wave[0]:2d} x{mult:<3d}"
                    f" Spooky2 {wire * mult:12.6f} Hz  firmware pc_freq {hz(fw):14.8f} Hz")

    n, r = 0, sched[0][0] + args.every * 1000.0
    while r < end_ms:
        b.at(r, lambda n=n, r=r: take(n, r))
        n, r = n + 1, r + args.every * 1000.0

    print(f"{args.capture.name}: {len(win)} PC lines from {t_from:.2f} s to {t_to:.2f} s on {port} of {args.elf}, "
          f"{(end_ms - PC_READY_MS) / 1000:.1f} s of replay, {n} shots -> {out}")
    t_wall = time.time()
    b.run(end_ms)
    wrong = 0
    for tab, want in sorted(st.cycles.items()):   # every table the PC uploaded: the tap's cycles
        got = fw_cycles(b, ch, tab)
        if PCMON_TAB0 <= tab < PCMON_TAB0 + PCMON_TABS:
            wrong += got != want
            rows.append(f"table {tab:2d}: x{want:<3d} firmware x{got:<3d} {'ok' if got == want else 'DIFFERENT'}")
    isr = struct.unpack("<H", bytes(b.uc.mem_read(b.fw.syms["g_nx_diag"] + DIAG_ISR_MAX_OFF, 2)))[0]
    rows.append(f"longest interrupt handler: {isr} us of emulator time (a table's cycles are worked out in the receive interrupt)")
    report = "\n".join(rows)
    print(report)
    out.mkdir(parents=True, exist_ok=True)
    (out / "report.txt").write_text(report + "\n")
    return finish(b, t_wall) or (1 if wrong else 0)


# ---- standalone: the built-in preset against the capture ----

REGS = (11, 12, 14, 17, 20, 21, 28, 29, 32, 33)
# after the boot gate (4.44 s): DOWN opens the menu (on Settings after power-on), UP and G1 go to
# Chains, START opens it, six DOWN reach the first row after the plans, START plays it
KEYS = "DOWN,UP,G1,START,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,START"
KEY_T0_MS, KEY_STEP_MS = 6000.0, 700.0


def reg_state(lines: list[str]) -> dict:
    """The generator's registers after the lines: an empty value keeps what it was (:w11=1,,)."""
    st = {}
    for line in lines:
        if not re.fullmatch(r":w\d\d=.*", line):
            continue
        n = int(line[2:4])
        vals = line[5:].split(",")[:-1]
        cur = list(st.get(n, ()))
        for i, v in enumerate(vals):
            if v.strip() == "":
                continue
            while len(cur) <= i:
                cur.append(None)
            cur[i] = int(v)
        st[n] = tuple(cur)
    return st


def tx_lines_of(b, port: str) -> list[tuple[float, str]]:
    """(ms, line) of what the firmware sent on port; the board hands long lines over in pieces."""
    out, cur, t0 = [], "", None
    for u in b.uart_lines:
        if u["port"] != port or u["dir"] != "tx":
            continue
        if t0 is None:
            t0 = u["t_ms"]
        cur += u["text"]
        if cur.endswith("\n"):
            out.append((t0, cur.strip()))
            cur, t0 = "", None
    return out


def capture_run(tx: list[tuple[float, bytes]]):
    """Of the capture: the lines before the first sweep line of its first run, the table lines,
    and the (seconds, Hz) of every :w24 of its second pass (the first starts late: Spooky2's
    clock runs while it sets the generator up) with the time and code of each step's start."""
    lines = [(t, d.decode("latin-1").strip()) for t, d in tx]
    on = next(i for i, (_, s) in enumerate(lines) if s.startswith(":w11=1"))
    setup_end = next(i for i in range(on, len(lines)) if lines[i][1].startswith(":w29="))
    tables = {s[:4]: s for _, s in lines[:on] if s.startswith(":a")}
    w24 = [(t, s) for t, s in lines[setup_end + 1:] if s.startswith(":w24=") and int(s[5:-1])]   # not the 0 Hz of a stop
    # a pass starts where the frequency falls back to the lowest start of the run
    f = [hz(int(s[5:-1])) for _, s in w24]
    lowest = min(f)
    starts = [i for i, v in enumerate(f) if v == lowest]
    if len(starts) < 2:
        raise SystemExit("the capture holds less than one whole pass after its first: nothing to compare with")
    a, bnd = starts[0], starts[1]
    t0 = w24[a][0]
    pts = [(t - t0, hz(int(s[5:-1])), s) for t, s in w24[a:bnd]]
    return [s for _, s in lines[:setup_end + 1]], tables, pts, w24[bnd][0] - t0


def standalone(args) -> int:
    tx = pc_lines(args.capture)
    cap_setup, cap_tables, cap_pts, cap_pass_s = capture_run(tx)
    keys = [k for k in args.keys.split(",") if k]
    b = new_board(args.elf, pb1=0, keys=[(KEY_T0_MS + i * KEY_STEP_MS, k) for i, k in enumerate(keys)])
    t_start = KEY_T0_MS + (len(keys) - 1) * KEY_STEP_MS
    out = args.out
    b.at(t_start - 100.0, lambda: shot(b, out / "list.png"))
    for name, dt in (("sweep", 5.0), ("fixed", 18.0), ("sweep2", 30.0)):
        if dt < args.seconds:
            b.at(t_start + dt * 1000.0, lambda name=name: shot(b, out / f"dash_{name}.png"))
    print(f"{args.elf}: keys {args.keys} from {KEY_T0_MS / 1000:.1f} s, {args.seconds:.0f} s of the preset -> {out}")
    t_wall = time.time()
    b.run(t_start + args.seconds * 1000.0)
    rc = finish(b, t_wall)
    fw = [(t, s) for t, s in tx_lines_of(b, args.port) if t >= t_start]
    (out / "sent.txt").parent.mkdir(parents=True, exist_ok=True)
    (out / "sent.txt").write_text("".join(f"{t:10.1f} {s[:60]}{' ...' if len(s) > 60 else ''}\n" for t, s in fw))
    ok = True

    def check(what, good, detail=""):
        nonlocal ok
        ok = ok and good
        print(f"  {'ok  ' if good else 'FAIL'} {what}{': ' + detail if detail else ''}")

    # the tables
    for nn in (":a11", ":a25"):
        mine = next((s for _, s in fw if s.startswith(nn + "=")), None)
        check(f"wave table {nn[2:]} as Spooky2's", mine is not None and mine == cap_tables.get(nn),
              "not sent" if mine is None else f"{len(mine)} bytes")
    # the generator's settings once the first step is out
    first = next((i for i, (_, s) in enumerate(fw) if s == ":w11=1,1,"), None)
    if first is None:
        check("the preset plays", False, "no :w11=1,1, line (did the keys reach the preset's row? see list.png)")
        return 1
    mine, theirs = reg_state([s for _, s in fw[:first + 2]]), reg_state(cap_setup)
    for n in REGS:
        check(f"register w{n} as Spooky2 leaves it", mine.get(n) == theirs.get(n), f"{mine.get(n)} / Spooky2 {theirs.get(n)}")
    # the steps: a step's first line is followed by :w11=1,1,
    after = fw[first - 1:]
    steps = [(t, x) for i, (t, x) in enumerate(after[:-1]) if x.startswith(":w24=") and after[i + 1][1] == ":w11=1,1,"]
    t0, cap_t = steps[0][0], []
    for k, (t, x) in enumerate(steps):   # Spooky2's steps take 11.06 s, the firmware's 11.00 s
        rel = (t - t0) / 1000.0
        hit = next((ct for ct, _, cs in cap_pts if cs == x and abs(ct - rel) <= 0.3 + 0.1 * k), None)
        check(f"step {k + 1} starts with Spooky2's line {x}", hit is not None,
              f"{rel:.2f} s in" + ("" if hit is None else f" (Spooky2: {hit:.2f} s)"))
        cap_t.append(hit)
    for k in range(1, len(steps)):
        mine_s = (steps[k][0] - steps[k - 1][0]) / 1000.0
        theirs_s = cap_t[k] - cap_t[k - 1] if cap_t[k] is not None and cap_t[k - 1] is not None else None
        check(f"step {k} lasts as long", theirs_s is not None and abs(mine_s - theirs_s) < 0.15,
              f"{mine_s:.2f} s" + ("" if theirs_s is None else f" (Spooky2: {theirs_s:.2f} s)"))
    # the sweeps: every line between two step starts, against Spooky2's points of that step
    n = bad = 0
    worst = 0.0
    for k, (t_s, _) in enumerate(steps):
        if cap_t[k] is None:
            continue
        t_e = steps[k + 1][0] if k + 1 < len(steps) else float("inf")
        seg = [(ct - cap_t[k], f) for ct, f, _ in cap_pts if cap_t[k] <= ct < cap_t[k] + 10.9]
        for t, x in after:
            if not (t_s < t < t_e) or not x.startswith(":w24="):
                continue
            n += 1
            tau, f = (t - t_s) / 1000.0, hz(int(x[5:-1]))
            if len(seg) < 2:            # a fixed frequency: no line expected, and never another value
                bad += f != seg[0][1]
                continue
            j = max(i for i in range(len(seg) - 1) if seg[i][0] <= tau or i == 0)
            slope = (seg[j + 1][1] - seg[j][1]) / (seg[j + 1][0] - seg[j][0])
            err = abs(f - (seg[j][1] + slope * (tau - seg[j][0])))
            late = err / abs(slope) if slope else float("inf") if err else 0.0
            worst = max(worst, late)
            bad += late > 0.2
    check(f"{n} sweep lines on the line Spooky2's sweep draws", n > 0 and bad == 0,
          f"the furthest {worst * 1000:.0f} ms of the slope off, {bad} beyond 200 ms")
    others = sorted({x[:4] for _, x in fw[first + 2:]} - {":w24", ":w11", ":w12"})
    check("while it plays only :w24, :w11=1,1, and :w12=0,0, go out", not others, " ".join(others))
    print("the same on the wire as Spooky2" if ok else "DIFFERENT from Spooky2 (see above)")
    return rc if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    rp = sub.add_parser("replay", help="the PC's lines into the firmware in PC mode, LCD shots")
    rp.add_argument("capture", type=Path)
    rp.add_argument("--elf", type=Path, default=ROOT / "build" / "nx" / "gxpro.elf")
    rp.add_argument("--out", type=Path, default=ROOT / "build" / "replay" / "pc")
    rp.add_argument("--port", choices=("U1", "U2"), default="U1", help="link the capture is replayed on (U1 = G1)")
    rp.add_argument("--from", dest="t_from", type=float, help="capture time in s (default: the first run's name line)")
    rp.add_argument("--to", dest="t_to", type=float, help="capture time in s (default: 45 s into the first run)")
    rp.add_argument("--max-gap", type=float, default=2.0, help="idle gaps are shortened to this many seconds")
    rp.add_argument("--every", type=float, default=2.0, help="seconds between LCD shots")
    rp.set_defaults(fn=replay)
    sp = sub.add_parser("standalone", help="the firmware's built-in preset against the capture")
    sp.add_argument("capture", type=Path)
    sp.add_argument("--elf", type=Path, default=ROOT / "build" / "nx" / "gxpro.elf")
    sp.add_argument("--out", type=Path, default=ROOT / "build" / "replay" / "standalone")
    sp.add_argument("--port", choices=("U1", "U2"), default="U1", help="link of the generator it plays on (U1 = G1)")
    sp.add_argument("--keys", default=KEYS, help="key presses, 0.7 s apart from 6 s, the last one starts the preset")
    sp.add_argument("--seconds", type=float, default=45.0, help="how long the preset plays")
    sp.set_defaults(fn=standalone)
    args = ap.parse_args()
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
