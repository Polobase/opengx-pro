"""The emulated board: Cortex-M0+ core (Unicorn), memory map, NVIC/SCB/SysTick, time base.

Time: one CPU cycle per 16-bit instruction unit of every executed basic block (Unicorn block
hook), plus modelled stalls (SPI transfers, UART waits, exception entry/exit) and fast-forwarded
busy loops: `delay_loop` costs exactly 4 cycles per iteration (measured on the real unit), and
a WFI sleeps until the next event. HCLK = 64 MHz throughout (the few microseconds before the
firmware switches to the PLL are not modelled).

Interrupts: generic NVIC (enable/pending/priority, level-sensitive peripheral lines that
re-pend while asserted), SysTick and PendSV, PRIMASK. Exceptions are taken at basic-block
boundaries: the 8-word frame is stacked like the hardware does, the handler runs with
LR = a trampoline address, and returning to the trampoline unstacks the frame. Handlers are
plain AAPCS functions in both firmwares, so they never look at EXC_RETURN values.
"""

import ctypes
import struct
from collections import deque
from dataclasses import dataclass, field

from unicorn import (UC_ARCH_ARM, UC_HOOK_BLOCK, UC_HOOK_CODE, UC_HOOK_MEM_WRITE, UC_MODE_MCLASS,
                     UC_MODE_THUMB, Uc, UcError)
from unicorn import arm_const as A

from . import periph as P
from .fastuc import Fast, uclib
from .devices import GeneratorBoard, St7789, W25Q16

HCLK = 64_000_000
MS = HCLK // 1000
NEVER = P.NEVER
FLASH_BASE, FLASH_SIZE = 0x08000000, 0x20000
RAM_BASE, RAM_SIZE = 0x20000000, 0x9000
SYSMEM_BASE, SYSMEM_SIZE = 0x1FFF0000, 0x8000
TRAMPOLINE = SYSMEM_BASE            # stands in for EXC_RETURN
UID_ADDR = 0x1FFF7590
DEFAULT_UID = bytes.fromhex("3a0027001350523357383320")

KEY_PINS = {
    "LOOP": ("D", 0), "START": ("D", 1), "STOP": ("B", 5), "UP": ("B", 4), "DOWN": ("D", 3),
    "G1": ("B", 3), "G2": ("D", 2), "POWER": ("B", 11),
}
FRAME_REGS = [A.UC_ARM_REG_R0, A.UC_ARM_REG_R1, A.UC_ARM_REG_R2, A.UC_ARM_REG_R3, A.UC_ARM_REG_R12,
              A.UC_ARM_REG_LR, A.UC_ARM_REG_XPSR, A.UC_ARM_REG_SP]
ENTRY_REGS = [A.UC_ARM_REG_SP, A.UC_ARM_REG_LR, A.UC_ARM_REG_XPSR]
UNSTACK_REGS = [A.UC_ARM_REG_R0, A.UC_ARM_REG_R1, A.UC_ARM_REG_R2, A.UC_ARM_REG_R3, A.UC_ARM_REG_R12,
                A.UC_ARM_REG_LR, A.UC_ARM_REG_SP, A.UC_ARM_REG_XPSR]
IRQ_NAMES = {9: "DMA1_CH1", 10: "DMA1_CH2_3", 11: "DMA1_CH4_7", 13: "TIM1", 16: "TIM3", 17: "TIM6", 18: "TIM7",
             19: "TIM14", 20: "TIM15", 21: "TIM16", 22: "TIM17", 27: "USART1", 28: "USART2", 29: "USART3_4"}

# RCC_CSR flags per reset cause; the internal reset pulse drives NRST, so every reset also sets
# PINRSTF (RM0454 "Reset"; verify with the sizeprobe spike, docs/nx/drop0-checklist.md)
RESET_FLAGS = {"software": 1 << 28, "watchdog": 1 << 29, "pin": 0, "power": 1 << 27}
PINRSTF = 1 << 26
RESET_TEXT = {"software": "system reset requested (AIRCR.SYSRESETREQ)", "watchdog": "watchdog reset (IWDG expired)",
              "pin": "reset pulse on NRST", "power": "power cut"}


@dataclass
class BoardConfig:
    reference: bool = False         # original firmware: internal-flash programming is emulated
    variant: int = 0xFF00           # u16 at 0x08003C00 (user's unit: 0xFF00 = panel variant A)
    flash_3d00: int = 0             # u64 at 0x08003D00 (user's unit: 0, no factory wipe)
    spi_image: bytes = None         # 2 MB SPI NOR content (None: erased)
    allow_tx: bool = True           # UART transmission is expected (else: violation)
    allow_spi_write: bool = True    # SPI NOR program/erase is expected (else: violation)
    generators: dict = field(default_factory=dict)   # {"U1": ack_delay_ms, ...}
    generator_replies: dict = field(default_factory=dict)   # exact TX line -> reply (all ports)
    stop_on_power_off: bool = True
    on_reset: str = "stop"          # "continue": a software/watchdog reset reboots the board (else the run ends)
    expect_watchdog: bool = False   # an IWDG reset is expected (else it is a violation)
    boot_scribble: tuple = None     # (start, end, byte): RAM the bootloader overwrites on every reboot
    uid: bytes = DEFAULT_UID
    adc: dict = field(default_factory=dict)   # ADC channel -> raw reading (overrides periph.Adc.READINGS)
    bl_full: float = 0.5            # backlight duty shown as full brightness in screenshots (classic: 8/16)


