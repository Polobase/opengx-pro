# OpenGX Pro (`APP=nx`): new UI/UX and an optimized core, backward compatible

## Context

Parity is done: `APP=fw` reproduces the original v7789 display firmware, runs on the device, passes
13 differential scenarios and 19017 host checks. `APP=nx` is the next generation: an optimized core,
a fresh UI with the original's flows, and resume after a power cut. It is **backward compatible**:
- Spooky2 works 1:1 over serial.
- Existing SPI data (programs, names, calibration) keeps working.
- Flashing back the original keeps working.

Absolute safety rules (unchanged):
- Never write option bytes/RDP or 0x08000000–0x08003FFF (0x08003C00 is only read, for the LCD
  variant).
- No vendor handshake or anti-clone code.
- No vendor material in git (fonts, preset files, captures, tables derived from vendor CSVs). One
  stated exception since 2026-10-13: the two tables of the built-in Terrain Protocol and preset in
  `src/nx/data/`, generated from Spooky2's presets (`docs/nx/THIRD_PARTY.md`).

## Decisions

| Topic | Decision |
|---|---|
| Classic build | `APP=fw` stays the pixel-exact classic build and test reference. The new firmware is `APP=nx`, sharing the protocol/storage core. |
| Compatibility | Serial replies byte-identical and in the same order; faster timing allowed, never slower. |
| Scope (2026-09-24) | nx = Drop 1 (new UI, optimized core) plus resume after a power cut. Drops 3 and 4 and the rest of Drop 2 are cancelled (see "Cancelled"). |
| UI | As fancy as the hardware allows, but with the original's flows (e.g. the "Program G1/G2" list), so existing users are not surprised. Layout **Focus + mini strip**. English only. |
| Flash | Up to 2026-09-24 the image had to fit the 74,672 bytes the vendor bootloader is proven to accept, so the UI was cut. On 2026-09-25 it was decided to bring most of it back (77,372 bytes) and to prove a larger size on the device (`docs/nx/drop0-checklist.md` §1) before flashing it. |
| POWER short | Screen off/on (stop both moves to STOP held 1 s). POWER held 0.8 s = off (unchanged). |
| Test gear | Oscilloscope + Spooky2 Remote/Boost available. |

## Key findings

- **Hardware:**
  - Free: 64 MHz already, ~81 KB flash, ~24 KB RAM, DMA1 ×7, TIM1/6/7/14/15/16/17, SysTick, ADC
    (temp, VREFINT), IWDG, CRC, and ~1.5 MB SPI flash at 0x080000–0x1FFFFF (never touched by the
    vendor fw).
  - Limits: no framebuffer (strip rendering), LCD SPI ≤32 MHz (full frame ≥38.4 ms), no TE pin, no
    RTC/battery/buzzer/LED, PB12 backlight has no hardware PWM, bootloader proven up to
    112,640-byte images (74.7 KB until the size test of 2026-09-25).
- **Protocol boundary:** `link.c` never reads UI state, so the UI is free.
  - Byte-exact: the RX parser (value-index carry-over, clamps, drop-while-pending), immediate vs
    deferred w numbers, reply builders, `:ok` after a flash write, w92 (26.7 ms → PB2 high →
    `:w91=0,`), PC entry/exit, the factory port, legacy SPI sectors.
  - Anything sent while PB2 is high never reaches the PC, so PC sequences finish before replies.
- **Waveforms:**
  - `:w20/:w21` select a 1024-sample table in generator memory.
  - 11–19 = Spooky2 waves 1–9; 25 = Out2 inverse; 41–70 = the table of stored slot n (40+n).
  - The dashboard's scope shows 11–19 and 25; anything else shows "wave #N ?".

## Architecture

```
          ┌──────────────── APP=nx ─────────────────────────────────────────────┐
keys ISR ─► nx_keys (gestures) ─► nx_ui (screens, VM diff) ─► nx_render ─► nx_lcd (SPI2 DS16 + DMA)
SysTick 1 kHz ─ time/sched ─────┐      ▲ snapshot/events   │ actions            nx_bl (TIM17 PWM)
                                ▼      │                   ▼
        nx_runner (stored slots 1–30) ─► runner_seq ─► txq (per link, own :ok)
USART1/2 ISR ─► link_rx (UNCHANGED legacy parser) ─► link_process (shared)
                storage v2 (0x080000+: superblock, KV) ─ nx_flash (guarded) ─ spiflash
          └───────────────────────────────────────────────────────────────────────┘
shared, fw-identical: link.c, storage.c, factory.c, calib.c, spiflash.c, fmt.c, core_pcmode.c,
core_calmenu.c, core_actions.c, runner_seq.c (exact wire bytes of the classic runner)
```

