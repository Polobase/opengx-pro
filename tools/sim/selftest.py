"""Self-test of the emulator with tiny bare-metal images (selftest/*.c, linked like the
application at 0x08004000), one per model. Each image writes its results to res[]; the checks
read them and the board state after the run.

    cd tools && ../.venv/bin/python -m sim.selftest [NAME ...]     (needs arm-none-eabi-gcc)

Without names every test runs; the last line reads "selftest: N checks, F failed".
"""

import json
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

from PIL import Image, ImageChops
from unicorn import arm_const as A

from . import scenario as S
from .firmware import load_elf
from .machine import Board, BoardConfig, Inputs

HERE = Path(__file__).resolve().parent / "selftest"
TESTS = {}


def test(name):
    def register(fn):
        TESTS[name] = fn
        return fn
    return register


def build(src, tmp):
    """Compile selftest/<src> into tmp and load it."""
    elf = Path(tmp) / (Path(src).stem + ".elf")
    subprocess.run(["arm-none-eabi-gcc", "-mcpu=cortex-m0plus", "-mthumb", "-O1", "-nostdlib", "-ffreestanding",
                    "-fno-tree-loop-distribute-patterns", "-I", str(HERE), "-T", str(HERE / "selftest.ld"),
                    str(HERE / src), "-o", str(elf), "-lgcc"], check=True)   # libgcc: division helpers
    subprocess.run(["arm-none-eabi-objcopy", "-O", "binary", str(elf), str(elf.with_suffix(".bin"))], check=True)
    return load_elf(elf)


def results(b, fw, n=16):
    """The image's res[] words."""
    return struct.unpack(f"<{n}I", bytes(b.uc.mem_read(fw.syms["res"], 4 * n)))


def clean(b):
    text = "no violations" + (": " + "; ".join(v.strip() for v in b.violations[:3]) if b.violations else "")
    return text, not b.violations


@test("core")
def t_core(tmp):
    """USART1 (priority 0) must preempt a spinning TIM3 handler (priority 1), PendSV must wait for
    cpsie, WFI must sleep until an interrupt, TIM3 must tick 10x per SysTick tick."""
    fw = build("selftest.c", tmp)
    b = Board(fw, BoardConfig(allow_tx=False, allow_spi_write=False), Inputs())
    b.at(0.55, lambda: b.inject("U1", b.ms, b"Z"))
    b.run(300)
    res = results(b, fw, 8)
    return [("USART1 (prio 0) preempts a spinning TIM3 handler (prio 1)", res[0] == 1),
            ("PendSV stays pending while PRIMASK is set", res[1] == 0),
            ("pending PendSV is taken right after cpsie", res[2] == 1),
            ("WFI sleeps until an interrupt", res[3] >= 1),
            (f"TIM3 ticks per 100 SysTick ms = {res[4]} (expect 1000 +- 2)", abs(res[4] - 1000) <= 2),
            ("USART1 received byte 'Z'", res[5] == ord("Z")),
            ("test ran to the end", res[7] == 0xC0FFEE),
            clean(b)]


@test("spi16")
def t_spi16(tmp):
    """SPI data size: DS = 16 sends one frame per 16-bit write, high byte first; DS = 8 packs two
    frames per 16-bit write, low byte first."""
    fw = build("st_spi16.c", tmp)
    b = Board(fw, BoardConfig(allow_tx=False, allow_spi_write=False), Inputs())
    b.run(5)
    r = results(b, fw)
    return [(f"DS=16: 0x9F goes out first, reply frame 0x{r[0]:04X} (expect 0xFFEF)", r[0] == 0xFFEF),
            (f"DS=16: next frame 0x{r[1]:04X} (expect 0x4015)", r[1] == 0x4015),
            (f"DS=8 packing: 0x{r[2]:04X} (expect 0xEFFF, first byte in bits 0-7)", r[2] == 0xEFFF),
            (f"DS=8 packing: 0x{r[3]:04X} (expect 0x1540)", r[3] == 0x1540),
            ("test ran to the end", r[15] == 0xC0FFEE),
            clean(b)]


