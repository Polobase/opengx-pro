# Display subsystem: LCD driver, graphics primitives, fonts and images

Firmware: GX Pro display firmware v7789 (`original/GxDisplayFW.bin`, decoded with `python3 tools/gxfw.py decode`; the image is loaded at `0x08004000`).
Scope: LCD controller and its init, the SPI2 transfer layer, drawing primitives, text rendering, and every
font/bitmap in the constant data. Names are in `tools/sim/names/display.tsv`.
The PNG names below are the asset renders and screen mock-ups made during the analysis; they show vendor graphics and are not part of this repository.

Tags: **[V]** = VERIFIED (read from code/data at the cited address), **[I]** = INFERRED (reasoned, not proven).

---

## 1. Summary

| Item | Value |
|---|---|
| Controller | **Sitronix ST7789** (V/V2 class) [I, from the command set: B2/B7/BB/C0/C2/C3/C4/C6/D0/E0/E1 are ST7789 extended commands, 0x0800A188] |
| Resolution | 240 × 320 GRAM, MADCTL = 0x00 (native portrait) [V 0x0800A1B8] |
| Pixel format | RGB565, COLMOD = 0x05, sent big-endian (high byte first) [V 0x0800A1C4, 0x0800D8D8] |
| Bus | SPI2, 4-wire write-only (SCK PB13, MOSI PB15), 8-bit frames, polled, **no DMA** [V 0x0800B0A8] |
| Control pins | CS = PA8 (active low), DC = PC6 (0 = command, 1 = data), RST = PC7 (active low) [V] |
| Backlight | PB12, software PWM in TIM3 ISR, 16 steps, duty = `backlight_duty` (0x2000001B) [V 0x0800C604] |
| UI orientation | Landscape 320 × 240, done in software: every bitmap is stored pre-rotated. **X_logical = 319 − y_phys, Y_logical = x_phys** [V, checked by rendering text] |
| Frame buffer | none; everything is streamed directly into GRAM with opaque background pixels [V] |

---

## 2. Pins and peripherals

GPIO init `0x0800AD9C` (MX_GPIO_Init; see the hardware spec). LL_GPIO_SetOutputPin = `0x0800A8D6` (writes BSRR),
LL_GPIO_ResetOutputPin = `0x0800A8D2` (writes BRR), LL_GPIO_Init = `0x0800A7D8`.

| Signal | Pin | Config (LL_GPIO_InitTypeDef {Pin, Mode, Speed, OType, Pull}) | Level after init | Evidence |
|---|---|---|---|---|
| LCD CS | PA8 | output, speed 3 (very high), push-pull, no pull | high (inactive) | [V] 0x0800AD9C: `FUN_0800a8d6(GPIOA,0x100)`, init {0x100,1,3,0,0} |
| LCD DC | PC6 | output, speed 3, PP | low | [V] `FUN_0800a8d2(GPIOC,0x40)`, {0x40,1,3,0,0} |
| LCD RST | PC7 | output, speed 2 (high), PP | high | [V] `FUN_0800a8d6(GPIOC,0x80)`, {0x80,1,2,0,0} |
| Backlight | PB12 | output, speed 0, PP | low (off) | [V] `FUN_0800a8d2(GPIOB,0x1000)`, {0x1000,1,0,0,0}; PWM in 0x0800C604 |
| SPI2_SCK | PB13 | AF0, speed 3, PP, no pull | – | [V] 0x0800B0A8 {0x2000,2,3,0,0,AF0}; AF0 = SPI2_SCK on G070 [I, from datasheet] |
| SPI2_MOSI | PB15 | AF0, speed 3, PP | – | [V] 0x0800B0A8 {0x8000,2,3,0,0,AF0} [I AF mapping] |
| SPI2_MISO | – | not configured, so the LCD is never read (no ID read) | – | [V] |

### SPI2 configuration (`MX_SPI2_Init` 0x0800B0A8) [V]
`RCC_APBENR1 |= 0x4000` (SPI2EN), GPIOB clock on (`0x0800A8F8(2)`), then `LL_SPI_Init(SPI2, &s)` (0x0800AA78) with
`s = {TransferDirection=0 (full duplex), Mode=0x104 (master, SSI), DataWidth=0x700 (8 bit), ClockPolarity=2 (CPOL=1),
ClockPhase=1 (CPHA=1), NSS=0x200 (software), BaudRate=0 (fPCLK/2), BitOrder=0 (MSB first), CRCCalculation=0, CRCPoly=7}`,
then `LL_SPI_SetStandard(SPI2, Motorola)` (0x0800AAFC) and `LL_SPI_DisableNSSPulseMgt` (0x0800AA64).
`main` sets SPE later (`LL_SPI_Enable(SPI2)` at 0x0800E490), then calls `delay_loop(0x82355)` (0x0800E496) and `lcd_init` (0x0800E49A).
So the bus uses **SPI mode 3, MSB first, 8-bit**. The SCK is PCLK/2, which is **32 MHz** if PCLK = 64 MHz [I: RCC setup at 0x0800C564 is HSE, PLLN=16, PLLR=/2, AHB/APB /1, assuming an 8 MHz HSE].

---

## 3. Transfer layer

All transfers are polled 8-bit writes to `SPI2->DR` (`LL_SPI_TransmitData8` 0x0800AB08, a byte store to DR+0x0C) followed by
`while (LL_SPI_IsActiveFlag_BSY(SPI2));` (0x0800AAF0, SR bit 7). TXE is never checked. [V]

