"""STM32G070 peripheral models.

Each peripheral is a register file behind a Unicorn MMIO page (1 KB). Live registers (status
flags, data registers, counters) are computed from the board's cycle counter. Everything the
two firmwares touch is modelled; registers of other peripherals read back what was written and
are reported as "unmodelled" in the run summary.

Timing model: SPI frame = 8 or 16 bit times at the programmed prescaler (16-bit frames high byte
first); reading SPIx_SR waits for the frame in flight (the CPU stalls instead of spinning).
UART frame = 10 bit times from BRR (8N1).
Polling USARTx_ISR in a tight loop fast-forwards to the next flag change.
"""

from collections import deque

from unicorn import UcError

NEVER = 1 << 62


def _extract(word, off, size):
    return (word >> ((off & 3) * 8)) & ((1 << (size * 8)) - 1)


def _merge(old, off, size, value):
    sh = (off & 3) * 8
    mask = ((1 << (size * 8)) - 1) << sh
    return (old & ~mask) | ((value << sh) & mask)


def _bitrev(v, n):
    """The n low bits of v in reverse order."""
    return int(f"{v & ((1 << n) - 1):0{n}b}"[::-1], 2)


class Periph:
    """Plain register file with reset values. Subclasses override read/write for live registers."""

    NAME = "?"
    RESET = {}
    CLOCK = None        # (RCC enable register offset, bit); None = always clocked
    RESET_BIT = None    # (RCC reset register offset, bit)

    def __init__(self, board, base, name=None):
        self.b = board
        self.base = base
        self.name = name or self.NAME
        self.clk = self.CLOCK is None
        self._poll_n, self._poll_v = -1, None
        self.reset()

    def _poll(self, v, until=NEVER):
        """Bookkeeping of a status-register read that returned v. Read again with the same value and
        no other MMIO in between (a tight polling loop), time jumps to the next event or `until`,
        whichever comes first: the earliest moment the value can change."""
        b = self.b
        if self._poll_n == b.mmio_n and self._poll_v == v:
            nxt = min(b.next_event, until)
            if b.cycles < nxt < NEVER:
                b.cycles = nxt
        b.mmio_n += 1
        self._poll_n, self._poll_v = b.mmio_n, v

    def reset(self):
        self.regs = dict(self.RESET)

    # Unicorn MMIO callbacks. board.mmio_n counts writes and time-dependent reads: the idle-loop
    # detector only skips ahead over code whose MMIO reads cannot change before the next event.
    def mmio_read(self, off, size):
        if not self.clk:
            self.b.warn(f"{self.name} read while its RCC clock is disabled (reads 0)")
            return 0
        return self.read(off, size)

    def mmio_write(self, off, size, value):
        self.b.mmio_n += 1
        if not self.clk:
            self.b.warn(f"{self.name} written while its RCC clock is disabled (write ignored)")
            return
        self.write(off, size, value)

    def read(self, off, size):
        return _extract(self.regs.get(off & ~3, 0), off, size)

    def write(self, off, size, value):
        a = off & ~3
        if size != 4:
            value = _merge(self.regs.get(a, 0), off, size, value)
        self.regs[a] = value & 0xFFFFFFFF


class Unmodelled(Periph):
    """Any other peripheral page: read-back storage, accesses are listed in the run summary."""

    def read(self, off, size):
        self.b.unmodelled.add(f"{self.name}+0x{off & ~3:03X}")
        self.b.mmio_n += 1
        return super().read(off, size)

    def write(self, off, size, value):
        self.b.unmodelled.add(f"{self.name}+0x{off & ~3:03X}")
        super().write(off, size, value)


class Plain(Periph):
    """Peripherals whose reset values are enough (PWR, SYSCFG, DBG)."""


class Rcc(Periph):
    NAME = "RCC"
    RESET = {0x00: 0x00000500, 0x04: 0x40000000, 0x08: 0, 0x0C: 0x00001000, 0x38: 0x00000100,
             0x60: 0x0C000000}          # CSR after power-on: PWRRSTF | PINRSTF
    ENABLE_REGS = (0x34, 0x38, 0x3C, 0x40)
    RESET_REGS = (0x24, 0x28, 0x2C, 0x30)

    def read(self, off, size):
        a = off & ~3
        v = self.regs.get(a, 0)
        if a == 0x00:          # CR: HSIRDY/HSERDY/PLLRDY follow HSION/HSEON/PLLON
            v &= ~((1 << 10) | (1 << 17) | (1 << 25))
            v |= ((v >> 8) & 1) << 10 | ((v >> 16) & 1) << 17 | ((v >> 24) & 1) << 25
        elif a == 0x08:        # CFGR: SWS = SW
            v = (v & ~0x38) | ((v & 7) << 3)
        elif a == 0x60:        # CSR: LSIRDY follows LSION, or the watchdog, which forces the LSI on
            v = (v & ~2) | ((v & 1) | self.b.iwdg.running) << 1
        return _extract(v, off, size)

    def write(self, off, size, value):
        a = off & ~3
        old = self.regs.get(a, 0)
        if a == 0x60:          # CSR: the reset flags (bits 24-31) are read-only, RMVF (bit 23) clears them
            v = _merge(old, off, size, value)
            self.regs[0x60] = (0 if v >> 23 & 1 else old & 0xFF000000) | (v & 1)
            return
        super().write(off, size, value)
        if a in self.ENABLE_REGS:
            self.b.update_clocks()
        elif a in self.RESET_REGS:
            rising = self.regs[a] & ~old
            for bit in range(32):
                if rising >> bit & 1:
                    self.b.reset_peripheral(a, bit)


