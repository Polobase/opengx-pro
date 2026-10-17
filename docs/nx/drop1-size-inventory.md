# APP=nx: what fills the flash, and what each part costs

State: 2026-10-06, after the decisions of 2026-10-05 (below): a build `make APP=nx` with the
Terrain sets and the Radionics preset built in (then a personal build, with `TERRAIN_DIR` /
`PRESET_FILE` set in an untracked `local.mk`).

Since 2026-10-13 that is the default build and the released image (`make release`): the two
tables are committed in `src/nx/data/`, and `NX_TERRAIN` and `NX_PRESETS` are on by default. The
image is 108,664 bytes in 0.9.1 (2026-10-14), 3,976 left of the 112,640 the bootloader is proven
to take; in 0.9.0 it was 108,456. Since 2026-10-14 the two text fonts are 2 bpp and the image is
linked with link-time optimisation (section 5; `NX_LTO=0` builds without it, which the per-module
figures below need, and which no longer fits with the scan in), and since 2026-10-17 the
biofeedback scan is built in: 112,080 bytes, 560 left (`NX_FEAT_BFB=0`: 103,722). Without the
two tables it is 104,726 bytes; with only `NX_TERRAIN=0` 106,350, with only `NX_PRESETS=0` 110,174
(without the scan: 96,614, 98,342 and 102,038; 0.9.1: 102,876, 104,084 and 107,248). The numbers below are those of 2026-10-06 and were not measured again.

**Image: 106,538 B of 112,640 B (`NX_FLASH_LIMIT` = `FLASH_PROVEN`): 6,102 B left.**
RAM: 24,008 B + 40 B crash record + 3,072 B stack of 36,864 B, 9,744 B left. RAM is not the
limit; this file is about flash only.

The `.bin` file is 106,544 B, 6 B more than `make APP=nx nx-size` prints: `tools/nx_size.py` does
not count 6 B of padding inside `.data`. The linker's limit check and `make flash` use the real
size, so the real room is 6,096 B. All numbers below are the `nx-size` kind (the linker map),
as everywhere in the docs.

## Decided on 2026-10-05

The inventory of that morning (image 112,586 B, 54 B left) led to these changes:

| Change | Bytes | State now |
|---|---:|---|
| Ghost splash artwork off by default | −2,264 | the drawn splash shows; `make APP=nx NX_VENDOR=1` builds the ghost in |
| History and totals off by default | −2,120 | no History cell, nothing is recorded; `NX_LCD_OPTS=-DNX_FEAT_HIST=1` builds it in. Its 11 texts, its hint line, its menu text and its icon (248 B) are still in the image: section 5 |
| 48 px frequency digits off by default (new switch `NX_FEAT_HERO48`) | −2,008 | the dashboard frequency in the 28 px digits; `NX_LCD_OPTS=-DNX_FEAT_HERO48=1` builds the old layout in |
| PC monitor extras reworked: Out2's waveform and amplitude, "×16" for H-Bomb tables | +344 | a fixed part now: the switch `NX_FEAT_PCMON` is gone. (An amplitude rule added the same day was wrong and was taken out on 2026-10-06) |
| **Together** | **−6,048** | image 106,538 B |

The three removals are each measured alone against today's image (what it costs to build that
one in again). Any two of them fit back in; all three together are 112,930 B, 290 B over the
limit.

The earlier content of this file (2026-09-24) described how the 96,010-byte image was cut to
72,600 B for the first proven bootloader limit.

## How to read the numbers

| Mark | How the number was made | How far to trust it |
|---|---|---|
| **measured** | the image built with the switch off, compared with the default image | exact: this is what you get |
| **trial** | no switch exists: the feature's entry point was taken out in a throwaway build (sources restored afterwards), image sizes compared | a lower bound: callers keep small pieces, and the feature's texts and icons stay in the tables |
| **exact** | a whole module, table, font, icon or text, from the linker map or the asset tables | exact, but removing it may leave or free a few bytes elsewhere |
| **est.** | the size of the functions in a build without inlining (`-fno-inline -fno-ipa-sra -fno-ipa-cp -fno-partial-inlining`; that build is 1.9 % bigger overall) | about ±15 %; the real number comes when the switch is built |

Savings do not add up exactly. Shared code goes only when its last user goes: all switches off
together save 760 B more than the sum of the single switches. Nine of the trials together
saved 4,836 B against 4,896 B as a sum.

## 1. Summary

| Part | Bytes | Share | What is in it |
|---|---:|---:|---|
| Features with a switch (section 2) | 22,700 | 21.3 % | chains, plans, Terrain, presets, manual generator, diagnostics, slot actions, timers |
| Assets (section 4) | 17,350 | 16.3 % | 3 fonts (the 48 px digits are not linked), 28 icons, 303 texts, 4 palettes |
| UI framework and the screens that have no switch | 27,740 | 26.0 % | dashboard, program list and detail, settings, menu and sheets, PC monitor with its extras, calibration, splash, goodbye, toasts, cards |
| Display engine | 13,446 | 12.6 % | panel driver, renderer, drawing, text, transitions, the frequency layout, scope |
| nx platform | 12,764 | 12.0 % | send queue, runner, keys, key-value store, resume, backlight, UARTs, crash record, main loop |
| Shared core (the same code as `APP=fw`) | 8,673 | 8.1 % | serial protocol, calibration, storage, factory port, SPI flash |
| Board and startup | 2,056 | 1.9 % | clock, pins, panel init, vector table, `main` |
| Compiler library | 1,726 | 1.6 % | division, 64-bit helpers, `memcpy` and friends |
| Padding | 83 | 0.1 % | between sections |
| **Image** | **106,538** | 100 % | |

With every switch off the image is 83,838 B (measured). The rows below the first one are that
image, split by module (section 6).

### The largest things you can decide on

