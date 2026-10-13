#!/usr/bin/env python3
"""Talk to the OpenGX Pro link-probe firmware over the GX Pro's two USB serial channels.

  gx_link.py probe                 map which PC channel reaches which display UART (both PB2 states)
  gx_link.py monitor [--send CH:TEXT]   print every line arriving on either channel
  gx_link.py dump [FILE]           back up the whole 2 MB SPI flash (hex over serial, CRC-checked)

All traffic uses lines starting with '#'; the generator boards' protocol starts with ':'.
probe/monitor/dump need the link-probe firmware (make APP=probe flash).
"""

from __future__ import annotations

import argparse
import hashlib
import sys
import time
import zlib
from datetime import datetime
from pathlib import Path

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial is missing: run this with .venv/bin/python")

WCH_VID = 0x1A86
SPI_FLASH_SIZE = 0x200000
CHUNK = 0x8000
BACKUP_DIR = Path(__file__).resolve().parent.parent / "backups"


def find_channels(requested: list[str] | None) -> list[str]:
    if requested:
        return requested
    ports = [p for p in list_ports.comports() if p.vid == WCH_VID]
    wch = sorted(p.device for p in ports if "wchusbserial" in p.device)
    found = wch or sorted(p.device for p in ports)
    if not found:
        sys.exit("no WCH USB-serial channel found; is the GX Pro connected?")
    return found


class Link:
    def __init__(self, devices: list[str]):
        self.ser = {}
        for dev in devices:
            s = serial.Serial()
            s.port, s.baudrate, s.timeout = dev, 115200, 0
            s.dtr = s.rts = False
            s.open()
            self.ser[dev] = s
        self.pending = {dev: bytearray() for dev in devices}

    @staticmethod
    def short(dev: str) -> str:
        return "…" + dev[-3:]

    def send(self, dev: str, text: str) -> None:
        self.ser[dev].write(text.encode("ascii") + b"\r\n")

    def drain(self) -> None:
        for dev, s in self.ser.items():
            s.reset_input_buffer()
            self.pending[dev].clear()

    def lines(self, seconds: float, stop=None):
        """Yield (device, line) for `seconds`, or until stop(device, line) is true."""
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            idle = True
            for dev, s in self.ser.items():
                data = s.read(s.in_waiting or 1)
                if not data:
                    continue
                idle = False
                buf = self.pending[dev]
                buf += data
                while b"\n" in buf:
                    raw, _, rest = bytes(buf).partition(b"\n")
                    buf[:] = rest
                    line = raw.decode("latin-1").strip()
                    if line:
                        yield dev, line
                        if stop and stop(dev, line):
                            return
            if idle:
                time.sleep(0.002)


def cmd_probe(link: Link) -> int:
    devs = list(link.ser)
    print("channels:", ", ".join(f"{link.short(d)} = {d}" for d in devs))
    findings = []
    for pb2 in (0, 1):
        print(f"\n== PB2 = {pb2}")
        link.drain()
        for dev in devs:
            link.send(dev, f"#pb2 {pb2}")
        for dev, line in link.lines(0.6):
            print(f"  {link.short(dev)} <- {line}")
        for sent_on in devs:
            link.drain()
            link.send(sent_on, "#ping")
            heard = [(dev, line) for dev, line in link.lines(0.6)]
            if not heard:
                print(f"  #ping on {link.short(sent_on)}: no reply on any channel")
            for dev, line in heard:
                print(f"  #ping on {link.short(sent_on)}: {link.short(dev)} <- {line}")
                if line.startswith("#pong"):
                    fields = dict(f.split("=") for f in line.split()[1:] if "=" in f)
                    findings.append((pb2, link.short(sent_on), fields.get("rx"), fields.get("tx"), link.short(dev)))
    # Leave the device reachable: PB2 = 1 disconnects the PC, so try to switch it back.
    link.drain()
    for dev in devs:
        link.send(dev, "#pb2 0")
    if not any(line.startswith("#pb2 0") for _, line in link.lines(0.6)):
        print("\n!! The device did not answer '#pb2 0'. Press LOOP once on the device (header must show PB2 0).")
    print("\nSummary (PB2, PC sends on -> display receives on; display sends on -> PC hears on):")
    for pb2, sent, rx, tx, heard in sorted(set(findings)):
        print(f"  PB2={pb2}: PC {sent} -> display {rx};  display {tx} -> PC {heard}")
    if not findings:
        print("  nothing answered. Is the link-probe firmware running? Try pressing LOOP (toggles PB2)"
              " and START (sends #hello) on the device while `gx_link.py monitor` runs.")
    return 0


