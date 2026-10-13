#!/usr/bin/env python3
"""Low-fidelity schematic sheets for the Spooky2 GeneratorX Pro (GX Pro), rendered as SVG.

Sources: board photos in ../ (all IC markings are removed, so parts are identified by
footprint, silkscreen and firmware behaviour) and the display-firmware RE in docs/spec/.

Run:  python3 build_schematic.py   -> writes sheet1_system.svg, sheet2_display_board.svg,
                                      sheet3_main_board.svg next to this file.
Solid lines/boxes = seen on the board or verified in firmware; dashed = inferred.
"""
from pathlib import Path
from xml.sax.saxutils import escape

W, H = 1680, 1188
FONT = "Menlo, 'DejaVu Sans Mono', Consolas, monospace"
C_PART = "#8b1a1a"     # component outline
C_FILL = "#fffbe6"     # component fill
C_WIRE = "#1f6f1f"     # wires
C_TEXT = "#111"
C_NOTE = "#555"
C_BOARD = "#3a5a8a"    # board outline
C_NET = "#1c3f94"      # net labels
DASH = "6 4"
TODAY = "2026-09-23"


class Sheet:
    def __init__(self, no, total, title, subtitle):
        self.no, self.total, self.title, self.subtitle = no, total, title, subtitle
        self.el = []

    # ---------- primitives ----------
    def rect(self, x, y, w, h, stroke=C_PART, fill=C_FILL, sw=1.6, dashed=False, rx=0):
        d = f' stroke-dasharray="{DASH}"' if dashed else ""
        self.el.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" '
                       f'fill="{fill}" stroke="{stroke}" stroke-width="{sw}"{d}/>')

    def text(self, x, y, s, size=12, anchor="start", weight="normal", color=C_TEXT,
             italic=False, rotate=None):
        st = ' font-style="italic"' if italic else ""
        rt = f' transform="rotate({rotate} {x} {y})"' if rotate is not None else ""
        self.el.append(f'<text x="{x}" y="{y}" font-size="{size}" text-anchor="{anchor}" '
                       f'font-weight="{weight}" fill="{color}"{st}{rt}>{escape(s)}</text>')

    def lines(self, x, y, rows, size=12, lh=None, **kw):
        lh = lh or size + 4
        for i, r in enumerate(rows):
            self.text(x, y + i * lh, r, size=size, **kw)

    def wire(self, pts, dashed=False, color=C_WIRE, sw=1.6, arrow=False):
        d = f' stroke-dasharray="{DASH}"' if dashed else ""
        a = ' marker-end="url(#arrow)"' if arrow else ""
        p = " ".join(f"{x},{y}" for x, y in pts)
        self.el.append(f'<polyline points="{p}" fill="none" stroke="{color}" '
                       f'stroke-width="{sw}"{d}{a}/>')

    def dot(self, x, y):
        self.el.append(f'<circle cx="{x}" cy="{y}" r="3" fill="{C_WIRE}"/>')

    def nc(self, x, y):
        self.wire([(x - 5, y - 5), (x + 5, y + 5)], color=C_PART)
        self.wire([(x - 5, y + 5), (x + 5, y - 5)], color=C_PART)

    def gnd(self, x, y):
        self.wire([(x, y), (x, y + 10)])
        for i, hw in enumerate((10, 6, 2)):
            self.wire([(x - hw, y + 10 + i * 4), (x + hw, y + 10 + i * 4)])

    def netlabel(self, x, y, name, side="right", dashed=False):
        """Flag-shaped label whose tip touches (x, y); side = direction the label extends."""
        w = 7.3 * len(name) + 14
        if side == "right":
            pts = [(x, y), (x + 7, y - 9), (x + w, y - 9), (x + w, y + 9), (x + 7, y + 9)]
            tx = x + 10
        else:
            pts = [(x, y), (x - 7, y - 9), (x - w, y - 9), (x - w, y + 9), (x - 7, y + 9)]
            tx = x - w + 4
        d = f' stroke-dasharray="{DASH}"' if dashed else ""
        p = " ".join(f"{a},{b}" for a, b in pts)
        self.el.append(f'<polygon points="{p}" fill="#eef2fb" stroke="{C_NET}" '
                       f'stroke-width="1.2"{d}/>')
        self.text(tx, y + 4, name, size=11.5, color=C_NET)

    # ---------- symbols ----------
    def switch(self, x, y, label=None):
        """Normally-open push button from (x, y) to (x + 40, y)."""
        self.wire([(x, y), (x + 8, y)])
        self.wire([(x + 32, y), (x + 40, y)])
        for cx in (x + 10, x + 30):
            self.el.append(f'<circle cx="{cx}" cy="{y}" r="2.2" fill="none" stroke="{C_PART}"/>')
        self.wire([(x + 11, y - 2), (x + 31, y - 9)], color=C_PART)
        self.wire([(x + 20, y - 6), (x + 20, y - 12), (x + 14, y - 12), (x + 26, y - 12)],
                  color=C_PART)
        if label:
            self.text(x + 46, y + 4, label, size=11)

    def crystal(self, x, y, h=40):
        """Vertical crystal between (x, y) and (x, y + h)."""
        m = y + h / 2
        self.wire([(x, y), (x, m - 9)])
        self.wire([(x, m + 9), (x, y + h)])
        self.wire([(x - 8, m - 9), (x + 8, m - 9)], color=C_PART, sw=2)
        self.wire([(x - 8, m + 9), (x + 8, m + 9)], color=C_PART, sw=2)
        self.rect(x - 6, m - 5, 12, 10, sw=1.4)

    def cap(self, x, y, h=30):
        """Vertical capacitor from (x, y) down to (x, y + h)."""
        m = y + h / 2
        self.wire([(x, y), (x, m - 3)])
        self.wire([(x, m + 3), (x, y + h)])
        self.wire([(x - 8, m - 3), (x + 8, m - 3)], color=C_PART, sw=2)
        self.wire([(x - 8, m + 3), (x + 8, m + 3)], color=C_PART, sw=2)

    def opamp(self, x, y, label=None, dashed=False):
        """Triangle with inputs on the left at (x, y +/- 10), output at (x + 50, y)."""
        d = f' stroke-dasharray="{DASH}"' if dashed else ""
        self.el.append(f'<polygon points="{x},{y - 22} {x + 50},{y} {x},{y + 22}" '
                       f'fill="{C_FILL}" stroke="{C_PART}" stroke-width="1.6"{d}/>')
        self.text(x + 5, y - 6, "−", size=11, color=C_PART)
        self.text(x + 5, y + 14, "+", size=11, color=C_PART)
        if label:
            self.text(x + 25, y + 38, label, size=11, anchor="middle")

    def jack(self, x, y, label):
        """Output jack symbol, signal enters at (x, y)."""
        self.wire([(x, y), (x + 14, y)])
        self.el.append(f'<circle cx="{x + 22}" cy="{y}" r="8" fill="#fff" '
                       f'stroke="{C_PART}" stroke-width="1.6"/>')
        self.el.append(f'<circle cx="{x + 22}" cy="{y}" r="2.5" fill="{C_PART}"/>')
        self.text(x + 36, y + 4, label, size=12, weight="bold")

    # ---------- composite ----------
    def ic(self, x, y, w, ref, value, left=(), right=(), pitch=24, top=58, stub=30,
           dashed=False, extra=()):
        """Box with pins; None in a pin list leaves a gap. Returns {pin: (x, y)}."""
        n = max(len(left), len(right), 1)
        h = top + n * pitch
        self.rect(x, y, w, h, dashed=dashed)
        self.text(x, y - 7, ref, size=12, weight="bold", color=C_PART)
        self.text(x + w / 2, y + 20, value, size=13, anchor="middle", weight="bold")
        for i, e in enumerate(extra):
            self.text(x + w / 2, y + 36 + i * 14, e, size=11, anchor="middle", color=C_NOTE)
        pins = {}
        for i, p in enumerate(left):
            if p is None:
                continue
            py = y + top + i * pitch
            self.wire([(x - stub, py), (x, py)], color=C_PART)
            self.text(x + 6, py + 4, p, size=11.5)
            pins[p] = (x - stub, py)
        for i, p in enumerate(right):
            if p is None:
                continue
            py = y + top + i * pitch
            self.wire([(x + w, py), (x + w + stub, py)], color=C_PART)
            self.text(x + w - 6, py + 4, p, size=11.5, anchor="end")
            pins[p] = (x + w + stub, py)
        return pins, h

    def block(self, x, y, w, h, title, rows=(), dashed=False, fill=C_FILL, stroke=C_PART,
              size=11.5, title_size=13):
        self.rect(x, y, w, h, dashed=dashed, fill=fill, stroke=stroke)
        self.text(x + 8, y + 18, title, size=title_size, weight="bold")
        self.lines(x + 8, y + 36, rows, size=size, lh=size + 4)

    def board(self, x, y, w, h, name, sub=""):
        self.rect(x, y, w, h, stroke=C_BOARD, fill="#f4f7fc", sw=2.4, rx=10)
        self.text(x + 12, y + 24, name, size=15, weight="bold", color=C_BOARD)
        if sub:
            self.text(x + 12, y + 41, sub, size=11, color=C_BOARD)

    def note(self, x, y, w, rows, title=None, size=11.5):
        h = (len(rows) + (1 if title else 0)) * (size + 4) + 16
        self.rect(x, y, w, h, stroke="#999", fill="#fafafa", sw=1)
        yy = y + 18
        if title:
            self.text(x + 8, yy, title, size=size + 1, weight="bold")
            yy += size + 4
        self.lines(x + 8, yy, rows, size=size, lh=size + 4, color=C_NOTE)
        return h

    # ---------- frame ----------
    def frame(self):
        self.rect(12, 12, W - 24, H - 24, stroke="#333", fill="none", sw=1.2)
        self.text(30, 44, self.title, size=20, weight="bold")
        self.text(30, 66, self.subtitle, size=12, color=C_NOTE)
        # legend
        lx, ly = 30, H - 92
        self.rect(lx, ly, 560, 70, stroke="#999", fill="#fff", sw=1)
        self.wire([(lx + 12, ly + 20), (lx + 52, ly + 20)])
        self.text(lx + 60, ly + 24, "seen on the board / verified in firmware", size=11)
        self.wire([(lx + 12, ly + 42), (lx + 52, ly + 42)], dashed=True)
        self.text(lx + 60, ly + 46, "inferred (not traced)", size=11)
        self.text(lx + 330, ly + 24, "[FW]  display-firmware RE", size=11)
        self.text(lx + 330, ly + 40, "[PCB] silkscreen / footprint", size=11)
        self.text(lx + 330, ly + 56, "[I] inferred   ? unknown", size=11)
        self.text(lx + 12, ly + 62, "All IC markings are removed.", size=11, color=C_NOTE)
        # title block
        tx, ty, tw, th = W - 482, H - 92, 452, 70
        self.rect(tx, ty, tw, th, stroke="#333", fill="#fff", sw=1.2)
        self.text(tx + 10, ty + 22, "Spooky2 GeneratorX Pro: drawn from the board", size=13,
                  weight="bold")
        self.text(tx + 10, ty + 40, "Low-fidelity schematic, rev 0.1", size=11.5)
        self.text(tx + 10, ty + 58, f"{TODAY}", size=11.5, color=C_NOTE)
        self.text(tx + tw - 10, ty + 58, f"Sheet {self.no}/{self.total}", size=13,
                  anchor="end", weight="bold")

    def svg(self):
        self.frame()
        head = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" '
                f'viewBox="0 0 {W} {H}" font-family="{FONT}">\n'
                f'<title>GX Pro schematic – sheet {self.no}: {escape(self.title)}</title>\n'
                '<defs><marker id="arrow" viewBox="0 0 10 10" refX="9" refY="5" '
                'markerWidth="7" markerHeight="7" orient="auto-start-reverse">'
                f'<path d="M0,0 L10,5 L0,10 z" fill="{C_WIRE}"/></marker></defs>\n'
                f'<rect width="{W}" height="{H}" fill="#ffffff"/>\n')
        return head + "\n".join(self.el) + "\n</svg>\n"




