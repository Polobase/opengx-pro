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
| STOP, short | Stop the generator in focus: back to step 1, outputs off. |
| STOP, held 1 s, then released | Stop both generators. A ring shows from 0.5 s. |
| LOOP, short | While running: repeat on or off. Stopped or paused: play with repeat. |
| LOOP, held | The detail of the loaded program; for the manual generator, its screen. |
| G1 / G2 | Show that generator. The one already shown, or one without a program: its program list. |
| UP | Quick settings. |
| DOWN | The menu. |

The status bar shows the state, the repeat badge, the minutes left of a "stop after" timer, and
"Chain 2/5" or "Day 3/11" while a chain or a plan plays.

## Menu

DOWN on the dashboard opens a sheet with six cells: Programs, Chains, Manual in the first row,
Timers, Diagnostics, Settings in the second. After power-on it opens on Settings, then on the cell
you used last. G1 and G2 move along the row, UP and DOWN change the row, START opens, STOP closes.
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

Menu > Chains lists Chain 1 to 4 and Plan 1 and 2.

| Key | List | Chain editor | Plan editor |
|---|---|---|---|
| UP / DOWN | Choose a row. | Choose a program. | Choose a day. |
| G1 / G2 | The generator it plays on. | The slot of the selected program, down and up. | The chain of the selected day (1 to 4). |
| START | Play it there. An empty chain or plan opens its editor. | Add a copy of the selected program after it. | Add a copy of the selected day after it. |
| START, held | – | Remove the selected program. | Remove the selected day. |
| LOOP | Edit. | The repeat count: ×1, ×2, ×3, ×5, ×10, until stopped. | The plan back to day 1. |
| STOP | Back. | Save and back. | Save and back. |

A plan plays the chain of its current day. When that chain has run to its end, the plan moves on
one day; START on the plan plays the next day. A stopped day does not count.

## Manual generator, timers, diagnostics

**Manual generator** (menu > Manual, or LOOP held on its dashboard). Rows: Run on, Mode (Fixed or
Sweep), Frequency, or Start, End, Time, Shape and Repeat for a sweep, then Waveform and Amplitude.
UP and DOWN choose a row. G1 and G2 change the value, and repeat while held. LOOP on a frequency
row moves the underlined digit that G1 and G2 change. START plays on the chosen generator, or
stops it if it plays there. STOP goes back. Changes apply at once, also while it plays.

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
| "Resume after power cut?" (15 s) | After a power cut or a crash while a program played or was paused. | START resumes now, STOP discards. |
| "Start the last programs?" (10 s) | "Start last programs at boot" is on, a normal power-on, nothing to resume. | START starts now, STOP cancels. |
| "All programs finished · Powering off" (15 s) | "Power off when all finished" is on and every program has ended. | Any key cancels. |

## Quick settings and settings

UP and DOWN move, G1 and G2 change a value, START opens a page, flips a switch or runs an action,
STOP closes. A hint line at the bottom says so.

- **Quick settings** (UP on the dashboard): Brightness, Night mode, Lock keys, Screen off,
  Auto-dim, Reduce motion.
- **Display**: Theme (Dark, Light, High contrast), Brightness, Dim after, Screen off after, When
  idle (Screen off or Night view), Splash screen (Off, Short, Full).
- **Keys & lock**: Key repeat (Slow, Normal, Fast), Lock keys automatically, Resume after power
  cut, Original key behaviour, Show key guide.
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
- Menu: Programs, Chains (LOOP: chain or plan editor), Manual, Timers, Diagnostics (LOOP: key
  test), Settings (Display, Keys & lock, Motion, About).
- USB connected: PC monitor. USB unplugged: dashboard.
