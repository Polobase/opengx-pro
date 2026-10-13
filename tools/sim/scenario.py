"""Scenario files and the runner.

A scenario is a JSON file:

{
  "description": "what it covers",
  "end_ms": 12000,                      simulated time to run
  "pb1": 0,                             initial PB1 level (1 = PC/USB connected)
  "spi_image": "backup",                "backup" (newest backups/spi_flash_*.bin, the default),
                                        "erased", or a path relative to the scenario file
  "variant": "0xFF00",                  LCD panel halfword at 0x08003C00 (default 0xFF00)
  "generators": {"U1": 1.0, "U2": 1.0}, ack delay in ms per port; false = no generator boards
  "generator_replies": {":r80=0,": ":w80=13583792,...,\\r\\n"},   extra canned replies (optional)
  "timing_tol_ms": 50,                  diff tolerance for UART/PB2 timestamps (optional)
  "tolerance_px": 0,                    default per-checkpoint pixel tolerance (optional)
  "two_sided_before_ms": 5000,          diff.py --timing one-sided/anchor: reference lines before this
                                        time stay two-sided (the boot sequence must not start earlier)
  "on_reset": "stop",                   "continue": a software (SYSRESETREQ) or watchdog reset reboots
                                        the board and the run goes on (default "stop": the run ends
                                        there, stop reason "reset" / "watchdog")
  "expect_watchdog": false,             an IWDG reset is expected (otherwise it is a violation)
  "boot_scribble": ["0x20000000", "0x20000400", "0xA5"],   RAM [start, end) the bootloader
                                        overwrites on every reboot (optional)
  "bl_full": 0.5,                       backlight duty shown as full brightness in the screenshots
                                        (default 0.5, the classic firmware's normal level)
  "timeline": [
    {"at": 6000, "key": "G1"},                    press (held 150 ms unless "ms" is given)
    {"at": 6000, "key": "G1+G2", "ms": 3000},     combo, held 3 s
    {"at": 8000, "pb1": 1},                       PB1 level change
    {"at": 8100, "pc_unlock": true},              set the "PC unlocked" bit 0x4000 of ui_flags, as the
                                                  vendor handshake (not implemented) would
    {"at": 6000, "intent": "open_list", "ch": 1}, what the user wants, expanded into key presses by
                                                  the run's keymap (tools/scenarios/keymaps/NAME.json:
                                                  classic for the original and APP=fw, nx for nx);
                                                  intents: open_list focus nav_up nav_down confirm back
                                                  play_pause stop loop stop_all power_hold ("ch": 1|2
                                                  where the keymap uses {ch})
    {"at": 9000, "rx": "U1", "text": ":w24=3750007,\\r\\n"},   bytes sent to the display
    {"at": 9200, "reboot": true},                 reset pulse on NRST: RAM kept, RCC_CSR.PINRSTF
    {"at": 9300, "power_cut": true},              power lost and back: RAM lost, SPI flash kept (an
                                                  erase/program in progress is torn), panel reset
    {"at": 9300, "power_cut": true, "tear": "program"},   armed: the cut happens halfway through
                                                  the next SPI-flash program ("erase", "any")
    {"at": 9500, "checkpoint": "pc_mode", "tolerance_px": 0, "mask": [[x0, y0, x1, y1]]},
    {"at": 9600, "checkpoint": "x", "informational": true}     reported by diff.py, never fails
  ],
  "note": "free text shown in the diff report (e.g. known deviations)"
}

Times are simulated milliseconds from power-on. Checkpoint masks are logical screen
rectangles (landscape, X 0..319, Y 0..239, inclusive) excluded from the pixel comparison.
Key names: LOOP START STOP UP DOWN G1 G2 POWER; combos with '+'.
"""

import hashlib
import json
import struct
import time
from pathlib import Path

from .firmware import NAMES_DIR, ROOT, load
from .machine import Board, BoardConfig, Inputs

DEFAULT_PRESS_MS = 150
SCENARIO_DIR = Path(__file__).resolve().parents[1] / "scenarios"
KEYMAP_DIR = SCENARIO_DIR / "keymaps"


def newest_backup():
    backups = sorted((ROOT / "backups").glob("spi_flash_*.bin"))
    if not backups:
        raise FileNotFoundError("no backups/spi_flash_*.bin found (needed for spi_image 'backup')")
    return backups[-1]


def spi_image(spec, base_dir=None):
    if spec in (None, "backup"):
        return newest_backup().read_bytes()          # read-only; the board works on a copy
    if spec == "erased":
        return b"\xff" * 0x200000
    p = Path(spec)
    if not p.is_absolute() and base_dir:
        p = Path(base_dir) / p
    return p.read_bytes()


EVENT_KEYS = ("key", "pb1", "rx", "checkpoint", "reboot", "power_cut", "intent", "pc_unlock")


