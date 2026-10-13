# GX Pro display firmware – application logic specification

Scope: boot flow, key handling, UI state machine / screens, treatment-program execution,
settings & persistence, time keeping and the float math used by the application.
Hardware pin roles, LCD primitives/fonts and the serial framing are specified by the other
specs; they are referenced here by address only.

Legend: **[V]** = verified from code (decompile cross-checked with capstone disassembly where the
decompiler was unreliable), **[I]** = inferred (plausible interpretation, not provable from this
image alone). All addresses are absolute (flash `0x0800xxxx`, RAM `0x2000xxxx`). Names used below
are the ones proposed in `tools/sim/names/application.tsv`; the original `FUN_`/`DAT_` address is always
given at least once.

Timing assumptions used for all "ms" figures **[I]**: SYSCLK = 64 MHz (HSE 8 MHz → PLL ×16 /2,
`SystemClock_Config` 0x0800C564, HSE constant 8 000 000 in 0x0800BC24), flash prefetch+cache on,
busy loop `delay_loop` 0x08012518 = 4 cycles/iteration → `delay(n)` ≈ n × 62.5 ns.

---------------------------------------------------------------------------------------------------

## 1. Global time base (TIM3, 10 kHz)

TIM3 is configured with PSC = 63, ARR = 99 (0x0800B168) → update IRQ every 100 µs at 64 MHz **[V
config, I rate]**. It is started in `main` (TIM3_DIER.UIE, CR1.CEN at 0x0800E404+). Everything
time-related in the application derives from `TIM3_IRQHandler` 0x0800C604:

```
tick8++                                  // u8 0x20000008, wraps every 256 ticks = 25.6 ms
PB7  = (tick8 & 15) < pb7_pwm_duty       // 0x2000000C, 16-step PWM, 625 Hz
elapsed_prescaler = (elapsed_prescaler < 100) ? +1 : 0   // 0x2000000A, period 101 ticks = 10.1 ms
if elapsed_prescaler == 50:
    if ui_flags & 0x40: g1_elapsed++     // 0x20000024 (u32)
    if ui_flags & 0x80: g2_elapsed++     // 0x20000028 (u32)
PB12 = (tick8 & 15) < backlight_duty && !(ui_flags & 1)   // 0x2000001B; ui_flags bit0 is never set
if key_event < 0x11: key_hold_count = 0  // 0x2000001A / 0x20000009
elif tick8 == 0 && key_hold_count != 0xFF: key_hold_count++      // +1 per 25.6 ms while held
if tick8 == 100:
    if !(ui_flags & 4): marquee_offset = 0                       // 0x20000018 (u16)
    elif marquee_offset < name_bmp_width + 130: marquee_offset++  // 0x20001C88
    else: marquee_offset = 100; ui_flags ^= 0x20
```
All of the above is **[V]** (disassembly 0x0800C604–0x0800C722).

Consequences:
* Program time unit: `g1_elapsed/g2_elapsed` count in 10.1 ms steps; one "second" of program dwell
  is 100 counts = **1.01 s** **[V count, I absolute]**.
* PB12 = backlight PWM (duty 8/16 in normal operation, 0 = off) **[I role]**; PB7 = 16-step PWM
  whose duty is only changed by the factory interface (default 1/16) **[V]**, purpose unknown.
* Marquee (scrolling program names) advances 1 pixel column per 25.6 ms.
* SysTick only drives the HAL tick (`0x08009568`), unused by the application **[V]**.

---------------------------------------------------------------------------------------------------

## 2. Boot flow – `main` 0x0800E404

| # | Call / action | Meaning |
|---|---|---|
| 1 | `SCB_VTOR = 0x08004000` | vector table of the app (bootloader occupies 0x08000000–0x08003FFF) [V] |
| 2 | 0x08009580 | HAL_Init (FLASH_ACR prefetch, HAL tick) [V] |
| 3 | 0x0800C564 | SystemClock_Config → 64 MHz from 8 MHz HSE [V/I] |
| 4 | 0x0800AD9C | GPIO init (keys PB3–PB5, PB11, PD0–PD3 inputs with pull-up; PB1 input pull-up; PB2, PB7, PB12 outputs …) – see hardware spec [V] |
| 5 | 0x0800B218, 0x0800AFD0, 0x0800B0A8, 0x0800B168, 0x0800B304, 0x0800B3D4 | USART1, SPI1 (SPI-NOR flash), SPI2 (LCD), TIM3, USART2, USART3 init [V] |
| 6 | `GPIOB_BRR = 1` | PB0 low [V] |
| 7 | TIM3 DIER/CR1 | start 10 kHz tick (backlight still 0 → dark) [V] |
| 8 | 0x0800ABEA/0x0800ABDE ×3, SPI1 FRXTH, 0x0800AA6E ×2 | USART1/2/3 CR1 \|= RXNEIE (0x20) and PEIE (0x100); SPI1 CR2.FRXTH; SPI1/SPI2 CR1.SPE [V] |
| 9 | `delay(0x82355)` | ≈33 ms [I] |
| 10 | 0x0800A188 | LCD controller init (display spec) [V] |
| 11 | `app_state_init` 0x0800DE2C | list_top = list_sel = 1; `names_load` (SPI flash 0x5000); both channel program buffers set to defaults (hdr = 0,0,120,1,1; all steps 0); `cal_load` (SPI flash 0x74000); `pb7_pwm_duty = 1` [V] |
| 12 | `lcd_fill_rect(0,0,239,319,0)` | clear to black [V] |
| 13 | `backlight_duty = 8` | backlight on, 50 % [V] |
| 14 | `splash_draw` 0x080054F4 | 5 monochrome bitmaps (white/red on black) = boot logo [V] |
| 15 | `key_scan()` 0x0800E0F8 | boot-time key check, see below [V] |
| 16 | if `ui_state == 0xFF`: `PB2 = 1`, `cal_apply_test()` 0x08011468 | calibration mode: test signal for index 0 [V] |
| 17 | `factory_first_boot_wipe` 0x0800DDF0 | if internal flash dword @0x08003D00 ≠ 0: erase SPI-flash sectors 0x74000 and 0x75000 (calibration + backup), then program 0 at 0x08003D00 [V]; "first boot after full-chip programming" [I] |
| 18 | `license_check_uid` 0x0800DD4C | Vendor licence check bound to the device (sets `pc_flags.0x400` on failure). **Excluded from scope**: the open firmware has no device-binding check (the flag is never set) and never writes 0x08003E00. |
| 19 | `flash_ensure_rdp1` 0x0800BC84 | read option bytes; if RDP level < 0xBB → program RDP level 1 and launch option-byte reload (MCU resets) [V code, I effect] |
| 20 | `main_loop` 0x0800E55E | never returns [V] |