class Inputs:
    """External inputs over time: keys (active low against the pull-ups) and the PB1 level."""

    def __init__(self, keys=(), pb1=0):
        self.keys = list(keys)                  # (start_ms, end_ms, KEY)
        self.pb1 = [(0.0, int(pb1))]            # sorted (t_ms, level) changes

    def add_key(self, start, end, name):
        for k in name.split("+"):
            if k not in KEY_PINS:
                raise ValueError(f"unknown key {k!r} (keys: {', '.join(KEY_PINS)})")
            self.keys.append((float(start), float(end), k))

    def set_pb1(self, t, level):
        self.pb1.append((float(t), int(level)))
        self.pb1.sort(key=lambda x: x[0])

    def pb1_level(self, t):
        level = 0
        for when, lv in self.pb1:
            if when <= t:
                level = lv
        return level

    def pressed(self, t):
        return sorted({k for s, e, k in self.keys if s <= t < e})

    def driven(self, port, t):
        """(mask, level) of the pins of `port` driven from outside at time t (ms)."""
        mask = level = 0
        for s, e, k in self.keys:
            p, pin = KEY_PINS[k]
            if p == port and s <= t < e:
                mask |= 1 << pin
        if port == "B":
            mask |= 2
            level |= self.pb1_level(t) << 1
        return mask, level


class _Timeline:
    """Callbacks at given times (scenario actions, checkpoints, end of run)."""

    def __init__(self):
        self.items = []
        self.due = NEVER

    def add(self, t_cycles, fn):
        self.items.append((int(t_cycles), len(self.items), fn))
        self.items.sort(key=lambda x: (x[0], x[1]))
        self.due = self.items[0][0]

    def fire(self):
        t, _, fn = self.items.pop(0)
        self.due = self.items[0][0] if self.items else NEVER
        fn()


class Core:
    """System control space: SysTick, NVIC, SCB."""

    def __init__(self, b):
        self.b = b
        self.reset()

    def reset(self):
        self.iser = self.ispr = 0
        self.ipr = bytearray(32)
        self.vtor = 0
        self.shpr2 = self.shpr3 = 0
        self.scr = 0
        self.pend_systick = self.pend_pendsv = False
        self.csr = self.rvr = 0
        self.countflag = 0
        self.t0 = 0
        self.due = NEVER
        self.other = {}

    # SysTick
    def _div(self):
        return 1 if self.csr & 4 else 8

    def _period(self):
        return (self.rvr + 1) * self._div()

    def _restart(self):
        self.t0 = self.b.cycles
        self.due = self.t0 + self._period() if self.csr & 1 and self.rvr else NEVER
        self.b.reschedule()

    def fire(self):
        self.countflag = 1
        if self.csr & 2:
            self.pend_systick = True
            self.b.kick()
        self.t0 = self.due
        self.due += self._period()
        if self.due <= self.b.cycles:
            self.due = self.b.cycles + self._period()

    def prio(self, exc):
        if exc >= 16:
            return self.ipr[exc - 16] >> 6
        if exc == 15:
            return self.shpr3 >> 30
        if exc == 14:
            return (self.shpr3 >> 22) & 3
        if exc == 11:
            return self.shpr2 >> 30
        return -1

    def mmio_read(self, off, size):
        b = self.b
        a = off & ~3
        if a < 0x020:
            b.mmio_n += 1                               # SysTick: time-dependent
        if a == 0x010:
            v = self.csr | self.countflag << 16
            self.countflag = 0
        elif a == 0x014:
            v = self.rvr
        elif a == 0x018:
            if self.csr & 1 and self.rvr:
                v = self.rvr - ((b.cycles - self.t0) % self._period()) // self._div()
            else:
                v = self.other.get(a, 0)
        elif a == 0x01C:
            v = 0
        elif a in (0x100, 0x180):
            v = self.iser
        elif a in (0x200, 0x280):
            v = self.ispr
        elif 0x400 <= a < 0x420:
            v = int.from_bytes(self.ipr[a - 0x400:a - 0x400 + 4], "little")
        elif a == 0xD00:
            v = 0x410CC601
        elif a == 0xD04:
            v = (b.active[-1][0] if b.active else 0) | self.pend_systick << 26 | self.pend_pendsv << 28
            if self.ispr & self.iser or self.pend_systick or self.pend_pendsv:
                v |= 1 << 22
        elif a == 0xD08:
            v = self.vtor
        elif a == 0xD0C:
            v = 0xFA050000
        elif a == 0xD10:
            v = self.scr
        elif a == 0xD14:
            v = 0x208
        elif a == 0xD1C:
            v = self.shpr2
        elif a == 0xD20:
            v = self.shpr3
        else:
            v = self.other.get(a, 0)
            b.unmodelled.add(f"SCS+0x{a:03X}")
        return P._extract(v, off, size)

    def mmio_write(self, off, size, value):
        b = self.b
        b.mmio_n += 1
        a = off & ~3
        if 0x400 <= a < 0x420:
            for i in range(size):
                self.ipr[off - 0x400 + i] = (value >> (8 * i)) & 0xC0
            return
        if size != 4:
            value = P._merge(0, off, size, value)
        if a == 0x010:
            was = self.csr & 1
            self.csr = value & 7
            if self.csr & 1 and not was:
                self._restart()
            elif was and not self.csr & 1:
                self.due = NEVER
                b.reschedule()
        elif a == 0x014:
            self.rvr = value & 0xFFFFFF
        elif a == 0x018:
            self.countflag = 0
            self._restart()
        elif a == 0x100:
            self.iser |= value
            b.kick()
        elif a == 0x180:
            self.iser &= ~value
        elif a == 0x200:
            self.ispr |= value
            b.kick()
        elif a == 0x280:
            self.ispr &= ~value
        elif a == 0xD04:
            if value & 1 << 28:
                self.pend_pendsv = True
            if value & 1 << 27:
                self.pend_pendsv = False
            if value & 1 << 26:
                self.pend_systick = True
            if value & 1 << 25:
                self.pend_systick = False
            b.kick()
        elif a == 0xD08:
            self.vtor = value & 0xFFFFFF80
        elif a == 0xD0C:
            if value >> 16 == 0x05FA and value & 4:
                b.reset_request("software")
        elif a == 0xD10:
            self.scr = value
        elif a == 0xD1C:
            self.shpr2 = value & 0xC0000000
        elif a == 0xD20:
            self.shpr3 = value & 0xC0C00000
        else:
            self.other[a] = value
            b.unmodelled.add(f"SCS+0x{a:03X}")


