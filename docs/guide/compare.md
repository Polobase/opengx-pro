# The original firmware and OpenGX Pro side by side

This page compares the vendor's original display firmware (v7789) with OpenGX Pro 0.9.0. The
table says what stays the same, what is new and what is different on purpose. The list below it
names the differences you will notice when you use the unit.

"Same" means the same behaviour by design, checked on a computer and on an emulated board. What
has been checked on a real unit so far is in [Known issues](./known-issues.md).

## The table

| Topic | Original firmware | OpenGX Pro | |
|---|---|---|---|
| What Spooky2 gets from the display over USB | replies to the program, name and version commands | the same replies in the same order; the version reply is `:Rev201` on both | Same |
| Commands a stored program sends to a generator | a fixed set of lines per step | the same lines in the same order for each generator | Same |
| PC mode (USB connected) | starts and ends with a fixed sequence | the same sequence | Same |
| Stored programs, names, calibration | in the SPI flash of the display board | same places, same formats; they stay when you change firmware | Same |
| Program list | G1 or G2 opens it; 30 programs, 7 rows; START loads, STOP goes back | the same steps, new look | Same |
| Calibration menu | G1+G2 held while switching on | the same values, keys and commands | Same |
| Factory connector | commands of the factory jig | the same commands and replies | Same |
| Update mode | unit off, hold STOP, press POWER | unchanged: it belongs to the bootloader, which is never written | Same |
| First command to the generators after power-on | after about 4.4 s | after about 4.4 s | Same |
| POWER held | stops both generators, goodbye screen, power off | the same order; the goodbye is shorter | Same |
| Reply time of the display to the PC | about 20 ms (median) | about 2 ms (median) | Different on purpose |
| Length of a step | 1 % longer than set (180 s takes 181.8 s) | exactly as set | Different on purpose |
| Commands to G1 and G2 | G1's lines first, then G2's | both at the same time; the order for each generator is unchanged | Different on purpose |
| POWER pressed briefly | stops both generators | switches the screen off and on; an option gives the original's behaviour back | Different on purpose |
| START | play and pause; clears repeat | play and pause; keeps repeat; option | Different on purpose |
| LOOP while a program runs | pauses with repeat on | switches repeat on or off; option | Different on purpose |
| STOP | stops the shown generator | the same; held 1 s it stops both | Same; the hold is new |
| Keys in PC mode | none work, except power off | UP and DOWN set the brightness; POWER works | Different on purpose |
| Splash and goodbye | vendor artwork, about 4 s each | own drawn screens, 1.5 s each; any key skips the splash | Different on purpose |
| Backlight | always on at one level | brightness setting, dims and switches off when idle | Different on purpose |
| PC screen | names and four frequencies | adds waveforms, amplitudes, "gated"; shows the output frequency for wave tables with several cycles | Different on purpose |
| USB icon in PC mode | changes colour when Spooky2 has found the generator | one icon; it does not show this | Different on purpose |
| Characters in program names | the vendor font | printable ASCII; other bytes show as "?" | Different on purpose |
| Lines that arrive cut off | can shift the next upload | the next line starts clean | Different on purpose |
| Licence check, forced read protection, access check of the display | part of the firmware | not implemented; the check in the generator boards is unchanged | Different on purpose |
| Hang or fault | the unit stops responding | a watchdog restarts it; a fault is noted and the unit restarts; repeated faults switch it off | New |
| Menu, settings, themes, key lock | none | DOWN opens the menu; settings are kept | New |
| Program detail, copy, move, erase, play on both | none | on the unit | New |
| Chains, plans, manual generator with sweeps, timers | none | on the unit | New |
| Terrain Protocol without a PC | only from Spooky2 on the PC | built in: Terrain, Terrain-Mercury and Terrain-Nonviolent, each as Remote and as Contact, with a day counter and a set-end notice; the data is taken from Spooky2's presets | New |
| A preset with sweeps without a PC | only from Spooky2 on the PC | one built in: "Radionics (Boost) Manifestation"; the data is taken from Spooky2's preset | New |
| The list of menu > Chains | none | the six Terrain rows first, then the built-in preset, then Chain 1 to 4 and Plan 1 and 2 | New |
| Resume after a power cut | both generators start empty | the unit offers to continue | New |
| After PC mode | both generators stay empty | the unit offers to continue what played before PC mode | New |
| Diagnostics and key test | none | menu > Diagnostics | New |
| Own data in the SPI flash | none | settings, chains, plans, the positions in the Terrain protocols and the resume position, in an area the original does not use | New |

## What you will notice

1. **POWER pressed briefly switches the screen off.** It no longer stops both generators. To stop
   both, hold STOP for one second and release it.
2. **LOOP while a program runs only switches repeat.** It does not pause. START pauses.
3. **START keeps the repeat setting** when it pauses and plays on.
4. **Steps are exact.** A step of 180 s takes 180 s. A long program ends about 1 % earlier than
   on the original firmware.
5. **The screen dims and switches off.** By default it dims after one minute without a key and
   switches off five minutes later. Any key wakes it; when the screen was off, that first key
   does nothing else. In PC mode the screen stays lit.
6. **The splash and the goodbye are short** and show no vendor artwork. The generators still get
   their first command about 4.4 seconds after power-on; until then the status bar shows
   "STARTING".
7. **The dashboard shows more**, and the frequency also when a program is ready or paused.
8. **DOWN opens a menu, UP opens quick settings.** In PC mode UP and DOWN set the brightness.
9. **Names show ASCII only.** Other characters appear as "?". The texts are English.
10. **In a serial capture** replies come sooner, and the lines for G1 and G2 can overlap in time.
11. **The PC screen can show another number** than the original for presets that use a wave table
    with several cycles: the frequency at the output with the cycle count ("396.00 Hz ×11"),
    where the original shows the value on the wire (36.00 Hz).
12. **Nothing shows which firmware answers on USB.** Both answer the version query with
    `:Rev201`. The splash and Settings > About show the firmware and its version.
13. **Menu > Chains starts with built-in programmes.** The Terrain Protocol and one preset with
    sweeps play without a PC. Their frequencies, times and names are taken from Spooky2's preset
    files ([What OpenGX Pro can do](./features.md)).

Items 1 to 3 have a switch: Settings > Keys & lock > "Original key behaviour" gives back the
original's POWER, START and LOOP and its fast key repeat in lists ([Keys and screens](./keys.md)).

## The full record

- [`DEVIATIONS.md`](../../DEVIATIONS.md) lists every deliberate difference from the original with
  the place in the code, including the errors of the original that are fixed.
- [`docs/parity.md`](../parity.md) goes through every behaviour of the original, says how
  OpenGX Pro covers it and names the test that shows it.