### 2.1 Boot-time key check (step 15)
* `key_scan() == 8` (keys 6 **and** 7 held together, see §3) **[V]**:
  1. `ui_state = 0xFF` (calibration menu).
  2. Busy-wait until all keys are released (`while key_scan() > 0`).
  3. `ui_flags |= 2` (full redraw), send `":r80=0,"` on USART1 (G1 link, 0x0800C268).
  4. Clear screen; if a complete USART1 line is already pending (`ui_flags & 0x400`) process it
     now (0x0800F22C – the expected answer is `:w80=13583792,c0..c20,` which loads cal[0..20]).
  5. `cal_draw_values` 0x0800DED4 draws the 29 calibration numbers.
  6. Then step 16 above (PB2 high, test signal index 0: all four outputs on, 1 234 560 Hz, 15.00 amplitude).
* Otherwise: wait up to 100 × `delay(640000)` (≈ 4.0 s) with the splash visible, aborting early as
  soon as a factory command has been received on USART3 (`factory_cmd_flags & 1`, 0x20000014)
  **[V]**. `tmp_counter` 0x20000074 is the loop counter.

### 2.2 First main-loop iterations
`ui_state` is 0 after reset. Iteration 1: state 0 → `ui_state = 1` (HOME) if PB1 = 0, otherwise the
PC-mode entry path runs (→ 0x10). Because `g1_step_sent`/`g2_step_sent` start at 0 and a default
program has count 1, the program runner (§6) immediately computes step 1 ≠ 0 and sends the **idle
configuration** to both generators (10 commands each, outputs off) **[V]** – i.e. every boot into
HOME initialises both generator boards. No program is loaded after power-up (slots 0x2000001D/1E = 0).

---------------------------------------------------------------------------------------------------

## 3. Keys

### 3.1 Scan – `key_scan` 0x0800E0F8 [V]
All keys are active-low GPIO inputs with pull-ups. The function first computes
`((GPIOB_IDR & 0x38) << 1) | (GPIOD_IDR & 0xF)` and compares it with 0xFF to decide whether to wait
– that value can never be 0xFF (max 0x7F), so it **always** executes `delay(0x34155)` ≈ 13.3 ms,
then re-reads and returns the first matching code in this priority order:

| Code | Condition (low = pressed) | Proposed label [I] | Evidence |
|---|---|---|---|
| 8 | PB3 **and** PD2 | "G1+G2" combo | boot → calibration |
| 10 | PB11 **and** PB5 | "Power+Stop" combo | unused by app; matches the updater's "hold Stop then Power" bootloader combo |
| 1 | PD0 | LOOP / "Play repeat" | starts program with repeat flag |
| 2 | PD1 | START/PAUSE (OK) | start/pause, confirm in list, save in cal menu |
| 3 | PB5 | STOP / BACK | stop channel, leave list |
| 4 | PB4 | UP | list −1, cal index −1 |
| 5 | PD3 | DOWN | list +1, cal index +1 |
| 6 | PB3 | G1 (channel 1) | G1 screen/list, cal value + |
| 7 | PD2 | G2 (channel 2) | G2 screen/list, cal value − |
| 9 | PB11 | POWER | long hold = power off, short = stop both channels |
| 0 | none | | |

PB6 and PB10 are configured as pulled-up inputs but never read by the application **[V]**.

### 3.2 Event model – top of `main_loop` [V]
`key_event` (0x2000001A) is updated once per main-loop iteration:
```
k = key_scan()                        // 13.3 ms
if k == 0:
    if key_event != 0:
        if key_event < 0x20: key_event |= (key_hold_count < 16) ? 0x20 : 0x40   // release event
        else: key_event = 0
elif key_event == 0: key_event = key_scan()   // 2nd scan (another 13.3 ms): press event = code 1..10
else: key_event |= 0x10                       // held
```
* **Press** event `k` (1…10) is delivered for exactly one iteration, and only if the key is seen in
  two consecutive scans (implicit debounce).
* **Held** `k|0x10` every following iteration; the low nibble keeps the first key's code.
* **Release** `k|0x30` (short, <16 hold counts ≈ 410 ms) or `k|0x50` (long) for one iteration, then 0.
  Release events are **not used** by any handler.
* `key_hold_count` counts 25.6 ms periods while `key_event ≥ 0x11` (TIM3, §1).
* **Auto-repeat** (list, keys 4/5): event `0x14/0x15` acts when `key_hold_count > 10` (≈ 0.28 s) and
  `tick8 < 100` (first 10 ms of each 25.6 ms window). In the calibration menu keys 6/7 repeat
  (`0x16/0x17`) when `key_hold_count > 9` and `tick8 > 99`.
* **Power off**: evaluated before the state dispatch, in every state: `key_event == 0x19` (POWER
  held) and `key_hold_count > 30` (≈ 0.8 s after hold start) → §8.

---------------------------------------------------------------------------------------------------

## 4. RAM state

| Address | Name | Type | Meaning [V unless noted] |
|---|---|---|---|
| 0x20000000 | ui_prev_state | u8 | state rendered last (full frame only drawn when it changes, states 2/3/4) |
| 0x20000002 | marquee_prev_offset | u16 | marquee offset last drawn |
| 0x20000008 | tick8 | u8 | TIM3 counter |
| 0x20000009 | key_hold_count | u8 | 25.6 ms hold counter |
| 0x2000000A | elapsed_prescaler | u8 | 0..100 |
| 0x2000000C | pb7_pwm_duty | u8 | 0..16 (default 1) |
| 0x2000000E | ui_flags | u16 | see below |
| 0x20000010 | pc_flags | u16 | see below |
| 0x20000012 | misc_flags | u16 | bit0 cal-edit direction inverted; bits1..4 PC-mode output enabled G1o1,G1o2,G2o1,G2o2 |
| 0x20000014 | factory_cmd_flags | u16 | bit0 = USART3 command pending (set by 0x0800D7D8) |
| 0x20000018 | marquee_offset | u16 | |
| 0x2000001A | key_event | u8 | §3.2 |
| 0x2000001B | backlight_duty | u8 | 0..16; 8 = on |
| 0x2000001C | ui_state | u8 | 0 boot, 1 HOME, 2 LIST, 3 CH1, 4 CH2, 0x10 PC, 0xFF CAL |
| 0x2000001D/1E | g1_prog_slot / g2_prog_slot | u8 | loaded program number 1..30, 0 = none |
| 0x2000001F | list_top | u8 | first visible list entry (1..24) |
| 0x20000020 | list_sel | u8 | selected list entry (1..30) |
| 0x20000024/28 | g1_elapsed / g2_elapsed | u32 | 10.1 ms units; 0 = freshly loaded, 1 = stopped/reset |
| 0x20000030/38 | pc_g1_out1_freq / pc_g1_out2_freq | u64 | frequencies sniffed from USART1 (`w24`/`w25`) |
| 0x20000040/48 | pc_g2_out1_freq / pc_g2_out2_freq | u64 | same from USART2 |
| 0x20000050/58 | g1_step_freq / g2_step_freq | u64 | frequency of the current step |
| 0x20000060/62 | g1_step / g2_step | u8 | current step (1-based, 0 = none) |
| 0x20000061/63 | g1_step_sent / g2_step_sent | u8 | step last sent to generator; 0xFF forces resend |
| 0x2000006E | cal_freq_exp | u8 | cal test frequency selector 0 or 1..9 |
| 0x2000006F | cal_ampl_exp | u8 | cal test amplitude selector 0 or 4..10 |
| 0x20000074 | tmp_counter | u16 | boot wait counter; also scratch in 0x0800F22C/0x0800F9C4 |
| 0x2000007C | cal_index | u8 | 0..28 |
| 0x2000007E | cal_table | s16[32] | calibration values, [31] = 31 marker |
| 0x200001F8 | name_table | char[32][60] | program names; slot 0 = PC G1 name, 31 = PC G2 name |
| 0x20000978 | g1_program | 0x668 B | active G1 program record (§6.1) |
| 0x20000FE0 | g2_program | 0x668 B | active G2 program record |
| 0x20001648 | g1_step_params | u16[400] | per-step (a,b) pairs of G1 |
| 0x20001968 | g2_step_params | u16[400] | per-step (a,b) pairs of G2 |
| 0x20001C88 | name_bmp_width | u16 | width (columns) of rendered name |
| 0x20001C8A | name_bmp | u8[1998] | rendered name bitmap, 3 bytes/column (24 px) |