def cmd_monitor(link: Link, sends: list[str]) -> int:
    print("listening on", ", ".join(link.short(d) for d in link.ser), "(Ctrl+C to stop)")
    for spec in sends:
        ch, _, text = spec.partition(":")
        dev = next(d for d in link.ser if d.endswith(ch))
        link.send(dev, text)
        print(f"  {link.short(dev)} -> {text}")
    start = time.monotonic()
    try:
        for dev, line in link.lines(10 ** 9):
            print(f"{time.monotonic() - start:8.3f}s {link.short(dev)} <- {line}")
    except KeyboardInterrupt:
        pass
    return 0


def find_dump_channel(link: Link) -> str | None:
    for dev in link.ser:
        link.drain()
        link.send(dev, "#id")
        for got_on, line in link.lines(0.8, stop=lambda d, l: l.startswith("#id")):
            if got_on == dev and line.startswith("#id"):
                print(f"device: {line[4:]} (via {link.short(dev)})")
                return dev
    return None


def dump_chunk(link: Link, dev: str, addr: int, length: int) -> bytes:
    link.drain()
    link.send(dev, f"#dump {addr:06X} {length:06X}")
    data, expect = bytearray(), addr
    timeout = length * 2.4 / 11520 * 1.5 + 2
    for got_on, line in link.lines(timeout, stop=lambda d, l: d == dev and l.startswith(("#end", "#error"))):
        if got_on != dev:
            continue
        if line.startswith("#d "):
            _, a, hexdata = line.split()
            if int(a, 16) != expect:
                raise ValueError(f"address jump: expected {expect:06X}, got {a}")
            chunk = bytes.fromhex(hexdata)
            data += chunk
            expect += len(chunk)
        elif line.startswith("#error"):
            raise ValueError(line)
        elif line.startswith("#end"):
            _, a, n, crc = line.split()
            if int(a, 16) != addr or int(n, 16) != length or len(data) != length:
                raise ValueError(f"incomplete chunk: {len(data)} of {length} bytes")
            if int(crc, 16) != zlib.crc32(data):
                raise ValueError("CRC mismatch")
            return bytes(data)
    raise ValueError(f"timeout after {len(data)} of {length} bytes")


def cmd_dump(link: Link, out: Path | None, start: int, length: int) -> int:
    dev = find_dump_channel(link)
    if dev is None:
        print("ERROR: no channel answered #id. Run `gx_link.py probe` first.", file=sys.stderr)
        return 1
    if out is None:
        BACKUP_DIR.mkdir(exist_ok=True)
        out = BACKUP_DIR / f"spi_flash_{datetime.now():%Y%m%d-%H%M%S}.bin"
    image = bytearray()
    t0 = time.monotonic()
    for addr in range(start, start + length, CHUNK):
        n = min(CHUNK, start + length - addr)
        for attempt in range(1, 4):
            try:
                image += dump_chunk(link, dev, addr, n)
                break
            except ValueError as e:
                print(f"\n  chunk {addr:06X}: {e} (attempt {attempt}/3)")
        else:
            print("ERROR: giving up; nothing written.", file=sys.stderr)
            return 1
        done = len(image)
        rate = done / (time.monotonic() - t0)
        print(f"\r  {done // 1024:5d} / {length // 1024} KB  ({rate / 1024:.1f} KB/s, "
              f"{(length - done) / rate:4.0f} s left)", end="", flush=True)
    print()
    out.write_bytes(image)
    used = [a for a in range(0, len(image), 0x1000) if image[a:a + 0x1000] != b"\xff" * 0x1000]
    print(f"saved {out} ({len(image)} bytes, sha256 {hashlib.sha256(image).hexdigest()[:16]}…)")
    print(f"non-empty 4 KB sectors: {len(used)} — " +
          ", ".join(f"{start + a:06X}" for a in used[:40]) + (" …" if len(used) > 40 else ""))
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", action="append", help="serial device, repeatable (default: all WCH channels)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("probe")
    m = sub.add_parser("monitor")
    m.add_argument("--send", action="append", default=[], help="CHANNEL-SUFFIX:TEXT, e.g. 901:#ping")
    d = sub.add_parser("dump")
    d.add_argument("file", nargs="?", type=Path)
    d.add_argument("--start", type=lambda s: int(s, 0), default=0)
    d.add_argument("--length", type=lambda s: int(s, 0), default=SPI_FLASH_SIZE)
    args = ap.parse_args()

    link = Link(find_channels(args.port))
    if args.cmd == "probe":
        return cmd_probe(link)
    if args.cmd == "monitor":
        return cmd_monitor(link, args.send)
    return cmd_dump(link, args.file, args.start, args.length)


if __name__ == "__main__":
    sys.exit(main())
