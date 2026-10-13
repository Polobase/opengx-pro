<!-- design note of 2026-09-23: UI engine, design system, screens and UX.
Superseded where docs/nx/fw2-design.md decides differently. -->

> **Scope (2026-09-24):** Drops 3 and 4 are cancelled (presets, protocols/Terrain, chains, manual mode, sweeps), and of Drop 2 only resume after a power cut remains. Their wireframes (W3, W8, W10–W14) were removed; the menu cells, tabs and sources that point to them are not built.

> **Note (Drop 1 as built):** Drop 1 implements a reduced version of this design to fit the
> 74,672-byte image (`docs/nx/fw2-design.md`, "Drop 1 as built"). It is English only and has no
> menu (DOWN opens Settings), options sheet, reset/confirm dialogs, licence page, ambient view,
> key guide, Diagnostics or key test, key-hint bar, "…" ellipsis or 22 px title font. The keys
> as built: `docs/nx/key-model.md`.

---

# nx (`APP=nx`) plan: UI engine, design system, screens and UX

## 0. Key decisions

| Topic | Decision |
|---|---|
| Rendering model | Immediate-mode drawing, repainted one band at a time and limited to dirty rectangles. There is no frame buffer. Every dirty rectangle is drawn in bands of up to 2,560 pixels into one of two RGB565 buffers (5 KB each), which DMA sends to the LCD while the next band is drawn. All layers (background, screen, scrim, sheet, toast, lock) are blended in the band buffer, so anti-aliasing is exact and nothing needs to be read back from the panel. |
| Orientation | Keep the variant A/B init lists byte-identical, then send **MADCTL = 0x60 (MV\|MX)** so the panel is addressed in landscape directly (logical X = column, Y = row). The emulator model predicts this matches today's rotation; confirm on the device in M0. |
| Bus | SPI2 permanently in **16-bit frames**. Commands go out as `0x00,cmd` (0x00 is the ST7789 NOP). Pixel data needs no byte swapping. DMA1 channel 1 through DMAMUX (SPI2_TX, request ID 19 per RM0454, to verify). A polled fallback is used until the emulator models DMA. |
| Hardware scroll | **Note:** ST7789 vertical scroll always runs along the 320 physical lines. MADCTL only changes how writes are addressed. In landscape it is therefore a **horizontal pan whichever MADCTL is used**. It suits full-screen horizontal "push" transitions (G1↔G2, entering/leaving pages) at the cost of one screen of pixels. It cannot scroll lists vertically, and it cannot keep a header or footer fixed. |
| Backlight | New driver on TIM17 using two update interrupts per period (high phase, low phase) at 1 kHz. 256 perceptual levels via a gamma table, fades, 100 % allowed. PB12 handling is removed from the TIM3 ISR. |
| Fonts | Default **Inter** (text) and **Inter Display** (numbers), SIL OFL 1.1. Alternative for A/B review in the gallery: Atkinson Hyperlegible Next. Pillow/FreeType with raqm at build time (raqm is available in the repo `.venv`), which allows OpenType `tnum`/`ss02`/`zero`. **4bpp alpha, nibble RLE**, chosen per glyph as raw or RLE. About 17–19 KB of fonts in total. |
| Icons | Material Symbols Rounded SVGs (Apache-2.0) plus custom SVGs (waveforms, brand mark), rasterised by the tool's own SVG-subset renderer at 8× supersampling. |
| Look | Dark theme by default, plus Light, High-contrast and Night palettes. G1 = cyan, G2 = amber (the pair colour-blind users tell apart most easily). Status is always icon + text, never colour alone. |
| Keys | G1/G2 = generator, or left/right. START = OK/play. STOP = back/stop. LOOP = cycle (loop toggle, next tab), hold = options. UP/DOWN = move/value. POWER short = screen off/on; POWER hold 0.8 s = off (unchanged). |
| Budget | UI ≈ 55–62 KB flash, ≈ 12.5 KB RAM. Hard caps are in §8, with cut lines and a Plan B (assets in SPI flash). |

---

## 1. Findings in the current code that shape the plan

- `src/lcd.c` holds the init lists (must stay), plus `lcd_draw_char_ui24`/`gfx_text` which `#include "assets.h"` (vendor font). bringup, probe and linktest use `gfx_text`, and every object depends on `$(BUILD)/assets.h` in the Makefile.
  - **Prerequisite:** move the classic text helpers into `src/lcd_text.c`, which only classic apps link.
  - Make the asset header dependency depend on `APP`, so `make APP=nx` never touches `original/GxDisplayFW.bin`.
- `keys_read()` returns one priority-coded key (only the G1+G2 and POWER+STOP chords exist). nx needs a raw 8-bit mask sampled every 10 ms (§5.1).
- The classic scan blocks for 13.3 ms per loop pass, and `link_wait_ok` blocks for up to about 40 ms. The UI depends on the core plan's non-blocking loop (§6.5).
- `factory.c` calls `lcd_fill_rect` and `ui_cal_draw_values/ui_cal_draw_value` directly, and `link.c:608` writes `g_backlight_duty` (w64).
  - nx must own SPI2 exclusively. These calls become hooks (`ui_cal_enter/exit`, `ui_cal_draw_value(idx)`, `nx_bl_host_override(on)`), provided in nx by `src/nx_ui_compat.c`.
- The emulator (`tools/sim`) has these gaps:
  - No DMA/DMAMUX: 0x40020000 is `Unmodelled`.
  - The SPI treats a 16-bit DR write as two bytes, low byte first (packing). A real DS=16 frame goes out **high byte first**.
  - The ST7789 model lacks VSCRDEF/VSCSAD, and MADCTL≠0 is flagged "unverified".
  - Timers only have an update interrupt (fine for the two-phase PWM).
  - There is no ADC.
  - Screenshots treat the backlight as on/off only.
- RAM today: `.bss` 11.2 KB. `g_ch` alone is 4,960 B, `g_names` 1,920 B, `g_name_bmp` 1,998 B (classic only, drop it in nx) and RX buffers 2.2 KB.
- Program headers in a real SPI-flash backup hold `hdr0` = 41…48 ("waveform"; meaning open). Waveform names in the UI therefore go through a mapping table with a "verified" flag. Unverified codes show "wave #41" and a "?" badge.
- Dwell time is ×1.01 s (10.1 ms ticks). Remaining times must come from the core's tick arithmetic, not from nominal seconds.

---

## 2. Graphics engine (`src/nx_*`)

### 2.1 Layers

```
screens / components  (nx_scr_*.c, nx_comp_*.c)   draw(gc) = pure function of screen state + VM
render scheduler      (nx_render.c)                dirty rects -> bands -> slices, frame snapshot, transitions
raster core           (nx_gfx.c, nx_font.c)        primitives drawn into the band buffer, clipped, blended, tokens -> RGB565
bus / panel           (nx_lcd.c)                   16-bit SPI2, DMA queue + ISR, window, scroll, sleep/idle, fallback
backlight             (nx_bl.c)                    TIM17 two-phase PWM, gamma, fades
```

### 2.2 Coordinates and orientation

- Logical space: X 0..319 left→right, Y 0..239 top→bottom, `int16_t`. Rectangles are half-open `{x0, y0, x1, y1}`.
- `nx_lcd_init()`:
  1. Call the unchanged `lcd_init()` (variant A/B lists, including INVON for A).
  2. Switch SPI2 to DS=16 (SPE off → DS → SPE on).
  3. Send `MADCTL 0x60`, `VSCRDEF 0,320,0`, `VSCSAD 0`.
- The window is set with `CASET x0,x1` / `RASET y0,y1` / `RAMWR`, and the stream is row-major, exactly like the band buffer.
- If the device shows 0x60 mirrored, use 0xA0 (MV\|MY). The value lives in one constant, checked by the M0 test pattern.
  Result on the unit (2026-09-24): 0x60 shows the picture turned 180°, so `NX_LCD_MADCTL` is 0xA0.
- Scroll offset: while a push transition is running, `nx_lcd` maps logical X to GRAM column `(X + s) mod 320`, splitting a window that wraps into two. The sign is decided by the M0 test.

### 2.3 Bus layer and DMA ping-pong

- **Frames:**
  - Command = one 16-bit frame `0x00cc` with DC low.
  - Parameters = 16-bit frames (x0, x1 are naturally 16 bit) with DC high.
  - Pixels = the band buffer as `uint16_t` RGB565, MSB first on the wire.
- **Queue** (ISR-driven, 4 entries): `BAND(buf, rect)`, `FILL(rect, color)` and `SCROLL(s)`.
  - `FILL` uses DMA with MINC=0 on a single halfword: clearing large solid areas costs no CPU.
- **DMA1 ch1 TC ISR** (≈6–8 µs):
  1. Wait until FTLVL=0 and BSY=0 (≤ 1 µs).
  2. Mark the buffer FREE.
  3. If the next entry is ready: DC low, send the window commands (≈ 3 µs), DC high, start DMA.
- **Buffer states:** FREE → DRAWING → READY → SENDING → FREE. CS stays low for the whole frame.
- **Polled fallback** (`NX_LCD_DMA=0`, used by the emulator until DMA is modelled): same queue, sent by `nx_render_step` with a TXE-polled loop. It does one band per call (≤ 1.3 ms of SPI).
- **Rejected option:** RGB444 (COLMOD 0x03). It is 33 % faster (1.5 B/px), but causes visible banding in dark gradients and anti-aliased edges, and the 3-byte packing is awkward.

### 2.4 Rendering model: immediate mode, one clipped band at a time

- One frame:
  1. `nx_ui_tick()` applies queued events and copies the core snapshot into `vm_now`.
  2. Screens compare `vm_now` with `vm_drawn` and call `nx_inval()` for the regions that changed.
  3. The renderer processes the dirty list over several slices. After the frame, `vm_drawn = vm_now`.
  4. Events and snapshots are applied only at frame boundaries, so each frame is consistent. The longest frame is about 40 ms, which bounds input latency (typically < 15 ms).
