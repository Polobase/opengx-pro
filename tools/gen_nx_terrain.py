#!/usr/bin/env python3
"""Generate the nx firmware's built-in Terrain Protocol from Spooky2's presets.

Reads the "Terrain Protocol" preset folder of a Spooky2 installation (Detox/Terrain Protocol):

  Remote/Terrain (R) - BY.txt, Terrain-Mercury (R) - BY.txt, Terrain-Nonviolent (R) - BY.txt
      one [Preset] block per set: "PresetName=N. Name" (a whole day) or "N.1"/"N.2" (half days),
      "Schedule_Run_For=24" or 12 (hours the set loops before the next), "Loaded_Frequencies";
      the notes hold each flavour's Contact schedule ("Entire Schedule for Terrain... (Contact)",
      lines "day, generator, Contact\\File (C) - BY"; a day with one generator-1 line runs that
      set in both sessions)
  Contact/<File>.txt
      one block: the list, then a 0 Hz step (the "safe to pause" notification)
  ../Shells/<Base_Preset>.txt
      Out1_Amplitude (volts)

"Loaded_Frequencies" items are "freq=dwell" with optional tokens after a space; only "Wn" (Out1
waveform n, in force until changed; Spooky2 wave n is generator table 10 + n) is supported, and
a set must keep one waveform. Anything else (sweeps "a-b", other tokens, more than 2 decimals,
more than 200 steps) stops the build: the tables would not play what the preset plays.

The tables (src/nx/nx_terrain_data.h) are committed as src/nx/data/nx_terrain_data.c, so every
build of the firmware has the same protocol; `make nx-data` writes them again from a Spooky2
installation. They hold the frequencies, times and names of Spooky2's Terrain presets, which are
not this project's own work (docs/nx/THIRD_PARTY.md).

Usage: gen_nx_terrain.py <Terrain Protocol dir> <output .c>
"""

import re
import sys
from pathlib import Path

FLAVOURS = ["Terrain", "Terrain-Mercury", "Terrain-Nonviolent"]
EXPECT_BLOCKS = {"Terrain": 15, "Terrain-Mercury": 15, "Terrain-Nonviolent": 13}
EXPECT_DAYS = {"Terrain": 11, "Terrain-Mercury": 11, "Terrain-Nonviolent": 10}
MAX_STEPS = 200
MAX_SETS = 64


def fail(msg: str):
    sys.exit(f"gen_nx_terrain: {msg}")


def blocks(path: Path) -> list[dict]:
    """The [Preset] blocks of a preset file as key -> value (multi-line values joined)."""
    text = path.read_text(encoding="latin-1").replace("\r\n", "\n").replace("\r", "\n")
    out, cur, key, val = [], None, None, None
    for line in text.split("\n"):
        if key is not None:   # inside a multi-line value
            if line.endswith('"'):
                cur[key] = val + "\n" + line[:-1]
                key = None
            else:
                val += "\n" + line
            continue
        s = line.strip()
        if s == '"[Preset]"':
            cur = {}
        elif s == '"[/Preset]"':
            out.append(cur)
            cur = None
        elif cur is not None and s.startswith('"') and "=" in s:
            k, v = s[1:].split("=", 1)
            if v.endswith('"'):
                cur[k] = v[:-1]
            else:
                key, val = k, v
    if not out:
        fail(f"{path}: no [Preset] block")
    return out


def steps(block: dict, where: str) -> tuple[list[tuple[int, int]], int]:
    """(centi-Hz, dwell s) per item and the set's generator waveform table."""
    items = [i for i in block.get("Loaded_Frequencies", "").split(",") if i.strip()]
    if not items:
        fail(f"{where}: no frequencies")
    out, wave = [], None
    for it in items:
        parts = it.strip().split(" ")
        head, toks = parts[0], parts[1:]
        for t in toks:
            m = re.fullmatch(r"W(\d+)", t)
            if not m:
                fail(f"{where}: unsupported token {t!r} in {it!r}")
            w = 10 + int(m.group(1))
            if wave is not None and w != wave:
                fail(f"{where}: the waveform changes inside the set")
            wave = w
        if "=" not in head:
            fail(f"{where}: item {it!r} has no dwell")
        f, d = head.split("=", 1)
        if "-" in f:
            fail(f"{where}: sweep {it!r} is not supported")
        c = round(float(f) * 100)
        if abs(float(f) * 100 - c) > 1e-6 or c < 0 or c >= 1 << 24:
            fail(f"{where}: frequency {f} needs more than 2 decimals or is out of range")
        d = int(d)
        if not 0 < d <= 65535:
            fail(f"{where}: dwell {d}")
        out.append((c, d))
    if wave is None:
        fail(f"{where}: no waveform token (W1..W9)")
    return out, wave