# =====================================================================================
# Sheet 1 – system / interconnect
# =====================================================================================
def sheet1():
    s = Sheet(1, 3, "Sheet 1/3 — System overview and board interconnect",
              "Four boards in one case. Photos: main_board_assembled.jpg, display_board_rear.jpg, "
              "gxext_board_*.jpg, amp_board_*.jpg.")

    # Display board -------------------------------------------------------------
    s.board(40, 100, 420, 600, "DISPLAY BOARD", "front panel, mounted in the lid → sheet 2")
    s.block(64, 160, 190, 96, "MCU  [FW]", ["STM32G070RBT6", "LQFP-64, 8 MHz xtal", "64 MHz PLL"])
    s.block(276, 160, 164, 96, "LCD  [FW]", ["240×320 TFT", "ST7789-type", "18-pin FPC"])
    s.block(276, 276, 164, 70, "SPI NOR  [FW]", ["SOIC-8, 25-series", "≥512 KB"])
    s.block(64, 276, 190, 70, "8 KEYS  [PCB]", ["6×6 mm THT tact", "active low"])
    s.block(64, 366, 190, 56, "3.3 V LDO  [PCB]", ["J1, SOT-223"])
    s.block(276, 366, 164, 56, "BACKLIGHT", ["T2 SOT-23, PWM"])
    s.wire([(254, 208), (276, 208)])
    s.wire([(254, 240), (266, 240), (266, 311), (276, 311)])
    s.wire([(159, 256), (159, 276)])
    s.block(64, 452, 190, 88, "J  5-pin JST  [PCB]",
            ["silk: GND 5V Rx Tx FSEL", "no cable fitted", "USART3 factory port? [I]"])
    s.block(64, 556, 190, 70, "J  3-pin  [PCB]", ["silk: U4Tx U4Rx GND", "unpopulated"])
    s.block(276, 452, 164, 174, "CABLE CONNECTORS",
            ["J 2-pin  ?", "J 4-pin", "J 6-pin", "", "10 signal wires [I]:", "5V, GND,",
             "USART1 → gen A", "USART2 → gen B", "PB1 in, PB2 out"])

    # Main board ------------------------------------------------------------------
    s.board(600, 100, 620, 820, "MAIN BOARD", "P/N label 2622232388, silk 6746202 → sheet 3")
    s.block(624, 160, 260, 120, "POWER  [PCB]",
            ["DC jack, 270 µF/16 V input", "4 × SMPS (SO-8 + 10 µH)", "LDO 3.3 V (DPAK)",
             "LDO ?.5 V (SOT-223)", "rails ±5 V, ±12 V, 3.3 V", "+ one LED per rail"])
    s.block(904, 160, 290, 120, "USB  [PCB]",
            ["USB-B receptacle", "D602 ESD (SOT-23-6)", "USB-UART bridge (QFN-24)",
             "2 × SO-8 (function ?)"])
    s.block(624, 310, 260, 104, "MCU  [PCB]",
            ["LQFP-48, part ?", "own 3225 crystal", "1×5 header (SWD?) DNP"])
    s.block(624, 444, 260, 124, "FPGA? U201  [PCB]",
            ["BGA ≈17×17 mm", "SO-8 config flash", "3225 oscillator",
             "'Done' LED + 2 buttons"])
    s.block(904, 310, 290, 104, "GENERATOR A (3xx)",
            ["resistor-ladder DAC bank A [I]", "≈7 op-amp stages (SO-8)", "→ OUT A1, A2"])
    s.block(904, 444, 290, 104, "GENERATOR B (4xx)",
            ["resistor-ladder DAC bank B [I]", "≈9 op-amp stages (SO-8)", "→ OUT B1, B2"])
    s.block(624, 600, 570, 70, "DNP  [PCB]",
            ["4 × SO-8 PowerPAD amp footprints (R343–R350, R443–R450): on-board",
             "alternative to the amp board [I]"], fill="#f3f3f3")
    s.block(624, 690, 570, 104, "OUTPUT JACKS  [PCB]",
            ["4 × threaded jacks on the rear bracket",
             "fed from the amp board through the 2×5 sockets [I]",
             "A1, A2: clamps D301, D302 (SOT-23)", "B1, B2: clamps D401, D402 (SOT-23)"])
    # Cables (drawn after the main board so they stay on top) -------------------------------------------------------------------
    for lbl, y in (("2-wire  ?", 480), ("4-wire", 520), ("6-wire", 560)):
        s.wire([(440, y), (600, y)], sw=2.4, color="#777")
        s.wire([(600, y), (612, y)], dashed=True)
        s.text(470, y - 6, lbl, size=11.5)
    s.wire([(612, 560), (612, 360), (624, 360)], dashed=True)
    s.text(470, 600, "white ribbon cables", size=11, color=C_NOTE)

    s.wire([(884, 506), (904, 506)])
    s.wire([(894, 506), (894, 362), (904, 362)])
    s.dot(894, 506)
    s.wire([(1049, 280), (1049, 294), (754, 294), (754, 310)], dashed=True)
    s.wire([(754, 414), (754, 444)], dashed=True)
    s.text(762, 434, "bus ?", size=11, color=C_NOTE)

    # External I/O ----------------------------------------------------------------
    s.text(624, 950, "DC IN (voltage ?, input cap 16 V)", size=12, weight="bold")
    s.wire([(700, 936), (700, 920)], arrow=True)
    s.text(904, 950, "USB to PC (host serial)", size=12, weight="bold")
    s.wire([(1000, 936), (1000, 920)], arrow=True)

    # Amp board --------------------------------------------------------------------
    s.board(1290, 100, 360, 330, "AMP BOARD", "plugs onto 2 × 2×5 sockets")
    s.block(1310, 160, 320, 150, "4 × POWER OP-AMP  [PCB]",
            ["under glued heatsinks, part ?", "one per output: A1, A2, B1, B2",
             "2 × 2×5 male header (one per gen.)", "4 screws with insulating washers",
             "±12 V via 'JF' 1206 parts on main"])
    s.block(1310, 322, 320, 90, "PER SOCKET  [I]",
            ["in:  2 × signal from generator", "out: 2 × amplified output",
             "supply: +12 V, −12 V, GND"], dashed=True)
    s.wire([(1194, 362), (1290, 362)], sw=2.4, color="#777")
    s.text(1206, 356, "2×5 (A)", size=11.5)
    s.wire([(1194, 496), (1250, 496), (1250, 400), (1290, 400)], sw=2.4, color="#777")
    s.text(1200, 514, "2×5 (B)", size=11.5)

    # GxExt -----------------------------------------------------------------------
    s.board(1290, 460, 360, 250, "GxExt BOARD", "silk GxExt26090599, 2 × 2×8 headers")
    s.block(1310, 516, 320, 176, "ANALOG EXTENSION  [PCB]",
            ["two mirrored sections, each:", "  2 × SO-8, 1 × MSOP-8, ~6 SOT-23",
             "shared: SO-8, TSSOP-20, 4 × SOT-23-5", "1×5 holes (unpopulated)",
             "function ? — signal conditioning [I]", "signals on the 2×8 headers ?"])
    s.wire([(1220, 640), (1290, 640)], sw=2.4, color="#777")
    s.text(1228, 634, "2 × 2×8", size=11.5)

    s.note(1290, 760, 360, [
        "Display board talks to 'board A' on USART1",
        "and 'board B' on USART2 (4 outputs A1, A2,",
        "B1, B2 in the firmware). On this unit both",
        "are served by one main board: 3xx parts =",
        "generator A, 4xx parts = generator B [I].",
    ], title="Firmware ↔ hardware")
    return s


