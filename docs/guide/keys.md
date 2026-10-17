# Keys and screens

The unit has eight keys. OpenGX Pro uses the names printed on the case: LOOP, START, STOP, UP,
DOWN, G1, G2 and POWER. A key acts when you press it. Where a key also has a "hold" meaning, the
short press acts when you release it within half a second, and holding does the other thing.
On every screen except the dashboard, STOP goes back.

After the first start a key guide shows the keys once. Any key closes it and then does what it
does on the dashboard. Settings > Keys & lock > Show key guide opens it again.

![The key guide of the first start](../../tests/gallery/nx/key_guide.png) ![Dashboard with a program that is ready](../../tests/gallery/nx/dash_ready.png)

## Everywhere

| Key | What it does |
|---|---|
| POWER, short | Screen off. Any key switches it on again; that first key does nothing else. |
| POWER, held 0.8 s | Power off: both generators stop, then the goodbye screen. A ring fills while you hold. Works when the keys are locked. |
| G1+G2, held 1.5 s | Lock the keys. |
| START+STOP, held 1.5 s | Unlock the keys. |
| No key for the "Dim after" time | The screen dims; the next key acts and brightens it. After the "Screen off after" time more: screen off, or the night view if you chose it; the first key only wakes. |

## Dashboard

The generator in focus is large; the other one is the strip at the bottom.

| Key | What it does |
|---|---|
| START | Play, pause, play on. After the end: play again. Without a program: open the program list. |
| START, held | The biofeedback scan, on the generator shown (only in a build with `NX_FEAT_BFB`). |
| STOP, short | Stop the generator in focus: back to step 1, outputs off. |
| STOP, held 1 s, then released | Stop both generators. A ring shows from 0.5 s. |
| LOOP, short | While running: repeat on or off. Stopped or paused: play with repeat. |
| LOOP, held | The detail of the loaded program; for the manual generator, its screen; for a Terrain protocol, the Terrain screen. |
| G1 / G2 | Show that generator. The one already shown, or one without a program: its program list. |
| UP | Quick settings. |
| DOWN | The menu. |

The status bar shows the state, the repeat badge, the minutes left of a "stop after" timer, and
"Chain 2/5" or "Day 3/11" while a chain, a plan or a Terrain protocol plays.

## Menu

DOWN on the dashboard opens a sheet with six cells: Programs, Chains, Manual in the first row,
Timers, Diagnostics, Settings in the second. After power-on it opens on Settings, then on the cell
you used last. UP and DOWN move through the cells in order and round the end; G1 and G2 move along
the row. START opens, STOP closes.
"Programs" is the program list of the generator in focus.

## Program list and program detail

| Key | Program list | Program detail |
|---|---|---|
| UP / DOWN | Choose a slot. The selection stays for the next visit. | Scroll the frequencies. |
| G1 / G2 | The generator the program loads into (the badge in the title). | The generator it runs on ("Run on"). |
| START | Load the slot and show that generator; START there plays it. | Load and play, then that generator's dashboard. A program it has already loaded is not restarted. |
| LOOP | Held: the detail of the slot. | The slot actions. |
| STOP | That generator's dashboard, nothing loaded. | Back. |

**Slot actions** ("Program NN"): Play on G1 and G2, Copy to another slot, Move to another slot,
Erase. UP and DOWN choose, START does it, STOP closes. Copy and Move open a list of target slots,
on the first empty slot after the source. Erase asks first: hold START for one second, or STOP to
cancel. A message says when a request is done or why it was refused (the target is not empty, a
generator has the program loaded, the source is empty).

## Chains and plans

Menu > Chains is one list. The built-in programmes come first: the six rows of the Terrain
Protocol, then the built-in preset. Chain 1 to 4 and Plan 1 and 2 follow. The list opens on the
first Terrain row; DOWN goes on to the preset, the chains and the plans.

| Key | List | Chain editor | Plan editor |
|---|---|---|---|
| UP / DOWN | Choose a row. | Choose a program. | Choose a day. |
| G1 / G2 | The generator it plays on. | The slot of the selected program, down and up. | The chain of the selected day (1 to 4). |
| START | Play it there. An empty chain or plan opens its editor. | Add a copy of the selected program after it. | Add a copy of the selected day after it. |
| START, held | – | Remove the selected program. | Remove the selected day. |
| LOOP | Edit a chain or a plan. On a Terrain row: the Terrain screen. On the preset: nothing. | The repeat count: ×1, ×2, ×3, ×5, ×10, until stopped. | The plan back to day 1. |
| STOP | Back. | Save and back. | Save and back. |