Initial values (RW data from 0x080162E0) are zero except `cal_table` [V]:
`32005, 1662,316,496,1050,638, 1662,316,496,1000,638, 1680,316,496,1250,638, 1680,316,496,1120,638,
21,22,23,24,25,26,27,28,29,30,31`.

`ui_flags` (0x2000000E) bits [V]:

| bit | meaning |
|---|---|
| 0x0001 | backlight force-off (never set) |
| 0x0002 | full redraw request (cleared by `ui_render`) |
| 0x0004 | marquee active (name drawn and scrollable) |
| 0x0008 | LIST target is G2 (else G1) |
| 0x0010 | last LIST move was "up" (highlight bookkeeping) |
| 0x0020 | PC mode: which name (0 = G1 slot 0, 1 = G2 slot 31) scrolls; toggled at each marquee wrap |
| 0x0040 / 0x0080 | G1 / G2 running |
| 0x0100 / 0x0200 | G1 / G2 repeat (loop) mode |
| 0x0400 / 0x0800 | complete line pending from USART1 / USART2 |
| 0x1000 / 0x2000 | PC mode G1 / G2 "sync": out2 displays out1's frequency (`w14`) |
| 0x4000 | PC session unlock (vendor handshake via `w91`, excluded from scope). Reimplementation: treated as always unlocked, so serial writes are always allowed |
| 0x8000 | "erase all programs" already executed this power cycle |

`pc_flags` (0x20000010): bits 0x01/0x02/0x04/0x08 = PC-mode frequency field G1o1/G1o2/G2o1/G2o2
up to date (cleared → redraw), 0x10/0x20 = name slot 0/31 up to date, 0x40/0x80 (G1) and
0x100/0x200 (G2) = stored `w15` values (never read elsewhere), 0x400 = vendor licence flag (excluded; always clear in the reimplementation) [V].

---------------------------------------------------------------------------------------------------

## 5. Main loop and state machine – `main_loop` 0x0800E55E

The decompilation shows the loop as a tail self-call; it is an endless loop **[V]**. One iteration:
```
1. key event update (§3.2)                                   (≥13.3 ms, 26.7 ms on a new press)
2. power-off check (§8)
3. PC-mode entry/exit, then key dispatch by ui_state (§5.1)
4. if ui_state ∉ {0x10, 0xFF}: program_runner_tick()  0x0800B564 (§6)
5. if ui_flags & 0x400: process USART1 line  0x0800F22C (§7)
6. if ui_flags & 0x800: process USART2 line  0x0800F9C4 (§7)
7. ui_render()  0x080065D8 (§5.3)
8. (vendor licence gating of the PC unlock – excluded from scope, not reimplemented)
9. if factory_cmd_flags & 1: factory_cmd_execute()  0x0800522C (§9.3)
```

### 5.1 Transitions [V]
**PC-mode entry** (checked first, any state incl. CAL): if PB1 = 1 and `ui_state ≠ 0x10`:
PB2 = 1; `ui_state = 0x10`; clear running and loop flags of both channels (`ui_flags &= 0xFC3F`),
`g1/g2_elapsed = 0`, `g1/g2_prog_slot = 0`; redraw; `delay(0x34155)`; send `:w11=0,0,` to G1 (wait ack),
then G2 (wait ack); `delay(0x34155)`; PB2 = 0. Keys are ignored in state 0x10 (except power off).

**PC-mode exit**: if PB1 = 0 and `ui_state == 0x10`: `ui_state = 1`; PB2 = 1; send `:w15=0,0,` to G1
and G2 (each + ack wait); `pc_flags &= 0xFC3F`; the four PC frequencies = 0; redraw.

(PB1 is read directly from GPIOB_IDR bit 1; its physical meaning – most likely "PC/USB connected" –
is for the hardware spec [I]. PB2 behaves like "display owns the generator UART lines" [I].)

Key handling per state (`E(n)` = key_event == n):

