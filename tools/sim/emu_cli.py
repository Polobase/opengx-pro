"""Command line of tools/emu.py: one run of a firmware on the emulated board."""

import argparse
import codecs
import sys
from pathlib import Path

from unicorn import UcError
from unicorn import arm_const as A

from . import scenario as S
from .firmware import load
from .machine import Board, BoardConfig, Inputs

DOC = """Run the firmware on an emulated GX Pro display board and render what the LCD shows.

Board model (tools/sim/): Cortex-M0+ (Unicorn) with NVIC, SysTick and exception entry/return;
RCC/FLASH/PWR, GPIO (keys, PB1), timers with compare channels, SPI1 with a W25Q16 SPI NOR (full
command set, write tracking), SPI2 with an ST7789 panel (framebuffer, scrolling), DMA1/DMAMUX1,
USART1-4 (FIFO mode), IWDG, CRC, ADC (internal channels). Time: 1 cycle per instruction halfword
at 64 MHz, delay_loop at 4 cycles/iteration, SPI/UART transfers take their bit times.

Safety checks: UART transmission (unless --allow-tx), internal-flash controller access,
writes into the flash address range, SPI-flash program/erase (unless --allow-spi-write) and
faults are violations (exit 1).

ELF may also be 'reference' (the original firmware: your copy of the vendor's update file,
original/GxDisplayFW.bin), which runs with the internal-flash programming it does on a first boot
emulated.

Screenshots follow the backlight duty (full at --bl-full, default 0.5 = the classic 8/16).

Examples:
  emu.py build/gxpro.elf --until 4000 --shot 1500:splash.png --shot 3900:screen.png
  emu.py build/gxpro.elf --key 4000-5000:POWER --until 12000 --shot 11000:off.png
  emu.py build/gxpro.elf --allow-tx --rx '3000:U1:#ping\\r\\n' --until 4000 --tx-log tx.txt
  emu.py reference --allow-tx --generators 1 --spi-image backup --until 7000 --shot 6500:home.png
  emu.py build/nx/gxpro.elf --shot-hook nx_shot:build/nx/shots --until 8000
  emu.py build/nx/gxpro.elf --frames 4000:4400:20:build/nx/anim --until 4500
"""