- Band sizing: `h = min(rect_h, NX_BAND_PX / rect_w)` with `NX_BAND_PX = 2560`, i.e. 320×8, 100×25 or 28×60 (one digit cell in a single band).
- For each band, the gc clip is the band. The layer stack is drawn in z-order:
  1. The screen background (solid or vertical gradient with 4×4 Bayer dither).
  2. `screen->draw(gc)`.
  3. A scrim, if a modal is open: the gc tint blends every token colour 55 % towards bg while drawing the base layer, so there is still no readback.
  4. Sheet/dialog, then toast, then lock/power ring.
- Primitives reject on their bounding box first (≈ 20 cycles), usually on Y. About 60 primitives × 30 bands × 20 cycles ≈ 0.6 ms per full frame of rejection overhead.
- **Empty-band fast path:** if no primitive intersects a band apart from a solid background, a `FILL` entry is queued instead.

### 2.5 Dirty rectangles

- Fixed list of 16 `nx_rect`. A new rectangle is merged with an existing one if they overlap or if the union is ≤ 1.3× the sum of the areas. When the list is full, merge the pair with the least area growth.
- Rectangles are processed top to bottom.
- `nx_inval_all()` is a special case (theme change, wake).
- While the display is off, invalidations accumulate. Wake = full render with the backlight at 0, then fade in.

### 2.6 Primitives (all anti-aliased where it matters, all token-coloured)

| Primitive | Method | CPU |
|---|---|---|
| `gc_fill`, `gc_vgrad` | 32-bit stores; gradient colour per row, Bayer dither per pixel | 0.5 / 3 cycles/px |
| `gc_rrect` (fill + optional border), `gc_pill`, chips, cards | Rows inside the corners are plain fills. Corner pixels get coverage `clamp(((r+½)² − d²) · inv2r)`, with `inv2r = 65536/(2r)` precomputed per call (no hardware divider). | ≈25 cycles per corner pixel |
| `gc_bar` (progress) | Track + fill + round caps. The fill end has fractional coverage in Q8, so the bar grows smoothly by sub-pixels. Optional diagonal hatch (paused). | trivial |
| `gc_segbar` (chain/preset) | Segments with widths ∝ duration (min 4 px, 2 px gap). Done / current (partial) / future. | trivial |
| `gc_ring(cx, cy, r, w, a0, a1, caps)` | d² band test. Angle test by cross product against start/end vectors from a 64-entry quarter-sine table (no atan). Round caps are small AA discs. | ≈12 cycles/px in the annulus bbox |
| `gc_trace(rect, y_q4[], n, width, glow)` | Column-span polyline for the scope, sparklines and sweep previews (§2.9) | ≈15 cycles per trace px |
| `gc_icon(id, x, y, tok)` | Alpha mask (2/4bpp, raw/RLE) tinted with the token | same as glyphs |
| `gc_text`, `gc_digits` | §2.7 | |

Blend: `c = bg + ((fg − bg)·a >> 5)` on the `0x07E0F81F` spread form, a = 0..32 from a 16-entry table. That is 8–10 cycles with the M0+ single-cycle multiplier.
Hot loops (fill, blend, glyph decode, span) can go into `.ramfunc` (≈1 KB RAM) if profiling shows flash wait states hurt.

### 2.7 Text and numbers

- **Glyph lookup:** dense table for 0x20–0x7E, plus a sorted extra table for private codes 0x80–0x9F. The string compiler maps UI-only characters (ä ö ü Ä Ö Ü ß ° · … – × µ ‹ › ▸) to these codes.
  - Name bytes are rendered with `NX_TXT_RAW`: 0x7F and ≥ 0x80 draw a tofu box.
  - Full ASCII, including `" ; < > ? \ \` | *`, is always present in the text fonts.
- **Decoder:** walks tokens from the glyph start and skips rows above the band (runs skip in O(1), literal runs by pointer arithmetic), then blends only the rows inside the band.
- **Tabular digits:** the generator forces every digit's advance to the widest digit (the glyph is centred in the cell) in all fonts. Numbers never jitter.
- **Truncation:** ellipsis (…) by measuring. A **marquee** runs only for the focused name field:
  - 2 px steps at 20 Hz (40 px/s), 1 s pause at the start, 30 px gap, 3.6 ms per step.
  - Stops when the screen is dimmed or reduced motion is on (then ellipsis only).
- **Per-digit dirty updates:** every numeric field is a row of fixed cells. On a value change, only cells whose character changed are invalidated. The time-left countdown redraws one or two cells per second (≈ 0.08 ms).
- **Odometer roll** (hero frequency and step counter):
  - A changed cell animates over 160 ms (ease-out). The old digit moves up by p·H and the new digit comes in from below, clipped to the cell. Digits are staggered 20 ms from right to left.
  - The digit briefly tints to the generator accent and fades back to `text1` over 400 ms.
  - **Live rule:** if a field updates more than 3 times per second (sweeps, PC scans), rolls are disabled and digits swap instantly. The layout is locked for the duration of the sweep item.

### 2.8 Hero frequency layout (u64 `v = m·10 + e`, Hz = m·10^(e−8), up to 8 + 8 digits)

- Decode once per change into `int[8]` + `frac[8]` (u64 split once by 10^8, then 32-bit divisions).
- Clamp with an overflow flag, where the classic code wrapped mod 10^16.
- The layout is chosen by pure arithmetic (all advances are compile-time constants; host-tested). Available width is 296 px.
  - **Big part:** D1 (48 px, advance ≈ 29) shows the integer part with a 7 px gap per group of 3, then `.` (12 px), then `k` big decimals.
  - **Tail:** B1 (16 px) shows the remaining `8−k` decimals, grouped by 3, top-aligned to the big digits. "Hz" sits under the tail, baseline-aligned.
  - **Choice:** try `D1, k = 3..0`, then `D2 (28 px), k = 8..0`, and take the first that fits.
  - **Trailing-zero tail digits** are drawn in `text3` ("ghost zeros"). Significant tail digits use `text1`.

| Value | Result |
|---|---|
| 727.00000000 Hz | `727.000` + tail `000 00` + Hz |
| 37 500 Hz | `37 500.00` + tail `0 000 00` |
| 123 456.78 Hz | `123 456` + tail `.780 000 00` |
| 12 345 678.12 Hz | whole number in D2 |

### 2.9 Waveform preview (scope), sweep bar and chain bar

**Sizes**

| Where | Size |
|---|---|
| Focus dashboard, mid zone right | 136×52 (plot 128×44) |
| Program/preset detail and manual editor | 296×64 |
| PC monitor | none; a 20 px waveform glyph beside the output line |
| Mini strip | 20 px static waveform glyph |

**Samples**
- One sample per plot column. `y_q4[]` is int16 in Q12.4, computed each frame from phase φ (Q16).
- Shapes:
  - sine (64-entry quarter table)
  - square (duty 50 % unless known)
  - saw and inverse saw
  - triangle
  - damped sine / damped square: carrier × 32-entry exponential burst envelope
  - H-bomb edge / H-bomb sine: approximations flagged "approx."
  - custom: 64-sample table supplied by a preset
- Display shows 2.5 periods, normalised; caption "shape, not to scale".

**Overlays**
- **Gating:** on/off bands shaded in `surface2`; signal inside "on" windows, flat line at offset in "off" windows; label "gate 50 %".
- **Out1 vs Out2:**
  - Out1 is a 2 px trace in the accent with a glow pass (4 px at 20 %).
  - Out2 is a 1.5 px trace in the accent at 60 %.
  - **sync:** single trace with label "1 = 2".
  - **inverse:** Out2 mirrored.
  - **independent:** Out2 at the visual frequency ratio clamped to 0.25–4, with a label such as "×12.5" beyond that.
- **Scale:** amplitude label ("20.0 V", Vpp/Vp to be verified) at top left, dotted 4×2 grid, zero line. Nonzero offset shows as a shifted dashed zero line once `hdr2` semantics are known.
- **Unverified wave code:** "?" badge and "wave #41" label.

**Animation**
- Phase advances 0.5 cycles/s at 12 fps while running.
- Paused: frozen, drawn in `text2` colours. Stopped: flat line.
- Reduced motion or dimmed: static.
- Cost per frame, small scope: raster ≈ 0.3 ms, SPI 3.5 ms → 42 ms/s bus (4.2 %) at 12 fps.
- Cost per frame, detail scope (296×64): 9.5 ms → 10 fps = 9.5 % bus, on that screen only.

**Sweep bar** (296×20, running sweep or preset sweep item)
- Log or linear track from start to end frequency, with decade ticks for log.
- The swept part is filled in the accent. A marker (triangle + 1 px line) sits at the current frequency.
- End labels are compact ("1.2k", "45k"); a direction arrow shows up / down / ping-pong.
- Marker moves invalidate two 12×20 rectangles (≈ 0.25 ms). Log position uses a table-based fixed-point log2, because the M0+ has no CLZ or FPU.

**Chain/preset bar:** `gc_segbar`, 296×10. It is redrawn only when the item changes; otherwise only the current segment's fill advances.

### 2.10 Smooth list scrolling

- **Selection move inside the viewport:** the highlight (surface2 + 3 px accent bar) slides over 120 ms. Each frame repaints only the rows under the old and new highlight positions (2 × 320×32 → 10 ms/frame).
- **Viewport scroll**, when the selection leaves the viewport: the content offset tweens by one row (32 px) over 4 frames. The viewport is 320×160, i.e. 25.6 ms/frame.
- **Held keys:** during auto-repeat, and always with reduced motion, jump without animation (keeps up with 20 steps/s).
- **Long lists:** the fast-scroll letter bubble shows while held (§5.1).
- **Scrollbar:** 2 px thumb at the right edge.

### 2.11 Transitions