def load_scenario(path):
    path = Path(path)
    if not path.exists() and (SCENARIO_DIR / f"{path}.json").exists():
        path = SCENARIO_DIR / f"{path}.json"
    sc = json.loads(path.read_text())
    sc.setdefault("name", path.stem)
    sc["_path"] = str(path)
    for ev in sc.get("timeline", []):
        if not any(k in ev for k in EVENT_KEYS):
            raise ValueError(f"{path.name}: timeline entry without {'/'.join(EVENT_KEYS)}: {ev}")
    if sc.get("on_reset", "stop") not in ("stop", "continue"):
        raise ValueError(f"{path.name}: on_reset must be \"stop\" or \"continue\"")
    return sc


def load_keymap(name):
    """tools/scenarios/keymaps/<name>.json, or a path."""
    p = Path(name)
    if not p.suffix:
        p = KEYMAP_DIR / f"{name}.json"
    km = json.loads(p.read_text())
    km["_path"] = str(p)
    return km


def expand_timeline(sc, keymap="classic"):
    """The scenario's timeline with every intent replaced by the key presses of `keymap` (a name,
    a path or a loaded keymap); other events are returned unchanged."""
    km = load_keymap(keymap) if isinstance(keymap, str) else keymap
    out = []
    for ev in sc.get("timeline", []):
        if "intent" not in ev:
            out.append(ev)
            continue
        steps = km["intents"].get(ev["intent"])
        if steps is None:
            raise ValueError(f"{sc['name']}: intent {ev['intent']!r} is not in keymap {km['_path']}")
        for st in steps:
            key = st["key"]
            if "{ch}" in key:
                if ev.get("ch") not in (1, 2):
                    raise ValueError(f"{sc['name']}: intent {ev['intent']!r} at {ev['at']} ms needs \"ch\": 1 or 2")
                key = key.replace("{ch}", str(ev["ch"]))
            k = {"at": ev["at"] + st.get("dt", 0), "key": key}
            if "ms" in st:
                k["ms"] = st["ms"]
            out.append(k)
    return out


def fingerprint(sc, target, keymap="classic"):
    """Hash of everything a run depends on: scenario file (and its keymap when it uses intents),
    firmware image (and the reference names), SPI-flash image and the emulator sources. diff.py
    reuses a stored reference run only while this matches."""
    h = hashlib.sha1()
    h.update(Path(sc["_path"]).read_bytes())
    if any("intent" in ev for ev in sc.get("timeline", [])):
        h.update(Path(load_keymap(keymap)["_path"]).read_bytes())
    fw = load(target)
    h.update(bytes(fw.image))
    if fw.reference:
        for tsv in sorted(NAMES_DIR.glob("*.tsv")):
            h.update(tsv.read_bytes())
    h.update(hashlib.sha1(spi_image(sc.get("spi_image"), Path(sc["_path"]).parent)).digest())
    for src in sorted(Path(__file__).resolve().parent.glob("*.py")):
        h.update(src.read_bytes())
    return h.hexdigest()


def _int(v, default):
    if v is None:
        return default
    return int(v, 0) if isinstance(v, str) else int(v)


def _scribble(spec):
    """["0x20000000", "0x20000400", "0xA5"] -> (start, end, byte); None when not given."""
    return tuple(_int(v, 0) for v in spec) if spec else None


def _pc_unlock(b):
    """Set the "PC unlocked" bit (0x4000) of the firmware's ui_flags, the state the vendor unlock
    handshake produces (excluded from this project, docs/spec/application.md section 7): g_ui_flags
    in our builds, ui_flags in the original (tools/sim/names)."""
    addr = b.fw.syms.get("g_ui_flags", b.fw.syms.get("ui_flags"))
    if addr is None:
        raise ValueError("pc_unlock: the firmware has no g_ui_flags / ui_flags symbol")
    v = struct.unpack("<H", bytes(b.uc.mem_read(addr, 2)))[0]
    b.uc.mem_write(addr, struct.pack("<H", v | 0x4000))
    b.event("PC unlock state set (ui_flags |= 0x4000)")


