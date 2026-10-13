# Does OpenGX Pro do everything the original does?

This page goes through the behaviour of the original display firmware (vendor v7789) and says,
for each point, what OpenGX Pro does and where that can be checked: the file and function, and the
host test or emulator scenario that covers it. It is the developer's record behind
[the comparison in the user guide](guide/compare.md). State: 2026-10-13, version 0.9.0.

Names used here:

- **nx**: the firmware released as OpenGX Pro (`APP=nx`, `src/nx/`).
- **classic**: the re-implementation of the original (`APP=fw`, `src/classic/`), source only.
- **shared**: a source file in `src/` that is compiled into both firmwares, so both run the same
  code for that part.

The column "Original" is taken from the behaviour specification in [`docs/spec/`](spec/) and from
[`DEVIATIONS.md`](../DEVIATIONS.md), which were recovered from the original image; nothing in this
page was measured on the original again. "Scenario" means an emulator scenario of
`tools/scenarios/`; which of them run against nx is listed under "How parity is tested".

| Status | Meaning |
|---|---|
| Same | the same behaviour, by the same shared code or by nx code that is tested against it |
| Different on purpose | changed deliberately; the difference is listed in `DEVIATIONS.md` |
| Fixed | an error of the original is corrected |
| Option | changed by default; "Original key behaviour" gives the original's behaviour back |
| Not implemented | left out in both open firmwares |

**Short answer.** Everything the original does towards the PC, the generator boards and the
stored data is either kept or changed on purpose; the parts tied to the vendor's device
protection are not implemented. The evidence is host tests and the emulated board. On a real unit
only a small part has been checked so far (the last section).

## Boot and power

| Original | nx | Status | Evidence |
|---|---|---|---|
| Splash of five vendor bitmaps for about 4 s; it ends early at the first factory-port command | A drawn splash of 1.5 s (Short 0.8 s, or off), any key skips it. The gate on the wire stays: no generator line and no reply to the PC before 4.44 s, or the first factory-port command | Different on purpose | `splash_draw`, `SPLASH_FULL_MS` in `src/nx/nx_scr_sys.c`; `nx_core_update_ready` in `src/nx/nx_core.c`, `NX_GEN_READY_MS` in `src/nx/nx_config.h`; host test `test_boot_gate` (`tests/host/nx/test_nx_core.c`); scenario `boot_home` |
| After the splash: HOME, or PC mode when USB is connected; both generators get the idle configuration of an empty program | The same lines to both generators at the gate, then the dashboard or the PC monitor | Same | `default_programs` in `src/nx/main_nx.c`; `nx_core_pcmode_task`; `play_step` in `src/nx/nx_runner.c` with `seq_idle`; `test_boot_gate`; scenario `boot_home` |
| G1+G2 held at power-on: calibration menu. `:r80=0,` to G1, a wait of about 110 ms for the reply, PB2 high, the test signal of index 0 | The same sequence | Same | `nx_core_boot` in `src/nx/nx_core.c`; host test `test_cal_boot`; scenario `cal_boot` |
| First-boot wipe of the calibration, licence check bound to the device, read-out protection forced to level 1, `:w00` written into the bootloader page | None of it. No code in `src/` writes the processor's flash or its option bytes | Not implemented | `DEVIATIONS.md`, "Policy"; `process_w` case 0 in `src/link.c` |
| POWER held 0.8 s: goodbye screen, 4 s, then the power-off signal on PB2. The press before it has stopped both generators | Both generators get their idle configuration first (at most 300 ms; not in PC mode or calibration), goodbye for 1.5 s, the same PB2 signal. A changed setting is saved and the resume record cleared, in the nx area of the SPI flash | Different on purpose | `NX_KEY_POWER_OFF_MS` in `src/nx/nx_keys.h`; `nx_core_power_off` in `src/nx/nx_core.c`; `nx_ui_goodbye` in `src/nx/nx_ui.c`; `power_off_signal` in `src/board.c` (shared); host tests `test_power_off`, `test_power_off_stops_both`; scenario `power_off` |
| POWER held from power-on on: the unit switches off again after about 5 s | A key that is down at the first sample is ignored until it is released once | Fixed | `nx_keys_feed` in `src/nx/nx_keys.c`; host test `test_power_on_hold` |
| No watchdog. A fault or an unexpected interrupt hangs the processor | Independent watchdog, 2 s. A fault writes a crash record and resets; after 3 abnormal resets in a row, each within 30 s of boot, the power-off signal instead | Different on purpose | `nx_wdg_start` in `src/nx/nx_wdg.c`; `nx_fault`, `nx_fault_record`, `nx_fault_boot` in `src/nx/nx_fault.c`; host test `test_nx_fault` |
| Backlight: software PWM at 625 Hz, fixed at 8 of 16 steps | Timer PWM at 1 kHz, 256 levels, a brightness setting, dimming and screen off | Different on purpose | `nx_bl_on_us`, `nx_bl_fade_to` in `src/nx/nx_bl.c`; `power_task` in `src/nx/nx_ui.c`; host test `test_nx_bl` |
| PB7 output: 16-step PWM at 625 Hz | Unchanged | Same | `TIM3_IRQHandler` in `src/nx/nx_bl.c` |
| Panel start-up: the variant word at 0x08003C00 selects command list A or B | The same lists from the same word, then the panel's landscape mode and 16-bit transfers | Same, then different | `lcd_init` in `src/lcd.c` (shared); `nx_lcd_init` in `src/nx/nx_lcd.c` |

