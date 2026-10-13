<!-- design note of 2026-09-23: Core architecture, non-blocking main loop, # channel, compatibility tests.
Superseded where docs/nx/fw2-design.md decides differently. -->

# APP=nx core plan: architecture, non-blocking core, compatibility tests

> **Scope (2026-09-24):** Drops 3 and 4 (presets, Terrain, chains, plans, archive, manual mode, sweeps and the rest) are cancelled, and of Drop 2 only resume after a power cut remains (on the KV store, no journal ring). Sections that described only cancelled work were removed; what is left may still mention it in passing. Current scope: `docs/nx/fw2-design.md`.

One blocking constraint shapes the whole plan: any byte the display sends on U1/U2 while PB2 is high never reaches the PC. That makes the order "PC-mode sequence first, then pending replies" load-bearing (see 2.3).

APP=fw object code stays unchanged because all hooks are compile-time macros that only nx defines.

---

## 1. Code structure for APP=nx

### 1.1 What the shared code touches today

| Global | Used by the shared protocol core | Used only by the classic UI |
|---|---|---|
| `g_ch`, `g_ui_flags`, `g_pc_flags`, `g_misc_flags`, `g_factory_cmd_flags`, `g_ui_state`, `g_cal_*`, `g_names`, `g_backlight_duty`, `g_pb7_duty`, `delay_loop` | link.c, factory.c, calib.c, storage.c | – |
| `g_ui_prev_state`, `g_key_event`, `g_key_hold_count`, `g_elapsed_prescaler`, `g_marquee_*`, `g_list_*`, `g_name_bmp*`, `g_ticks`, `g_tick8` | – | app.c, ui.c |

Two more facts matter:
- factory.c calls `lcd_fill_rect` / `ui_cal_draw_values` / `ui_cal_draw_value` directly (factory.c:104-105, 144). linktest already stubs the two UI calls (main_linktest.c:41-46), so there is a precedent.
- lcd.c includes vendor `assets.h` (lcd.c:200-247). Every object depends on `$(BUILD)/assets.h` (Makefile:54). As written, nx could not build without `original/GxDisplayFW.bin`.

### 1.2 Shared modules, unchanged or additive only

| File | Change for nx | fw impact |
|---|---|---|
| `src/link.c` | Add `LINK_RX_TAP(ch,c)` in `uart_rx_hook` before `link_rx` (link.c:330-337). Add `LINK_OK_HOOK(ch)` next to `gen_ok = 1` (link.c:281-282). Add `int link_rx_idle(unsigned ch)` (reads `rx[ch].state == RX_IDLE`). Add pure builders `link_fmt_w_u64(buf,id,v)` and `link_fmt_w_pair5(buf,id,a,b)`, used by `link_send_w_u64` / `link_send_w_pair5` (link.c:404-422). | Macros default to `((void)0)`. The new functions are removed by gc-sections. Builder bytes are covered by `test_tx_builders`. |
| `src/fmt.c` | Takes `put_fixed` / `put_dec` from link.c:364-386 as `fmt_fixed` / `fmt_u64` (non-terminating variants). Takes `crc32_update` from main_probe.c:37-46. Adds base64 decode/encode (nx only). | Same bytes. |
| `src/spiflash.c` | Add `SPIFLASH_ACQUIRE()` at the top of `spiflash_read` / `read_jedec_id` (spiflash.c:73-89). Add `SPIFLASH_POLL_HOOK()` inside the `wait_ready` loop (spiflash.c:98-100). New non-blocking primitives: `spiflash_busy()`, `spiflash_erase_begin(addr)` (WREN+0x20, no wait), `spiflash_program_begin(addr,buf,len)`. Optional: `spiflash_suspend()/resume()` (0x75/0x7A). | Macros are empty; new functions are gc'd. |
| `src/storage.c`, `src/factory.c`, `src/calib.c` | none | none |
| `src/startup.c` | `#ifdef CFG_FAULT_HOOK`: a naked HardFault wrapper passes MSP to the hook, and Default_Handler calls the hook with IPSR. Then `power_off_signal()` as today (startup.c:15-23). | Nothing compiled for fw. |
| `src/board.c` | Keep `clock_init`, `delay_loop`, `power_off_signal`, `board_init`. Expose `board_gpio_init()` (today static `gpio_init`, board.c:72). `#ifdef CFG_IWDG` feed inside `power_off_signal` (board.c:183-191). **Move** `tim3_init`, `TIM3_IRQHandler`, `g_ticks`, `g_tick8`, `g_backlight_duty`, `g_pb7_duty`, `delay_ms` into new `src/board_tim3.c`. | Same code, different file. |
| `src/lcd.c` | Split into `lcd.c` (init tables, `lcd_init`, variant, bus helpers exposed as `lcd_bus_begin/end/cmd/data/window`), `src/lcd_draw.c` (polled fill/blit/stream, lcd.c:149-198) and `src/lcd_text.c` (vendor-font text, lcd.c:200-247). | Same code. |
| `src/state.c` | Move the classic-UI variables (table above) into `src/ui_state.c`. The `ui*` wildcard picks it up for fw and uitest. nx saves the 2 KB `g_name_bmp`. | Same symbols, different RAM addresses. |

