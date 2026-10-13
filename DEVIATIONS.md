# Deliberate differences from the original firmware (v7789)

The goal is **the same features and behaviour as the original**. The firmware differs only where the
original is unsafe, has an error that is fixed here, or is tied to vendor protection. Every item
below is marked in the code with a `DEVIATION:` comment (40 marks); the bus traffic and the open
points of the raw output check further down are not marked. Ideas for new features are kept
separately in [`BACKLOG.md`](BACKLOG.md) and are not implemented.

## Current state (2026-09-23)

| Check | Result |
|---|---|
| Real device | flashed and used: boot, program list, loading and running programs, power off behave like the original |
| Differential test (`tools/diff.py`, original and ours on the emulated board) | 18/18 scenarios (incl. every serial command on both links, malformed lines and the factory port): screens pixel-identical, serial traffic identical in content and order, timing within 8–44 ms (62 ms for one factory reply after a redraw) |
| UI screens | compared pixel by pixel with the original by `tools/diff.py` (the row above); `APP=uitest` shows every screen in turn, as a test app for the emulator |
| Host tests (`make -C tests/host`) | 19198 checks pass, incl. rebuilding the used sectors of a real SPI-flash backup byte for byte |
| Display response time over USB (`tools/gx_latency.py`, median) | ours 22–23 ms, original 27–30 ms |

## Policy (vendor protection, device safety)

| Area | Original | classic build | Where |
|---|---|---|---|
| Read-out protection | forces RDP level 1 on first boot | never touches option bytes | `app.c` |
| Licence check (device binding) | a check bound to the device ID in the bootloader page gates PC access | no device-binding check: PC access is never gated; the option bytes and the bootloader page are not touched | `app.c` |
| PC access gate (`w91`) | serial writes only after a vendor handshake while PC-connected | always allowed; `w91` accepted with any value, PC icon always "unlocked" | `link.c` |
| `w00` | writes a word into the bootloader flash page | acknowledged, nothing written | `link.c` |
| First-boot SPI wipe | wipes calibration if a flag in the bootloader page is set, then clears it | skipped (the flag cannot be cleared without writing the bootloader page; on units that ran the original it is already clear) | `app.c` |
| Faults / unexpected interrupts | hang | run the original power-off signal, so the unit can switch off | `startup.c` |

## Timing

The bytes sent to the generators and the PC are the same, and the panel gets the same pixels in the
same order; some things happen earlier. The bytes on the panel's own bus differ ("Raw output check").

| Original | classic build | Where |
|---|---|---|
| Waits until the SPI bus is idle after every byte and toggles chip select for every pixel | streams through the SPI FIFO: screens draw about twice as fast. Anything that follows a redraw happens earlier (e.g. the idle configuration after the boot splash ≈ 44 ms earlier; display replies to the PC ≈ 5–7 ms earlier) | `src/lcd_bus.h` |
| Calibration boot: checks once, right after its slow screen clear (~0.1 s after `:r80=0,`), whether G1's `:w80=…` reply has arrived | waits for that reply up to the same point in time, so the same replies are shown regardless of drawing speed | `app.c` |

## Bug fixes

### Core and hardware (`app.c`, `runner.c`, `uart.c`, `state.h`, `spiflash.c`)

| Original | classic build | Where |
|---|---|---|
| UART error flags never cleared: an overrun can stall the main loop | errors cleared in the receive interrupt | `uart.c` |
| Main loop and interrupts race on the shared flags (a pending serial line can be lost) | atomic read-modify-write in main-loop code | `state.h` |
| Runner: G2 looks up its per-step (a, b) pair with G1's step index | each channel uses its own step | `runner.c` |
| Runner: division by zero (undefined step) after an upload with dwell = 0 | dwell 0 treated as 1 s | `runner.c` |
| SPI-flash busy wait forever | bounded wait | `spiflash.c` |

### Display (`gfx.c`, `ui.c`)

