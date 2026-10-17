# nx key model and screen map

The keys keep the names printed on the case: LOOP, START, STOP, UP, DOWN, G1, G2,
POWER. A key acts when pressed, except where it also has a "hold" meaning: then a short press acts
when released (within 0.5 s) and holding does the other thing. Held UP/DOWN repeat in lists; the
repeat speed is a setting (Settings > Keys & lock). The UI is in English only. DOWN on the
dashboard opens the menu (Programs, Chains, Manual, Timers, Diagnostics, Settings). After the
first boot's splash a key guide explains the keys; any key closes it and then does what it does on
the dashboard (Settings > Keys & lock > Show key guide opens it again; START or STOP closes it
there).

## Everywhere

| Key | Does |
|---|---|
| POWER short | screen off; any key switches it on again (that first key does nothing else). With "Original key behaviour" on: see below |
| POWER held 0.8 s | power off: both generators stop, then the goodbye (a ring fills from 0.2 s; works even when the keys are locked) |
| G1+G2 held 1.5 s | lock the keys (a lock icon in the status bar; any key shows how to unlock) |
| START+STOP held 1.5 s | unlock the keys |
| after the dim time without a key | the screen dims; the next key acts and brightens it |
| after the "off" time more | screen off, or the night view (Settings > Display > When idle): night colours, 3 % backlight, both generators; the first key only wakes |

## Original key behaviour (Settings > Keys & lock, off by default)

For users who want the original's keys. When on:

| Key | Default | Original key behaviour on |
|---|---|---|
| POWER short | screen off / on | on the dashboard, when a program is loaded and not in PC mode: stop both generators ("Both generators stopped"); elsewhere nothing. The screen goes off only from quick settings > Screen off |
| START on the dashboard | pause and play keep the repeat setting | pause and play clear it |
| LOOP short while running | repeat on/off | pause with repeat on |
| Held UP/DOWN (lists), G1/G2 (calibration) | the "Key repeat" setting: first repeat after 600 / 400 / 300 ms, then every 180 / 110 / 70 ms (every 50 ms once held 1.5 s) | first repeat after 280 ms, then every 26 ms |

## Dashboard (the focused generator large, the other one as a strip at the bottom)

| Key | Does |
|---|---|
| START | play, pause, resume; after the end: play again; no program: open the program list |
| STOP short | stop the focused generator (back to step 1, outputs off; the status shows STOPPED) |
| STOP held 1 s, then released | stop both generators (a ring shows from 0.5 s) |
| LOOP short | running: repeat on/off; stopped or paused: play with repeat |
| LOOP held | the program detail of the loaded program; the manual generator: its screen; a Terrain protocol: the Terrain screen; nothing when none is loaded |
| G1 / G2 | show that generator (the screen slides); the one already shown, or one without a program: its program list (as the original) |
| UP | quick settings: brightness, night mode, lock keys, screen off, auto-dim, reduce motion |
| DOWN | the menu |

The status bar also shows the minutes left of a "stop after" timer, and "Chain 2/5" or "Day 3/11"
while a chain, a plan or the Terrain Protocol plays.

## Menu (DOWN on the dashboard)

A sheet with a grid of 3 columns: Programs, Chains, Manual in the first row, Timers, Diagnostics,
Settings in the second. A cell whose feature is switched off in the build is left out; with more
than 6 cells the grid has 4 columns (History, off by default, comes before Diagnostics when it is
built in). It opens on Settings after power-on, then on the item used last.

| Key | Does |
|---|---|
| G1 / G2 | move left / right along the row (wraps) |
| UP / DOWN | move to the other row (wraps); on the shorter row: the same column, or its last cell |
| START | open: Programs = the focused generator's program list |
| STOP | close |

## Program list and program detail

| Key | Program list | Program detail |
|---|---|---|
| UP / DOWN | choose a slot (the selection and the window stay for the next visit, for both generators) | scroll the frequencies |
| G1 / G2 | the generator it loads into (the badge in the title "Program G1") | the generator it runs on ("Run on") |
| START | load the slot and show that generator (then START plays); an empty slot loads the empty program, as in the original | load and play, then that generator's dashboard; the program it already has loaded: only its dashboard (no restart) |
| LOOP | held: the program detail of the slot | the slot actions (below) |
| STOP | that generator's dashboard, nothing loaded (as the original) | back |

The program detail also opens from the dashboard (LOOP held): the focused generator's program, at
its playing step (when both generators play the slot, at the step of the one it runs on).

## Slot actions (program detail > LOOP)

A sheet "Program NN": Play on G1 and G2, Copy to another slot, Move to another slot, Erase.