### 1.3 New shared cores split out of app.c/runner.c (fw keeps exact behaviour)

| New file | Content, moved verbatim | fw caller |
|---|---|---|
| `src/core_pcmode.c` | `enum pcm_event core_pcmode_update(uint32_t pb1)` = app.c:104-135 with PB1 passed in. Includes the `delay_loop(0x34155)` delays, `link_send_line` and `link_wait_ok`. Returns ENTERED / LEFT / NONE. | `pc_mode_update()` becomes `core_pcmode_update(pin_read(GPIOB, HOST_DETECT_PIN))` |
| `src/core_calmenu.c` | `core_calmenu_key(enum key_code k, int repeat)` = app.c:217-255, including the per-pass MISC_CAL_INVERT update. `core_calboot_begin()` = the serial part of app.c:322-345 (`:r80=0,`, reply wait, PB2 high, `cal_apply_test`), with UI callbacks. | fw adapter maps `pressed()`/`cal_repeat()` to (k, repeat) in the same order |
| `src/core_actions.c` | State parts of the key actions: `core_load(ch,slot)` (app.c:141-148, incl. setting both `step_sent` to 0xFF), `core_start_pause`, `core_stop`, `core_loop`, `core_stop_all` (app.c:186-213). | list_keys/channel_keys call them, then set `g_ui_state`/redraw |
| `src/runner_seq.c` | Pure wire-format builders: `seq_idle(prog, freq, out[10])`, `seq_play(freq, a, b, out[5])`, `seq_off(out)`. They emit the exact line bytes of runner.c:45-67. | runner.c `send_step` loops over the lines with `link_send_raw` + `link_wait_ok`. The step and finish logic (runner.c:7-17, 72-93) stays in runner.c, including "compute both steps first, then process G1, then G2". |

The time logic is not shared. nx gets its own runner with a millisecond time base (section 4). Only the bytes the generators and PC see are shared.

### 1.4 nx-only code (`src/nx/`, one flat folder including `main_nx.c`; classic-only files live in `src/classic/`)

`sys_time.c` (SysTick `g_ms`, TIM6 µs), `sys_sched.c` (main loop, `sys_yield_io`, sleep), `sys_wdg.c`, `sys_fault.c` (crash record, reset cause), `uart_nx.c` (implements the `uart.h` API with FIFO and TX rings), `keys_nx.c`, `backlight.c` (TIM3 compare-channel PWM; defines `g_backlight_duty` / `g_pb7_duty`), `lcd_dma.c` (implements `lcd_fill_rect` plus the DMA API), `txq.c`, `runner_nx.c`, `ext_rx.c` / `ext_cmd.c` / `flashjob.c` ('#' channel), `store2.c` (settings, log, library index), `diag.c`, `ui_compat.c` (`ui_cal_draw_values/_value` for factory.c), and the UI engine under `src/nx/ui/`.

### 1.5 Makefile

Switch to explicit lists. The prefix wildcards would pull fw files into nx.

```make
BASE_SRCS  := src/board.c src/classic/uart.c src/lcd.c src/classic/keys.c src/spiflash.c src/fmt.c src/startup.c
CLASSIC_HW := src/board_tim3.c src/lcd_draw.c src/lcd_text.c
UI_SRCS    := $(wildcard src/gfx*.c src/ui*.c)                  # incl. ui_state.c
PROTO_SRCS := src/state.c src/link.c src/storage.c src/factory.c src/calib.c
CORE_SRCS  := src/core_pcmode.c src/core_calmenu.c src/core_actions.c src/runner_seq.c
NX_SRCS    := $(wildcard src/nx/*.c src/nx/ui/*.c src/nx/gen/*.c)

MODULES_fw       := $(CLASSIC_HW) $(PROTO_SRCS) $(CORE_SRCS) $(UI_SRCS) src/app.c src/runner.c
MODULES_uitest   := $(CLASSIC_HW) src/state.c $(UI_SRCS)
MODULES_linktest := $(CLASSIC_HW) $(PROTO_SRCS) $(CORE_SRCS)
MODULES_bringup  := $(CLASSIC_HW)
MODULES_probe    := $(CLASSIC_HW)
MODULES_nx       := $(SHARED_BASE) $(PROTO_SRCS) $(CORE_SRCS) $(NX_SRCS)   # SHARED_BASE: board, lcd, spiflash, fmt, startup
BASE_nx := # nx replaces uart.c with src/nx/uart_nx.c (same uart.h API)

VENDOR_ASSETS := fw uitest bringup probe linktest     # apps that need original/GxDisplayFW.bin
CFLAGS_nx  := -DAPP_NX=1 -DCFG_IWDG=1 -DLINK_RX_TAP=nx_ext_rx_tap -DLINK_OK_HOOK=nx_txq_on_ok \
              -DSPIFLASH_ACQUIRE=nx_flash_quiesce -DSPIFLASH_POLL_HOOK=sys_yield_io \
              -DCFG_FAULT_HOOK=nx_fault -DNX_GEN_READY_MS=4440
LDSCRIPT_nx := ld/nx.ld           # default ld/app.ld
```

