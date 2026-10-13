#!/usr/bin/env python3
"""Run emulator scenarios (tools/scenarios/*.json) on the original firmware or an ELF build.

Each run writes <out>/<checkpoint>.png (640x480, as seen on the glass), <checkpoint>.rgb565
(raw 240x320 GRAM, big-endian RGB565 as sent to the panel) and result.json (UART lines per port
with timestamps, PB2 edges, SPI-flash operations, violations, warnings).

Examples:
  tools/scenario.py boot_home                            # reference -> tools/scenarios/reference/boot_home/
  tools/scenario.py --all                                # every scenario on the reference
  tools/scenario.py list_nav --target build/fw/gxpro.elf # candidate -> build/sim/list_nav/gxpro/
  tools/scenario.py --list
  tools/scenario.py ch1_program --expand --keymap nx     # print the key presses of the intents
The scenario format is documented in tools/sim/scenario.py.
"""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from sim import scenario as S  # noqa: E402

FW_DIR = Path(__file__).resolve().parents[1]


def default_out(name, target):
    if target in ("reference", "original", "ref"):
        return S.SCENARIO_DIR / "reference" / name
    return FW_DIR / "build" / "sim" / name / Path(target).parent.name


def summary(res, out=None):
    lines = [f"{res['scenario']} on {res['target']}: stopped at {res['stopped_at_ms']:.1f} ms "
             f"({res['stop_reason']}), {res['wall_s']:.1f} s wall"]
    for c in res["checkpoints"]:
        lines.append(f"  checkpoint {c['t_ms']:>8} ms  {c['name']:<16} backlight {c['backlight']:.2f}"
                     + (f"  (after stop: {c['after_stop']})" if c.get("after_stop") else ""))
    tx = [u for u in res["uart"] if u["dir"] == "tx"]
    for u in tx[:40]:
        lines.append(f"  {u['t_ms']:9.1f} ms  {u['port']} -> {u['text'].rstrip()!r}")
    if len(tx) > 40:
        lines.append(f"  ... {len(tx) - 40} more TX lines")
    for op in res["spi_flash"]["ops"][:10]:
        lines.append(f"  {op['t_ms']:9.1f} ms  SPI flash {op['op']} 0x{op['addr']:06X} ({op['len']} bytes)")
    for e in res["events"]:
        lines.append(f"  {e['t_ms']:9.1f} ms  {e['text']}")
    for w in res["warnings"]:
        lines.append(f"  warning: {w['text']} (first at {w['first_ms']} ms, {w['count']}x)")
    for v in res["violations"]:
        lines.append(f"  VIOLATION {v}")
    if out:
        lines.append(f"  -> {out}")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("scenarios", nargs="*", help="scenario names (tools/scenarios/NAME.json) or paths")
    ap.add_argument("--target", default="reference", help="'reference' (original firmware) or an ELF path")
    ap.add_argument("--out", help="output directory (single scenario only)")
    ap.add_argument("--all", action="store_true", help="run every scenario in tools/scenarios/")
    ap.add_argument("--list", action="store_true", help="list scenarios")
    ap.add_argument("--keymap", default="classic",
                    help="key model for intent events: tools/scenarios/keymaps/NAME.json (default classic)")
    ap.add_argument("--expand", action="store_true", help="print the timeline with intents expanded; do not run")
    args = ap.parse_args()

    names = sorted(p.stem for p in S.SCENARIO_DIR.glob("*.json"))
    if args.list:
        for n in names:
            print(f"{n:<20} {S.load_scenario(n).get('description', '')}")
        return 0
    todo = names if args.all else args.scenarios
    if not todo:
        ap.error("no scenario given (or --all / --list)")
    if args.expand:
        for name in todo:
            print(json.dumps(S.expand_timeline(S.load_scenario(name), args.keymap), indent=1))
        return 0
    bad = 0
    for name in todo:
        sc = S.load_scenario(name)
        out = Path(args.out) if args.out and len(todo) == 1 else default_out(sc["name"], args.target)
        res = S.run(sc, args.target, out, keymap=args.keymap)
        print(summary(res, out))
        bad |= bool(res["violations"])
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