| Addr | Name | Exact sequence [V] |
|---|---|---|
| 0x0800D980 | `lcd_write_cmd(u8 c)` | PA8←0 (BRR 0x100); wait !BSY; PC6←0 (BRR 0x40); DR←c; wait !BSY; PA8←1 (BSRR 0x100) |
| 0x0800D938 | `lcd_write_data8(u8 d)` | PA8←0; wait !BSY; PC6←1 (BSRR 0x40); DR←d; wait !BSY; PA8←1 |
| 0x0800D8D8 | `lcd_write_data16(u16 v)` | PA8←0; wait !BSY; PC6←1; DR←v>>8; wait; DR←v&0xFF; wait; PA8←1. **This is the only "push pixel" call; CS toggles for every pixel.** |
| 0x0800C4EC | `lcd_set_window(xs, xe, ys, ye)` | cmd 0x2A, data16 xs, data16 xe, cmd 0x2B, data16 ys, data16 ye, cmd 0x2C (RAMWR). **The argument order is x-range then y-range.** |
| 0x080091AC | `lcd_fill_rect(x0, y0, x1, y1, color)` | `lcd_set_window(x0,x1,y0,y1)`; PA8←0; wait; PC6←1; then (x1−x0+1)·(y1−y0+1) times {DR←hi; wait; DR←lo; wait}; PA8←1. CS stays low for the whole fill. Draws nothing if x1<x0 or y1<y0, but the window/RAMWR are still sent. |

The controller auto-increments column first (x0→x1), then row (y0→y1), and wraps inside the window (MADCTL=0).
Every blit below relies on this: it sets a window and streams pixels. Many windows are taller than the data
(e.g. `ye = 0x13F`), and the stream just stops early.

Reimplementation note: identical pixels come out with any SPI/DMA method as long as the byte stream is the same.
A correct implementation should wait for TXE before each DR write and for BSY=0 before CS goes high. The original's
BSY-only polling works in practice. [I]

---

## 4. LCD controller init (`lcd_init` 0x0800A188)

Called once from `main` (0x0800E404) after the GPIO/SPI/TIM/USART init and `delay_loop(0x82355)`. Sequence [V]:

1. `PC7←0` (RST low), `delay_loop(0x682AA)`; `PC7←1`, `delay_loop(0x682AA)`. No software reset (0x01) is sent.
2. `PA8←0` (CS low; each write routine toggles CS again anyway).
3. Read the **u16 at 0x08003C00** (`lcd_panel_variant`, in the last 1 KB page of the bootloader area, `ldrh` at 0x0800A1AE)
   and choose one of two command lists:
   * **Variant A**: value `0xFF00` (0x0800A1B4) or `0xF000` (0x0800A4B2). The two branches are byte-identical.
   * **Variant B**: value `0xFFF0` (0x0800A32C) or anything else, including erased flash `0xFFFF`. The two branches are byte-identical.
4. `PA8←1`.

Nothing in this image writes 0x08003C00. It must come from the bootloader or factory programming (cross-reference: hardware map).

### Command lists (C = command byte, then its data bytes) [V]

| # | Cmd | Name (ST7789) | Variant A data | Variant B data |
|---|---|---|---|---|
| 1 | 0x36 | MADCTL | 00 | 00 |
| 2 | 0x3A | COLMOD | 05 (16 bpp) | 05 |
| 3 | 0xB2 | PORCTRL | 0C 0C 00 33 33 | 0C 0C 00 33 33 |
| 4 | 0xB7 | GCTRL | 56 | 35 |
| 5 | 0xBB | VCOMS | 15 | 1C |
| 6 | 0xC0 | LCMCTRL | 2C | 2C |
| 7 | 0xC2 | VDVVRHEN | 01 | 01 |
| 8 | 0xC3 | VRHS | 18 | 0B |
| 9 | 0xC4 | VDVS | – (not sent) | 20 |
| 10 | 0xC6 | FRCTRL2 | 0F | 0F |
| 11 | 0xD0 | PWCTRL1 | A4 A1 | A4 A1 |
| 12 | 0xE0 | PVGAMCTRL | F0 19 17 03 04 13 38 33 4E 36 13 13 25 36 | D0 00 03 09 13 1C 3A 55 48 18 12 0E 19 1E |
| 13 | 0xE1 | NVGAMCTRL | F0 16 18 05 05 01 38 32 4E 38 14 15 27 38 | D0 00 03 09 05 25 3A 55 50 3D 1C 1D 1D 1E **29** (15 bytes, see note) |
| 14 | 0x11 | SLPOUT | – then `delay_loop(0x271000)` | same |
| 15 | 0x21 | INVON | sent | **not sent** |
| 16 | 0x29 | DISPON | sent | sent |
| 17 | 0x2C | RAMWR | sent (no data) | sent |

Note on E1, variant B: the firmware sends **15** data bytes (`D0 00 03 09 05 25 3A 55 50 3D 1C 1D 1D 1E 29`).
ST7789 NVGAMCTRL takes 14, so the controller should ignore the last byte `0x29` [V byte count, I effect].
Replay it exactly as listed.