- Asset dependency becomes per app: `ASSET_DEP := $(if $(filter $(APP),$(VENDOR_ASSETS)),$(BUILD)/assets.h)`. `assets.o` is only added for those apps.
- Object rule gets `@mkdir -p $(dir $@)` for `src/nx/` subfolders.
- nx fonts: `tools/gen_nx_assets.py` rasterizes OFL fonts (sources and licences in `assets/nx/fonts/`) to 4-bpp RLE glyphs. The output is committed as `src/nx/gen/*.c` ("generated, do not edit") so the normal build stays stdlib-only. `make nx-assets` regenerates it with the .venv. No `original/` needed.
- `ld/nx.ld`: same as app.ld plus a `.noinit` NOLOAD section at the address M1 proves safe, `_min_stack = 3K`, and `ASSERT(LOADADDR(.data)+SIZEOF(.data) - 0x08004000 <= NX_FLASH_LIMIT)`. `NX_FLASH_LIMIT` defaults to 74 KB until the spike (section 7) raises it; the hard ceiling is 114 672 bytes.
- Why that ceiling: `build_packets()` in flash_gx_pro.py counts the 4 ID bytes into the block count and `gxfw.encode` pads to 16. 114 672 + 16-byte pad + 4 = 56 blocks, the last one ending at 0x0801FFFF.
- Also add `tools/gxfw.py check --max-blocks 56` to the `GxCustomFW.bin` rule.

---

## 2. Non-blocking main loop

### 2.1 Principles
- Interrupts do the time-critical work: UART RX parsing (link.c unchanged), UART TX from rings, key sampling, PWM, LCD DMA.
- Main-loop tasks run to completion. Only the protocol-mandated legacy work may block, and it blocks exactly as long as in the original:
  - reply TX until TC
  - `:ok` after a flash write
  - w92's 26.7 ms
  - w96 erase-all
  - PC-mode entry/exit sequences
  - calibration sequences
- That work stays in the shared synchronous code. This is what keeps the bytes identical.
- Everything else never blocks: keys, runner, UI slices, '#' channel, flash jobs.
- Blocking waits inside shared code call `sys_yield_io()` through `SPIFLASH_POLL_HOOK` and inside `uart_nx`'s flush/full-ring wait. It does only three things: feed the IWDG, run `txq_service()` (other link's in-flight runner lines), and WFI. It never plans new runner sequences, runs the UI, or touches SPI1.

### 2.2 Loop (`src/nx/sys_sched.c`)

The order mirrors the original's pc_mode → runner → link0 → link1 → render → factory.

```c
void nx_run(void) {
  for (;;) {
    wdg_feed();
    if (power_off_requested()) power_off_nx();             // no link work after this point
    if (core_ready()) {                                     // t >= NX_GEN_READY_MS, or factory cmd seen, or cal boot
      pcmode_task();        // PB1 debounced edge && txq_idle(0)&&txq_idle(1) -> core_pcmode_update() (blocking <=110 ms)
      runner_nx_task();     // not ST_PC/ST_CAL: plan steps -> txq_submit_seq() (non-blocking)
      txq_service();        // per link: send line -> wait own :ok / 40 ms -> 133 us tail -> next
      for (ch = 0; ch < 2; ch++)
        if ((g_ui_flags & UIF_LINE(ch)) && txq_idle(ch)) link_process(ch);   // shared, synchronous
      if ((g_factory_cmd_flags & 1) && txq_idle(0) && txq_idle(1)) link_factory_execute();
      ext_task();           // '#': at most one line or one flash step (<= ~0.3 ms)
    }
    ui_task(UI_SLICE_US);   // <= 1.5 ms of CPU; LCD DMA continues in the background
    sys_sleep_if_idle();    // cpsid; if (!work_pending()) { __DSB(); __WFI(); } cpsie
  }
}
```