| Original | classic build |
|---|---|
| Names wider than the 1998-byte bitmap buffer overflow into other RAM | only the leading characters that fit are rendered |
| The marquee's 30-column gap reads past the buffer for names over 636 px | the gap is blank |
| Name slots ≥ 32 unchecked | ignored |
| Name width written incrementally (the TIM3 wrap test can see a partial width) | written once when complete |
| Marquee position after a full redraw depended on redraw timing | every full render that stops the marquee draws names at offset 0 and resets the offset |
| PC mode: a re-sent name that is not the scrolling one could disturb the scrolling bitmap | drawn static; the scrolling bitmap is kept |
| Marquee offset read several times per render | read once per render |
| PC-mode "up to date" bits set after reading the frequency | set before reading (no lost update) |
| PB12 written directly around frames (overwritten by TIM3 within 100 µs) | left out |
| Out1/Out2 label with an unknown code blits QR-code data | draws nothing (the UI never uses unknown codes) |

### Serial protocols and storage (`link.c`, `factory.c`, `calib.c`, `storage.c`, `fmt.c`)

| Original | classic build |
|---|---|
| USART2 `w60` reads USART1's arguments, no index check | uses its own arguments, index checked (`misc_flags = 0` kept) |
| `:ok` detection loose, missed while a line is pending | exactly `:ok`, also recognised while a line is pending |
| Name text: only a character arriving directly after 59 others drops the line; a CR there is stored, and later text is appended without limit (buffer overflow) | the same within the 63-byte buffer; what does not fit is left out and the line still counts (the original writes it into the RAM behind the buffer) |
| A value index left by an aborted `:g` or `:p` frame can lie past the array of the next frame's letter; its values are written into the RAM behind the array (G1's `:p` values into G2's program) | the index counts on as in the original; the values past the array are not stored |
| `w25` in sync mode blocks the link until the main loop runs | dropped immediately |
| `w91` redraws the PC icon according to the vendor handshake | accepted, icon stays "unlocked" (policy) |
| `w97` clears the two PC names without redrawing them | also redraws them |
| Numbers ≥ 10^16 misformatted | printed in full |
| Factory `:c` could write past the calibration table; index sent unchecked | bounded; index sent as its last two digits |
| `cal_save(1)` skipped the backup copy when the working copy was unchanged | backup copy compared and written on its own |
| `program_save` / `program_erase` accept any slot (`:n00=,` erased sector 0x10000) | only slots 1..30 |
| Factory parser state wraps | saturates |
| Calibration test exponents unchecked (values the menu never produces) | clamped |

## Kept on purpose

Original behaviour that looks odd but is visible to the PC software, which may depend on it:

- The `:n#` step-table dump sends only the first 200 of the 400 values.
- `w95 = 12021` resets the MCU.
- The main loop still paces itself with the original's 13.3 ms key-debounce delay, so key timing
  (hold, auto-repeat, power-off after ≈ 0.8 s) and the order of serial replies stay the same.
- The serial parser resets its value index only after a `:w`, `:p` or `:g` frame is *handled*: never
  at frame start, and not after an `:n` frame. So an aborted line carries its index into the next
  frames and `:w` argument slots keep earlier values — reproduced exactly for a 1:1 wire protocol.
  Within one letter the index stops at the last element; carried into a frame of a shorter type it
  counts on (what is then not stored: see the serial bug fixes). Proven by the `serial_adversarial`
  and `serial_malformed` differential scenarios.
- A key first seen in the main-loop pass that enters or leaves PC mode is not acted on: the original
  leaves out its key dispatch in that pass (`pc_exit_key` scenario).

## Raw output check (2026-10-12)

Every byte the original and the classic build put out on the emulated board was recorded for the
differential scenarios and compared. The scenarios run about 71 % of the original's application
code; the rest was compared with the classic sources from the disassembly of the original image.
That comparison found the two serial rows and the two "Kept on purpose" points added on this date.