Replayable form (variant A): `C36 00 | C3A 05 | CB2 0C 0C 00 33 33 | CB7 56 | CBB 15 | CC0 2C | CC2 01 | CC3 18 | CC6 0F | CD0 A4 A1 | CE0 F0 19 17 03 04 13 38 33 4E 36 13 13 25 36 | CE1 F0 16 18 05 05 01 38 32 4E 38 14 15 27 38 | C11 | wait | C21 | C29 | C2C`.
Variant B: `C36 00 | C3A 05 | CB2 0C 0C 00 33 33 | CB7 35 | CBB 1C | CC0 2C | CC2 01 | CC3 0B | CC4 20 | CC6 0F | CD0 A4 A1 | CE0 D0 00 03 09 13 1C 3A 55 48 18 12 0E 19 1E | CE1 D0 00 03 09 05 25 3A 55 50 3D 1C 1D 1D 1E 29 | C11 | wait | C29 | C2C`.

No other LCD command appears anywhere in the firmware (`lcd_write_cmd` is called only from lcd_init and lcd_set_window) [V]:
no sleep-in or display-off at shutdown, no scrolling, no partial mode, no read-back.

### Delays [V counts, I times]
`delay_loop(n)` (0x08012518) runs `nop; do { n--; } while (n);`. That is about 4 cycles per iteration on the M0+
(SUBS 1 + CMP 1 + taken BNE 2, with the flash cache hitting). At SYSCLK 64 MHz this gives:

| n | ≈ time | where |
|---|---|---|
| 0x82355 (533 333) | 33 ms | main, before lcd_init |
| 0x682AA (426 666) | 27 ms | RST low, and after RST high |
| 0x271000 (2 560 000) | 160 ms | after SLPOUT (ST7789 needs ≥120 ms before the next sleep command, 5 ms before other commands) |

The counts equal 64 000 000 / {120, 150, 25}. The author apparently meant "SYSCLK/k cycles", so each wait is 4× longer than that intent [I].
A reimplementation should use at least these times.

### Backlight [V 0x0800C604, 0x0800E404, 0x0800E55E]
* TIM3 update IRQ (TIM3 PSC=0x3F, ARR=99, so 10 kHz at a 64 MHz timer clock [I], init at 0x0800B168) increments the u8 `DAT_20000008`.
  `PB12 = ((DAT_20000008 & 0xF) < backlight_duty) && !(flags_2000000E & 1)`. This is 16-step PWM at about 625 Hz [I].
  Active-high polarity is inferred: duty 0 is used as "off" while redrawing. [I]
* `backlight_duty` (0x2000001B) is 0 at reset (RW data), so the backlight is off during lcd_init and the first clear.
  `main` sets it to 8 (50 %) just before the boot splash. `main_loop` sets 0 → clears the screen → draws the shutdown screen → sets 8.
  Protocol message 0x40 sets 8 or 0 (0x0800F22C / 0x0800F9C4; values 0x12038B1 / 0x989681; cross-reference: protocol).
* The UI renderer 0x080065D8 also writes `GPIOB_BRR/BSRR = 0x1000` around redraws. The ISR overwrites PB12 within 100 µs, so these writes have almost no effect.
* No writer of `flags_2000000E` bit 0 (forced backlight off) was found (open question).

---

## 5. Coordinates, orientation, colours

**Physical** coordinates are the controller's: x = column 0..239 (CASET), y = row 0..319 (RASET).
The product is used in landscape. Every bitmap and font is stored so that it reads correctly after
**rotating the physical image 90° clockwise**:

```
X_logical (0..319, left→right) = 319 − y_phys
Y_logical (0..239, top→bottom) = x_phys
```

Verified by rendering the boot splash and the fonts (`screen_boot_splash.png`); the opposite rotation shows mirrored or upside-down text.
In a stored bitmap, one physical row (constant y) is one **logical column**. The MSB of the first byte is the **top** pixel (smallest x).
Consecutive rows (increasing y) run from the logical **right** to the **left**. Text therefore advances by *decreasing* y.

A reimplementation can keep MADCTL = 0 and apply the transform above, which gives identical GRAM contents.
Setting MADCTL to a landscape mode (MV=1) is possible but has not been verified. [I]

### Colour format and constants [V]
RGB565, sent MSB first. Every colour used by the drawing code:

| Value | RGB888 approx. | Use |
|---|---|---|
| 0x0000 | black | background, corner pixels of `lcd_fill_half_round_rect`, list text |
| 0xFFFF | white | text, panel bodies, circles |
| 0x2B12 | (41,97,148) steel blue | header bars, borders, selection bar, active text |
| 0x8430 | (132,134,132) grey | inactive panel bodies, inactive status icon background, inactive frequency |
| 0xF800 | red | red "2" in the logo; USB icon when not connected |
| 0xFFE0 | yellow | sync icon when set |
| 0x0016 | (0,0,181) dark blue | "G1"/"G2" badge text |

---

## 6. Drawing primitives (API)

All coordinates below are **physical**. All blits are opaque (bg and fg are both written). "bits" means MSB-first 1 bpp.
Parameters are masked to 16 bits (`& 0xFFFF`) as in the original.