Latency of a completed ':' line = rest of the current slice, typically under 1 ms and worst case about 2 ms. The exception is when the other link is doing deterministic legacy work, which is the same as the original.

### 2.3 How each PC-visible sequence keeps its bytes and order

| Sequence | Original | nx mechanism |
|---|---|---|
| Replies to g/p/n | link.c:657-693, sync; UIF_LINE cleared after `tx_end` = TC | Same code. `uart_nx`'s `uart_flush` waits for ring empty plus TC (yield inside), so the drop window still ends only when the reply is on the wire. |
| `:ok` after flash write | `program_save` inside link_process | Same. `nx_flash_quiesce()` first finishes an in-flight '#' op (bounded). |
| `:n#` 200 values, `:n*` format, value-index carry-over, immediate vs deferred w | link.c | Same code. The legacy parser receives every byte exactly as in fw; the '#' tap only observes. |
| w92: delay 13.3 ms → PB2 high → delay 13.3 ms → `:w91=0,` (link.c:624-629) | blocking, line pending throughout | Same shared code. `delay_loop` is still used, so the 26.7 ms minimum and the drop window are identical. |
| w95=12021 reset | link.c:630-633 | Same. |
| PC entry: PB2 high, state, delay 13.3 ms, `:w11=0,0,` G1+ack, G2+ack, delay, PB2 low | app.c:107-122 | `core_pcmode.c` (moved verbatim), run in the loop only when both runner queues are idle. The original's runner_tick also always finished before pc_mode_update. Lines pending during it are processed after PB2 goes low, which is the original order. |
| PC exit: PB2 high, `:w15=0,0,` both, clear | app.c:123-134 | Same. The runner then resends the idle config if `:g`/`:p` in PC mode set `step_sent=0xFF`. This path has no scenario today; add `pc_upload_exit`. |
| Boot idle config (4441 / 4464 ms) | first loop pass after the ~4 s splash | `runner_nx` first transmits at `NX_GEN_READY_MS=4440`, or earlier if a factory command ends the boot wait early (app.c:339-341 semantics). G1 and G2 run in parallel (G2 about 24 ms earlier, within tolerance). |
| Factory 'Z' | after the command's work, incl. `cal_apply_test_jig` lines and acks (factory.c:151) | Shared sync code, started only when both txqs are idle, so 'Z' still follows the U1/U2 lines. `:a0` draws through `ui_compat`, so 'Z' comes earlier (faster, allowed). |
| Calibration sequences | calib.c:108-129, shared `gen_ok` | Same code and the same shared `link_wait_ok`. |

**PC-mode TX rule, enforced:** `txq_submit*()` returns an error and increments `diag.txq_rejected_pc` when `g_ui_state == ST_PC`. In PC mode only three things transmit on U1/U2:
1. link.c replies
2. core_pcmode sequences (with PB2 high)
3. '#' replies to '#' requests

### 2.4 Risks where faster handling could change bytes or order, and the fix for each

1. **Reply while PB2 is high (the PC is isolated per hardware.md §9).**
   - Risk: a reply sent during PC entry or exit, or during w92, never reaches the PC.
   - Fix: pcmode sequences block the loop and link_process runs after them; '#' replies are allowed only in ST_PC and outside sequences.
2. **Replies before 4.4 s.**
   - Risk: the original is stuck in the splash loop and never replies before its first loop pass (~4.4 s).
   - Fix: no link, runner or pcmode work before `core_ready()`; lines stay pending, exactly as in the original. The UI can animate meanwhile.
3. **Replies during the 4 s goodbye.**
   - Risk: the original is blocked in `delay_loop(64000000)` (app.c:295) and never answers.
   - Fix: after a power-off request nx stops link and runner work.
4. **Runner line versus reply on the same link.**
   - Risk: the original always finishes a runner sequence before link_process (runner_tick comes first in each pass).
   - Fix: runner planning before link work in the loop, plus `txq_idle(ch)` gating, so sequences stay atomic per link.
5. **Legacy synchronous senders interleaving with async runner lines** (factory `:b`/`:c`/`:d`, calibration, pcmode).
   - Fix: all gated on both txqs being idle.
6. **PB1 glitches.**
   - Risk: 1 ms sampling would catch bounces the original's 13-27 ms sampling misses, producing extra `:w11`/`:w15` sequences.
   - Fix: `NX_PB1_DEBOUNCE_MS=15` stable time.