## Keys and screens

| Original | nx | Status | Evidence |
|---|---|---|---|
| Key scan in the main loop with a blocking delay of 13.3 ms per pass | Keys sampled every 10 ms in the tick interrupt, two equal samples, then gesture events (press, release, hold, long, repeat, two keys together) | Different on purpose | `nx_tick_1ms` in `src/nx/nx_time.c`; `nx_keys_feed` in `src/nx/nx_keys.c`; host test `test_nx_keys` |
| HOME screen that asks to choose a program; G1 or G2 opens that generator's list | Dashboard without a program; G1, G2 or START opens the list | Same flow, new look | `draw_none`, `dash_key` in `src/nx/nx_scr_dash.c`; gallery `dash_idle`; scenarios `ch1_program`, `g2_switch` |
| Program list: 30 slots, 7 rows "NN. name"; the selection and the window are kept; G1 and G2 switch the target; START loads; STOP goes back without loading | The same | Same flow, new look | `lib_enter`, `lib_key` in `src/nx/nx_scr_lib.c` (`s_lib_sel`, `s_lib_top`); the list in `src/nx/nx_comp.c`; gallery `library`; scenarios `ch1_program`, `g2_switch`, `upload_run` |
| Key repeat in the list and the calibration menu: after about 0.28 s, then about every 26 ms | A setting (Slow, Normal, Fast). "Original key behaviour": 280 ms, then 26 ms | Option | `nx_ui_apply` in `src/nx/nx_ui.c`, `nx_keys_set_repeat` in `src/nx/nx_keys.c`; the two times of the option have no test of their own |
| Long names scroll | They scroll, with their own timing; can be switched off | Same idea | `nx_marquee_offset` in `src/nx/nx_gfx.c`; `nx_marquee_tick` in `src/nx/nx_comp.c` |
| Names drawn with the vendor font, any byte | Printable ASCII; other bytes show as "?" | Different on purpose | `nx_uc_name` in `src/nx/nx_uicore.c`; host test `test_names_and_slots` |
| One screen per generator with the states ready, running, paused, stopped; the frequency only while running; a loop badge | The same states plus FINISHED and "not responding"; the frequency also when not running; the other generator in a strip with its own loop badge | Same states, more shown | `state_icon_label`, `draw_status`, `draw_strip` in `src/nx/nx_scr_dash.c`; `build_vm` in `src/nx/nx_ui.c`; gallery `dash_ready`, `dash_running`, `dash_paused`, `dash_finished`, `dash_error` |
| START: play and pause, with the loop flag cleared | Play and pause keep repeat. Option: cleared | Option | `act_start` in `src/nx/nx_scr_dash.c`; `nx_core_act` (`NXA_PLAY`, `NXA_PAUSE`); host test `test_original_keys` (`tests/host/nx/test_nx_ui.c`); scenario `ch1_program` |
| LOOP: stopped, play with loop; running, pause with loop on | Stopped or paused: play with repeat. Running: repeat on or off, nothing is sent. Option: pause with repeat on | Option | `dash_key`; `nx_core_act` (`NXA_LOOP`); `test_original_keys` |
| STOP: stops the shown generator | The same on release within 0.5 s; held 1 s and released, it stops both | Same, plus one | `dash_key` (`STOP_ALL_MS`); `nx_core_act` (`NXA_STOP`, `NXA_STOP_ALL`); scenario `nx_keys` |
| POWER short: stops both generators | Screen off and on. Option: stops both, on the dashboard | Option | `global_key` in `src/nx/nx_ui.c`; `test_original_keys`; scenario `nx_keys` |
| Goodbye screen with vendor artwork | Own goodbye with the session time | Different on purpose | `bye_draw` in `src/nx/nx_scr_sys.c`; gallery `goodbye` |