| State | Event | Action |
|---|---|---|
| 0 BOOT | – | `ui_state = PB1 ? 0x10 : 1`, redraw |
| 1 HOME | E(6) | → LIST for G1 (`ui_flags &= ~8`), redraw |
| | E(7) | → LIST for G2 (`ui_flags \|= 8`), redraw |
| 2 LIST | E(2) | load selected program into target channel: G1 → `ui_state=3`, `g1_step_sent=g2_step_sent=0xFF`, clear G1 running, `g1_elapsed=0`, `program_load(list_sel,1)`, `g1_prog_slot=list_sel`; G2 analog (state 4, slot 0x1E, `program_load(list_sel,2)`); redraw |
| | E(3) | back without loading → state 3 (G1 target) or 4 (G2 target), redraw |
| | E(6) / E(7) | switch list target to G1 / G2 (stay in LIST), redraw |
| | E(4) or (E(0x14) & repeat) | `if list_sel ≠ 1: list_sel--`; `if list_sel < list_top: list_top = list_sel`; `ui_flags \|= 0x12` |
| | E(5) or (E(0x15) & repeat) | `if list_sel ≠ 30: list_sel++`; `if list_sel > list_top+6: list_top++`; `ui_flags = ui_flags & ~0x10 \| 2` |
| 3 CH1 | E(6) | → LIST for G1 |
| | E(7) | G2 loaded ? → state 4 : → LIST for G2 |
| | E(2) | not running: `g1_step_sent=0xFF`, set running, **clear** loop; running: `g1_step_sent=0xFF`, clear running and loop (pause) |
| | E(3) | STOP: `g1_step_sent=0xFF`, `g1_elapsed=1`, clear running and loop |
| | E(9) | STOP BOTH: `g1/g2_step_sent=0xFF`, `g1/g2_elapsed=1`, clear both running flags (loop flags kept) |
| | E(1) | not running: `g1_step_sent=0xFF`, set running **and** loop; running: `g1_step_sent=0xFF`, clear running, set loop (pause) |
| 4 CH2 | E(6) | G1 loaded ? → state 3 : → LIST for G1 |
| | E(7) | → LIST for G2 |
| | E(2),E(3),E(1) | as CH1 but for G2 (bits 0x80/0x200, `g2_step_sent`, `g2_elapsed`) |
| | E(9) | STOP BOTH (identical to CH1) |
| 0xFF CAL | see §9.1 | |
| 0x10 PC | – | no key handling |

All CH1/CH2 actions set the redraw flag. There is no key-driven exit from CAL (only USART3 `:aN`,
PB1 → PC mode, or power cycle) **[V]**.

### 5.2 Screens
Coordinates are firmware coordinates (x = 0..239 short axis, y = 0..319 long axis, text advances
toward decreasing y); the physical orientation is defined in the display spec. Colours RGB565:
TEAL 0x2B12, GREY 0x8430, WHITE 0xFFFF, BLACK 0x0000, YELLOW 0xFFE0, RED 0xF800, NAVY 0x0016.

Common elements:
* **Channel panel** G1 at x 0x14..0x72, G2 at x 0x82..0xE0, y 0x14..~0x12B: TEAL title bar
  (25 px, rounded top) + body. Inactive body = GREY (0x08007A00 with flag 1); active panel =
  WHITE rounded box with TEAL border (0x080086A4) + TEAL title bar; channel badge = white disc r=15
  at (0x20,0x126)/(0x8E,0x126) (active: TEAL ring r=17) with glyph '1'/'2' in NAVY
  (0x08005AE4, x 0x18/0x87 or 0x86, y 0x11C/0x11B).
* **Title text** at (0x14,0x110) / (0x82,0x110), WHITE on TEAL: program name via the name
  marquee (`name_render_bitmap` 0x0800DC90 + `name_draw_window` 0x08006450), or the literal
  `"Choose Your Treatment Program"` (0x080069EC) when the channel has no program.
* **Status line** of a loaded channel (G1: icon (0x37,0x1E), freq (0x41,0x122); G2: icon
  (0xA5,0x1E), freq (0xAF,0x122)); colours: active channel bg WHITE / fg TEAL, inactive bg GREY /
  fg WHITE. Icon (0x08006070, 48×51 px bitmaps at 0x08012F7D keyed by letter):

  | condition | icon | frequency shown |
  |---|---|---|
  | not running, elapsed == 0 | 'c' (freshly loaded) | 0 → "0.00000000 Hz" |
  | not running, elapsed == 1 | 's' (stopped) | 0 |
  | not running, other | 'p' (paused) | 0 |
  | running | 'r' (running) | `gX_step_freq` |

* **Loop badge** 16×16 (0x08005B74, bitmap 0x0801357D) at (0x19,0x19) for G1, (0x87,0x19) for G2:
  YELLOW background/TEAL glyph when the loop flag is set, all TEAL (invisible) otherwise.
* **Frequency format** (`draw_frequency` 0x08005BDC) **[V]**: value v (u64) is decoded as
  `m = v/10, e = v%10, n = m·10^e`; n is printed as 16 digits = 8 integer + 8 fraction digits, i.e.
  **Hz = m · 10^(e−8)** (e.g. `3750007` → 37 500 Hz, `123456007` → 1 234 560 Hz). Integer digits
  0..6 that are leading zeros: even positions are drawn as '0' in the background colour (keeps an
  11 px slot), odd positions are skipped and counted; position 7 is always drawn. Then '.', 8
  fraction digits, ' ', 'H', 'z', then one background-coloured '0' per skipped digit (erases stale
  pixels). Advances: digit 11 px, '.' 6, ' ' 3, 'H' 10, 'z' 8. Font 0x08014671 (display spec).

Screens by state (`ui_render` 0x080065D8; a full screen is drawn only when `ui_flags & 2`,
otherwise only `ui_render_incremental` 0x0800773E runs) **[V]**:

| State | Full render |
|---|---|
| 1 HOME | clear (0x73,0x14)-(0x82,0xB4); erase PC icon (4,5); `home_frame_draw` 0x080056B8 (both panels inactive-style GREY); "Choose Your Treatment Program" in both title bars. Always drawn completely. |
| 2 LIST | if previous state ≠ 2: clear (10,0x14)-(0x27,0x138), (0x28,0x119)-(0xA0,0x13B), (0xD7,0x14)-(0xE6,300); WHITE box (0x28,0x14) h 0xAF w 0x118. Header `"Program  G1"`/`"Program  G2"` (0x08006A10/0x08006A1C) at (0x0F,200) WHITE on BLACK. `list_highlight_draw` 0x08005760 (new row TEAL bar, previous row WHITE; rows x = 0x28+25·i, i=0..6; rows 1/7 rounded). `program_list_draw` 0x0800620C: 7 rows, each "NN." (two digits of list_top+i at y 0x122/0x119, '.' at 0x110) + name at y 0x109; selected row WHITE on TEAL, others BLACK on WHITE. Marquee enabled for the selected row. |
| 3 CH1 | if previous ≠ 3: clear (0x73,0x14)-(0x82,0x13B), `frame_draw_g1_active` 0x08005A28, G2 title (name or "Choose…"), G1 title (name, marquee). Always: G2 status (inactive colours) if G2 loaded, G1 status (active colours) if G1 loaded, both loop badges. |
| 4 CH2 | mirror of CH1 (`frame_draw_g2_active` 0x0800596C; G1 inactive, G2 active; marquee on G2). |
| 0x10 PC | clear (0x73,0x14)-(0x82,0x13C); PC-link icon (0x08006104, 16×33 at (4,5), bitmap 0x0801344A) with background RED (`ui_flags.0x4000`=0) or WHITE (=1); `frame_draw_pc_mode` 0x0800B498 (both panels active-style); names of slot 31 (G2) and slot 0 (G1) in the title bars (the one selected by `ui_flags.0x20` last, marquee on it); output labels '1','2','1','2' (0x08005E44) at x 0x3A,0x55,0xA8,0xC3, y 0xFA; the four frequencies at the same rows, y 0xF5, GREY on WHITE (full render always uses `pc_g1_out2_freq` even in sync mode); sets `pc_flags` bits 0..3. |
| 0xFF CAL | switch on `cal_index` (switch table at 0x08006F56): redraw the selected value highlighted (BLACK on WHITE) and its neighbours normal (WHITE on BLACK). The value grid itself is drawn by `cal_draw_values` 0x0800DED4 (§9.1). |
| other (0) | nothing but the incremental part |