| Addr | Signature | Semantics [V unless noted] |
|---|---|---|
| 0x080091AC | `lcd_fill_rect(x0,y0,x1,y1,c)` | Inclusive rectangle, see §3. `main` clears with (0,0,239,319,0). |
| 0x08008688 | `lcd_draw_pixel(x,y,c)` | window(x,x,y,y) + one pixel |
| 0x08008F8E | `lcd_draw_vline(x,y0,y1,c)` | window(x,x,y0,y1), y1−y0+1 pixels |
| 0x08008FBC | `lcd_draw_hline(x,y,len,c)` | window(x,x+len,y,y), **len** pixels, covering x..x+len−1 |
| 0x08009240 | `lcd_fill_circle(cx,cy,r,c)` | See the algorithm below. It is 2r px wide (x from cx−r to cx+r−1) and 2r+1 rows tall. |
| 0x08007A00 | `lcd_fill_half_round_rect(x,y,w,h,c,side)` | Filled block x..x+w−1 × y..y+h (**h+1 rows**). The corners on one x side are rounded (r≈8): side 0 rounds the low-x side (logical top), side 1 the high-x side (logical bottom). Corner pixels are written as **0x0000**, not left untouched. |
| 0x080086A4 | `lcd_draw_round_rect(x,y,w,h,bg,border,fill)` | Rounded rectangle on all 4 corners, x..x+w−1 × y..y+h. 2 px `border`, interior `fill`, pixels outside the corners painted `bg`. Has a one-pixel quirk (below). |
| 0x0800616C | `lcd_draw_char_ui24(ch,x,y,bg,fg) → w` | Linear search in font_ui24 for code `ch`. Not found: return 0, draw nothing. Found: window(x,x+23,y−w+1,y) and 3·w bytes of bits (1 = fg). Returns w. |
| 0x08006358 | `lcd_draw_string_ui24(str,x,y,bg,fg)` | For each char until NUL: `y -= lcd_draw_char_ui24(...)`. |
| 0x08006384 | `lcd_draw_uint5_ui24(v,x,y,bg,fg) → y` | Digits v/10000, (v%10000)/1000, …, v%10 as `'0'+d`, each advancing by the glyph width (9). Always 5 digits, leading zeros kept. Returns the final y. |
| 0x08005DA4 | `lcd_draw_char_freq24(ch,x,y,bg,fg) → w` | Same as the ui24 version but uses font_freq24 (18 glyphs). |
| 0x08005BDC | `lcd_draw_frequency(u64 v,x,y,bg,fg)` | Frequency readout, see §7.2. |
| 0x08005AE4 | `lcd_draw_gnum_label(code,x,y,bg,fg)` | code '1'/'2' → "G1"/"G2" bitmap. window(x,x+15,y,0x13F), 44 bytes. Unknown code: draws nothing (window already set). |
| 0x08005E44 | `lcd_draw_out_label(code,x,y,bg,fg)` | "Out1"/"Out2", window(x,x+23,y,0x13F), 132 bytes. **Quirk:** the index is looked up in the *G1/G2* code table (0x0801264F), not its own. There is no range check, so an unknown code uses index 2 and draws 132 bytes from 0x08014B00 (QR data). |
| 0x08006070 | `lcd_draw_status_icon(code,x,y,bg,fg)` | code ∈ {'c','r','p','s'}, window(x,x+47,y,0x118), 306 bytes (51 rows). Unknown code: nothing drawn. |
| 0x08006104 | `lcd_draw_usb_icon(x,y,c0,c1)` | window(x,x+15,y,0x13F), 66 bytes. **0-bits → c0**, 1-bits → c1. The symbol is the 0-bits. |
| 0x08005B74 | `lcd_draw_sync_icon(x,y,c0,c1)` | window(x,x+15,y,0x13F), 32 bytes, 0-bits → c0 (the symbol). |
| 0x0800DC90 | `marquee_render_name(idx)` | Renders program name idx into the RAM text bitmap, §7.3. |
| 0x08006450 | `marquee_draw(x,y,bg,fg)` | 24 × 230 px text field, §7.3. |
| 0x080054F4 | `ui_draw_boot_splash()` | §9 |
| 0x08005ED0 | `ui_draw_shutdown_screen()` | §9 |

### `lcd_fill_circle` (0x08009240), exact [V]
```
n   = (r*707)/1000 + 1;          lim = r*r + (r>>1)     // computed once from the input r
hline(cx-r, cy, 2r)
for i = 1 .. n:                   // i <= n (unsigned compare, 0x080092F8)
    if (i*i + r*r > lim):         // r = CURRENT (already decremented) radius, 0x0800927C..0x0800928A
        if (r > n):  hline(cx-i+1, cy+r, 2(i-1));  hline(cx-i+1, cy-r, 2(i-1))
        r = r - 1
    hline(cx-r, cy+i, 2r);  hline(cx-r, cy-i, 2r)
```
All values are masked to u16 as in the original.

### Rounded rectangles [V 0x08007A00, 0x080086A4]
Per-row corner insets for rows k = 0..7 from the edge: **outer = 8,6,4,3,2,2,1,1**.
For the bordered version the interior starts at **inner = –,–,6,5,4,4,3,3**: rows 0 and 1 are all border, and row k
has border pixels on outer[k] ≤ i < inner[k] and on the mirrored side.
Top rows are y+k and bottom rows are y+h−k. The straight part (rows y+8 … y+h−8) is drawn as `vline` at x, x+1, x+w−2, x+w−1
(border colour) plus `lcd_fill_rect(x+2, y+8, x+w−3, y+h−8, fill)`.
Quirk in `lcd_draw_round_rect`: on the bottom row k=7 (y+h−7) the left border is only 2 px (`i < 2`) while the right one is 3 px, so pixel (x+2, y+h−7) gets `fill`.
All corner and border rows are drawn pixel by pixel with `lcd_draw_pixel`, which is slow but gives the same result.