## Program runner

| Original | nx | Status | Evidence |
|---|---|---|---|
| The idle configuration and the lines of a step, one line at a time, each waiting for the generator's `:ok` (about 40 ms at most) | The same lines from the same builder. Each generator has its own queue; a line waits for its own `:ok` or 40 ms | Same lines | `seq_idle`, `seq_play`, `seq_off` in `src/runner_seq.c` (shared); `play_step` in `src/nx/nx_runner.c`; `service_link`, `nx_txq_on_ok` in `src/nx/nx_txq.c`; host tests `test_runner_seq` (`tests/host/test_core.c`) and the differential test in `tests/host/nx/test_nx_runner.c`: 10,500 of 10,500 steps identical to the classic runner |
| G1's lines first, then G2's; the `:ok` of either generator ends a wait | Both generators at the same time; the order for each generator is unchanged | Different on purpose | `nx_txq_service` in `src/nx/nx_txq.c`; host tests `test_boot_gate` (both start at the same time), `test_link_order` |
| A step lasts dwell × 1.01 s | Exactly dwell × 1.00 s | Different on purpose | `current_step`, `nx_runner_tick_1ms`, `NX_DWELL_UNITS`; `test_nx_runner` |
| At the end: outputs off, or from step 1 again with loop. The program runs on while the list shows | The same | Same | `finished` in `src/nx/nx_runner.c`; the runner is called from `nx_loop_pass`, whatever screen shows; scenario `upload_run` |
| G2 takes its step parameters with G1's step index | Each generator uses its own step (as classic) | Fixed | `play_step` |
| A dwell of 0 after an upload: division by zero | Counts as 1 s (as classic) | Fixed | `current_step` |
| A step count above 200 plays the bytes behind the table | Ends after step 200 | Fixed | `step_count` in `src/nx/nx_runner.c` |
| Programs come only from the 30 slots of the SPI flash | The same for slots. nx adds sources of its own: chains, plans, the manual generator | Same, plus new | `program_load` in `src/storage.c` (shared) through `nx_core_act` (`NXA_LOAD`); `src/nx/nx_src.c`, `nx_chain.c`, `nx_manual.c` |

## PC mode

| Original | nx | Status | Evidence |
|---|---|---|---|
| Entry: PB2 high, `:w11=0,0,` to both generators, PB2 low. Exit: `:w15=0,0,` to both. Running programs are dropped | The same code, run once both generator queues are idle | Same | `core_pcmode_update` in `src/core_pcmode.c` (shared); `nx_core_pcmode_task` in `src/nx/nx_core.c`; host tests `test_pcmode` (`tests/host/test_core.c`), `test_pc_mode`; scenario `pc_mode` |
| The USB-detect input (PB1) is read once per main-loop pass | It must be stable for 15 ms | Different on purpose | `pb1_sample` in `src/nx/nx_time.c`; host test `test_nx_time` |
| A key first seen in the pass that enters or leaves PC mode is not acted on | A key that is down when PC mode starts or ends does nothing on the next screen (POWER keeps its meaning) | Same | `modes` in `src/nx/nx_ui.c`; host test `test_traffic` (`tests/host/nx/test_nx_ui.c`) for leaving PC mode |
| PC screen: both names scrolling in turn, four frequencies with up to 8 decimals, on or off, Out 2 following Out 1 | PC monitor: the same, plus waveforms, amplitudes, "gated", and the output frequency for wave tables with several cycles; a changed line is redrawn at most every 100 ms | Different on purpose | `pc_draw`, `pc_tick` in `src/nx/nx_scr_pc.c`; `nx_rx_tap`, `tab_cycles` in `src/nx/nx_diag.c`; `nx_fmt_pc_freq` in `src/nx/nx_uifmt.c`; gallery `pc_monitor`, `pc_monitor_sync`, `pc_monitor_info`, `pc_monitor_wcm` |
| The PC icon shows the state of the vendor's session handshake | One USB icon; the handshake value `:w91` is accepted as it comes | Different on purpose | `process_w` case 91 in `src/link.c`; `DEVIATIONS.md`, "Policy" |
| Keys do nothing in PC mode, except power off | START, STOP and LOOP do nothing and nothing is sent; UP and DOWN set the brightness; POWER short switches the screen off | Different on purpose | `pc_key` in `src/nx/nx_scr_pc.c`; `nx_core_act` returns `NX_REFUSED_PC`; `test_traffic` |
| Two sessions opened close together: the second generator's `:w91` answer never reaches the PC, because the first is handled a loop pass later | The same result by an explicit hold of 15 ms after the last `:w92` | Same | `link_task` in `src/nx/nx_loop.c`, `NX_W91_HOLD_MS`; host test `test_w92_pair`; scenario `pc_dual_session` |
| After a PC session with an upload, the runner sends the idle configuration built from that upload | The same | Same | `link_process` in `src/link.c` (shared); scenario `pc_upload_exit` |
| Replies come at the end of a main-loop pass of 13 to 27 ms | Replies come within about 2 ms | Different on purpose | `link_task`; scenarios with one-sided timing; [measurements](guide/measurements.md) |