class Board:
    BL_WINDOW = 16 * MS     # whole periods of the classic 1.6 ms soft PWM and of 625 Hz / 1 kHz / 2 kHz PWM

    def __init__(self, fw, cfg=None, inputs=None):
        self.fw = fw
        self.cfg = cfg or BoardConfig(reference=fw.reference)
        self.inputs = inputs or Inputs()
        self.cycles = 0
        self.next_event = 0
        self.kicked = False
        self.masked = False
        self.mmio_n = 0
        self.sources = []
        self.stop_reason = None
        self.violations = []
        self.warnings = {}              # text -> [first_ms, count]
        self.events = []                # (ms, text)
        self.unmodelled = set()
        self.active = []                # [(exc, prio, frame_sp)]
        self.irq_counts = {}
        self.uart_lines = []            # dicts: port, dir, t_ms, t_end_ms, text
        self.tx_raw = {}
        self._line = {}
        self.pb2_edges = []
        self.pb2_wave = None
        self.bl_edges = deque(maxlen=512)
        self._q_n = -1                  # idle-loop detector state (see _idle_check)
        self._quiet = 0
        self._anchor = None
        self._anchor_q = 0
        self._seen = {}
        self._visits = 0
        self._backoff = 1
        self.idle_skips = 0
        self._ram_buf = ctypes.create_string_buffer(RAM_SIZE)
        self.flash_programmed = 0
        self.timeline = _Timeline()
        self.resets = []                # {"t_ms", "cause"} of every reset the run went through
        self._pending_reset = None      # cause of a reset that run() carries out outside the emulation

        uc = self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        uc.ctl_set_cpu_model(A.UC_CPU_ARM_CORTEX_M0)
        fx = self.fx = Fast(uc)
        self._frame = fx.batch(FRAME_REGS)
        self._unstack = fx.batch(UNSTACK_REGS)
        self._entry = fx.batch(ENTRY_REGS)
        self._map_memory()

        # peripherals
        self.core = Core(self)
        self.rcc = P.Rcc(self, 0x40021000)
        self.flash = P.FlashCtl(self, 0x40022000)
        self.gpio = {p: P.Gpio(self, 0x50000000 + 0x400 * i, p) for i, p in enumerate("ABCD") }
        self.gpio["F"] = P.Gpio(self, 0x50001400, "F")
        self.gpio["A"].watch, self.gpio["B"].watch, self.gpio["C"].watch = 0x0102, 0x1004, 0x00C0
        self.lcd = St7789(panel_inverted=self.cfg.variant in (0xFF00, 0xF000))
        self.nor = W25Q16(self, self.cfg.spi_image or b"")
        self.spi1 = P.Spi(self, 0x40013000, "SPI1", self._nor_xfer, (0x40, 12), (0x30, 12))
        self.spi2 = P.Spi(self, 0x40003800, "SPI2", self._lcd_xfer, (0x3C, 14), (0x2C, 14))
        self.dma = P.Dma(self, 0x40020000)
        self.dmamux = P.DmaMux(self, 0x40020800)
        self.iwdg = P.Iwdg(self, 0x40003000)
        self.crc = P.Crc(self, 0x40023000)
        self.adc = P.Adc(self, 0x40012400)
        self.tims = [P.Tim(self, base, name, irq, clk, rst, nch) for base, name, irq, clk, rst, nch in (
            (0x40000400, "TIM3", 16, (0x3C, 1), (0x2C, 1), 4), (0x40001000, "TIM6", 17, (0x3C, 4), (0x2C, 4), 0),
            (0x40001400, "TIM7", 18, (0x3C, 5), (0x2C, 5), 0), (0x40002000, "TIM14", 19, (0x40, 15), (0x30, 15), 1),
            (0x40014000, "TIM15", 20, (0x40, 16), (0x30, 16), 2), (0x40014400, "TIM16", 21, (0x40, 17), (0x30, 17), 1),
            (0x40014800, "TIM17", 22, (0x40, 18), (0x30, 18), 1))]
        self.usarts = {name: P.Usart(self, base, name, irq, clk, rst) for base, name, irq, clk, rst in (
            (0x40013800, "U1", 27, (0x40, 14), (0x30, 14)), (0x40004400, "U2", 28, (0x3C, 17), (0x2C, 17)),
            (0x40004800, "U3", 29, (0x3C, 18), (0x2C, 18)), (0x40004C00, "U4", 29, (0x3C, 19), (0x2C, 19)))}
        pwr = P.Plain(self, 0x40007000, "PWR")
        pwr.RESET_BIT, pwr.CLOCK = (0x2C, 28), None
        pwr.regs = {0x00: 0x208}
        syscfg = P.Plain(self, 0x40010000, "SYSCFG")
        dbg = P.Plain(self, 0x40015800, "DBG")
        pages = {}
        for p in [self.rcc, self.flash, self.spi1, self.spi2, self.dma, self.dmamux, self.iwdg, self.crc, self.adc,
                  pwr, syscfg, dbg,
                  *self.gpio.values(), *self.tims, *self.usarts.values()]:
            pages[p.base] = p
        self.periphs = list(pages.values())
        self._unmodelled_pages = []
        for base in list(range(0x40000000, 0x40018000, 0x400)) + list(range(0x40020000, 0x40026000, 0x400)) \
                + list(range(0x50000000, 0x50002000, 0x400)):
            p = pages.get(base)
            if p is None:
                p = P.Unmodelled(self, base, f"periph@0x{base:08X}")
                self._unmodelled_pages.append(p)
            fx.mmio_map(base, 0x400, p.mmio_read, p.mmio_write)
        fx.mmio_map(0xE000E000, 0x1000, self.core.mmio_read, self.core.mmio_write)
        self.update_clocks()

        self.irq_lines = ([(t.irq, t) for t in self.tims] + [(u.irq, u) for u in self.usarts.values()]
                          + self.dma.irq_lines())
        self._lines, self._lines_iser = [], None
        self._refresh_sources()
        self.generators = {}
        for port, delay in (self.cfg.generators or {}).items():
            self.generators[port] = GeneratorBoard(self, self.usarts[port], delay, self.cfg.generator_replies)

        # hooks
        fx.hook_block(self._on_block)
        fx.hook_code(self._on_return, TRAMPOLINE, TRAMPOLINE)
        uc.hook_add(UC_HOOK_MEM_WRITE, self._on_flash_write, begin=FLASH_BASE, end=FLASH_BASE + FLASH_SIZE - 1)
        if fw.delay_loop is not None:
            fx.hook_code(self._on_delay_loop, fw.delay_loop, fw.delay_loop)
        if fw.power_off is not None:
            fx.hook_code(self._on_power_off, fw.power_off, fw.power_off)
        for name, addr in fw.fault_handlers.items():
            fx.hook_code(lambda a, s, name=name: self._on_fault(name), addr, addr)
        for addr in fw.primask_enables:                  # right after cpsie / msr primask
            fx.hook_code(lambda a, s: self._due(a, 0), addr, addr)
        for addr, (kind, cost) in fw.hle.items():
            fx.hook_code(lambda a, s, fn=getattr(self, "_hle_" + kind), cost=cost: fn(cost), addr, addr)

        # input changes are events, so the idle-loop detector never skips over one
        for s, e, _ in self.inputs.keys:
            self.at(s, lambda: None)
            self.at(e, lambda: None)
        for t, _ in self.inputs.pb1:
            if t > 0:
                self.at(t, lambda: None)
        self._allregs = fx.batch([getattr(A, f"UC_ARM_REG_R{i}") for i in range(13)]
                                 + [A.UC_ARM_REG_SP, A.UC_ARM_REG_LR, A.UC_ARM_REG_XPSR])

        # reset state as left by the bootloader: vector table of the app, PRIMASK = 0
        sp, pc = struct.unpack("<2I", bytes(fw.image[:8]))
        self.core.vtor = fw.base
        uc.reg_write(A.UC_ARM_REG_SP, sp)
        uc.reg_write(A.UC_ARM_REG_PC, pc | 1)
        self.reschedule()

    # ------------------------------------------------------------------ memory
    def _map_memory(self):
        uc, cfg, fw = self.uc, self.cfg, self.fw
        uc.mem_map(FLASH_BASE, FLASH_SIZE)
        flash = bytearray(b"\xff" * FLASH_SIZE)
        flash[0x3C00:0x3C02] = struct.pack("<H", cfg.variant)
        flash[0x3D00:0x3D08] = struct.pack("<Q", cfg.flash_3d00)
        off = fw.base - FLASH_BASE
        flash[off:off + len(fw.image)] = fw.image
        uc.mem_write(FLASH_BASE, bytes(flash))
        uc.mem_map(RAM_BASE, RAM_SIZE)
        uc.mem_map(SYSMEM_BASE, SYSMEM_SIZE)
        sysmem = bytearray(b"\xff" * SYSMEM_SIZE)
        sysmem[0:2] = b"\xfe\xe7"                                       # trampoline: b .
        sysmem[UID_ADDR - SYSMEM_BASE:UID_ADDR - SYSMEM_BASE + 12] = cfg.uid[:12]
        sysmem[0x75E0:0x75E2] = struct.pack("<H", FLASH_SIZE >> 10)    # flash size in KB
        sysmem[0x75A8:0x75AC] = struct.pack("<HH", 1037, 1656)          # TS_CAL1 (30 °C), VREFINT_CAL (3.0 V)
        uc.mem_write(SYSMEM_BASE, bytes(sysmem))

    def update_clocks(self):
        regs = self.rcc.regs
        for p in self.periphs:
            if p.CLOCK is not None:
                off, bit = p.CLOCK
                p.clk = bool(regs.get(off, 0) >> bit & 1)
        if hasattr(self, "irq_lines"):
            self._refresh_sources()

    def _refresh_sources(self):
        # stopped timers without a clock cannot produce events; UART RX queues always can
        self.sources = [self.core, self.timeline, *[t for t in self.tims if t.clk or t.due < NEVER],
                        *self.usarts.values(), self.dma, self.iwdg]
        self.reschedule()

    def reset_peripheral(self, reg, bit):
        for p in self.periphs:
            if p.RESET_BIT == (reg, bit):
                p.reset()

    # ------------------------------------------------------------------ bookkeeping
    @property
    def ms(self):
        return self.cycles / MS

    def violation(self, text):
        self.violations.append(f"{self.ms:9.1f} ms  {text}")

    def warn(self, text):
        w = self.warnings.get(text)
        if w:
            w[1] += 1
        else:
            self.warnings[text] = [round(self.ms, 3), 1]

    def event(self, text):
        self.events.append((self.ms, text))

    def request_stop(self, reason):
        if self.stop_reason is None:
            self.stop_reason = reason
        self.uc.emu_stop()

    def at(self, t_ms, fn):
        """Call fn() when simulated time reaches t_ms."""
        self.timeline.add(max(int(t_ms * MS), self.cycles), fn)
        self.reschedule()

    def at_cycles(self, t, fn):
        """Call fn() when the cycle counter reaches t."""
        self.timeline.add(max(int(t), self.cycles), fn)
        self.reschedule()

    # ------------------------------------------------------------------ resets
    def reset_request(self, cause):
        """A reset: 'software' (AIRCR.SYSRESETREQ), 'watchdog' (IWDG expiry), 'pin' (NRST pulse,
        scenario "reboot") or 'power' (power cut, scenario "power_cut"). With on_reset "stop" a
        software or watchdog reset ends the run (stop reason "reset" / "watchdog"); otherwise the
        board reboots and runs on. A watchdog reset is a violation unless expect_watchdog."""
        self.event(RESET_TEXT[cause])
        if cause == "watchdog" and not self.cfg.expect_watchdog:
            self.violation("watchdog reset (IWDG expired) that the scenario does not expect")
        if cause in ("software", "watchdog") and self.cfg.on_reset != "continue":
            self.request_stop("reset" if cause == "software" else "watchdog")
            return
        if self._pending_reset is None:
            self._pending_reset = cause
        self.uc.emu_stop()

    def power_cut(self, tear=None):
        """Supply lost and back at once: RAM lost, SPI-flash content kept (an erase or program in
        progress is torn now), panel reset. tear = 'program' | 'erase' | 'any' instead arms the cut
        for the middle of the next such SPI-flash operation."""
        if tear:
            self.nor.arm_cut(tear)
            self.event(f"power cut armed for the next SPI-flash {tear} operation")
            return
        self.nor.power_cut()
        self.reset_request("power")

    def _reboot(self):
        """Carry out the pending reset (called by run() between two emulation slices)."""
        cause, self._pending_reset = self._pending_reset, None
        self.resets.append({"t_ms": round(self.ms, 3), "cause": cause})
        # while the MCU is in reset its outputs float: backlight off, PB2 released, flash deselected
        if self.gpio["B"].odr >> 12 & 1:
            self.bl_edges.append((self.cycles, 0))
        if self.pb2_wave is None and self.gpio["B"].odr >> 2 & 1:
            self.pb2_edges.append((round(self.ms, 3), "reset"))
        self.nor.deselect()
        self._cut_tx_lines()
        csr = self.rcc.regs.get(0x60, 0)
        for p in self.periphs + self._unmodelled_pages:
            p.reset()
        flags = RESET_FLAGS[cause] | PINRSTF
        self.rcc.regs[0x60] = flags if cause == "power" else (csr & 0xFF000000) | flags
        self.core.reset()
        self.update_clocks()
        if cause == "power":
            self.uc.mem_write(RAM_BASE, self._ram_garbage())
            self.lcd.power_on()
        elif self.cfg.boot_scribble:
            lo, hi, byte = self.cfg.boot_scribble
            self.uc.mem_write(lo, bytes([byte]) * (hi - lo))
        self.active.clear()
        self.masked = self.kicked = False
        self._q_n, self._quiet, self._anchor, self._anchor_q, self._seen = -1, 0, None, 0, {}
        self._lines_iser = None
        # start the application as the bootloader does: its vector table, PRIMASK = 0
        sp, pc = struct.unpack("<2I", bytes(self.fw.image[:8]))
        self.core.vtor = self.fw.base
        for reg, v in ((A.UC_ARM_REG_PRIMASK, 0), (A.UC_ARM_REG_CONTROL, 0), (A.UC_ARM_REG_LR, 0xFFFFFFFF),
                       (A.UC_ARM_REG_XPSR, 1 << 24), (A.UC_ARM_REG_SP, sp), (A.UC_ARM_REG_PC, pc | 1)):
            self.uc.reg_write(reg, v)
        self.reschedule()

    def _cut_tx_lines(self):
        """A reset interrupts the lines being transmitted: they end as partial lines."""
        for (port, direction), (start, buf) in list(self._line.items()):
            if direction == "tx":
                self.uart_lines.append({"port": port, "dir": direction, "t_ms": round(start / MS, 3),
                                        "t_end_ms": round(self.ms, 3), "text": buf.decode("latin-1"),
                                        "partial": True})
                del self._line[(port, direction)]

    @staticmethod
    def _ram_garbage():
        """RAM after a power cut: a fixed pseudo-random pattern (xorshift32), so firmware that
        trusts uninitialised RAM fails the same way in every run."""
        x, out = 0x2545F491, bytearray(RAM_SIZE)
        for i in range(0, RAM_SIZE, 4):
            x ^= (x << 13) & 0xFFFFFFFF
            x ^= x >> 17
            x ^= (x << 5) & 0xFFFFFFFF
            out[i:i + 4] = x.to_bytes(4, "little")
        return bytes(out)

    # ------------------------------------------------------------------ scheduler
    def kick(self):
        self.kicked = True
        self.next_event = self.cycles

    def reschedule(self):
        nxt = min(s.due for s in self.sources) if self.sources else NEVER
        if self.kicked or self.masked:
            nxt = min(nxt, self.cycles if self.kicked else self.cycles + 1000)
        self.next_event = nxt

    def service(self):
        for _ in range(10000):
            now = self.cycles
            fired = False
            for s in self.sources:
                if s.due <= now:
                    s.fire()
                    fired = True
            if not fired or self.stop_reason or self._pending_reset:
                break
        self.reschedule()

    def _select(self):
        core = self.core
        active = self.active
        act = 0
        for exc, _, _ in active:
            if exc >= 16:
                act |= 1 << (exc - 16)
        if self._lines_iser != core.iser:
            self._lines_iser = core.iser
            self._lines = [(irq, per) for irq, per in self.irq_lines if core.iser >> irq & 1]
        for irq, per in self._lines:
            if not act >> irq & 1 and per.irq_level():
                core.ispr |= 1 << irq
        best = None
        if core.pend_systick:
            best = (core.prio(15), 15)
        if core.pend_pendsv and (best is None or core.prio(14) < best[0]):
            best = (core.prio(14), 14)
        cand = core.ispr & core.iser
        while cand:
            low = cand & -cand
            cand ^= low
            irq = low.bit_length() - 1
            key = (core.ipr[irq] >> 6, 16 + irq)
            if best is None or key < best:
                best = key
        self.masked = False
        if best is None:
            return None
        if active and best[0] >= active[-1][1]:
            return None                                 # retried at exception return
        if self.fx.reg(A.UC_ARM_REG_PRIMASK) & 1:
            self.masked = True                          # retried on cpsie or every 1000 cycles
            return None
        return best

    def _read_ram(self):
        uclib.uc_mem_read(self.fx.h, RAM_BASE, self._ram_buf, RAM_SIZE)
        return self._ram_buf.raw

    def _due(self, ret_addr, undo):
        self.kicked = False
        self._seen = {}
        self.service()
        if self.stop_reason or self._pending_reset:
            return False
        sel = self._select()
        entered = False
        if sel is not None:
            self.cycles -= undo
            entered = self._enter(sel[1], sel[0], ret_addr)
        self.reschedule()
        return entered

    # ------------------------------------------------------------------ exceptions
    def _enter(self, exc, prio, ret_addr):
        uc, core, fx = self.uc, self.core, self.fx
        r0, r1, r2, r3, r12, lr, xpsr, sp = self._frame.read()
        fsp = (sp - 32) & ~7
        if fsp < RAM_BASE + 0x100:
            self.violation(f"stack overflow on exception entry (SP 0x{sp:08X})")
            self.request_stop("fault")
            return False
        handler = struct.unpack("<I", uc.mem_read(core.vtor + 4 * exc, 4))[0]
        if handler in (0, 0xFFFFFFFF) or not handler & 1:
            self.violation(f"exception {exc} has no valid vector (VTOR 0x{core.vtor:08X}: 0x{handler:08X})")
            self.request_stop("fault")
            return False
        st_xpsr = (xpsr & ~(1 << 9)) | ((1 << 9) if sp & 4 else 0)
        uc.mem_write(fsp, struct.pack("<8I", r0, r1, r2, r3, r12, lr, ret_addr & ~1, st_xpsr))
        self._entry.write([fsp, TRAMPOLINE | 1, (xpsr & 0xF0000000) | 1 << 24 | exc])
        fx.set_reg(A.UC_ARM_REG_PC, handler | 1)
        if exc >= 16:
            core.ispr &= ~(1 << (exc - 16))
        elif exc == 15:
            core.pend_systick = False
        elif exc == 14:
            core.pend_pendsv = False
        self.active.append((exc, prio, fsp))
        self.irq_counts[exc] = self.irq_counts.get(exc, 0) + 1
        self.cycles += 15                               # exception entry latency (M0+)
        return True

    def _on_return(self, addr, size):
        uc, fx = self.uc, self.fx
        if not self.active:
            self.violation("jump to the exception-return trampoline outside a handler")
            self.request_stop("fault")
            return
        exc, _, fsp = self.active.pop()
        sp = fx.reg(A.UC_ARM_REG_SP)
        if sp != fsp:
            self.violation(f"exception {exc} returned with SP 0x{sp:08X}, frame at 0x{fsp:08X}")
            sp = fsp
        r0, r1, r2, r3, r12, lr, pc, xpsr = struct.unpack("<8I", uc.mem_read(sp, 32))
        self._unstack.write([r0, r1, r2, r3, r12, lr, sp + 32 + (4 if xpsr & 1 << 9 else 0),
                             (xpsr & ~(1 << 9)) | 1 << 24])
        fx.set_reg(A.UC_ARM_REG_PC, pc | 1)
        self.cycles += 12
        self.kick()

    # ------------------------------------------------------------------ hooks
    def _on_block(self, addr, size):
        c = self.cycles + (size >> 1)
        self.cycles = c
        if c >= self.next_event:
            self._due(addr, size >> 1)
        elif self.mmio_n != self._q_n:
            self._q_n = self.mmio_n
            self._quiet = self._anchor_q = 0
            self._seen = {}
        else:
            self._quiet += 1
            if self._quiet > 8:
                self._idle_check(addr, size >> 1)

    def _idle_check(self, addr, blk):
        """Skip ahead over busy-wait loops. After 8 blocks without MMIO writes or time-dependent
        MMIO reads, a block becomes the anchor (kept across interrupts). Each time execution is
        back at the anchor, the registers and the whole RAM are compared with the states seen
        there before; a repeated state means the loop can only change through an interrupt or an
        input change, so time jumps to the next event. Loops that count (timeouts) or compute
        never repeat a state and keep running normally."""
        if addr != self._anchor:
            if self._anchor is None or self._quiet - self._anchor_q > 256:   # anchor not in this loop
                self._anchor, self._anchor_q, self._seen, self._visits, self._backoff = addr, self._quiet, {}, 0, 1
            return
        self._anchor_q = self._quiet
        self._visits += 1
        if self._visits < self._backoff:
            return
        self._visits = 0
        regs = tuple(self._allregs.read())
        ram = self._read_ram()
        if self._seen.get(regs) == ram:
            self.idle_skips += 1
            self.cycles = max(self.cycles, self.next_event + blk)   # the anchor block runs after
            self._quiet = self._anchor_q = 0
            self._due(addr, blk)
            return
        self._seen[regs] = ram
        if len(self._seen) > 8:                     # computing, not waiting: check less often
            self._seen = {}
            self._backoff = min(self._backoff * 2, 256)

    def _on_delay_loop(self, addr, size):
        """delay_loop(n): nop; loop: subs r0,#1; cmp r0,#0; bne loop -> 4 cycles/iteration."""
        fx = self.fx
        n = fx.reg(A.UC_ARM_REG_R0) or 0x100000000
        while True:
            if n <= 1 or self.cycles + 4 * n < self.next_event:
                self.cycles += 4 * n
                fx.set_reg(A.UC_ARM_REG_R0, 0)
                fx.set_reg(A.UC_ARM_REG_PC, fx.reg(A.UC_ARM_REG_LR) | 1)
                return
            # run up to the next event; at least one iteration must remain (R0 = 0 means 2^32)
            k = min(n - 1, max(1, -(-(self.next_event - self.cycles) // 4)))
            self.cycles += 4 * k
            n -= k
            fx.set_reg(A.UC_ARM_REG_R0, n)
            if self._due(addr, 0) or self.stop_reason or self._pending_reset:
                return                                  # handler runs, then delay_loop resumes

    def _on_power_off(self, addr, size):
        # our firmware: entry of power_off_signal(); logged like the detected square wave
        if self.pb2_wave is None:
            self.pb2_wave = round(self.ms, 3)
            self.pb2_edges.append((self.pb2_wave, "square wave"))
        self.event("power-off signal (PB2 square wave) started")
        if self.cfg.stop_on_power_off:
            self.request_stop("power_off")

    def _on_fault(self, name):
        self.violation(f"reached {name}")
        self.request_stop(name)

    def _on_flash_write(self, uc, access, addr, size, value, _):
        if not self.cfg.reference:
            self.violation(f"write to flash address 0x{addr:08X}")
        elif self.flash.programming:
            self.flash_programmed += 1                  # reference: emulated doubleword programming
        else:
            self.violation(f"flash write without FLASH_CR.PG at 0x{addr:08X}")

    # ------------------------------------------------------------------ high-level emulation
    def _hle_ret(self, cost):
        self.mmio_n += 1                                # it did I/O: not an idle loop
        self.cycles += cost
        self.fx.set_reg(A.UC_ARM_REG_PC, self.fx.reg(A.UC_ARM_REG_LR) | 1)

    def _hle_lcd_data16(self, cost):
        """lcd_write_data16(v): CS low, D/C high, v >> 8, v & 0xFF, CS high."""
        v = self.fx.reg(A.UC_ARM_REG_R0)
        ga, gc = self.gpio["A"], self.gpio["C"]
        ga.set_odr(ga.odr & ~0x100)
        gc.set_odr(gc.odr | 0x40)
        self.lcd.byte(1, v >> 8 & 0xFF)
        self.lcd.byte(1, v & 0xFF)
        ga.set_odr(ga.odr | 0x100)
        self._hle_ret(cost)

    def _hle_lcd_cmd(self, cost):
        """lcd_write_cmd(c): CS low, D/C low, c, CS high."""
        ga, gc = self.gpio["A"], self.gpio["C"]
        ga.set_odr(ga.odr & ~0x100)
        gc.set_odr(gc.odr & ~0x40)
        self.lcd.byte(0, self.fx.reg(A.UC_ARM_REG_R0) & 0xFF)
        ga.set_odr(ga.odr | 0x100)
        self._hle_ret(cost)

    def _hle_lcd_data8(self, cost):
        """lcd_write_data8(d): CS low, D/C high, d, CS high."""
        ga, gc = self.gpio["A"], self.gpio["C"]
        ga.set_odr(ga.odr & ~0x100)
        gc.set_odr(gc.odr | 0x40)
        self.lcd.byte(1, self.fx.reg(A.UC_ARM_REG_R0) & 0xFF)
        ga.set_odr(ga.odr | 0x100)
        self._hle_ret(cost)

    # ------------------------------------------------------------------ devices
    def _nor_xfer(self, byte):
        return self.nor.xfer(byte) if not self.gpio["A"].odr & 2 else 0xFF

    def _lcd_xfer(self, byte):
        ga = self.gpio["A"]
        if ga.odr & 0x100:
            self.warn("SPI2 byte sent while LCD CS (PA8) is high (ignored by the panel)")
        else:
            self.lcd.byte(self.gpio["C"].odr >> 6 & 1, byte)
        return 0

    def pins_changed(self, port, mask, odr):
        if port == "A":
            if mask & 2:
                if self.cycles < self.spi1.busy_until or self.dma.tx_busy(self.spi1):
                    self.warn("SPI flash CS (PA1) changed while an SPI1 byte was in flight")
                if odr & 2:
                    self.nor.deselect()
                else:
                    self.nor.select()
            if mask & 0x100 and (self.cycles < self.spi2.busy_until or self.dma.tx_busy(self.spi2)):
                self.warn("LCD CS (PA8) changed while an SPI2 byte was in flight")
        elif port == "C":
            if mask & 0x40 and (self.cycles < self.spi2.busy_until or self.dma.tx_busy(self.spi2)):
                self.warn("LCD D/C (PC6) changed while an SPI2 byte was in flight")
            if mask & 0x80 and not odr & 0x80:
                self.lcd.hw_reset()
        elif port == "B":
            if mask & 0x1000:
                self.bl_edges.append((self.cycles, odr >> 12 & 1))
            if mask & 4:
                self._pb2_edge(odr >> 2 & 1)

    def _pb2_edge(self, level):
        if self.pb2_wave is not None:
            return
        self.pb2_edges.append((round(self.ms, 3), level))
        e = self.pb2_edges
        if len(e) >= 10 and e[-1][0] - e[-10][0] < 1.0:
            self.pb2_wave = e[-10][0]
            del e[-10:]
            e.append((self.pb2_wave, "square wave"))
            self.event("PB2 square wave (power-off signal) detected")
            if self.cfg.stop_on_power_off:
                self.request_stop("power_off")

    def backlight(self):
        """PB12 duty over the last 16 ms, 0.0 .. 1.0."""
        now = self.cycles
        win = self.BL_WINDOW
        start = now - win
        high, t = 0, now
        edges = self.bl_edges
        for when, level in reversed(edges):        # level holds from `when` until t
            seg = max(when, start)
            if level:
                high += t - seg
            t = seg
            if when <= start:
                break
        else:
            before = 1 - edges[0][1] if edges else self.gpio["B"].odr >> 12 & 1
            if before and t > start:
                high += t - start
        return high / win

    def uart_tx(self, u, byte, start, end):
        port = u.name
        if not self.cfg.allow_tx:
            self.violation(f"{port} transmitted 0x{byte:02X}")
        self.tx_raw.setdefault(port, bytearray()).append(byte)
        self._uart_byte(port, "tx", byte, start, end)

    def uart_rx(self, u, byte):
        self._uart_byte(u.name, "rx", byte, self.cycles - u.frame(), self.cycles)

    def _uart_byte(self, port, direction, byte, start, end):
        key = (port, direction)
        cur = self._line.get(key)
        if cur is None:
            cur = self._line[key] = [start, bytearray()]
        cur[1].append(byte)
        if byte == 0x0A or len(cur[1]) >= 512:
            text = cur[1].decode("latin-1")
            self.uart_lines.append({"port": port, "dir": direction, "t_ms": round(cur[0] / MS, 3),
                                    "t_end_ms": round(end / MS, 3), "text": text})
            del self._line[key]
            if direction == "tx" and port in self.generators:   # a long line arrives in pieces
                self.generators[port].on_line(text.rstrip("\r\n"), end, complete=byte == 0x0A)

    def flush_lines(self):
        """Unterminated partial lines (at the end of a run) as entries with 'partial': True."""
        for (port, direction), (start, buf) in list(self._line.items()):
            self.uart_lines.append({"port": port, "dir": direction, "t_ms": round(start / MS, 3),
                                    "t_end_ms": round(self.ms, 3), "text": buf.decode("latin-1"),
                                    "partial": True})
        self._line.clear()
        self.uart_lines.sort(key=lambda x: x["t_ms"])

    def inject(self, port, t_ms, data):
        """Schedule bytes arriving on USARTn (port 'U1'..'U3'), first byte starting at t_ms."""
        self.usarts[port].inject(int(t_ms * MS), data)

    # ------------------------------------------------------------------ run
    def run(self, until_ms):
        uc = self.uc
        if self.stop_reason == "time limit":
            self.stop_reason = None
        self.at(until_ms, lambda: self.request_stop("time limit"))
        while self.stop_reason is None:
            if self._pending_reset:
                self._reboot()
            pc = uc.reg_read(A.UC_ARM_REG_PC)
            try:
                uc.emu_start(pc | 1, 0xFFFFFFFF)
                self.fx.check()
            except UcError as e:
                pc = uc.reg_read(A.UC_ARM_REG_PC)
                self.violation(f"CPU exception ({e}) at 0x{pc:08X}")
                self.stop_reason = "fault"
                break
            if self.stop_reason:
                break
            if self._pending_reset:
                continue                                # reboot at the top of the loop
            pc = uc.reg_read(A.UC_ARM_REG_PC)
            try:
                hw = struct.unpack("<H", uc.mem_read(pc - 2, 2))[0]
            except UcError:
                hw = 0
            if hw in (0xBF30, 0xBF20):                  # WFI/WFE: sleep until the next event
                if self.next_event >= NEVER:
                    self.violation("WFI with nothing left that could wake the CPU")
                    self.stop_reason = "fault"
                    break
                self.cycles = max(self.cycles, self.next_event)
                self._due(pc, 0)
            else:
                self.violation(f"emulation stopped unexpectedly at 0x{pc:08X}")
                self.stop_reason = "fault"
        return self.stop_reason