| Output | Result |
|---|---|
| USART1, USART2 (generators, PC), USART3 (factory port) | identical bytes in every scenario |
| Panel | the same pixels in the same order; the bus bytes differ (below). Drawn differently: frames of the name area are repeated more often (the loop is faster), and the PC icon and the PC-mode values follow the policy above |
| SPI flash | the same content; the bus traffic differs (below) |
| PB2 | the same level changes; power-off square wave with the same levels, half-period 860–862 CPU cycles (original 862–863) |
| Backlight (PB12) | the same 625 Hz pattern, starting 24 ms earlier |
| Calibration test signals (29 indices, both tables), `:w50` / `:w51` lines, panel init of both panel variants, UART settings, pin levels at start | identical; not run by a scenario, compared from the code |

Bus traffic inside the board (not marked in the code):

| Original | classic build |
|---|---|
| Panel: one window (CASET, RASET, RAMWR) for every pixel it draws pixel by pixel (about 3,040 single-pixel writes for the boot screens); chip select pulsed for every byte | runs of pixels in one window; one chip-select window per command or data block |
| Name table save: one write of 1920 bytes (8 page programs) | three writes of 60, 1800 and 60 bytes (10 page programs) |
| Program load: reads 1640 bytes, then 800 | reads the 40-byte header first; a blank or invalid slot is not read further |
| `:nNN=*`: one read of 1640 bytes; `:nNN=#`: one read of 800 bytes | the header, then 8 bytes per frequency; 2 bytes, then five reads of 80 bytes |
| Flash write: starts with the write enable | a status read first |
| Sector erase: write enable, status read, erase, status read | status read, write enable, erase, status read |

Differences in rare situations, found by reading the original's code and not reproduced on the
emulator; left as they are:

| Situation | Original | classic build |
|---|---|---|
| A program with a step count above 200 (only from a serial upload, played from RAM) | steps 201–255 read behind its tables: G1 takes G2's program record as frequencies, G2 takes G1's step table | reads behind its own tables: other values |
| Calibration menu after a factory `:b` with an index of 32 or more | edits RAM behind the calibration table | also unchecked, other RAM: the value in `:w60=…` differs |
| An upload that arrives while a step's lines wait for `:ok` | reads waveform, amplitude, offset and the pair again before each line | reads them once per step |
| PB1 rising within about 1 µs between two reads in the first main-loop pass | enters PC mode without the `:w11=0,0,` lines and the PB2 pulse | the normal PC-mode entry |

## nx (`APP=nx`)

The nx firmware (`docs/nx/fw2-design.md`) shares the protocol core with the classic build: its serial
replies are byte-identical and in the same order. Differences:

