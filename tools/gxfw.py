#!/usr/bin/env python3
"""Decode / encode GX Pro display firmware images.

The images that FWUpd2.exe flashes are an STM32G070 (Cortex-M0+) application
linked at 0x08004000, obfuscated in two layers:

  1. Every byte whose value is 1 mod 3 has its nibbles swapped (0x46 <-> 0x64).
     Such a byte stays 1 mod 3 after the swap, so the step is its own inverse.
  2. The bytes of each 16-byte block are shuffled: plain[i] = stored[SHUFFLE[i]].

The last 4 bytes of an image are a big-endian firmware ID (e.g. b"X2Vx"). They
are not obfuscated, and the flasher never sends them.

Usage:
  python3 gxfw.py decode GxDisplayFW.bin app.bin     # -> plain Cortex-M image
  python3 gxfw.py encode app.bin GxCustomFW.bin --id X2Vx
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

LOAD_ADDRESS = 0x08004000
SHUFFLE = (10, 9, 5, 13, 8, 2, 12, 14, 4, 1, 0, 15, 6, 3, 7, 11)
BLOCK = len(SHUFFLE)


def swap_nibbles(value: int) -> int:
    return ((value << 4) | (value >> 4)) & 0xFF if value % 3 == 1 else value


def decode(image: bytes) -> tuple[bytes, bytes]:
    """Return (plain application, firmware ID)."""
    body, fw_id = image[:-4], image[-4:]
    if len(body) % BLOCK:
        raise ValueError(f"image body is {len(body)} bytes, not a multiple of {BLOCK}")
    plain = bytearray()
    for off in range(0, len(body), BLOCK):
        plain += bytes(swap_nibbles(body[off + s]) for s in SHUFFLE)
    return bytes(plain), fw_id


def encode(plain: bytes, fw_id: bytes) -> bytes:
    if len(fw_id) != 4:
        raise ValueError("firmware ID must be 4 bytes")
    plain = plain + b"\xff" * (-len(plain) % BLOCK)
    body = bytearray(len(plain))
    for off in range(0, len(plain), BLOCK):
        for i, s in enumerate(SHUFFLE):
            body[off + s] = swap_nibbles(plain[off + i])
    return bytes(body) + fw_id


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="cmd", required=True)
    d = sub.add_parser("decode", help="obfuscated image -> plain application binary")
    d.add_argument("src", type=Path)
    d.add_argument("dst", type=Path)
    e = sub.add_parser("encode", help="plain application binary -> flashable image")
    e.add_argument("src", type=Path)
    e.add_argument("dst", type=Path)
    e.add_argument("--id", default="X2Vx", help="4-character firmware ID (default: X2Vx)")
    args = parser.parse_args()

    if args.cmd == "decode":
        plain, fw_id = decode(args.src.read_bytes())
        args.dst.write_bytes(plain)
        print(f"{args.dst}: {len(plain)} bytes, load address 0x{LOAD_ADDRESS:08X}, firmware ID {fw_id!r}")
    else:
        image = encode(args.src.read_bytes(), args.id.encode("latin-1"))
        args.dst.write_bytes(image)
        print(f"{args.dst}: {len(image)} bytes, firmware ID {args.id!r}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