def run(sc, target="reference", out_dir=None, log=None, use_hle=True, keymap="classic"):
    """Run scenario `sc` (dict from load_scenario) on `target` ('reference' or an ELF path).
    Writes <out_dir>/<checkpoint>.png/.rgb565 and result.json; returns the result dict.
    Intent events expand through `keymap` (tools/scenarios/keymaps/)."""
    t_wall = time.time()
    fw = load(target, use_hle=use_hle)
    out = Path(out_dir) if out_dir else None
    if out:
        out.mkdir(parents=True, exist_ok=True)
    gens = sc.get("generators", {"U1": 1.0, "U2": 1.0})
    cfg = BoardConfig(reference=fw.reference,
                      variant=_int(sc.get("variant"), 0xFF00),
                      flash_3d00=_int(sc.get("flash_3d00"), 0),
                      spi_image=spi_image(sc.get("spi_image"), Path(sc["_path"]).parent),
                      allow_tx=True, allow_spi_write=True,
                      generators=dict(gens) if gens else {},
                      generator_replies=sc.get("generator_replies", {}),
                      stop_on_power_off=True,
                      on_reset=sc.get("on_reset", "stop"),
                      expect_watchdog=bool(sc.get("expect_watchdog", False)),
                      boot_scribble=_scribble(sc.get("boot_scribble")),
                      bl_full=float(sc.get("bl_full", 0.5)))
    inputs = Inputs(pb1=sc.get("pb1", 0))
    timeline = sorted(expand_timeline(sc, keymap), key=lambda e: e["at"])
    for ev in timeline:
        if "key" in ev:
            inputs.add_key(ev["at"], ev["at"] + ev.get("ms", DEFAULT_PRESS_MS), ev["key"])
        if "pb1" in ev:
            inputs.set_pb1(ev["at"], ev["pb1"])
    inputs_log = []
    for ev in timeline:
        if "key" in ev:
            key = ev["key"]
            inputs_log.append({"t_ms": float(ev["at"]), "what": f"press {key}"})
            inputs_log.append({"t_ms": float(ev["at"] + ev.get("ms", DEFAULT_PRESS_MS)), "what": f"release {key}"})
        if "pb1" in ev:
            inputs_log.append({"t_ms": float(ev["at"]), "what": f"PB1={ev['pb1']}"})
        if "rx" in ev:
            inputs_log.append({"t_ms": float(ev["at"]), "what": f"{ev['rx']} receives {ev['text'].rstrip()!r}"})
        if ev.get("reboot") or ev.get("power_cut"):
            inputs_log.append({"t_ms": float(ev["at"]), "what": "reboot" if ev.get("reboot") else "power cut"})
    inputs_log.sort(key=lambda x: x["t_ms"])
    b = Board(fw, cfg, inputs)
    checkpoints = []

    def capture(ev, after_stop=False):
        name = ev["checkpoint"]
        bl = b.backlight()
        entry = {"name": name, "t_ms": ev["at"], "sim_ms": round(b.ms, 3), "backlight": round(bl, 3),
                 **b.lcd.state()}
        if after_stop:
            entry["after_stop"] = b.stop_reason
        if out:
            b.lcd.image(backlight=bl, full=b.cfg.bl_full).save(out / f"{name}.png")
            (out / f"{name}.rgb565").write_bytes(bytes(b.lcd.fb))
            entry["png"], entry["raw"] = f"{name}.png", f"{name}.rgb565"
        entry["sha1"] = hashlib.sha1(bytes(b.lcd.fb)).hexdigest()
        checkpoints.append(entry)
        if log:
            log(f"{b.ms:9.1f} ms  checkpoint {name} (backlight {bl:.2f})")

    pending = []
    for ev in timeline:
        if "rx" in ev:
            data = ev["text"].encode("latin-1")
            b.at(ev["at"], lambda ev=ev, data=data: b.inject(ev["rx"], b.ms, data))
        if ev.get("reboot"):
            b.at(ev["at"], lambda: b.reset_request("pin"))
        if ev.get("power_cut"):
            b.at(ev["at"], lambda ev=ev: b.power_cut(ev.get("tear")))
        if ev.get("pc_unlock"):
            b.at(ev["at"], lambda: _pc_unlock(b))
        if "checkpoint" in ev:
            pending.append(ev)
            b.at(ev["at"], lambda ev=ev: (capture(ev), pending.remove(ev)))
    stop = b.run(float(sc.get("end_ms", 10000)))
    for ev in list(pending):              # stopped early (power-off, fault): final screen
        capture(ev, after_stop=True)
    b.flush_lines()

    nor = b.nor
    result = {
        "scenario": sc["name"], "target": fw.name, "reference": fw.reference,
        "fingerprint": fingerprint(sc, target, keymap), "keymap": keymap,
        "end_ms": sc.get("end_ms", 10000), "stopped_at_ms": round(b.ms, 3), "stop_reason": stop,
        "wall_s": round(time.time() - t_wall, 2),
        "checkpoints": checkpoints,
        "uart": b.uart_lines,
        "pb2": b.pb2_edges,
        "spi_flash": {"ops": nor.ops,
                      "changed": [{"start": s, "end": e, "sha1": hashlib.sha1(bytes(nor.mem[s:e])).hexdigest()}
                                  for s, e in nor.changed_ranges()],
                      "sectors": {f"0x{s:06X}": hashlib.sha1(bytes(nor.mem[s:s + 0x1000])).hexdigest()
                                  for s in nor.changed_sectors()}},
        "violations": b.violations,
        "warnings": [{"text": t, "first_ms": v[0], "count": v[1]} for t, v in b.warnings.items()],
        "events": [{"t_ms": round(t, 3), "text": x} for t, x in b.events],
        "unmodelled": sorted(b.unmodelled),
        "irq_counts": {str(k): v for k, v in sorted(b.irq_counts.items())},
        "generator_acks": {p: g.acks for p, g in b.generators.items()},
        "resets": b.resets,
        "inputs": [x for x in inputs_log if x["t_ms"] <= b.ms],
    }
    if fw.reference:
        result["internal_flash_words_programmed"] = b.flash_programmed
    if out:
        (out / "result.json").write_text(json.dumps(result, indent=1))
    return result
