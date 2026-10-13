"""Firmware images for the emulator: our ELF builds and the original image (reference mode)."""

import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # tools/: the image codec
import gxfw  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]          # repository root
REFERENCE_BIN = ROOT / "original" / "GxDisplayFW.bin"   # the user's copy of the vendor's update file
NAMES_DIR = Path(__file__).resolve().parent / "names"   # addresses of the original image's functions and data
APP_BASE = 0x08004000


@dataclass
class Firmware:
    name: str
    image: bytes
    base: int = APP_BASE
    reference: bool = False
    syms: dict = field(default_factory=dict)        # name -> address (code: Thumb bit clear)
    delay_loop: int = None                          # busy loop fast-forwarded at 4 cycles/iteration
    power_off: int = None                           # entry of the power-off signal loop
    fault_handlers: dict = field(default_factory=dict)   # name -> address: reaching it is a violation
    primask_enables: list = field(default_factory=list)  # addresses right after cpsie / msr primask
    hle: dict = field(default_factory=dict)         # addr -> (kind, cycles): functions run in Python


def _vector(image, n):
    return struct.unpack_from("<I", image, 4 * n)[0] & ~1


def _primask_enables(image, base):
    """Addresses following `cpsie i` (B662) or `msr primask, rN` (F38N 8810) in the image."""
    out = []
    for off in range(0, len(image) - 4, 2):
        hw = image[off] | image[off + 1] << 8
        if hw == 0xB662:
            out.append(base + off + 2)
        elif hw & 0xFFF0 == 0xF380 and image[off + 2] | image[off + 3] << 8 == 0x8810:
            out.append(base + off + 4)
    return out[:64]


def load_elf(elf):
    elf = Path(elf)
    out = subprocess.run(["arm-none-eabi-nm", str(elf)], capture_output=True, text=True, check=True).stdout
    syms = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3:
            syms[parts[2]] = int(parts[0], 16)
    binp = elf.with_suffix(".bin")
    if binp.exists() and binp.stat().st_mtime >= elf.stat().st_mtime:
        image = binp.read_bytes()
    else:
        with tempfile.TemporaryDirectory() as d:
            tmp = Path(d) / "fw.bin"
            subprocess.run(["arm-none-eabi-objcopy", "-O", "binary", str(elf), str(tmp)], check=True)
            image = tmp.read_bytes()
    faults = {n: syms[n] for n in ("Default_Handler", "HardFault_Handler") if n in syms}
    return Firmware(name=str(elf), image=image, syms=syms, delay_loop=syms.get("delay_loop"),
                    power_off=syms.get("power_off_signal"), fault_handlers=faults,
                    primask_enables=_primask_enables(image, APP_BASE))


def load_names(names_dir=NAMES_DIR):
    syms = {}
    for tsv in sorted(Path(names_dir).glob("*.tsv")):
        for line in tsv.read_text().splitlines():
            if not line.strip() or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) >= 2 and parts[0].startswith("0x"):
                syms.setdefault(parts[1].strip(), int(parts[0], 16) & ~1 if int(parts[0], 16) < 0x20000000
                                else int(parts[0], 16))
    return syms


def reference_image(path=REFERENCE_BIN):
    """The original firmware as the plain image at 0x08004000: from the vendor's update file (its
    last 4 bytes are the firmware ID; decoded here, tools/gxfw.py) or from an already decoded image."""
    data = Path(path).read_bytes()
    if data[-4:-2] in (b"X0", b"X1", b"X2", b"X3", b"M0", b"M1", b"M2", b"M3") and (len(data) - 4) % gxfw.BLOCK == 0:
        return gxfw.decode(data)[0]
    return data


def load_reference(path=REFERENCE_BIN, names_dir=NAMES_DIR, use_hle=True):
    """The original firmware (plain image at 0x08004000) with the names of tools/sim/names/*.tsv."""
    image = reference_image(path)
    syms = load_names(names_dir)
    faults = {"HardFault_Handler": _vector(image, 3), "Default_Handler": _vector(image, 16)}
    # The LCD byte/pixel senders (display.md section 3) dominate the run time when a name
    # scrolls. They run in Python with the cycle cost the emulated code has (measured with
    # hle=False: data16 77, cmd 52, data8 52 cycles from entry to return, minus the
    # entry block (10, 11, 11) the block hook has already counted); interrupts wait until they return.
    hle = {syms.get("lcd_write_data16", 0x0800D8D8): ("lcd_data16", 67),
           syms.get("lcd_write_cmd", 0x0800D980): ("lcd_cmd", 41),
           syms.get("lcd_write_data8", 0x0800D938): ("lcd_data8", 41)} if use_hle else {}
    return Firmware(name=f"reference ({Path(path).name})", image=image, reference=True, syms=syms,
                    delay_loop=syms.get("delay_loop", 0x08012518), power_off=None,
                    fault_handlers=faults, primask_enables=_primask_enables(image, APP_BASE), hle=hle)


def load(target, use_hle=True):
    """'reference' / 'original' / a .bin of the original (the update file or its decoded image) ->
    reference mode, else an ELF path."""
    if target in ("reference", "original", "ref") or str(target).endswith(("GxDisplayFW.bin", "GxDisplayFW_decoded.bin")):
        return load_reference(REFERENCE_BIN if target in ("reference", "original", "ref") else target,
                              use_hle=use_hle)
    return load_elf(target)