@test("dma")
def t_dma(tmp):
    """DMA1 + DMAMUX1: 128 16-bit frames memory -> SPI2 (DS = 16) into the LCD with HT and TC
    interrupts on IRQ 9, timed at the SPI rate; SPI1 full duplex (TX channel 3, RX channel 2,
    IRQ 10) reads the flash's JEDEC ID."""
    fw = build("st_dma.c", tmp)
    b = Board(fw, BoardConfig(allow_tx=False, allow_spi_write=False), Inputs())
    b.run(10)
    r = results(b, fw)
    im = b.lcd.image(scale=1)
    red, green = im.getpixel((5, 1)), im.getpixel((5, 6))
    unmodelled = sorted(u for u in b.unmodelled if u.startswith("periph@0x4002"))
    return [("SPI2 TX: one transfer-complete interrupt (IRQ 9)", r[0] == 1),
            ("SPI2 TX: one half-transfer interrupt", r[1] == 1),
            (f"TC {r[2]} cycles after TXDMAEN (127 frames x 32 cycles; expect 3900..4300)", 3900 <= r[2] <= 4300),
            (f"CNDTR afterwards {r[3]} (expect 0)", r[3] == 0),
            (f"frames 0-63 red on logical rows 0-3: {red}", red[0] > 200 and red[1] < 40 and red[2] < 40),
            (f"frames 64-127 green on rows 4-7: {green}", green[1] > 200 and green[0] < 40 and green[2] < 40),
            (f"SPI1 TX+RX DMA read the JEDEC ID: 0x{r[4]:08X} (expect 0x1540EFFF)", r[4] == 0x1540EFFF),
            ("SPI1 RX channel 2: one TC interrupt (IRQ 10)", r[5] == 1),
            (f"DMA registers modelled (unmodelled: {unmodelled})", not unmodelled),
            ("test ran to the end", r[15] == 0xC0FFEE),
            clean(b)]


@test("tim")
def t_tim(tmp):
    """Timer compare channels: CCxIF/CCxIE, OC2PE preload (new value from the next update), CCR3
    without preload (same period), CCxG; TIM6/14/16/17 interrupts on their IRQ numbers."""
    fw = build("st_tim.c", tmp)
    b = Board(fw, BoardConfig(allow_tx=False, allow_spi_write=False), Inputs())
    b.run(20)
    r = results(b, fw)

    def near(v, e):
        return abs(v - e) <= 1
    return [(f"TIM3 CC1 interrupts in 10 ms: {r[0]} (expect 10)", near(r[0], 10)),
            (f"TIM3 CC2 interrupts: {r[1]} (expect 10)", near(r[1], 10)),
            (f"CNT in the CC1 handler: {r[2]} (expect 250..252)", 250 <= r[2] <= 252),
            (f"CC2 before the preloaded value applies: CNT {r[3]} (expect 500..502)", 500 <= r[3] <= 502),
            (f"CC2 after the next update (OC2PE): CNT {r[4]} (expect 800..802)", 800 <= r[4] <= 802),
            (f"CCR3 without preload fires in the same period: {r[5]} at CNT {r[6]} (expect 1 at 300..302)",
             r[5] == 1 and 300 <= r[6] <= 302),
            (f"TIM17 (IRQ 22) update/CC1 interrupts: {r[8]}/{r[9]} (expect 100/100)",
             near(r[8], 100) and near(r[9], 100)),
            (f"TIM14 (IRQ 19) CC1 interrupts: {r[10]} (expect 50)", near(r[10], 50)),
            (f"TIM16 (IRQ 21) CC1 interrupts: {r[11]} (expect 50)", near(r[11], 50)),
            (f"TIM3 update interrupts: {r[12]} (expect 10)", near(r[12], 10)),
            ("EGR.CC1G sets CC1IF", r[13] == 2),
            ("TIM6 (IRQ 17) ended the test", r[15] == 0xC0FFEE),
            clean(b)]


@test("usart")
def t_usart(tmp):
    """USART FIFO mode (USART2): TX FIFO depth (TXFNF), TXFE and TC timing, RX FIFO full (RXFF),
    overrun on the ninth byte, one RXFNE interrupt per byte, sending from the TXFNF interrupt."""
    fw = build("st_usart.c", tmp)
    b = Board(fw, BoardConfig(allow_tx=True, allow_spi_write=False), Inputs())
    b.at(2.0, lambda: b.inject("U2", b.ms, b"0123456789"))
    b.at(5.0, lambda: b.inject("U2", b.ms, b"abc"))
    b.run(20)
    r = results(b, fw)
    sent = bytes(b.tx_raw.get("U2", b""))
    return [(f"bytes accepted before TXFNF clears: {r[0]} (expect 9: shift register + 8)", r[0] == 9),
            ("TXFE and TC clear right after the burst", r[1] == 0),
            (f"TXFE after {r[2]} cycles (8 frames of 5560: expect 44480 +- 300)", abs(r[2] - 44480) <= 300),
            (f"TC after {r[3]} cycles (9 frames: expect 50040 +- 300)", abs(r[3] - 50040) <= 300),
            ("RXFF set when the overrun shows", r[4] == 1),
            (f"the FIFO kept the first eight bytes: 0x{r[6]:08X} 0x{r[7]:08X}",
             (r[6], r[7]) == (0x33323130, 0x37363534)),
            (f"bytes read until RXFNE cleared: {r[8]} (expect 8)", r[8] == 8),
            (f"RXFNE interrupts for three bytes: {r[9]} (expect 3, one per byte)", r[9] == 3),
            (f"bytes read in the interrupt: 0x{r[10]:06X} (expect 'abc')", r[10] == 0x636261),
            (f"TXFNF interrupt sent {r[11]} bytes; U2 carried {sent!r}",
             r[11] == 20 and sent == b"012345678ABCDEFGHIJKLMNOPQRST"),
            ("test ran to the end", r[15] == 0xC0FFEE),
            clean(b)]