def build_parser():
    ap = argparse.ArgumentParser(description=DOC, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("elf", help="ELF (with the .bin next to it) or 'reference'")
    ap.add_argument("--until", type=float, default=5000, help="simulated ms to run")
    ap.add_argument("--shot", action="append", default=[], help="MS:FILE.png screenshot")
    ap.add_argument("--key", action="append", default=[], help="FROM-TO:KEY[+KEY] held between those ms")
    ap.add_argument("--rx", action="append", default=[], help="MS:U1|U2|U3:TEXT received (escapes like \\r\\n allowed)")
    ap.add_argument("--allow-tx", action="store_true", help="UART transmission is expected, not a violation")
    ap.add_argument("--tx-log", help="write everything transmitted, one line per UART line, to this file")
    ap.add_argument("--spi-image", help="2 MB SPI-flash image, or 'backup' for the newest backups/ dump "
                                        "(default: deterministic test pattern with 'Test' at 0x5000)")
    ap.add_argument("--variant", type=lambda s: int(s, 0), default=0xFF00,
                    help="halfword at 0x08003C00 (user unit: 0xFF00 = variant A)")
    ap.add_argument("--pb1", type=int, default=0)
    ap.add_argument("--init-log", action="store_true", help="print LCD commands (except pixel data)")
    # additions of the tools/sim board
    ap.add_argument("--allow-spi-write", action="store_true", help="SPI-flash program/erase is expected")
    ap.add_argument("--generators", type=float, metavar="MS",
                    help="generator boards on USART1/2 answer every ':w' line with ':ok' after MS ms")
    ap.add_argument("--pb1-at", action="append", default=[], metavar="MS:LEVEL", help="PB1 level change")
    ap.add_argument("--no-hle", action="store_true", help="reference: emulate the LCD senders instruction by instruction")
    ap.add_argument("--shot-hook", action="append", default=[], metavar="SYMBOL:DIR",
                    help="save a screenshot to DIR every time the firmware calls SYMBOL, as NNN_<name>.png "
                         "with <name> from the C string in r1 (the function's second argument)")
    ap.add_argument("--frames", metavar="FROM:TO:STEP:DIR",
                    help="save a screenshot every STEP ms from FROM to TO (inclusive) into DIR as <ms>ms.png")
    ap.add_argument("--bl-full", type=float, default=0.5, metavar="DUTY",
                    help="backlight duty shown as full brightness in screenshots (default 0.5)")
    return ap


def default_spi_image():
    img = bytearray(((a * 7) ^ (a >> 8)) & 0xFF for a in range(0x200000))
    img[0x5000:0x5005] = b"Test\x00"
    return bytes(img)


def c_string(b, ptr, limit=48):
    """The NUL-terminated string at ptr as a file-name part ('' when unreadable)."""
    out = []
    for i in range(limit):
        try:
            c = b.uc.mem_read(ptr + i, 1)[0]
        except UcError:
            break
        if c == 0:
            break
        ch = chr(c)
        out.append(ch if ch.isascii() and (ch.isalnum() or ch in "-_.") else "_")
    return "".join(out)


def main(argv=None):
    args = build_parser().parse_args(argv)
    fw = load(args.elf, use_hle=not args.no_hle)
    inputs = Inputs(pb1=args.pb1)
    for spec in args.key:
        span, names = spec.split(":")
        start, end = (float(x) for x in span.split("-"))
        inputs.add_key(start, end, names)
    for spec in args.pb1_at:
        t, level = spec.split(":")
        inputs.set_pb1(float(t), int(level))
    if args.spi_image == "backup":
        spi = S.newest_backup().read_bytes()
    elif args.spi_image:
        spi = Path(args.spi_image).read_bytes()
    else:
        spi = default_spi_image()
    gens = {"U1": args.generators, "U2": args.generators} if args.generators is not None else {}
    cfg = BoardConfig(reference=fw.reference, variant=args.variant, spi_image=spi, allow_tx=args.allow_tx,
                      allow_spi_write=args.allow_spi_write, generators=gens, stop_on_power_off=True,
                      bl_full=args.bl_full)
    b = Board(fw, cfg, inputs)
    for spec in args.rx:
        t, port, text = spec.split(":", 2)
        data = codecs.decode(text, "unicode_escape").encode("latin-1")
        b.at(float(t), lambda port=port, data=data: b.inject(port, b.ms, data))

    def snap(path):
        bl = b.backlight()
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        b.lcd.image(backlight=bl, full=b.cfg.bl_full).save(path)
        b.event(f"screenshot {path} (backlight duty {bl:.2f})")

    shots = sorted((float(s.split(":")[0]), s.split(":", 1)[1]) for s in args.shot)
    done = set()

    def save(i, path):
        snap(path)
        done.add(i)

    for i, (t, path) in enumerate(shots):
        b.at(t, lambda i=i, path=path: save(i, path))
    for spec in args.shot_hook:
        sym, _, out = spec.partition(":")
        if sym not in fw.syms or not out:
            print(f"--shot-hook {spec}: need SYMBOL:DIR with a symbol of {fw.name}")
            return 2
        addr, calls = fw.syms[sym] & ~1, [0]

        def on_call(a, s, out=Path(out), calls=calls):
            calls[0] += 1
            snap(out / f"{calls[0]:03d}_{c_string(b, b.fx.reg(A.UC_ARM_REG_R1)) or 'shot'}.png")
        b.fx.hook_code(on_call, addr, addr)
    if args.frames:
        try:
            t0, t1, step, out = args.frames.split(":", 3)
            t0, t1, step = float(t0), float(t1), float(step)
        except ValueError:
            print(f"--frames {args.frames}: need FROM:TO:STEP:DIR (ms)")
            return 2
        n = int((t1 - t0) // step) + 1 if step > 0 and t1 >= t0 else 0
        for k in range(n):
            t = t0 + k * step
            b.at(t, lambda t=t, out=Path(out): snap(out / f"{t:010.3f}ms.png"))
    b.run(args.until)
    for i, (t, path) in enumerate(shots):
        if i not in done:
            save(i, path)
    b.flush_lines()

    for t, what in b.events:
        print(f"{t:9.1f} ms  {what}")
    if args.init_log:
        for cmd, data in b.lcd.log[:40]:
            print(f"  LCD C{cmd:02X} {data.hex(' ')}")
    if args.tx_log:
        Path(args.tx_log).parent.mkdir(parents=True, exist_ok=True)
        with open(args.tx_log, "w") as f:
            for port, raw in sorted(b.tx_raw.items()):
                for line in raw.decode("latin-1").splitlines():
                    f.write(f"{port}: {line}\n")
    for port, raw in sorted(b.tx_raw.items()):
        print(f"{port} sent {len(raw)} bytes, first line: {raw.decode('latin-1').splitlines()[0]!r}")
    for w, (first, n) in b.warnings.items():
        print(f"warning: {w} (first at {first} ms, {n}x)")
    if b.nor.ops:
        print(f"SPI flash: {len(b.nor.ops)} program/erase operations, first: {b.nor.ops[0]}")
    reason = b.stop_reason if b.stop_reason != "time limit" else None
    print(f"stopped at {b.ms:.1f} ms ({reason or 'time limit'}), PB2={(b.gpio['B'].odr >> 2) & 1}")
    if b.violations:
        print("VIOLATIONS:")
        for v in b.violations[:20]:
            print("  " + v)
        return 1
    print("no violations" + ("" if args.allow_tx else " (no UART TX)") + ", no flash writes, no faults")
    return 0


if __name__ == "__main__":
    sys.exit(main())