# =====================================================================================
# Sheet 2 – display board, pin level
# =====================================================================================
def sheet2():
    s = Sheet(2, 3, "Sheet 2/3 — Display board (front panel)",
              "MCU pin functions verified from the display firmware (docs/spec/hardware.md §3). "
              "Parts and connectors from display_board_rear.jpg. Connector pin order is not traced.")

    left = ["PF0 OSC_IN", "PF1 OSC_OUT", None,
            "PA13 SWDIO", "PA14 SWCLK", "NRST", None,
            "PA9  USART1_TX", "PA10 USART1_RX", "PA2  USART2_TX", "PA3  USART2_RX",
            "PB1  in, pull-up", "PB2  out", None, None,
            "PB8  USART3_TX", "PB9  USART3_RX", None,
            "PB0  out", "PB7  out", None,
            "VDD", "VSS"]
    right = ["SPI2_SCK  PB13", "SPI2_MOSI PB15", "LCD_CS#   PA8", "LCD_DC    PC6",
             "LCD_RST#  PC7", "BL_PWM    PB12", None, None, None, None,
             "SPI1_SCK  PA5", "SPI1_MISO PA6", "SPI1_MOSI PA7", "FLASH_CS# PA1",
             "PB6", "PB10",
             "KEY  PD0", "KEY  PD1", "KEY  PB5", "KEY  PB4", "KEY  PD3", "KEY  PB3",
             "KEY  PD2", "KEY  PB11"]
    p, mh = s.ic(660, 120, 330, "U?  (no silk visible)", "STM32G070RBT6", left, right,
                 extra=["LQFP-64, Cortex-M0+ @ 64 MHz  [FW]"])

    # --- left side --------------------------------------------------------------
    xp, _ = s.ic(470, 120, 120, "X1  [PCB]", "8.000 MHz", right=["1", "2"],
                 extra=["HC-49/S, C2/C3"])
    for pin, mcu in (("1", "PF0 OSC_IN"), ("2", "PF1 OSC_OUT")):
        s.wire([xp[pin], p[mcu]])

    s.block(250, 228, 190, 80, "J  1×5 (unpopulated)", ["next to R5", "SWD? + 3V3 + GND [I]"],
            dashed=True)
    for nm in ("PA13 SWDIO", "PA14 SWCLK", "NRST"):
        x, y = p[nm]
        s.wire([(x, y), (440, y)], dashed=True)

    s.block(210, 318, 230, 164, "J 6-pin + J 4-pin + J 2-pin",
            ["ribbon cables to main board", "USART1 = board A", "USART2 = board B",
             "115200 8N1  [FW]", "+5V, GND; order ? [I]", "J 2-pin: function ?"])
    cab = ["PA9  USART1_TX", "PA10 USART1_RX", "PA2  USART2_TX", "PA3  USART2_RX",
           "PB1  in, pull-up", "PB2  out"]
    nets = ["GEN_A_RX", "GEN_A_TX", "GEN_B_RX", "GEN_B_TX", "PC_MODE", "HANDSHAKE"]
    for nm, net in zip(cab, nets):
        x, y = p[nm]
        s.wire([(x, y), (440, y)], dashed=True)
        s.netlabel(452, y, net, side="right", dashed=True)

    s.block(210, 498, 230, 90, "J 5-pin JST  [PCB]",
            ["silk: GND 5V Rx Tx FSEL", "no cable in this unit", "FSEL: ?"])
    for nm in ("PB8  USART3_TX", "PB9  USART3_RX"):
        x, y = p[nm]
        s.wire([(x, y), (440, y)], dashed=True)
    s.text(452, p["PB8  USART3_TX"][1] - 12, "factory / cal port [FW]", size=11, color=C_NOTE)

    x, y = p["PB0  out"]
    s.wire([(x, y), (560, y)])
    s.nc(560, y)
    s.text(546, y + 4, "held low, pull-down; purpose ? [FW]", size=11, anchor="end",
           color=C_NOTE)
    x, y = p["PB7  out"]
    s.wire([(x, y), (560, y)], dashed=True)
    s.text(546, y + 4, "soft PWM 625 Hz, 16 steps; load ? [FW]", size=11, anchor="end",
           color=C_NOTE)

    xv, yv = p["VDD"]
    xg, yg = p["VSS"]
    s.wire([(xv, yv), (560, yv)])
    s.netlabel(560, yv, "+3V3", side="left")
    s.wire([(xg, yg), (600, yg), (600, yg + 12)])
    s.gnd(600, yg + 12)
    s.block(250, 730, 230, 76, "J1  LDO 3.3 V  [PCB]",
            ["SOT-223 (AMS1117-type? [I])", "in: 5 V from the cable", "C8, C10"])
    s.wire([(480, 768), (520, 768)])
    s.netlabel(520, 768, "+3V3", side="right")
    s.wire([(250, 768), (200, 768)], dashed=True)
    s.netlabel(200, 768, "+5V", side="left", dashed=True)
    s.block(40, 830, 300, 56, "J 3-pin (unpopulated)  [PCB]",
            ["silk: U4Tx U4Rx GND — not used by FW"], fill="#f3f3f3")

    # --- right side ---------------------------------------------------------------
    lcd = ["SPI2_SCK  PB13", "SPI2_MOSI PB15", "LCD_CS#   PA8", "LCD_DC    PC6", "LCD_RST#  PC7"]
    lcd_pins = ["SCL", "SDA", "CS#", "D/C", "RESET#", "LED− (BL)"]
    lp, _ = s.ic(1250, 120, 200, "LCD module (FPC 18-pin)", "240×320 TFT",
                 left=lcd_pins, right=["VCC 3V3", "GND"], extra=["ST7789-type [FW]"])
    for nm, pin in zip(lcd, lcd_pins):
        s.wire([p[nm], lp[pin]])
    x, y = p["BL_PWM    PB12"]
    s.wire([(x, y), (1080, y)], dashed=True)
    s.block(1080, y - 20, 110, 40, "T2 SOT-23", ["R7 R9 R10"], dashed=True, size=11,
            title_size=12)
    s.wire([(1190, y), lp["LED− (BL)"]], dashed=True)
    s.text(1030, y + 42, "backlight PWM 625 Hz [FW]", size=11, color=C_NOTE)
    s.lines(1462, 250, ["SPI2 mode 3, 32 MHz", "write-only, no MISO", "FPC pin order ?",
                        "init variant: flash", "0x08003C00 [FW]"], size=11, color=C_NOTE)

    fl = ["SPI1_SCK  PA5", "SPI1_MISO PA6", "SPI1_MOSI PA7", "FLASH_CS# PA1"]
    fl_pins = ["CLK", "DO", "DI", "CS#"]
    fp, _ = s.ic(1250, p[fl[0]][1] - 58, 200, "U?  SOIC-8", "SPI NOR flash",
                 left=fl_pins, right=["VCC 3V3", "GND"], extra=["25-series cmds [FW]"])
    for nm, pin in zip(fl, fl_pins):
        s.wire([p[nm], fp[pin]])
    s.lines(1462, 490, ["SPI1 mode 3, 32 MHz", "≥512 KB used [FW]", "no JEDEC-ID read",
                        "decoupling C7"], size=11, color=C_NOTE)

    for pins in (lp, fp):
        x, y = pins["VCC 3V3"]
        s.netlabel(x, y, "+3V3", side="right")
        x, y = pins["GND"]
        s.wire([(x, y), (x + 10, y), (x + 10, y + 6)])
        s.gnd(x + 10, y + 6)

    for nm in ("PB6", "PB10"):
        x, y = p[nm]
        s.nc(x, y)
        s.text(x + 14, y + 4, "spare input, unread", size=11, color=C_NOTE)

    keys = [("KEY  PD0", "code 1"), ("KEY  PD1", "code 2"), ("KEY  PB5", "code 3"),
            ("KEY  PB4", "code 4"), ("KEY  PD3", "code 5"), ("KEY  PB3", "code 6"),
            ("KEY  PD2", "code 7"), ("KEY  PB11", "code 9, hold = power off")]
    bus_x = 1110
    for i, (nm, code) in enumerate(keys):
        x, y = p[nm]
        s.wire([(x, y), (1040, y)])
        s.switch(1040, y)
        s.wire([(1080, y), (bus_x, y)])
        s.text(bus_x + 14, y + 4, f"SW{i + 1}  {code}", size=11)
    ky0, ky1 = p[keys[0][0]][1], p[keys[-1][0]][1]
    for nm, _ in keys[1:]:
        s.dot(bus_x, p[nm][1])
    s.wire([(bus_x, ky0), (bus_x, ky1 + 20)])
    s.gnd(bus_x, ky1 + 20)
    s.note(1360, ky0 - 16, 290, [
        "active low, internal pull-ups [FW]",
        "8 × 6×6 mm THT tact switches [PCB]",
        "PB3 + PD2 → code 8:",
        "   held at boot = service/cal mode",
        "PB11 + PB5 → code 10",
        "SW numbers assigned here",
    ], title="Keys")

    s.note(660, 120 + mh + 30, 560, [
        "HSE 8 MHz → PLL 64 MHz; SysTick 1 kHz; TIM3 10 kHz (soft PWMs)",
        "UCPD dead-battery strobes set (PA8, PB15, PD0, PD2 no pull-down)",
        "PB1 low = display drives the generators; high = PC/remote mode",
        "PB2 high while the display is in control; toggles ~40–50 kHz",
        "     after long-press power-off (purpose ?)",
        "USART3 = factory/calibration port (':a'..':l', replies 'Z')",
    ], title="Firmware facts [FW]")
    return s