**A. Hardware-scroll push** (G1↔G2 focus, entering or leaving detail pages, library tab change)
1. Over 240 ms, use 40 steps of Δ = 8 px (one step every 6 ms).
2. At step k:
   1. Draw new-screen columns `[(k−1)Δ, kΔ)` into GRAM columns `[(k−1)Δ, kΔ)`, which currently show the old screen's left edge.
   2. Queue `SCROLL(s ± Δ)`. Those columns wrap around to the right edge, exactly where the new screen belongs.
3. After 320 px, s is back to 0, so the mapping returns to identity.

- Total cost is exactly one screen (38.4 ms of SPI) spread over 240 ms, plus 40 five-byte commands.
- **Artifact:** for about 1 ms per step, the 8 px band being rewritten shows new content at the old edge. At most one refresh (16.7 ms), 8 px wide, near-invisible on the dark background.
- Without a TE pin the scroll isn't synchronised, so a mild tear may appear. Verify both on the device in M0.
- Keys pressed during a transition are buffered (max 2), not dropped. Transitions are not interruptible because they are short.
- Push right is the mirror image (s decreasing, columns written right to left).

**B. Backlight dip** (hierarchy changes such as menus, settings and PC mode enter/exit, where a push would look wrong)
- Backlight down to 25 % in 60 ms, full render (≈ 40 ms), back up in 120 ms.
- This hides the top-to-bottom repaint wipe. It is also the reduced-motion transition.

**C. Reveal wipe** (bottom sheets, dialogs)
- The scrimmed base is rendered once (full screen, 38 ms).
- The sheet's rows are then revealed from the bottom edge upward over 180 ms. Content is static and the mask moves, so each row is drawn once.
- Toasts slide 30 px from the top: 5 frames × 4.4 ms.

**D. Fade of a small region** (splash wordmark, toast text): re-render the region at alpha 0→1 in 6 steps.

### 2.12 Backlight and panel power states

- **TIM17** (1 MHz tick, ARPE=0), update interrupt only (works in the emulator today).
  - Entering the high phase: set PB12, `ARR = on_us − 1`.
  - Entering the low phase: clear PB12, `ARR = off_us − 1`.
  - Levels 0 and 1000 stop the timer with a static pin.
- 256 perceptual levels map to 0..1000 µs through a gamma-2.2 table (512 B, or computed as x²/65 to save flash).
- Fades step the level every 10 ms from the UI tick. Default 1 kHz; test 2 kHz for whine and flicker in M0.
- DMA to GPIO BSRR is not an option: GPIO sits on the M0+ IOPORT. It isn't needed.

**Panel states**

| State | Sequence |
|---|---|
| Screen off | Fade to 0 over 600 ms → `DISPOFF` → `SLPIN` |
| Wake | `SLPOUT` → ≥ 5 ms → full render in the dark → `DISPON` → fade in over 150 ms (keep ≥ 120 ms between SLPOUT and SLPIN) |
| Power-off | Goodbye, then fade out, then `DISPOFF` + `SLPIN` before the core's PB2 signal, so no garbage shows while the supply decays |

- Idle mode (8 colours) and partial mode were evaluated and are not used. In landscape the partial area is a vertical band, and the backlight dominates power anyway.

### 2.13 Splash animation (about 1.2 s, skippable by any key; "Short" = 0.5 s, "Off")

| Time | What happens |
|---|---|
| 0–300 ms | Backlight fades 0 → level |
| 0–500 ms | Brand ring (the "O" of the wordmark, Ø 30 px, 5 px) sweeps 0→360°; only the new arc segment is drawn each frame (< 0.2 ms/frame) |
| 200–600 ms | A sine wave traces left to right inside the ring |
| 500–800 ms | Wordmark "OpenGX Pro" (its "O" is the ring with the sine) fades in, with the line "Open firmware for the GeneratorX Pro" (C1); the foot shows "v0.9.0" |

- If the core needs a longer boot window (factory port), the ring breathes (accent tint ±10 %) until released.
- The ring and the sine are **procedural**; the letters of the wordmark come from the brand font. There are no Spooky2 trademarks: no ghost and no red "2".

### 2.14 Time budgets (SPI 2.0 Mpx/s; CPU at 64 MHz)

| Operation | Pixels | SPI | CPU raster (overlapped with DMA) |
|---|---|---|---|
| Full screen | 76,800 | 38.4 ms | 6–12 ms (DMA: ≈ 40 ms total; polled ≈ 50 ms) |
| One band 320×8 | 2,560 | 1.28 ms | 0.15–0.5 ms |
| Status bar 320×22 | 7,040 | 3.5 ms | 0.6 ms |
| Name line 296×24 (marquee step) | 7,104 | 3.6 ms | 0.8 ms; 20 steps/s = 7.1 % bus |
| Hero frequency, full relayout 296×60 | 17,760 | 8.9 ms | 2.5 ms |
| One hero digit cell 28×60 | 1,680 | 0.84 ms/frame | 0.3 ms |
| Roll of 3 digits (6 frames) | ≈30 k | 15 ms spread over 160 ms | |
| Meta line 148×17 | 2,516 | 1.3 ms | |
| Scope 136×52 / 296×64 | 7,072 / 18,944 | 3.5 / 9.5 ms per frame | 0.3 / 0.8 ms |
| Progress fill step / time-left digit | ≤ 16 / 153 | < 0.1 ms | |
| Sweep marker move | 480 | 0.24 ms | |
| Chain bar 296×10 | 2,960 | 1.5 ms (on item change) | |
| Mini strip 320×31 / its frequency field | 9,920 / 1,800 | 5.0 / 0.9 ms | |
| List row / viewport (5 rows) / highlight move | 10,240 / 51,200 / 20,480 | 5.1 / 25.6 / 10.2 ms | |
| Toast 296×30 per frame (slide = 5 frames) | 8,880 | 4.4 ms | |
| Sheet reveal 320×132 (once) + scrim base | 42,240 + 76,800 | 21 + 38 ms spread over 180 ms | |
| HW push (whole transition) | 76,800 | 38.4 ms spread over 240 ms | |
| Key-hint bar 320×24 | 7,680 | 3.8 ms | |
| PC-mode frequency (28 px), changed digits only | ≈ 2,000 | ≈ 1 ms | ≤ 25 Hz per field |
| Calibration cell (5 digits) | ≈ 1,000 | 0.5 ms | |

**Steady-state budget, running dashboard:** marquee 7.1 % + scope 4.2 % + running pulse 0.2 % + per-second fields ≈ 0.1 % → **≈ 12 % bus, ≈ 4 % CPU.**
Rules: steady state ≤ 20 % bus and ≤ 15 % CPU. A render slice is ≤ 2 ms of CPU. The main loop never waits on SPI.

### 2.15 Non-blocking contract

- `nx_render_step(budget_us)` draws bands while a buffer is FREE and budget remains, then returns. The TC ISR does the rest.
- With DMA, the worst CPU hold is about 1 ms (one complex band). Without DMA it is about 2 ms (band + polled send).
- The serial RX/mirror path stays entirely in the ISRs (w11/14/15/24/25 and the optional w20/21/28/29 remain immediate).

---

## 3. Asset pipeline (`tools/gen_nx_assets.py`)

### 3.1 Inputs, committed under `assets/nx/`

```
assets/nx/manifest.json             fonts, sizes, charsets, bpp, features; icon list; palettes; string files
assets/nx/fonts/inter/*.ttf         Inter 4.x static TTFs (Regular, Medium, SemiBold; Display Medium, SemiBold) + OFL.txt
assets/nx/fonts/SOURCES.md          URL, version, SHA-256 per file, copyright line
assets/nx/icons/material/*.svg      Material Symbols Rounded (wght 500, opsz 20/24 exports) + LICENSE (Apache-2.0)
assets/nx/icons/custom/*.svg        wave_*, gate, sweep_lin/log, out_sync/out_inverse, badge shapes (own, CC0)
assets/nx/strings/en.json          id -> {text, font, max_px}
assets/nx/palettes.json             tokens per theme (§4.1) + text/surface pairings to contrast-check
LICENSES/OFL-1.1.txt, LICENSES/Apache-2.0.txt, docs/nx/THIRD_PARTY.md
```

- Check each font's `OFL.txt` for a Reserved Font Name clause. Inter appears to have none; if one exists, the rasterised copy must not use that name. Internally the fonts are called `F_TITLE22` and so on.
- The About screen lists the credits: "Inter © The Inter Project Authors, SIL OFL 1.1; Material Symbols © Google, Apache-2.0".

### 3.2 Fonts

| Id | Face | px | Glyphs | Format | Est. size |
|---|---|---|---|---|---|
| F_CAP13 | Inter Medium | 13 | ASCII 95 + UI extras | 4bpp best-of raw/RLE (2bpp raw as a cut line) | 3.4 KB (2.3) |
| F_BODY16 | Inter Regular | 16 | ASCII + extras | 4bpp best-of (2bpp cut line) | 4.5 KB (3.1) |
| F_TITLE22 | Inter SemiBold | 22 | ASCII + extras | 4bpp nibble RLE | 6.6 KB |
| F_NUM28 | Inter Display Medium | 28 | `0-9 . , : - + % space` | 4bpp RLE | 1.1 KB |
| F_NUM48 | Inter Display SemiBold | 48 | `0-9 . , - space` | 4bpp RLE | 2.1 KB |
| extras | ä ö ü Ä Ö Ü ß ° · … – × µ ‹ › ▸ | | only where the string catalogue uses them | | ≈ 1.2 KB |
| **Total** | | | | | **≈ 18.9 KB** (≈ 16.3 with 2bpp small fonts) |

