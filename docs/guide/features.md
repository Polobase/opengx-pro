# What OpenGX Pro can do

OpenGX Pro is open firmware for the display board of the Spooky2 GeneratorX Pro. It replaces the
firmware of the display only; the two generator boards and the bootloader stay as they are. This
page lists what version 0.9.0 (beta) contains: the file that the site installs. OpenGX Pro is an
independent project, not affiliated with or endorsed by Spooky2.

The pictures are drawn by the firmware's own screen code from sample data. The program names and
numbers in them are examples; a program name is only the name stored with a program.

Most of this was tested on a computer and on an emulated board, not yet on a real unit. What was
checked on a unit, and what was not, is in [Known issues](./known-issues.md).

## On the screen

![Splash: the OpenGX Pro wordmark and the version](../../tests/gallery/nx/splash_1100.png) ![Dashboard while a program runs](../../tests/gallery/nx/dash_running.png)

The splash shows the name and the version. The dashboard shows one generator large and the other
as a strip at the bottom.

- **Dashboard**: program name, frequency, step n of N, time per step and time left in the step,
  amplitude and waveform, a waveform view, a progress bar with the time left, the next frequency.
  The frequency also shows when the program is ready or paused.
- **States**: no program, ready, running, paused, stopped, finished ("Program complete") and
  "not responding" when a generator board stops answering.
- **Menu** (DOWN): Programs, Chains, Manual, Timers, Diagnostics, Settings.
- **Quick settings** (UP): brightness, night mode, lock keys, screen off, auto-dim, reduce motion.
- **Themes**: Dark, Light and High contrast. Brightness from 10 to 100 %.
- **Dim and screen off**: the screen dims after a settable time without a key and switches off
  later. Instead of a dark screen you can choose a night view with both generators at a glance.
- **Settings are kept** over a power cycle. Settings > About shows the project, the version, the
  panel type and the licences, and opens "Support the project".
- All texts are in English.

![Menu](../../tests/gallery/nx/menu.png) ![Quick settings](../../tests/gallery/nx/quick.png)

The menu and the quick settings open over the dashboard.

![Dashboard, light theme](../../tests/gallery/nx/dash_running_light.png) ![Night view](../../tests/gallery/nx/night_view.png)

The light theme, and the night view that can replace the dark screen when the unit is idle.

![Settings, About](../../tests/gallery/nx/settings_about.png) ![Support the project](../../tests/gallery/nx/support.png)

Settings > About, and the page "Support the project" with its QR code.

## Without a PC

- **Program list**: the 30 stored programs, chosen and started as on the original firmware. G1 or
  G2 opens the list, UP and DOWN choose, START loads, START plays, LOOP plays with repeat, STOP
  stops.
- **Program detail** (hold LOOP): all step frequencies of a program, the playing step marked.
- **Slot actions**: play a program on both generators, copy or move it to another slot, erase it.
- **Chains**: up to 4 chains of up to 12 programs, played one after the other, with a repeat count.
- **Plans**: 2 plans of up to 14 days with one chain per day. The unit has no clock: you start the
  next day with START.
- **Manual generator**: a fixed frequency from 0.01 Hz to 40 MHz, or a sweep (linear or
  logarithmic; once, again, or back and forth), with one of nine waveforms and an amplitude from
  0 to 20 V in steps of 0.1 V, on G1 or G2. Out 2 follows Out 1 inverted.
- **Timers**: stop a generator after 5 to 240 minutes of running, power off when all programs
  have finished, start the last programs at power-on.
- **Resume after a power cut**: the unit offers to continue what was playing, at most one minute
  back.
- **Key lock**: hold G1+G2 to lock, START+STOP to unlock. It can lock by itself while a program
  runs.
- **Diagnostics and key test**: firmware version, uptime, last reset, both generator links, PC
  mode counters, SPI flash, main loop, chip temperature and supply (not calibrated).
- **Calibration menu**: G1+G2 held while switching on, with the keys of the original.

![Program list](../../tests/gallery/nx/library.png) ![Program detail](../../tests/gallery/nx/detail.png)

The program list keeps the original's seven rows. The detail lists every step.

![Chains and plans](../../tests/gallery/nx/chains.png) ![Manual generator, a sweep](../../tests/gallery/nx/manual_sweep.png)

Chains and plans share one list. The manual generator sets a frequency or a sweep by hand.

![Timers](../../tests/gallery/nx/settings_timers.png) ![Resume after a power cut](../../tests/gallery/nx/resume_offer.png)

The timers page, and the offer after a power cut.

Every key and screen is described in [Keys and screens](./keys.md).

## With Spooky2 (PC mode)

When USB is connected the unit is in PC mode, as before. The firmware answers the commands of the
PC with the same replies in the same order as the original firmware. The code for this is shared
with a re-implementation of the original that was compared with it line by line on an emulated
board. Only lines that arrive cut off are handled differently, on purpose
([parity record](../parity.md)).

- **PC monitor**: program names, the four output frequencies, outputs on or off, Out 2 following
  Out 1, the waveforms and amplitudes the PC set, and "gated". The screen only displays; it never
  sends anything to the generators.
- **Output frequency**: when a preset uses a wave table of several cycles, the monitor shows the
  frequency at the output with the number of cycles ("396.00 Hz ×11").
- **Keys**: START, STOP and LOOP do nothing, as on the original. UP and DOWN set the brightness.
- **Reply time**: about 2 ms for a query to the display ([Measurements](./measurements.md)).

![PC monitor](../../tests/gallery/nx/pc_monitor.png) ![PC monitor with waveforms and amplitudes](../../tests/gallery/nx/pc_monitor_info.png)

The PC monitor, without and with the waveforms and amplitudes the PC sent.

## Robustness

- **Watchdog**: if the firmware hangs for 2 seconds, the chip restarts.
- **Crash recovery**: after a fault the firmware notes it and restarts. If faults repeat within
  30 seconds of each start, the third one switches the unit off instead.
- **Generator link**: after three unanswered lines in a row the dashboard says which generator
  does not respond.
- **Own data**: settings, chains, plans and the resume position are stored with a checksum, in a
  way that keeps the old or the new value when the power fails during a write.
- **Storage rules**: the firmware writes its own data only in a part of the SPI flash that the
  original firmware does not use, reads every write back, and writes none of it in PC mode
  ([Safety](./safety.md)).
- **Fixes**: each generator uses its own step data, a step lasts exactly its time, and holding
  POWER from switch-on no longer switches the unit off again ([compare](./compare.md)).

![Keys locked](../../tests/gallery/nx/lock_card.png) ![A generator does not respond](../../tests/gallery/nx/dash_error.png)

The card of the key lock, and the dashboard when a generator board does not answer.

## Not in the download

- The Terrain Protocol and a built-in preset with sweeps. They exist only in a personal build
  made from your own Spooky2 preset files ([Build from source](./build.md)).
- History and the 48-pixel frequency digits. They are switched off to save space and can be built
  in.
- The splash artwork of the original firmware.