# =====================================================================================
# Sheet 3 – main board and daughterboards
# =====================================================================================
def sheet3():
    s = Sheet(3, 3, "Sheet 3/3 — Main board, GxExt and amp board",
              "Block level. Reference-designator ranges from the silkscreen: 1xx power, 2xx FPGA, "
              "3xx generator A, 4xx generator B, 5xx MCU, 6xx USB. "
              "Photos: main_board_top.jpg, main_board_closeup_*.jpg.")

    # ---- power --------------------------------------------------------------------
    s.board(30, 90, 560, 420, "POWER  (R101–R134)", "top-left corner of the main board")
    s.block(50, 150, 120, 60, "DC JACK", ["barrel, V ?"])
    s.block(200, 150, 110, 60, "C 270 µF", ["16 V polymer"])
    s.wire([(170, 180), (200, 180)])
    s.wire([(310, 180), (340, 180)])
    s.netlabel(340, 180, "VIN", side="right")
    s.block(410, 150, 160, 90, "HEATSINKED PART",
            ["under clip-on", "heatsink, R101–R105", "lin. reg or FET ?"], dashed=True)
    s.block(50, 250, 250, 118, "SMPS ×4  [PCB]",
            ["SO-8 / MSOP-8 controllers", "10 µH '100' inductors", "(2 large, 4 small)",
             "'JG' 1206 parts on 5 V"])
    s.wire([(180, 210), (180, 250)])
    for i, r in enumerate(("+5V", "−5V", "+12V", "−12V")):
        y = 276 + i * 24
        s.wire([(300, y), (330, y)], dashed=True)
        s.netlabel(330, y, r, side="right", dashed=True)
    s.lines(420, 290, ["which SMPS makes", "which rail ? [I]"], size=11, color=C_NOTE)
    s.block(50, 392, 200, 64, "LDO 3.3 V  [PCB]", ["DPAK, silk '3.3V'", "from +5V ? → +3V3"])
    s.block(270, 392, 300, 64, "LDO ?.5 V  [PCB]",
            ["SOT-223, silk '?.5V' (digit hidden)", "2.5 V FPGA aux ? [I]"])
    s.text(50, 490, "Rail LEDs with SOT-23 drivers: +5V −5V +12V −12V 3.3V [PCB]", size=11,
           color=C_NOTE)

    # ---- USB -------------------------------------------------------------------------
    s.board(30, 540, 560, 250, "USB  (6xx)", "top edge, next to the DC jack")
    s.block(50, 600, 100, 70, "USB-B", ["receptacle"])
    s.block(170, 600, 110, 70, "D602", ["ESD, SOT-23-6", "R52, R53"])
    s.block(300, 600, 130, 70, "USB-UART", ["QFN-24, part ?", "C603–C605"])
    s.block(450, 600, 120, 70, "2 × SO-8", ["function ?", "isolator? [I]"], dashed=True)
    for a, b in ((150, 170), (280, 300), (430, 450)):
        s.wire([(a, 635), (b, 635)])
    s.block(170, 690, 180, 70, "U606 SOT-23", ["LDO / reset ?", "R605, R606"], dashed=True)
    s.block(370, 690, 200, 70, "SOP-4 (2+2 leads)", ["opto-coupler? [I]"], dashed=True)

    # ---- FPGA / MCU -----------------------------------------------------------------
    fp, _ = s.ic(660, 150, 270, "U201  BGA ≈17×17 mm", "FPGA? [I]",
                 right=["DAC bank A", None, "DAC bank B", None, "MCU bus ?"], top=100,
                 extra=["'Done' LED silk → FPGA", "SO-8 cfg flash, 3225 osc",
                        "2 buttons: PROG/RST ?"])
    mp, mh = s.ic(660, 560, 270, "U5xx?  LQFP-48", "MCU (part ?)",
                  left=["UART A → display", "UART B → display", "PB1/PB2 link ?", "USB serial"],
                  right=["FPGA bus ?", None, "SWD 1×5 DNP"], extra=["R501–R506, 3225 crystal"],
                  dashed=True)
    s.wire([(570, 635), (600, 635), (600, mp["USB serial"][1]), mp["USB serial"]], dashed=True)
    s.lines(660, 560 + mh + 22, ["UART A/B + PB1/PB2 →", "display cables (sheet 2)"],
            size=11, color=C_NOTE)
    s.wire([mp["FPGA bus ?"], (968, mp["FPGA bus ?"][1]), (968, fp["MCU bus ?"][1]),
            fp["MCU bus ?"]], dashed=True)

    # ---- generators -------------------------------------------------------------------
    def generator(y, tag, rng, clamps, dnp, bulk, route_x):
        s.board(990, y, 660, 330, f"GENERATOR {tag}  ({rng})",
                f"outputs {tag}1, {tag}2; display USART{'1' if tag == 'A' else '2'} [I]")
        s.block(1010, y + 60, 170, 116, f"DAC bank {tag}  [PCB]",
                ["2 rows × ~24 0402 R", "+ 4 C per row", "R-2R ladder ×2 ? [I]",
                 "~12 bit each ?"])
        s.block(1210, y + 60, 210, 116, "ANALOG CHAIN  [I]",
                ["≈7–9 × SO-8/MSOP-8", "op-amps: filter,", "gain, offset ?", bulk], dashed=True)
        s.block(1450, y + 60, 180, 96, "J 2×5 socket", ["→ amp board", "±12 V via 'JF'",
                                                          "1206 parts"])
        for yy in (y + 100, y + 140):
            s.wire([(1180, yy), (1210, yy)], dashed=True)
            s.wire([(1420, yy), (1450, yy)], dashed=True)
        s.block(1010, y + 196, 280, 56, "DNP  2 × SO-8 PowerPAD", [f"{dnp} (unfitted amps)"],
                fill="#f3f3f3")
        s.wire([(1510, y + 156), (1510, y + 214), (1310, y + 214), (1310, y + 262),
                (1320, y + 262)])
        s.wire([(1570, y + 156), (1570, y + 240), (1450, y + 240), (1450, y + 262),
                (1470, y + 262)])
        s.jack(1320, y + 262, f"OUT {tag}1")
        s.jack(1470, y + 262, f"OUT {tag}2")
        s.text(1010, y + 300, f"outputs come back from the amp board; clamps {clamps} (SOT-23)",
               size=11, color=C_NOTE)
        px, py = fp[f"DAC bank {tag}"]
        s.wire([(px, py), (route_x, py), (route_x, y + 118), (1010, y + 118)])

    generator(90, "A", "R3xx", "D301, D302", "R343–R350", "±12 V bulk 2 × 100 µF", 975)
    generator(440, "B", "R4xx", "D401, D402", "R443–R450", "", 980)

    # ---- daughterboards ------------------------------------------------------------------
    s.board(30, 820, 560, 180, "GxExt  (daughterboard)", "2 × 2×8 male → main-board sockets")
    s.lines(46, 882, [
        "two mirrored sections (per generator? [I]), each:",
        "   2 × SO-8, 1 × MSOP-8, ~6 SOT-23 (transistors / SOT-23-5)",
        "shared: SO-8, TSSOP-20 (DAC / pot / mux ?), 4 × SOT-23-5",
        "1×5 holes, unpopulated;  function: signal conditioning ? [I]",
        "sockets: one mid-board near gen A, one at the bottom edge",
    ], size=11.5)
    s.board(620, 820, 350, 180, "AMP BOARD", "2 × 2×5 male (A, B)")
    s.lines(636, 882, [
        "4 × power op-amp under heatsinks",
        "   (A1, A2, B1, B2; part ?)",
        "heatsinks screwed through the board",
        "   with red insulating washers",
        "the 4 DNP footprints on main = same job",
    ], size=11.5)
    s.board(990, 820, 660, 180, "UNPOPULATED HEADERS ON MAIN  [PCB]", "")
    s.lines(1006, 870, [
        "1×5 near MCU / R501  → SWD? [I]",
        "1×6 near R309/R310   → ?",
        "1×7 next to the rail LEDs → ?",
        "1×8 on the bottom edge → ?",
        "2 tactile buttons + 'Done' LED next to U201 (FPGA prog/reset?)",
        "Pad-level tracing needs the bottom side: main_board_bottom.jpg",
    ], size=11.5)
    return s


def main():
    out = Path(__file__).resolve().parent
    for name, fn in (("sheet1_system.svg", sheet1), ("sheet2_display_board.svg", sheet2),
                     ("sheet3_main_board.svg", sheet3)):
        (out / name).write_text(fn().svg(), encoding="utf-8")
        print("wrote", out / name)


if __name__ == "__main__":
    main()
