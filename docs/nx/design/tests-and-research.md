<!-- design note of 2026-09-23: Test infrastructure notes and GX Pro user research.
Superseded where docs/nx/fw2-design.md decides differently. -->

# OpenGX Pro: test infrastructure and GX Pro UI/UX research

> **Scope (2026-09-24):** Drops 3 and 4 (presets, Terrain, chains, plans, archive, manual mode, sweeps and the rest) are cancelled, and of Drop 2 only resume after a power cut remains (on the KV store, no journal ring). Sections that described only cancelled work were removed; what is left may still mention it in passing. Current scope: `docs/nx/fw2-design.md`.

## Summary

- **Serial-only comparison is not possible today.** `diff.py` has no flag or scenario key to skip screens. The screen check is one loop (`tools/diff.py:131-154`), and `informational` (`:149-150`) is the only way to relax it. The UART, PB2, SPI-flash and end-of-run checks (`:155-200`) already don't depend on the UI, so a serial-only mode is a small change.
- **The boot timing leaves almost no room.** Every scenario starts with the idle configuration sent to both generators right after the splash (U1 4441.6 ms, U2 4464.2 ms in the reference). Ours already sends it 43.9 ms early against a 50 ms tolerance (`build/sim/diff/boot_home/report.txt`). A new UI that changes the splash or draw time before that point will fail all 13 scenarios on timing, even though the traffic content is identical.
- **Only 7 of 13 scenarios survive a new UI as written.** Six use no keys: `queries`, `serial_storage`, `serial_adversarial`, `serial_maint`, `cal_trim`, `pc_mode`. `boot_home` has no keys but its traffic timing depends on the splash. The other six are driven by key presses (`list_nav`, `ch1_program`, `g2_switch`, `upload_run`, `cal_boot`, `power_off`) and need a different way of expressing "what the user does".
- **Plenty of room left.** The `fw` build uses about 31 KB of 112 KB flash and about 11 KB of 36 KB RAM. There is no room for a full framebuffer (153.6 KB), so a new UI must draw directly with partial updates.
- **New UI files need a different name prefix.** The Makefile picks up every `src/ui*.c` and `src/gfx*.c` into the existing apps, so new files named like that would be linked into `APP=fw` and clash.
- **Main user complaints found:** 30 slots (no delete on the device), no preset chains, Out2 not visible, "Start then Loop" confusion, runs stopping with no explanation, 10-digit frequency display, no visible firmware version, worry about leaving the screen on overnight.
- **Contrast problem in the original palette:** white text on the grey inactive panel is 3.72:1, below the WCAG 4.5:1 minimum for normal text.

---

# PART 1: Test and verification infrastructure

## 1.1 How the differential test compares original and ours (`tools/diff.py`)

**Runs**
- Each scenario runs twice on the same emulated board: once on `"reference"` (the original image `original/GxDisplayFW_decoded.bin`, `tools/sim/firmware.py:10,97-102`) and once on the candidate ELF (default `build/fw/gxpro.elf`, `tools/diff.py:51`).
- Runs execute in parallel (`tools/diff.py:237-245`).
- The reference result is cached in `tools/scenarios/reference/<name>/` and reused while its fingerprint matches (`tools/diff.py:224-231`). The fingerprint hashes the scenario file, the firmware image, `tools/sim/names/*.tsv`, the SPI image and all `tools/sim/*.py` (`tools/sim/scenario.py:75-89`). Any change to the emulator therefore re-runs the original.
- The reference is hard-wired as the target string `"reference"` (`tools/diff.py:225-230`). There is no option to diff against another candidate build or a stored baseline of our own.

**Screens, pixel by pixel** (`tools/diff.py:65-100`, `131-154`)
- The checkpoint PNG is shrunk to the logical 320x240 view, and the absolute difference is taken with `ImageChops.difference`. Pixels inside the checkpoint's `mask` rectangles are ignored.
- A checkpoint fails if the differing pixel count exceeds `tolerance_px` (per event, per scenario, or `--tolerance-px`, default 0), or if the panel state differs (`display_on`, `sleep`, `inversion`, backlight on/off; `:143-145`).
- `"informational": true` turns a failure into info only (`:149-150`).
- A side-by-side diff sheet (reference | candidate | magenta difference) is written to `<name>_diff.png` (`:83-97`).

