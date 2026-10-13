# Known issues and what has not been checked yet

OpenGX Pro 0.9.0 is a beta. Its code is tested on a computer and on an emulated board, but most
of it has not yet been checked on a real unit. This page says what was checked and how, what is
still open, and which limits are known. State: 2026-10-13, before the release checklist was run
with the release file.

## How it was tested

| Test | What it covers | Result |
|---|---|---|
| Host tests, on a computer | protocol, storage, program runner, keys, screens logic, power cuts during writes | 971 + 19,198 + 22,592 checks, 0 failed |
| Screen gallery | every screen drawn from sample data, compared with approved pictures | 122 pictures, pixel-exact |
| Emulated board, re-implementation of the original | 18 scenarios against the original firmware: screens pixel by pixel, serial traffic line by line | 18 of 18 match |
| Emulated board, OpenGX Pro | 15 of these scenarios against the original firmware: the serial traffic, the storage and how each run ends (the screens differ on purpose) | 15 of 15 pass on the release build (2026-10-13) |
| Real unit | see below | part done |

## Checked on a real unit

All on one unit, with development builds, on 2026-09-25:

- **Installing.** Test images of 96, 104 and 110 KB went through the update mode, arrived byte for
  byte and ran. 110 KB (112,640 bytes) is therefore the size limit of the project.
- **Starting.** The firmware with the new screens was installed and started.
- **Spooky2 in PC mode with both generators at once.** One program on each generator, started
  together: both ran in parallel, as with the original firmware.
- **Reply time** of the display to the PC ([Measurements](./measurements.md)).

## Not yet checked on a real unit

The device checklist ([`docs/nx/drop1-checklist.md`](../nx/drop1-checklist.md)) has a result only
for the size test and for the Spooky2 run above. Everything else in it is open. These parts work
in the host tests and on the emulated board; on a unit they have not been checked:

| Area | Open |
|---|---|
| Screens and keys | A walk through every screen: dashboard, program list, detail, menu, quick settings, settings, key lock, dimming and screen off, the key guide, the messages |
| Programs without a PC | Frequencies and step time of a stored program on both generators, measured at the outputs; repeat; a program to its end |
| Generator link | The rows of both links in Diagnostics (lines, errors, reply time), and that "not responding" does not show in normal use |
| PC mode | The fields of the PC monitor against what Spooky2 shows; an upload from Spooky2 and unplugging USB; a longer run with several starts and stops; the reply check against the original with this version |
| Power | Power off while both generators play; settings kept over a power cycle; resume after a power cut |
| Timers and auto-start | Stop after N minutes, power off when all finished, start the last programs at power-on |
| Slot actions | Copy, move, erase and "play on both", and what Spooky2 then reads from the unit |
| Chains and plans | Playing, the change of waveform and amplitude between two programs, resume inside a chain |
| Terrain Protocol | The six rows and the Terrain screen; a Remote set at the outputs, its power levels and the change to the next set after its hours; a Contact session to its set-end notice; the position after STOP, after a power cut and with auto-start. The sets play the generator boards' standard waveform tables, which the original firmware never does without a PC |
| Built-in preset with sweeps | Whether the generator boards take a waveform table from the display at all; the output on a scope, against the same preset played from Spooky2; pause, stop and resume after a power cut |
| Manual generator and sweeps | Every waveform at the outputs, live changes, sweeps |
| Calibration menu | The grid and its keys on the unit |
| Original key behaviour | The four changes of that option |
| Watchdog and crash recovery | A watchdog reset was seen with a test program on the unit; the watchdog and the crash recovery of this firmware were not |
| New in 0.9.0 | The splash with the wordmark, Settings > About, and the QR code scanned with a phone |
| Going back through the site | Restoring the original from the vendor's updater file in the browser |
| A stopped update | An update interrupted on purpose has not been tried ([Install](./install.md)) |

Before a version is published, the maintainer goes through
[`docs/release-checklist.md`](../release-checklist.md) on a real unit with exactly the release
file.

## Known limits

**Waveforms without a PC**

- The manual generator and the Terrain sets play the generator boards' standard waveform tables
  (sine, square, sawtooth and so on) without a PC. The original firmware never does that; it only
  plays the tables of stored programs. Whether the boards play them this way has not been checked.
- Before the manual generator plays a sine for the first time after power-on, after a PC session
  or after the built-in preset, the firmware sends the standard sine table to the generator board,
  because a preset can leave another shape there. The start is about half a second later that
  once. Whether the boards accept a table from the display has not been checked. If they do not,
  the firmware waits 2 seconds once and plays what the table holds.
