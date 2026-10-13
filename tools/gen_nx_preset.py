#!/usr/bin/env python3
"""Generate the nx firmware's built-in presets with sweeps from Spooky2's presets.

Each preset file (one [Preset] block, with its Base_Preset shell from the same "Preset
Collections" folder) becomes one preset of src/nx/nx_preset_data.h: its "Loaded_Frequencies"
lines, one after the other, as steps "freq=seconds" (a fixed frequency) or "from-to=seconds" (a
linear sweep over that time).

How Spooky2 plays such a preset on a GX Pro was read from a serial capture of "Spooky Radionics
(Boost) Manifestation - AW" (docs/nx/features.md, "Presets with sweeps"), and only what that
capture shows is accepted:

  Out1 sine with Sine_WCM = n     wave table 11 with n sine cycles at half scale (Sine_Spike_Length
                                  1, Sine_Spike_Ratio 2); the generator gets the frequency / n
  Out2 the inverse, following     wave table 25 = 1023 - table 11, Out2 on Out1's frequency
  offsets -100 % / +100 %         :w32=20, / :w33=220, (120 + percent)
  amplitude 20 V at that offset   :w28=1000, / :w29=1000,
  Repeat_Sequence 0 or 1          repeats until stopped, or plays once

Anything else (another waveform, gating, wobble, multipliers, other offsets or amplitudes, tokens
after a frequency, more than 2 decimals, more than 200 steps) stops the build: the firmware would
not play what Spooky2 plays.

The table is committed as src/nx/data/nx_preset_data.c, so every build of the firmware has the
same preset; `make nx-data` writes it again from a Spooky2 installation. It holds the frequencies,
times and name of a Spooky2 preset, which are not this project's own work
(docs/nx/THIRD_PARTY.md).

Usage: gen_nx_preset.py <preset .txt>... <output .c>
"""

import re
import sys
from pathlib import Path

MAX_STEPS = 200
MAX_PRESETS = 15
MAX_CHZ = (1 << 24) - 1
WAVE1, WAVE2 = 11, 25

# key -> the only value the capture covers (after the shell's values, the preset's own on top)
REQUIRED = {
    "InverseWave": "True", "SpikeWave": "False", "Out2_Follow_Out1_Wave": "False",
    "Out2_Follow_Out1_Frequency": "True", "Out2_Hz_Factor": "1", "Out2_Hz_Constant": "0",
    "Out1_Offset": "-100", "Out2_Offset": "100", "Out1_Amplitude": "20", "Out2_Amplitude": "20",
    "Sine_Spike_Length": "1", "Sine_Spike_Ratio": "2",
    "Frequency_Multiplier": "1", "Frequency_Constant": "0", "Dwell_Multiplier": "1",
    "Repeat_Each_Frequency": "1", "Repeat_Each_Program": "1",
    "Enable_Gate_Out1": "0", "Enable_Gate_Out2": "0",
    "Amplitude_Wobble_Type": "Disabled", "Frequency_Wobble_Type": "Disabled",
    "Amplitude_Modulation_Enabled": "0", "Enable_F2_Modulation": "0",
    "Enable_Reduce_Low_Frequency_Amplitude": "0", "Enable_Amplitude_RampUp": "0",
    "Enable_Amplitude_Ramp_Down": "0", "Enable_Swap_Frequency_And_Amplitude": "0",
}
AMPL_CV, OFFS1, OFFS2 = 1000, 20, 220


def fail(msg: str):
    sys.exit(f"gen_nx_preset: {msg}")


def entries(path: Path) -> list[tuple[str, str]]:
    """The "Key=Value" lines of the file's [Preset] block in order (a key may repeat; a value may
    run over several lines)."""
    text = path.read_text(encoding="latin-1").replace("\r\n", "\n").replace("\r", "\n")
    out, key, val, inside, blocks = [], None, None, False, 0
    for line in text.split("\n"):
        if key is not None:   # inside a multi-line value
            if line.endswith('"'):
                out.append((key, val + "\n" + line[:-1]))
                key = None
            else:
                val += "\n" + line
            continue
        s = line.strip()
        if s == '"[Preset]"':
            inside, blocks = True, blocks + 1
        elif s == '"[/Preset]"':
            inside = False
        elif inside and s.startswith('"') and "=" in s:
            k, v = s[1:].split("=", 1)
            if v.endswith('"'):
                out.append((k, v[:-1]))
            else:
                key, val = k, v
    if blocks != 1:
        fail(f"{path.name}: {blocks} [Preset] blocks, expected one")
    return out


def collections_dir(path: Path) -> Path:
    for p in path.resolve().parents:
        if p.name == "Preset Collections":
            return p
    fail(f"{path}: not inside a \"Preset Collections\" folder (needed to find its Base_Preset)")


def centi(text: str, where: str) -> int:
    if not re.fullmatch(r"\d+(\.\d{1,2})?", text):
        fail(f"{where}: frequency {text!r} (digits with at most 2 decimals expected)")
    whole, _, frac = text.partition(".")
    v = int(whole) * 100 + int((frac + "00")[:2])
    if not 0 < v <= MAX_CHZ:
        fail(f"{where}: frequency {text} Hz is outside 0.01 .. {MAX_CHZ / 100} Hz")
    return v