## Serial protocol, storage, calibration

| Original | nx | Status | Evidence |
|---|---|---|---|
| Frames `:g`, `:p`, `:n` with `$`, `*`, `#`, `,`, `?`; replies `:ok`, `:err` and the dumps; the `#` dump sends 200 of the 400 values | The same code | Same (shared) | `link_rx`, `link_process`, `process_n`, `dump_program`, `dump_params` in `src/link.c`; host tests `test_g_frames`, `test_p_frames`, `test_n_frames` (`tests/host/test_link.c`, built with the classic rules); scenarios `queries`, `serial_storage`, `serial_adversarial` |
| Version query `:n00=$` answered `:Rev201`, always | The same answer | Same (shared) | `process_n`; `test_boot_gate`; scenario `queries` |
| After a line that was cut off, the value index stays for the next frame, also over an `:n` frame; values can land behind the frame's array | Every frame nx handles starts the next one at index 0; nothing is stored past an array | Different on purpose | `LINK_RX_EXACT`, `store_value`, `link_process` in `src/link.c`; host test `test_malformed_lines` (`tests/host/nx/test_nx_core.c`) |
| `:w11`, `:w14`, `:w15`, `:w24`, `:w25` from the PC are mirrored for the PC screen | The same code | Same (shared) | `w_immediate` in `src/link.c`; host test `test_w_mirror`; scenario `pc_mode` |
| `:w` commands 60, 62, 63, 64, 80, 92, 95, 96, 97 | The same code. `:w64` "on" lights the screen at the user's brightness; a `:w95` restart shows as a software reset in Diagnostics | Same (shared) | `process_w` in `src/link.c`; `nx_bl_tick_1ms` in `src/nx/nx_bl.c`; `classify` in `src/nx/nx_fault.c`; host test `test_w_deferred`; scenarios `serial_maint`, `cal_trim` |
| The display answers `:g`, `:p`, `:n` in PC mode only after the vendor's session handshake | Always answered (as classic). The same check in the generator boards is not part of this firmware and is unchanged | Not implemented | `link_gate_open` in `src/link.c`; host test `test_gate`; `docs/serial-commands.md`, "Session / access gate" |
| Name table, 30 program slots, calibration and its backup in the SPI flash | The same code and the same places | Same (shared) | `src/storage.c`; host tests `test_storage`, `test_real_backup`; scenarios `serial_storage`, `upload_run`, `cal_trim` |
| The display writes the SPI flash only for uploads, erase and calibration | nx also writes its own records, only in 0x080000–0x1FFFFF, never in PC mode; slot copy, move and erase write the slots and the name table as an upload does | Different on purpose | `inside`, `nx_flash_program` in `src/nx/nx_flash.c`; `src/nx/nx_kv.c`; `nx_slots_task` in `src/nx/nx_slots.c`; host tests `test_nx_flash`, `test_nx_store`, `test_nx_slots` |
| Calibration menu: 29 values, the keys, the test signals, `:w60` and `:w61` | The same code behind a new grid in the same positions | Same (shared) | `core_calmenu_key` in `src/core_calmenu.c`; `cal_apply_test` in `src/calib.c`; `nx_core_cal_key`; host tests `test_calmenu`, `test_cal_sequences`; scenario `cal_boot` |
| Factory connector: `:a` to `:l`, each answered with `Z` | The same code. Two differences on the screen: the screen clear of `:a0` and `:a1` only repaints, and `:e` to `:l` outside the calibration screen show a message instead of drawing | Same (shared) | `link_factory_execute` in `src/factory.c`; `lcd_fill_rect` in `src/nx/nx_render.c`; `events` in `src/nx/nx_ui.c`; host test `test_factory`; scenario `serial_maint` |
| Receive errors are never cleared; a busy SPI flash is waited for without limit | Errors are cleared in the receive interrupt; the wait is bounded and feeds the watchdog | Fixed | `irq` in `src/nx/nx_uart.c`; `wait_ready` in `src/spiflash.c` (shared) |