class Iwdg(Periph):
    """Independent watchdog on the LSI (32 kHz nominal): KR 0xCCCC starts it (and the LSI), 0x5555
    unlocks PR/RLR/WINR, 0xAAAA reloads the counter, any other key locks again. PVU/RVU/WVU stay set
    for 5 LSI periods after a write. Expiry resets the MCU (board.reset_request('watchdog')); a
    reset stops it (software watchdog option, as on the user's unit). WINR is stored and reloads
    the counter when written, but the window is not enforced."""

    NAME = "IWDG"
    RESET = {0x04: 0, 0x08: 0xFFF, 0x10: 0xFFF}
    LSI_CYCLES = 2000                   # HCLK cycles per LSI period (64 MHz / 32 kHz)

    def __init__(self, board, base):
        self.due = NEVER
        super().__init__(board, base)

    def reset(self):
        super().reset()
        self.running = False
        self.unlocked = False
        self.busy = {0x04: 0, 0x08: 0, 0x10: 0}     # register -> cycle its update completes
        self.due = NEVER
        if hasattr(self.b, "sources"):
            self.b.reschedule()

    def timeout(self):
        """Cycles from a reload to the reset: (RLR + 1) x 4 x 2^PR LSI periods."""
        pr = min(self.regs[0x04] & 7, 6)
        return ((self.regs[0x08] & 0xFFF) + 1) * (4 << pr) * self.LSI_CYCLES

    def _reload(self):
        self.due = self.b.cycles + self.timeout()
        self.b.reschedule()

    def sr(self):
        now = self.b.cycles
        return sum(bit for bit, reg in ((1, 0x04), (2, 0x08), (4, 0x10)) if now < self.busy[reg])

    def read(self, off, size):
        a = off & ~3
        if a == 0x0C:
            v = self.sr()
            self._poll(v, until=max(self.busy.values()))
            return _extract(v, off, size)
        if a == 0x00:
            return 0
        return super().read(off, size)

    def write(self, off, size, value):
        a = off & ~3
        b = self.b
        if a == 0x00:                                   # KR
            key = value & 0xFFFF
            self.unlocked = key == 0x5555
            if key == 0xCCCC and not self.running:
                self.running = True
                self._reload()
            elif key == 0xAAAA and self.running:
                self._reload()
            return
        if a in (0x04, 0x08, 0x10):                     # PR, RLR, WINR
            if not self.unlocked:
                b.warn(f"IWDG: register 0x{a:02X} written while write-protected (ignored)")
                return
            super().write(off, size, value)
            self.busy[a] = b.cycles + 5 * self.LSI_CYCLES
            if a == 0x10 and self.running:
                self._reload()
            return
        super().write(off, size, value)

    def fire(self):
        self.due = NEVER
        self.running = False
        self.b.reset_request("watchdog")


class Crc(Periph):
    """CRC calculation unit. A DR write of 8/16/32 bits feeds that many bits, most significant
    first, after CR.REV_IN reversal (by byte, half-word or word, within the written size); DR
    reads the CRC, bit-reversed with CR.REV_OUT. CR.RESET and writing INIT load INIT (RM0454:
    "CRC_DR is automatically initialized upon CRC_INIT register write access"; verify).
    POLYSIZE 32/16/8/7 bits with POL. Defaults: polynomial 0x04C11DB7, INIT 0xFFFFFFFF."""

    NAME = "CRC"
    CLOCK, RESET_BIT = (0x38, 12), (0x28, 12)
    RESET = {0x04: 0, 0x08: 0, 0x10: 0xFFFFFFFF, 0x14: 0x04C11DB7}

    def __init__(self, board, base):
        self._tables = {}
        super().__init__(board, base)

    def reset(self):
        super().reset()
        self.crc = 0xFFFFFFFF

    def _width(self):
        return (32, 16, 8, 7)[self.regs[0x08] >> 3 & 3]

    def _table(self, w, poly):
        t = self._tables.get((w, poly))
        if t is None:
            mask, top = (1 << w) - 1, 1 << (w - 1)
            t = []
            for byte in range(256):
                c = byte << (w - 8)
                for _ in range(8):
                    c = ((c << 1) ^ poly) & mask if c & top else (c << 1) & mask
                t.append(c)
            self._tables[(w, poly)] = t
        return t

    def _feed(self, value, nbits):
        rev = self.regs[0x08] >> 5 & 3
        if rev:
            unit = min(nbits, 8 << (rev - 1))
            value = sum(_bitrev(value >> k, unit) << k for k in range(0, nbits, unit))
        w = self._width()
        mask = (1 << w) - 1
        poly = self.regs[0x14] & mask
        crc = self.crc & mask
        if w >= 8:
            t = self._table(w, poly)
            for k in range(nbits - 8, -1, -8):
                crc = ((crc << 8) & mask) ^ t[((crc >> (w - 8)) ^ (value >> k)) & 0xFF]
        else:
            for k in range(nbits - 1, -1, -1):
                top = (crc >> (w - 1) ^ value >> k) & 1
                crc = (crc << 1) & mask
                if top:
                    crc ^= poly
        self.crc = crc

    def read(self, off, size):
        if off & ~3 == 0x00:
            v = _bitrev(self.crc, self._width()) if self.regs[0x08] >> 7 & 1 else self.crc
            return _extract(v, off, size)
        return super().read(off, size)

    def write(self, off, size, value):
        a = off & ~3
        if a == 0x00:
            self._feed(value & ((1 << (8 * size)) - 1), 8 * size)
            return
        super().write(off, size, value)
        if a == 0x08 and self.regs[0x08] & 1:          # RESET: load INIT; the bit reads back 0
            self.regs[0x08] &= ~1
            self.crc = self.regs[0x10] & ((1 << self._width()) - 1)
        elif a == 0x10:
            self.crc = self.regs[0x10] & ((1 << self._width()) - 1)