### Compatibility contract (enforced by tests)
- The legacy parser sees every byte exactly as in fw.
- Shared synchronous code keeps all PC-visible sequences and their minimum delays (w92, PC
  entry/exit, `:ok` after a flash write).
- Link/factory work runs only when the txqs are idle. Runner planning precedes link work in each
  pass (the original's order).
- In PC mode:
  - No generator TX except protocol replies and PC sequences (the txq rejects; its counter must stay
    0).
  - No nx-initiated SPI writes except the legacy uploads. Settings and the resume snapshot are
    written after PC exit.
- Nothing is sent or answered before `NX_GEN_READY_MS=4440` (the original's first loop pass). PB1 is
  debounced 15 ms.
- Documented nx deviations (DEVIATIONS.md, "nx" section):
  - faster replies
  - exact 1.00 s dwell (the original's dwell runs 1 % long)
  - G1/G2 sent in parallel with per-link acks
  - idle config after PC exit from the channel's program
  - new UI/keys
  - resume after a power cut

### Core decisions

| Area | Decision |
|---|---|
| Code layout | `src/` holds what both firmwares share (hardware layer, protocols, storage, wire-level cores). `src/classic/` holds the classic firmware and its test/spike apps. `src/nx/` holds every nx file in one flat folder, prefixed `nx_`: platform, runner, storage, UI engine, screens, committed generated assets, `main_nx.c`. The Makefile has explicit per-app lists; `make APP=nx` works without `original/`. Hooks in shared files are macros that default to no-ops (`LINK_RX_TAP`, `LINK_OK_HOOK`, `SPIFLASH_ACQUIRE`, `SPIFLASH_POLL_HOOK`, `CFG_FAULT_HOOK`, `CFG_IWDG`). |
| Main loop | Cooperative scheduler, WFI when idle. Order: pcmode → runner plan → txq service → link_process(0/1) when the txq is idle → factory → UI tick + render slice (≤2 ms CPU). Replies take about 2 ms (the original: 27–30 ms). |
| Time | SysTick 1 kHz `g_ms`; dwell from milliseconds (exact); TIM6 free-running µs stamps. |
| IRQs | USART1/2 prio 0 (FIFO on, drain loop); USART3 1; SysTick 1 (keys every 10 ms, PB1, UI deadlines); TIM3 1 (PB7 PWM via CC at 625 Hz, unchanged load); TIM17 1 (backlight two-phase PWM 1 kHz, 256 gamma levels, fades, w64 → host override); DMA1 ch1 2 (SPI2 TX). Every IRQ has its own handler (Default_Handler = power-off). |
| LCD | Unchanged variant A/B init lists, then SPI2 DS=16 and MADCTL 0xA0 (landscape native; checked on the unit, 0x60 is turned 180°). VSCRDEF/VSCSAD for push transitions. Commands as NOP-prefixed 16-bit frames. DMA ping-pong band buffers (2×2560 px), polled fallback. |
| Robustness | IWDG 2 s (fed in the loop, the flash poll hook, the power-off loop, goodbye). HardFault → `.noinit` crash record → reset (≤3 within 30 s, then the power-off signal). Reset reason from RCC_CSR; bounded waits; error counters (`struct nx_diag`). |
| Storage v2 | Only 0x080000–0x1FFFFF, range-guarded. A/B superblock at 0x080000/0x081000; KV log at 0x082000 (two 16 KB pages: the settings record, the resume snapshot). The rest of the region is reserved and unused. CRC-32 on the hardware CRC unit (zlib-compatible); commit words are programmed last, so it is power-cut safe (tested by cutting at every flash op). Legacy sectors only through `storage.c`. Foreign data in the region → read-only. |
| Runner | Plays the stored slots 1–30. Plain slot playback sends the same per-link lines in the same order as `runner.c` (proven by a host differential test). |
| Resume | A snapshot per generator (slot, flags, elapsed ms, CRC of the stored program) in the KV store. It is written on a state change and every 60 s while running, through the deferred-write gate: not in PC mode, queues idle, no step due within 1 s. After a power cut, the dashboard offers to resume (15 s ring; START now, STOP discard, the full ring resumes). A clean power-off, both generators stopped, or a PC session deletes it. |

## UI / UX (as built)

- **Look:**
  - Dark default plus Light and High-contrast themes, as palette tokens. G1 cyan #38C7DA, G2 amber
    #F5B544. Text ≥4.5:1 and UI ≥3:1, enforced by the asset tool. Status is always icon + text.
  - Fonts: Inter / Inter Display (OFL) 13/16 px (2 bpp raw since 2026-10-14) plus 28/48 px tabular
    digits, 4 bpp nibble-RLE (the
    48 px digits are off by default since 2026-10-05, `NX_FEAT_HERO48`).
  - Icons: Material Symbols Rounded (Apache-2.0) plus own SVG icons.
  - English string tables with build-time width checks. Procedural splash (no Spooky2 trademarks).
- **Engine:**
  - Immediate mode: a dirty-rect list rendered in bands into two 5 KB RGB565 buffers, sent by DMA.
    Exact anti-aliasing (no GRAM readback).
  - Hero frequency layout (8+8 digits, ghost zeros) with an odometer roll.
  - Transitions: hardware-scroll push, backlight dip, sheet reveal, toast slide. Reduced-motion
    option.
- **Dashboard (Focus + strip):**
  - Status bar: G badge, state, loop, lock.
  - Program name (marquee), hero frequency.
  - Step n/N, dwell, volts and wave, plus a live scope.
  - Progress bar with time left, next frequency, mini strip for the other generator.
  - States: idle, ready, running, paused, finished, error (generator not responding), resume offer.
- **Screens:**
  - splash
  - dashboard
  - program list (as the original's "Program G1/G2")
  - program detail
  - quick settings (UP)
  - Settings (DOWN)
  - PC monitor (never transmits)
  - calibration (the classic topology)
  - toasts, key lock, hold rings
  - goodbye

  Keys: `docs/nx/key-model.md`. Device checks: `docs/nx/drop1-checklist.md`. What fills the flash:
  `docs/nx/drop1-size-inventory.md`.

## Roadmap

### Drop 0: on-device spikes
- **Done:** MADCTL (0xA0); the size test with `APP=sizeprobe` (`docs/nx/drop0-checklist.md` §1):
  images up to 112,640 bytes are proven (2026-09-25).
- **Cancelled:** the generator probe (`genprobe`), the PB1 test and the Spooky2 captures
  (`s2capture.py`). They only answered questions for presets, sweeps and manual mode.

### Drop 1: foundation + new UI (built)
1. Baselines, shared-core refactor (fw byte-identical), test infrastructure (emulator DMA/SPI
   DS16/IWDG/FIFO/CRC/ST7789, `diff.py --serial-only`, timing modes, nx keymap, `test_nx`).
2. nx platform:
   - time, UART rings, key gestures, backlight, PB7, watchdog and crash record
   - txq with per-link acks
   - runner (exact dwell, parallel links)
   - PC mode, calibration and power-off
   - storage v2 (superblock, KV, settings)
3. Assets (`tools/gen_nx_assets.py`, `make nx-assets`), the UI engine, the gallery (`APP=nxgallery`,
   `tools/nx_gallery.py`, baselines in `tests/gallery/nx/`).
4. The UI in English:
   - dashboard, program list (the original's flow), program detail
   - Settings (Display with "When idle", Keys & lock, Motion, About with licences, Reset all
     settings with a hold-START confirmation) and quick settings
   - PC monitor, calibration (title, test-step readout, help lines), splash and goodbye (with
     their info lines), the key guide (first boot, and from Settings), the night view
   - key lock, toasts, hold rings, the key-hint bar, "…" for cut texts, every animation

   Cut for space on 2026-09-24 and restored on 2026-09-25: reset/confirm, the licence lines, the
   night (ambient) view, the key guide, the key-hint bar, "…", the back arrow, the dashed ring,
   the starting hint, the confirmation toasts, the splash/goodbye/calibration texts. Still cut:
   the menu, the options sheet, Diagnostics and the key test, the 22 px font, the list columns.
   Everything nx has, compared with the original: `docs/nx/features.md`.
5. Device validation: `docs/nx/drop1-checklist.md`, which covers:
   - conformance COMPARE (all replies identical)
   - `gx_serial_test.py`, `gx_latency.py`
   - a Spooky2 session, a standalone run on the oscilloscope
   - `make flash-original` recovery

### Drop 2: resume after a power cut
See "Resume" above. Host tests: `tests/host/nx/test_nx_resume.c`. Device check:
`docs/nx/drop1-checklist.md` §6.

### Cancelled (2026-09-24)
- **Drop 2, except resume:**
  - auto-start, session timer / auto power-off
  - journal and history rings, history screen
  - full diagnostics
  - PC monitor sniffing
  - the `#` side channel and `nx_ctl.py`
- **Drop 3:**
  - presets and the Terrain protocol
  - chains, favourites/recent, archive, on-device slot copy/erase
  - multi-day plans
- **Drop 4:**
  - manual generator, sweeps, independent Out2
  - amplitude cap, waveform uploads
  - dual mirror, program inspector, usage statistics
- **The backlog:** on-device biofeedback, the RTC and buzzer add-ons, and the rest.

## Budgets
- **Flash (nx):** the image (the default build, and the released image of `make release`) holds
  the Terrain table and the preset table (committed in `src/nx/data/` since 2026-10-13) and no
  vendor splash: 112,080 bytes of the 112,640 the bootloader is proven to take (the biofeedback
  scan built in since 2026-10-17, the text fonts 2 bpp and link-time optimisation since
  2026-10-14; 0.9.1: 108,664 bytes), 104,726 without the two tables
  (`NX_TERRAIN=0 NX_PRESETS=0`). `make nx-data` writes the tables again from a
  Spooky2 installation (`TERRAIN_DIR` / `PRESET_FILE`, e.g. in an untracked `local.mk`).
  The linker checks the build budget (`NX_FLASH_LIMIT`, 112,640 bytes); `make flash` refuses
  anything above `FLASH_PROVEN` (112,640 bytes, the size test of 2026-09-25). Breakdown of the
  image, every feature and asset with its cost: `docs/nx/drop1-size-inventory.md`. Earlier
  sizes: 70.8 KB for the reduced Drop 1 (2026-09-24), 77,372 bytes with the restored features
  (2026-09-25).
- **RAM:** about 30 KB of 36 KB: g_ch 4.9 KB, names 1.9 KB, band buffers 10 KB, UI state
  ≈3.5 KB, TX rings + txq, storage indices, stack 3 KB.

## Verification (gate for every change)
1. `make` (fw byte-identical) + `make APP=nx` + all other apps; `make APP=nx ORIGINAL=/nonexistent`
   builds.
2. `make -C tests/host` (test_link, test_nx, test_nx_assets).
3. `.venv/bin/python tools/diff.py --jobs 6` → fw 13/13 unchanged.
4. `tools/diff.py --candidate build/nx/gxpro.elf --serial-only --timing one-sided`, 9/9 on:
   queries, serial_storage, serial_adversarial, serial_maint, cal_trim, pc_mode, boot_home,
   cal_boot, power_off.
5. `--keymap nx --timing anchor`, 5/5 on: ch1_program, g2_switch, upload_run, pc_upload_exit,
   nx_keys.
6. `tools/nx_gallery.py run --host` + `compare --source host` (screens vs approved baselines).
7. Device: `make APP=nx flash` and `docs/nx/drop1-checklist.md`, with `make flash-original`
   recovery.

## Risks
| Risk | Mitigation |
|---|---|
| The bootloader rejects images above the proven 112,640 bytes | The size test (2026-09-25); the `make flash` guard (`FLASH_PROVEN`); cut lines in `docs/nx/drop1-size-inventory.md` |
| MADCTL/DS16/scroll behave differently on the panel | Checked on the unit (0xA0); 8-bit + REV16 and polled fallbacks |
| Timing-dependent PC behaviour | Trace-inclusion argument, `NX_LINE_HOLD_MS` knob, conformance on the device |
| Flash wear / power cuts | KV commit words, cut-at-every-op tests, the resume snapshot at most once a minute |
| Vendor data leaking into git | `.gitignore` covers vendor files; the tools read the original image only from `original/` and generate into `build/`. Preset files are read only by `make nx-data`, from `TERRAIN_DIR` / `PRESET_FILE` (e.g. an untracked `local.mk`); what it writes is committed on purpose: the two tables in `src/nx/data/`, the one stated exception (`docs/nx/THIRD_PARTY.md`) |
