#!/usr/bin/env python3
"""Send serial commands to a real GX Pro over USB and log every reply, per firmware.

Run it once with the original firmware and once with ours, then compare the two logs:

  .venv/bin/python tools/gx_serial_test.py run --save build/serial-original.json
  .venv/bin/python tools/gx_serial_test.py run --save build/serial-ours.json
  .venv/bin/python tools/gx_serial_test.py compare build/serial-original.json build/serial-ours.json

The device must be in PC mode (USB connected). Commands are grouped in tiers (default: read-only
and harmless writes only). Higher tiers change stored data or reset the device; they are opt-in and
listed here so nothing surprising is ever sent:

  query      versions, name/program/step-table reads, out-of-range and malformed lines   (default)
  display    display-only writes that get no reply (backlight, PC-mode mirrors, w16)      (default)
  write      upload a program + name to one slot, read it back, then restore the slot     --write
  cal        write calibration values (w60/w80) and read them back                        --cal
  erase      w96 erase-all-programs, then w95 reset                                        --erase
  generator  generator-board reads (:r11/:r12/:r80): only reply inside a Spooky2 session   --generator

The `write` tier reads the target slot first and re-uploads it afterwards, so it is non-destructive
on its own; still, keep a full SPI-flash backup (tools/gx_link.py dump) before --cal or --erase.
Generator OUTPUTS are never switched on by this tool (no ':w11=1'); disconnect anything from the
outputs anyway.

Two independent access gates matter here (see docs/serial-commands.md, "Session / access gate"):
- the DISPLAY answers ':n00=$' always but returns ':err' to other :n/:g/:p frames unless its gate
  is open (no PC, or an opened session); OpenGX Pro removes this gate;
- the GENERATOR board returns ':err' to :w/:r commands unless a session is open; this gate is in the
  generator firmware, unchanged by either display firmware.
This tool does NOT open a session, so :w/:r commands get ':err' on both firmwares (from the
generator) and the :n/:g/:p commands get ':err' on the original but full replies on ours (the
display gate we removed). Those are the documented deviations, not faults.

For a full comparison where BOTH firmwares answer every command, use a tool that performs the
vendor session handshake first: the `conformance` suite of the `spooky2` package in
github.com/Polobase/freqgen (`make conformance`) drives a real unit, records every reply, and
diffs the original against ours.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial is missing: run this with .venv/bin/python")

WCH_VID = 0x1A86
REPLY_TIMEOUT = 0.4          # a display reply comes within a main-loop pass (~30 ms); generous
QUIET_AFTER_REPLY = 0.05     # keep reading briefly, so multi-line replies are captured whole
WRITE_SLOT = 30              # slot used by the --write round-trip


def find_links(requested):
    """Return {'A': device, 'B': device}: A = ...903 (USART1/G1), B = ...901 (USART2/G2)."""
    if requested:
        return requested
    ports = [p for p in list_ports.comports() if p.vid == WCH_VID and "wchusbserial" in p.device]
    links = {}
    for p in ports:
        if p.device.endswith("903"):
            links["A"] = p.device
        elif p.device.endswith("901"):
            links["B"] = p.device
    if len(links) < 2:
        devs = sorted(p.device for p in ports)
        if len(devs) == 2:                       # fall back to sorted order
            links = {"A": devs[1], "B": devs[0]}
    if "A" not in links or "B" not in links:
        sys.exit("could not find both GX Pro serial channels; is it connected in PC mode?")
    return links


class Device:
    def __init__(self, links):
        self.ser = {}
        for name, dev in links.items():
            s = serial.Serial()
            s.port, s.baudrate, s.timeout = dev, 115200, 0
            s.dtr = s.rts = False
            s.open()
            self.ser[name] = s

    def close(self):
        for s in self.ser.values():
            s.close()

    def exchange(self, link: str, text: str) -> list[str]:
        """Send text (CRLF for :-frames, LF for factory) and return the reply lines within the window."""
        s = self.ser[link]
        s.reset_input_buffer()
        s.write(text.encode("latin-1") + (b"\n" if _is_factory(text) else b"\r\n"))
        deadline = time.perf_counter() + REPLY_TIMEOUT
        buf, lines, last = b"", [], None
        while time.perf_counter() < deadline:
            chunk = s.read(s.in_waiting or 1)
            if chunk:
                buf += chunk
                last = time.perf_counter()
                while b"\n" in buf:
                    raw, buf = buf.split(b"\n", 1)
                    line = raw.decode("latin-1").strip()
                    if line:
                        lines.append(line)
            elif last and time.perf_counter() - last > QUIET_AFTER_REPLY:
                break
        if buf.strip():
            lines.append(buf.decode("latin-1").strip())
        return lines


def _is_factory(text: str) -> bool:
    """Factory commands are ':<a..l><number>' with no '='; frames always have '=' after ':XNN'."""
    return len(text) >= 2 and text[0] == ":" and text[1] in "abcdefghijkl" and "=" not in text


# (id, link, text, tier, description). link 'A', 'B', or 'AB' (sent on both).
def catalog(write_slot: int):
    slot = f"{write_slot:02d}"
    return [
        ("version", "AB", ":n00=$", "query", "firmware version"),
        ("name_read", "A", ":n01=?", "query", "read name of slot 1"),
        ("name_read_b", "B", ":n05=?", "query", "read name of slot 5 (link B)"),
        ("name_pc_slot0", "A", ":n00=?", "query", "read PC name (slot 0)"),
        ("name_pc_slot31", "B", ":n00=?", "query", "read PC name of link B (slot 31)"),
        ("prog_read", "A", ":n01=*", "query", "read stored program 1"),
        ("steps_read", "A", ":n01=#", "query", "read stored step table 1"),
        ("slot_out_of_range", "A", ":n40=?", "query", "name query, slot 40 -> :err"),
        ("g_slot0", "A", ":g00=1,", "query", "gate upload to slot 0 -> :err"),
        ("p_slot31", "A", ":p31=0,", "query", "program upload to slot 31 -> :err"),
        ("unknown_letter", "A", ":x01=?", "query", "unknown frame letter"),
        ("no_colon", "A", "hello there", "query", "line without a leading colon"),
        ("short_number", "A", ":n5=$", "query", "one-digit frame number"),
        ("version_after_junk", "A", ":n00=$", "query", "version again, to show the parser recovered"),

        ("backlight_off", "A", ":w64=10000001,", "display", "display off (no reply)"),
        ("backlight_on", "A", ":w64=18888881,", "display", "display on (no reply)"),
        ("pc_out_flags", "A", ":w11=0,0,", "display", "PC mode: both outputs shown off (no reply)"),
        ("pc_sync", "A", ":w14=0,", "display", "PC mode: independent (no reply)"),
        ("pc_freq1", "A", ":w24=3750007,", "display", "PC mode: show 37.5 kHz on out1 (no reply)"),
        ("w16_noop", "A", ":w16=1,", "display", "w16, no effect (no reply)"),

        # write tier: capture the slot, change it, read back, restore it
        ("save_current_prog", "A", f":n{slot}=*", "write", f"read program {slot} (to restore later)"),
        ("save_current_steps", "A", f":n{slot}=#", "write", f"read step table {slot} (to restore later)"),
        ("save_current_name", "A", f":n{slot}=?", "write", f"read name {slot} (to restore later)"),
        ("upload_gates", "A", f":g{slot}=11,22,33,44,,,", "write", "upload gate parameters"),
        ("upload_prog", "A", f":p{slot}=0,2000,120,1,3,100007,200007,300007,", "write", "upload a 3-step program"),
        ("upload_name", "A", f":n{slot}=Serial self-test", "write", "set the name"),
        ("readback_prog", "A", f":n{slot}=*", "write", "read the uploaded program back"),
        ("readback_steps", "A", f":n{slot}=#", "write", "read the uploaded step table back"),
        ("readback_name", "A", f":n{slot}=?", "write", "read the uploaded name back"),
        # (restore of the slot is done in code after the run, using the saved replies)

        ("cal_read", "A", ":r80=0,", "cal", "read the calibration table (generator reply)"),
        ("cal_set_w80", "A", ":w80=13583792,32001,1601,301,401,1001,601,1602,302,402,1002,602,1603,303,403,1003,603,1604,304,404,1004,604,",
         "cal", "set calibration 0..20 (no reply)"),
        ("cal_set_w60", "B", ":w60=1112111,5,1234,", "cal", "set calibration value 5 (no reply)"),
        ("cal_trim_up", "A", ":w63=111999,", "cal", "calibration value 0 +1 and save (no reply)"),
        ("cal_trim_down", "A", ":w63=111000,", "cal", "calibration value 0 -1 and save (no reply)"),
        ("cal_restore", "A", ":w62=3336333,", "cal", "restore calibration from the backup copy (no reply)"),

        ("generator_version", "A", ":r02=0,", "generator", "generator firmware version (session only)"),
        ("generator_r11", "A", ":r11=,", "generator", "read measurement 1 (Spooky2 session only)"),
        ("generator_r12", "A", ":r12=,", "generator", "read measurement 2 (Spooky2 session only)"),

        ("erase_all", "A", ":w96=12321,", "erase", "erase all programs and names (no reply)"),
        ("erase_check", "A", ":n01=?", "erase", "read name 1 after erase"),
        ("reset", "A", ":w95=12021,", "erase", "restart the display MCU (no reply)"),
    ]


DEFAULT_TIERS = {"query", "display"}
TIER_FLAG = {"write": "--write", "cal": "--cal", "erase": "--erase", "generator": "--generator"}


def run(args) -> int:
    tiers = set(DEFAULT_TIERS)
    for tier, flag in TIER_FLAG.items():
        if getattr(args, tier.replace("generator", "generator")):
            tiers.add(tier)
    commands = [c for c in catalog(WRITE_SLOT) if c[3] in tiers]

    print("Make sure the GX Pro is connected by USB and shows the PC screen, and that nothing is")
    print("connected to the generator outputs.")
    if {"cal", "erase"} & tiers:
        print("\n!! This run writes calibration or erases programs. Have a full SPI-flash backup")
        print("   (tools/gx_link.py dump) ready to restore.")
    if not args.yes:
        input("\nPress Enter to start (Ctrl+C to cancel)... ")

    dev = Device(find_links(args.port))
    saved = {}
    log = []
    try:
        version = dev.exchange("A", ":n00=$")
        fw = version[0] if version else "(no reply)"
        print(f"device firmware: {fw}\n")
        for cid, link, text, tier, desc in commands:
            for ln in (["A", "B"] if link == "AB" else [link]):
                reply = dev.exchange(ln, text)
                saved[cid] = reply
                entry = {"id": cid, "link": ln, "tier": tier, "sent": text,
                         "reply": reply, "description": desc}
                log.append(entry)
                shown = " | ".join(reply) if reply else "(no reply)"
                print(f"  {ln} {text:60s} -> {shown}")
        if "write" in tiers:
            _restore_slot(dev, saved, log)
    finally:
        dev.close()

    result = {"label": args.label or (Path(args.save).stem if args.save else "run"),
              "firmware": fw, "time": time.strftime("%Y-%m-%d %H:%M:%S"),
              "tiers": sorted(tiers), "log": log}
    if args.save:
        Path(args.save).parent.mkdir(parents=True, exist_ok=True)
        Path(args.save).write_text(json.dumps(result, indent=1, ensure_ascii=False))
        print(f"\nsaved {args.save} ({len(log)} commands)")
    return 0


def _restore_slot(dev: Device, saved: dict, log: list) -> None:
    """Re-upload slot WRITE_SLOT from the read-back captured at the start of the write tier."""
    prog = _first(saved.get("save_current_prog"), ":p")
    steps = _first(saved.get("save_current_steps"), ":g")
    name = saved.get("save_current_name", [])
    name_text = name[0].split("=", 1)[1] if name and "=" in name[0] else ""
    print("\n  restoring slot", WRITE_SLOT)
    for text, note in ((steps, "gates"), (prog, "program")):
        if text:
            reply = dev.exchange("A", text)
            log.append({"id": f"restore_{note}", "link": "A", "tier": "write", "sent": text,
                        "reply": reply, "description": f"restore original {note}"})
            print(f"  A {text[:40]}… -> {' | '.join(reply) or '(no reply)'}")
    if prog is None and steps is None:
        print("  (slot was empty; leaving it erased)")
    text = f":n{WRITE_SLOT:02d}={name_text}" if name_text else f":n{WRITE_SLOT:02d}=,"
    reply = dev.exchange("A", text)
    log.append({"id": "restore_name", "link": "A", "tier": "write", "sent": text,
                "reply": reply, "description": "restore original name"})


def _first(reply, prefix):
    if reply:
        for line in reply:
            if line.startswith(prefix):
                return line
    return None


def compare(a_path: Path, b_path: Path) -> int:
    a, b = json.loads(a_path.read_text()), json.loads(b_path.read_text())
    la = {(e["id"], e["link"]): e for e in a["log"]}
    lb = {(e["id"], e["link"]): e for e in b["log"]}
    print(f"{a['label']} ({a['firmware']}) vs {b['label']} ({b['firmware']})\n")
    diffs = 0
    for key in [k for k in la if k in lb]:
        ea, eb = la[key], lb[key]
        ra, rb = ea["reply"], eb["reply"]
        mark = "  " if ra == rb else "!!"
        if ra != rb:
            diffs += 1
        if ra != rb or not ra:
            print(f"{mark} {ea['link']} {ea['sent']:56s} {ea['description']}")
            print(f"     {a['label']:9s}: {' | '.join(ra) or '(no reply)'}")
            print(f"     {b['label']:9s}: {' | '.join(rb) or '(no reply)'}")
    only = sorted(set(la) ^ set(lb))
    for key in only:
        print(f"!! only in one log: {key}")
    print(f"\n{diffs} reply difference(s) across {len(set(la) & set(lb))} shared commands"
          f"{'; logs match' if diffs == 0 and not only else ''}")
    return 1 if diffs or only else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run", help="send commands to the device and log the replies")
    r.add_argument("--save", help="write the log to this JSON file")
    r.add_argument("--label", help="name of this run (default: file name of --save)")
    r.add_argument("--port", action="append", help="serial device (repeatable); default: auto-detect")
    r.add_argument("--write", action="store_true", help="also upload/read-back/restore one program slot")
    r.add_argument("--cal", action="store_true", help="also write and read calibration values")
    r.add_argument("--erase", action="store_true", help="also erase all programs and reset (needs a backup)")
    r.add_argument("--generator", action="store_true", help="also send generator reads (reply only in a Spooky2 session)")
    r.add_argument("-y", "--yes", action="store_true", help="do not ask before starting")
    c = sub.add_parser("compare", help="compare two logs")
    c.add_argument("a", type=Path)
    c.add_argument("b", type=Path)
    args = ap.parse_args()
    if args.cmd == "compare":
        return compare(args.a, args.b)
    if args.port:
        args.port = {"A": args.port[0], "B": args.port[1]} if len(args.port) == 2 else None
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