class Adc(Periph):
    """ADC1, enough for the internal channels: calibration finishes at once (ADCAL clears, EOCAL,
    CALFACT), ADEN sets ADRDY, ADDIS clears both, CHSELR/CFGR1 writes set CCRDY, ADSTART converts
    the lowest selected channel at once (EOSMP, EOC, EOS; single conversion) with fixed readings,
    reading DR clears EOC. The common register CCR (TSEN, VREFEN) sits at +0x308 of the page.
    Interrupts, sequences and the analog watchdogs are not modelled."""

    NAME = "ADC"
    CLOCK, RESET_BIT = (0x40, 20), (0x30, 20)
    # raw 12-bit readings at VDDA = 3.3 V and 30 °C, consistent with the calibration values the
    # board puts in system memory (TS_CAL1 = 1037, VREFINT_CAL = 1656, both taken at 3.0 V);
    # channels 12 = temperature sensor, 13 = VREFINT (RM0454 ADC channel table; verify)
    READINGS = {12: 943, 13: 1505}
    OTHER = 0x800

    def reading(self, ch):
        return self.b.cfg.adc.get(ch, self.READINGS.get(ch, self.OTHER))

    def read(self, off, size):
        if off & ~3 == 0x40:                            # DR: reading it clears EOC
            self.regs[0x00] = self.regs.get(0x00, 0) & ~4
        return super().read(off, size)

    def write(self, off, size, value):
        a = off & ~3
        if a == 0x00:                                   # ISR: write 1 to clear
            self.regs[0x00] = self.regs.get(0x00, 0) & ~value
            return
        super().write(off, size, value)
        isr = self.regs.get(0x00, 0)
        if a == 0x08:                                   # CR
            cr = self.regs[0x08]
            if cr >> 31 & 1:                            # ADCAL: done at once
                cr &= ~(1 << 31)
                isr |= 1 << 11
                self.regs[0xB4] = 0x40
            if cr & 2:                                  # ADDIS
                cr &= ~3
                isr &= ~1
            elif cr & 1:                                # ADEN
                isr |= 1
            if cr & 4:                                  # ADSTART: one conversion
                sel = self.regs.get(0x28, 0)
                ch = (sel & -sel).bit_length() - 1 if sel else 0
                self.regs[0x40] = self.reading(ch) & 0xFFF
                cr &= ~4
                isr |= 0xE                              # EOSMP, EOC, EOS
            self.regs[0x08] = cr
        elif a in (0x0C, 0x28):                         # CFGR1 / CHSELR: channel configuration ready
            isr |= 1 << 13
        self.regs[0x00] = isr


class FlashCtl(Periph):
    """FLASH interface registers. Our firmware must never write them (violation); the original
    (reference mode) may unlock and program doublewords, which is emulated."""

    NAME = "FLASH"
    OPTR_RDP1 = 0xDFFFE1BB       # factory user options, RDP level 1 (0xBB) as on the real unit
    RESET = {0x00: 0x00000600, 0x10: 0, 0x14: 0xC0000000, 0x20: OPTR_RDP1, 0x2C: 0x000000FF,
             0x30: 0x000000FF}
    KEYS = (0x45670123, 0xCDEF89AB)
    OPTKEYS = (0x08192A3B, 0x4C5D6E7F)

    def reset(self):
        super().reset()
        self.key_step = 0
        self.optkey_step = 0

    @property
    def programming(self):
        cr = self.regs[0x14]
        return bool(cr & 1) and not cr >> 31 & 1

    def read(self, off, size):
        return super().read(off, size)

    def write(self, off, size, value):
        a = off & ~3
        if a != 0x00 and not self.b.cfg.reference:
            self.b.violation(f"flash controller write 0x{self.base + a:08X} = 0x{value:08X}")
        if a == 0x00:                                   # ACR (latency, prefetch, cache)
            super().write(off, size, value)
        elif a == 0x08:                                 # KEYR
            if value == self.KEYS[self.key_step]:
                self.key_step += 1
                if self.key_step == 2:
                    self.regs[0x14] &= ~(1 << 31)
                    self.key_step = 0
            else:
                self.key_step = 0
        elif a == 0x0C:                                 # OPTKEYR
            if value == self.OPTKEYS[self.optkey_step] and not self.regs[0x14] >> 31 & 1:
                self.optkey_step += 1
                if self.optkey_step == 2:
                    self.regs[0x14] &= ~(1 << 30)
                    self.optkey_step = 0
            else:
                self.optkey_step = 0
        elif a == 0x10:                                 # SR: write 1 to clear
            self.regs[0x10] &= ~value
        elif a == 0x14:                                 # CR
            cr = self.regs[0x14]
            if cr >> 31 & 1:
                return                                  # locked: ignored by the hardware
            self.regs[0x14] = value & 0xFFFFFFFF
            if value & (1 << 16) and value & 0x6:
                self.b.warn("FLASH: page/mass erase requested (not emulated)")
            if value & (1 << 17) and not cr >> 30 & 1:
                self.b.warn("FLASH: option byte programming requested (not emulated)")
            if value & (1 << 27):
                self.b.event("FLASH OBL_LAUNCH: option byte reload resets the MCU")
                self.b.request_stop("reset")
        else:
            super().write(off, size, value)


class Gpio(Periph):
    RESET_A = {0x00: 0xEBFFFFFF, 0x08: 0x0C000000, 0x0C: 0x24000000}
    RESET_X = {0x00: 0xFFFFFFFF}

    def __init__(self, board, base, port):
        self.port = port
        self.watch = 0          # pins whose output changes are reported to the board
        self.CLOCK = (0x34, "ABCD.F".index(port))
        self.RESET_BIT = (0x24, "ABCD.F".index(port))
        super().__init__(board, base, f"GPIO{port}")

    def reset(self):
        self.regs = dict(self.RESET_A if self.port == "A" else self.RESET_X)
        self.odr = 0
        self._masks()

    def _masks(self):
        moder, pupdr = self.regs.get(0x00, 0), self.regs.get(0x0C, 0)
        out = inp = 0
        for pin in range(16):
            mode, pull = (moder >> 2 * pin) & 3, (pupdr >> 2 * pin) & 3
            if mode == 1:
                out |= 1 << pin
            elif mode != 3 and pull != 2:      # input/AF without pull-down idles high; analog reads 0
                inp |= 1 << pin
        self.out_mask, self.in_default = out, inp
        self.digital = sum(1 << pin for pin in range(16) if (moder >> 2 * pin) & 3 != 3)

    def idr(self):
        v = (self.in_default & ~self.out_mask) | (self.odr & self.out_mask)
        mask, level = self.b.inputs.driven(self.port, self.b.ms)
        mask &= ~self.out_mask & self.digital
        return (v & ~mask) | (level & mask)

    def read(self, off, size):
        a = off & ~3
        if a == 0x10:
            return _extract(self.idr(), off, size)
        if a == 0x14:
            return _extract(self.odr, off, size)
        if a in (0x18, 0x28):
            return 0
        return super().read(off, size)

    def write(self, off, size, value):
        a = off & ~3
        if a == 0x18:                                   # BSRR
            v = value << ((off & 3) * 8)
            self.set_odr((self.odr & ~(v >> 16)) | (v & 0xFFFF))
        elif a == 0x28:                                 # BRR
            self.set_odr(self.odr & ~(value << ((off & 3) * 8)) & 0xFFFF)
        elif a == 0x14:                                 # ODR
            self.set_odr(_merge(self.odr, off, size, value) & 0xFFFF)
        else:
            super().write(off, size, value)
            if a in (0x00, 0x0C):
                self._masks()

    def set_odr(self, new):
        changed = self.odr ^ new
        self.odr = new
        if changed & self.watch:
            self.b.pins_changed(self.port, changed & self.watch, new)