| What | Saves | Mark | Section |
|---|---:|---|---|
| Everything in menu > Chains (chains, plans, Terrain Protocol, presets with sweeps) | 13,348 | measured | 2 |
| – of it: Terrain Protocol | 6,452 | measured | 2 |
| – of it: presets with sweeps | 2,384 | measured | 2 |
| – of it: multi-day plans | 1,096 | measured | 2 |
| Manual generator and sweeps | 3,268 | measured | 2 |
| Diagnostics and key test | 2,604 | measured | 2 |
| Resume after a power cut (auto-start goes with it) | 2,216 | trial | 3.1 |
| Screen transitions | 1,552 | trial | 3.1 |
| Slot copy / move / erase, play on both | 1,548 | measured | 2 |
| Program detail screen | 1,480 | trial | 3.1 |
| Timers and auto-start | 1,172 | measured | 2 |
| Live scope on the dashboard | 1,080 | trial | 3.1 |
| Splash screen (the drawn one) | 728 | trial | 3.1 |
| Signed division replaced by unsigned (nothing visible changes) | 512 | measured | 5 |
| History's texts and icon, still in the image though History is off | 248 | exact | 5 |

## 2. Features with a switch (measured)

Switch a feature off for one build with `make APP=nx NX_LCD_OPTS="-DNX_FEAT_X=0"` (several
switches in the one string), or change its default in `src/nx/nx_config.h`. The data switches
are Makefile variables: `make APP=nx NX_TERRAIN=0`. Every feature-off build compiles (checked at
each commit).

"Texts and icons that stay": switching a feature off removes its code and its menu cell, but
its texts and icons stay in the asset tables (`src/nx/nx_assets.c` is one committed table). The
column shows what the asset generator could drop in addition if it learned the switches (bytes
before tail sharing, so slightly high).

| Feature | Switch | Saves | Texts and icons that stay | What the user loses |
|---|---|---:|---:|---|
| **Everything in menu > Chains** | `-DNX_FEAT_CHAINS=0` | 13,348 | 945 + icon 42 | the Chains cell of the menu with all four things below. Plans, Terrain and presets follow this switch |
| – Terrain Protocol with its screen and the set-end alert | `-DNX_FEAT_TERRAIN=0` | 6,452 | 456 | the built-in Terrain Protocol (2 protocols, days, sessions, power level, resume of a set) |
| – only the Terrain sets (the code stays) | `NX_TERRAIN=0` | 4,372 | – | the same for the user: no Terrain rows. Saves less than the switch above |
| – presets with sweeps | `-DNX_FEAT_PRESET=0` | 2,384 | – | the Radionics preset standalone: its two wave tables, the sweeps, its row in the list |
| – only the preset data (the code stays) | `NX_PRESETS=0` | 1,208 | – | the same for the user: no preset row |
| – multi-day plans | `-DNX_FEAT_PLANS=0` | 1,096 | 225 | plans (a chain per day), the plan editor, "Day 3/11" |
| – chains themselves (what is left of the 13,348) | (only with all of the above) | 3,416 | 264 + icon 42 | programs one after the other, the chain editor, "Chain 2/5". Computed, not measured alone |
| **Manual generator and sweeps** | `-DNX_FEAT_MANUAL=0` | 3,268 | 229 | menu > Manual: a frequency or a sweep by hand (linear / logarithmic, once / again / back and forth) |
| **Diagnostics and key test** | `-DNX_FEAT_DIAG=0` | 2,604 | 640 + icon 59 | menu > Diagnostics: firmware, uptime, last reset, link statistics, PC mode counters, SPI flash, loop time, MCU temperature and supply; the key test |
| **Slot actions** | `-DNX_FEAT_SLOTS=0` | 1,548 | 487 + icons 124 | program detail > LOOP: play on G1 and G2, copy, move, erase a slot |
| **Timers and auto-start** | `-DNX_FEAT_TIMERS=0` | 1,172 | 260 + icon 72 | menu > Timers: G1/G2 stop after N min, power off when all finished, start the last programs at power-on |
| **All of them off** | all five | **22,700** | 2,561 + icons 297 | image 83,838 B |

Why the rows do not add up to the last one: the single switches sum to 21,940 B. The 760 B on
top are parts that several features share, for example the 64-bit signed division (manual
generator and presets), the per-step times of the runner (Terrain and presets), the frequency
line of the send queue (manual generator and presets).

### Off by default since 2026-10-05

| Feature | Switch on | Costs | What it brings back |
|---|---|---:|---|
| Ghost splash artwork | `make APP=nx NX_VENDOR=1` (needs `original/GxDisplayFW.bin`) | 2,264 | the original's ghost at power-on instead of the drawn splash |
| History and totals | `NX_LCD_OPTS=-DNX_FEAT_HIST=1` | 2,120 | menu > History: every run with its end, all-time hours and runs. The records already in the SPI flash are untouched meanwhile |
| 48 px frequency digits | `NX_LCD_OPTS=-DNX_FEAT_HERO48=1` | 2,008 | the dashboard frequency in 48 px digits when it fits (the font is 1,844 B of it) |

The PC monitor's extras (waveforms, amplitudes, gating, the output frequency "×11") had a
switch worth 1,136 B. They are a fixed part since 2026-10-05; with the rework they are about
1.5 KB.

Engine parts that are already off for the image (`src/nx/nx_config.mk`, they cost nothing):
gradient backgrounds, the region fade, and on the scope the gating bands, custom shapes, the Out2
ratio and the labels inside the card.

## 3. Features and parts without a switch

### 3.1 Measured by a trial build

Each row: the feature's entry point removed, nothing else changed, image built and compared.
A real switch would save this plus the texts and icons in the third column (when the asset
generator drops them) plus the small pieces the callers keep.