**Serial traffic** (`tools/diff.py:103-125`, `155-162`)
- For each port U1/U2/U3, only transmitted lines (`dir == "tx"`) are compared.
- The lines are aligned with `difflib.SequenceMatcher` on their text; a missing trailing newline is marked `<no newline>`.
- Any insert or delete is a failure. Matched lines also fail if their start time differs by more than `timing_tol_ms` (default 50 ms, `:52`; set per scenario or with `--timing-tol-ms`).

**Other checks**
- PB2 edges, compared the same way (`:163-167`).
- SPI-flash erase/status operations in order, and a SHA-1 of every changed 4 KB sector. How writes are split into page programs is reported only (`:168-189`).
- How and when the run ended (`:191-196`).
- Candidate violations always fail (`:197-198`).

**Exit codes and reports**
- 0 = pass, 1 = mismatch, 2 = harness error (`:22-23`, `248-283`).
- A per-scenario `report.txt` goes to `build/sim/diff/<scenario>/` (`:276-279`).

### Can diff.py compare serial only, ignoring screens?

**No, not today.** There is no CLI flag and no scenario key for it (argparse at `tools/diff.py:205-215`). The screen loop always runs for every `checkpoint` event (`:134`). The only workaround is to mark every checkpoint `"informational": true`, or to delete checkpoints from the scenario files, which also changes their fingerprints.

### What to change so serial 1:1 compatibility stays automatic while screens may differ

1. **Serial-only mode.** Add `--serial-only` (or a scenario key `"compare": ["uart","pb2","spi","stop"]`) and skip or downgrade the screen loop at `tools/diff.py:131-154` to info. Everything from `:155-200` stays as it is. This is a few lines.
2. **Fix the timing anchor.** In the reference, `boot_home` sends the idle configuration at 4441.6 ms (U1) and 4464.2 ms (U2). Ours is already 43.9 ms earlier (`build/sim/diff/boot_home/report.txt`; DEVIATIONS.md, Timing, "≈ 44 ms earlier"). The tolerance is 50 ms, so the margin is about 6 ms. Options:
   - keep the new UI's splash at about 4.0 s and send the idle configuration at a fixed time rather than "after the splash is drawn" (application.md §11 item 2);
   - or add a timing mode that compares time relative to an anchor, such as the first line of each burst or the preceding RX line or key event, instead of absolute time;
   - or relax the timing tolerance for the new-UI build only.
   Similar dependencies: `serial_maint` already needs 80 ms because the factory `'Z'` reply follows a redraw (`tools/scenarios/serial_maint.json` note). `pc_mode`'s PB2 pulse and `:w11=0,0,` depend on when the main loop reacts to PB1.
3. **Our own UI baseline.** Add a `--baseline <dir|elf>` option to `diff.py` so the new UI's screens can be regression-tested against its own approved PNGs. The code already stores `.png`, `.rgb565` and a `sha1` per checkpoint (`tools/sim/scenario.py:125-137`). Today reference means the original only (`tools/diff.py:225-231`, `253`).
4. **Key-driven scenarios.** The generator traffic in these scenarios (per step: `:w24`, `:w11=1,1,`, `:w12`; the 10-command idle configuration on load, pause, stop and end) comes from the runner, not the UI. But the key sequence that triggers it is specific to the original UI. Options:
   - **(a)** Add intent events, e.g. `{"at":…, "intent":"load", "ch":1, "slot":4}`, expanded into key presses by a per-UI keymap file. `load_scenario` would have to accept the new event type; it currently rejects anything other than key/pb1/rx/checkpoint (`tools/sim/scenario.py:69-71`).
   - **(b)** Make it a design rule that the new UI keeps the hardware gestures of the original: G1/G2 → list, UP/DOWN, START load/run/pause, STOP, LOOP, POWER short/hold, G1+G2 at boot, Stop+Power. Then these scenarios stay valid for serial-only checking.
   - **(c)** Use a timing mode that checks content and order only.
