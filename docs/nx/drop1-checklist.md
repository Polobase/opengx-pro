# Drop 1 device checklist (nx firmware with the new UI)

With the device on the desk, generator outputs disconnected unless a step says so. Each
flash: power off, hold **Stop**, press **Power**, then the `make` command. `make flash-original` (the
original) or `make flash` (the classic build) bring the device back at any time; stop and do that
when anything looks wrong. Fill in the result column; photograph screens that look odd.

The UI is in English only (no language setting). DOWN on the dashboard opens the menu (Programs,
Chains, Manual, Timers, Diagnostics, Settings). The keys and screens are in
`docs/nx/key-model.md`; everything nx does compared with the original: `docs/nx/features.md`.

Since 2026-10-13 every build, and the released image (`make release`), has the Terrain Protocol
and the preset with sweeps from the committed tables in `src/nx/data/`: 108,456 bytes (106,538
bytes on 2026-10-06, when the tables were made at build time from `TERRAIN_DIR` / `PRESET_FILE`
in an untracked `local.mk`). So §23, §24 and §28 need no preset files (§23 and §24 still use the
test build with fast time). `make release` is built without the vendor splash. Off by default
since 2026-10-05: the ghost splash (§9), History (§15) and the 48 px frequency digits; the
dashboard shows the frequency in 28 px digits.
The bootloader is proven up to 112,640 bytes (`FLASH_PROVEN`, the size test of 2026-09-25), which
is also the build budget. Order: §1, then §8 (both generators in PC mode, the reported problem),
then the rest. The Terrain checks (§23, §24) use a test build whose time runs faster; §25 goes
back to the normal build; §27 and §28 check the PC monitor's output frequency and the built-in
preset.

## 0. Before

| Step | Result |
|---|---|
| Image size accepted by the bootloader: 77 380-byte update file (38 blocks) flashed and runs, 2026-09-25 (`docs/nx/drop0-checklist.md` §1) | done |
| Size test (`docs/nx/drop0-checklist.md` §1): 96, 104 and 110 KB all "CRC OK" on 2026-09-25; `FLASH_PROVEN` = 112,640 | done |
| `make APP=nx` links; `make APP=nx nx-size` shows the image size | |
| For the ghost splash (§9) only: `original/GxDisplayFW.bin` is there and the image was built with `NX_VENDOR=1`; `git status` shows no new file (`build/nx/nx_vendor.c` stays in `build/`) | |
| The build has the Terrain Protocol (§23) and the built-in preset (§28) from the committed tables `src/nx/data/nx_terrain_data.c` and `src/nx/data/nx_preset_data.c` (since 2026-10-13; no preset folder is needed); `git status` shows no changed file after the build | |
| The original's recorded replies exist: `build/conf-original.json` (2026-09-23) | done |
| `make flash-original` works once (recovery path) | |

## 1. Flash

`make APP=nx flash`.