| Feature | Trial saving | Texts, icons on top | What the user loses, and notes |
|---|---:|---:|---|
| Resume after a power cut, and auto-start with it | 2,216 | 176 | after a power cut both generators are empty, as on the original; "Start last programs at boot" needs the same snapshot and goes too. With Timers already off this saves 1,680 B |
| Screen transitions | 1,552 | – | screens change at once: no push, no sheet reveal, no toast slide, no backlight dip into PC mode. "Reduce motion" then has little left to do |
| Program detail screen | 1,480 | 48 | LOOP held on a program: name, summary, all step frequencies, the playing step. The slot actions open from this screen and would need another door (measured with slot actions off; both together 3,028 B) |
| Live scope | 1,080 | – | the waveform picture on the dashboard (the text "20.0 V · Sine" stays). Less than before the ghost splash went: the drawn splash's sine uses the same line routine (352 B), which now stays |
| Splash screen | 728 | 67 | the unit shows the dashboard at once (the drawn splash: the wordmark "OpenGX Pro" with the ring and the sine). The original has a splash |
| Quick settings sheet | 648 | 113 | UP on the dashboard. Brightness, auto-dim and reduce motion are also in Settings; night mode, "Lock keys" and "Screen off" are only here |
| Key test | 636 | 204 | LOOP on Diagnostics. Part of the Diagnostics switch (2,604 B); this row is for keeping Diagnostics without it |
| Toasts | 416 | 423 | every 4 s message: stopped, loaded, repeat on/off, finished, timer stop, keys locked, slot results, and the two errors "does not respond" and "Memory error", which would need another place. Lower bound: the trial left the calls in place |
| Night idle view | 388 | 41 | Settings > Display > "When idle: Night view"; idle then always ends in screen off (today's default) |
| Key guide | 348 | 315 + icon 51 | the 7 key explanations after the first boot and from Settings > Keys & lock |
| Key lock | 184 | 183 + icons 266 | G1+G2 held locks, START+STOP held unlocks, automatic lock, the lock card and icon. Lower bound: the chord detection in the key driver stayed in |
| Hold rings | 168 | 47 | "Keep holding to switch off", "Hold: stop both". The ring card is also the card of resume, auto-start, power-off countdown and set-end, so little goes while one of those stays |
| Key-hint bar | 112 | 584 | the line of key help at the bottom of 13 pages. The code is small; the cost is the 13 texts (9 of them belong to switched features) |

### 3.2 Estimated from function sizes

No trial was made for these. The bytes are the functions named, as they are in the build
without inlining, plus texts and icons (exact).

| Part | Size (est.) | Made of | What the user loses |
|---|---:|---|---|
| PC monitor extras (a fixed part since 2026-10-05) | ≈ 1.5 KB | measured 1,136 B as a switch before the rework, + 344 B for the rework: the receive tap (`nx_rx_tap` 452, `tab_cycles` 158, `pcmon_set` 144), `info_text` 224, the "×N" formatting | the PC monitor shows only names and wire frequencies again (36 Hz for 396 Hz ×11), as the original |
| Settings screen as a whole | ≈ 2.6 KB | `nx_scr_set` 1,654 + texts 975 (Settings, About, themes, confirm). The Timers page is the same form, so this needs Timers off too | no settings page; the values stay at their defaults or only in quick settings |
| About page | ≈ 0.35 KB | texts 277 + its rows | firmware version, developer, panel type, the licence lines of the fonts and icons (they must then be in the docs) |
| "Reset all settings" and the hold-START confirm sheet | ≈ 0.65 KB | `conf_draw` 248, `conf_key` 84, `conf_tick` 80, `nx_ui_confirm` 32, `do_reset` 68 + texts 141 | the reset. The confirm sheet is shared with "Erase this program?" of the slot actions |
| Dim, screen off, wake on events | ≈ 0.7 KB | `power_task` 264, `wake` 100, `idle` 68, `nx_ui_screen_off` 68, `nx_lcd_power_step` 144, backlight fades | the screen stays on at one brightness, as the original's |
| Dashboard cards "Program complete" and "not responding" | ≈ 0.65 KB | `draw_card` 156 + texts 124 + the two 32 px icons 356 | the state shows only in the status bar (FINISHED, NO RESPONSE) |
| Odometer roll of changing digits | ≈ 0.6 KB | `roll_set` 132, `roll_len` 40, most of `hero_draw` 588 (the digit cells) | digits change at once |
| Goodbye ring and session line | ≈ 0.4 KB | `bye_draw` 212, `bye_tick` 44, part of `nx_ui_goodbye_step` 144 + text 19 + 32 px power icon 167 | a plain "Goodbye" text. The original has a goodbye screen |
| Menu as a grid with icons | ≈ 0.4 KB | `nx_grid_cell` 232 + the Programs and Settings icons 152 (the other cells' icons are counted in section 2) | the menu as a plain list. The menu itself has to stay: it is the door to every feature |
| Name marquee | ≈ 0.4 KB | `nx_text_marquee` 156, `nx_marquee_offset` 58, `nx_marquee_at` 44, `nx_marquee_tick` 34, part of `nx_text_box` 104 + text 20 | long names end in "…". The original scrolls them |
| Empty-dashboard picture and hints | ≈ 0.33 KB | `draw_none` 240 + texts 88 | an empty dashboard says nothing about what to press |
| Themes Light and High contrast | ≈ 0.17 KB | 2 palettes 96 + texts 31 + the row | Dark only |
| Night mode | ≈ 0.15 KB | palette 48 + text 13 + the row and `theme_now` 32 | no night palette (the night idle view uses the same palette) |
| "Original key behaviour" option | ≈ 0.15 KB | the three places in the key handling + text 25 | START / LOOP / POWER always behave the nx way |
| Calibration screen extras | ≈ 0.25 KB | texts 137 + their drawing | the title, the TEST FREQ. / TEST LEVEL readout and the two help lines; the 29 values stay |

The dashboard's info lines (step n of N, time per step and time left, amplitude and waveform,
progress bar, next frequency, pass counter) are `draw_program`, 816 B (est.) with about 200 B of
texts. They were not split further: each line is 60 to 150 B.

### 3.3 What the original also has (listed so that nothing is missing)

Not candidates unless nx may do less than the original firmware.

| Part | Bytes | Where |
|---|---:|---|
| Serial protocol with the PC | 3,530 | `link`: `:w`, `:r`, `:n`, program and name transfer |
| Calibration: values, menu, test outputs, the screen | 3,474 | `calib` 2,228 (1,672 of it the 29 menu and 29 jig lines), `core_calmenu` 260, `nx_scr_cal` 986 |
| Programs, names and calibration in the SPI flash | 1,328 | `storage` 774, `spiflash` 554 |
| Factory port (only the factory jig uses it) | 560 | `factory` |
| PC mode and the PC screen | 2,440 | `nx_scr_pc` 2,136 (with the extras, section 3.2), `core_pcmode` 304 |
| Program list (7 rows, 30 slots) | 1,100 | est.: `lib_key` 368, `lib_row` 156, `lib_draw` 156, `lib_enter` 136, `lib_tick` 96 and the shared list |
| The other generator's strip on the dashboard | 640 | est.: `draw_strip` |
| Backlight PWM, PB7 PWM | 833 | `nx_bl` |

## 4. Assets (exact)

`nx_assets` in the image: 17,350 B = fonts 9,872 + icons 2,105 + texts 5,181 + palettes 192.

### Fonts

| Font | Used for | Glyphs | Pixels | Tables | Total |
|---|---|---:|---:|---:|---:|
| `body16`: Inter Regular 16 px | names, list rows, values, titles, toasts | 100 | 3,744 (2,053 at 2 bpp) | 825 | 4,569 (2,878) |
| `cap13`: Inter Medium 13 px | captions, labels, hints, the status bar | 101 | 3,119 (1,628 at 2 bpp) | 834 | 3,953 (2,462) |
| `num28`: Inter Display Medium 28 px | the dashboard frequency, the PC monitor, the night view | 17 | 1,187 | 163 | 1,350 |
| `num48`: Inter Display SemiBold 48 px | the dashboard frequency with `NX_FEAT_HERO48=1` only: **not linked** by default | 14 | 1,708 | 136 | (1,844) |
| **Fonts in the image** | | | | | **9,872** (6,690 since 2026-10-14) |

All were 4-bit anti-aliased and run-length coded; since 2026-10-14 the two text fonts are 2-bit
(4 alpha levels, stored raw: at 2 bpp the run-length code never wins) and look the same on the
panel. The two text fonts carry every printable
ASCII character (95) plus 5 or 6 signs (· … – × ‹ › ▸ °), because a program name from Spooky2 can
contain any of them; a glyph is 39 to 46 B on average. The digit fonts carry only what a number
needs; a 28 px digit is about 80 B.

What could be done with fonts: one text font instead of two saves 3,953 B (`cap13`) but every
caption becomes 16 px, so every screen needs a new layout.

### Icons

20 px set: 1,413 B (24 icons, pixels 1,201, tables 212). 32 px set: 692 B (4 icons, pixels
640, tables 52). Every icon is used. Bytes include the 8 B table entry.

| Icon | Bytes | Where it shows | Goes with |
|---|---:|---|---|
| PLAY | 31 | state icon everywhere, list rows, cards | core |
| PAUSE | 44 | state icon | core |
| STOP | 31 | state icon, toasts | core |
| CHECK_CIRCLE | 76 | READY and FINISHED state; "loaded", "finished" and slot-result toasts; set-end card | core |
| ADD | 30 | the "no program" state icon; the "Add" rows of the chain and plan editors | core |
| REPEAT | 57 | LOOP badge, repeat toasts | core |
| USB | 52 | PC monitor, "PC disconnected" toast | PC mode |
| WARNING | 72 | the "no response" state; error and refusal toasts; confirm sheet | errors |
| ERROR | 76 | error toasts | errors |
| HOURGLASS | 56 | STARTING in the status bar and its toast; timer-stop toast | boot, Timers |
| LOCK | 59 | status bar, toast | key lock |
| LOCK_OPEN | 58 | "Keys unlocked" toast | key lock |
| BRIGHTNESS | 72 | brightness toast in PC mode | PC mode |
| KEYBOARD | 51 | key guide title | key guide |
| LIBRARY | 76 | menu cell Programs | menu |
| SETTINGS | 76 | menu cell Settings | menu |
| TUNE | 64 | menu cell Manual, factory-value toast | Manual, calibration |
| TIMER | 72 | menu cell Timers, status bar minutes | `NX_FEAT_TIMERS` |
| HISTORY | 61 | menu cell History | `NX_FEAT_HIST`: off, so unused in the image |
| DIAG | 59 | menu cell Diagnostics | `NX_FEAT_DIAG` |
| CHAIN | 42 | menu cell Chains, status bar, rows | `NX_FEAT_CHAINS` |
| COPY | 67 | slot actions | `NX_FEAT_SLOTS` |
| MOVE | 57 | slot actions | `NX_FEAT_SLOTS` |
| DELETE | 54 | slot actions, "Reset all settings" | Slots, Settings |
| CHECK_CIRCLE 32 px | 195 | "Program complete" card | dashboard card |
| POWER 32 px | 167 | goodbye, power-off ring, power-off countdown | power off |
| WARNING 32 px | 161 | "not responding" card | dashboard card |
| LOCK 32 px | 149 | key-lock card | key lock |

### Texts

303 texts: 5,181 B (the pool 4,573 + a 2-byte offset each 606). A text costs its length + 1
+ 2. The groups below sum to 5,683 B; the generator stores a text that is the end of another
one only once, which saves the 502 B difference. Every text is used by the code. Source:
`assets/nx/strings/en.json`.

| Group | Texts | Bytes | What it is |
|---|---:|---:|---|
| `set` | 38 | 603 | Settings rows, values, the Timers rows (139 B of it) |
| `hint` | 13 | 584 | the 13 key-hint lines |
| `slot` | 18 | 444 | `NX_FEAT_SLOTS`: actions, questions, results, refusals |
| `terrain` | 14 | 414 | `NX_FEAT_TERRAIN` |
| `dash` | 20 | 397 | dashboard: step, times, cards, empty-dashboard hints |
| `diag` | 26 | 386 | `NX_FEAT_DIAG` |
| `guide` | 10 | 298 | key guide |
| `about` | 12 | 277 | Settings > About, licences |
| `plan` | 9 | 174 | `NX_FEAT_PLANS` |
| `manual` | 17 | 170 | `NX_FEAT_MANUAL` |
| `cal` | 6 | 164 | calibration screen, factory-value toast |
| `chain` | 10 | 154 | `NX_FEAT_CHAINS` |
| `hist` | 11 | 147 | `NX_FEAT_HIST`: off by default, so unused in the image |
| `toast` | 7 | 143 | confirmation toasts |
| `wave` | 9 | 117 | the 9 waveform names (dashboard, detail, PC monitor) |
| `pc` | 6 | 107 | PC monitor |
| `state` | 10 | 103 | state words of the status bar |
| `kt` | 6 | 101 | key test (`NX_FEAT_DIAG`) |
| `menu` | 8 | 80 | the 8 menu texts |
| `quick` | 6 | 79 | quick settings |
| `confirm` | 2 | 64 | confirm sheet |
| `auto` | 2 | 62 | auto-start card (`NX_FEAT_TIMERS`) |
| `resume` | 2 | 59 | resume card |
| `lock` | 3 | 58 | key lock |
| `done` | 2 | 58 | power-off countdown card (`NX_FEAT_TIMERS`) |
| `key` | 8 | 52 | key names of the key test (`NX_FEAT_DIAG`) |
| `err` | 2 | 51 | the two error toasts |
| `strip` | 2 | 47 | the other generator's strip |
| `splash` | 2 | 36 | splash |
| `lib` | 3 | 33 | program list |
| `theme` | 3 | 31 | theme names |
| `bye` | 2 | 29 | goodbye |
| `power` | 1 | 29 | power-off ring |
| `ui` | 1 | 29 | "Generators are starting…" |
| `gen` | 4 | 26 | G1, G2, OUT 1, OUT 2 |
| `hold` | 1 | 18 | "Hold: stop both" |
| `ambient` | 1 | 16 | night view |
| `common` | 2 | 14 | Off, Never |
| `out` | 1 | 11 | "Inverted" |
| `unit` | 2 | 9 | Hz, V |
| `detail` | 1 | 9 | "Run on" |
| **All** | **303** | **5,683** | 5,181 in the image |

The longest single texts (40 B and more):

| Bytes | Text | Group |
|---:|---|---|
| 53 | G1/G2 slot · START add · hold: remove · LOOP × | `hint` |
| 51 | G1/G2 chain · START add, hold del · LOOP day 1 | `hint` |
| 50 | LOOP level · STOP frequency · hold POWER: off | `cal` |
| 50 | G1/G2 value · LOOP digit · START play · STOP | `hint` |
| 48 | START play · LOOP more · G1/G2 · STOP back | `hint` |
| 48 | START play · LOOP edit · G1/G2 · STOP back | `hint` |
| 47 | UP/DOWN select · G1/G2 change · START open | `hint` |
| 47 | UP/DOWN select · G1/G2 change · STOP close | `hint` |
| 47 | UP/DOWN select · G1/G2 adjust · START save | `cal` |
| 47 | UP/DOWN scroll · LOOP key test · STOP back | `hint` |
| 46 | Spooky Remote on the Boost BN port or Out 1 | `terrain` |
| 45 | Drink plenty of water during the protocol. | `terrain` |
| 43 | Copyright 2016 The Inter Project Authors | `about` |
| 43 | Not made or endorsed by the device maker | `about` |
| 43 | UP/DOWN select · START do · STOP close | `hint` |
| 43 | Pads on Boost HIGH PWR CONTACT, or Out 1 | `terrain` |
| 42 | G1 · G2: switch · again: program list | `guide` |
| 42 | Hold START to confirm · STOP to cancel | `confirm` |
| 42 | Display only · this screen never sends | `pc` |
| 42 | All sessions done · START: day 1 again | `terrain` |
| 42 | START play · UP/DOWN set · LOOP power | `hint` |
| 41 | That slot is not empty: erase it first | `slot` |
| 40 | LOOP: repeat · hold: program details | `guide` |
| 40 | Press every key · hold STOP to leave | `hint` |

### Palettes

192 B: Dark, Light, High contrast and Night, 24 colours of 2 B each. One palette is 48 B.

### Data from the original image and from Spooky2's presets

The ghost splash is made at build time and is not in git. So were the Terrain sets and the preset
until 2026-10-13; since then their tables are committed in `src/nx/data/`
(`docs/nx/THIRD_PARTY.md`).

| Data | Bytes | Made of | Switch |
|---|---:|---|---|
| Ghost splash bitmaps (`build/nx/nx_vendor.c`) | 0 | ghost logo 1,120, "Welcome" 648, tagline 368, layout table 80, the "2" large 72 and small 48 | `NX_VENDOR` |
| Terrain sets (`src/nx/data/nx_terrain_data.c`; then `build/nx/nx_terrain_data.c`) | 2,632 | frequencies 930, dwell index 617, names 388, sets 372, items 193, dwell table 52, protocols 48, header 32 | `NX_TERRAIN` |
| Preset (`src/nx/data/nx_preset_data.c`; then `build/nx/nx_preset_data.c`) | 174 | 18 steps of 8 B, header 30 | `NX_PRESETS` |

The preset's two wave tables (2 × 1,024 samples) are not stored: `nx_preset_sample` computes them
(76 B + 46 B, using the sine table the drawing code already has).

## 5. Savings where nothing visible changes

| What | Saves | Mark | State |
|---|---:|---|---|
| The two text fonts at 2 bits per pixel instead of 4 (`"bpp": 2` in `assets/nx/fonts.json`): 4 alpha levels, no visible change in the 122 gallery shots | 3,182 | exact | **applied 2026-10-14** |
| Unsigned instead of signed division in `nx_manual_chz`, `chz_at`, `set_key`, `nx_lcd_scroll`: the library's signed 64-bit division (536 B) is no longer linked | 512 | measured | tried against today's image, host tests pass, **not applied**; a patch of 4 files |
| The same for the last 5 signed 32-bit divisions (`nx_bl_tick_1ms`, `nx_list_draw` ×3, `nx_diag_sensors`): `_divsi3` (468 B) goes | ≈ 450 | est. | not tried |
| History's texts and icon: History is off, but its 11 texts (147 B), its hint line (30 B), its menu text (10 B) and its icon (61 B) are still in the tables | 248 | exact | needs the asset generator to follow the switches (they must come back with `NX_FEAT_HIST=1`) |
| Texts and icons of the other features, when one of them is switched off (section 2) | up to ≈ 2,800 | exact | the same generator work; saves nothing while those features are on |
| Link-time optimisation (`-flto`) | 2,376 | measured | **applied 2026-10-14** for the nx image (`NX_LTO=0` switches it off). The assembler error was the reset handler's inline assembly: its `ldr =` literal had no pool in reach once LTO put everything into one section (`.ltorg` after the final branch fixes it); then LTO dropped `reset_c` and `nx_fault`, referenced from assembly only (`__attribute__((used))`). Checked: the image boots on the emulated board, and the whole gallery rendered from an LTO build is pixel-identical to the baselines |

The library today: `libgcc` 2,076 B (`_divsi3` 468, `_divdi3` 464, `_udivmoddi4` 408, `_udivsi3`
276, `_muldi3` 80, `_aeabi_ldivmod` 72, `_aeabi_uldivmod` 64, `_clzsi2` 60 and smaller ones) and
`libc_nano` 284 B (`strncpy`, `strrchr`, `memmove`, `memcmp`, `strchr`, `strcat`, `strcmp`,
`memcpy`, `memset`, `strcpy`, `strlen`).

## 6. Every module (exact)

"All switches off" is the same module in the 83,838-byte image; the difference is what the
switches of section 2 take out of it. The modules of History (`nx_hist`, `nx_scr_hist`) and the
ghost bitmaps (`nx_vendor`) are not in the image any more.

| Module | Bytes | All switches off | What it is |
|---|---:|---:|---|
| `nx_assets` | 17,350 | 17,350 | fonts, icons, texts, palettes (section 4) |
| `nx_ui` | 6,544 | 6,380 | UI framework: screen stack, key routing, toasts, cards, hold rings, key lock, dim / off, goodbye |
| `nx_scr_dash` | 5,771 | 5,439 | dashboard |
| `nx_gfx` | 5,012 | 5,012 | drawing: fills, rounded shapes, rings, text, icons, scope line |
| `link` | 3,530 | 3,445 | serial protocol with the PC (shared with `APP=fw`) |
| `nx_scr_chain` | 3,381 | 0 | menu > Chains: the list, chain editor, plan editor, preset and Terrain rows |
| `nx_comp` | 3,292 | 3,292 | UI parts: list, form rows, title bar, G badge, menu cell, sheet frame, hint bar, hold ring |
| `nx_render` | 2,643 | 2,643 | band renderer: dirty rectangles, layers, scrim, themes |
| `nx_scr_lib` | 2,640 | 1,828 | program list, program detail, slot actions |
| `nx_terrain_data` | 2,632 | 0 | Terrain sets (a table from Spooky2's presets) |
| `calib` | 2,228 | 2,228 | calibration: the 29 values, menu and jig lines, test outputs (shared) |
| `nx_uicore` | 2,208 | 1,444 | the UI's view of the core: the snapshot per frame, doors to the features |
| `nx_terrain` | 2,200 | 26 | Terrain Protocol: days, sessions, power, resume of a set |
| `nx_scr_pc` | 2,136 | 2,136 | PC monitor |
| `libgcc.a` | 2,076 | 1,468 | division and 64-bit helpers (section 5) |
| `nx_resume` | 1,884 | 1,332 | resume after a power cut; the snapshot of auto-start |
| `nx_scr_diag` | 1,830 | 0 | Diagnostics and key test |
| `nx_scr_set` | 1,654 | 1,654 | Settings pages and the Timers page |
| `nx_scr_sheets` | 1,652 | 1,596 | menu, quick settings, confirm sheet |
| `nx_scr_manual` | 1,648 | 0 | manual generator screen |
| `nx_scr_sys` | 1,635 | 1,635 | splash, key guide, goodbye, night view |
| `nx_preset` | 1,574 | 2 | presets with sweeps: wave tables, setup lines, sweep; the standard sine for built-in sources |
| `nx_kv` | 1,552 | 1,552 | key-value store in the SPI flash (settings, resume, chains, plans, history totals) |
| `nx_runner` | 1,472 | 928 | program runner: steps, dwell, loop, times |
| `nx_manual` | 1,456 | 4 | manual generator and sweeps |
| `nx_txq` | 1,342 | 1,064 | send queue to the generators (waits for `:ok`), long lines |
| `nx_lcd` | 1,316 | 1,316 | panel driver: windows, band buffers, sleep, hardware scroll |
| `nx_keys` | 1,266 | 1,266 | keys: debounce, hold, repeat, chords |
| `nx_core` | 1,264 | 1,264 | actions (load, play, pause, stop), boot, power off, PC-mode switch |
| `nx_hero` | 1,160 | 1,160 | the dashboard frequency: layout, digit groups, dimmed zeros |
| `nx_uifmt` | 1,128 | 1,080 | formatting: frequency, times, volts, waveform names |
| `nx_trans` | 1,108 | 1,108 | transitions: push, slide, reveal, dip |
| `board` | 992 | 992 | clock, pins, power-off signal (shared) |
| `nx_scr_cal` | 986 | 986 | calibration screen |
| `nx_scope` | 983 | 983 | scope: waveform samples and the trace |
| `nx_scr_terrain` | 974 | 0 | Terrain screen |
| `nx_diag` | 928 | 856 | PC monitor's receive tap (waveforms, amplitudes, offsets, wave table cycles); MCU temperature and supply; loop time |
| `nx_chain` | 892 | 22 | chains and plans |
| `nx_bl` | 833 | 833 | backlight PWM and fades; the PB7 PWM |
| `storage` | 774 | 774 | programs, names, calibration in the SPI flash (shared) |
| `nx_uart` | 774 | 774 | the three UARTs |
| `nx_slots` | 660 | 2 | slot copy / move / erase |
| `factory` | 560 | 560 | factory port (shared) |
| `spiflash` | 554 | 510 | SPI-flash driver (shared) |
| `nx_font` | 528 | 528 | text width, fitting, "…", texts with `{0}` |
| `nx_timer` | 520 | 8 | stop-after timers, power off when all finished |
| `nx_fault` | 514 | 514 | crash record, reset cause, safe mode |
| `lcd` | 512 | 512 | panel init lists A / B, SPI (shared) |
| `nx_store` | 510 | 498 | the nx area of the SPI flash: mount, format |
| `nx_lcd_port` | 488 | 488 | SPI and DMA to the panel |
| `nx_loop` | 450 | 450 | main loop |
| `nx_flash` | 404 | 404 | SPI-flash access with the quiet rule |
| `nx_hw` | 384 | 116 | key pins, ADC, PB1, chip id |
| `runner_seq` | 360 | 360 | the serial lines of a step, of idle and of off (shared) |
| `startup` | 340 | 340 | vector table, reset |
| `nx_time` | 328 | 328 | 1 ms tick, PB1 sampling |
| `core_pcmode` | 304 | 304 | PC-mode detection (shared) |
| `libc_nano.a` | 284 | 258 | `memcpy`, `strlen` and friends |
| `nx_uiset` | 270 | 270 | UI settings: defaults, limits, the stored record |
| `core_calmenu` | 260 | 260 | calibration menu keys (shared) |
| `nx_crc` | 245 | 245 | CRC-32 |
| `main_nx` | 212 | 212 | `main`, the default programs |
| `nx_anim` | 208 | 208 | easing curves |
| `nx_src` | 178 | 46 | program sources: slot, preset, Terrain set, manual |
| `nx_preset_data` | 174 | 0 | preset steps (a table from a Spooky2 preset) |
| `fmt` | 166 | 166 | number formatting (shared) |
| `nx_settings` | 140 | 140 | the settings record |
| `(alignment)` | 109 | 83 | padding between sections |
| `nx_wdg` | 80 | 80 | watchdog |
| `state` | 66 | 66 | calibration table, flags (shared) |
| `nx_hist` | 10 | 10 | history ring and totals |
| **Image** | **106,538** | **83,838** | |

## 7. Inside the large UI modules (est.)

Function sizes from the build without inlining, so the pieces the normal build folds into their
callers are visible. "Others" are the functions not listed. The module total is the real image.

### `nx_ui`: 6,544 B

| Function | Bytes | What it does |
|---|---:|---|
| `nx_ui_init` | 484 | start: layers, settings, the first screen (dashboard, splash, guide, PC or calibration) |
| `draw_overlay` | 440 | the key-lock card; the ring card of POWER hold, "stop both", resume, auto-start, power-off countdown, set-end |
| `events` | 428 | events from the core become toasts and wake the screen: finished, timer stop, no response, memory error, factory value, slot results |
| `card_task` | 404 | the four cards with a countdown ring: resume, power off when finished, auto-start, Terrain set-end |
| `global_key` | 356 | keys the framework takes before a screen: POWER (screen off / on), the lock and unlock chords, the lock card |
| `key_event` | 300 | a key to the top screen or sheet |
| `toast_task` | 280 | toast queue, slide in and out, 4 s |
| `hold_task` | 272 | rings of held keys, the lock card's time, automatic lock |
| `power_task` | 264 | dim, screen off, night view, wake by `w64` |
| `nx_ui_tick` | 224 | one UI pass |
| `modes` | 220 | PC mode, calibration and link errors change the screen |
| `nx_ui_goodbye` | 212 | power off: the goodbye screen |
| `nx_ui_goodbye_step` | 144 | power off: ring, backlight fade, panel sleep |
| `frame_start` | 164 | freeze the state for the frame being drawn |
| `draw_toast` | 160 | the toast |
| `nx_ui_act` | 128 | an action to the core |
| `nx_ui_apply` | 116 | apply the settings (brightness, theme, key repeat, motion) |
| `nx_ui_busy` | 116 | is there UI work left (may the MCU sleep) |
| `transition` | 108 | start the transition of a screen change |
| `nx_ui_toast_s` | 104 | queue a toast |
| `update_scrim` | 100 | the dark layer under a sheet, card or ring |
| `wake` | 100 | screen back on |
| `card_answer` | 100 | START / STOP on a card |
| `build_vm` | 100 | fetch the snapshot of the core |
| others (34) | 1,244 | |

### `nx_scr_dash`: 5,771 B

| Function | Bytes | What it does |
|---|---:|---|
| `dash_tick` | 912 | what changed since the last frame, so what is redrawn; digit roll, marquee, scope motion |
| `draw_program` | 816 | step n of N, time per step and left, amplitude and waveform, scope, progress bar, next frequency, pass counter |
| `draw_status` | 704 | status bar: G badge, state icon and word, LOOP ×N, timer minutes, "Chain 2/5" / "Day 3/11", lock |
| `draw_strip` | 640 | the other generator's strip: name, state, frequency, loop badge |
| `hero_draw` | 560 | the big frequency, digit cells with the odometer roll |
| `dash_key` | 520 | START, STOP, LOOP, G1, G2, UP, DOWN on the dashboard |
| `dash_draw` | 296 | the layout: which parts for which state |
| `draw_none` | 240 | "No program loaded" with the dashed ring and two hints |
| `draw_card` | 156 | "Program complete" / "not responding" card |
| `roll_set` | 132 | start a digit roll |
| `draw_name` | 120 | the program name (marquee) |
| others (22) | 695 | |

### `nx_comp`: 3,292 B

| Function | Bytes | What it does |
|---|---:|---|
| `nx_list_draw` | 660 | the list: rows, selection bar with glide, scrolling (program list, settings, history, chains, …) |
| `nx_form_row` | 492 | a settings row: label, value with ‹ ›, switch, link |
| `nx_list_key` | 254 | UP / DOWN in a list |
| `nx_grid_cell` | 232 | a menu cell: icon over its label |
| `nx_form_bar` | 208 | a row with a bar (brightness) |
| `nx_comp_title` | 164 | page title with the back arrow |
| `nx_comp_badge` | 164 | the G1 / G2 badge |
| `nx_comp_title_target` | 124 | title with "Run on G1 G2" |
| `nx_sheet_frame` | 124 | the frame of a bottom sheet |
| `nx_comp_hints` | 120 | the key-hint bar |
| `nx_text_box` | 104 | a text in a box: marquee or "…" |
| `nx_hold_ring` | 102 | the ring that fills while a key is held |
| `nx_list_tick` | 100 | list animation |
| others (14) | 440 | |

### `nx_gfx`: 5,012 B

| Function | Bytes | What it does |
|---|---:|---|
| `nx_ring` | 690 | anti-aliased ring arc: splash, goodbye, hold and countdown rings, the dashed ring |
| `blit_mask` | 576 | a glyph or an icon (4-bit, run-length coded) into the band |
| `nx_rrect_line` | 522 | rounded outline: frames, focus |
| `nx_trace` | 352 | the scope line |
| `nx_rrect` | 344 | filled rounded rectangle: panels, cards, selection |
| `pill_q4` | 262 | pill with sub-pixel ends: bars, switches |
| `nx_disc` | 208 | anti-aliased dot |
| `nx_text_n` | 164 | text |
| `nx_text_marquee` | 156 | scrolling text |
| `nx_rect_or` | 144 | bounding box of two rectangles |
| `k_qsin` | 130 | quarter sine table (rings, scope, the preset's wave tables) |
| `nx_bar` | 124 | progress bar |
| `nx_clip_push` | 118 | clipping |
| `nx_rect_and` | 104 | intersection of two rectangles |
| `nx_text_fit` | 100 | text cut with "…" |
| others (24) | 1,004 | |

### `nx_render`: 2,643 B

| Function | Bytes | What it does |
|---|---:|---|
| `add` | 496 | the list of rectangles to redraw: add and merge |
| `nx_render_step` | 412 | render waiting bands within a time budget |
| `frame_start` | 232 | start of a frame |
| `nx_render_band` | 220 | compose one band from the layers and queue it |
| `nx_render_init` | 168 | set-up |
| `nx_rect_mergeable` | 162 | may two rectangles become one |
| `plain` | 152 | a band with only background goes out as a fill |
| `draw_layers` | 128 | the layers of a band in order |
| others (20) | 747 | |

### `nx_scr_lib`: 2,640 B

| Function | Bytes | What it does |
|---|---:|---|
| `lib_key` | 368 | program list: keys |
| `lib_row` | 156 | program list: a row "NN. name" |
| `lib_draw` | 156 | program list: page |
| `lib_enter` | 136 | program list: keeps the selection and the window |
| `detail_row` | 272 | detail: a row of step frequencies, the playing step marked |
| `detail_key` | 240 | detail: keys |
| `detail_draw` | 180 | detail: title, summary, the list |
| `detail_enter` | 152 | detail: reads the slot |
| `act_key` | 228 | slot actions: keys (`NX_FEAT_SLOTS`) |
| `act_row` | 108 | slot actions: a row (`NX_FEAT_SLOTS`) |
| others (18) | 644 | |

### `nx_scr_set`: 1,654 B

| Function | Bytes | What it does |
|---|---:|---|
| `set_row` | 332 | draws a row of any page |
| `change` | 324 | G1 / G2 / START on a row |
| `value_of` | 296 | the value text of a row |
| `set_key` | 244 | keys, page changes |
| others (21) | 494 | |

### `nx_scr_sheets`: 1,652 B

| Function | Bytes | What it does |
|---|---:|---|
| `quick_key` | 260 | quick settings: keys |
| `conf_draw` | 248 | confirm sheet: question, body, hold ring |
| `menu_key` | 256 | menu: moving in the grid, START opens |
| `quick_row` | 216 | quick settings: a row |
| `menu_draw` | 112 | menu: the grid |
| others (15) | 562 | |

### `nx_scr_sys`: 1,635 B

| Function | Bytes | What it does |
|---|---:|---|
| `nx_ambient_draw` | 356 | night view: both generators, state, frequency |
| `bye_draw` | 212 | goodbye: icon, ring, "Goodbye", session time |
| `splash_draw` | 352 | splash: ghost artwork or the drawn one, version, "any key: skip" |
| `guide_draw` | 144 | key guide: 7 lines |
| others (22) | 575 | |

### `nx_scr_pc`: 2,136 B

| Function | Bytes | What it does |
|---|---:|---|
| `block` | 812 | one generator: name line, OUT 1 / OUT 2 with dots and amplitudes, "= OUT 1", "×N" |
| `pc_tick` | 544 | what changed, so what is redrawn |
| `names_tick` | 192 | the two names scroll in turn |
| `info_text` | 224 | "Sine / Inverted · gated": the waveforms of both outputs and the gating |
| `pc_draw` | 168 | the page |
| others (6) | 226 | |

### `nx_uicore`: 2,208 B

| Function | Bytes | What it does |
|---|---:|---|
| `gen_vm` | 560 | one generator's snapshot: state, frequency, step, times, position in a chain / plan / Terrain protocol, a preset's real frequency |
| `nx_uc_vm` | 436 | the whole snapshot, with the PC monitor's values |
| `nx_uc_diag` | 268 | values for Diagnostics (`NX_FEAT_DIAG`) |
| `nx_uc_terrain_view` | 224 | values for the Terrain screen (`NX_FEAT_TERRAIN`) |
| `nx_uc_slot_info` | 128 | the header of a slot |
| others (34) | 580 | |

The modules behind a switch, with their largest functions: `nx_scr_chain` (`edit_key`, `edit_row`,
`chains_key`, `chains_row`, `plan_row`, `terrain_row`), `nx_terrain` (`nx_terrain_task`, `load_play`,
`nx_tset_fill`), `nx_scr_diag` (`diag_value`, `kt_draw`, `diag_row`), `nx_scr_manual` (`manual_row`,
`change`, `manual_key`), `nx_manual` (`nx_manual_task`, `nx_manual_changed`), `nx_preset`
(`nx_preset_task`, `nx_preset_setup`), `nx_scr_terrain` (`terrain_draw`): their sizes are in the
linker map of the build without inlining.

## 8. How to measure again

```sh
make APP=nx && make APP=nx nx-size                 # the image, per group; the room left
python3 tools/nx_size.py build/nx/gxpro.map --modules   # per module
make APP=nx NX_LCD_OPTS="-DNX_FEAT_DIAG=0" BUILD=build/nx-off-DIAG   # a switch off: compare the images
make APP=nx NX_LCD_OPTS="-DNX_FEAT_HIST=1" BUILD=build/nx-on-HIST    # one of the three back in
make APP=nx NX_TERRAIN=0 NX_PRESETS=0 BUILD=build/nx-nodata
# pieces inside inlined functions (never flash this build):
make APP=nx NX_FLASH_LIMIT=400000 BUILD=build/nx-noinline \
     NX_LCD_OPTS="-fno-inline -fno-ipa-sra -fno-ipa-cp -fno-partial-inlining"
```

A variant build always goes into its own `BUILD` directory, so `build/nx` stays the image that
is flashed. Per-function and per-table sizes are in the linker map (`build/nx/gxpro.map`): the
build uses one section per function and per table.