5. **Keep both UIs buildable.** Keep `APP=fw` (the original UI, pixel-exact against the original) and add `APP=<new>` sharing `link*`, `storage*`, `calib*`, `factory*` and `runner*`. Run the full diff on `fw` and the serial-only diff on the new app. The protocol code is then covered twice.
6. **Checks that already don't depend on the UI:**
   - host tests: exact TX bytes via `CHECK_TX` (`tests/host/test_link.c:44`);
   - `tools/gx_serial_test.py`: real-device request/reply log and compare, with opt-in write, cal and erase tiers (`tools/gx_serial_test.py:14-19`, `124-176`, `258-279`);
   - `make conformance`: authenticated real-device conformance via the freqgen tool (`Makefile:76-88`);
   - `tools/gx_latency.py`: response-time regression (`tools/gx_latency.py:10-15`, `139-151`).

### Which scenarios are serial-only and which depend on the UI

Traffic counts come from the reference `result.json` files.

| Scenario | Inputs | Serial/PB2/SPI checked | What depends on the UI |
|---|---|---|---|
| `queries` | RX only | U1 14 / U2 13 TX lines | 1 home checkpoint |
| `serial_storage` | RX only | 48/42 TX, 43 SPI ops | 1 checkpoint |
| `serial_adversarial` | RX only | 36/10 TX, 12 SPI ops | 1 checkpoint |
| `cal_trim` | RX only | 8 SPI ops | 1 checkpoint |
| `serial_maint` | RX, including U3 factory port | U3 `'Z'` replies, PB2, 33 SPI ops, reset | factory `:a0` draws the calibration grid; `'Z'` timing depends on drawing |
| `pc_mode` | PB1 + RX | `:w11=0,0,` + PB2 pulse on entry, `:w15=0,0,` on exit | PC screen, already masked or informational |
| `boot_home` | none | idle configuration after the splash | splash duration moves the timing |
| `power_off` | POWER held 1.5 s | PB2 square wave about 4 s later, `stop_reason=power_off` | goodbye screen; the hold gesture |
| `cal_boot` | G1+G2 at power-on, DOWN, G1, START | `:r80=0,`, test signals, `:w60`, `:w61`, PB2 | calibration menu keys and screen |
| `list_nav` | keys only | only the boot idle configuration (10+10) | pure UI test; serial adds almost nothing |
| `ch1_program` | G1, DOWN×3, START×3, STOP, LOOP, POWER | 56/30 TX (per-step `:w24`/`:w11`/`:w12`, idle configuration) | key meanings |
| `g2_switch` | G1/G2/START/STOP | 26/43 TX | key meanings |
| `upload_run` | RX upload, then G1, DOWN×8, START×2 | `:ok`, SPI ops, step traffic every 1.01 s | list navigation to slot 9 |

## 1.2 Emulator: LCD rendering, keys, serial

**Entry points**
- `tools/emu.py` hands off to `tools/sim/emu_cli.py` (`tools/emu.py:422-428`).
- `tools/scenario.py` runs JSON scenarios (`tools/scenario.py:57-81`).

**CLI** (`tools/sim/emu_cli.py:35-57`)
- `--shot MS:FILE.png`, `--key FROM-TO:KEY[+KEY]`, `--rx MS:U1|U2|U3:TEXT`, `--allow-tx`, `--tx-log`, `--spi-image backup|path`, `--generators MS` (auto `:ok`), `--pb1`/`--pb1-at`, `--variant`, `--init-log`, `--allow-spi-write`.
- Violations (TX without `--allow-tx`, flash writes, SPI writes, faults) give exit 1 (`:129-135`).

**LCD to PNG** (`tools/sim/devices.py:11-128`)
- The ST7789 model decodes commands and keeps the 240x320 GRAM as big-endian RGB565 (`:17`). It handles CASET/RASET windows and MADCTL (`:79-86`, `94-106`).
- `image()` (`:115-128`) swaps bytes, XORs the colours if the inversion setting differs from the panel's, rotates 270° to the 320x240 view seen on the glass, shows black when the display is off or asleep, dims by 1/6 when the backlight is off, and scales 2x (640x480).
- Backlight is the PB12 soft-PWM duty over the last 3.2 ms (`tools/sim/machine.py:754-772`).
- Scenario checkpoints write `<name>.png`, `<name>.rgb565` and a `sha1` (`tools/sim/scenario.py:125-137`).

**Keys**
- `KEY_PINS` maps keys to active-low GPIOs (`tools/sim/machine.py:39-42`). `Inputs.add_key` supports `+` combos (`:73-77`); `driven()` pulls the pins low (`:93-103`).
- Scenario presses last 150 ms unless `ms` is given (`tools/sim/scenario.py:40`, `117-119`).
- Key edges are timeline events, so skipping over idle loops never jumps past a press (`tools/sim/machine.py:382-388`).