@test("reboot")
def t_reboot(tmp):
    """Reboot-continue: SYSRESETREQ and an NRST pulse keep RAM (except the boot_scribble range) and
    set the RCC_CSR reset flags, RMVF clears them; the same through scenario keys and events; a
    power cut in the middle of a page program tears it and loses RAM; by default SYSRESETREQ still
    ends the run."""
    fw = build("st_reboot.c", tmp)
    cfg = BoardConfig(allow_tx=False, allow_spi_write=True, on_reset="continue",
                      boot_scribble=(0x20001800, 0x20001810, 0xA5))
    b = Board(fw, cfg, Inputs())
    b.at(2.0, lambda: b.reset_request("pin"))
    b.run(5)
    k = struct.unpack("<8I", bytes(b.uc.mem_read(0x20001000, 32)))
    r = results(b, fw)
    causes = [x["cause"] for x in b.resets]
    checks = [(f"resets {causes} (expect software, pin)", causes == ["software", "pin"]),
              (f"RCC_CSR at power-on 0x{k[1]:08X} (expect PWRRSTF|PINRSTF 0x0C000000)", k[1] == 0x0C000000),
              (f"RCC_CSR after RMVF 0x{k[2]:08X} (expect 0)", k[2] == 0),
              (f"RCC_CSR after SYSRESETREQ 0x{k[3]:08X} (expect SFTRSTF|PINRSTF 0x14000000)", k[3] == 0x14000000),
              ("RAM kept over the software reset", k[4] == 1),
              (f"boot_scribble range after the reset: 0x{k[5]:08X} (expect 0xA5A5A5A5)", k[5] == 0xA5A5A5A5),
              (f"RCC_CSR after the NRST pulse 0x{k[6]:08X} (expect PINRSTF 0x04000000)", k[6] == 0x04000000),
              ("RAM kept over the NRST pulse", k[7] == 1),
              ("third boot ran to the end", r[15] == 0xC0FFEE),
              clean(b)]

    sc_path = Path(tmp) / "reboot_scenario.json"
    sc_path.write_text(json.dumps({"end_ms": 5, "spi_image": "erased", "generators": False, "on_reset": "continue",
                                   "boot_scribble": ["0x20001800", "0x20001810", "0xA5"],
                                   "timeline": [{"at": 2, "reboot": True}]}))
    res = S.run(S.load_scenario(sc_path), str(Path(tmp) / "st_reboot.elf"))
    got = [x["cause"] for x in res["resets"]]
    checks.append((f"scenario keys on_reset/boot_scribble and the reboot event: resets {got}",
                   got == ["software", "pin"] and not res["violations"]))

    b = Board(fw, BoardConfig(allow_tx=False, allow_spi_write=True), Inputs(pb1=1))
    b.power_cut(tear="program")
    b.run(5)
    r = results(b, fw)
    page = bytes(b.nor.mem[0x1000:0x1100])
    causes = [x["cause"] for x in b.resets]
    last = b.nor.ops[-1] if b.nor.ops else {}
    checks += [(f"power-cut run: resets {causes} (expect power)", causes == ["power"]),
               ("torn page program: the first 128 bytes programmed, the rest still erased",
                page == b"\x00" * 128 + b"\xff" * 128),
               (f"torn operation recorded: {last}", last.get("torn_after") == 128),
               (f"RCC_CSR after the power cut 0x{r[0]:08X} (expect 0x0C000000)", r[0] == 0x0C000000),
               ("RAM lost in the power cut", r[1] == 0),
               ("power-cut run ran to the end", r[15] == 0xC0FFEE),
               clean(b)]

    b = Board(fw, BoardConfig(allow_tx=False, allow_spi_write=False), Inputs())
    stop = b.run(5)
    checks.append((f"default on_reset 'stop': the run ends at SYSRESETREQ (stop reason {stop!r})",
                   stop == "reset" and not b.resets))
    return checks