`ui_render_incremental` 0x0800773E (also the tail of every full render) **[V]**:
1. If marquee active and `marquee_offset ≠ marquee_prev_offset`: redraw the scrolling name
   (LIST: re-render name of `list_sel` at row x = (list_sel−list_top)·25+40, y 0x109, WHITE on
   TEAL; CH1: G1 title; CH2: G2 title; PC: slot 0 or slot 31 depending on `ui_flags.0x20`).
2. PC mode only: for each cleared `pc_flags` bit redraw that field: frequencies in TEAL if the
   output is enabled (`misc_flags` bits 1..4) else GREY; out2 shows out1's value when the channel is
   in sync mode (0x1000/0x2000); names slot 0/31.
3. `marquee_prev_offset = marquee_offset; ui_prev_state = ui_state; ui_flags &= ~2`.

**Name marquee** [V]: `name_render_bitmap(slot)` 0x0800DC90 renders characters 58..0 of the
60-byte name (reverse order, NUL bytes skipped, unknown characters skipped) using the 86-glyph
24-px font at 0x080135A5 (entry = code, width, 48 bitmap bytes), appending `width·3` bytes each;
`name_bmp_width` = total columns; rest of the buffer up to 0x20002458 is zeroed. `name_draw_window`
draws a 230-column (0xE6) window: if width ≤ 230 → padded static text; if width > 230 and
`marquee_offset < 100` → static beginning (≈2.56 s pause, only after a full redraw because wraps
reset the offset to 100); else circular scroll with a 30-column gap, 1 column per 25.6 ms.
Note: a 59-character name of wide glyphs can exceed the 1998-byte buffer (the original then
overflows into 0x20002458).

**Backlight writes**: several renders write PB12 directly (BRR before/BSRR after drawing the LIST
frame, BSRR after other frames); since TIM3 rewrites PB12 every 100 µs these writes have no
lasting effect **[V/I]**.

---------------------------------------------------------------------------------------------------

## 6. Treatment programs

### 6.1 Data model [V]
Each channel has one active program record in RAM (G1 0x20000978, G2 0x20000FE0) plus a per-step
parameter table (G1 0x20001648, G2 0x20001968). Stored programs live in the external SPI-NOR flash
(SPI1, CS = PA1, commands 0x03 read / 0x20 4 KB erase / page program – hardware spec); **30 slots**
(1..30), slot n at `0x10000 + n·0x1000`:

| Offset | Type | Field | Default | Sent as |
|---|---|---|---|---|
| 0x000 | u64 | hdr0 – waveform [I] | 0 | `:w20=`, `:w21=` (both outputs) |
| 0x008 | u64 | hdr1 – amplitude, 0.01 V units [I] | 0 | `:w28=`, `:w29=` |
| 0x010 | u64 | hdr2 – offset/bias parameter [I] | 120 | `:w32=`, `:w33=` |
| 0x018 | u64 | dwell per step, seconds (×1.01 s) | 1 | – |
| 0x020 | u64 | step count, valid 1..200 | 1 | – |
| 0x028 | u64[200] | step frequency 1..200 (encoding §5.2) | 0 | `:w24=` (out1 only) |
| 0x668 | (u16 a, u16 b)[200] | per-step extra parameters; 0xFFFF = none | – | `:w50=aaaaa,bbbbb,` `:w51=aaaaa,bbbbb,` `:w12=1,1,` or `:w12=0,0,` |

`program_load(slot, ch)` 0x0800E2E4: reads 0x668 bytes; if `count == 0 || count > 200 || dwell == 0`
(64-bit compares; erased flash qualifies) the header is replaced by the defaults and all steps are
zeroed; then the 800-byte step-parameter table is read unconditionally [V].
Programs are **never** built into the firmware – they only come from SPI flash, written over the
serial links (§7) [V].

### 6.2 Runner – `program_runner_tick` 0x0800B564 (every main-loop iteration unless state 0x10/0xFF) [V]
```
for ch in (G1, G2): step[ch] = (count==0) ? 0 : (u8)(elapsed[ch] / (dwell·100)) + 1   // 64-bit
for ch in (G1, G2):                      // G1 block completely, then G2 block
  if count_hi == 0 and step > count_lo:            // program finished
      if !loop[ch]:
          if running[ch]: send ":w11=0,0," ; wait_ack
          running[ch] = 0
      if ui_state != LIST: redraw
      elapsed[ch] = 1                                // restart point (loop keeps running)
  elif step != step_sent[ch] and step != 0:        // new step (or forced by step_sent = 0xFF)
      step_freq[ch] = freq[step]; step_sent[ch] = step
      replace 0xFFFF in params[step].a / .b by 0
      if !running[ch]:                             // idle configuration, outputs off
          w24=freq, w11=0,0, w14=1, w17=0,1, w20=hdr0, w21=hdr0, w28=hdr1, w29=hdr1, w32=hdr2, w33=hdr2
      else:                                        // playing
          w24=freq, w11=1,1, then (a,b)==(0,0) ? w12=0,0 : (w50=a,b ; w51=a,b ; w12=1,1)
      if ui_state not in {LIST, PC}: redraw
```
Each command is sent with the channel's send routine (G1: 0x0800C268/0x0800C084/0x0800C140 on
USART1; G2: 0x0800C4AC/0x0800C2C8/0x0800C384 on USART2) followed by `wait_ack` 0x08012524 (waits
for ":ok" – `0x2000000B == 2` – or ≈30 ms timeout, then 133 µs) **[V code, I timing]**. The number
encodings (`:wNN=<u64 decimal>,`) belong to the serial spec.

Behavioural consequences:
* Step k (1-based) is active while `(k−1)·dwell·100 ≤ elapsed < k·dwell·100`; after STOP the first
  step is one tick shorter (elapsed restarts at 1).
* Starting/resuming (`step_sent = 0xFF`) resends the current step with outputs on; pausing resends it
  with outputs off plus the full idle configuration. Waveform/amplitude/offset are only (re)sent in
  the idle configuration, i.e. at load, pause, stop and program end – never while playing.