class Spi(Periph):
    RESET = {0x00: 0, 0x04: 0x0700, 0x10: 7}

    def __init__(self, board, base, name, device, clock, reset_bit):
        self.device = device    # callable(byte) -> byte received
        self.CLOCK, self.RESET_BIT = clock, reset_bit
        super().__init__(board, base, name)

    def reset(self):
        super().reset()
        self.rx = bytearray()
        self.busy_until = 0
        self.ovr = False

    def ds16(self):
        """Data size above 8 bits (the models know 8 and 16)."""
        return (self.regs[0x04] >> 8 & 0xF) > 7

    def frame_cycles(self):
        """Duration of one frame: 8 or 16 bit times at the programmed baud-rate prescaler."""
        return (16 if self.ds16() else 8) << (((self.regs[0] >> 3) & 7) + 1)

    def read(self, off, size):
        b = self.b
        if off == 0x08:                                 # SR
            n = len(self.rx)
            flags = ((n >= (1 if self.regs[0x04] >> 12 & 1 and not self.ds16() else 2))
                     | (0x40 if self.ovr else 0) | (min(n, 3) << 9))
            if b.dma.tx_busy(self):                     # a DMA stream keeps the shifter busy
                v = flags | 0x80 | 0x1800               # BSY, TX FIFO full, TXE = 0
                self._poll(v)
                return v
            b.mmio_n += 1
            if b.cycles < self.busy_until:              # the CPU waits for the frame in flight
                b.cycles = self.busy_until
            return 0x2 | flags
        b.mmio_n += 1
        if off == 0x0C:                                 # DR: pop 1 or 2 bytes from the RX FIFO
            rx = self.rx
            if size == 1:
                return rx.pop(0) if rx else 0
            lo = rx.pop(0) if rx else 0
            hi = rx.pop(0) if rx else 0
            return lo | hi << 8
        return super().read(off, size)

    def write(self, off, size, value):
        if off == 0x0C:
            if not self.regs[0] & 0x40:
                self.b.warn(f"{self.name}: DR written while SPE=0 (nothing sent)")
                return
            self.tx(value, size)
            return
        if off == 0x08:
            return
        super().write(off, size, value)
        if off & ~3 == 0 and not value & 0x40:
            self.rx.clear()
        if off & ~3 in (0x00, 0x04):                    # SPE, TXDMAEN, RXDMAEN: DMA requests
            self.b.dma.update()

    def tx(self, value, size):
        """What a `size`-byte write of `value` to DR (by the CPU or a DMA channel) puts on the wire.
        DS = 8: one frame per byte, a 16-bit write packs two frames, low byte first. DS = 16: one
        frame, high byte first (MSB first). The device answers byte by byte; a received 16-bit frame
        enters the RX FIFO low byte first, so a 16-bit DR read returns it."""
        b = self.b
        start = max(b.cycles, self.busy_until)
        dev = self.device
        if self.ds16():
            hi = dev(value >> 8 & 0xFF)
            lo = dev(value & 0xFF)
            self._received(hi << 8 | lo, (lo, hi))
            nbytes = 2
        else:
            nbytes = 1 if size == 1 else 2
            for i in range(nbytes):
                r = dev((value >> (8 * i)) & 0xFF)
                self._received(r, (r,))
        self.busy_until = start + (8 << (((self.regs[0] >> 3) & 7) + 1)) * nbytes

    def _received(self, frame, raw):
        """A received frame (value, its bytes in FIFO order): an SPI -> memory DMA channel takes it,
        else it goes to the 32-bit RX FIFO."""
        if self.b.dma.rx_take(self, frame):
            return
        rx = self.rx
        for r in raw:
            if len(rx) < 4:
                rx.append(r)
            else:
                self.ovr = True


class DmaChannel:
    """One DMA1 channel: its registers and the state of the transfer."""

    def __init__(self, n):
        self.n = n
        self.base = 0x08 + 0x14 * (n - 1)       # CCR; CNDTR +4, CPAR +8, CMAR +0xC
        self.clear()

    def clear(self):
        self.ccr = self.count = self.cpar = self.cmar = 0
        self.count0 = 0          # CNDTR when enabled: circular reload and half-transfer point
        self.mar = 0             # current memory address
        self.flags = 0           # TCIF bit 1, HTIF bit 2, TEIF bit 3 (per channel, as in DMA_ISR)
        self.spi = None          # SPI of the request line
        self.tx = False          # memory -> SPI (else SPI -> memory)
        self.active = False      # enabled, data left and the request line on
        self.next_push = NEVER   # memory -> SPI: when the SPI can take the next chunk
        self.pending = []        # [(cycle, flag bits)]: flags that rise once the frame reaches the SPI

    def irq(self):
        return bool(self.flags & self.ccr & 0xE)       # TCIE/HTIE/TEIE sit at the flag positions


class _DmaLine:
    """One DMA interrupt line (channels 2-3 share IRQ 10, channels 4-7 IRQ 11)."""

    def __init__(self, channels):
        self.channels = channels

    def irq_level(self):
        return any(c.irq() for c in self.channels)


class DmaMux(Periph):
    """DMAMUX1 request routing: CxCR bits 0-5 = request ID; DMAMUX channel n-1 feeds DMA1 channel n.
    Clocked and reset with DMA1. Synchronisation and the request generators are not modelled."""

    NAME = "DMAMUX1"
    CLOCK, RESET_BIT = (0x38, 0), (0x28, 0)

    def write(self, off, size, value):
        super().write(off, size, value)
        if off & ~3 < 0x1C:
            self.b.dma.update()


