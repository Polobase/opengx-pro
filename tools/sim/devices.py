"""Devices on the GX Pro display board: ST7789 panel (SPI2), W25Q16 SPI NOR (SPI1) and the two
generator boards on USART1/USART2 (acknowledge model)."""

import array

from PIL import Image

MS = 64_000        # cycles per millisecond at 64 MHz


class St7789:
    """ST7789 controller: command decoding, 240x320 GRAM (physical order). MADCTL MY/MX/MV map the
    address counters (landscape 0xA0: CASET runs along the logical X axis, RASET along Y); ML and
    MH only change the refresh order (not modelled); RGB means BGR panel order, red and blue
    exchanged on the glass. Vertical scrolling (VSCRDEF areas, VSCSAD start row) remaps the
    physical rows on the glass until NORON; sleep (SLPIN) and display off (DISPOFF) show black.
    16-bit bus frames arrive as two bytes, high byte first, so a command frame 0x00cc is NOP
    followed by the command. The MADCTL mapping was checked on the unit (0xA0 upright, 0x60 turned
    180 degrees); the scroll direction is still to be confirmed (docs/nx/drop0-checklist.md, nxbring
    page 3)."""

    W, H = 240, 320

    def __init__(self, panel_inverted=True):
        self.fb = bytearray(self.W * self.H * 2)      # big-endian RGB565, as sent on the bus
        self.panel_inverted = panel_inverted          # panel needs INVON to show GRAM colours
        self.log = []                                 # (cmd, args) except pixel data and NOP, first 2000
        self.pixels = 0
        self.hw_reset()

    def hw_reset(self):
        self.cmd = None
        self.args = bytearray()
        self.win = [0, self.W - 1, 0, self.H - 1]     # column start/end, row start/end
        self.c = self.r = 0
        self.hi = None
        self.madctl = 0
        self.colmod = 0x66
        self.sleep = True
        self.disp_on = False
        self.inverted = False
        self.vscroll = [0, self.H, 0]                 # VSCRDEF: top fixed, scroll area, bottom fixed (rows)
        self.vsp = 0                                  # VSCSAD: memory row shown first in the scroll area
        self.scrolling = False                        # VSCSAD seen since the last NORON

    def power_on(self):
        """Supply back after a power cut: the GRAM content is lost (black here), registers reset."""
        self.fb[:] = bytes(len(self.fb))
        self.hw_reset()

    def byte(self, dc, b):
        if not dc:
            self._end_cmd()
            self.cmd, self.args, self.hi = b, bytearray(), None
            if b in (0x2C, 0x3C):
                if b == 0x2C:
                    self.c, self.r = self.win[0], self.win[2]
            elif b == 0x01:
                self.hw_reset()
            elif b == 0x11:
                self.sleep = False
            elif b == 0x10:
                self.sleep = True
            elif b == 0x29:
                self.disp_on = True
            elif b == 0x28:
                self.disp_on = False
            elif b == 0x21:
                self.inverted = True
            elif b == 0x20:
                self.inverted = False
            elif b == 0x13:
                self.scrolling = False
            return
        cmd = self.cmd
        if cmd == 0x2C or cmd == 0x3C:
            if self.hi is None:
                self.hi = b
                return
            hi, self.hi = self.hi, None
            c, r = self.c, self.r
            if self.madctl & 0xE0:
                idx = self._phys(c, r)
            else:
                idx = (r * 240 + c) * 2 if c < 240 and r < 320 else -1
            if idx >= 0:
                self.fb[idx] = hi
                self.fb[idx + 1] = b
            self.pixels += 1
            if c >= self.win[1]:
                self.c = self.win[0]
                self.r = r + 1 if r < self.win[3] else self.win[2]
            else:
                self.c = c + 1
            return
        self.args.append(b)
        a = self.args
        if len(a) == 4 and cmd in (0x2A, 0x2B):
            k = 0 if cmd == 0x2A else 2
            self.win[k], self.win[k + 1] = a[0] << 8 | a[1], a[2] << 8 | a[3]
        elif cmd == 0x36 and len(a) == 1:
            self.madctl = b
        elif cmd == 0x3A and len(a) == 1:
            self.colmod = b
        elif cmd == 0x33 and len(a) == 6:
            self.vscroll = [a[0] << 8 | a[1], a[2] << 8 | a[3], a[4] << 8 | a[5]]
        elif cmd == 0x37 and len(a) == 2:
            self.vsp = a[0] << 8 | a[1]
            self.scrolling = True

    def _end_cmd(self):
        if self.cmd is not None and self.cmd not in (0x00, 0x2C, 0x3C) and len(self.log) < 2000:
            self.log.append((self.cmd, bytes(self.args)))
        elif self.cmd in (0x2C, 0x3C) and len(self.log) < 2000 and (not self.log or self.log[-1][0] != self.cmd):
            self.log.append((self.cmd, b""))

    def _phys(self, c, r):
        # MV exchanges the column and row counters, then MX / MY mirror the physical column (x) and
        # row (y) (ST7789 datasheet 8.12). Mirroring the counters before the exchange instead turns an
        # MV picture by 180 degrees: on the unit 0x60 shows the landscape picture upside down.
        mv = self.madctl & 0x20
        cmax, rmax = (319, 239) if mv else (239, 319)
        if c > cmax or r > rmax:
            return -1
        x, y = (r, c) if mv else (c, r)
        if self.madctl & 0x40:
            x = 239 - x
        if self.madctl & 0x80:
            y = 319 - y
        return (y * 240 + x) * 2

    def showing(self):
        return self.disp_on and not self.sleep

    def state(self):
        return {"display_on": self.disp_on, "sleep": self.sleep, "inversion": self.inverted,
                "madctl": self.madctl, "colmod": self.colmod,
                "vscroll": [*self.vscroll, self.vsp] if self.scrolling else None}

    def _scanout(self):
        """GRAM in the order the panel shows its rows: while scrolling, the scroll area starts at
        memory row VSP and wraps inside the area."""
        fb = bytes(self.fb)
        tfa, vsa, _ = self.vscroll
        if not self.scrolling or vsa == 0 or tfa + vsa > self.H:
            return fb
        row = self.W * 2
        out = bytearray(fb)
        for k in range(vsa):
            src = tfa + (self.vsp - tfa + k) % vsa
            out[(tfa + k) * row:(tfa + k + 1) * row] = fb[src * row:(src + 1) * row]
        return bytes(out)

    @staticmethod
    def brightness_steps(backlight, full=0.5):
        """Screenshot brightness in 1/64 steps from the PB12 duty: `full` (default 8/16, the classic
        firmware's normal level) or more is full brightness, 0 keeps 1/6 so the content stays
        visible, as before. A bool means on (full) or off."""
        if backlight is True or backlight is False:
            level = float(backlight)
        else:
            level = min(1.0, max(0.0, backlight) / full) if full > 0 else 1.0
        return round(level * 64)

    def image(self, backlight=1.0, scale=2, full=0.5):
        """Logical (landscape) view as seen on the glass: X = 319 - y_row, Y = x_phys."""
        px = array.array("H")
        px.frombytes(self._scanout())
        px.byteswap()
        if self.inverted != self.panel_inverted:
            px = array.array("H", (v ^ 0xFFFF for v in px))
        im = Image.frombytes("RGB", (self.W, self.H), px.tobytes(), "raw", "BGR;16")
        if self.madctl & 0x08:                        # BGR panel order: red and blue exchanged
            r, g, b = im.split()
            im = Image.merge("RGB", (b, g, r))
        im = im.transpose(Image.Transpose.ROTATE_270)
        if not self.showing():
            im = Image.new("RGB", im.size)
        else:
            q = self.brightness_steps(backlight, full)
            if q < 64:
                im = im.point(lambda v: v * (64 + 5 * q) // 384)
        return im.resize((im.width * scale, im.height * scale), Image.NEAREST) if scale != 1 else im


class W25Q16:
    """Winbond W25Q16 (2 MB) SPI NOR: read/fast read, JEDEC/manufacturer/unique ID, status,
    write enable, page program, 4K/32K/64K/chip erase, typical busy times."""

    SIZE = 0x200000
    JEDEC = (0xEF, 0x40, 0x15)
    T_PP, T_SE, T_BE32, T_BE64, T_CE = 0.4, 45, 120, 150, 5000     # ms, datasheet typical
    UID = bytes.fromhex("D1636C1A4F2B3C17")

    def __init__(self, board, image):
        self.b = board
        self.mem = bytearray(image[:self.SIZE].ljust(self.SIZE, b"\xff"))
        self.orig = bytes(self.mem)
        self.ops = []                # {"t_ms", "op", "addr", "len"}
        self.sel = False
        self.cmd = None
        self.n = 0
        self.addr = 0
        self.data = bytearray()
        self.wel = False
        self.busy_until = 0
        self.polls = 0
        self.inflight = None         # (kind, base, before, order, t0, t1) of the erase/program in progress
        self.cut_armed = None        # 'program' | 'erase' | 'any': power cut halfway through the next one

    def select(self):
        self.sel, self.cmd, self.n, self.addr, self.data = True, None, 0, 0, bytearray()

    def deselect(self):
        if self.sel and self.cmd is not None and self.cmd >= 0:
            self._finish()
        self.sel = False

    def status(self):
        return (1 if self.b.cycles < self.busy_until else 0) | (2 if self.wel else 0)

    def xfer(self, byte):
        if not self.sel:
            return 0xFF
        n = self.n
        self.n = n + 1
        if n == 0:
            self.cmd = byte
            if byte != 0x05:
                self.polls = 0
            if self.b.cycles < self.busy_until and byte not in (0x05, 0x35, 0x15, 0x9F):
                self.b.warn(f"SPI flash: command 0x{byte:02X} while busy (ignored by the chip)")
                self.cmd = -1
            return 0xFF
        cmd = self.cmd
        if cmd == 0x03 or cmd == 0x0B:
            if n <= 3:
                self.addr = (self.addr << 8 | byte) & 0xFFFFFF
                return 0xFF
            if cmd == 0x0B and n == 4:
                return 0xFF
            v = self.mem[self.addr % self.SIZE]
            self.addr += 1
            return v
        if cmd == 0x05:
            self.polls += 1
            if self.polls > 2 and self.b.cycles < self.busy_until:     # busy polling: skip ahead
                self.b.cycles = max(self.b.cycles, min(self.busy_until, self.b.next_event))
            return self.status()
        if cmd == 0x9F:
            return self.JEDEC[n - 1] if n <= 3 else 0x00
        if cmd in (0x02, 0x20, 0x52, 0xD8):
            if n <= 3:
                self.addr = (self.addr << 8 | byte) & 0xFFFFFF
            else:
                self.data.append(byte)
            return 0xFF
        if cmd == 0x90:
            return 0xFF if n <= 3 else (0xEF, 0x14)[(n - 4) & 1]
        if cmd == 0xAB:
            return 0xFF if n <= 3 else 0x14
        if cmd == 0x4B:
            return 0xFF if n <= 4 else self.UID[(n - 5) % 8]
        if cmd in (0x35, 0x15):
            return 0x00
        return 0xFF

    def _op(self, op, addr, length, ms):
        b = self.b
        if not self.wel:
            b.warn(f"SPI flash: {op} without WREN (ignored by the chip)")
            return False
        self.wel = False
        self.busy_until = b.cycles + int(ms * MS)
        self.ops.append({"t_ms": round(b.ms, 3), "op": op, "addr": addr, "len": length})
        if not b.cfg.allow_spi_write:
            b.violation(f"SPI flash: {op} 0x{addr:06X} len {length}")
        return True

    def _finish(self):
        cmd = self.cmd
        if cmd == 0x06:
            self.wel = True
        elif cmd == 0x04:
            self.wel = False
        elif cmd == 0x02 and self.n > 4:
            data = self.data[-256:]
            if self._op("program", self.addr, len(data), self.T_PP):
                page, off = self.addr & ~0xFF, self.addr & 0xFF
                before = bytes(self.mem[page:page + 256])
                order = []
                for i, d in enumerate(data):
                    a = page | ((off + i) & 0xFF)
                    self.mem[a] &= d
                    order.append(a)
                self._started("program", page, before, order)
        elif cmd in (0x20, 0x52, 0xD8) and self.n >= 4:
            size, t = {0x20: (0x1000, self.T_SE), 0x52: (0x8000, self.T_BE32), 0xD8: (0x10000, self.T_BE64)}[cmd]
            start = self.addr & ~(size - 1)
            if self._op(f"erase{size >> 10}k", start, size, t):
                before = bytes(self.mem[start:start + size])
                self.mem[start:start + size] = b"\xff" * size
                self._started("erase", start, before, range(start, start + size))
        elif cmd in (0xC7, 0x60):
            if self._op("chip_erase", 0, self.SIZE, self.T_CE):
                before = bytes(self.mem)
                self.mem[:] = b"\xff" * self.SIZE
                self._started("erase", 0, before, range(0, self.SIZE))
        elif cmd in (0x01, 0x31, 0x11):
            self._op(f"write_status_0x{cmd:02X}", 0, self.n - 1, 15)

    def _started(self, kind, base, before, order):
        """An erase/program is in progress (its effect is applied at once and undone in part by a
        power cut before it ends); an armed cut is scheduled for its middle."""
        b = self.b
        self.inflight = (kind, base, before, order, b.cycles, self.busy_until)
        if self.cut_armed in ("any", kind):
            self.cut_armed = None
            b.at_cycles((b.cycles + self.busy_until) // 2, b.power_cut)

    def arm_cut(self, kind):
        if kind not in ("program", "erase", "any"):
            raise ValueError(f"power_cut tear must be 'program', 'erase' or 'any', not {kind!r}")
        self.cut_armed = kind

    def power_cut(self):
        """Supply lost: an operation in progress stops part-way (bytes in the chip's order: the
        first ones done, the others as before) and the chip's state is lost; the array is kept."""
        b = self.b
        op = self.inflight
        if op and b.cycles < self.busy_until:
            kind, base, before, order, t0, t1 = op
            done = int(len(order) * (b.cycles - t0) / max(1, t1 - t0))
            if isinstance(order, range):
                self.mem[order.start + done:order.stop] = before[order.start + done - base:order.stop - base]
            else:
                for a in order[done:]:
                    self.mem[a] = before[a - base]
            self.ops[-1]["torn_after"] = done
            b.event(f"power cut tore the SPI-flash {kind} at 0x{base:06X} after {done} of {len(order)} bytes")
        self.inflight = None
        self.sel, self.cmd, self.n, self.wel, self.busy_until, self.polls = False, None, 0, False, 0, 0

    def changed_sectors(self):
        """Start addresses of the 4 KB sectors that differ from the initial image."""
        return [s for s in range(0, self.SIZE, 0x1000) if self.mem[s:s + 0x1000] != self.orig[s:s + 0x1000]]

    def changed_ranges(self):
        """[(start, end)] of bytes that differ from the initial image, merged per 4 KB sector."""
        out = []
        for s in self.changed_sectors():
            if out and out[-1][1] == s:
                out[-1] = (out[-1][0], s + 0x1000)
            else:
                out.append((s, s + 0x1000))
        return out


class GeneratorBoard:
    """Stand-in for a generator board: answers every ':w...' line the display sends with
    ':ok\\r\\n' after a fixed delay (the display's gen_wait_ok waits for it), and every wave
    table (':aNN=' and 1024 samples) TABLE_ACK_MS after the end of its line (a Spooky2 capture
    shows 40 to 700 ms)."""

    TABLE_ACK_MS = 300.0

    def __init__(self, board, usart, ack_delay_ms=1.0, extra_replies=None):
        self.b = board
        self.usart = usart
        self.delay = int(ack_delay_ms * MS)
        self.extra = extra_replies or {}        # exact line text -> reply text
        self.acks = 0
        self.head = None                        # start of a line that arrives in pieces

    def on_line(self, text, t_end, complete=True):
        if self.head is None:
            self.head = text
        if not complete:                        # the board cuts lines of more than 512 bytes
            return
        head, self.head = self.head, None
        reply, delay = None, self.delay
        if text in self.extra:
            reply = self.extra[text]
        elif head.startswith(":w"):
            reply = ":ok\r\n"
        elif head.startswith(":a"):
            reply, delay = ":ok\r\n", int(self.TABLE_ACK_MS * MS)
        if reply:
            self.acks += 1
            self.usart.inject(t_end + delay, reply.encode("latin-1"))