* Loading a program immediately sends the idle configuration of step 1 (state LIST → 3/4 sets both
  `step_sent` to 0xFF, so the **other** channel's current step is resent too).
* Loop mode: at the end elapsed = 1 → step 1 is resent with outputs on (no resend if count == 1).
* Without loop: outputs off, icon 's', frequency hidden.
* Both channels run independently and simultaneously; the runner keeps running while the user is in
  the LIST (redraws suppressed there).
* **Original behaviour [V]**: in the G2 block the per-step parameter table is indexed with `g1_step`
  (0x20000060) instead of `g2_step` (disassembly 0x0800B92A.., 0x0800B9E6..): G2's `w50/w51/w12`
  values come from G2's table at G1's step index (0 → entry 0 = bytes before the table when G1 has
  no step).
* If `dwell == 0` in RAM (possible after a serial upload, §7) the division is by zero (Keil
  `__aeabi_uldivmod` returns without dividing) – undefined step behaviour.

---------------------------------------------------------------------------------------------------

## 7. Serial-triggered application behaviour (framing: see serial spec)

USART1 = G1 link, USART2 = G2 link (same command set, per-channel buffers); the display both
drives the generators (`:w…` commands) and receives lines from that link. Parsing happens in
`USART1_IRQHandler` 0x0800C758 / `USART2_IRQHandler` 0x0800CF90; lines are executed partly in the
ISR and partly in the main loop by 0x0800F22C (USART1) / 0x0800F9C4 (USART2). "Allowed" below means
`(ui_flags & 0x4000) || PB1 == 0` **[V]**.

Program storage (all [V]):

| Line | ISR effect | Main-loop effect / reply |
|---|---|---|
| `:gNN=v0,…,v399` | stores u16 values into the channel's **active** step-parameter table (empty field → 0xFFFF), regardless of permission | allowed & NN∈1..30: `step_sent=0xFF` (resend), reply `:ok\r\n`; else `:err\r\n` |
| `:pNN=v0,…` | stores u64 values into the channel's **active** program record (index 0..4 header, 5.. steps; max index 204) regardless of permission | allowed & NN∈1..30: `program_save(NN,ch)` 0x080113EC (erase sector, write record 0x668 B + step table 800 B), `step_sent=0xFF`, `:ok`; else `:err` |
| `:nNN=$` | – | `:Rev201\r\n` (version; always allowed if NN ≤ 30) |
| `:nNN=*` | – | `:pNN=` + `program_dump` (0x08010DB0/0x0801101C: header + `count` frequencies as `v,` each, only if 1 ≤ count ≤ 200) + `\r\n` |
| `:nNN=#` | – | `:gNN=` + `steptable_dump` (0x08010BE8/0x08010CCC: first **200** u16 only, if entry 0 ≠ 0xFFFF) + `\r\n` |
| `:nNN=,` | – | erase slot NN's flash sector (name is **not** removed), `:ok` |
| `:nNN=?` | – | `:nNN=<name>\r\n` (USART2: NN = 0 means slot 31) |
| `:nNN=<text>` | – | `name_set(NN,text)` 0x080124D8 (≤60 chars); if NN∈1..30 `names_save` 0x08011380 (whole table to 0x5000, slots 0/31 zeroed); USART2 NN = 0 → slot 31; mark PC name dirty; `:ok` |

`'n'` requests other than `$` require "allowed" and NN ≤ 30, otherwise `:err`.
Uploading therefore replaces the **currently active** program data of that channel (even when the
reply is `:err`) – original behaviour.

PC-mode display (ISR, only when allowed) [V]: `w11=a,b` → output-enabled colours
(`misc_flags`), `w14=0|1` → sync flag 0x1000/0x2000, `w15=a,b` → stored only, `w24=f` → out1 freq
(and out2 in sync mode), `w25=f` → out2 freq (ignored in sync mode).

Main-loop `:w` commands (value = first field) [V]:

| Cmd | Condition / magic | Effect |
|---|---|---|
| w00 | – | original: write-once value at 0x08003F00 (bootloader page; never read by the app). **Reimplementation: acknowledge but do not write** (bootloader region is off-limits) |
| w62 (USART1) | allowed & 3336333 | `cal_restore_backup` 0x08011288 (copy 0x75000 → RAM, rewrite 0x74000 if different) |
| w60 (USART2) | USART**1** value buffer = 1112111 | `cal_table[f1] = f2` (the original reads the USART1 buffer); always `misc_flags = 0` |
| w63 | allowed & 111999 / 111000 | `cal_table[0]` +1 (max 32100) / −1 (min 31900), `cal_save(0)` |
| w64 | 18888881 / 10000001 | `backlight_duty = 8` / `0` (display on/off) |
| w80 | 13583792 | `cal_table[0..20] = fields 1..21` (no redraw) |
| w91 | (vendor handshake values excluded) | PC-unlock handshake – **excluded from scope**. Reimplementation: accept any value, PC icon always WHITE (unlocked), PB2 = 0 as in the original |
| w92 | – | delay, PB2 = 1, delay, send `:w91=0,` back on the same link |
| w95 | 12021 | NVIC system reset |
| w96 | 12321, once per boot | erase slots 1..30 and the name table (flash + RAM names), set 0x8000 |
| w97 | allowed & 12621 | reset the channel's PC display: its two frequencies = 0, sync on, dirty, names slot 0 and 31 cleared |

---------------------------------------------------------------------------------------------------

## 8. Power off [V]
Triggered by POWER held ≈0.8 s (§3.2), in any state:
`backlight_duty = 0`, PB12 = 0, clear screen, `poweroff_screen_draw` 0x08005ED0 (5 bitmaps: four
BLACK-on-WHITE bitmaps stored as one u16 per 8 pixels (low byte used) at 0x08014B00, 0x0801532A,
0x0801550A, 0x08015B16, plus the RED-on-BLACK element 0x080126AA also used by the splash),
`backlight_duty = 8`, PB12 = 1, `delay(64 000 000)` ≈ 4 s,
then forever: PB2 = 0, `delay(213)`, PB2 = 1, `delay(213)` (≈37 kHz square wave on PB2 [I]). TIM3
keeps running, so the goodbye screen stays lit. No flash writes happen at power off. Power-on is a
hardware function (the app has no power-on key logic) [I].

---------------------------------------------------------------------------------------------------

## 9. Calibration / factory functions

### 9.1 Calibration menu (ui_state 0xFF) [V]
Entered by holding G1+G2 at boot (§2.1) or USART3 `:a0`. Screen: 29 five-digit numbers
(`draw_number5` 0x08006384, WHITE on BLACK), positions (x,y):

| cal index | positions |
|---|---|
| 0 | (0x46,0xB2) |
| 1..5 | x 0x6E,0x87,0xA0,0xB9,0xD2 at y 0x12A |
| 6..10 | same x at y 0xDA |
| 11..15 | same x at y 0x8A |
| 16..20 | same x at y 0x3A |
| 21,22 / 23,24 / 25,26 / 27,28 | x 0x0A,0x23 at y 0x12A / 0xDA / 0x8A / 0x3A |

Before dispatch each iteration: `misc_flags.bit0 = (1 ≤ cal_index ≤ 20) && ((cal_index−1) % 5 ∈ {2,3})`
(direction inversion for the 3rd/4th value of each group).

| Event | Action |
|---|---|
| E(5) | `cal_index = (cal_index == 28) ? 0 : +1`; `cal_apply_test()`; redraw |
| E(4) | `cal_index = (cal_index == 0) ? 28 : −1`; `cal_apply_test()`; redraw |
| E(6) / E(0x16)* | value += 1 (−1 if inverted); send `:w60=1112111,II,VVVVV,` on USART1 (0x0800BFA8); redraw |
| E(7) / E(0x17)* | value −= 1 (+1 if inverted); same |
| E(2) | save: `cal_index == 26` → `cal_save(1)` + `:w61=1114111,`; else `cal_save(0)` + `:w61=1113111,` (USART1) |
| E(1) | `cal_ampl_exp`: 0→4, 10→4, else +1; `cal_apply_test()` |
| E(3) | `cal_freq_exp`: 0→1, 9→1, else +1; `cal_apply_test()` |

(*) repeat condition §3.2. `cal_save(backup)` 0x08011310 compares RAM with 0x74000 and only if
different erases/writes 0x74000 and, when `backup`, also 0x75000. `cal_table` is never used by the
display itself – it is only displayed, stored and sent to G1 [V].

`cal_apply_test` 0x08011468 sends a fixed test set per `cal_index` (0: all outputs on, w32/w33=120,
w24/w25 = 123456007, w28/w29 = 1500 on both links; 1..5: G1 out1; 6..10: G1 out2; 11..15: G2 out1;
16..20: G2 out2; 21: G2 off; 28: all off; 22..27: nothing). Test frequency (`cal_freq_exp` = n ≠ 0):
`(u64)(pow(2.0, n) · 200000.0 + 8.0)` = 40 kHz·2^(n−1) with exponent digit 8 (n=1..9 → 40 kHz …
10.24 MHz); otherwise `3750007` (37.5 kHz). Test amplitude (`cal_ampl_exp` = n ≠ 0):
`(u64)pow(10.0, n/3.0)` → 21, 46, 100, 215, 464, 1000, 2154; otherwise 1200 [V formula].
`cal_apply_test_jig` 0x08011C2C is the same sequence with additional waveform commands
(`w20/w21=0|1`, `w36/w37=0`) and without `w13`; it is used only by USART3 `:b`.

### 9.2 Float math (only used by the two test-signal functions) [V identification by structure, I names]
Keil fplib/mathlib: `__aeabi_ui2d` 0x08004CC6, `__aeabi_dmul` 0x08004CCC, `__aeabi_ddiv` 0x080047C0
(`__aeabi_drdiv` 0x08004BF0 swaps its args), `__aeabi_dadd` 0x08004760 (sign dispatch to 0x08004468 /
0x0800458A), `__aeabi_d2ulz` 0x08004C08, `pow` 0x0801018C (special cases via 0x0800DC3C–0x0800DC7C,
`__kernel_poly` 0x0800DB90, `__ARM_scalbn` 0x08004F2C, `sqrt` 0x08012490/`_dsqrt` 0x08005024,
errno 0x08004F1E/0x08004F8C). Constants: 0x40000000:00000000 = 2.0, 0x41086A00:00000000 = 200000.0,
0x40200000:00000000 = 8.0, 0x40080000:00000000 = 3.0, 0x40240000:00000000 = 10.0. The integer
helpers `__aeabi_lmul` 0x08004194, `__aeabi_uldivmod` 0x080041C4, `__aeabi_uidivmod` 0x08004306,
`__aeabi_idivmod` 0x0800431A are used everywhere for 64-bit frequencies, decimal conversion and the
step computation. No other floating point is used by the application.

### 9.3 Factory interface on USART3 – `factory_cmd_execute` 0x0800522C [V]
The USART3 ISR (0x0800D7D8) accepts `:<letter a..l><decimal>\n` into 0x20000070/0x20000072 and sets
`factory_cmd_flags.0`. Executed from the main loop (switch8 table 0x0800523A); every recognised
command replies `'Z'` (0x5A) on USART3, then the pending bit is cleared:

| Cmd | Action |
|---|---|
| `a0` | `cal_index=0`, `ui_state=0xFF`, redraw, clear screen, `cal_draw_values` |
| `aN` (N≠0) | `ui_state = 0` (→ HOME/PC), redraw, clear screen |
| `bN` | `cal_index = N` (unchecked), `cal_apply_test_jig()`, redraw, `pb7_pwm_duty` = 1 (N≤5), 6 (6..10), 10 (11..15), 15 (16..20), else 1 |
| `cN` | `cal_table[cal_index] = N`, send `:w60=1112111,II,VVVVV,` on USART1, redraw |
| `d11141` / `d11131` | `cal_save(1)` + `:w61=1114111,` / `cal_save(0)` + `:w61=1113111,` |
| `e`..`l` N | `cal_table[21..28] = N` and draw it at its position |

---------------------------------------------------------------------------------------------------

## 10. Persistence summary [V]

| Location | Content | Written by |
|---|---|---|
| SPI flash 0x05000 (0x780 B) | name table, 32×60 B (slots 0/31 always zero); 0xFF bytes read as 0 | `names_save` (`:n` upload), w96 erase |
| SPI flash 0x10000+n·0x1000, n=1..30 | program record 0x668 B + step table 800 B | `program_save` (`:p`), `:nNN=,` erase, w96 erase |
| SPI flash 0x74000 (64 B) | cal_table (valid if [31]==31) | `cal_save`, w63, w62 restore; erased by first-boot wipe |
| SPI flash 0x75000 (64 B) | cal_table backup ("factory") | `cal_save(1)`; erased by first-boot wipe |
| int. flash 0x08003D00 | first-boot flag (≠0 → wipe cal, then 0) | `factory_first_boot_wipe` |
| int. flash 0x08003E00 | vendor licence data (excluded; reimplementation never writes it) | `license_check_uid` (original only) |
| int. flash 0x08003F00 | device ID (write-once) | `w00` |
| option bytes | RDP level 1 | `flash_ensure_rdp1` |

Nothing else is persistent: loaded programs, list position, loop flags, running state, backlight
level and PB7 duty are lost at power-off. No counters or timers (elapsed/remaining time) are
displayed anywhere – only the current step frequency [V: `g1/g2_elapsed` are used only for the
icon choice and the step computation].

---------------------------------------------------------------------------------------------------

## 11. Feature checklist (acceptance tests)

1. Backlight PWM at 625 Hz, 8/16 duty, off during LCD init; PB7 PWM 1/16 duty (§1).
2. Power-up shows the splash (5 bitmaps) for ≈4.0 s; ends early when a USART3 command arrives.
3. Holding G1+G2 at power-up (released afterwards) enters the calibration menu, sends `:r80=0,` to G1, shows 29 values and switches on the index-0 test signal (PB2 high).
4. First boot with 0x08003D00 ≠ 0 erases SPI 0x74000/0x75000 (original also zeroes 0x08003D00 in the bootloader page; the reimplementation must not write that page, see hardware spec §8 policy; since it could not clear the flag, the reimplementation skips this factory wipe entirely — on units that ran the original the flag is already 0). No licence check, no RDP change (deliberate deviations).
5. After boot: HOME (PB1 = 0) or PC mode (PB1 = 1); both generators receive the idle configuration (outputs off) on the first loop when entering HOME.
6. Key scan: 13.3 ms per scan, priority order and combos of §3.1; press/held/release model, two-scan confirmation.
7. POWER held ≈0.8 s in any state → goodbye screen, 4 s, PB2 square wave forever.
8. HOME: two grey panels with "Choose Your Treatment Program"; G1/G2 open the program list for that channel.
9. LIST: header "Program  G1/G2", 7 visible rows "NN." + name for slots 1..30, TEAL highlight, UP/DOWN with auto-repeat (≈0.28 s delay), scrolling window, selection clamped 1..30.
10. LIST: G1/G2 switch target channel; STOP returns without loading; START/OK loads the slot into the channel and shows its channel screen.
11. Long names scroll (1 px/25.6 ms, 2.56 s initial pause, 30 px gap); short names static.
12. CH1/CH2 screens: active panel white, other grey; names or "Choose…"; status icon c/s/p/r; frequency only while running; loop badge.
13. START toggles run/pause (start clears loop); LOOP toggles run/pause with loop set; STOP resets to step 1 (icon 's'), clears loop; POWER short = stop both channels.
14. G1/G2 keys switch between channel screens or open the list when that channel has no program / is already shown.
15. Program execution: step duration = dwell × 1.01 s; per-step `w24` frequency; per-step `w50/w51/w12` extras; idle config (10 commands) on load/pause/stop/end; outputs on (`w11=1,1`) when playing; ack wait after each command.
16. Program end: stop with outputs off, or restart at step 1 in loop mode; both channels independent; runner active also while in LIST.
17. Reproduce the original's G2 step-parameter indexing (it uses G1's step index).
18. PC mode: entry sends `:w11=0,0,` to both (PB2 pulse), clears loaded programs; exit sends `:w15=0,0,`, returns to HOME.
19. PC mode screen: PC icon red/white (unlock state), 4 frequencies (grey/teal by `w11`), sync display (`w14`), names from `:n00=` on each link, alternating name marquee.
20. Frequency display format `m·10^(e−8)` Hz with 8 decimals, leading-zero layout of §5.2.
21. Serial program storage: `:g`, `:p`, `:n` (store/`$`/`*`/`#`/`,`/`?`) with `:ok`/`:err`/`:Rev201` replies and permission rule; uploads overwrite the active channel buffers.
22. Serial maintenance commands w00 (ack only), w60/w62, w63, w64, w80, w91 (accept, no gate), w92, w95, w96, w97 with their confirmation values.
23. Deliberate deviation: no licence check and no PC-unlock gate; serial writes are always permitted.
24. Calibration menu: index navigation with test signals, value edit with inversion rule and `:w60` transmission, save variants with `:w61`, frequency/amplitude test cycling (pow formulas), no key exit.
25. USART3 factory commands a–l with `'Z'` replies.
26. Persistence exactly as §10; nothing else survives power-off.