## How parity is tested

**Host tests** (`make -C tests/host`, on a computer; 2026-10-13: 971 + 19,198 + 22,592 checks, 0
failed).

- `test_link`: the shared protocol, storage and core code with models of the serial ports and the
  SPI flash, built with the classic rules; it also rebuilds the used sectors of a real SPI-flash
  backup when one is present.
- `test_nx`: the nx modules on a model of the board with a simulated clock and power cuts. Its
  runner test drives the classic runner and the nx runner with the same 10,500 random key, time
  and upload events and compares every line sent.
- `test_nx_assets`: the font, icon and text tables.

**Emulator scenarios** (`tools/diff.py`, 18 scenarios in `tools/scenarios/`). Each runs on the
original image and on a build, on the same emulated board. The classic build must match the
original in all 18: every screen pixel by pixel, the serial traffic line by line, PB2, the SPI
flash and how the run ends.

For nx the screens differ on purpose, so `--serial-only` compares everything except the pixels:
the lines on the three serial ports, PB2, the backlight state, the SPI flash in the original's
areas, and how the run ends. Times are judged one-sided: nx may be earlier, never later than the
tolerance; only the boot sequence must not start earlier either.

| Run against nx | Scenarios |
|---|---|
| With the same events | `queries`, `serial_storage`, `serial_adversarial`, `serial_maint`, `cal_trim`, `pc_mode`, `boot_home`, `cal_boot`, `power_off`, `pc_dual_session` |
| With the nx key map (the same intentions, other keys) | `ch1_program`, `g2_switch`, `upload_run`, `pc_upload_exit`, `nx_keys` |
| Classic only | `list_nav` (screens of the list), `serial_malformed` (nx differs on purpose, see `test_malformed_lines`), `pc_exit_key` (see `test_traffic`) |

Result for nx on 2026-10-13, on a local release build (`make release`, update file SHA-256
`24932724…e07e7`): 10 of 10 and 5 of 5 pass. The commands are in
[`CONTRIBUTING.md`](../CONTRIBUTING.md).

**Gallery** (`tools/nx_gallery.py`). Every nx screen is drawn from fixtures and compared pixel
for pixel with the 122 approved pictures in `tests/gallery/nx/`: by the host renderer, by the host
renderer with the image's switches, and by the real display driver on the emulated board. The
gallery shows that the screens do not change by accident; it does not compare with the original.

**Real unit.** Done so far, with development builds: the size test of the update (96, 104 and 110
KB images), a start of the firmware with the new screens, and Spooky2 playing a program on each
generator at the same time in PC mode ([`docs/nx/drop1-checklist.md`](nx/drop1-checklist.md),
section 8, step 2). An earlier nx build without the new screens passed 39 of 39 cases of the
conformance test on 2026-09-24 (`make conformance`, recorded locally): the same replies as the
original, apart from two measured values that the generator boards return. That test has not
been repeated with the release image. Before a release the maintainer goes through
[`docs/release-checklist.md`](release-checklist.md).

## What is still open

- **The device checklist.** Apart from the points above, every result in
  `docs/nx/drop1-checklist.md` is empty: the screens and keys, a stored program measured at the
  outputs, the PC monitor against Spooky2, uploads, the calibration menu, power off, resume and
  all features beyond the original. The user-level list is in
  [Known issues](guide/known-issues.md).
- **A key at the start of PC mode.** `modes` handles entry and exit alike; the host test covers
  only the exit, and the scenario `pc_exit_key` runs against the classic build only.
- **Key repeat of "Original key behaviour".** The times (280 ms, then 26 ms) are set in
  `nx_ui_apply`; no test checks them.
- **The original outside the scenarios.** The scenarios run about 71 % of the original's
  application code. The rest of the classic build was compared by reading
  (`DEVIATIONS.md`, "Raw output check"), with four rare situations left different. nx inherits
  this through the shared code.
- **Spooky2's buttons.** Load was seen in serial captures. Which commands Copy, Paste, Erase and
  Reset send was not captured; the commands themselves (`:w96`, `:w95`, `:w60` and so on) are in
  the shared code and in the scenarios.
- **Generator commands the display does not understand.** `:w13`, `:w15` and `:w40` are sent by
  Spooky2 to the generator boards; their meaning is unknown. In PC mode the display only listens.
- **Reply time on 0.9.0.** The published numbers are from a development build.
- **Hardware values.** Some constants are defaults that were not measured on the unit
  (`docs/nx/drop0-checklist.md`): the backlight frequency and the timing pages of the display
  test.