---

## 7. Text

### 7.1 Font format [V]
Glyph records are `{u8 code; [u8 width;] u8 bits[N]}`, looked up by linear search on `code` (a raw 8-bit character code, ASCII here).
Bits are physical rows, MSB first: 3 bytes per row for 24 px glyphs, 2 bytes for 16 px ones. For proportional fonts `width` = number of rows
(the logical advance). The unused tail of each 48-byte slot is zero. No compression, no kerning.

### 7.2 Frequency readout `lcd_draw_frequency` (0x08005BDC) [V]
Input: 64-bit value `v` (r0:r1). Decoding uses Keil runtime `__aeabi_uldivmod` (0x080041C4) and `__aeabi_lmul` (0x08004194):
```
q = (v / 10) * 10^(v % 10)          // last decimal digit of v is a power-of-ten exponent
buf[16] = 16 decimal digits of q (q mod 10^16), zero padded   // shown as buf[0..7] "." buf[8..15]
started = 0; skipped = 0
for i in 0..7:                      // integer part, font_freq24, step 11
    if buf[i]=='0' && !started && i!=7 && i even: draw '0' in (bg,bg) (blank); y -= 11
    elif buf[i]=='0' && !started && i!=7 (odd):  skipped++          // no draw, no advance
    else: draw buf[i] (bg,fg); y -= 11; started = 1
draw '.' ; y -= 6
for i in 8..15: draw buf[i]; y -= 11
draw ' ' ; y -= 3 ;  draw 'H' ; y -= 10 ;  draw 'z' ; y -= 8
repeat skipped times: draw '0' in (bg,bg); y -= 11              // trailing blanks
```
The effect is a centred string of the form `IIII.FFFFFFFF Hz` with all 8 fractional digits, value = q / 10^8 Hz.
Half of the leading zeros become blanks in front and half become blanks at the end.
Example: v = 3750007 → q = 375000·10^7 → "37500.00000000 Hz" (see `screen_mock_state3_example.png`).
The same mantissa·10+exponent format appears in the generator command strings in flash (e.g. `":w24=3750007,"` at 0x080118E8), so it is presumably the protocol's frequency unit (cross-reference: protocol).
Callers: 0x080065D8 / 0x0800773E (x = 0x41/0xAF, y = 0x122 in states 3/4, x = 0x3A/0x55/0xA8/0xC3, y = 0xF5 in state 0x10).

### 7.3 Program-name marquee [V 0x0800DC90, 0x08006450, 0x0800C604]
* Names: RAM table `0x200001F8`, 32 × 60 bytes, loaded from external SPI flash offset 0x5000 (0x780 bytes) by 0x0800E29C, with 0xFF bytes turned into 0 (cross-reference: hardware/app).
* `marquee_render_name(idx)`: sets `marquee_width_px` (0x20001C88) to 0. For char index 58 down to 0 of name idx it skips 0 and unknown codes;
  otherwise it appends the glyph's `3·w` bytes from font_ui24 to `marquee_bitmap` (0x20001C8A) and adds w to the width. Then it zeroes
  the rest of the buffer up to 0x20002458 (1998 bytes = 666 px). The text is stored **last character first**, so buffer row 0 is the
  logical right end of the text. Char index 59 is never rendered. **Overflow:** there is no length check, so text wider than 666 px writes past 0x20002458 into the
  protocol buffer at 0x20002458 (possible only with very wide names).
* `marquee_draw(x,y,bg,fg)`: window(x, x+23, y−229, y), 690 bytes (230 logical px). With W = `marquee_width_px`, pos = `marquee_scroll_pos` (0x20000018):
  * W ≤ 230: first (230−W) rows of bg (logical right padding), then buffer rows 0..W−1, so the text is left-aligned.
  * W > 230 and pos < 100: buffer rows W−230 … W−1 (the start of the text, static).
  * W > 230 and pos ≥ 100: `off = (2W − pos − 100) mod (W+30)`; row j shows buffer row `(off + j) mod (W+30)`.
    Rows past W are zero, which leaves a 30 px gap between repeats.