A plan plays the chain of its current day. When that chain has run to its end, the plan moves on
one day; START on the plan plays the next day. A stopped day does not count.

## Terrain Protocol and the built-in preset

The first rows of menu > Chains. The Terrain rows are Terrain, Terrain-Mercury and
Terrain-Nonviolent, each as "· Remote" and as "· Contact", with the day ("Day 3/11") or
"complete". The preset's row is "Radionics (Boost) Manifestation". A play icon marks a row for
each generator that plays it.

| Key | Terrain row | Terrain screen | Preset row |
|---|---|---|---|
| UP / DOWN | Choose a row. | The position one set back (UP) or on (DOWN). A generator that plays the protocol goes on at the new set. | Choose a row. |
| G1 / G2 | The generator it plays on. | The generator it plays on. | The generator it plays on. |
| START | Play it there from its position, then that generator's dashboard. | The same. | Load it there and play it from its start, then that generator's dashboard. |
| LOOP | The Terrain screen. | Remote: the power level, 5, 2.5, 1, 0.5 or 0.25 V. | Nothing. |
| STOP | Back. | Back to the list. | Back. |

The Terrain screen shows the set, "Day 6.1 of 11 · 12 h" (Remote) or "Day 6 of 11 · session 1 of
2" (Contact), the time the set has run and the power level (Remote), and what to connect. A
Contact protocol that is through shows "All sessions done · START: day 1 again". Stopping a
generator keeps the position; the next START goes on from there.

At the end of a Contact session the card "Set finished · safe to pause" shows, the screen wakes
and the backlight pulses three times. Any key closes the card and does nothing else; after 60
seconds it closes by itself.

On the dashboard the preset plays as any program: START pauses and plays on, STOP stops, LOOP
switches repeat. After STOP, START plays it once and LOOP plays it with repeat.

## Manual generator, timers, diagnostics

**Manual generator** (menu > Manual, or LOOP held on its dashboard). Rows: Run on, Mode (Fixed or
Sweep), Frequency, or Start, End, Time, Shape and Repeat for a sweep, then Waveform and Amplitude.
UP and DOWN choose a row. G1 and G2 change the value, and repeat while held. LOOP on a frequency
row moves the underlined digit that G1 and G2 change. START plays on the chosen generator, or
stops it if it plays there. STOP goes back. Changes apply at once, also while it plays.

**Biofeedback scan** (menu > Bio scan, or START held on the dashboard; only in a build with `NX_FEAT_BFB`). Rows: Run on,
Speed, Refine, Amplitude, Per hit, Save as program (the list scrolls). UP and DOWN choose a row,
G1 and G2 change the value (on Run on, G1 picks G1 and G2 picks G2). Speed: Standard is Spooky2's sweep, every 0.025 % at 70 ms a
step, about 18 minutes; Balanced takes every other frequency, about 9 minutes; Fast also shortens
the step to 45 ms, about 6 minutes. A response is 6 to 20 standard steps wide, so the coarser grid
still finds it; a very narrow one can be missed. Refine on: when the hits' program ends by itself
(not looped), the scan screen comes back on the hits, marked "Pass 2", and sweeps a narrow
window around each hit at half the step, 10 steps each side, about 20 seconds; the new hits
replace the old ones in the program, which plays again, and so on until a pass finds no hits or
you stop or replace the program. START
starts the scan on the chosen generator (from the graph or hits page it first shows the settings,
so a stray press restarts nothing). While the sweep runs, START pauses it: no step goes out,
the generator holds the frequency reached with the outputs on (so going on needs no ramp), the
time left excludes the pause; START again goes on. The line under the rows shows its stage, with a progress bar
for the ramp and the baseline, then the step, the frequency, the missed reads and the reading, a
progress bar, the time elapsed at the left and the time left at the right (from the pace so far). When the sweep begins the rows give way to the scan graph: on top the last readings
(about 20 seconds) scrolling by, their deviation from the running average, with a mark in the
generator's colour above each hit still in view and a line down to its peak; below, the whole
sweep so far, the current column in the generator's colour, a mark above each hit.
When the scan ends with hits they play as the chosen program, repeating, and the dashboard comes
up with a toast saying so (with Refine on they play once, the next pass follows the end; the
last possible pass repeats); menu > Bio scan then opens on the hits, strongest first, with their deviation;
UP and DOWN scroll them. LOOP goes to the next page that has something to show: settings, graph,
hits. STOP ends a running scan and stops the generator (nothing stays loaded on it); the screen
keeps the graph and the hits found so far and says at which step it stopped, until the next scan.
Otherwise STOP goes back.