- These sizes were measured with a stand-in font (Avenir Next) and the proposed codec. Inter should land within ±15 %.
- Plain byte-RLE was *worse* than raw at 13/16 px (5.0 vs 3.2 KB), hence the per-glyph choice.
- Rendering: `ImageFont.truetype(..., layout_engine=RAQM)`, `features=["tnum","ss02","zero"]` (disambiguates l/I/1 and 0/O). Hinting stays on (FreeType default) for 13/16 px.
- **Glyph record, 8 B:** `u16 offset|RLE-flag, u8 w, u8 h, i8 x_off, u8 y_off (from line top), u8 advance, u8 reserved`.
- **Font header:** height, baseline, bpp, dense range, extra-code list, pointers.
- **Nibble codec:** row-major w×h, runs may cross rows.

  | Nibbles | Meaning |
  |---|---|
  | `0, n` | n+1 transparent pixels (1..16) |
  | `F, n` | n+1 opaque pixels |
  | `1..E` | one pixel with that alpha |

  Raw 4bpp = 2 px per byte, high nibble first. Raw 2bpp = levels {0, 5, 10, 15}.
- **Determinism:** Pillow is pinned (12.3.0). The tool writes the FreeType and raqm versions and a SHA-256 per generated table to `assets/nx/assets.lock.json`; `make APP=nx assets-check` fails on drift, because gallery baselines depend on it.

### 3.3 Icons (tinted alpha masks)

- **20 px** (2bpp raw or 4bpp RLE, best-of):
  - playback: play, pause, stop, loop(repeat), skip_next, skip_previous, restart
  - library: library/folder, archive(inventory_2), star, star_outline, history, playlist(chain), calendar(protocol), tune(manual), sweep(show_chart), waves
  - timers and power: timer, hourglass, power, moon(bedtime), brightness, lock, lock_open
  - system: settings, info, warning, error, check_circle, usb, sync, thermostat, bolt, memory, storage, cable(link)
  - misc: keyboard, language, contrast, motion_off, edit, delete, add, chevron_left, chevron_right, expand_less, expand_more, back, download
  - ≈ 46 icons, ≈ 3.6 KB
- **32 px** (4bpp RLE): play_circle, pause_circle, check_circle, warning, lock, power, usb, restart. ≈ 1.9 KB.
- **Custom 20 px:** wave_sine, wave_square, wave_saw, wave_invsaw, wave_triangle, wave_damped_sine, wave_damped_square, wave_hbomb, wave_custom, gate, sweep_lin, sweep_log, out_sync, out_inverse. ≈ 1.0 KB.
- **Rasteriser:** SVG subset (path M/L/H/V/C/Q/A/Z, circle, rect; fill with even-odd holes; stroke via polygon offset) is flattened, filled at 8× with `ImageDraw.polygon`, reduced with `Image.reduce(8)`, then quantised. Pillow is the only dependency.
- **Total icons ≈ 6.5 KB.**

### 3.4 Strings and localisation

- String ids are generated in `nx_str_ids.h`, with tables per language as u16 offsets plus text. English ≈ 4 KB, German ≈ 4.5 KB.
- UTF-8 is converted to the private 0x80+ codes. An unknown character fails the build.
- **Width check:** each string declares `font` + `max_px`, and the tool measures it with the real metrics. A translation that overflows **fails the build**.
- Library tabs use icon + label for the active tab and icons only for the others, so German never overflows.
- The language switches at runtime (Settings). `LANGS=en` is a build option (cut line).

### 3.5 Build integration and independence from `original/`

- In the Makefile, per APP:
  - `APP=nx`/`nxgallery` generate `$(BUILD)/nx_assets.{c,h}` using `.venv/bin/python` (Pillow). The classic `gen_assets.py` stays stdlib-only.
  - Object files depend on `$(ASSET_HDR_$(APP))`.
- The nx link list excludes `gfx.c`, `ui.c`, `lcd_text.c` and `assets.o`.
- A CI check runs `make APP=nx ORIGINAL=/nonexistent` to prove independence.
- The tool prints a **budget report** (per font, icon set and string table), and the build fails if assets exceed `NX_ASSET_MAX` (32 KB).

### 3.6 Plan B: assets in SPI flash

- Fonts, icons and the second language live in an "asset pack" region of SPI flash (≥ 0x080000).
- The pack is written by the same Mac tool that uploads presets.
- The firmware keeps F_CAP13 + F_BODY16 built in as a fallback.
- Glyph access already goes through `nx_font_glyph(font, code) → const uint8_t*`. The SPI-backed version reads into a 2 KB RAM glyph cache: a 300 B glyph reads in about 80 µs over SPI1 at 32 MHz.
- This frees about 20 KB of MCU flash if the bootloader size spike fails.

---

## 4. Design system

### 4.1 Palette tokens (RGB888; RGB565 computed and contrast re-checked by the tool after quantisation)

| Token | Dark (default) | Light | High contrast | Night |
|---|---|---|---|---|
| `bg` | #0B0F14 | #F3F5F8 | #000000 | #000000 |
| `bg_top` (gradient) | #111823 | #FAFBFC | #000000 | #000000 |
| `surface1` (cards, sheets) | #151B24 | #FFFFFF | #000000 | #0A0303 |
| `surface2` (selected, raised) | #1E2733 | #E6EBF1 | #1A1A1A | #140606 |
| `outline` / divider | #2B3645 | #CBD3DD | #FFFFFF | #2A0E0A |
| `text1` | #E9EEF3 | #10151C | #FFFFFF | #C8563E |
| `text2` | #A7B1BE | #46505C | #E0E0E0 | #8A3A2A |
| `text3` (hints, ghost zeros) | #8B96A4 | #5E6773 | = text2 | = text2 |
| `g1` / `g1_dim` (track) | #38C7DA / #16414A | #0B7285 / #BFE6EC | #00E5FF / #004A55 | text1 / #3A150F |
| `g2` / `g2_dim` | #F5B544 / #4A3814 | #A65A00 / #F6E0B8 | #FFC400 / #553F00 | text1 / #3A150F |
| `success` | #4ADE80 | #15803D | #00FF66 | text1 |
| `warning` | #FF8A5B | #C2410C | #FF9900 | text1 |
| `error` | #FF5D73 | #BE123C | #FF3355 | text1 |
| `on_accent` (text on accent fills) | #0B0F14 | #FFFFFF | #000000 | #000000 |
| `grid` (scope) | #1F2935 | #DDE3EA | #333333 | #1A0806 |