* Scrolling: in the TIM3 ISR, when the u8 tick `DAT_20000008 == 100` (once every 256 ticks, about 25.6 ms [I]) and flag
  `0x2000000E & 4` (marquee active) is set, `pos++` while `pos < W + 130`; otherwise `pos = 100` and flag bit 0x20 is toggled
  (in state 0x10 this alternates which generator's name scrolls). One full cycle moves the text left by exactly W+30 px, one px per step, about 39 px/s [I].
  The UI redraws the marquee only when `pos != marquee_last_pos` (0x20000002).

### 7.4 Strings in the image [V]
Only three UI strings exist. Everything else is bitmaps or comes from the SPI-flash name table:

| Addr | Text | Drawn by / where |
|---|---|---|
| 0x080069EC | `Choose Your Treatment Program` | `lcd_draw_string_ui24` at (0x14 or 0x82, 0x110), bg 0x2B12, fg 0xFFFF, for a generator with no program |
| 0x08006A10 | `Program  G1` | state 2 (list) title at (0x0F, 200), bg 0, fg 0xFFFF |
| 0x08006A1C | `Program  G2` | same, when flag 0x2000000E & 8 is set |

The `":wNN=…,"` / `":r80=0,"` strings in the image are protocol commands, not displayed (cross-reference: protocol).

---

## 8. Font / image catalogue

All of these live in the constant region 0x0801264F–0x0801611F. They are laid out back to back with 1-byte code prefixes, and all sizes check out.
"phys W×H" is bytes-per-row·8 × rows as streamed. The PNGs are in **logical orientation** (rotated 90° CW), glyph sheets at ×3/×4 and images at ×2.
Used/unused sizes are [V] (loop counts in the drawing code). Contents were checked by viewing the PNGs.

### Fonts / glyph tables

| Addr (table) | Name | Records | Format | Content | PNG |
|---|---|---|---|---|---|
| 0x080135A5 | font_ui24 | 86 × 50 B | {code, width, 48 B}, phys 24 × width (logical 24 px high, 3–16 px wide) | Proportional sans font: `a–z A–Z 0–9 ( ) - , . ' : + / SP ^ & @ { } [ ] $ = ! # % ~ _` (code order). Widths: a9 b9 c8 d9 e8 f6 g9 h9 i3 j4 k8 l3 m13 n9 o9 p9 q9 r6 s8 t6 u8 v8 w14 x8 y10 z8 A10 B10 C10 D10 E9 F8 G10 H10 I3 J7 K10 L7 M12 N10 O10 P9 Q10 R10 S9 T9 U10 V10 W16 X10 Y9 Z8 digits 9, ( 5 ) 5 - 5 , 3 . 3 ' 3 : 3 + 9 / 5 SP 3 ^ 9 & 12 @ 13 { 6 } 6 [ 5 ] 5 $ 8 = 8 ! 3 # 8 % 11 ~ 9 _ 9 | `font_ui24.png` |
| 0x08014671 | font_freq24 | 18 × 50 B | same format | Larger digits for the frequency: `0–9` (w 11), `(` `)` `-` (5), SP (3), `.` `,` (6), `H` (10), `z` (8) | `font_freq24.png` |
| 0x0801264F | font_gnum_16x22 | 2 × 45 B | {code, 44 B}, phys 16 × 22 | code '1' = "G1", '2' = "G2" (generator badge text), logical 22 w × 16 h | `font_gnum_16x22.png` |
| 0x080149F5 | font_out_24x44 | 2 × 133 B | {code, 132 B}, phys 24 × 44 | '1' = "Out1", '2' = "Out2", logical 44 w × 24 h | `font_out_24x44.png` |
| 0x08012F7D | icons_status_48x51 | 4 × 307 B | {code, 306 B}, phys 48 × 51 | 'c' = check mark + "Ready", 'r' = play + "Running", 'p' = pause + "Paused", 's' = stop square + "Stopped". Logical 51 w × 48 h | `icons_status_48x51.png` |

### Images

| Addr (code byte / data) | Name | Bytes used / record | phys W×H | Colours when drawn | Content (logical) | PNG |
|---|---|---|---|---|---|---|
| 0x080129AF / 0x080129B0 | bmp_boot_ghost_logo | 0x460 / 0x461 | 112×80 | 1→0xFFFF, 0→0 | "Spooky" hand-lettered + cartoon ghost | `img_boot_ghost_logo.png` |
| 0x080126A9 / 0x080126AA | bmp_red2_small | 0x2A / 0x2B | 24×14 | 1→0xF800 | red "2" of the "Spooky2" logo (boot and shutdown) | `img_red2_small.png` |
| 0x080126D4 / 0x080126D5 | bmp_boot_welcome | 0x27C / 0x285 (8 zero bytes unused) | 24×212 | 1→0xFFFF | "Welcome to Spooky" | `img_boot_welcome.png` |
| 0x08012959 / 0x0801295A | bmp_red2_large | 0x42 / 0x56 (19 zero bytes unused) | 24×22 | 1→0xF800 | red "2" | `img_red2_large.png` |
| 0x08012E10 / 0x08012E11 | bmp_boot_tagline | 0x16C / 0x16D | 16×182 | 1→0xFFFF | "Professional • Friendly • Effective" | `img_boot_tagline.png` |
| 0x08013449 / 0x0801344A | bmp_usb_icon | 0x42 / 0x133 (240 zero bytes unused) | 16×33 | 0→c0 (0xF800 or 0xFFFF), 1→0 | USB trident symbol | `img_status_icon_16x33.png` |
| 0x0801357C / 0x0801357D | bmp_sync_icon | 0x20 / 0x29 (8 bytes 0xFF unused) | 16×16 | 0→0xFFE0 (or 0x2B12 = hidden), 1→0x2B12 | two circular "sync/repeat" arrows | `img_header_icon_16x16.png` |
| 0x08014B00 | bmp_bye_qrcode | u16[0x415], low byte used | 88×95 | 1→0x0000, 0→0xFFFF | QR code, decodes to **`http://spooky2.com`** (decoded with OpenCV) | `img_bye_qrcode.png` |
| 0x0801532A | bmp_bye_logo | u16[0xF0] | 32×60 | 1→0, 0→0xFFFF | "Spooky" lettering, smaller (shown white on black) | `img_bye_logo.png` |
| 0x0801550A | bmp_bye_text | u16[0x306] | 24×258 | 1→0, 0→0xFFFF | "Looking forward to seeing you again!" | `img_bye_text.png` |
| 0x08015B16 | bmp_bye_url | u16[0x124] | 16×146 | 1→0, 0→0xFFFF | "www.spooky2.com" | `img_bye_url.png` |
| 0x08015D5E | bmp_unused_waveforms | 960 (+2 pad) | 24×320 | – | **Not referenced by any code or pointer** [V: literal-pool and raw pointer scan]. A full-width strip of waveforms: an irregular/arbitrary wave, a damped or "H-bomb"-like wave, 2 sawtooth, 2 triangle and 2 sine periods. Probably left over from another product. | `img_unused_waveforms_24x320.png` |

The u16-array images (0x08014B00…0x08015D5D) have all high bytes = 0 [V]. The code reads them with `ldrh` and uses bits 7..0 (0x08005EEE),
so the storage is 2 bytes per 8 pixels. Their polarity is also inverted: 1 = black ink on a white field, while the ink is white on
screen because 0 → 0xFFFF. The QR code sits on a white quiet zone.

The 1-byte "code" in front of the single images (`'G' 'W' 'p' '2' '#'`) is not used by the drawing functions (they index at +1). It shows that
all assets were generated with one struct-per-bitmap tool.

---

## 9. Screens composed by display code (physical args; see the mock-ups)

### Boot splash `ui_draw_boot_splash` 0x080054F4 [V], called by `main` right after `lcd_fill_rect(0,0,239,319,0)` and `backlight_duty = 8`
| Blit window (x0..x1, y0..ye) | Data | 0/1 colour | Logical position |
|---|---|---|---|
| 0x1E..0x8D, 0x78..0x13F | 0x080129B0, 0x460 B | 0/0xFFFF | ghost logo X 120–199, Y 30–141 |
| 0x1E..0x35, 0x83..0x13F | 0x080126AA, 0x2A B | 0/0xF800 | red "2" X 175–188, Y 30–53 |
| 0xAD..0xC4, 0x32..0x13F | 0x080126D5, 0x27C B | 0/0xFFFF | "Welcome to Spooky" X 58–269, Y 173–196 |
| 0xAE..0xC5, 0x1E..0x13F | 0x0801295A, 0x42 B | 0/0xF800 | red "2" X 268–289, Y 174–197 |
| 200..0xD7, 0x3C..0x13F | 0x08012E11, 0x16C B | 0/0xFFFF | tagline X 78–259, Y 200–215 |

Result: `screen_boot_splash.png`.

### Shutdown screen `ui_draw_shutdown_screen` 0x08005ED0 [V], called from `main_loop` (0x0800E55E) with backlight 0 → clear → draw → backlight 8, then about 4 s delay
| Window | Data | 0/1 colour | Logical position |
|---|---|---|---|
| 0x46..0x9D, 0x6E.. | 0x08014B00 u16×0x415 | 0xFFFF/0 | QR X 115–209, Y 70–157 |
| 0x1F..0x3E, 0x86.. | 0x0801532A u16×0xF0 | 0xFFFF/0 | "Spooky" X 126–185, Y 31–62 |
| 0x1F..0x36, 0x7D.. | 0x080126AA 0x2A B | 0/0xF800 | red "2" X 181–194, Y 31–54 |
| 0xA5..0xBC, 0x20.. | 0x0801550A u16×0x306 | 0xFFFF/0 | "Looking forward…" X 30–287, Y 165–188 |
| 0xBE..0xCD, 0x53.. | 0x08015B16 u16×0x124 | 0xFFFF/0 | "www.spooky2.com" X 91–236, Y 190–205 |

Result: `screen_shutdown.png`.

### Generator panels (UI state machine 0x080065D8: see the application spec; these helpers only draw) [V]
Panel geometry for G1 (G2 is the same with x+0x6E): header = `lcd_fill_half_round_rect(0x14,0x14,0x19,0x118,0x2B12,0)` (logical top 20..44, X 19..299),
body = `lcd_fill_half_round_rect(0x2D,0x14,0x46,0x118,0x8430,1)` (grey, normal) or `lcd_draw_round_rect(0x14,0x14,0x5F,0x118,0,0x2B12,0xFFFF)`
(white with blue border, "active"), badge = `lcd_fill_circle(0x20,0x126,0xF,0xFFFF)` (active: first a blue ring `r=0x11`, colour 0x2B12) +
`lcd_draw_gnum_label('1', 0x18, 0x11C, 0xFFFF, 0x0016)`.

| Fn | State | G1 | G2 | label coords (G1 / G2) |
|---|---|---|---|---|
| 0x080056B8 ui_draw_panels_idle | 1 | normal | normal | (0x18,0x11C) / (0x87,0x11C) |
| 0x08005A28 ui_draw_panels_g1_active | 3 | active | normal | (0x18,0x11C) / (0x87,0x11C) |
| 0x0800596C ui_draw_panels_g2_active | 4 | normal | active | (0x18,0x11B) / (0x86,0x11B) |
| 0x0800B498 ui_draw_panels_both_active | 0x10 | active | active | (0x18,0x11C) / (0x86,0x11B) |

The 1 px differences between label positions are in the original. Other elements drawn by 0x080065D8:
USB icon at (4,5), logical top-right X 282–314 / Y 4–19. It is red 0xF800 or white 0xFFFF depending on flag 0x2000000E & 0x4000, which protocol messages set and clear in 0x0800F22C/0x0800F9C4 (values 0x97B1963 → white, 0x392A2162 → red). Presumably this is the PC/USB link state [I]. State 1 erases the icon with colours (0,0).
Sync icon at (0x19,0x19)/(0x87,0x19) (yellow when flag 0x100/0x200 is set, otherwise invisible 0x2B12/0x2B12).
Status icon at (0x37 / 0xA5, 0x1E). Frequency at (0x41 / 0xAF, 0x122). Name marquee at (0x14 / 0x82, 0x110).
In state 0x10 there are "Out1/Out2" labels at x = 0x3A/0x55/0xA8/0xC3, y = 0xFA, and four frequencies at y = 0xF5.
Mock-ups: `screen_mock_idle.png` (exact, state 1) and `screen_mock_state3_example.png` (state 3 with **example** name, status and frequency).

### Program list (state 2) [V]
`lcd_draw_round_rect(0x28,0x14,0xAF,0x118,0,0xFFFF,0xFFFF)`, title string at (0x0F,200).
`ui_draw_list_highlight(new,old)` (0x08005760, Keil `__ARM_common_switch8` 0x0800D9C8): row 1 = `lcd_fill_half_round_rect(0x28,0x14,0x19,0x118,c,0)`,
rows 2..6 = `lcd_fill_rect(0x28+25(r−1), 0x14, 0x28+25r−1, 0x12D, c)`, row 7 = `lcd_fill_half_round_rect(0xBE,0x14,0x19,0x118,c,1)`.
New row in 0x2B12, old row in 0xFFFF. **Quirk:** when new == old, row 1 uses h = **0x50** instead of 0x118 (0x0800577E).
`ui_draw_program_list` (0x0800620C): for i = 0..6, n = first + i at x = 0x28+25i: tens digit at y=0x122, ones digit at 0x119, '.' at 0x110
(fixed positions), then `marquee_render_name(n)` + `marquee_draw(x,0x109,…)`. Selected row colours are (bg 0x2B12, fg 0xFFFF), others (0xFFFF, 0).
Mock-up with **example names**: `screen_mock_list_example.png`.

### Service / parameter screens
`ui_draw_param_table` (0x0800DED4, shown when `FUN_0800e0f8()==8` at boot): 29 × `lcd_draw_uint5_ui24` of the u16 table at 0x2000007E
(rows x=0x0A…0xD2, columns y=0x12A/0xDA/0x8A/0x3A, plus (0x46,0xB2)), white on black. The state-0xFF branch of 0x080065D8
(0x08006F4C–0x08007738, **not decompiled**, a switch8 on 0x2000007C) redraws the previously selected value normal and the new one
inverted (bg 0xFFFF, fg 0). Cross-reference: app logic.

---

## 10. Cross-references to other areas
* Hardware map: GPIO init 0x0800AD9C, RCC 0x0800C564, TIM3 init 0x0800B168 / ISR 0x0800C604 (the ISR also drives **PB7** as 16-step soft PWM with duty `DAT_2000000C` (=1 after init, 0x0800DE2C); not display-related as far as seen), config words 0x08003C00–0x08003F00.
* App logic: UI state machine 0x080065D8 (state byte 0x2000001C; last drawn state = byte at 0x20000000, decompiler label `RW_Data`; flags 0x2000000E), dynamic refresh 0x0800773E, main_loop 0x0800E55E.
* Protocol: backlight on/off message 0x40, USB-icon messages, frequency value format (mantissa·10 + exponent).
* Runtime helpers used here: `__aeabi_lmul` 0x08004194, `__aeabi_uldivmod` 0x080041C4, `__aeabi_uidivmod` 0x0800431A (quotient r0, remainder r1), `__aeabi_uidiv` 0x08004306, `__aeabi_memclr4` 0x08004302, `__ARM_common_switch8` 0x0800D9C8.

---

## 11. Open questions
1. **Which panel variant is fitted?** 0x08003C00 is written outside this image (bootloader/factory). Variant A (INVON, gamma F0…) suits IPS glass; variant B (no INVON, the default for erased flash) is a TN-style setting [I]. A reimplementation must read the same halfword.
2. Exact delay durations depend on SYSCLK and flash wait states/cache. The 4 cycles/iteration and 64 MHz above are [I]. HSE crystal frequency not confirmed.
3. Backlight polarity (PB12 high = on) is [I]. Nothing sets flag bit 0 of 0x2000000E (backlight force-off) in the decompiled code; the disassembly of the undecompiled ranges was not searched for it.
4. (resolved) `lcd_fill_circle` compares against the current, decremented radius (disassembly 0x0800927C).
5. E1 gamma in variant B has 15 parameters (one extra 0x29). Harmless on ST7789 [I], but it is sent.
6. `bmp_unused_waveforms` (0x08015D5E) is unreferenced. Other firmware (v7789 variants, or the main board) may use it.
7. Marquee buffer overflow for names wider than 666 px (writes into 0x20002458+). Should the reimplementation keep or clamp this? It is only reachable with long names made of very wide glyphs.
8. `lcd_draw_out_label` looks its index up in the G1/G2 table, and `ui_draw_list_highlight` uses h=0x50 when new==old. Both look unintended, but they are what the device draws.
9. The TIM3 clock (so the 10 kHz tick, 625 Hz backlight PWM and ~39 px/s marquee speed) depends on the RCC setup [I].

---

## 12. Corrections found during the reimplementation

* Program-list selection bar, rows 2–6: the disassembly at 0x080057BC fills to y = 300 (0x12C), not
  0x12D as §9 and the list mock-up `screen_mock_list_example.png` show (the mock differs by 25 px in
  that row). The reimplementation uses 300 [V].

