#!/usr/bin/env python3
"""Check a release build of the nx firmware (`make release`) and name its files.

The image that is published must hold nothing of the original firmware image, must carry the
built-in Terrain Protocol and preset from the committed tables, and must fit what the bootloader
is proven to take. This tool checks the finished build directory:

  - the compiler flags: NX_RELEASE=1, the Terrain and preset tables on, no vendor splash, no test
    timing;
  - the map file and the directory: no object or source generated from the original image
    (nx_vendor, assets), and the two tables linked from src/nx/data/;
  - the image is not larger than --limit (FLASH_PROVEN);
  - the update file is exactly the image encoded with ID X2Vx.

Then it writes, next to the build:

  opengx-pro-<version>.bin          the update file (what the flashers send)
  opengx-pro-<version>.bin.sha256   its SHA-256, `shasum -c` format
  manifest.json                     version, channel, sizes, SHA-256, commit and date

    python3 tools/release_check.py build/release [--limit 112640] [--strict]

--strict (the release workflow) also refuses uncommitted changes and a tag on HEAD that is not
v<version>. Exit status 1 when a check fails. Standard library only.
"""

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gxfw  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
FW_ID = b"X2Vx"
BLOCK_SIZE = 2048   # bytes of image per update packet
# Flags and files of what comes out of the original firmware image (the Makefile's NX_VENDOR and
# the classic apps' assets) and of the test timing (NX_TIME_X): never in a release.
BAD_FLAGS = ("-DNX_VENDOR_SPLASH", "-DNX_DWELL_UNITS")
BAD_STEMS = ("nx_vendor", "assets")
# The built-in Terrain Protocol and preset: in every release, from the committed tables.
NEED_FLAGS = ("-DNX_TERRAIN_DATA=1", "-DNX_PRESET_DATA=1")
NEED_OBJECTS = ("nx/data/nx_terrain_data.o", "nx/data/nx_preset_data.o")


def git(*args: str) -> str:
    try:
        return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


def version() -> str:
    m = re.search(r'#define\s+NX_FW_VERSION\s+"([^"]+)"', (ROOT / "src/nx/nx_config.h").read_text())
    if not m:
        raise SystemExit("release_check: NX_FW_VERSION not found in src/nx/nx_config.h")
    return m.group(1)


def channel(ver: str) -> str:
    """beta: every version before 1.0.0 and every pre-release (1.2.0-rc1); else stable."""
    return "beta" if ver.startswith("0.") or "-" in ver else "stable"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build", type=Path, help="the release build directory (build/release)")
    ap.add_argument("--limit", type=int, default=112640, help="largest image in bytes (FLASH_PROVEN)")
    ap.add_argument("--strict", action="store_true", help="also refuse uncommitted changes and a wrong tag")
    args = ap.parse_args()
    build: Path = args.build
    errors: list[str] = []

    flags = (build / "cflags.stamp").read_text()
    if "-DNX_RELEASE=1" not in flags:
        errors.append("not a release build: -DNX_RELEASE=1 is missing from the compiler flags")
    errors += [f"compiler flag {f} in a release build" for f in BAD_FLAGS if f in flags]
    errors += [f"compiler flag {f} is missing" for f in NEED_FLAGS if f not in flags]

    linked = (build / "gxpro.map").read_text()
    objects = set(re.findall(r"([\w.-]+)\.o\b", linked))
    errors += [f"{stem}.o is linked into the image" for stem in BAD_STEMS if stem in objects]
    errors += [f"{o} (the committed table) is not linked into the image" for o in NEED_OBJECTS if o not in linked]
    errors += [f"{p.relative_to(build)} is in the build directory"
               for stem in BAD_STEMS for p in sorted(build.rglob(f"{stem}.[co]"))]

    image = (build / "gxpro.bin").read_bytes()
    update = (build / "GxCustomFW.bin").read_bytes()
    if len(image) > args.limit:
        errors.append(f"image is {len(image)} bytes, more than the proven {args.limit}")
    if update != gxfw.encode(image, FW_ID):
        errors.append("GxCustomFW.bin is not gxpro.bin encoded with ID X2Vx")
    elif gxfw.decode(update)[0][:len(image)] != image:
        errors.append("the update file does not decode back to the image")

    ver = version()
    commit = git("rev-parse", "--short", "HEAD") or "unknown"
    dirty = bool(git("status", "--porcelain", "--untracked-files=no"))
    if args.strict:
        if dirty:
            errors.append("uncommitted changes in the working tree")
        tags = git("tag", "--points-at", "HEAD").split()
        if f"v{ver}" not in tags:
            errors.append(f"HEAD is not tagged v{ver} (tags here: {' '.join(tags) or 'none'})")

    if errors:
        for e in errors:
            print(f"release_check: {e}", file=sys.stderr)
        return 1

    name = f"opengx-pro-{ver}.bin"
    digest = hashlib.sha256(update).hexdigest()
    (build / name).write_bytes(update)
    (build / f"{name}.sha256").write_text(f"{digest}  {name}\n")
    manifest = {
        "name": "OpenGX Pro",
        "version": ver,
        "channel": channel(ver),
        "file": name,
        "bytes": len(update),
        "image_bytes": len(image),
        "blocks": -(-len(image) // BLOCK_SIZE),
        "sha256": digest,
        "commit": commit + ("+changes" if dirty else ""),
        "date": git("log", "-1", "--format=%cs") or "unknown",
    }
    (build / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"{build / name}: OpenGX Pro {ver} ({manifest['channel']}), image {len(image)} of {args.limit} bytes "
          f"({args.limit - len(image)} free), {manifest['blocks']} blocks, sha256 {digest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