Contrast, computed:
- **Dark:** `text1` on `bg` ≈ 16:1. `text2` on `surface2` ≈ 7:1. `text3` on `surface2` ≈ 5.1:1. A first `text3` candidate (#7D8896) failed on `surface2` at 4.35:1, which is why it is #8B96A4. `g1` on `bg` ≈ 9:1, `g2` ≈ 10.5:1, `error` ≈ 6.5:1.
- **Light:** `text3` on `surface2` ≈ 4.8:1. `g1` on white ≈ 5.6:1, `g2` ≈ 5.1:1.
- **Night:** `text1` on black ≈ 4.9:1. The first candidate, #C0503A, failed at 4.4:1.

The tool enforces ≥ 4.5:1 for every declared text/surface pair and ≥ 3:1 for non-text UI such as the focus ring and bar fill against its track.
Themes: Dark / Light / High contrast (Settings). Night is automatic in ambient mode, or toggled in quick settings.
Implementation: `const uint16_t nx_pal[THEMES][TOKENS]` (≈ 24 tokens). A theme change swaps the pointer and invalidates everything.

### 4.2 Status semantics (never colour alone)

| State | Icon | Label | Colour |
|---|---|---|---|
| No program | dashed ring | NO PROGRAM | text3 |
| Ready | ● | READY | text2 |
| Running | ▶ (pulsing ring on the badge at 2 Hz) | RUNNING / PRESET 3/12 / DAY 5/30 / MANUAL / SWEEP | generator accent |
| Paused | ‖ | PAUSED 04:12 | text2 (+ hatched bar) |
| Looping | ⟳ | LOOP ×n | accent |
| Finished | ✓ | FINISHED | success |
| Error | /!\ | NO RESPONSE / ERROR | error |
| PC mode | ⇆ | PC MODE | text1 |

The generator is always shown as the text badge "G1"/"G2" (accent fill, `on_accent` text). Outputs are shown as filled vs outlined dots plus ON/OFF text.

### 4.3 Grid, spacing and zones

- **Spacing:** base 4 px, steps 4/8/12/16/24. Screen margins 12 px.
- **Radii:** cards 12, chips 8, bars 4, pills h/2.
- **Focus:** 2 px accent ring with a 1 px bg gap.
- **Zones:**

  | Zone | Y range | Height |
  |---|---|---|
  | Status bar | 0–21 | 22 px |
  | Content | 22–215 | |
  | Key-hint bar | 216–239 | 24 px (non-dashboard screens) |
  | Dashboard mini strip (instead of the hint bar) | 209–239 | 31 px, with a 1 px divider at 208 |

- **List rows:** 32 px, 5 per viewport.

### 4.4 Type scale (tracking baked in by the tool)

| Style | Font | Line | Use |
|---|---|---|---|
| D1 | F_NUM48 | 56 | hero frequency |
| D2 | F_NUM28 | 32 | PC-mode frequencies, ambient time-left, large durations |
| T1 | F_TITLE22 | 26 | program name on the dashboard, screen titles |
| B1 | F_BODY16 | 20 | list rows, values, meta |
| C1 | F_CAP13 | 16 | labels, hints, chips, UPPERCASE status (+0.5 px tracking) |

Minimum text size is 13 px.

### 4.5 Motion rules

| What | Duration | Easing | Reduced motion |
|---|---|---|---|
| Digit roll + accent tint | 160 ms + 400 ms tint | ease-out cubic | instant swap, no tint |
| Selection highlight slide | 120 ms | ease-out | instant |
| List viewport scroll | 4 frames (≈ 120–160 ms) | ease-out | instant |
| Toast in / out | 200 / 160 ms | out / in | instant |
| Sheet reveal | 180 ms | ease-out | instant with backlight dip |
| Screen push (HW scroll) | 240 ms | ease-in-out (step timing) | backlight dip |
| Backlight dip | 60 + 120 ms | linear | kept (it is not motion) |
| Progress bar | continuous tween | linear | same |
| Scope phase | 12 fps | linear | static |
| Running pulse | 2 Hz, 10 fps, 16×16 px | sine | static ▶ |
| Marquee | 40 px/s, 1 s start pause | linear | ellipsis |
| Wake / dim / off fades | 150 / 1200 / 600 ms | gamma space | same |

- Animations are driven by time, sampled at frame start. Dropped frames skip ahead and never slow down.
- Never animated: error screens, PC-mode values (instant for accuracy), lock and confirm prompts (except the hold ring).
- Nothing animates while dimmed or off.

---

## 5. UX and navigation

### 5.1 Key gesture engine (`nx_keys.c`, host-testable)

- **Input:** the core calls `nx_keys_feed(mask, now_ms)` every 10 ms. The mask holds the 8 raw bits (LOOP, START, STOP, UP, DOWN, G1, G2, POWER).
- **Debounce:** 2 equal samples (20 ms).
- **Events:** `PRESS`, `RELEASE(dur)`, `HOLD` (500 ms), `LONG` (1,500 ms), `REPEAT(n)`, `CHORD(mask)`.
  - `REPEAT`: first at 400 ms, then every 110 ms, speeding up to 50 ms after 1.5 s.
  - `CHORD`: a second key within 80 ms. G1/G2 presses are delayed 60 ms to detect the chord, which is not noticeable.
- **Act-on rule:** a key acts on **press**, unless it has a HOLD meaning in the current context. Then it acts on **release** (< 500 ms) or on **HOLD**.
- **Letter jump** (lists ≥ 40 entries): holding UP/DOWN for ≥ 1.2 s moves by initial letter (bubble shows the letter). Releasing stops.
- **POWER:** from 200 ms the power ring overlay is shown. At 800 ms the core powers off (the core stays the authority). This works even when keys are locked.
- **Wake:**
  - Screen off: the first press only wakes (it is consumed).
  - Dimmed: the key acts normally and brightness is restored.
  - "Attention" events (finished, error) wake the screen.

### 5.2 Global key model (physical labels)

| Key | Dashboard (root) | Lists (library, menus, settings) | Detail pages | Digit/field editors | Sheets / dialogs |
|---|---|---|---|---|---|
| UP / DOWN | UP: quick settings; DOWN: menu | move (repeat, letter jump) | scroll | ±1 at the cursor, carrying (hold accelerates; after 1.5 s the step escalates ×10) | move |
| START | play / pause / resume / restart; opens the library if no program is loaded | open / select | run on the target generator | apply / close | confirm (hold for destructive actions) |
| STOP | stop the focused generator; hold 1 s: stop both | back | back | cancel (revert) | cancel |
| LOOP | short: loop toggle (when stopped, "play looped", as the original); hold: generator options | short: next tab; hold: item options | options | short: Roll/Set mode; hold: tools (×10, ÷10, zero right of cursor, recent frequencies) | — |
| G1 / G2 | focus that generator (push); pressing the focused one again opens its library (original habit) | set the load target G1/G2 (as in the original list); settings rows: value −/+ | target G1/G2 | cursor left/right; hold: jump to the ends | left/right |
| POWER | short: screen off/on; hold 0.8 s: power off (everywhere) | same | same | same | same |
| Chords | G1+G2 held 1.5 s: key lock; when locked, START+STOP held 1.5 s unlocks | | | | |

- **Behaviour change:** in the classic firmware, POWER short stops both generators. In nx, "stop both" moves to STOP hold (and the options sheet). Needs sign-off.
- The G1/G2 "left/right" meanings assume G1 sits left of (or above) G2 on the case. Verify in M0; if not, swap only in the editors.
- In PC mode, generator actions are hidden. START/STOP/LOOP show the toast "PC controls the generators". G1/G2, UP/DOWN (brightness) and POWER still work.

### 5.3 Reach (presses)

| Action | Presses |
|---|---|
| Play/pause | 1 |
| Switch generator | 1 |
| Loop | 1 |
| Screen off | 1 |
| Stop both | 1 (hold) |
| Key lock | 1 gesture |
| Load a program on G1 | G1, G1 → library, then move + START |
| Brightness | UP, then G1/G2 (2+) |
| Manual mode | LOOP-hold → Manual (2–3) |
| Settings, History, Timers | DOWN, move, START (≤ 3; the menu remembers the last item) |
| Diagnostics | 3–4 |

### 5.4 Digit editor (frequency, sweep start/end)

- **Cursor:** 16 digit cells (8 integer + 8 fraction). Leading zeros show as ghost zeros only while the cursor is on them. A step label under the cursor reads, for example, "step ×0.001 Hz".
- **Roll mode** (default when adjusting): UP/DOWN change the digit with carry, clamped to [min, max]. Hitting a limit shakes the field 2 px.
- **Set mode** (default for a new value): the digit cycles 0–9 without carry.
- **LOOP-hold tools:** ×10 / ÷10 (shift), zero right of the cursor, paste the current program step, recent frequencies.
- **Live mode** (manual running): changes are sent after 150 ms of debounce, and a "● LIVE" chip is shown. START closes; STOP reverts.
- **Fast entry example:** 727 Hz from 0 takes about 6 presses (G1×2, UP×7 with repeat, G2, UP×2, G2, UP×7).

### 5.5 Confirmation, undo and lock

- **Destructive actions** (erase slot, delete chain, restart protocol, clear history, reset settings) use a dialog with **hold START 1 s** (the ring fills). STOP cancels.
- **Stopping a generator is never confirmed.** An undo toast offers "Stopped at step 12/180 — START resumes" for 5 s.
- **Key lock:** G1+G2 held 1.5 s, or quick settings, or automatically after start (setting).
  - While locked, any key shows the lock card for 2 s.
  - Unlock = START+STOP held 1.5 s. POWER hold still powers off.

### 5.6 Screen inventory and wireframes (320×240, 1 character ≈ 5 px)

Legend: ▶ play, ‖ pause, ■ stop, ⟳ loop, ✓ done, /!\ warning, ★ favourite, ∿ wave, [L] lock, ‹ back, ▸ next, ◔ ring.

**W1 Splash**
```
┌──────────────────────────────────────────────────────────────┐
│                                                              │
│                          ╭──────╮                            │
│                        ╭─╯ ∿∿∿∿ ╰─╮   ring 0→360° (500 ms)   │
│                        │  ∿    ∿  │   sine traces inside     │
│                        ╰─╮      ╭─╯                          │
│                          ╰──────╯                            │
│                                                              │
│                         OpenGX Pro         fades in          │
│             Open firmware for the GeneratorX Pro   C1        │
│                                                              │
│ v0.9.0                                     any key: skip     │
└──────────────────────────────────────────────────────────────┘
```

**W2 Focus dashboard, running** (the variants below replace only the focus area)
```
┌──────────────────────────────────────────────────────────────┐
│ (G1) ▶ RUNNING                         ⟳ LOOP ×2    3/12 [L] │ 0-21  status
│ Lyme Borreliosis                                             │ 26-49 T1, marquee
│                                                    000 00    │ 52-111 D1 hero
│   727.000                                          Hz        │        + B1 tail/unit
│ Step 12 / 180 ◔        ╭─────────────────────────────╮       │ 114-165 meta (B1)
│ 3:00 per step · 1:12   │ ∿∿∿∿∿∿∿ Out1 = Out2   20.0 V │       │        scope 136×52
│ 20.0 V · Sine          ╰─────────────────────────────╯       │
│ ████████████████████░░░░░░░░░░░░░░░░░░     1:52:10 left      │ 170-189 bar + time
│ Next ▸ 728.000 Hz                               pass 2       │ 192-207 C1
├──────────────────────────────────────────────────────────────┤ 208
│▌G2  ‖ Detox Liver              1 234.56 Hz           12 %    │ 209-239 mini strip
└──────────────────────────────────────────────────────────────┘
```
Dashboard variants (focus area only):
```
IDLE      (G1) ○ NO PROGRAM
           ╭ ─ ─ ╮   Nothing loaded on G1
           │  +  │   START  Choose a program
           ╰ ─ ─ ╯   LOOP (hold)  Manual / sweep     DOWN  Menu
READY     (G1) ● READY   · hero in text2 · "180 steps · 3:00 each · 9:00:00 · 20.0 V · Sine"
          empty bar 9:00:00 · chips [START ▶ Play] [LOOP ⟳ Play looped]
PAUSED    (G1) ‖ PAUSED 04:12 · digits text2 · scope frozen · bar hatched · time frozen
FINISHED  (G1) ✓ FINISHED · 32 px check ring · "180/180 steps · 9:00:00"
          START Restart · G1 Library · STOP Clear
ERROR     (G1) /!\ NO RESPONSE · card: "G1 is not answering (3 timeouts).
          Program paused at step 12/180."  START Retry · STOP Stop G1 · LOOP Details
MANUAL    (G1) ◉ MANUAL · hero = set frequency · "Out1 ● ON  Out2 ● ON  Sync" · START Edit
```
Mini strip variants:
```
▌G2  ▶ Detox Liver                1 234.56 Hz      0:39:12
▌G2  ✓ Detox Liver                          finished 0:48:00
▌G2  ○ no program · press G2 twice to choose
▌G2  /!\ not responding                        STOP clears
▌G2  ▶ Evening 2/4 · Lyme Disease               727.00 Hz
```

**W4 Menu sheet** (DOWN on the dashboard: 2×4 grid, G1/G2 columns, UP/DOWN rows)
```
┌──────────────────────────────────────────────────────────────┐
│ (dashboard re-rendered under a 55 % scrim)                   │
│╭────────────────────────────────────────────────────────────╮│
││  [Library]   [Presets]   [Protocols]  [Manual]             ││ cells 72×52: icon + C1 label
││  [Timers]    [History]   [Settings]   [Diagnostics]        ││ focus = accent ring
│╰────────────────────────────────────────────────────────────╯│
│ G1◀ ▶G2 Column   ▲▼ Row   START Open   STOP Close            │
└──────────────────────────────────────────────────────────────┘
```

**W5 Quick settings** (UP) and **W6 generator options** (LOOP hold)
```
╭ Quick ──────────────────────────────────────────────────────╮
│ Brightness  ▕█████████████░░░░░░▏ 70 %        G1 − / G2 +   │
│ Night mode  Off     Lock keys  ›     Screen off  ›          │
│ Auto-dim    1 min   Reduced motion  Off                     │
╰─────────────────────────────────────────────────────────────╯
╭ Options · G1 · Lyme Borreliosis ────────────────────────────╮
│ ▸ Next step          ◂ Previous step       ↺ Restart        │
│ Program details      Load another…         Manual on G1     │
│ Stop G1 after…       ■ Stop both generators                 │
╰─────────────────────────────────────────────────────────────╯
```

**W7 Library, Slots tab** (sources: Quick, Slots, Presets, Protocols, Manual; LOOP = next tab)
```
┌──────────────────────────────────────────────────────────────┐
│ Library                               Load to  (G1)   G2     │ target in its accent
│  ★   [≡ Slots]   ▤   ▦   ∿                                   │ active tab: icon + label
│ 01  Detox                                  16 st   0:48:00   │
│▌02  Lyme Disease                     ★     12 st   6:00:00   │ surface2 + accent bar
│ 03  Pain Relief (general) - long pro…       8 st   0:24:00   │ marquee after 1 s
│ 04  Sleep                                   4 st   0:12:00   │
│ 05  — empty —                                             ▐  │
│ ▲▼ Move  START Open  LOOP Tab  G1/G2 Target  STOP Back       │
└──────────────────────────────────────────────────────────────┘
```
The Quick tab has two sections: RECENT (last 5 across sources, with generator and run count) and FAVOURITES (slots, presets and protocols).

**W9 Program detail (slot) + frequency list** (read through `storage_slot_read` into a 16-entry page cache, never `g_ch`)
```
┌──────────────────────────────────────────────────────────────┐
│ ‹ 02 · Lyme Disease                   Run on  (G1)   G2      │
│ 12 steps · 30:00 each · 6:00:00 · 20.0 V · wave #41 ?        │
│  1     432.000 000 00 Hz                                     │
│▌ 2     727.000 000 00 Hz              ▶ playing on G1        │
│  3     728.000 000 00 Hz                        gate 12,40   │
│  4     880.000 000 00 Hz                                     │
│  5   1 550.000 000 00 Hz                                  ▐  │
│ START Run  ▲▼ Scroll  LOOP Options (★, erase…)  STOP Back    │
└──────────────────────────────────────────────────────────────┘
```

**W15 Power & timers**, **W16 Resume prompt**, **W17 Settings root**
```
│ ‹ Power & timers                                             │
│  Resume after power loss            ◀ Ask ▶  (Ask/Auto/Off)  │
│  Auto-start at power-on             Off (Last session/Chain) │
│  Stop generators after              2 h                      │
│  Power off when all finished        Off                      │
│  Dim screen after                   1 min                    │
│  Then                               Screen off (or Ambient)  │
│  Auto key lock                      After start              │
┌──────────────────────────────────────────────────────────────┐
│                    ↺  Power was interrupted                  │
│  (G1) Lyme Borreliosis        step 12/180 · 1:52:10 left     │
│  (G2) Detox Liver             step 3/16 · 0:39:00 left       │
│      [ START  Resume both ]        [ STOP  Start fresh ]     │
│      G1 / G2: resume only that generator                     │
│               Resuming in 8 s  ◔                             │
└──────────────────────────────────────────────────────────────┘
│ ‹ Settings                                                   │
│  Display          Dark · 70 % · dim 1 min · splash short  ›  │
│  Power & timers   Resume: Ask                             ›  │
│  Keys & lock      Repeat normal · auto-lock off           ›  │
│  Motion           Full                                    ›  │
│  Language         English                                 ›  │
│  Data             History 48 · 4 chains · 22 ★            ›  │
│  About & licences                                         ›  │
```

**W18 Diagnostics** (+ key test)
```
┌──────────────────────────────────────────────────────────────┐
│ ‹ Diagnostics                                   up 3d 04:12  │
│  Firmware  0.9.0 · a1b2c3d · 2026-10-01                      │
│  Reset     Power-on (previous: watchdog)                     │
│  MCU       41.5 °C · VDD 3.29 V · HSE 8 MHz ok               │
│  Panel     ST7789 var A · full frame 40 ms · bus 9 %         │
│  G1 link   ● OK  ack 2.9 ms  timeouts 0   ▁▂▁▁▃▁▁▂           │
│  G2 link   ● OK  ack 3.1 ms  timeouts 0   ▁▁▁▂▁▁▁▁           │
│  Storage   W25Q16 EF4015 · 1.4 MB free · errors 0            │
│  Memory    RAM free 5.1 KB · stack peak 1.3 KB               │
│ LOOP Key test   START Refresh   STOP Back                    │
└──────────────────────────────────────────────────────────────┘
Key test: 8 key caps laid out like the case, lit while pressed, last gesture
and chord timing shown, hold STOP 2 s to leave.
```

**W19 PC monitor** (never transmits; changed-digit updates only; "sniffed" marks w20/21/28/29 if mirrored)
```
┌──────────────────────────────────────────────────────────────┐
│ ⇆ PC MODE · generators controlled by Spooky2      ● 12 upd/s │
│ (G1) Scan: Biofeedback (General)                             │
│   Out1 ● 41 234.567 89 Hz              ▁▂▂▃▄▅▅▆▇  log trail  │ D2 digits
│   Out2 ● = Out1 (sync)      Sine · 20.0 V (sniffed)          │
│──────────────────────────────────────────────────────────────│
│ (G2) Detox Liver                                             │
│   Out1 ● 727.000 00 Hz                 ▁▁▁▁▁▁▁▁▁             │
│   Out2 ○ 728.000 Hz  off    Square · 10.0 V                  │
│ ↓ p07 "Lyme Borreliosis" received ✓                          │
└──────────────────────────────────────────────────────────────┘
```

**W20 Calibration** (same function and topology as the classic grid: 21..28 top pairs, 0 centre, 1..20 in four columns × 5)
```
┌──────────────────────────────────────────────────────────────┐
│ CALIBRATION                        test f ×10³ · ampl step 4 │
│   G1·Out1        G1·Out2        G2·Out1        G2·Out2        │
│   21  31990      23  31990      25  31990      27  31990      │
│   22  32010      24  32010      26  32010      28  32010      │
│                    0 ▐ 32000 ▌  (selected = accent fill)     │
│    1  01234       6  01234      11  01234      16  01234      │
│    2  …           7  …          12  …          17  …          │
│    3              8             13             18             │
│    4              9             14             19             │
│    5             10             15             20             │
│ ▲▼ Select  G1 +  G2 −  START Save  LOOP Ampl  STOP Freq      │
└──────────────────────────────────────────────────────────────┘
```
- The factory port's `ui_cal_draw_value(idx)` becomes an invalidation of that cell.
- Values 21–24 set by the factory keep the classic "inverted" meaning: accent fill.
- Outside calibration mode, the call shows a toast "Factory: cal[22] = 12345" instead of drawing over the current screen.

**W21 Toast / error / hold-confirm / key lock**
```
╭──────────────────────────────────────────────────────────╮   toast: slides over the status bar,
│ ✓  G2 finished · Detox Liver · 0:48:00                    │   4 s, queue of 4; the key still acts
╰──────────────────────────────────────────────────────────╯
┌──────────────────────────────────────────────────────────────┐
│ /!\ STORAGE ERROR                                            │ error screen (instant)
│ The SPI flash did not confirm a write. Settings and history │
│ may not be saved. Generators keep running.                  │
│ Code E-SF-02 · sector 0x080000 · uptime 2:13:44              │
│ START Retry        STOP Continue        LOOP Diagnostics     │
└──────────────────────────────────────────────────────────────┘
╭ Erase slot 07 "Lyme Borreliosis"? ──────────────────────────╮
│ Program and name are deleted from the device.               │
│ Hold START to erase  ◔───   (1.0 s)        STOP Cancel      │
╰─────────────────────────────────────────────────────────────╯
            ╭──────────────────────────────────╮
            │              [L]                 │   key lock card over a scrim,
            │         Keys are locked          │   shown for 2 s on any key
            │  Hold START + STOP to unlock  ◔  │
            ╰──────────────────────────────────╯
```
Other toasts: "All finished — power off in 60 s (STOP cancels)", "Session timer stopped both generators after 2 h", "PC disconnected", "Program 7 received".

**W22 Power hold, goodbye, ambient/night, history**
```
power hold (200–800 ms): 64 px ring filling around the power icon, "Keep holding to power off"
goodbye: ring closes 360→0 · "Goodbye" · "This session 3 h 12 min · 2 finished"
         "✓ Saved — resume available" · fade out · DISPOFF/SLPIN · core sends PB2
┌──────────────────────────────────────────────────────────────┐
│                                                              │ ambient (night palette,
│  G1 ▶  Lyme Borreliosis                          1:52:10     │ backlight ≈3 %, D2 digits,
│        step 12/180                                           │ 1 update/s)
│  G2 ‖  Detox Liver                               0:39:00     │
│                                               any key: wake  │
└──────────────────────────────────────────────────────────────┘
│ ‹ History                                        48 entries  │
│ POWER-ON #214                                                │
│▌ ✓ (G1) Lyme Borreliosis        slot 02       3:00:00        │
│  ■ (G2) Detox Liver    stopped 12/16           0:36:00        │
│ POWER-ON #213                                                │
│  ⚡(G1) Terrain Day 4  power lost at 1:12:00                  │
```
There is no wall clock: history is grouped by power-on counter, with uptime offsets.
A one-time **Key guide** (keys diagram) is shown on first boot and is available again under Settings → Keys.

### 5.7 Navigation state diagram

```
 power-on ─► SPLASH ─┬─ G1+G2 held at boot ─────────────────────► CALIBRATION (until power-off)
                     ├─ resume record + setting=Ask ─► RESUME PROMPT ─┐
                     └────────────────────────────────────────────────┴─► DASHBOARD(Gx)

 DASHBOARD(G1) ◄──── G1 / G2 (HW push) ────► DASHBOARD(G2)
   │ UP ─► QUICK SETTINGS (sheet)            │ LOOP-hold ─► GENERATOR OPTIONS (sheet)
   │ DOWN ─► MENU (sheet) ─┬► LIBRARY ─────────────┐  ─► MANUAL ─► SWEEP SETUP ─► run ─► DASHBOARD
   │                       ├► TIMERS / HISTORY / SETTINGS ─► sub-pages
   │                       └► DIAGNOSTICS ─► KEY TEST
   │ START (no program) or G(focused) again ─► LIBRARY(target)
   │
 LIBRARY [Quick|Slots|Presets|Protocols|Manual]  (LOOP = next tab, G1/G2 = target)
   ├ START on slot ─► PROGRAM DETAIL ─ START ─► DASHBOARD (running)
   ├ START on folder ─► PRESET LIST ─ START ─► PRESET DETAIL ─ START ─► DASHBOARD (preset)
   ├ START on protocol ─► PROTOCOL VIEW ─ START ─► DASHBOARD (day k, session s)
   └ "My chains" ─► CHAIN EDITOR ─ START ─► DASHBOARD (chain)
 STOP = back one level everywhere (screen stack depth ≤ 4)

 Global (any state):  PB1 high ─► PC MONITOR ─ PB1 low ─► DASHBOARD + toast
                      POWER short ─► SCREEN OFF ◄─ any key
                      inactivity ─► DIM ─► OFF or AMBIENT
                      POWER hold ─► power ring ─► GOODBYE ─► off
                      overlays: TOAST · ERROR · KEY LOCK · HOLD-CONFIRM
```

---

## 6. Module breakdown and interfaces

### 6.1 Files (prefix `nx_`, linked only by `APP=nx`/`nxgallery`)

| File | Role | Host-testable |
|---|---|---|
| `nx_lcd.c/.h` | SPI2 16-bit, DMA queue + TC ISR, window, scroll, sleep, fallback | via host backend |
| `nx_bl.c/.h` | TIM17 PWM, gamma, fades, host override (w64) | logic yes |
| `nx_gfx.c/.h` | gc, tokens, blend, primitives, trace, segbar, ring | yes (host framebuffer) |
| `nx_font.c/.h` | glyph lookup, decoder, measure, ellipsis, digit cells | yes (vs a Python reference decoder) |
| `nx_render.c/.h` | dirty list, band scheduler, frame snapshot, transitions | yes |
| `nx_anim.c/.h` | tweens, easing tables, clock | yes |
| `nx_keys.c/.h` | gesture engine | yes |
| `nx_vm.c/.h` | snapshot types, diff helpers | yes |
| `nx_fmt.c/.h` | frequency decode + layout choice, durations, volts, percentages | yes |
| `nx_ui.c/.h` | root: event intake, screen stack, overlays, dim/off/lock, toasts, action outbox | yes |
| `nx_comp_list.c`, `nx_comp_sheet.c`, `nx_comp_form.c`, `nx_comp_digits.c`, `nx_comp_hints.c`, `nx_comp_tabs.c` | reusable components | yes |
| `nx_scr_dash.c` (+strip, scope, sweep bar, chain bar), `nx_scr_library.c`, `nx_scr_detail.c`, `nx_scr_preset.c`, `nx_scr_protocol.c`, `nx_scr_chain.c`, `nx_scr_manual.c`, `nx_scr_sweep.c`, `nx_scr_settings.c` (table-driven pages incl. timers), `nx_scr_history.c`, `nx_scr_diag.c`, `nx_scr_pc.c`, `nx_scr_cal.c`, `nx_scr_system.c` (splash, resume, goodbye, ambient, error, key guide) | screens | yes |
| `nx_theme.c`, `nx_str.c` (+ generated tables) | palettes, strings | — |
| `nx_ui_compat.c` | classic `ui.h` names for core/factory: `ui_cal_draw_values/value`, `ui_cal_enter/exit` | — |

Most screens are **providers for generic components**: lists (library, settings, history, detail, chain, protocol sessions), sheets (menu, options, quick, confirm) and forms (manual, sweep, timers). Only the dashboard, PC monitor, calibration, protocol day strip and system screens are bespoke. This keeps screen code around 15–18 KB.

### 6.2 Interfaces (load-bearing signatures)

```c
/* core -> UI */
void nx_ui_init(const struct nx_settings_ui *s);
void nx_ui_tick(uint32_t now_ms);                 /* ≤ 1 ms: events, anims, diff -> invalidate */
int  nx_render_step(uint32_t budget_us);          /* non-blocking; returns 1 while work remains */
void nx_keys_feed(uint8_t raw_mask, uint32_t now_ms);   /* every 10 ms */
void nx_ui_post(const struct nx_event *e);        /* ISR-safe ring, 16 entries */
struct nx_event { uint8_t type, ch; uint16_t a; uint32_t b; };
/* NXE_GEN_STATE, NXE_STEP, NXE_FINISHED, NXE_LINK_TIMEOUT, NXE_LINK_OK, NXE_UPLOAD_DONE(slot),
   NXE_NAME_CHANGED(slot), NXE_PC_MODE(on), NXE_PC_VALUE(ch,field), NXE_FLASH_ERROR(code),
   NXE_CHAIN_ITEM, NXE_PROTOCOL_DAY, NXE_TIMER_FIRED, NXE_CAL_VALUE(idx), NXE_CAL_ALL,
   NXE_POWER_HOLD(ms), NXE_RESUME_AVAILABLE, NXE_FACTORY_MODE(on) */
const struct nx_sys_vm *nx_core_snapshot(void);   /* pulled once per frame */
void nx_ui_goodbye(void);                         /* core calls; returns ≤ 1.5 s later, before PB2 */

/* UI -> core (queued, never blocks, refused in PC mode for generator actions) */
struct nx_action { uint8_t type, ch; uint16_t id; uint32_t v; const void *p; };
int nx_core_act(const struct nx_action *a);       /* NX_OK / NX_REFUSED_PC / NX_BUSY */
/* NXA_LOAD(slot|archive), NXA_PLAY, NXA_PAUSE, NXA_STOP, NXA_STOP_ALL, NXA_LOOP(on), NXA_SKIP(±1),
   NXA_RESTART, NXA_PRESET_RUN(id), NXA_PROTOCOL_RUN(id,day,session,variant), NXA_PROTOCOL_MARK,
   NXA_CHAIN_RUN/EDIT, NXA_MANUAL_SET(freq,ampl,wave,outs,relation,live), NXA_SWEEP_RUN(params),
   NXA_SETTING(key,val), NXA_FAV(kind,id,on), NXA_ERASE_SLOT, NXA_HISTORY_CLEAR, NXA_RESUME(mask),
   NXA_RETRY_LINK(ch), NXA_POWER_OFF */

/* screens */
struct nx_screen_ops {
    void    (*enter)(uint32_t arg);
    void    (*leave)(void);
    uint8_t (*event)(const struct nx_kev *k);      /* gesture -> state change + actions */
    void    (*tick)(const struct nx_sys_vm *now, const struct nx_sys_vm *drawn, uint32_t t);
    void    (*draw)(struct nx_gc *gc);             /* clip-driven, pure */
    const uint16_t *hints;                         /* string ids for the hint bar */
    uint8_t flags;                                 /* PC_ALLOWED, NO_DIM, MODAL ... */
};
extern const struct nx_screen_ops *const nx_screens[NX_SCR_COUNT];
```

**VM per generator** (`nx_gen_vm`, ≈ 110 B):
- mode: none / program / preset / protocol / chain / manual / sweep; run state; loop flag and pass count
- name pointer + length, plus a caption such as the preset name
- step, count, dwell, remaining in step / item / program / chain (ms, core-computed); segment durations (≤ 16 × u16)
- current and next frequency (u64); sweep {start, end, lin/log, direction}
- amplitude (0.01 V), offset, wave code + shape enum + verified flag, gate {a, b, known}
- outputs on/off, relation, Out2 frequency; link {state, timeouts, last ack µs}

**System VM:**
- pc_mode, cal_mode, factory; PC data (names 0/31, 4 frequencies, w11 bits, sync, sniffed wave/amplitude, update counters)
- resume available; protocol {id, day k/N, session, per-day state bits}
- uptime, temperature, VDD, reset reason, flash status, RAM free
- settings snapshot; favourites/recent counts

### 6.3 Host testability

Nav tests are key scripts that check the resulting action stream and screen stack. Examples:
- dashboard: G1, G1, DOWN×3, START → `NXA_LOAD(ch0, slot 4)`, `NXA_PLAY`
- lock: G1+G2 held 1.5 s, START → nothing; START+STOP held 1.5 s → unlocked
- PC mode: `NXE_PC_MODE`, START → `NX_REFUSED_PC` toast; no generator actions are ever emitted

Other host tests:

| Test | Checks |
|---|---|
| `nx_keys` | debounce, repeat curve, chord window, act-on-release with hold |
| `nx_fmt` | frequency layouts for the fixture set in §2.8, overflow |
| `nx_font` | RLE decode equals the tool's reference output |
| `nx_render` | dirty merge, band partitioning, scroll mapping with wrap |

The host backend renders to a 320×240 array, which is also used by the fast host gallery.

### 6.4 Requirements the UI places on the core plan

1. **Main loop:** non-blocking, pass ≤ 5 ms. It calls `nx_keys_feed` (10 ms), `nx_ui_tick`, `nx_render_step(2000)`.
2. **Keys and time:** raw key mask, not `keys_read`, and a ms clock.
3. **Power-off:** the core keeps authority (POWER held 800 ms) and awaits `nx_ui_goodbye()`.
4. **Resources reserved for the UI:** DMA1 ch1 + IRQ (≤ 8 µs ISR) and SPI2 exclusively. TIM17 for the backlight, with PB12 removed from the TIM3 ISR. w64 goes to `nx_bl_host_override`.
5. **factory.c:** direct `lcd_fill_rect` becomes `ui_cal_enter/exit`.
6. **Snapshot and events:** the snapshot provides every VM field above. Remaining times come from the core's tick arithmetic (dwell ×1.01 s). Events as listed.
7. **Action queue:** with a PC-mode gate as defence in depth (the UI also hides these actions).
8. **Store read APIs, each ≤ 1 ms:**
   - slot header, frequency and parameter pages (`storage_slot_read`)
   - preset directory **sorted by name per category**, with a precomputed **letter index** (27 × u16) and counts
   - preset items (per program: name, type, steps or sweep parameters, wave/amplitude/gate/Out2 relation, duration)
   - protocol definition + persistent day/session state
   - chains CRUD, favourites/recent, history ring
   - UI settings blob (≤ 32 B, defined by the UI), resume record
9. **Link health:** per-channel counters and error events. Generator timeouts must become detectable; the classic `link_wait_ok` silently times out.
10. **Diagnostics data:** reset reason (RCC_CSR at boot), ADC temperature/VDD at 1 Hz, flash JEDEC, stack high-water mark.
11. **Sniffed values:** w20/21/28/29 in the IRQ mirror (immediate) for the PC monitor.
12. **Waveform mapping:** a verified wave-code → shape table (hardware task).
13. **Multi-day protocols, no RTC:** a day advances only when its sessions complete or the user explicitly starts the next day. The optional "remote mode" counts 24 h of uptime and pauses across power cuts.

---

## 7. Screenshot and gallery tooling

- **`APP=nxgallery`** (`src/main_nxgallery.c`, pattern of `main_uitest.c`):
  - Fixture table `{name, screen, theme, setup_vm(), key_script, t_ms[]}`.
  - Stub core `src/nx_core_stub.c`: fixture VM, no UART, no SPI-flash writes.
  - Virtual clock, so animations are deterministic.
  - For each fixture: reset the UI, apply theme and VM, enter the screen, play keys, advance virtual time, render fully, call `nxg_done(id, name)` (noinline).
- **Emulator extensions:**
  - `tools/sim/periph.py`: SPI DS=16 frames sent MSB first; new `Dma`/`Dmamux` (memory→SPI2 16-bit, MINC/no-MINC, TC interrupt 9, timed at the SPI rate).
  - `tools/sim/devices.py`: VSCRDEF/VSCSAD applied in `image()`, SLPIN/DISPOFF shown as black, MADCTL MV/MX/MY tests.
  - `tools/sim/emu_cli.py`:
    - `--shot-hook SYMBOL:DIR` saves a PNG on every call, named from the r1 string.
    - `--frames FROM:TO:STEP:DIR` for animation capture.
    - Backlight-proportional brightness in screenshots (dim and night previews).
  - Minimal ADC model (optional; fixtures inject values anyway).
- **Host gallery:** `tests/host/nx/` compiles the same engine and screens against a host LCD backend with the same window/scroll semantics. It renders every fixture in under 1 s, for design iteration.
- **`tools/nx_gallery.py`:**
  - `run --host|--emu`, `compare`, `report`, `approve [names|--all]`, `anim NAME` (GIF/APNG via Pillow).
  - Baselines: `tests/gallery/nx/<theme>/<fixture>.png`, 1×, lossless, committed. They contain only open fonts and icons.
  - Compare is pixel-exact (RGB565 is deterministic). It writes `build/gallery/index.html` with baseline / current / diff heat map and a 2× nearest-neighbour view.
  - It also diffs **host vs emulator** renders to catch driver bugs.
  - Asset drift is caught first by `assets.lock.json`.
- **Fixtures (≈ 55; dark for all, light and high-contrast for ≈ 20 key ones):**
  - splash ×2, resume
  - dashboard: idle, ready, running, loop, paused, finished, error, manual, sweep, preset-sweep, protocol, G2 focus
  - the frequency layout set (§2.8), strip variants
  - menu, quick, options
  - library: quick, slots, long name, preset folders, letter jump, protocols
  - detail: slot, preset; protocol view; chain editor (moving); manual (cursor); sweep setup
  - settings: root, display, power; history; diagnostics; key test
  - PC monitor: normal, sync + sniffed
  - calibration: selection at 5, factory values
  - toast, storage error, confirm half-held, key lock, power ring, goodbye, ambient
  - engine sheets: primitives, font (all glyphs), icons, palette with contrast labels
  - Animation fixtures: digit roll, G1→G2 push, list scroll, toast in, sheet reveal, power ring, splash.
- **CI:** `make -C tests/host nx`, `nx_gallery.py compare --emu`, then the size gate (§8).

---

## 8. RAM and flash budget (UI only)

**RAM**

| Item | Bytes |
|---|---|
| Band buffers 2 × 2,560 px | 10,240 |
| Dirty list, render state, DMA queue | 220 |
| VM now + drawn (2 × ≈ 240) | 480 |
| Screen stack (4) + active screen state | 200 |
| Per-screen arena (list row cache 8 × 72 **or** detail scope 2 × 296 × 2 **or** frequency page cache) | 1,536 |
| Dashboard scope samples 2 × 136 × 2 | 544 |
| PC sparklines 2 × 64 × 2 | 256 |
| Toasts 4 × 40, animation slots 16 × 12, keys, marquee | 400 |
| **Total** | **≈ 13.9 KB** (≈ 11.4 KB with `NX_BAND_PX=1920`) |

The nx build also drops the classic `g_name_bmp` (−1,998 B). Current `.bss` is 11.2 KB and the stack minimum 2 KB, leaving ≈ 24 KB for the UI plus new core state.

**Flash (UI)**

| Component | KB |
|---|---|
| lcd + backlight + render + animation | 4.5 |
| gfx + font engine | 5.5 |
| keys + ui root + vm + fmt | 5.0 |
| Components (list, sheet, form, digits, hints, tabs) | 4.0 |
| Screens (bespoke + providers) | 13–15 |
| **Code subtotal** | **≈ 32–34** |
| Fonts (4bpp) | 18.9 (16.3 with 2bpp small fonts) |
| Icons | 6.5 |
| Strings English (+ German) | 4.0 (+4.5) |
| Tables (palettes, easing, sine, gamma, decay) | 1.0 |
| **Assets subtotal** | **≈ 30 (+4.5)** |
| **UI total** | **≈ 58–62 KB (German included ≈ 62–66)** |

- **Caps:** UI ≤ 62 KB, core ≤ 30 KB, image ≤ 96 KB.
- **Cut lines, in order:**
  1. German to SPI flash or dropped (−4.5)
  2. 2bpp 13/16 px fonts (−2.6)
  3. `-flto` (−5–8 % of code)
  4. hot loops kept out of `.ramfunc`
  5. history viewer (−1)
  6. splash simplified (−0.5)
- **Plan B:** asset pack in SPI flash (§3.6), which frees ≈ 20 KB.

---

## 9. Sequencing and risks

| Milestone | Content |
|---|---|
| **M0 device spikes** (`APP=nxbring`) | See the list below |
| **M1** | Emulator extensions (§7), host LCD backend, `lcd.c` split + Makefile per-APP assets |
| **M2** | `gen_nx_assets.py` (fonts, icons, strings, palettes, width and contrast checks, lock file, budget report), licence files, `nx_font` + reference-decoder test |
| **M3** | `nx_lcd` (polled, then DMA), `nx_gfx`, `nx_render`, `nx_bl`; gallery skeleton with primitive, font, icon and palette sheets; baselines approved |
| **M4** | `nx_keys`, `nx_ui` framework, VM/events/actions against the stub; host nav tests |
| **M5** | Parity screens: dashboard + strip, PC monitor, calibration (factory hooks), splash, goodbye, dim/off, lock, toasts, errors |
| **M6** | Library (slots, favourites, recent), program detail, settings/timers, resume, history |
| **M7** | Manual + sweep editors, scope, chains, presets, protocols (as the core store lands), diagnostics |
| **M8** | Motion polish, reduced motion, light/high-contrast/night themes, German; budget and gallery gates; device validation |

M0 device spikes, first:
1. Bootloader size spike: a ≈ 100 KB image with a filler checksum, verified at boot. This decides Plan B.
2. MADCTL 0x60 vs 0xA0 test pattern.
3. 16-bit SPI with NOP-prefixed commands.
4. DMA full-screen throughput (expect ≈ 40 ms).
5. Scroll direction and push artifacts at Δ = 8.
6. TIM17 PWM at 1 and 2 kHz: flicker, whine, lowest usable level, 100 % thermal check over 30 min.
7. SLPIN/SLPOUT keeps GRAM.
8. ADC temperature/VDD readings.
9. Physical G1/G2 arrangement.
10. Waveform-code mapping (w20/21 values incl. 41–48) on an oscilloscope.

**Top risks:**
- Bootloader image-size limit (mitigation: Plan B).
- MADCTL and scroll behaviour are unverified (mitigation: M0, and the 0xA0 alternative).
- Flash budget creep (mitigation: generic components, build-time budget gates).
- Waveform/amplitude semantics are unknown (mitigation: verified flag in the UI).
- No RTC for multi-day protocols (mitigation: explicit day model).
- FreeType output drifting between machines (mitigation: pinned Pillow + lock file).

### Critical Files for Implementation
- src/lcd.c
- Makefile
- tools/sim/devices.py
- tools/sim/periph.py
- src/classic/main_uitest.c