@test("iwdg")
def t_iwdg(tmp):
    """Watchdog: write protection, LSI forced on, PVU/RVU after writes, reloads keep the MCU alive,
    expiry 10 ms after the last reload resets it (IWDGRSTF); expected, or a violation that ends
    the run."""
    fw = build("st_iwdg.c", tmp)
    cfg = BoardConfig(allow_tx=False, allow_spi_write=False, on_reset="continue", expect_watchdog=True)
    b = Board(fw, cfg, Inputs())
    b.run(100)
    k = struct.unpack("<5I", bytes(b.uc.mem_read(0x20001000, 20)))
    r = results(b, fw)
    resets = [(x["cause"], x["t_ms"]) for x in b.resets]
    checks = [(f"PR written before the unlock is ignored (reads {k[1]})", k[1] == 0),
              ("RCC_CSR.LSIRDY while the watchdog runs", k[2] == 1),
              (f"SR busy after writing PR/RLR ({k[0]} polls)", k[0] >= 1),
              (f"fed {k[3]} times (expect 5)", k[3] == 5),
              (f"one watchdog reset about 10 ms after the last reload: {resets}",
               len(resets) == 1 and resets[0][0] == "watchdog" and 34.5 <= resets[0][1] <= 36.0),
              (f"RCC_CSR after it 0x{k[4]:08X} (expect IWDGRSTF|PINRSTF 0x24000000)", k[4] == 0x24000000),
              ("the boot after the reset ran to the end", r[15] == 0xC0FFEE),
              clean(b)]
    b = Board(fw, BoardConfig(allow_tx=False, allow_spi_write=False), Inputs())
    stop = b.run(100)
    checks.append((f"not expected: stop reason {stop!r} and a violation",
                   stop == "watchdog" and any("watchdog" in v for v in b.violations)))
    return checks


@test("crcadc")
def t_crcadc(tmp):
    """CRC unit (MPEG-2 defaults, zlib settings, 16-bit polynomial, INIT write, IDR) and the ADC's
    internal channels (instant calibration, ADRDY/CCRDY/EOC, fixed VREFINT/TS readings)."""
    fw = build("st_crcadc.c", tmp)
    b = Board(fw, BoardConfig(allow_tx=False, allow_spi_write=False), Inputs())
    b.run(5)
    r = results(b, fw)
    z = zlib.crc32(b"123456789")
    return [(f"CRC-32/MPEG-2 of '123456789': 0x{r[0]:08X} (expect 0x0376E6E7)", r[0] == 0x0376E6E7),
            (f"zlib CRC-32 (REV_IN word, REV_OUT, final XOR): 0x{r[1]:08X} (expect 0x{z:08X})", r[1] == z),
            (f"CRC-16/CCITT-FALSE (POLYSIZE 16, POL 0x1021): 0x{r[2]:04X} (expect 0x29B1)", r[2] == 0x29B1),
            (f"writing INIT loads DR: 0x{r[3]:08X}", r[3] == 0x12345678),
            ("IDR holds a value", r[4] == 0xA5),
            ("ADC calibration completes at once (EOCAL)", r[5] == 1),
            ("ADEN sets ADRDY", r[6] == 1),
            (f"VREFINT reading {r[7]} (expect 1505), temperature sensor {r[8]} (expect 943)",
             (r[7], r[8]) == (1505, 943)),
            (f"VDDA from VREFINT_CAL: {r[9]} mV (expect 3300 +- 5)", abs(r[9] - 3300) <= 5),
            (f"TS_CAL1 in system memory: {r[10]} (expect 1037)", r[10] == 1037),
            ("reading DR clears EOC", r[11] == 0),
            ("test ran to the end", r[15] == 0xC0FFEE),
            clean(b)]