7. **Shorter drop window** (frames the original drops while a line is pending are now accepted).
   - Argument: the original's window is (random loop phase, 0 to ~27 ms in PC mode, longer in standalone) plus (deterministic work). nx removes only the random part. Any nx window equals an original window at some loop phase, so every nx reply trace is one the original can produce ("trace inclusion"). No reply depends on runtime timing state.
   - Consequences: some lines the original drops with an unlucky phase get answered, and more w24/w25 mirrors are shown.
   - Fix: keep link.c's drop rule verbatim and keep every deterministic window. Add a knob `NX_LINE_HOLD_MS` (default 0) that holds UIF_LINE for a minimum time, in case field tests ever show a host relying on drops.
8. **Shared versus per-link `:ok`.**
   - Effect: per-link order is unchanged. Only cross-link interleaving changes, which neither generator can see. A late G2 `:ok` no longer ends G1's wait early.
9. **Dwell 1.00 s instead of 1.01 s.**
   - Effect: timing only (a 3-step program ends 30 ms earlier). Documented deviation; optional `NX_DWELL_COMPAT` setting.
10. **UART FIFO batching.**
    - Effect: bytes reach link_rx a few µs later, in order. Negligible.
11. **'#' traffic.**
    - Guarantee: the legacy parser sees identical bytes (section 3). '#' replies never contain ':' (asserted in the ext TX path).

---

## 4. Timing, timers and DMA

**Dwell fix.** The 1 % error comes from the 101-count prescaler (app.c:19-25), not from the time source. `runner_nx` keeps per-channel `t_start_ms` and `paused_ms` from the crystal-derived 1 kHz SysTick counter:

```
step = (now - t_start - paused) / (dwell * 1000) + 1
```