**Serial**
- `inject()` queues RX bytes at 8N1 frame timing (`tools/sim/machine.py:807-809`, `tools/sim/periph.py:427-435`).
- TX bytes are assembled into timestamped lines per port (`tools/sim/machine.py:774-805`).
- `GeneratorBoard` answers every `:w…` with `:ok\r\n` after a delay, plus canned `generator_replies` (`tools/sim/devices.py:259-278`).
- The W25Q16 SPI NOR is fully modelled and logs every operation (`tools/sim/devices.py:131-256`).

**Speed.** Reference runs take 7.7–114 s wall time for 7–19 s of simulated time (`result.json` `wall_s`).

**Reviewing screens without hardware**
- The `APP=uitest` pattern (`src/main_uitest.c:1-12`) shows one screen per second and calls the `uitest_done(id)` hook (`:51`).
- For a new UI, a similar gallery app plus `emu.py --shot` gives PNGs of every screen.
- There is no live or interactive viewer; all runs are batch.

## 1.3 Host tests (`tests/host`)

**Build** (`tests/host/Makefile`)
- One binary built from `test_link.c`, `mocks.c`, `w25q16.c` plus `src/link.c`, `factory.c`, `calib.c`, `storage.c`, `spiflash.c`, `state.c` (`:21-22`).
- `-DHOST_TEST`, `-Werror`, UBSan always, ASan with `ASAN=1` (`:13-19`).
- The SPI backup path is hard-coded (`:11`) and passed as argv (`:28-29`).

**Harness**
- `CHECK`, `CHECK_EQ`, `CHECK_BYTES` (`tests/host/harness.h:12-33`).
- Mocked UART logs per port plus a combined event log with `[W]` markers for ack waits (`:37-42`).
- `auto_ack` generator model and `inject_on_delay` (`:54-58`).
- W25Q16 model with violation counters (`:66-70`; `tests/host/w25q16.c:1-30`).

**Mocks**
- `mock/mock_board.h` replaces `board.h` (GPIOB, `delay_loop`, `NVIC_SystemReset`), selected through `#ifdef HOST_TEST` in `src/link_hw.h:2-5`. `src/spiflash.c:3` also has a HOST_TEST branch.
- `mocks.c` stubs `uart_*` (`:98-138`), `delay_loop`, which delivers acks and injections (`:142-165`), and the UI and LCD calls as counters: `lcd_fill_rect`, `ui_cal_draw_values`, `ui_cal_draw_value` (`:174-191`).

**Coverage** (`tests/host/test_link.c:1443-1457`)
- TX builders, `wait_ok`, `:g`/`:p`/`:n` frames, `w` mirroring, deferred `w`, access gate, factory port, calibration sequences, storage, and a byte-exact rebuild of the used sectors of a real SPI backup.
- Not covered on the host: `ui.c`, `gfx.c`, `lcd.c`, `app.c` (state machine and keys), `runner.c`, `keys.c`, `fmt.c`.

**How to add tests for new modules**
- `main()` lives in `test_link.c`, and `mocks.c` defines `lcd_fill_rect` as a counter. A test that links the real `gfx.c` would clash, so add a second binary target, e.g. `test_ui` with its own `mock_lcd.c` holding a RAM framebuffer that can be dumped to PPM or PNG.
- Or move to a small shared test runner and per-module mock sets.
- For a new UI, keep a pure model (state plus key events → actions and requests to the runner) separate from rendering, so key logic can be host-tested without the emulator.
- The runner (`runner.c`) is a good candidate: feed it ticks and assert TX lines with the existing UART mocks.

## 1.4 Build: app variants, feature flags, memory reporting

**Apps**
- `APP ?= fw` (`Makefile:11`); output goes to `build/$(APP)` (`:13`).
- Source groups by file-name prefix: `UI_SRCS = src/gfx*.c src/ui*.c`, `LINK_SRCS = link*/storage*/factory*/calib*`, `CORE_SRCS = app*/runner*` (`:33-35`).
- Per-app lists `MODULES_bringup|probe|uitest|linktest|fw` (`:37-41`); `SRCS = BASE + MODULES_$(APP) + src/main_$(APP).c` (`:43`).