class Dma(Periph):
    """DMA1 (7 channels) for SPI transfers: memory -> SPIx_DR (SPI1_TX, SPI2_TX) paced by the SPI,
    SPIx_DR -> memory (SPI1_RX, SPI2_RX); 8/16/32-bit sizes, MINC, circular mode, TC/HT/TE flags and
    interrupts (IRQ 9: channel 1, 10: channels 2-3, 11: channels 4-7). Memory -> SPI runs in chunks
    of up to CHUNK frames ahead of the shifter and reads the source memory then; TC and HT rise when
    their frame enters the SPI FIFO, one frame before it is shifted out. Memory-to-memory and other
    requests are reported as warnings."""

    NAME = "DMA1"
    CLOCK, RESET_BIT = (0x38, 0), (0x28, 0)
    CHUNK = 32
    # DMAMUX request ID -> (board attribute, memory -> SPI). RM0454, table "DMAMUX: assignment of
    # multiplexer inputs to resources" (verify against the reference manual).
    REQUESTS = {16: ("spi1", False), 17: ("spi1", True), 18: ("spi2", False), 19: ("spi2", True)}

    def __init__(self, board, base):
        self.due = NEVER
        self.ch = [DmaChannel(n) for n in range(1, 8)]
        self.tx_map, self.rx_map = {}, {}
        super().__init__(board, base)

    def reset(self):
        super().reset()
        for c in self.ch:
            c.clear()
        self.tx_map, self.rx_map = {}, {}
        self.due = NEVER
        if hasattr(self.b, "sources"):
            self.b.reschedule()

    def irq_lines(self):
        return [(9, _DmaLine(self.ch[0:1])), (10, _DmaLine(self.ch[1:3])), (11, _DmaLine(self.ch[3:7]))]

    # --- registers
    def _chan(self, a):
        return self.ch[(a - 0x08) // 0x14] if 0x08 <= a < 0x08 + 7 * 0x14 else None

    def isr(self):
        v = 0
        for c in self.ch:
            f = c.flags & 0xE
            v |= (f | bool(f)) << (4 * (c.n - 1))      # GIFx = any flag of the channel
        return v

    def _value(self, a):
        if a == 0x00:
            return self.isr()
        c = self._chan(a)
        return 0 if c is None else (c.ccr, c.count, c.cpar, c.cmar, 0)[(a - c.base) >> 2]

    def read(self, off, size):
        a = off & ~3
        if a == 0x00:
            v = self.isr()
            self._poll(v)
            return _extract(v, off, size)
        c = self._chan(a)
        if c is not None and a - c.base == 0x04:
            self.b.mmio_n += 1                          # CNDTR counts down
        return _extract(self._value(a), off, size)

    def write(self, off, size, value):
        a = off & ~3
        if size != 4:
            value = _merge(self._value(a), off, size, value)
        if a == 0x04:                                   # IFCR: 1 clears; CGIFx clears the channel
            for c in self.ch:
                bits = value >> (4 * (c.n - 1)) & 0xF
                c.flags &= ~(0xE if bits & 1 else bits)
            return
        c = self._chan(a)
        if c is None:
            return                                      # ISR is read-only
        r = a - c.base
        if r == 0x00:
            was = c.ccr & 1
            c.ccr = value & 0x7FFF
            if c.ccr & 1 and not was:                   # enable: latch the programmed transfer
                c.count0, c.mar, c.pending = c.count, c.cmar, []
            elif was and not c.ccr & 1:                 # disable: frames already sent keep their flags
                for _, bits in c.pending:
                    c.flags |= bits
                c.pending = []
            self.update()
        elif c.ccr & 1:
            self.b.warn(f"DMA1 channel {c.n}: CNDTR/CPAR/CMAR written while enabled (ignored)")
        elif r == 0x04:
            c.count = value & 0xFFFF
        elif r == 0x08:
            c.cpar = value
        elif r == 0x0C:
            c.cmar = value

    # --- transfers
    def update(self):
        """Start or pause channels as their request lines change: channel enable, DMAMUX request
        ID, SPI enable (SPE) and TXDMAEN / RXDMAEN."""
        b = self.b
        for c in self.ch:
            want = False
            if c.ccr & 1 and c.count:
                req = b.dmamux.regs.get(4 * (c.n - 1), 0) & 0x3F
                if req in self.REQUESTS:
                    attr, tx = self.REQUESTS[req]
                    spi = getattr(b, attr)
                    if bool(c.ccr & 0x10) != tx or c.ccr & 0x4000:
                        b.warn(f"DMA1 channel {c.n}: direction or MEM2MEM does not fit request {req} (not modelled)")
                    elif c.cpar != spi.base + 0x0C:
                        b.warn(f"DMA1 channel {c.n}: CPAR 0x{c.cpar:08X} is not {spi.name}_DR (not modelled)")
                    else:
                        c.spi, c.tx = spi, tx
                        want = spi.clk and bool(spi.regs[0x00] & 0x40) and bool(spi.regs[0x04] & (2 if tx else 1))
                elif req:
                    b.warn(f"DMA1 channel {c.n}: DMAMUX request {req} not modelled")
            if want and not c.active:
                c.active = True
                c.next_push = b.cycles if c.tx else NEVER
            elif c.active and not want:
                c.active, c.next_push = False, NEVER
        self.tx_map = {c.spi: c for c in self.ch if c.active and c.tx}
        self.rx_map = {c.spi: c for c in self.ch if c.active and not c.tx}
        self._sched()

    def tx_busy(self, spi):
        """A memory -> SPI stream is feeding `spi`."""
        c = self.tx_map.get(spi)
        return c is not None and c.active

    def rx_take(self, spi, frame):
        """A frame `spi` received: an active SPI -> memory channel stores it (True); otherwise it
        belongs in the SPI's RX FIFO (False)."""
        c = self.rx_map.get(spi)
        if c is None or not c.active:
            return False
        msz = 1 << (c.ccr >> 10 & 3)
        try:
            self.b.uc.mem_write(c.mar, (frame & ((1 << (8 * msz)) - 1)).to_bytes(msz, "little"))
        except UcError:
            self._error(c)
            return True
        if c.ccr & 0x80:
            c.mar = (c.mar + msz) & 0xFFFFFFFF
        # its flags rise once the frame is shifted in (spi.busy_until is still the frame's start)
        self._count(c, max(self.b.cycles, spi.busy_until) + spi.frame_cycles())
        self._sched()
        return True

    def _error(self, c):
        """Bus error: TEIF, and the hardware disables the channel."""
        self.b.warn(f"DMA1 channel {c.n}: transfer error at 0x{c.mar:08X} (channel disabled)")
        c.flags |= 0x8
        c.ccr &= ~1
        c.active, c.next_push, c.pending = False, NEVER, []
        self.b.kick()

    def _count(self, c, t_flag):
        """One data item done: HT at half, TC at the end (pending until t_flag); circular mode
        reloads. True at the end of the block."""
        c.count -= 1
        bits = 0x4 if c.count == c.count0 // 2 else 0
        if c.count == 0:
            bits |= 0x2
            if c.ccr & 0x20:
                c.count, c.mar = c.count0, c.cmar
            else:
                c.active, c.next_push = False, NEVER
        if bits:
            c.pending.append((t_flag, bits))
        return bool(bits & 0x2)

    def _push(self, c, now):
        """Memory -> SPI: feed frames until about CHUNK frames are queued in the SPI."""
        spi, b = c.spi, self.b
        fc = spi.frame_cycles()
        msz, psz = 1 << (c.ccr >> 10 & 3), 1 << (c.ccr >> 8 & 3)
        minc = c.ccr & 0x80
        horizon = now + self.CHUNK * fc
        while c.active and spi.busy_until <= horizon:
            k = min(c.count, max(1, (horizon - max(now, spi.busy_until)) // fc + 1))
            try:
                data = bytes(b.uc.mem_read(c.mar, msz * k if minc else msz))
            except UcError:
                self._error(c)
                return
            for i in range(k):
                v = int.from_bytes(data[i * msz:(i + 1) * msz] if minc else data, "little")
                frame_start = max(now, spi.busy_until)
                spi.tx(v & ((1 << (8 * psz)) - 1), psz)
                if minc:
                    c.mar = (c.mar + msz) & 0xFFFFFFFF
                if self._count(c, max(now, frame_start - fc)):
                    break
        c.next_push = max(now + 1, spi.busy_until - fc) if c.active else NEVER

    def fire(self):
        now = self.b.cycles
        for c in self.ch:
            while c.pending and c.pending[0][0] <= now:
                c.flags |= c.pending.pop(0)[1]
            if c.active and c.tx and c.next_push <= now:
                self._push(c, now)
        self._sched()

    def _sched(self):
        due = NEVER
        for c in self.ch:
            if c.active and c.tx:
                due = min(due, c.next_push)
            if c.pending:
                due = min(due, c.pending[0][0])
        self.due = due
        if hasattr(self.b, "sources"):
            self.b.reschedule()


class Tim(Periph):
    """General-purpose/basic timer, up-counting: update event and interrupt (UG, URS, ARPE, OPM)
    and the output-compare side of the capture/compare channels: CCRx matches set CCxIF (whether or
    not the interrupt is enabled), CCxIE interrupts, OCxPE preload (the new value becomes active at
    the update event), CCxG software events. Input capture, down/center-aligned counting and the
    output pins are not modelled."""

    RESET = {0x2C: 0xFFFF}
    CCR = (0x34, 0x38, 0x3C, 0x40)

    def __init__(self, board, base, name, irq, clock, reset_bit, channels=0):
        self.irq = irq
        self.nch = channels
        self.CLOCK, self.RESET_BIT = clock, reset_bit
        self.due = self.upd_due = NEVER
        super().__init__(board, base, name)

    def reset(self):
        super().reset()
        self.psc_s, self.arr_s = 0, 0xFFFF
        self.ccr_s = [0, 0, 0, 0]       # compare values in use (CCRx itself is the preload register)
        self.matched = 0                # channels whose match in the current period is behind us
        self.t_upd = 0
        self.due = self.upd_due = NEVER # upd_due: next update event; due: next update or match
        if hasattr(self.b, "sources"):
            self.b.reschedule()

    def period(self):
        return (self.psc_s + 1) * (self.arr_s + 1)

    def running(self):
        return self.regs.get(0x00, 0) & 1

    def cnt(self):
        if not self.running():
            return self.regs.get(0x24, 0)
        return ((self.b.cycles - self.t_upd) // (self.psc_s + 1)) % (self.arr_s + 1)

    # --- compare channels
    def _preload(self, k):
        """OCxPE of channel k: CCMR1 bits 3 / 11, CCMR2 bits 3 / 11."""
        return self.regs.get(0x18 + 4 * (k >> 1), 0) >> (3 + 8 * (k & 1)) & 1

    def _load_preloaded(self):
        for k in range(self.nch):
            if self._preload(k):
                self.ccr_s[k] = self.regs.get(self.CCR[k], 0) & 0xFFFF

    def _match_at(self, k):
        c = self.ccr_s[k]
        return self.t_upd + c * (self.psc_s + 1) if c <= self.arr_s else NEVER

    def _fold(self, t):
        """Compare matches of the current period up to cycle t set their CCxIF."""
        if not self.running():
            return
        for k in range(self.nch):
            if not self.matched >> k & 1 and self._match_at(k) <= t:
                self.matched |= 1 << k
                self.regs[0x10] = self.regs.get(0x10, 0) | 2 << k

    def _rematch(self):
        """After a counter or compare-value change: matches already behind the counter do not fire
        in this period any more, the others still do."""
        now = self.b.cycles
        self.matched = 0
        for k in range(self.nch):
            if self._match_at(k) < now:
                self.matched |= 1 << k

    def _next_due(self):
        due = self.upd_due
        if self.running():
            for k in range(self.nch):
                if not self.matched >> k & 1:
                    due = min(due, self._match_at(k))
        return due

    def _sched(self):
        self.due = self._next_due()
        self.b.reschedule()

    # --- registers
    def read(self, off, size):
        a = off & ~3
        if a == 0x24:
            self.b.mmio_n += 1
            return _extract(self.cnt(), off, size)
        if a == 0x10 and self.nch:
            self._fold(self.b.cycles)
        return super().read(off, size)

    def write(self, off, size, value):
        a = off & ~3
        b = self.b
        if self.nch:
            self._fold(b.cycles)
        if a == 0x10:                                   # SR: rc_w0
            self.regs[0x10] = self.regs.get(0x10, 0) & (value | 0xFFFF0000)
            return
        if a == 0x14:                                   # EGR
            cc = value & ((2 << self.nch) - 2)          # CCxG: software compare events
            if cc:
                self.regs[0x10] = self.regs.get(0x10, 0) | cc
                b.kick()
            if value & 1:                               # UG: reinitialise, reload shadows
                self.psc_s, self.arr_s = self.regs.get(0x28, 0), self.regs.get(0x2C, 0xFFFF)
                self._load_preloaded()
                self.regs[0x24] = 0
                self.t_upd = b.cycles
                self.matched = 0
                if not self.regs.get(0x00, 0) & 4:     # URS=0: UG sets UIF
                    self.regs[0x10] = self.regs.get(0x10, 0) | 1
                    b.kick()
                if self.running():
                    self.upd_due = self.t_upd + self.period()
                    self._sched()
            return
        was = self.running()
        cnt = self.cnt()
        super().write(off, size, value)
        if a == 0x00:
            if self.running() and not was:
                self.t_upd = b.cycles - cnt * (self.psc_s + 1)
                self.upd_due = self.t_upd + self.period()
                self._rematch()
            elif was and not self.running():
                self.regs[0x24] = cnt
                self.upd_due = NEVER
            self._sched()
        elif a == 0x2C and not self.regs.get(0x00, 0) & 0x80:   # ARR without preload
            self.arr_s = self.regs[0x2C]
            if self.running():
                self.upd_due = self.t_upd + self.period()
                self._sched()
        elif a == 0x24:
            self.t_upd = b.cycles - (value & 0xFFFF) * (self.psc_s + 1)
            if self.running():
                self._rematch()
                self.upd_due = self.t_upd + self.period()
                self._sched()
        elif a == 0x0C:
            b.kick()
        elif a in self.CCR[:self.nch]:
            k = self.CCR.index(a)
            if not self._preload(k):
                self.ccr_s[k] = self.regs[a] & 0xFFFF
                if self.running():
                    self._rematch()
                    self._sched()

    def fire(self):
        now = self.b.cycles
        if self.nch:
            self._fold(min(now, self.upd_due - 1))
        if self.upd_due <= now:
            self._update()
        if self.nch:
            self._fold(now)
        self.due = self._next_due()

    def _update(self):
        """Update event (counter overflow): UIF, shadow registers reloaded, next period."""
        self.regs[0x10] = self.regs.get(0x10, 0) | 1
        self.psc_s, self.arr_s = self.regs.get(0x28, 0), self.regs.get(0x2C, 0xFFFF)
        self._load_preloaded()
        self.t_upd = self.upd_due
        self.matched = 0
        if self.regs.get(0x00, 0) & 8:                  # one-pulse mode
            self.regs[0x00] &= ~1
            self.upd_due = NEVER
        else:
            self.upd_due = self.t_upd + self.period()
            if self.upd_due <= self.b.cycles:          # never fall behind by more than one period
                self.upd_due = self.b.cycles + self.period()

    def irq_level(self):
        return self.regs.get(0x10, 0) & self.regs.get(0x0C, 0) & 0x1F


class Usart(Periph):
    """USART with 8N1 frame timing from BRR. Without FIFO (CR1.FIFOEN = 0): 1-byte RDR/TDR with
    RXNE/TXE/TC/ORE as the classic firmware uses them. With FIFO: 8-byte RX and TX FIFOs; RXFNE
    rises with every received byte, RXFF when eight are waiting, ORE when one more arrives; TXFNF
    while the TX FIFO has room (the shift register takes the first byte at once), TXFE when it is
    empty, TC when the last frame is out; RXFT/TXFT thresholds (CR3) and all their interrupts."""

    FIFO = 8
    FT = (1, 2, 4, 6, 7, 8)             # RXFTCFG / TXFTCFG 0..5: 1/8 1/4 1/2 3/4 7/8 8/8 of the depth

    def __init__(self, board, base, name, irq, clock, reset_bit):
        self.irq = irq
        self.CLOCK, self.RESET_BIT = clock, reset_bit
        self.due = NEVER
        self.rxq = deque()
        self.rx_last = 0
        super().__init__(board, base, name)

    def reset(self):
        super().reset()
        self.rdr = 0
        self.rxne = False
        self.ore = False
        self.rxf = deque()      # FIFO mode: received bytes waiting
        self.tx_starts = deque()   # FIFO mode: start cycles of the frames still in the TX FIFO
        self.tx_free = 0        # cycle when the shift register is empty again (TC)
        self.txe_at = 0         # cycle when TDR moves into the shift register (TXE)
        self.tc_clear = -1
        self.isr_n = -10
        self.isr_last = None
        self._update_due()

    def fifo(self):
        return self.regs.get(0x00, 0) >> 29 & 1

    def frame(self):
        brr = self.regs.get(0x0C, 0)
        return (brr if brr >= 16 else 556) * 10

    # --- receive side
    def inject(self, t, data):
        """Bytes sent to this port, the first one starting on the wire at cycle t."""
        f = self.frame()
        t = max(t, self.rx_last)
        for byte in data:
            t += f
            self.rxq.append((t, byte))
        self.rx_last = t
        self._update_due()

    def fire(self):
        b = self.b
        now = b.cycles
        fifo = self.fifo()
        while self.rxq and self.rxq[0][0] <= now:
            _, byte = self.rxq.popleft()
            cr1 = self.regs.get(0x00, 0)
            b.uart_rx(self, byte)
            if not (cr1 & 1 and cr1 & 4):
                b.warn(f"{self.name}: byte received while the receiver is disabled (lost)")
            elif fifo:
                if len(self.rxf) >= self.FIFO:
                    self.ore = True
                    b.warn(f"{self.name}: receive FIFO overrun (byte lost; ORE set)")
                else:
                    self.rxf.append(byte)
            elif self.rxne:
                self.ore = True
                b.warn(f"{self.name}: receive overrun (byte lost; ORE set)")
            else:
                self.rdr, self.rxne = byte, True
        self._update_due()
        b.kick()

    def _tx_pending(self, now):
        """FIFO mode: frames waiting in the TX FIFO (not yet in the shift register)."""
        q = self.tx_starts
        while q and q[0] <= now:
            q.popleft()
        return len(q)

    def _tx_changes(self):
        """Cycles at which TXE/TXFNF or TC can change."""
        if self.fifo():
            return (self.tx_starts[0] if self.tx_starts else 0, self.tx_free)
        return (self.txe_at, self.tx_free)

    def _update_due(self):
        due = self.rxq[0][0] if self.rxq else NEVER
        cr1 = self.regs.get(0x00, 0)
        now = self.b.cycles if hasattr(self.b, "cycles") else 0
        if cr1 >> 29 & 1:
            if cr1 & 0xC0 or cr1 >> 30 & 1 or self.regs.get(0x08, 0) >> 23 & 1:   # a TX interrupt is on
                if self._tx_pending(now):
                    due = min(due, self.tx_starts[0])
                if self.tx_free > now:
                    due = min(due, self.tx_free)
        else:
            if cr1 & 0x80 and self.txe_at > now:
                due = min(due, self.txe_at)
            if cr1 & 0x40 and self.tx_free > now:
                due = min(due, self.tx_free)
        self.due = due
        if hasattr(self.b, "sources"):
            self.b.reschedule()

    # --- registers
    def isr(self):
        now = self.b.cycles
        cr1 = self.regs.get(0x00, 0)
        v = self.ore << 3
        if now >= self.tx_free and self.tx_free > self.tc_clear:
            v |= 1 << 6
        if cr1 >> 29 & 1:
            n_rx, pend = len(self.rxf), self._tx_pending(now)
            cr3 = self.regs.get(0x08, 0)
            if n_rx:
                v |= 1 << 5                             # RXFNE
            if n_rx >= self.FIFO:
                v |= 1 << 24                            # RXFF
            if n_rx >= self.FT[min(cr3 >> 25 & 7, 5)]:
                v |= 1 << 26                            # RXFT
            if pend < self.FIFO:
                v |= 1 << 7                             # TXFNF
            if pend == 0:
                v |= 1 << 23                            # TXFE
            if self.FIFO - pend >= self.FT[min(cr3 >> 29 & 7, 5)]:
                v |= 1 << 27                            # TXFT
        else:
            v |= self.rxne << 5
            if now >= self.txe_at:
                v |= 1 << 7
        if cr1 & 1:
            v |= ((cr1 >> 3) & 1) << 21 | ((cr1 >> 2) & 1) << 22
        return v

    def read(self, off, size):
        a = off & ~3
        if a == 0x1C:
            b = self.b
            v = self.isr()
            if (v & 0xC0) != 0xC0:                      # transmission in progress: TXE/TXFNF/TC will change
                if self.isr_n == b.mmio_n and v == self.isr_last:
                    # tight polling loop waiting for TXE/TC: jump to the next flag change
                    now = b.cycles
                    nxt = min([t for t in self._tx_changes() if t > now] or [now])
                    nxt = min(nxt, b.next_event)
                    if nxt > now:
                        b.cycles = nxt
                        v = self.isr()
                b.mmio_n += 1
                self.isr_n, self.isr_last = b.mmio_n, v
            return _extract(v, off, size)
        if a == 0x24:                                   # RDR
            self.b.mmio_n += 1
            if self.fifo():
                return (self.rxf.popleft() if self.rxf else 0) & ((1 << (8 * size)) - 1) if off == a else 0
            self.rxne = False
            return self.rdr & ((1 << (8 * size)) - 1) if off == a else 0
        if a == 0x28:
            return 0
        return super().read(off, size)

    def write(self, off, size, value):
        a = off & ~3
        b = self.b
        if a == 0x28:                                   # TDR
            cr1 = self.regs.get(0x00, 0)
            if not (cr1 & 1 and cr1 & 8):
                b.warn(f"{self.name}: TDR written while the transmitter is disabled (nothing sent)")
                return
            now = b.cycles
            if cr1 >> 29 & 1:
                if self._tx_pending(now) >= self.FIFO:
                    b.warn(f"{self.name}: TDR written while the TX FIFO is full (byte lost)")
                    return
            elif now < self.txe_at:
                b.warn(f"{self.name}: TDR written while TXE=0 (previous byte overwritten on real hardware)")
            start = max(now, self.tx_free)
            end = start + self.frame()
            self.txe_at, self.tx_free = start, end
            if cr1 >> 29 & 1 and start > now:
                self.tx_starts.append(start)
            b.uart_tx(self, value & 0xFF, start, end)
            self._update_due()
            return
        if a == 0x20:                                   # ICR
            if value & 8:
                self.ore = False
            if value & 0x40:
                self.tc_clear = b.cycles
            return
        if a == 0x18:                                   # RQR
            if value & 8:                               # RXFRQ: drop the received data
                self.rxne = False
                self.rxf.clear()
            return
        if a == 0x1C or a == 0x24:
            return
        super().write(off, size, value)
        if a == 0x00 or (a == 0x08 and self.fifo()):
            self._update_due()
            b.kick()

    def irq_level(self):
        cr1 = self.regs.get(0x00, 0)
        if not cr1 & 1:
            return False
        if cr1 >> 29 & 1:
            v = self.isr()
            cr3 = self.regs.get(0x08, 0)
            return bool((cr1 & 0x20 and (v & 0x20 or self.ore)) or (cr1 & 0x80 and v & 0x80)
                        or (cr1 & 0x40 and v & 0x40) or (cr1 >> 30 & 1 and v >> 23 & 1)
                        or (cr1 >> 31 & 1 and v >> 24 & 1) or (cr3 >> 28 & 1 and v >> 26 & 1)
                        or (cr3 >> 23 & 1 and v >> 27 & 1) or (cr3 & 1 and self.ore))
        if cr1 & 0x20 and (self.rxne or self.ore):
            return True
        if cr1 & 0xC0:
            v = self.isr()
            return bool((cr1 & 0x80 and v & 0x80) or (cr1 & 0x40 and v & 0x40))
        return False