| Key | Slot actions | Target list ("Copy NN to" / "Move NN to") |
|---|---|---|
| UP / DOWN | choose an action | choose the target slot (it opens on the first empty slot after the source) |
| START | Play on G1 and G2: load and play the slot on both, then the dashboard. Copy / Move: the target list. Erase: "Erase this program?" | copy or move there; a toast says when it is done; back to the detail (after a move, back past it) |
| STOP | close | back to the detail |

"Erase this program?": hold START 1 s to erase (back past the detail), STOP cancels. A refused
request shows a toast (the target is not empty, a generator has the slot loaded, the source is
empty, the last request is not done yet).

## Chains (menu > Chains)

The list shows the built-in programmes first: the Terrain protocols, then the built-in preset
(both below), then Chain 1–4, then Plan 1–2. It opens on the first Terrain row. (Until
2026-10-13: chains, plans, presets, Terrain.) The title shows the generator it plays on ("Run
on").

| Key | Chains list | Chain editor | Plan editor |
|---|---|---|---|
| UP / DOWN | choose a chain or plan | choose a program | choose a day |
| G1 / G2 | the generator it plays on | the selected program's slot −/+ (1–30, wraps; held: repeats) | the selected day's chain −/+ (1–4) |
| START | play it there and show that generator (a plan: its current day's chain); an empty chain, or an empty or complete plan: its editor | add a copy of the selected program after it ("Add a program" row: at the end) | add a copy of the selected day after it ("Add a day" row: at the end) |
| START held | — | remove the selected program | remove the selected day |
| LOOP | edit | the repeat count: ×1, ×2, ×3, ×5, ×10, until stopped | the plan back to day 1 |
| STOP | back | save and back | save and back |

## Presets with sweeps (menu > Chains, the row after the Terrain protocols)

The built-in preset is listed by its name ("Radionics (Boost) Manifestation"), with a play icon
for each generator it is loaded on. It is in every build; `make APP=nx NX_PRESETS=0` builds
without it.

| Key | Preset row (Chains list) |
|---|---|
| UP / DOWN | choose a row |
| G1 / G2 | the generator it plays on |
| START | load it there and play it (repeating, when the Spooky2 preset repeats), then that generator's dashboard. Its two wave tables go to the generator first (about 1.5 s), then the first step |
| LOOP | nothing |
| STOP | back |

On the dashboard it plays as any program: START pauses and plays on (a sweep goes on where it
was), STOP stops, LOOP switches repeat. After STOP, START plays it once; LOOP plays it repeating.
The big frequency is the output frequency and follows a sweep.

## Terrain Protocol (menu > Chains, the first rows)

Six rows: Terrain, Terrain-Mercury and Terrain-Nonviolent, each as "· Remote" and "· Contact",
with "Day 3/11" (or "complete") and a play icon for each generator that plays it. They are in
every build; `make APP=nx NX_TERRAIN=0` builds without them.

| Key | Terrain row (Chains list) | Terrain screen (LOOP on a Terrain row) |
|---|---|---|
| UP / DOWN | choose a row | the position one set back (UP) / on (DOWN); a generator that plays it goes on at the new set (held: repeats) |
| G1 / G2 | the generator it plays on | the generator it plays on |
| START | play it there from its position, then that generator's dashboard ("Day 3/11" in the status bar) | the same |
| LOOP | the Terrain screen | the power level (Remote): 5, 2.5, 1, 0.5, 0.25 V |
| STOP | back | back to the Chains list |

The Terrain screen shows the set, "Day 6.1 of 11 · 12 h" (Remote) or "Day 6 of 11 · session 1 of
2" (Contact), the time the set has run of its hours and the power level (Remote), what to connect,
and a hint to drink water. A complete Contact protocol shows "All sessions done · START: day 1
again". Stopping a generator keeps the position.

## Manual generator (menu > Manual, or LOOP held on its dashboard)

Rows: Run on, Mode (Fixed / Sweep), Frequency (Fixed) or Start, End, Time, Shape, Repeat (Sweep),
Waveform, Amplitude. Changes apply at once, also while it plays.

| Key | Does |
|---|---|
| UP / DOWN | choose a row |
| G1 / G2 | change the value down / up (held: repeats); on Run on: the generator |
| LOOP | on a frequency row: the digit that G1/G2 change (underlined; 0.01 Hz, 0.1 Hz … 10 MHz, then round again) |
| START | play on the chosen generator; if it already plays there: stop it. The screen stays |
| STOP | back |

## Timers (menu > Timers)

Rows: G1 stops after, G2 stops after, Power off when all finished, Start last programs at boot.
UP/DOWN select, G1/G2 change a value (G1 down, G2 up) or a switch, START steps a value or flips a
switch, STOP back (saved).

## History (menu > History)

Only in a build with `NX_FEAT_HIST=1` (off by default since 2026-10-05).

UP/DOWN scroll, STOP back.

## Diagnostics and key test (menu > Diagnostics)

| Key | Diagnostics | Key test |
|---|---|---|
| UP / DOWN | scroll | shown only |
| LOOP | the key test | shown only |
| START | read the values now (they also refresh every second) | shown only |
| STOP | back | shown only; held 1.5 s: back |
| POWER | as everywhere | shown only; held 0.8 s still switches off |

The key test shows every key event ("pressed", "released after N ms", "held", "held long",
"together") and acts on nothing else; the screen stays lit.

## Offer cards

A card with a ring that fills; a full ring answers "yes" by itself (the set-end card only closes).

| Card | When | Keys |
|---|---|---|
| "Resume where it stopped?" (15 s) | after a power cut or a crash while a program played or was paused; at the end of PC mode, when one played or was paused as it began | START resume now, STOP discard; other keys do nothing; POWER as everywhere |
| "Start the last programs?" (10 s) | auto-start on (menu > Timers), a normal power-on, nothing to resume | START start now, STOP cancel; other keys do nothing; POWER as everywhere |
| "All programs finished · Powering off" (15 s) | "Power off when all finished" on, and every program has ended | any key cancels, a short POWER press too; POWER held switches off as everywhere |
| "Set finished · safe to pause" (60 s) | a Terrain Contact session reaches its 0 Hz end step; the screen wakes and the backlight pulses 3 times (no buzzer) | any key closes it, a short POWER press too; it closes by itself after 60 s |

The resume and auto-start cards show after the splash on the dashboard; the resume card also
when PC mode ends. It continues at the last saved position (at most a minute back), or after PC
mode where PC mode began; paused programs stay paused; a chain or plan goes on. Nothing is offered
after a clean power-off (POWER held), when both generators were stopped, when the slot's program
changed, or when something else was loaded meanwhile. Settings > Keys & lock > "Offer to resume"
switches it off. The auto-start card starts the last program of each generator
from step 1.

## Quick settings and settings

Quick settings (a sheet over the dashboard) and the settings pages (Display, Keys & lock, Motion,
About): UP/DOWN move, G1/G2 change a value, START opens a page or switches, STOP closes. A hint
line at the bottom says so. "Reset all settings" (the last row of Settings) asks first: hold START
1 s to reset, STOP cancels.

Settings > About shows the rows Project ("OpenGX Pro"), Firmware (the version), Developer, Panel
and "Support the project", then the licence lines (font, icons, no warranty, not made by the
device maker). START on "Support the project" opens a page with a QR code of
buymeacoffee.com/mahapo and the address in words; STOP or START goes back.

## PC mode (USB connected)

The PC monitor shows what Spooky2 set (program names, the four output frequencies, outputs on/off,
Out2 following Out1, the waveforms and amplitudes of both outputs and the gating). It stays lit and never sends
anything to the generators: START, STOP and LOOP do nothing, as in the original; UP/DOWN set the
brightness; POWER works as everywhere. Unplugging USB returns to the dashboard.

## Calibration (G1+G2 held while switching on)

Unchanged from the original: UP/DOWN select a value, G1/G2 change it, START saves, LOOP / STOP set
the test level / frequency step, POWER held switches off.

## Screen map

```
power on ─► splash (any key skips) ─► dashboard (a resume or auto-start card may show first)
dashboard(G1) ◄── G1 / G2 ──► dashboard(G2)
  UP ─► quick settings
  LOOP held ─► detail of the loaded program ─► (LOOP) slot actions
            ─► (the manual generator loaded) manual generator
  DOWN ─► menu ─► Programs ─► program list
               ─► Chains ─► (LOOP) chain editor / plan editor / Terrain screen
               ─► Manual ─► manual generator
               ─► Timers
               ─► History (only with `NX_FEAT_HIST=1`)
               ─► Diagnostics ─► (LOOP) key test
               ─► Settings ─► display / keys & lock / motion / about ─► support the project
  START (no program), the focused G again, or the G of an empty generator ─► program list
program list ─► (LOOP held) detail ─► (LOOP) slot actions ─► copy / move: target list
                                                          ─► erase: "Erase this program?"
USB connected ─► PC monitor ─► (USB unplugged) dashboard
STOP = back everywhere
```