---------------------------------------------------------------------------------------------------

## 12. Open questions

1. Physical key labels: codes 1–7/9 are named from behaviour only; confirm against the front panel (LOOP vs "Repeat", G1/G2 vs "CH1/CH2", UP/DOWN orientation).
2. PB1: which signal (USB/PC connected?) and why "allowed" requires PB1 = 0 or the w91 unlock while PC mode is entered at PB1 = 1.
3. PB2: bus-ownership/mux hypothesis vs. the 37 kHz toggling after power-off (charge pump / power controller?). PB7 (factory-set PWM) purpose. What actually removes power after the goodbye screen.
4. What is on USART1/USART2 (generator MCUs? tapped PC lines?) and USART3 (factory jig?) – serial/hardware specs.
5. Generator command semantics: w12, w13, w14, w15, w17, w20/21 (waveform codes), w28/29 (amplitude unit), w32/33 (default 120), w36/37, w50/51 (per-step a/b), w60/61, w80, w91; meaning of the frequency exponent digit is inferred from `draw_frequency`.
6. Meaning of the 29 calibration values and of the special handling of index 26 on save.
7. Icon 'c' semantics ("ready/cued"?) and the exact look of splash/goodbye/PC icons; whether splash remnants remain visible around the HOME panels (HOME render does not clear the whole screen).
8. Absolute timings depend on the 64 MHz / 4-cycles-per-iteration assumption (key scan 13.3 ms, splash 4 s, ack timeout ≈30 ms).
9. Whether the `:r80=0,` reply normally arrives before the one-shot check in `main` (otherwise the values change without redraw).