- The other waveforms play as the last Spooky2 preset left their tables. That includes the two
  the Terrain sets use, the square and the inverse sawtooth.
- The display cannot see the shape of a stored program's waveform table. It shows "wave #N ?"
  and draws a sine as a stand-in.
- The waveform table of a stored program is in the generator board that received the upload from
  Spooky2. A program uploaded through one generator plays on the other with whatever table that
  one holds. This is the same with the original firmware; "Play on G1 and G2" and chains make it
  easier to meet. A copied or moved program keeps the table number of its source slot.
- Without a PC, Out 2 always follows Out 1 inverted, as with the original firmware. The built-in
  preset sets the waveform table and the offset of Out 2 itself, as Spooky2 does for that preset;
  there too Out 2 is the inverse of Out 1.

**Terrain Protocol and the built-in preset**

- Their frequencies, times and names are taken from Spooky2's preset files
  ([`docs/nx/THIRD_PARTY.md`](../nx/THIRD_PARTY.md)). Neither has been checked on a real unit.
- Terrain, Contact: the sets play with Out 2 following Out 1 inverted. Three things Spooky2 does
  for its Contact presets are not reproduced: the carrier on Out 2, the amplitude ramp up and
  down, and the lower amplitude at low frequencies. A Contact set plays its 20 V at once and at
  every frequency.
- Terrain, Remote: only running time counts towards the hours of a set. The unit has no clock,
  so a pause or the time it is switched off does not count.
- The end of a Contact session shows on the screen only (a card and three pulses of the
  backlight). The unit has no buzzer.
- The Plasma, Scalar, Laser and Coil versions of the Terrain Protocol are not included.
- One preset is built in, no other. To play it the display must send two waveform tables to the
  generator board. If a board does not answer a table within 2 seconds, the preset stops with the
  message that the generator does not respond.
- A step of the preset lasts 11.00 s; with Spooky2 the same step takes 11.02 to 11.09 s. One
  generator setting differs from a Spooky2 session (`:w15` is 0,0, with Spooky2 1,1). If the
  output differs from a Spooky2 run, this is the first thing to look at.
- For the preset the waveform view of the dashboard says "1 = 2", although Out 2 is the inverse
  of Out 1. LOOP on the preset's row does nothing, although the hint line offers "LOOP edit".
  After STOP, START plays the preset once; LOOP plays it with repeat.

**Playing**

- A chain plays each program once. A plan has no clock: you start the next day with START. A day
  whose chain repeats "until stopped" never completes.
- Resume goes back at most one minute. It is not offered after a PC session or when the program
  in the slot changed. The manual generator is not resumed and not auto-started.
- Auto-start plays the last program of each generator from step 1. It does not restart a chain
  or a plan. A Terrain protocol goes on from its stored position; the built-in preset plays from
  its start.
- A sweep of the manual generator sends a new frequency every 125 ms at most.

**PC mode**

- Both firmwares answer the version query with `:Rev201`. Spooky2 and the installer cannot tell
  which firmware is installed.
- The original firmware shows by the colour of its USB icon whether Spooky2 has found the
  generator. OpenGX Pro has one USB icon and does not show this.
- The PC monitor shows frequency, waveform, amplitude, "gated", Out 2 following and on or off. It
  does not show offset, duty cycle, phase or measured values. A waveform number it does not know
  shows as "#N". The cycle count of a wave table is found from its shape; for unusual tables the
  monitor shows the value on the wire.
- The restart command of the PC (`:w95`) restarts the display, as with the original firmware.
  Whether Spooky2's Reset button uses it, and whether the unit then switches off as Spooky2's
  guide says, has not been checked.

**Other**

- English only. Names show printable ASCII; other characters appear as "?".
- The chip temperature and supply voltage in Diagnostics are not calibrated.
- A few hardware values are defaults that were not measured on the unit, for example whether the
  backlight flickers or whines at its 1 kHz switching frequency.

## Personal builds

History and the 48-pixel frequency digits are not in the download. They can be switched on in a
build of your own ([Build from source](./build.md)). History has not been checked on a real unit.
It has no clock: it lists a session number and the uptime instead of a date. The details are in
[`docs/nx/features.md`](../nx/features.md), section 17.

## Report a problem

Open an issue at <https://github.com/Polobase/opengx-pro/issues>. Say which version you run
(Settings > About), what you did, what you expected and what happened. A photo of the screen
helps. You can go back to the original firmware at any time ([Install](./install.md)).