def volts(root: Path, block: dict, where: str) -> float:
    base = block.get("Base_Preset", "").strip("\\").replace("\\", "/")
    shell = root / (base + ".txt")
    if not shell.is_file():
        fail(f"{where}: shell {shell} not found")
    v = blocks(shell)[0].get("Out1_Amplitude")
    return float(block.get("Out1_Amplitude", v))


class Tables:
    def __init__(self):
        self.freq: list[int] = []      # list entries, centi-Hz
        self.lists: dict[tuple, int] = {}
        self.dwi: list[int] = []
        self.dwell: list[int] = []
        self.sets: list[tuple] = []    # (name, list, dw, n, k, wave, dv, end)
        self.set_ix: dict[tuple, int] = {}
        self.names: dict[str, int] = {}
        self.pool = ""

    def name(self, s: str) -> int:
        if s not in self.names:
            self.names[s] = len(self.pool)
            self.pool += s + "\0"
        return self.names[s]

    def dwell_ix(self, d: int) -> int:
        if d not in self.dwell:
            self.dwell.append(d)
        return self.dwell.index(d)

    def add_set(self, name: str, st: list[tuple[int, int]], wave: int, v: float, where: str) -> int:
        end = None
        if st[-1][0] == 0:
            end = st[-1][1]
            st = st[:-1]
        if any(c == 0 for c, _ in st):
            fail(f"{where}: a 0 Hz step before the end")
        total = len(st) + (end is not None)
        if total > MAX_STEPS:
            fail(f"{where}: {total} steps, more than {MAX_STEPS}")
        n = next(p for p in range(1, len(st) + 1) if len(st) % p == 0 and all(st[i] == st[i % p] for i in range(len(st))))
        body = tuple(c for c, _ in st[:n])
        if body not in self.lists:
            self.lists[body] = len(self.freq)
            self.freq += body
        dv = round(v * 10)
        if not 0 < dv <= 200:
            fail(f"{where}: amplitude {v} V")
        key = (name, body, tuple(d for _, d in st[:n]), len(st) // n, wave, dv, end)
        if key in self.set_ix:
            return self.set_ix[key]
        dw = len(self.dwi)
        self.dwi += [self.dwell_ix(d) for _, d in st[:n]]
        self.set_ix[key] = len(self.sets)
        self.sets.append((self.name(name), self.lists[body], dw, n, len(st) // n, wave, dv,
                          0xFF if end is None else self.dwell_ix(end)))
        if len(self.sets) > MAX_SETS:
            fail(f"more than {MAX_SETS} distinct sets")
        return self.set_ix[key]


def label(preset_name: str, where: str) -> tuple[int, int, str]:
    m = re.match(r"\s*(\d+)\.(\d?)\s+(.*)$", preset_name)
    if not m:
        fail(f"{where}: PresetName {preset_name!r} has no day number")
    return int(m.group(1)), int(m.group(2) or 0), m.group(3).strip()


def contact_schedule(notes: str, where: str) -> list[list[str]]:
    lines = notes.split("\n")
    try:
        at = next(i for i, l in enumerate(lines) if re.match(r"Entire Schedule for Terrain\S* \(Contact\)", l))
    except StopIteration:
        fail(f"{where}: no Contact schedule in the notes")
    days: dict[int, list[str]] = {}
    for l in lines[at + 1:]:
        if l.startswith("Entire Schedule"):
            break
        m = re.match(r"(\d+),\s*1,\s*Contact[\\/](.+?)\s*$", l)
        if m:
            days.setdefault(int(m.group(1)), []).append(m.group(2))
    if not days or sorted(days) != list(range(1, max(days) + 1)):
        fail(f"{where}: the Contact schedule's days are not 1..n")
    out = []
    for d in sorted(days):
        s = days[d]
        if len(s) == 1:
            s = s * 2
        if len(s) != 2:
            fail(f"{where}: day {d} has {len(s)} Contact sessions")
        out.append(s)
    return out


def main() -> int:
    src, out = Path(sys.argv[1]), Path(sys.argv[2])
    root = src.parent.parent   # "Preset Collections": Base_Preset paths start there
    t = Tables()
    protos = []   # (name, kind, n, days, item bytes)
    for fl in FLAVOURS:
        rpath = src / "Remote" / f"{fl} (R) - BY.txt"
        if not rpath.is_file():
            fail(f"{rpath} not found")
        bl = blocks(rpath)
        if len(bl) != EXPECT_BLOCKS[fl]:
            fail(f"{rpath.name}: {len(bl)} sets, expected {EXPECT_BLOCKS[fl]}")
        items, days = [], 0
        for i, b in enumerate(bl):
            where = f"{rpath.name} set {i + 1}"
            day, part, name = label(b.get("PresetName", ""), where)
            hours = int(b.get("Schedule_Run_For", "0") or 0)
            if hours not in (12, 24):
                fail(f"{where}: Schedule_Run_For {hours}")
            st, wave = steps(b, where)
            s = t.add_set(name, st, wave, volts(root, b, where), where)
            items += [s, hours, day << 2 | part]
            days = max(days, day)
        if days != EXPECT_DAYS[fl]:
            fail(f"{rpath.name}: {days} days, expected {EXPECT_DAYS[fl]}")
        protos.append((fl, 0, len(bl), days, items))
        sched = contact_schedule(bl[0].get("Preset_Notes", ""), rpath.name)
        if len(sched) != EXPECT_DAYS[fl]:
            fail(f"{rpath.name}: Contact schedule of {len(sched)} days, expected {EXPECT_DAYS[fl]}")
        items = []
        for day in sched:
            for f in day:
                cpath = src / "Contact" / f"{f}.txt"
                if not cpath.is_file():
                    fail(f"{cpath} not found")
                b = blocks(cpath)[0]
                name = re.sub(r"^\d+\.\s*", "", b.get("PresetName", "")).strip() or f
                st, wave = steps(b, cpath.name)
                if st[-1][0] != 0:
                    fail(f"{cpath.name}: no 0 Hz end step")
                items.append(t.add_set(name, st, wave, volts(root, b, cpath.name), cpath.name))
        protos.append((fl, 1, len(sched), len(sched), items))

    item_bytes, p_rows = [], []
    for name, kind, n, days, items in protos:
        p_rows.append(f"    {{ {t.name(name)}, {len(item_bytes)}, {kind}, {n}, {days}, 0 }},")
        item_bytes += items

    def arr(vals, per=16):
        return ",\n    ".join(", ".join(str(v) for v in vals[i:i + per]) for i in range(0, len(vals), per))

    freq_bytes = [b for c in t.freq for b in (c & 0xFF, (c >> 8) & 0xFF, c >> 16)]
    set_rows = [f"    {{ {a}, {b}, {c}, {d}, {e}, {f}, {g}, {h}, 0 }}," for a, b, c, d, e, f, g, h in t.sets]
    pool = t.pool.rstrip("\0")
    esc = "".join("\\0" if ch == "\0" else (ch if 0x20 <= ord(ch) < 0x7F and ch not in '"\\' else f"\\{ord(ch):03o}")
                  for ch in pool)
    c = [
        "/* Generated by tools/gen_nx_terrain.py (`make nx-data`) from Spooky2's preset collection",
        " * \"Detox/Terrain Protocol\": the frequencies, times and names of those presets, which are not",
        " * this project's own work (docs/nx/THIRD_PARTY.md). Do not edit (src/nx/nx_terrain_data.h). */",
        '#include "nx/nx_terrain_data.h"',
        "",
        f"static const uint8_t k_freq[] = {{\n    {arr(freq_bytes)},\n}};",
        f"static const uint8_t k_dwi[] = {{\n    {arr(t.dwi)},\n}};",
        f"static const uint16_t k_dwell[] = {{\n    {arr(t.dwell)},\n}};",
        "static const struct nx_tset k_set[] = {\n" + "\n".join(set_rows) + "\n};",
        f"static const uint8_t k_item[] = {{\n    {arr(item_bytes)},\n}};",
        "static const struct nx_tproto k_proto[] = {\n" + "\n".join(p_rows) + "\n};",
        "",
        "const struct nx_tdata k_nx_terrain = {",
        f'    k_freq, k_dwi, k_dwell, k_set, k_proto, k_item, "{esc}", {len(t.sets)}, {len(protos)},',
        "};",
    ]
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(c) + "\n")
    size = len(freq_bytes) + len(t.dwi) + 2 * len(t.dwell) + 12 * len(t.sets) + len(item_bytes) + 8 * len(protos) + len(t.pool)
    print(f"nx_terrain: {len(protos)} protocols, {len(t.sets)} sets, {len(t.freq)} list entries, "
          f"{len(t.dwell)} dwells, about {size} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