| Check | Result |
|---|---|
| Splash on black: the wordmark "OpenGX Pro" (its "O" is the ring with the sine), "Open firmware for the GeneratorX Pro", "v0.9.0" at the foot, "any key: skip"; any key skips it. (A build with `NX_VENDOR=1`: the original's Spooky2 ghost fades in, §9.) | |
| First boot: the key guide after the splash; a key closes it and acts (e.g. G1 opens the program list) | |
| Upright, not mirrored, colours right (G1 cyan, G2 amber) | |
| Every text in English; a text too long for its place ends in "…" | |

## 2. Walk every screen

| Screen | What to check | Result |
|---|---|---|
| Dashboard, no program | "No program loaded", how to choose one, "DOWN: Menu · UP: Quick settings" | |
| Program list (G1 without a program, or G1 twice) | like the original's list: "Program G1", 7 rows "NN. name" (an empty slot as "Slot NN", it loads the empty program), the selected row a G1-coloured bar; G1/G2 switch the target; START loads and shows that generator; STOP shows it without loading; long names scroll | |
| Load (START), play (START) | READY with a check icon after loading; hero frequency, step n of N, time per step, amplitude + waveform, live scope, bar + time left, next frequency, strip of G2 | |
| Step change | the changed digits roll up | |
| Pause / resume / stop / LOOP | PAUSED (grey), the LOOP badge on and off, STOPPED with a stop icon after STOP | |
| STOP held 1 s | ring, then both stop at the release | |
| LOOP held on the dashboard | the program detail of the loaded program at its playing step; nothing without a program | |
| Program details (LOOP held in the list) | frequencies, the playing step marked; START plays on the target; on the target's own loaded program START only returns to its dashboard (it keeps running) | |
| Finished | "Program complete" card, the screen wakes if dimmed | |
| G2 | the screen slides; G2's dashboard; the strip shows G1 | |
| Quick settings (UP) | brightness steps, night mode, lock, screen off, auto-dim, reduce motion | |
| Settings (DOWN, then Settings) | every page (Display, Keys & lock, Motion, About: version and panel); theme Light / High contrast readable | |
| Key lock | G1+G2 held: locked, keys show the card; START+STOP held: unlocked; POWER still works | |
| Screen off / dim | POWER tap; wait for dim (1 min default) and screen off | |

## 3. PC mode with Spooky2

Do §8 (both generators at once) first.

| Check | Result |
|---|---|
| USB connected: PC monitor (names, frequencies, outputs, sync); START/STOP/LOOP do nothing; the screen stays lit | |
| `make conformance PORT=<link A> COMPARE=build/conf-original.json RECORD=build/conf-nx-ui.json` (Spooky2 closed): all replies identical | |
| `.venv/bin/python tools/gx_serial_test.py run --save build/serial-nx.json`, then `compare build/serial-fw.json build/serial-nx.json`: identical | |
| `.venv/bin/python tools/gx_latency.py --save build/latency-nx.json`, then `compare build/latency-nx.json build/latency-fw.json`: nx faster (≈ 3-5 ms) | |
| Spooky2 session: upload a program to a slot, run it from Spooky2, a biofeedback scan: works as with the original | |
| USB unplugged: the dashboard again; the uploaded slot is in the program list | |

## 4. Standalone run (outputs connected, oscilloscope)

| Check | Result |
|---|---|
| A stored program on G1 and another on G2 at the same time: frequencies and dwell on the scope match the program (dwell exactly N s) | |
| Loop: runs again, "LOOP ×2" | |
| The waveform shown matches the scope for codes 11-19 (else note the code: WAVEMAP, Drop 0 T8) | |

## 5. Calibration, power-off, recovery

| Check | Result |
|---|---|
| G1+G2 held while switching on: the calibration grid, same layout as the classic one; UP/DOWN/G1/G2/START behave as in the original; a factory-jig value marks its cell | |
| POWER held while both play: both outputs stop first (scope), then ring from 0.2 s, goodbye, the screen fades and goes dark, the device switches off | |
| Settings survive a power cycle (theme, brightness) | |
| `make flash-original`, then `make APP=nx flash` again | |

## 6. Resume after a power cut

| Check | Result |
|---|---|
| G1 and G2 play (one paused), wait over a minute, then pull the power plug | |
| Power on: after the splash "Resume where it stopped?" with a filling ring; START: both continue within a minute of where they were (scope), the paused one stays paused | |
| Again, but press nothing: after 15 s it resumes by itself | |
| Again, STOP on the card: nothing is loaded; the next power-on offers nothing | |
| POWER held (clean power-off) while playing, then on: no card | |
| Spooky2 session (USB) while playing, unplug, power cycle: no card | |
| Settings > Keys & lock > Offer to resume off, pull the plug while playing: no card | |

## 7. Restored screens and details

| Check | Result |
|---|---|
| Settings > Keys & lock > Show key guide: the guide; START closes it | |
| Settings > About: Project, Firmware, Developer, Panel, "Support the project" (opens a page with a QR code of buymeacoffee.com/mahapo), then the licence lines (scroll down) | |
| Settings > Reset all settings: START shows "Reset all settings?"; STOP cancels; START held 1 s resets (theme, brightness back to defaults) | |
| Settings > Display > When idle: Night view; wait for dim + off time: night colours, both generators, very dim; a key only wakes | |
| Hint line at the bottom of Settings, quick settings, the menu, the program detail and the PC screen | |
| Calibration: title "Calibration", "TEST FREQ. n  TEST LEVEL n" changes with STOP / LOOP, two help lines at the bottom | |
| Goodbye: "This session h:mm:ss" under "Goodbye" | |
| First ~4.4 s after power-on: "STARTING" with an hourglass in the status bar; START then shows "Generators are starting…" | |
| Toasts: STOP "G1 stopped", STOP held "Both generators stopped", LOOP while running "Repeat on/off", loading "Program NN loaded on G1", a finished program "G1 finished · name", unplugging USB "PC disconnected" | |
| Empty dashboard: a dashed ring with "+"; texts too long for their place end in "…" | |

## 8. Spooky2 with both generators in PC mode at once

The reported problem (earlier nx image): with both generators in PC mode at once, Spooky2 turned
the device yellow, programs started and finished within a second, and the frequency flashed random
numbers. nx now holds a generator's `:w91` answer 15 ms after the last `:w92`, as the original does
by its loop timing. Do this right after §1.

| # | Step | Expected | Result |
|---|---|---|---|
| 1 | Power on without USB, wait for the dashboard, then connect USB and open Spooky2 | PC monitor ("PC MODE"), the screen stays lit | |
| 2 | In Spooky2, start a program on G1 and one on G2 at the same time (both generators selected, one Start) | Spooky2 shows both generators normally (not yellow); both programs run their full time; the frequencies change step by step in Spooky2 and on the PC monitor, no flashing random numbers | 2026-09-25: passed — both programs run in parallel, as on the original. A Biofeedback scan on one generator pauses the other and resumes it when the scan stops: Spooky2's own behaviour, the same with the original |
| 3 | Let both run 5 minutes, then stop both in Spooky2 | both stop; Spooky2 shows no error | |
| 4 | Repeat steps 2–3 three times, once starting G2 first and G1 right after it | as step 2 each time | |
| 5 | Close Spooky2, unplug USB (do not switch off: the counters count since power-on) | the dashboard, "PC disconnected" | |
| 6 | menu > Diagnostics, read the PC mode row "N in · N out · N w91 held" and the G1 / G2 link rows | in = out = the number of USB connections; "w91 held" is 1 or more when two sessions opened close together (0: the hold was never needed); link rows with a green dot and 0 errors. Note all numbers | |
| 7 | If the problem is still there: note what Spooky2 shows, photograph the PC monitor, then `make flash-original` and try step 2 with the original for comparison | | |

## 9. The original's ghost splash

Only for an image built with `make APP=nx NX_VENDOR=1` (off by default since 2026-10-05).

| # | Step | Expected | Result |
|---|---|---|---|
| 1 | Power on (Settings > Display > Splash screen: Full) | on black, the ghost logo, the red "2", "Welcome to Spooky", the red "2" and the tagline fade in at the original's places within about 0.6 s; "v0.9.0" bottom left, "any key: skip" bottom right; the dashboard after about 1.5 s | |
| 2 | Splash screen: Short, power cycle | the same, faster (about 0.8 s) | |
| 3 | Splash screen: Off, power cycle | no splash | |
| 4 | Any key during the splash | skips it and does nothing else | |

## 10. Menu (DOWN)

| # | Step | Expected | Result |
|---|---|---|---|
| 1 | After power-on, DOWN on the dashboard | a sheet "Menu": Programs, Chains, Manual in the first row, Timers, Diagnostics, Settings in the second; Settings selected | |
| 2 | G1 / G2, UP / DOWN | the selection moves along the row and between the rows, wrapping; from Timers, DOWN goes to Settings (the second row has 3 cells) | |
| 3 | Programs, START | the program list of the focused generator | |
| 4 | STOP back to the dashboard, DOWN again | the menu opens on Programs | |
| 5 | STOP | the menu closes | |

## 11. Diagnostics and key test

| # | Step | Expected | Result |
|---|---|---|---|
| 1 | menu > Diagnostics | Firmware "0.9.0 · ST7789 A" (or B), Uptime counting up and "boot N", Last reset "Power on", G1 link and G2 link with a green dot and a few ms, PC mode, SPI flash (an ID, KB free, 0 errors), Main loop, MCU; UP/DOWN scroll | |
| 2 | MCU row (after 10 min switched on, room temperature noted) | the temperature near room temperature (the chip may read a few degrees above it), the supply about 3.3 V (3.2–3.4 V). Note both values and the room temperature | |
| 3 | Watch the MCU row for a minute; START | it updates every second without jumping more than about 1 °C / 0.02 V; START reads it at once | |
| 4 | LOOP | the key test: 8 key caps | |
| 5 | Press each key once, then hold one, then press two together | the cap lights while held; "START pressed", "START released after N ms", "held", "held long", "START+STOP together"; nothing else happens | |
| 6 | Hold STOP 1.5 s | back to Diagnostics | |

## 12. Original key behaviour and the original's details

| # | Step | Expected | Result |
|---|---|---|---|
| 1 | Settings > Keys & lock > Original key behaviour: on | the switch on | |
| 2 | G1 and G2 play; POWER short on the dashboard | both stop, "Both generators stopped"; the screen stays on | |
| 3 | Quick settings > Screen off | the screen goes off (POWER no longer does it) | |
| 4 | G1 plays with LOOP on; START | pauses, the LOOP badge goes off; START again plays without repeat | |
| 5 | G1 plays; LOOP short | pauses with the LOOP badge on | |
| 6 | Program list, hold DOWN | the selection moves after about 0.28 s, then very fast (about 40 rows per second) | |
| 7 | Original key behaviour: off | POWER short switches the screen off again; the Key repeat setting applies | |
| 8 | G1 with LOOP on, G2 shown | the strip at the bottom shows G1's LOOP badge | |
| 9 | Program list: scroll to slot 20, STOP, open the list again | the same selection and the same 7 rows | |
| 10 | Spooky2 session with names in both generators' programs | the two names take turns scrolling; frequencies with up to 8 decimals; a synced Out2 shows Out1's value with "= OUT 1" | |
| 11 | If Spooky2 can switch the display off and on (`w64`): off, then on; also on after POWER switched the screen off | off darkens it, on lights it again, also after POWER | |
| 12 | Only with the factory jig or a PC tool that sets calibration values (`:c`, `w60`/`w62`/`w63`/`w80`), while the calibration grid shows | the changed value appears in the grid at once | |

## 13. Timers (use 5 min)

| # | Step | Expected | Result |
|---|---|---|---|
| 1 | menu > Timers | rows "G1 stops after", "G2 stops after" (Off), "Power off when all finished", "Start last programs at boot" (off) | |
| 2 | G1 stops after: G2 once | "5 min" (then 15, 30, 45, 60, 90, 120, 180, 240, Off) | |
| 3 | Play a program of 10 min or more on G1 | the status bar shows a timer icon and "5 min", counting down | |
| 4 | Pause G1 for 1 min, then play again | the minutes do not count while paused | |
| 5 | Wait until 5 min of running | G1 stops (STOPPED), toast "G1 stopped after 5 min", the screen wakes if dimmed | |
| 6 | Play again; after 2 min set "G1 stops after" to 15 min and back to 5 min | the count starts again: "5 min" left | |
| 7 | Power off when all finished: on; G2 empty; play a short program (or use the 5 min timer) on G1 | at its end: "All programs finished · Powering off · any key cancels" with a ring filling in 15 s | |
| 8 | Press any key on the card (POWER briefly) | the card goes, the unit stays on (POWER held still switches off) | |
| 9 | Step 7 again, press nothing | after 15 s the goodbye, the unit switches off | |
| 10 | Power on: menu > Timers | the settings are kept; Settings > Reset all settings sets them off again | |

## 14. Auto-start at power-on

| # | Step | Expected | Result |
|---|---|---|---|
| 1 | menu > Timers > Start last programs at boot: on | the switch on | |
| 2 | Play slot A on G1 with LOOP, slot B on G2; wait 10 s; POWER held (clean power-off) | the unit switches off | |
| 3 | Power on, press nothing | after the splash "Start the last programs?" with a ring filling in 10 s; then A plays on G1 (LOOP on) and B on G2, both from step 1 | |
| 4 | Power cycle again, STOP on the card | nothing starts | |
| 5 | Power cycle again, START on the card | both start at once | |
| 6 | Power on with USB connected | no card (PC monitor) | |
| 7 | Hold UP, press POWER, release UP when the splash shows | no card (a key was held) | |
| 8 | Play on G1, pull the plug (resume on) | the next power-on shows "Resume where it stopped?", not the auto-start | |
| 9 | Start last programs at boot: off, power cycle | no card | |

## 15. History

Only for an image built with `make APP=nx NX_LCD_OPTS=-DNX_FEAT_HIST=1` (off by default since
2026-10-05).

| # | Step | Expected | Result |
|---|---|---|---|
| 1 | menu > History before anything was played | "Nothing played yet" | |
| 2 | Play a program on G1 for 30 s, STOP; open History | the newest run at the top: "NN  name", "G1 · 0:30 · stopped · session N +h:mm"; the title shows the total time and runs | |
| 3 | Let a short program finish | "finished" in green | |
| 4 | A timer stop (§13) | "timer" | |
| 5 | Play on G1, connect USB (PC mode), unplug | "PC mode" | |
| 6 | A run of less than 5 s | not listed | |
| 7 | Play on G1 for over a minute (Offer to resume on), pull the plug, power on, answer the card | a "power cut" record for G1 with about the time it had played (up to a minute less: the last saved position), "+0:00" | |
| 8 | Play on G1 for 30 s, POWER held (clean power-off), power on | that run is listed as "stopped" | |
| 9 | A chain of two programs (§17) | each program is a run of its own | |
| 10 | Power cycle | the list and the totals are kept | |

## 16. Slot copy / move / erase, play on both

Use slots whose programs you can upload again from Spooky2.

| # | Step | Expected | Result |
|---|---|---|---|
| 1 | Program list, LOOP held on a slot with a program (e.g. 03), then LOOP | a sheet "Program 03": Play on G1 and G2, Copy to another slot, Move to another slot, Erase | |
| 2 | Play on G1 and G2 | both play slot 03 from step 1; the dashboard | |
| 3 | Stop both. Slot actions > Copy to another slot | "Copy 03 to", the first empty slot after 03 selected | |
| 4 | START | within about a second "Copied to NN"; the detail of 03 again; the list shows the name at NN | |
| 5 | Copy 03 onto a slot that holds a program | "That slot is not empty: erase it first" | |
| 6 | Load NN on G1, then try to erase NN | "A generator has that program loaded" | |
| 7 | Stop and unload NN (load another slot), Move NN to an empty slot MM | "Moved to MM"; NN shows "Slot NN", MM has the name | |
| 8 | Erase MM: START, hold START 1 s | "Program MM erased"; MM shows "Slot MM" | |
| 9 | Copy 03 to NN again, open the detail of NN and of 03 | the same steps, frequencies, dwell and waveform | |
| 10 | Connect USB, open Spooky2 and look at the programs stored in the device | NN holds the copy with its name; MM is empty; 03 unchanged | |
| 11 | Upload a program from Spooky2 into NN, unplug | the upload replaces the copy, as before | |

## 17. Chains

| # | Step | Expected | Result |
|---|---|---|---|
| 1 | menu > Chains | the list opens on the six Terrain rows (§23); DOWN: the built-in preset (§28), then Chain 1–4 and Plan 1–2, "empty · LOOP to edit"; the title "Run on G1 G2" | |
| 2 | LOOP on Chain 1; START; G1/G2 to a short program; START, change it; START, change it | "Chain 1 · ×1" with three programs | |
| 3 | LOOP once; START held on the last program; STOP | "×2", the last program removed; the list shows "2 programs · ×2" | |
| 4 | G1, START on Chain 1 | toast "Chain 1 on G1", G1's dashboard, "Chain 1/2" in the status bar | |
| 5 | Wait for the first program's end | the second one starts by itself ("Chain 2/2"); no power-off card in between (§13 step 7 on) | |
| 6 | Wait for the end of the second pass | G1 stops; "Program complete"; no "Chain" in the status bar | |
| 7 | Start the chain again, STOP during a program | the chain ends | |
| 8 | Start the chain, pull the plug during the second program, power on, START on the card | the second program resumes with "Chain 2/2" and the chain goes on | |
| 9 | Power cycle | the chains are kept | |

## 18. Multi-day plans

| # | Step | Expected | Result |
|---|---|---|---|
| 1 | Make Chain 1 and Chain 2 of short programs (×1). LOOP on Plan 1; START three times (three days of Chain 1); UP to Day 2, G2 (Chain 2); STOP | "Plan 1 · day 1/3": Day 1 Chain 1, Day 2 Chain 2, Day 3 Chain 1; the list shows "3 days · day 1" | |
| 2 | START on Plan 1 | toast "Plan 1: day 1", the dashboard with "Day 1/3" | |
| 3 | Wait until the day's chain has ended | the list shows "day 2"; nothing starts by itself | |
| 4 | START on Plan 1 | day 2's chain (Chain 2) plays, "Day 2/3" | |
| 5 | STOP during day 2, START on Plan 1 | day 2 again (a stopped day does not count) | |
| 6 | Let day 2 end, START on Plan 1, let day 3 end | the list shows "complete · LOOP to restart"; START opens the editor | |
| 7 | LOOP in the editor | "Plan 1 starts again at day 1" | |
| 8 | Pull the plug during a plan day, power on, resume | "Day N/3" is kept and the plan goes on | |

## 19. PC monitor: waveforms, amplitudes, gating

| # | Step | Expected | Result |
|---|---|---|---|
| 1 | Spooky2 session: a program on G1 (note its waveforms and amplitudes for Out1 and Out2) and one on G2 | the end of each name line shows Out1's waveform, " / " and Out2's when it is another one, and "gated" when the program uses gating, e.g. "Sine / Inverted"; under "OUT 1" and "OUT 2" the amplitude, e.g. "20.0 V" | |
| 2 | Compare with Spooky2's Control tab, "Generator Output" | the waveform names and the volts match what that pane shows for Out 1 and Out 2 (the volts are the ones Spooky2 sends: 10 V for a 20 V preset at a 100 % offset, less below the "Reduce Amplitude" frequency). If it shows "#N" instead of a name, note N and the waveform | |
| 3 | A program with fast frequency changes (a sweep) | the frequency lines change smoothly, no flicker; the names still take turns scrolling | |
| 4 | End the session, start a new one with other settings | only the new session's values show | |
| 5 | `gx_serial_test.py` compare (§3) with this image | still identical: the monitor only reads | |

## 20. Black splash

The splash is black in every theme, as the original's (decided on 2026-09-25).

| # | Step | Expected | Result |
|---|---|---|---|
| 1 | Settings > Display > Theme: Dark, power cycle | the splash on black: white ink, the sine red (a build with `NX_VENDOR=1`: the red "2"s red), the footer grey | |
| 2 | Theme: Light, power cycle | the splash on black as in step 1 (not white); the dashboard after it is light | |
| 3 | Theme: High contrast, power cycle | the splash on black as in step 1 | |
| 4 | Quick settings > Night mode on, power cycle | the splash on black as in step 1; then the night colours. Night mode off, Theme back to Dark | |
| 5 | Splash screen Short, then Off (§9 steps 2–3) | the same lengths as before: about 0.8 s, then no splash | |

## 21. Manual generator

Outputs connected to an oscilloscope if you have one. The manual generator plays the generator
tables 11–19 standalone, which the original never does (it only plays 41–70): this section checks
that the generator boards have them.

| # | Step | Expected | Result |
|---|---|---|---|
| 1 | menu > Manual | "Manual generator", Run on the generator in focus, Mode Fixed, Frequency 1000.00 Hz, Waveform Sine, Amplitude 5.0 V (the defaults) | |
| 2 | START | a play icon on the Run on row. The first sine after power-on starts about half a second late: nx uploads Spooky2's standard sine into table 11 first (§28 steps 12–14). Scope: a 1 kHz sine of 5 V on Out1, Out2 inverted. Without a scope: menu > Diagnostics, the G1 link row has a green dot, its lines grow and it shows 0 errors (every line acknowledged); then back to menu > Manual | |
| 3 | Waveform: G2 once (Square) | the output goes off for a moment and on again as a 1 kHz square (scope); the link still 0 errors | |
| 4 | Waveform: G2 twice more (Inverse sawtooth) | a falling sawtooth (scope). If a shape is wrong or the output stays off, note the waveform: that table is missing standalone | |
| 5 | Frequency: LOOP until the hundreds digit is underlined, then G2 | 1100.00 Hz; the scope follows at once, without a gap | |
| 6 | Amplitude: hold G1 down to 2.0 V | the value runs down; the scope amplitude follows (a short off / on at each change) | |
| 7 | STOP | the dashboard of G1: "Manual", 1100 Hz, running | |
| 8 | LOOP held on the dashboard | the Manual screen again | |
| 9 | START on the Manual screen | G1 stops | |
| 10 | Run on: G2, START | it plays on G2 (the play icon in G2's colour) | |
| 11 | Power cycle | nothing plays (the manual generator is not resumed); menu > Manual shows the values of step 6 | |

## 22. Sweeps

| # | Step | Expected | Result |
|---|---|---|---|
| 1 | menu > Manual: Mode Sweep; Start 100.00 Hz, End 1000.00 Hz, Time 1:00, Shape Linear, Repeat Once; START on G1 | plays | |
| 2 | STOP to the dashboard, watch 60 s | the frequency rises smoothly from 100 to 1000 Hz (about 8 new values a second), no jumps; the scope follows | |
| 3 | At 60 s | it ends like a program: toast "G1 finished · Manual", outputs off; with History built in, it lists a "Manual" run | |
| 4 | Shape Logarithmic, Once, START | the frequency rises slowly first and faster later (about 316 Hz after 30 s) | |
| 5 | Repeat Back and forth, START, watch 2 min | 100 → 1000 Hz in 60 s, then 1000 → 100 Hz in 60 s, without a jump at the turn; do it with Linear and Logarithmic | |
| 6 | Repeat Again | at the end back to 100 Hz, then up again | |
| 7 | While a sweep plays: menu > Diagnostics | the G1 link row: 0 errors | |
| 8 | Change End while it plays (e.g. 2000 Hz) | the sweep follows the new value at once | |

## 23. Terrain Protocol, Remote (test build)

Uses the test build whose program time runs faster: one second of dwell takes 16 ms (62.5 times
faster), so a 24 h set changes after about 23 min, a 12 h half day after about 11.5 min. A minute
of real time is about an hour on the Terrain screen.

| # | Step | Expected | Result |
|---|---|---|---|
| 1 | `make APP=nx NX_TIME_X=60 BUILD=build/nx-x60 flash` | the splash says "v0.9.0 TEST: fast time" | |
| 2 | menu > Chains | the list opens on the six Terrain rows (Terrain, Terrain-Mercury, Terrain-Nonviolent, each "· Remote" and "· Contact") with "Day 1/11" (Terrain-Nonviolent: "Day 1/10") | |
| 3 | LOOP on "Terrain · Remote" | the Terrain screen: the day-1 set's name, "Day 1 of 11 · 24 h" (a half day: "Day 1.1 of 11 · 12 h"), "Ran 0:00:00 of 24:00:00", "Power 5 V · LOOP changes", the Spooky Remote hint and the water hint | |
| 4 | LOOP five times | 2.5 V, 1 V, 0.5 V, 0.25 V, then 5 V again | |
| 5 | G1, START | toast; G1's dashboard with the set's name, "Day 1/11" in the status bar and the LOOP badge; scope: 5 V on Out1 | |
| 6 | Back to the Terrain screen (DOWN, Chains, LOOP on the row) | a play icon for G1; "Ran" counts about a minute per second | |
| 7 | Wait about 23 min (a half day: about 11.5 min) | the next set starts by itself: a new name and the next day label on the dashboard and the Terrain screen; with History built in, it lists the first set as "finished" | |
| 8 | On the Terrain screen, DOWN a few times while G1 plays, watching the dashboard's waveform and the scope | G1 goes on at each new set at once. Where a set's waveform differs (e.g. Square, then Inverse sawtooth), the dashboard names the new one and the scope shows it from the first step | |
| 9 | LOOP while G1 plays | G1 goes on at 2.5 V (scope); LOOP four more times back to 5 V | |
| 10 | Let "Ran" pass an hour on the screen (more than a minute of real time), then pull the plug | | |
| 11 | Power on, START on the resume card | G1 plays the same set with the same day label; "Ran" at most about a minute of real time back (about an hour on the screen) | |
| 12 | STOP on G1, then menu > Chains | the row keeps its day; START on it plays the same set again | |
| 13 | Timers > Start last programs at boot on; POWER held (clean power-off), power on, let the card start it | the Terrain protocol goes on at its set; then set Start last programs at boot off again | |

## 24. Terrain Protocol, Contact (test build)

A Contact session lasts about 2 h, about 1.9 min in the test build. Contact plays at 20 V: keep
the outputs on the scope only, not on pads.

| # | Step | Expected | Result |
|---|---|---|---|
| 1 | menu > Chains, LOOP on "Terrain · Contact" | "Day 1 of 11 · session 1 of 2", the pads hint and the water hint; no power row | |
| 2 | G1, START | G1 plays the session's set once; "Day 1/11" in the status bar; no LOOP badge; scope: 20 V on Out1 | |
| 3 | Let the screen dim, wait about 1.9 min | when the 0 Hz step starts: the card "Set finished · safe to pause" and "Next session in 6 to 12 h · any key"; the screen wakes and the backlight pulses 3 times; nothing is audible | |
| 4 | Any key | the card closes; the key does nothing else. Once the 0 Hz step has ended (a few seconds in the test build, a few minutes in the normal one), G1 stops and the Terrain screen shows "Day 1 of 11 · session 2 of 2" | |
| 5 | START session 2, wait for the card, press nothing | the card closes by itself after 60 s; the position is "Day 2 of 11 · session 1 of 2" | |
| 6 | START, STOP after 30 s | the position stays at that session; START plays it again from its start | |
| 7 | On the Terrain screen: UP until the position goes back past day 1, session 1 | it wraps to the last session: "Day 11 of 11 · session 2 of 2" | |
| 8 | START, wait for its end | the card; then the row shows "complete" and the Terrain screen "All sessions done · START: day 1 again" | |
| 9 | START | day 1, session 1 plays | |
| 10 | Pull the plug during a session, power on, START on the resume card | the session goes on; the card still comes at its 0 Hz step | |

## 25. Back to the normal build

| # | Step | Expected | Result |
|---|---|---|---|
| 1 | `make APP=nx flash` (the normal build) | the splash without "TEST: fast time" | |
| 2 | menu > Chains | the Terrain positions of the test build are still there (the same record); move them back with UP/DOWN on the Terrain screen if you like | |
| 3 | §8 steps 2–3 again | Spooky2 on both generators in PC mode as before | |
| 4 | Spooky2: upload a program into a slot, unplug, play it from the program list | unchanged: plays as before | |
| 5 | §3: `make conformance …` and the `gx_serial_test.py` compare | identical (no serial command is new) | |
| 6 | Upload two programs from Spooky2 with different waveforms and amplitudes (e.g. Square 10 V and Sine 5 V); a chain of the two on G1 (§17) | at the switch, the scope shows the second program's waveform and amplitude from its first step (before the fix of 2026-09-25 the chain kept the first program's) | |
| 7 | A chain on G2 with the same two programs | the same on G2 | |

## 26. Settings > About

Settings > About shows "Developer Manuel Haller Polo" under Firmware.

## 27. PC monitor: the output frequency

Spooky2 on the PC, the preset "Spooky Radionics (Boost) Manifestation - AW" (Presets > Radionics)
on one generator. With `Sine_WCM=11` Spooky2 uploads a wave table of 11 sine cycles and sends the
frequency / 11; the original's PC screen shows that (36 Hz for 396 Hz).

| # | Step | Expected | Result |
|---|---|---|---|
| 1 | Start the preset in Spooky2 | PC MODE; the generator's name line ends in "Sine / Inverted"; "10.0 V" under "OUT 1" and under "OUT 2", as in Spooky2's Control tab (the preset's 20 V at a 100 % offset) | |
| 2 | Watch the first 11 s | OUT 1 rises from 36 to 198 Hz, as Spooky2 shows, with "×11" after "Hz"; OUT 2 the same value with "= OUT 1"; nothing runs off the right edge | |
| 3 | The next 11 s | "396.00 Hz ×11", steady | |
| 4 | The next 11 s | 792 to 4356 Hz, rising; the digits are small while the number is long | |
| 5 | Stop in Spooky2; a program without a Wave Cycle Multiplier (e.g. a CAFL program on a standard shell) | its frequency as before, no "×" | |
| 6 | menu > Diagnostics after unplugging | 0 receive errors on that link (the tables are 4 KB lines) | |
| 7 | In Spooky2 choose the H-Bomb waveform for Out1 (H-Bomb sine or square; the shell's Wave Cycle Multiplier for it is 16) and run a program | the name line shows "H-Bomb sine" or "H-Bomb square"; the frequency is the one Spooky2 shows, with "×16" | |

## 28. Built-in preset with sweeps

The normal build: the preset's table is committed (`src/nx/data/nx_preset_data.c`, since
2026-10-13; `make nx-data` lists "gen_nx_preset: Radionics (Boost) Manifestation: 18 steps, 198 s,
11 cycles per table, repeats" when it writes the table again).
A scope on Out 1 and Out 2 of G1 if you have one; best after a Spooky2 run of the same preset
(§27) for comparison. The first question is whether the generator takes a wave table from the
display at all.

| # | Step | Expected | Result |
|---|---|---|---|
| 1 | menu > Chains, DOWN six times | the row "Radionics (Boost) Manifestation" after the six Terrain rows, before Chain 1 | |
| 2 | START (Run on G1) | the dashboard of G1: the name, RUNNING, LOOP. For about 1.5 s the time does not move (the two wave tables go out), then the frequency starts at 36 Hz and rises | |
| 3 | If instead the toast "G1 does not respond" shows after about 2.5 s and it stops | the generator did not answer the wave table: note it; nothing else of this section applies | |
| 4 | menu > Diagnostics | the G1 link row: 0 errors, the lines grow by about 8 a second in a sweep | |
| 5 | Watch one pass (3:18) | six times: a sweep up for 11 s, a steady frequency for 11 s, a sweep up for 11 s, with the frequencies of the preset's `Loaded_Frequencies` lines (the first: 36 to 198 Hz, 396 Hz, 792 to 4356 Hz); "Step n of 18", "Next" with the next start; then it starts again, the pass counter goes up | |
| 6 | Scope, in a steady step (e.g. step 2) | Out 1: a sine of 396 Hz (not 36 Hz), below zero (offset −100 %); Out 2: the same inverted, above zero; levels as in the Spooky2 run. If the frequency or the shape differs: note what you see (Spooky2 keeps `:w15=1,1,` during its sessions, standalone it is 0,0) | |
| 7 | START in a sweep, wait 5 s, START | PAUSED, outputs off, the time stands; then it goes on at the frequency where it stopped, not at the step's start | |
| 8 | STOP, then START | Stopped; START plays it from 36 Hz, once (after the last step: "finished"). LOOP instead of START plays it repeating | |
| 9 | menu > Chains, the preset's row, G2, START | it plays on G2 as well; both rows of Diagnostics without errors; G1's sweep goes on without a jump | |
| 10 | Pull the power plug while it plays, plug in again, power on | "Resume?": START goes on at about the same step; the tables are sent again first | |
| 11 | Plug in USB while the dashboard still stands at 0:00 of step 1 (the tables on their way) | PC MODE within a second, as always; Spooky2 connects | |
| 12 | After it: STOP, then menu > Manual, Sine, 1000 Hz, 5.0 V, START on G1, scope | about half a second later than usual (nx puts Spooky2's standard sine back into table 11) a plain 1 kHz sine at the full level. Not 11 kHz at half the level: that would be the preset's table still playing | |
| 13 | The same after a Spooky2 run of the Radionics preset on that generator (§27), USB unplugged | the same plain 1 kHz sine: nx does not trust the table after a PC session | |
| 14 | menu > Manual again, START (stop), START (play) | it starts at once now: the table is not sent a second time | |