def load(path: Path) -> dict:
    own = entries(path)
    keys = dict(own)
    base = keys.get("Base_Preset")
    merged = {}
    if base:
        shell = collections_dir(path) / Path(*[p for p in base.replace("\\", "/").split("/") if p])
        if not shell.is_file():
            fail(f"{path.name}: Base_Preset {base!r} not found at {shell}")
        shell_entries = entries(shell)
        if any(k == "Base_Preset" for k, _ in shell_entries):
            fail(f"{shell.name}: a shell with its own Base_Preset is not supported")
        merged.update(shell_entries)
    merged.update(own)
    where = path.name

    # Out1's waveform is a group of flags: the preset's own choice replaces the shell's
    if keys.get("Out1_Sine") != "True" or any(v == "True" for k, v in own
                                              if k.startswith("Out1_") and k != "Out1_Sine" and v in ("True", "False")):
        fail(f"{where}: Out1 must be the sine (Out1_Sine=True and no other Out1 waveform)")
    for k, want in REQUIRED.items():
        if merged.get(k) != want:
            fail(f"{where}: {k}={merged.get(k)!r}, but only {want!r} was checked against a capture")
    cycles = merged.get("Sine_WCM", "")
    if not cycles.isdigit() or not 1 <= int(cycles) <= 255:
        fail(f"{where}: Sine_WCM={cycles!r} (1..255 expected)")
    if merged.get("Repeat_Sequence") not in ("0", "1"):
        fail(f"{where}: Repeat_Sequence={merged.get('Repeat_Sequence')!r} (0 = until stopped, or 1)")

    steps = []
    for k, v in own:
        if k != "Loaded_Frequencies":
            continue
        for item in v.split(","):
            item = item.strip()
            if not item:
                continue
            m = re.fullmatch(r"([0-9.]+)(?:-([0-9.]+))?=(\d+)", item)
            if not m:
                fail(f"{where}: step {item!r} (\"freq=seconds\" or \"from-to=seconds\" expected)")
            a = centi(m.group(1), where)
            b = centi(m.group(2), where) if m.group(2) else a
            dwell = int(m.group(3))
            if not 1 <= dwell <= 65535:
                fail(f"{where}: step {item!r}: {dwell} s is outside 1 .. 65535 s")
            steps.append((a, b, dwell))
    if not 1 <= len(steps) <= MAX_STEPS:
        fail(f"{where}: {len(steps)} steps (1 .. {MAX_STEPS})")

    name = merged.get("PresetName", path.stem).strip()
    if name.startswith("Spooky "):
        name = name[len("Spooky "):]
    if not name or len(name) > 40 or not all(" " <= c <= "~" for c in name) or '"' in name or "\\" in name:
        fail(f"{where}: PresetName {name!r} (1 .. 40 ASCII characters)")
    return {"name": name, "steps": steps, "cycles": int(cycles), "loop": merged["Repeat_Sequence"] == "0"}


def u24(v: int) -> str:
    return f"{{ {v & 255}, {v >> 8 & 255}, {v >> 16} }}"


def emit(presets: list[dict]) -> str:
    out = ["/* Generated by tools/gen_nx_preset.py (`make nx-data`) from a preset of Spooky2's collection:",
           " * its frequencies, times and name, which are not this project's own work",
           " * (docs/nx/THIRD_PARTY.md). Do not edit (src/nx/nx_preset_data.h). */",
           '#include "nx/nx_preset_data.h"',
           "",
           "static const struct nx_pstep k_step[] = {"]
    names, heads, first = "", [], 0
    for p in presets:
        out.append(f"    /* {p['name']} */")
        for a, b, dwell in p["steps"]:
            out.append(f"    {{ {u24(a)}, {u24(b)}, {dwell} }},")
        heads.append(f"    {{ {len(names)}, {first}, {len(p['steps'])}, {p['cycles']}, {WAVE1}, {WAVE2}, {AMPL_CV}, "
                     f"{OFFS1}, {OFFS2}, {int(p['loop'])}, 0 }},")
        names += p["name"] + "\0"
        first += len(p["steps"])
    out += ["};", "", "static const struct nx_preset k_preset[] = {"] + heads + ["};", ""]
    text = names[:-1].replace("\0", '\\0" "')
    out += [f'const struct nx_pdata k_nx_presets = {{ k_step, k_preset, "{text}", {len(presets)} }};', ""]
    return "\n".join(out)


def main() -> int:
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    files, target = [Path(a) for a in sys.argv[1:-1]], Path(sys.argv[-1])
    if len(files) > MAX_PRESETS:
        fail(f"{len(files)} presets (at most {MAX_PRESETS})")
    presets = [load(f) for f in files]
    if sum(len(p["steps"]) for p in presets) > 65535:
        fail("too many steps")
    target.write_text(emit(presets))
    for p in presets:
        total = sum(d for _, _, d in p["steps"])
        print(f"gen_nx_preset: {p['name']}: {len(p['steps'])} steps, {total} s, {p['cycles']} cycles per table, "
              f"{'repeats' if p['loop'] else 'plays once'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