**Timers** (menu > Timers). Rows: G1 stops after, G2 stops after, Power off when all finished,
Start last programs at boot. UP and DOWN choose, G1 and G2 change, START steps a value or flips a
switch, STOP goes back.

**Diagnostics** (menu > Diagnostics). UP and DOWN scroll, START reads the values now, LOOP opens
the key test, STOP goes back. The key test shows every key event and acts on nothing else; hold
STOP for 1.5 s to leave it. POWER held still switches off.

## Cards that ask

A card with a ring that fills. When the ring is full, the card answers "yes" by itself.

| Card | When | Keys |
|---|---|---|
| "Resume where it stopped?" (15 s) | After a power cut or a crash while a program played or was paused, and when PC mode ends after it took over a program that played or was paused. | START resumes now, STOP discards. |
| "Start the last programs?" (10 s) | "Start last programs at boot" is on, a normal power-on, nothing to resume. | START starts now, STOP cancels. |
| "All programs finished · Powering off" (15 s) | "Power off when all finished" is on and every program has ended. | Any key cancels. |

## Quick settings and settings

UP and DOWN move, G1 and G2 change a value, START opens a page, flips a switch or runs an action,
STOP closes. A hint line at the bottom says so.

- **Quick settings** (UP on the dashboard): Brightness, Night mode, Lock keys, Screen off,
  Auto-dim, Reduce motion.
- **Display**: Theme (Dark, Light, High contrast), Brightness, Dim after, Screen off after, When
  idle (Screen off or Night view), Splash screen (Off, Short, Full).
- **Keys & lock**: Key repeat (Slow, Normal, Fast), Lock keys automatically, Offer to resume
  (after a power cut or PC mode), Original key behaviour, Show key guide.
- **Motion**: Reduce motion, Scroll long names.
- **About**: project, firmware version, developer, panel type, Support the project (a QR code and
  the address of the donation page), the licences of the font and the icons.
- **Reset all settings**: asks first. Hold START for one second, or STOP to cancel.

![Settings](../../tests/gallery/nx/settings.png) ![Settings, the page Keys and lock](../../tests/gallery/nx/settings_keys.png)

## Key lock

Hold G1+G2 for 1.5 s to lock the keys. A lock shows in the status bar, and any key shows the card
"Keys locked · Hold START+STOP to unlock". Hold START+STOP for 1.5 s to unlock. POWER keeps
working: short for the screen, held to switch off. With "Lock keys automatically" the keys lock
10 seconds after the last key while a generator runs. Quick settings > Lock keys locks at once.

## PC mode and calibration

**PC mode** (USB connected). The PC monitor shows what the PC set. START, STOP and LOOP do
nothing, as on the original firmware. UP and DOWN set the brightness. POWER works as everywhere.
Unplugging USB returns to the dashboard. A key that is down in the moment PC mode starts or ends
does nothing on the next screen.

**Calibration** (G1+G2 held while switching on). As on the original firmware: UP and DOWN select
a value, G1 and G2 change it, START saves, LOOP and STOP set the test level and the test
frequency, POWER held switches off.

## Original key behaviour

Settings > Keys & lock > "Original key behaviour" is off by default. Switched on:

| Key | Default | With "Original key behaviour" |
|---|---|---|
| POWER, short | Screen off and on. | On the dashboard with a program loaded: stop both generators. Elsewhere nothing. The screen goes off only from Quick settings > Screen off. |
| START on the dashboard | Pause and play keep repeat. | Pause and play clear repeat. |
| LOOP, short, while running | Repeat on or off. | Pause with repeat on. |
| UP or DOWN held in a list | The "Key repeat" setting. | First repeat after 0.28 s, then every 26 ms. |

## Screen map

- Power on: splash (any key skips it), then the dashboard. A resume or auto-start card may show.
- Dashboard: UP quick settings, DOWN menu, LOOP held program detail (there LOOP: slot actions),
  START without a program or the G key of the shown generator: program list.
- Menu: Programs, Chains (LOOP: chain or plan editor, or the Terrain screen), Manual, Timers,
  Diagnostics (LOOP: key test), Settings (Display, Keys & lock, Motion, About).
- USB connected: PC monitor. USB unplugged: dashboard.