@test("lcd")
def t_lcd(tmp):
    """ST7789: the classic 8-bit/MADCTL-0 drawing and the nx 16-bit/MADCTL-0xA0 drawing of the same
    corner pattern are identical and upright; VSCRDEF/VSCSAD scroll the rows until NORON; SLPIN shows
    black; MADCTL.RGB swaps red and blue; screenshots follow the backlight duty."""
    fw = build("st_lcd.c", tmp)
    b = Board(fw, BoardConfig(allow_tx=False, allow_spi_write=False), Inputs())
    shots = {}

    def shot(addr, size):
        bl = b.backlight()
        shots[b.fx.reg(A.UC_ARM_REG_R0)] = (b.lcd.image(backlight=bl, scale=1), bl)
    b.fx.hook_code(shot, fw.syms["st_shot"], fw.syms["st_shot"])
    b.run(120)
    r = results(b, fw)
    blank = Image.new("RGB", (320, 240))

    def px(n, x, y):
        return (shots.get(n) or (blank, 0))[0].getpixel((x, y))

    def same(n1, n2):
        return n1 in shots and n2 in shots and ImageChops.difference(shots[n1][0], shots[n2][0]).getbbox() is None

    def red(p):
        return p[0] > 200 and p[1] < 40 and p[2] < 40

    def green(p):
        return p[1] > 200 and p[0] < 40 and p[2] < 40

    def blue(p):
        return p[2] > 200 and p[0] < 40 and p[1] < 40

    full_red = px(2, 3, 3)[0]
    duty = shots.get(7, (blank, 0))[1]
    dim_half = px(7, 3, 3)[0]
    dim_one = b.lcd.image(backlight=duty, scale=1, full=1.0).getpixel((3, 3))[0]
    return [(f"seven screenshots taken: {sorted(shots)}", sorted(shots) == [1, 2, 3, 4, 5, 6, 7]),
            (f"classic drawing upright: red top left {px(1, 3, 3)}, green top right {px(1, 316, 3)}, "
             f"blue bottom left {px(1, 3, 236)}", red(px(1, 3, 3)) and green(px(1, 316, 3)) and blue(px(1, 3, 236))),
            ("MADCTL 0xA0 with 16-bit frames draws the identical upright landscape", same(1, 2)),
            (f"VSCSAD 8: red moved 8 columns right {px(3, 10, 3)}, green wrapped to the left {px(3, 3, 3)}",
             red(px(3, 10, 3)) and green(px(3, 3, 3)) and px(3, 316, 3) == (0, 0, 0)),
            ("NORON ends the scrolling", same(2, 4)),
            ("SLPIN shows black", 5 in shots and shots[5][0].getbbox() is None),
            (f"MADCTL.RGB: red shows blue {px(6, 3, 3)}, blue shows red {px(6, 3, 236)}",
             blue(px(6, 3, 3)) and red(px(6, 3, 236))),
            (f"backlight duty at shot 7: {duty:.3f} (expect 0.25)", 0.24 <= duty <= 0.26),
            (f"25 % backlight, full at 50 %: red {dim_half} (expect {full_red * 224 // 384})",
             dim_half == full_red * 224 // 384),
            (f"25 % backlight, full at 100 %: red {dim_one} (expect {full_red * 144 // 384})",
             dim_one == full_red * 144 // 384),
            ("test ran to the end", r[15] == 0xC0FFEE),
            clean(b)]


@test("cli")
def t_cli(tmp):
    """tools/emu.py --shot-hook (a PNG per call of a firmware function, named after the string in
    r1) and --frames (a PNG every STEP ms), on the lcd test image."""
    from .emu_cli import main as emu_main
    build("st_lcd.c", tmp)
    shots, frames = Path(tmp) / "shots", Path(tmp) / "frames"
    code = emu_main([str(Path(tmp) / "st_lcd.elf"), "--until", "60", "--shot-hook", f"st_shot:{shots}",
                     "--frames", f"0:40:10:{frames}"])
    names = sorted(p.name for p in shots.glob("*.png"))
    frame_names = sorted(p.name for p in frames.glob("*.png"))
    same = len(names) >= 2 and ImageChops.difference(Image.open(shots / names[0]).convert("RGB"),
                                                     Image.open(shots / names[1]).convert("RGB")).getbbox() is None
    return [(f"emu.py exit code {code}", code == 0),
            (f"--shot-hook files: {names}",
             names == ["001_classic.png", "002_nx.png", "003_scroll8.png", "004_noron.png", "005_sleep.png",
                       "006_bgr.png", "007_bl25.png"]),
            ("the classic and the nx screenshot are identical", same),
            (f"--frames files: {frame_names}",
             frame_names == ["000000.000ms.png", "000010.000ms.png", "000020.000ms.png", "000030.000ms.png",
                             "000040.000ms.png"])]


def main(argv=None):
    names = list(sys.argv[1:] if argv is None else argv) or list(TESTS)
    unknown = [n for n in names if n not in TESTS]
    if unknown:
        print(f"unknown test {', '.join(unknown)} (tests: {', '.join(TESTS)})")
        return 2
    total = failed = 0
    with tempfile.TemporaryDirectory() as tmp:
        for name in names:
            print(f"== {name}")
            for text, ok in TESTS[name](Path(tmp)):
                print(f"{'ok  ' if ok else 'FAIL'} {text}")
                total += 1
                failed += not ok
    print(f"selftest: {total} checks, {failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