---

## 13. Validation against the user's SPI-flash backup (2026-09-23)

The dump (`backups/spi_flash_*.bin`, 2 MB, read with the link-probe firmware) decodes exactly with
the formats of §6.1 and §10 [V, real data]:

- Used sectors: 0x05000 (names), 0x11000–0x2E000 (slots 1–30), 0x74000, 0x75000 — nothing else.
- Frequency encoding confirmed: slot 1 "Schumann Resonance (CAFL)" stores 7836 → m = 783, e = 6 → 783·10^(6−8) Hz = **7.83 Hz**. DNA programs decode to 24–37 MHz, plausible for Spooky2 DNA sets.
- Headers: hdr1 (amplitude) = 2000 (20.00 V), hdr2 = 120, dwell 90–2700 s, counts 1–16; all 30 slots valid.
  **hdr0 ("waveform") holds 41…48** (slot n → 40+n for slots 1–8, 48 afterwards): not obviously waveform codes; meaning open.
- Step parameters are (0, 0) for every used step (not 0xFFFF), so `:w50/:w51` + `:w12=1,1,` would be sent per step.
- Names: 7 non-empty entries (slots 1–7), one truncated at 60 bytes; slots 0/31 empty as specified.
- Calibration: word[31] = 31 marker present; 0x74000 (working copy) differs from 0x75000 (factory/backup) in
  words 1, 3, 6, 8, 11, 16, 18 by a few counts — user/vendor trims after the factory copy. Words 21–31 match.