**Adding a new app**
- Add `MODULES_<new> := src/state.c $(LINK_SRCS) $(CORE_SRCS) <new UI srcs>` and `src/main_<new>.c`.
- The prefix issue: the `ui*`/`gfx*` wildcard (`:33`) would pull new files into `fw` and `uitest`, causing duplicate symbols. Use a new prefix (e.g. `src/nx_*.c`) or narrow `UI_SRCS`.
- Assets are extracted from the user's original image at build time (`Makefile:51-52`; `tools/gen_assets.py:1-11`, pinned SHA-256 at `:22`). A new UI needs its own fonts and icons, generated at build time and licensed for redistribution.

**Feature flags**
- No mechanism exists yet. `CFLAGS` is at `Makefile:24-25`. Add e.g. `CFLAGS += $(CFLAGS_$(APP)) $(foreach f,$(FEATURES),-DFEAT_$(f)=1)`.
- The build directory is per app, so variants don't collide. Feature variants of one app would need `BUILD := build/$(APP)$(VARIANT)`.

**Memory reporting**
- `-Wl,--print-memory-usage` and a map file (`Makefile:26-27`); `arm-none-eabi-size` after the link (`:62`).
- Linker limits: FLASH 112 KB at `0x08004000`, RAM 36 KB, 2 KB minimum stack with an assert (`ld/app.ld:5-12`, `55`).
- Current sizes (`arm-none-eabi-size`):

| App | text (flash) | data | bss |
|---|---|---|---|
| `fw` | 31,372 | 72 | 11,196 |
| `uitest` | 23,104 | 72 | 10,484 |
| `linktest` | 17,232 | 128 | 9,524 |
| `probe` | 10,016 | 40 | 1,864 |
| `bringup` | 11,520 | 4 | 1,580 |

- For `fw` that is about 28% of flash and about 31% of RAM (plus the 2 KB stack reserve). Large static RAM users are `g_ch` (2 × 0x9B0 bytes) and `g_name_bmp` (1998 bytes) (`src/state.h`).
- Packaging: `GxCustomFW.bin` is encoded with ID `X2Vx` (`Makefile:67-68`).

---

# PART 2: Web research

## (a) Official GX Pro capabilities

