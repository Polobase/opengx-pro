#!/usr/bin/env python3
"""Pad a plain application image for the image-size spike (APP=sizeprobe, docs/nx/drop0-checklist.md).

Appends a pseudo-random filler (xorshift32, seeded with SEED ^ total length: no long 0xFF runs, so
a block the bootloader skipped is noticed, and no two PAD_KB sizes share a filler prefix, so a
trailer stale from an earlier, smaller PAD_KB run fails its CRC instead of validating against
whatever prefix the new, larger image happens to share with it) and a 16-byte trailer
{b"NXSZ", u32 length, u32 crc32, u32 ~crc32} (little endian; crc32 = zlib over every byte before
the trailer, length = the whole padded image). The sizeprobe app finds the trailer, recomputes the
CRC over the flash and shows whether the image arrived.

Usage:
  pad_image.py IN.bin OUT.bin --kb N     pad to N*1024 bytes (at most MAX_KB)
"""

from __future__ import annotations

import argparse
import struct
import sys
import zlib
from pathlib import Path

MAX_KB = 111        # 111 KB + the 4-byte firmware ID still fit the 56 flasher blocks (112 KB)
SEED = 0x2545F491
MAGIC = b"NXSZ"


def filler(n: int, seed: int) -> bytes:
    x, out = seed, bytearray()
    while len(out) < n:
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        out += struct.pack("<I", x)
    return bytes(out[:n])


def pad(image: bytes, kb: int) -> bytes:
    if not 1 <= kb <= MAX_KB:
        raise ValueError(f"--kb must be 1..{MAX_KB} (the application region is 112 KB)")
    total = kb * 1024
    if len(image) + 16 > total:
        raise ValueError(f"image is {len(image)} bytes: no room for the trailer in {kb} KB")
    # Seed depends on total so every PAD_KB gets its own filler stream (not a shared prefix):
    # a stale trailer left behind by a smaller, earlier PAD_KB run then fails its own CRC check
    # instead of validating against bytes a later, larger image happens to still share with it.
    body = image + filler(total - 16 - len(image), SEED ^ total)
    crc = zlib.crc32(body) & 0xFFFFFFFF
    return body + MAGIC + struct.pack("<III", total, crc, crc ^ 0xFFFFFFFF)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("src", type=Path)
    ap.add_argument("dst", type=Path)
    ap.add_argument("--kb", type=int, required=True)
    args = ap.parse_args()
    try:
        out = pad(args.src.read_bytes(), args.kb)
    except ValueError as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    args.dst.write_bytes(out)
    crc = struct.unpack_from("<I", out, len(out) - 8)[0]
    print(f"{args.dst}: {len(out)} bytes ({args.src.stat().st_size} code + filler + trailer), crc32 {crc:08X}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