This is exact at every step boundary with no accumulated drift, so no separate dwell timer is needed. DEVIATIONS.md gets a new "nx" section (the original's 1 % long dwell fixed).

**Timer and IRQ allocation (M0+, 4 priority levels):**

| Resource | Use | Priority |
|---|---|---|
| USART1/2 (IRQ 27/28) | RX: drain FIFO in a loop, `uart_rx_hook` per byte; TX: TXFNF fills from ring, TC for flush | 0 |
| USART3_4 (IRQ 29) | factory RX, 'Z' (no FIFO on G070 USART3) | 1 |
| SysTick | 1 kHz: `g_ms`; keys sampled every 2 ms; PB1 sampling; UI frame deadlines | 1 |
| TIM3 (IRQ 16) | PSC=63 (1 MHz), ARR=1599 → 625 Hz, the original's PWM frequency, so PB7's unknown load sees no change. Update sets PB12/PB7; CC1 clears PB12; CC2 clears PB7. OC1PE/OC2PE preload for glitch-free changes. Brightness via a 256-level gamma table mapped to 0..1600; fades step CCR1 every 8 ms from SysTick. `g_backlight_duty` 0..16 keeps its w64 meaning (0 = off, 8 = user brightness). PB12 has no timer output on the G070 (AF: SPI2_NSS / TIM1_BKIN / TIM15_BKIN per the datasheet AF table), so PWM stays in software. | 1 |
| TIM6 | free-running 1 MHz, no IRQ: µs stamps (133 µs ack tail, ISR latency, slice profiling) | – |
| DMA1 Ch1 (IRQ 9) | SPI2 TX (LCD) complete → next band | 2 |
| DMA1 Ch2/3 (IRQ 10) | optional SPI1 RX/TX for bulk library reads / CRC | 2 |
| TIM7/14/15/16/17 | reserved (TIM17_CH1N on PB7 could become hardware PWM later) | – |

**RX overrun margin.** Only the other UART's ISR (≤ 5-10 µs) can delay a UART ISR. The RX FIFO (8 bytes, 694 µs at 115200) is additional insurance. Rule: no PRIMASK section longer than 5 µs; `diag.isr_max_us` measures it.

**USART FIFO on U1/U2.** Set FIFOEN before UE. With RXFNEIE per-byte semantics are unchanged; the ISR loops `while (ISR & RXFNE)`.

**LCD DMA.** Keep SPI2 in 8-bit mode and use 16-bit data packing: DMA PSIZE=16 to `SPI2->DR`, which sends the low byte first. Band buffers hold byte-swapped RGB565 (`__REV16`). Fills use MINC=0 with a single pre-swapped colour, chunked at 65 535 per DMA transfer; a full screen takes about 38 ms of DMA with the CPU free. Window commands stay polled 8-bit (`lcd_bus_*`), so no DS switching is needed, and the emulator already models packed writes this way (periph.py:285-290).

---

## 5. Robustness

- **IWDG** (`sys_wdg.c`):
  - Started after `lcd_init` at run time (KR=0xCCCC, 0x5555, PR=4 (/64), RLR=999 → 2.0 s at LSI 32 kHz ±5 %). Never via option bytes.
  - Fed in: the main loop, `sys_yield_io`, `SPIFLASH_POLL_HOOK` (covers w96's 31 erases, up to 12 s), `power_off_signal` (`CFG_IWDG`, every 256 half-periods), and the goodbye wait.
  - Optionally frozen under debug (DBG_APB_FZ1).
- **Bounded waits** (add bounds and error counters in nx paths):
  - clock PLL/SWS loops (board.c:36-63)
  - SPI TXE/BSY (lcd.c:55-63): only in the DMA driver
  - UART TXE/TC (uart.c:98, :112): replaced by ring plus timeout
  - `while (key_scan() != KEY_NONE)` at cal boot (app.c:324): unbounded on a stuck key; bound to 10 s
  - `wait_ready`: already bounded (spiflash.c:63)
- **Faults** (`sys_fault.c`):
  - The naked HardFault captures the stacked PC/LR/xPSR and IPSR into a `.noinit` crash record (magic, CRC, count, uptime).
  - If there were fewer than 3 crashes within 30 s of boot: `NVIC_SystemReset()`. Generators get the idle config (outputs off) 4.4 s later, which is better than the original's hang with outputs still on.
  - Otherwise: `power_off_signal()` with IWDG feed.
  - Symbol names stay, so the emulator's fault and power-off hooks still work.
- **Reset reason:** read `RCC->CSR` (IWDGRSTF, SFTRSTF, PINRSTF, PWRRSTF, LPWRRSTF, OBLRSTF) first thing in `main_nx`, then set RMVF. The vendor bootloader may clear these flags; M1 checks this, with the noinit record as fallback.
- **Error counters** (`struct nx_diag`): UART ORE/FE/NE per port, bytes dropped while pending, lines per link, `:ok` timeouts, txq rejections in PC mode (must stay 0), flash timeouts and quiesce waits/max, '#' CRC/overrun/overlong errors, UI slice overruns, max loop gap, max ISR time, boots, IWDG resets, faults. Shown on the diagnostics screen and via `#diag`; boots, IWDG resets and faults also go to the log ring.

---

## 6. Emulator, diff.py and host tests

### 6.1 tools/sim

What already works: WFI (machine.py:834-840, covered by the selftest), SysTick, TXEIE/TCIE interrupts (periph.py:458-462, 544-546).

Needed:
1. **DMA1 + DMAMUX1** (`Dma` class at 0x40020000 / 0x40020800):
   - Registers CCR/CNDTR/CPAR/CMAR/ISR/IFCR and CxCR.DMAREQ_ID. Check SPI2_TX = 19, SPI1_RX/TX = 16/17 against RM0444.
   - When enabled with `SPI2.CR2.TXDMAEN`, perform all transfers at once through `Spi.write(0x0C, psize, …)`; its `busy_until` chaining times them correctly.
   - Schedule TCIF at `busy_until`. Add IRQs 9/10/11 to `irq_lines` and `IRQ_NAMES`.
   - Optionally an SPI1 RX+TX pair and mem2mem.
2. **SPI:** if DS=16 is ever used, send the high byte first (periph.py:285-290 currently sends low byte first, which is right for 8-bit packing only).
3. **IWDG** (0x40003000): KR/PR/RLR/SR, a timer source; expiry → event plus stop reason "watchdog" (a violation unless the scenario sets `expect_watchdog`).
4. **Timer compare:** CCR1-4, CCxIE/CCxIF, OCxPE preload in `Tim` (periph.py:305-396, today update-only), with next-event scheduling.
5. **USART FIFO** (CR1.FIFOEN): 8-deep RX (ORE only when full) and TX (TXFNF/TXFE/TC). Remove the "TDR written while TXE=0" warning in FIFO mode.
6. **Reboot-continue** (`continue_after_reset: true`): on SYSRESETREQ/IWDG, reset peripherals, keep RAM (optionally scribble a configured "bootloader RAM" range), set RCC_CSR flags, restart from the app vector. Needed for w95, crash record and IWDG tests. Also make the RCC_CSR RMVF write clear the flags.
7. **`backlight()`** (machine.py:754-772): its 3.2 ms window equals two 625 Hz periods, so it stays valid. Generalize it to integrate over N detected periods.
8. **W25Q16:** optional suspend/resume (0x75/0x7A).
9. Selftest additions: DMA-to-SPI2 completion IRQ, IWDG expiry, TIM3 CC ordering, FIFO RX burst without ORE.

### 6.2 tools/diff.py

- `--serial-only`:
  - Informational: checkpoint pixels, display_on/sleep/inversion.
  - Strict: backlight on/off (w64 is a protocol effect), U1/U2/U3 lines, PB2, stop reason.
  - SPI flash compared only for the legacy ranges (0x5000, 0x10000-0x2EFFF, 0x74000-0x75FFF); nx-owned sectors are informational.
- `--timing one-sided`: the candidate may be earlier by any amount but not later than tol. A scenario key `"two_sided_before_ms": 5000` keeps the boot idle config two-sided.
- `--timing anchor`: each tx line's time is measured from the latest preceding input (rx / key / pb1 / intent end) in its own run.
- `--baseline DIR [--update-baseline]`: candidate screens against its own stored screens (`tools/scenarios/baseline/nx/`, committable because nx has no vendor graphics).
- Optional `--reference-trace`: committed JSON of the original's serial/PB2/flash traces, for CI without the vendor image. To be decided.
- Scenarios get intents: `{"at":6000,"intent":"open_list","ch":1}` expanded through `tools/scenarios/keymaps/{classic,nx}.json` (key roles: open_list, nav_up/down, confirm, back, play_pause, stop, loop, stop_all, power_hold). `load_scenario` (scenario.py:62-72) must accept "intent". The keymap is chosen with `--keymap`, or per target: original/fw → classic.
- **Recommendation for the UI design:** keep the hardware key roles the same in nx (G1/G2 = focus generator, START = play/pause, STOP, LOOP, POWER short = stop both, POWER long = off). Then the nx keymap is nearly identity.

**Running the 13 scenarios on nx:**

| Scenarios | How they run on nx |
|---|---|
| queries, serial_storage, serial_adversarial, serial_maint, cal_trim, pc_mode, boot_home | unchanged, with `--serial-only --timing one-sided` |
| cal_boot, power_off | unchanged (hardware-level keys) |
| ch1_program, g2_switch, upload_run | intent versions, `--serial-only --timing anchor` |
| list_nav | replaced by the UI baseline scenario `nx_list_nav` |

New scenarios: `pc_upload_exit` (fw and nx), `nx_ext_mixed` (serial_storage-style ':' traffic on U1 while a '#' upload runs on U2; U1 lines must equal the reference).

### 6.3 Host tests

- Keep `test_link` and add `runner_seq.c`, `core_*.c` and `fmt.c` to its SRCS (tests/host/Makefile:22-23).
- Add **characterization tests first (M0):** golden `ev_log` traces (`1>…` / `[W]`) for idle, play with and without pairs, pause, stop, finish and loop, taken from today's runner.c through mocked `link_wait_ok`.
- New binary `tests/host/nx/test_nx` (`make -C tests/host` runs both):
  - `txq` with per-link acks, timeouts and the 133 µs tail
  - `runner_nx` against `runner_seq` goldens; dwell step boundaries at exactly 1000 ms
  - key engine (debounce, repeat, long press, chords, power 0.8 s)
  - ext tap: byte-for-byte proof that feeding '#' lines interleaved with ':' traffic leaves the `link_rx` state and replies identical to the same stream without the tap
  - base64 and CRC
  - flashjob plus quiesce on a W25Q16 model extended with busy timing
  - store2 ping-pong and power-loss cases

---

## 7. Bootloader image-size spike (M1, hardware only, no risk to the bootloader)

1. New `APP=sizeprobe` (`src/main_sizeprobe.c`, tiny built-in 5×7 font).
   - A linker `.pad` section fills to `PAD_KB` with an xorshift pattern. It must not be 0xFF, otherwise unwritten blocks would pass.
   - `tools/pad_image.py` patches a trailer {magic, length, CRC-32} into the last 16 bytes.
   - At boot it computes the CRC over [0x08004000, trailer), checks the pattern, and shows a large "92 KB CRC OK" (green) or "FAIL @0x…" (red). It also shows whether the flash after the image is erased.
   - It also shows RCC_CSR as seen after the bootloader, and results of the next two checks.
2. **noinit survival:** at first boot write 64-byte patterns at 0x20002000, 0x20004000, 0x20006000 and 0x20008000; press a "reset" key (SYSRESETREQ) and report which survive. This decides where `.noinit` goes, since the bootloader's stack and buffers are unknown.
3. **IWDG across reset:** enable a 2 s IWDG, reset by key, and check the app still boots normally. Then power off and enter programming mode (POR) and do a full flash taking over 30 s; the bootloader must not reset.
4. Sizes in order: 76, 80, 88, 96, 104, 110 KB. The flasher enforces ≤ 56 blocks (new `--max-blocks` check in `build_packets`).
5. Per size:
   - power off
   - hold Stop, press Power
   - `make APP=sizeprobe PAD_KB=N flash`
   - check every block got "C"
   - power-cycle and read the screen
6. On a black screen, FAIL, or refused blocks: Stop+Power, `make flash-original`, stop the spike, and set `NX_FLASH_LIMIT` to the last good size.
7. Check `make flash-original` recovery once before starting. Record the results in README.

Nothing in this procedure writes option bytes or 0x08000000-0x08003FFF.

---

## 8. Milestones (each flashable and verifiable)

| # | Content | Verify |
|---|---|---|
| M0 | Characterization tests; baselines | `make && make -C tests/host && .venv/bin/python tools/diff.py --jobs 6` (13/13); `make conformance PORT=… RECORD=build/conf-fw-baseline.json` |
| M1 | sizeprobe spike (section 7) | on-device screens; README entry |
| M2 | Shared-core refactor (1.2, 1.3), Makefile lists, hooks | `for a in fw uitest linktest bringup probe; do make APP=$a; done`; `make -C tests/host`; `diff.py --jobs 6` 13/13; device: `make flash` then `make conformance COMPARE=build/conf-fw-baseline.json`, `tools/gx_latency.py compare` (unchanged) |
| M3 | Emulator extensions, diff.py modes, intents | `cd tools && ../.venv/bin/python -m sim.selftest`; `diff.py --fresh-reference --jobs 6` still 13/13 on fw |
| M4 | nx-core: time, IWDG, faults, uart_nx, keys, PWM, LCD DMA, minimal open-font status UI, runner_nx boot config, cal menu, power-off, pcmode, diag | `make APP=nx`; `make -C tests/host`; `diff.py --candidate build/nx/gxpro.elf --serial-only --timing one-sided queries serial_storage serial_adversarial serial_maint cal_trim pc_mode boot_home cal_boot power_off`; device: `make APP=nx flash`, `gx_serial_test.py run --save build/serial-nx.json && … compare build/serial-ours.json build/serial-nx.json`, `gx_latency.py --save build/latency-nx.json` (expect ~3-5 ms vs 22), `make conformance COMPARE=build/conf-fw-baseline.json RECORD=build/conf-nx.json` (40/40 same replies) |
| M5 | Full runner, core actions, dwell fix, intent scenarios, `pc_upload_exit` | `diff.py --candidate build/nx/gxpro.elf --keymap nx --serial-only --timing anchor ch1_program g2_switch upload_run pc_upload_exit` |
| M6 | '#' channel, flashjob, store2, `tools/gx_nx.py` (upload / read / backup / diag, dual-link) | test_nx; `diff.py … nx_ext_mixed`; device: 256 KB upload with CRC verify, throughput, generator still answers afterwards, conformance again |
| M7 | UI engine (Focus + mini strip): `docs/nx/design/ui.md` | `diff.py --candidate … --baseline tools/scenarios/baseline/nx`; the M4/M5 serial gates stay green |
| M8+ | Library/chains, power & timers, manual generator + sweeps, diagnostics / PC monitor (RX-only, via tap) | per feature: test_nx cases, nx scenarios, serial regression gate |

---

## 9. Budgets

**Flash** (fw today: 31.4 KB, of which 11.5 KB vendor assets):

| Block | KB |
|---|---|
| Startup, board, clock, libgcc/libc | 2.8 |
| Shared protocol core (link 3.4, calib 2.2, factory 0.7, storage 0.8, spiflash 0.8, fmt/crc/b64 0.8, cores + runner_seq 1.2) | 9.9 |
| nx platform (time/sched/wdg/fault 1.2, uart_nx 0.8, keys 0.8, PWM + gamma 0.8, LCD DMA 1.2) | 4.8 |
| runner_nx + txq 2.5, '#' channel 2.5, flashjob 1.0, store2 3.0, diag 0.5 | 9.5 |
| UI engine | 12–16 |
| Fonts (4-bpp RLE, 3 sizes + large digits) and icons | 21–33 |
| Features | 12–18 |
| **Total** | **~72–94 KB** (proven 74.7 KB; the M1 spike decides; hard ceiling 114 672 B) |

**RAM** (36 KB):

| Item | Bytes |
|---|---|
| `g_ch[2]` | 4 960 |
| `g_names` | 1 920 |
| link rx | 688 |
| cal and core state | ~300 |
| TX rings (U1/U2 2×512, U3 32) | 1 056 |
| '#' line buffers (2×2×208) + page halves (2×128) | 1 088 |
| txq (2×12×32) | 768 |
| LCD band buffers (2×1 280) | 2 560 |
| UI state / dirty rects | ~1 500 |
| library index cache | 1 024 |
| settings, timers, log ring | ~800 |
| diag + noinit | ~200 |
| stack | 3 072 |
| **Total** | **~20 KB, about 16 KB headroom** (fw's unused 1.5 KB of RX rings and 2 KB `g_name_bmp` are not in nx) |

---

### Critical Files for Implementation
- src/link.c
- src/classic/app.c
- Makefile
- tools/sim/periph.py
- tools/diff.py