**Generators and signal**
- 2 independent DDS generators, 2 outputs each (4 outputs).
- 0–40 MHz for all waveforms; resolution "8 decimal places across the entire frequency range"; 50 ppm.
- 20 Vpp max, 0.01 V steps, 50 Ω; waveform 10 bits x 1024; 300 MSPS.
- AM (Out 2 modulates Out 1); gating up to 5 kHz at any duty cycle; short-circuit protection.
- USB 115200 bps; 12 V 2 A input; weight 2000 g. [spooky2.com GX Pro](https://www.spooky2.com/product/spooky2-generatorx-pro/)

**Display and CPUs**
- 3.2" TFT, 320x240.
- "2 x 128kB ARM CPU with Firmware upgrade, 1 x 64 MHz advanced STM32 core CPU for data collection" (this matches the STM32G070 display MCU). [spooky2.com](https://www.spooky2.com/product/spooky2-generatorx-pro/)
- The Remote Kit page repeats older GX numbers (266 MHz ARM / 16 MHz STM8, 266 MSPS, 0.00001 Hz up to 40 kHz), so spec pages are inconsistent. [Remote Kit](https://www.spooky2.com/product/spooky2-portable-generatorx-pro-remote-kit/)

**Biofeedback**
- Measures current and phase angle, high-side detection, 100 Hz–40 MHz, 16-bit, 3.4 µA, 0.0015°. Scan takes about 6 minutes. PC only, never standalone. [spooky2.com](https://www.spooky2.com/product/spooky2-generatorx-pro/), [GX guide (archive.org)](https://archive.org/stream/Spooky2GeneratorXUsersGuide/Spooky2_GeneratorX_Users_Guide_djvu.txt)

**Standalone mode**
- 30 program slots, shared by G1 and G2; at most 200 frequencies per program (a longer program is cut to the first 200).
- One waveform, amplitude, offset and dwell per program. Only waveform, amplitude, offset, duty cycle, gate, dwell and the frequency list are transferred.
- Out 2 is the inverse of Out 1, synced.
- Not available standalone: sweeps (only the start frequency is used), chains, wobbles, amplitude ramps and reductions, schedules, biofeedback, peptide programs. Plasma below 100 kHz is moved up to a harmonic.
- Sources: [load/run GX Pro standalone](https://www.spooky2-mall.com/blog/how-to-load-the-program-and-run-generatorx-pro-standalone/), [offline vs online](https://www.spooky2support.com/hc/en-us/articles/360021107793--What-are-the-differences-between-GX-offline-mode-and-online-mode), [modes and settings](https://www.spooky2support.com/hc/en-us/articles/360002612453-Do-different-modes-still-have-different-settings-when-being-loaded-to-the-GeneratorX), [load programs](https://www.spooky2-mall.com/blog/how-to-load-programs-into-generatorx/)

**Keys** (Up, Down, Stop, Power, G1, G2, Loop, Start)
- G1/G2 open the program list, and "the selected generator panel will turn white". Up/Down select.
- Start selects the program ("ready"); Start again runs it. Loop runs it continuously and shows the Loop icon at the top right. Stop stops.
- Statuses shown: Running, Stopped, Paused, Looping.
- Short press Power turns the unit on; hold Power 2 s turns it off; Stop then Power enters programming mode.
- Sources: [GX guide](https://archive.org/stream/Spooky2GeneratorXUsersGuide/Spooky2_GeneratorX_Users_Guide_djvu.txt), [firmware update](https://www.spooky2-mall.com/blog/spooky2-gx-pro-firmware-update-instruction/), [zendesk](https://spooky2scalar.zendesk.com/hc/en-us/articles/17814203521819-How-to-Load-the-Program-and-Run-GeneratorX-Pro-Standalone)

**Firmware v7789 background**
- Spooky2's developer says on groups.io that the old GX Pro firmware limited offline frequencies to 10 digits, causing wrong frequencies; v7789 fixed it. He also says v7789 added "if the generator ever locks up, it will automatically restart and resume." [groups.io Spooky2](https://groups.io/g/Spooky2/topics)
- Our spec says the display firmware uses no IWDG/WWDG (`docs/spec/hardware.md:281`), so that restart-and-resume feature is probably on the generator boards. Worth verifying.

## (b) User pain points

| Pain point | Source |
|---|---|
| 30-slot limit; a 31st upload silently fails; deleting needs the PC software | [spooky2-mall comments (2023)](https://www.spooky2-mall.com/blog/how-to-load-the-program-and-run-generatorx-pro-standalone/), [how to use GX standalone video](https://www.spooky2videos.com/video/how-to-use-generatorx-standalone/) |
| Preset chains are not supported: only the first program runs, then it stops. Terrain needs 15 slots and a manual change every day | [tips page comments (2024)](https://www.spooky2-mall.com/blog/tips-for-using-remote-and-contact-preset-with-generatorx-standalone/), [chains](https://www.spooky2support.com/hc/en-us/articles/360053584233-Is-there-a-way-to-run-preset-chains-on-Generator-X-standalone-without-connection-to-the-computer), [Terrain offline](https://www.spooky2support.com/hc/en-us/articles/360037634794-Why-does-Terrain-Protocol-stop-after-running-an-hour-when-I-run-it-offline), [detox setup](https://www.spooky2support.com/hc/en-us/articles/360041215253-How-do-I-set-up-the-detox-program-with-GeneratorX-GX) |
| "I only see Ch1 on the display": Out2 content is not visible standalone | [comment (2025)](https://www.spooky2-mall.com/blog/how-to-load-the-program-and-run-generatorx-pro-standalone/) |
| "Generator is not running 24/7… switched itself off"; support points to schedule settings and "press Start first, then Loop". Shows the Start/Loop confusion and the lack of on-device status and diagnostics | [comment (2025)](https://www.spooky2-mall.com/blog/how-to-load-the-program-and-run-generatorx-pro-standalone/) |
| "Not intuitive… the whole system not very user-friendly and the documentation so far is really bad" | [load programs comments](https://www.spooky2-mall.com/blog/how-to-load-programs-into-generatorx/) |
| Worry about damaging the small screens by running overnight (a case for dimming or sleep) | same page, comment 22 |
| Long program names cut off because of "the limitation of the words" | same page, comment 27 |
| Display shows only 10 digits (34,427,675.27 Hz); users want more digits and decimals | [groups.io](https://groups.io/g/Spooky2/topics) |
| No way to see which firmware is installed | [firmware update comments (2024)](https://www.spooky2-mall.com/blog/spooky2-gx-pro-firmware-update-instruction/) |
| Power cut: no auto-resume, the manual procedure is PC-side | [power cut](https://www.spooky2support.com/hc/en-us/articles/360001890074-What-happens-if-I-have-a-power-cut-while-my-Spooky2-is-running) |
| GenX display flicker or dimming (hardware, capacitors) | [flicker](https://www.spooky2support.com/hc/en-us/articles/7607326692763-Why-does-GenX-display-occasionally-flicker-dim-rapidly-once-in-a-while) |

- **No explicit complaints** were found about a missing timer or battery indicator. The GX Pro runs on 12 V mains (a car adapter is possible) and has no battery.
- "No schedules standalone" is officially documented.
- The spooky2support.com pages could not be opened directly; their contents above come from search-engine excerpts.

## (d) UI/UX best practices for 240x320 and 8 keys

**Key roles**
- Give every key one fixed role everywhere. For example: UP/DOWN = move focus; START = OK / play-pause; STOP = back / cancel, never blocked by a confirmation; G1/G2 = channel select or left/right tab; LOOP = secondary action; POWER = system.
- Show on-screen key hints; avoid hidden long presses (the JDS6600 criticism).
- References: [Android TV navigation](https://developer.android.com/design/ui/tv/guides/foundations/navigation-on-tv), [ISO/IEC 17549-2 (4-direction menu navigation)](https://www.iso.org/standard/79017.html), [ISO/IEC 17549-3](https://standards.iteh.ai/catalog/standards/iso/8b2371d5-9c0f-4555-9339-cd5c1b08f7b2/iso-iec-17549-3-2023) (loops with separators, at most 5 levels, "back" item placement, visible focus).

**Lists**
- Auto-repeat that speeds up; wrap-around, but stop auto-repeat at the ends so users don't loop endlessly. [KaiOS](https://medium.com/design-at-kai/basic-navigation-for-smart-feature-phones-e0dff7c75fbe)
- Always-visible focus highlight; a position indicator such as "04/30".
- Critical functions at most 3 presses from home; a persistent status bar. [RJY](https://rjydisplay.com/engineering-the-interface-a-practical-guide-to-designing-intuitive-uis-for-a-tft-touch-display/)

**Contrast**
- WCAG 1.4.3: 4.5:1 for text (3:1 for large text). WCAG 1.4.11: 3:1 for icons and UI components. [WebAIM](https://webaim.org/articles/contrast/), [W3C 1.4.11](https://www.w3.org/WAI/WCAG21/Understanding/non-text-contrast.html)
- Computed for the original palette:

| Combination | Ratio | Result |
|---|---|---|
| White on teal `0x2B12` | 6.5:1 | OK |
| Yellow loop icon on teal | 6.05:1 | OK |
| White on grey `0x8430` (inactive panel) | 3.72:1 | fails normal text |
| Grey on white (PC-mode disabled frequency) | 3.72:1 | fails normal text |

- Don't rely on colour alone: pair status colours with an icon or text.

**Rendering on this panel**
- Flat colours, since gradients band in RGB565. Use near-black instead of pure black to hide backlight bleed.
- Sans-serif bitmap fonts with 2–4 bpp anti-aliasing; about 8–10 px is the lowest readable height.
- Fixed-width digits for changing numbers; redraw only the changed areas. Avoid small saturated red text on black. [TechBloat ILI9341](https://www.techbloat.com/ili9341-tft-lcd-tiny-anti-aliased-colour-text.html), [RJY](https://rjydisplay.com/engineering-the-interface-a-practical-guide-to-designing-intuitive-uis-for-a-tft-touch-display/)
- There is not enough RAM for a framebuffer (153.6 KB vs 36 KB). Draw directly with partial updates or a text or tile buffer. [IJCA TUI paper](http://article.nadiapub.com/IJCA/vol5_no4/16.pdf)

**Layout**
- Make the most important information (current frequency, status, time left) the largest; move settings to secondary pages; tidy, uncluttered screens. [industrial-tft](https://industrial-tft.com/posts/3-5-inch-industrial-tft-handheld-instruments/), [E2IP HMI guidelines](https://e2ip.com/wp-content/uploads/2024/03/Product-Guidelines-for-Embedded-Systems_E2IP-Technologies.pdf)