| Original | nx | Where |
|---|---|---|
| After a line that was cut off, the value index it reached stays for the next frame, also over an `:n` frame (Spooky2 sends `:n00=$` all the time), so the next upload is stored shifted; text after a CR that follows 59 name characters is kept (the classic build follows both) | on purpose more forgiving: every frame nx handles starts the next one at index 0, an `:n` frame included, so a cut-off line cannot shift the next upload; such a name line is dropped. Like the classic build, nx stores nothing past the frame's array (host test `test_malformed_lines`). Only malformed lines differ | `src/link.c` (`LINK_RX_EXACT`) |
| After a `:w92` session handshake, the generator's `:w91` answer lowers PB2 only after the next pass's 13.3 ms key-scan delay | the same hold, made explicit: a pending `:w91` waits 15 ms after the last `:w92` sequence of either link (`NX_W91_HOLD_MS`), so when Spooky2 opens G1 and G2 close together, the second generator's answer never reaches the PC (scenario `pc_dual_session`) | `src/nx/nx_loop.c` |
| Replies at the end of a 13.3–27 ms main-loop pass (22–30 ms after the line) | replies within about 2 ms: the loop never blocks on keys or the runner. Every nx reply trace is one the original can produce at some loop phase (`docs/nx/design/core.md` §2.4 item 7) | `src/nx/nx_loop.c` |
| A step lasts dwell × 1.01 s (the original counts 10.1 ms units) | exactly dwell × 1.00 s (millisecond SysTick) | `src/nx/nx_runner.c` |
| G1's command sequence, then G2's; any link's `:ok` ends the wait | both links in parallel, each waiting for its own `:ok`; the order per link is unchanged | `src/nx/nx_txq.c` |
| A program count above 200 plays the bytes behind the frequency table as steps 201–255 | ends after step 200 | `src/nx/nx_runner.c` |
| PB1 sampled once per loop pass | PB1 must be stable for 15 ms | `src/nx/nx_time.c` |
| POWER short press stops both generators; power-off signal 4 s after the goodbye screen | POWER short switches the screen off/on, STOP held 1 s (then released) stops both; Settings > Keys & lock > "Original key behaviour" (off by default) gives back POWER short = stop both, on the dashboard. POWER held stops both generators first, as the original (their idle configuration, at most 300 ms; not in PC mode or calibration); the power-off signal follows 1.5 s after the goodbye, whose last 0.3 s fade the backlight out and put the panel to sleep (DISPOFF, SLPIN) | `src/nx/nx_scr_dash.c`, `src/nx/nx_ui.c`, `src/nx/nx_core.c` |
| POWER is the key that turns the board on, so it is usually still held when the app starts; the original only samples keys from its first main-loop pass (~4.4 s), so it treats a still-held POWER as freshly pressed there — a hold of about 5 s from power-on switches it off | keys are sampled from `__enable_irq()`, well before the splash ends; a key already down at the very first sample is ignored until it is released once, so holding POWER continuously from power-on never switches the unit off by itself, however long — only a release, then a fresh 0.8 s hold, does | `src/nx/nx_keys.c` |
| A fault ends in the power-off signal | a crash record in `.noinit`, then a reset; the power-off signal only after 3 abnormal resets in a row, each within 30 s of boot | `src/nx/nx_fault.c` |
| After a PC session the runner resends the idle configuration built from the last upload (when the upload forced it) | the same: the channel's program buffer is the upload buffer (runner v2 with its own sources changes this later) | `src/nx/nx_runner.c` |
| Loading a program sends its idle configuration (waveform table `w20`/`w21`, amplitude `w28`/`w29`, offset); START, a later key press, sends the step. The original never loads and plays in one key press | nx also loads and plays in one pass: a chain's next program, a Terrain set, the manual generator. Then the new program's idle configuration goes out first and the step on the next pass (`nx_runner_loaded`, called from `NXA_LOAD`). Before this fix (2026-09-25) a chain switched programs without sending the new waveform and amplitude. Slot playback through the keys is unchanged (runner differential test, emulator gates) | `src/nx/nx_runner.c`, `src/nx/nx_core.c` |
| Standalone, only stored slots play (uploaded programs, waveform tables 41–70); the Terrain Protocol runs from Spooky2 on the PC | the Terrain Protocol is built into the image (`NX_FEAT_TERRAIN`; tables made at build time from the user's own presets, never committed; no new serial command): Terrain, Terrain-Mercury, Terrain-Nonviolent as Remote and Contact protocols (`docs/nx/features.md` §9.12). Differences from Spooky2: Contact sets play with Out2 following Out1 inverted, as any Spooky2-uploaded program plays standalone; not reproduced: the Contact shell's Out2 octave carrier (1 MHz or more), its amplitude ramp up and down, its low-frequency amplitude reduction; the end of a Contact session is a card and three backlight pulses, not a beep (the board has no buzzer); the Plasma, Scalar, Laser and Coil versions are not included (their shells use gating, offset modes and harmonics; not checked); the Remote chain counts running time, so a pause or a power-off does not count, as Spooky2 counts only while it runs. The sets play the generator tables 11–19 (Spooky2 waves 1–9) standalone, which the original never does: not yet checked on the device | `src/nx/nx_terrain.c`, `src/nx/nx_scr_terrain.c`, `tools/gen_nx_terrain.py` |
| No manual generator | menu > Manual (`NX_FEAT_MANUAL`): a fixed frequency (0.01 Hz – 40 MHz) or a sweep, with a waveform (tables 11–19) and an amplitude (0–20 V), on G1 or G2, loaded like a one-step program. A frequency change sends one `:w24` line; a waveform or amplitude change sends outputs off, the new idle configuration, outputs on. A sweep sends a `:w24` line every 125 ms while the link's queue is idle (Spooky2's own sweeps: about 134 ms). Out2 follows Out1 inverted (`docs/nx/features.md` §9.14) | `src/nx/nx_manual.c`, `src/nx/nx_scr_manual.c` |
| The PC screen shows the frequency of `:w24` / `:w25` as sent | with a wave table of several cycles (Spooky2's Wave Cycle Multiplier: it sends the frequency divided by their number) the PC monitor shows the output frequency, wire × cycles rounded to 6 decimals, and the cycles: "396.00 Hz ×11" where the original shows 36.00 Hz. The receive tap finds the cycles of the uploaded tables (`:aNN=`): evenly spaced crossings of the middle, or crossings on one grid (Spooky2's H-Bomb tables: ×16); `docs/nx/features.md` §9.7. It also shows, per output, the waveform and the amplitude the PC sent (peak to peak, as Spooky2's own output pane: 10.0 V for a 20 V preset at a 100 % offset). The four frequencies are read with the interrupts masked (a code of 2^32 or more could show half old, half new for one redraw). "= OUT 1" no longer runs off the screen behind a wide number | `src/nx/nx_diag.c`, `src/nx/nx_scr_pc.c`, `src/nx/nx_uicore.c` |
| The display sends short `:w` lines to the generators, both outputs with one waveform table and one offset; programs with sweeps play only from Spooky2 on the PC | a Spooky2 preset with sweeps is built into the image (`NX_FEAT_PRESET`; its table made at build time from the user's own preset, never committed; no serial command of the display is new). For it the display sends what Spooky2 sends: the two wave tables (`:a11=`, `:a25=`, 1024 samples each, 4103 bytes a line), Out2 apart from Out1 (`:w17=0,0,` `:w21=25,` `:w33=220,`), and in a sweep a `:w24` line every 125 ms with the frequency / 11 (`docs/nx/features.md` §9.17). Differences from Spooky2: a step lasts 11.00 s (Spooky2: 11.02–11.09 s); the first sweep starts at its start (Spooky2's first pass starts 0.1 s in); the runner's idle configuration and `:w11=1,1,` / `:w12=0,0,` go out as for any program; `:w15` stays 0,0 (Spooky2: 1,1 in its sessions). Not yet checked on the device | `src/nx/nx_preset.c`, `src/nx/nx_txq.c`, `src/nx/nx_runner.c`, `tools/gen_nx_preset.py` |
| The display never changes a generator's wave tables | before the manual generator or a Terrain set plays the sine (table 11), nx uploads Spooky2's standard sine there (`:a11=`, one cycle at full scale, byte for byte the table Spooky2 uploads without a Wave Cycle Multiplier), once per power-on, PC session or built-in preset: a Spooky2 preset, or nx's own, can leave several cycles in that table, and "Sine" would play that many times too high. A generator that does not answer the table: the sine plays as before after the 2 s wait. Stored slots are not touched (their lines stay the original's). Not yet checked on the device | `src/nx/nx_preset.c` |
| Screens: home, program list, one screen per channel, PC screen, calibration grid (vendor bitmaps) | a new UI in English (`docs/nx/key-model.md`): dashboard with the focused generator and a strip for the other, the program list (the original's flow, rows in a framed box) and program detail, quick settings sheet, a menu on DOWN, settings (with reset and licences), a key guide at the first boot, a night view when idle, PC monitor, the calibration grid in the same positions (plus a title with the test steps and two help lines), and the pages of the extra features (Timers, History, Diagnostics, Chains, Terrain, Manual); open fonts and icons only; the original's ghost splash only in local builds made from the user's own original image (never committed); the splash is black in every theme, as the original's; the full list: `docs/nx/features.md` | `src/nx/nx_ui.c`, `src/nx/nx_scr_*.c` |
| START on a channel screen runs with the loop flag cleared; LOOP while running pauses with loop on | START keeps the repeat setting when it pauses or resumes; LOOP while running only switches repeat on/off (nothing is sent); stopped, LOOP plays with repeat as before. "Original key behaviour" gives back the original's START and LOOP | `src/nx/nx_scr_dash.c` |
| Key repeat in the list and the calibration menu after about 0.28 s, then about every 26 ms | a setting: first repeat after 600 / 400 / 300 ms (Slow / Normal / Fast), then every 180 / 110 / 70 ms, every 50 ms once held 1.5 s; "Original key behaviour": 280 ms, then 26 ms | `src/nx/nx_ui.c`, `src/nx/nx_keys.c` |
| No settings | brightness, theme, night mode, dimming, screen off, key repeat, auto-lock, resume, original key behaviour, splash, motion and the timers (menu > Timers) are kept in the nx settings record (SPI flash 0x082000+), written 3 s after the last key when no generator line is due, never in PC mode | `src/nx/nx_uiset.c`, `src/nx/nx_ui.c` |
| The display writes the SPI flash only for Spooky2 uploads, `w96` and calibration | nx also writes its own records, only in 0x080000–0x1FFFFF (an area the original never uses): in the KV log at 0x082000 the resume snapshot, the last started programs for auto-start (only while that setting is on), the chains, the plans, the history totals, the Terrain positions and the manual generator's values; the history ring at 0x0A0000 (32 sectors). Only when no running step ends within 100 ms (a whole quiet second before a sector erase or a KV compaction), never in PC mode; every write read back and compared | `src/nx/nx_kv.c`, `src/nx/nx_resume.c`, `src/nx/nx_chain.c`, `src/nx/nx_hist.c` |
| Program slots and names change only through Spooky2 (`:n`, `:p`, `w96`) | slot copy / move / erase on the device (program detail > LOOP) writes the legacy areas with the writes of an upload: the target's program sector erased and written (record and gating pairs) in 256-byte pieces, each read back and compared, then the name table rewritten; a move then erases the source. Queued until no running step ends within a second (1.5 s for a move), dropped when PC mode starts or after 10 s without such a moment; never in PC mode; refused for a slot a generator has loaded or a target that holds a program | `src/nx/nx_slots.c` |
| Backlight always at the fixed duty | dims after a settable time without a key, then switches off; a finished program, a timer stop, a generator that stops answering, PC mode or the PC's `w64` on wake it; the end of a Terrain Contact session wakes it and pulses it 3 times. The PC monitor stays lit, as the original's PC screen | `src/nx/nx_ui.c` |
| The PC screen redraws a changed frequency on every main-loop pass (13–27 ms) | a changed frequency line is redrawn at most every 100 ms, with the latest value, so a fast sweep reads as a sweep; nothing sent changes | `src/nx/nx_scr_pc.c` |
| Names drawn with the vendor font (any byte) | names show printable ASCII; other bytes appear as "?" | `src/nx/nx_uicore.c` |
| After a power cut both generators start empty | what played is kept in the KV store (written on a change and once a minute while running, never in PC mode), with its position in a chain, plan or Terrain protocol (not the manual generator); after a power cut or a crash the dashboard offers to resume it (START now, STOP discard, by itself after 15 s) at the saved position, if the slot still holds the same program. A clean power-off, both generators stopped or a PC session clear it; Settings > Keys & lock switches it off | `src/nx/nx_resume.c`, `src/nx/nx_ui.c` |
