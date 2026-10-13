# Release checklist

The maintainer goes through this list on a real unit before a release is published, with exactly
the file of the draft release ([`CONTRIBUTING.md`](../CONTRIBUTING.md), "Releases"). A file that
has not passed it is not published. Generator outputs stay unconnected unless a step needs them.
Fill in the result column; note anything odd with a photo. What was checked goes into the release
notes and into [Known issues](guide/known-issues.md).

| Run | |
|---|---|
| Version, file name | |
| SHA-256 of the file | |
| Unit, date | |
| Computer, browser | |

## 1. Prepare

| # | Step | Expected | Result |
|---|---|---|---|
| 1.1 | Download `opengx-pro-<version>.bin`, its `.sha256` and `manifest.json` from the draft release into one folder; `shasum -a 256 -c opengx-pro-<version>.bin.sha256` | OK | |
| 1.2 | `cd site && npm ci && node scripts/collect-firmware.mjs --local <folder> && npm run dev`; open `http://localhost:5173/opengx-pro/` in Chrome | Install lists the version (with "beta" when below 1.0.0), its size and "SHA-256 … checked" | |
| 1.3 | Have the vendor's updater `GX_Pro_Firmware_v7789.exe` ready | the file is there | |

## 2. Install

| # | Step | Expected | Result |
|---|---|---|---|
| 2.1 | Unit off, hold STOP, press POWER | the screen shows "Firmware Update" or stays dark (note which) | |
| 2.2 | "Find the unit", choose the wrong port first, then the right one | the wrong port is refused (it answered "X1"); the right one is found ("X2") | |
| 2.3 | Install | every block confirmed, "Done" | |
| 2.4 | Switch off and on | splash on black: the wordmark "OpenGX Pro", the version at the foot; then the dashboard (first start: the key guide) | |

## 3. Screens and programs

| # | Step | Expected | Result |
|---|---|---|---|
| 3.1 | Dashboard without a program | "No program loaded"; upright, colours right (G1 cyan, G2 amber) | |
| 3.2 | G1: program list; UP, DOWN; G2, G1 | 7 rows "NN. name" as stored; the target badge switches | |
| 3.3 | START on a program, then START | loaded, READY; then RUNNING with frequency, step n of N and time left | |
| 3.4 | START, START, STOP | PAUSED, RUNNING, STOPPED | |
| 3.5 | LOOP when stopped; LOOP while running | plays with the repeat badge; the badge goes off and on, the program runs on | |
| 3.6 | Load and play another program on G2; G1, G2 | both run; the strip shows the other generator; the screen slides | |
| 3.7 | Hold STOP for 1 s, release | a ring, then both stop | |
| 3.8 | Play a short program to its end | "Program complete"; outputs off | |
| 3.9 | DOWN: open every cell of the menu, STOP back | Programs, Chains, Manual, Timers, Diagnostics, Settings open; Diagnostics shows the version, both links with 0 errors | |
| 3.10 | menu > Chains | the list opens on the first row, "Terrain · Remote": six Terrain rows (Terrain, Terrain-Mercury, Terrain-Nonviolent, each "· Remote" and "· Contact"), each with its day ("Day 1/11" on a unit that has not played it; Terrain-Nonviolent: "Day 1/10"); DOWN six times: "Radionics (Boost) Manifestation", then Chain 1 to 4 and Plan 1 and 2 | |
| 3.11 | LOOP on "Terrain · Remote"; STOP | the Terrain screen: the set's name, its day and hours ("Day 1 of 11 · 24 h" on a unit that has not played it), "Ran … of …", "Power 5 V · LOOP changes"; back to the list | |
| 3.12 | G1, START on "Terrain · Remote" | a toast; G1's dashboard: the set's name, RUNNING, the row's day in the status bar ("Day 1/11"), the repeat badge; Diagnostics: the G1 link with 0 errors | |
| 3.13 | STOP; menu > Chains | STOPPED; the row shows the same day | |
| 3.14 | G1, START on "Terrain · Contact"; after a few steps STOP | G1's dashboard: the set's name, RUNNING, the row's day in the status bar, no repeat badge; then STOPPED. Contact plays at 20 V: keep the outputs unconnected | |
| 3.15 | menu > Chains, DOWN six times, G1, START on "Radionics (Boost) Manifestation" | G1's dashboard: the name, RUNNING, the repeat badge. For about 1.5 s the time stands (two wave tables go out), then the frequency rises from 36 Hz; "Step 1 of 18". If instead "G1 does not respond" shows and it stops: the generator did not take the table; note it | |
| 3.16 | Watch three steps (33 s); STOP | a sweep, a steady 396 Hz, a sweep, 11 s each; then STOPPED. Diagnostics: the G1 link with 0 errors | |

## 4. PC mode with Spooky2

| # | Step | Expected | Result |
|---|---|---|---|
| 4.1 | Connect USB, open Spooky2 | "PC MODE"; the screen stays lit; Spooky2 finds both generators | |
| 4.2 | Run a preset on G1 | it runs; the PC monitor shows its name, frequencies, waveform and amplitude as Spooky2 does | |
| 4.3 | Run a preset on G2 | the same for G2 | |
| 4.4 | Run a preset on both at once, for 5 minutes | both run their time; Spooky2 shows no error | |
| 4.5 | START, STOP, LOOP; then UP, DOWN | nothing happens; the brightness changes | |
| 4.6 | Close Spooky2. Hold START, unplug USB, release START | the dashboard, "PC disconnected"; the held key did nothing | |

## 5. Settings, lock, screen, power

| # | Step | Expected | Result |
|---|---|---|---|
| 5.1 | Settings: Display, Keys & lock, Motion, About | every page opens; About shows "OpenGX Pro" and the version | |
| 5.2 | About > Support the project; scan the QR code with a phone | the phone opens buymeacoffee.com/mahapo | |
| 5.3 | Change theme and brightness; switch off and on | both are kept | |
| 5.4 | Hold G1+G2 1.5 s; press a key; hold START+STOP 1.5 s | locked, the card "Keys locked"; unlocked | |
| 5.5 | POWER short; any key; then wait without a key | screen off; the key only wakes it; the screen dims after 1 minute and goes off 5 minutes later (the defaults) | |
| 5.6 | Hold POWER while a program plays | a ring, "Goodbye", the unit switches off | |
| 5.7 | Play for more than a minute, pull the power plug, power on | "Resume after power cut?"; START continues at most a minute back | |

## 6. The way back

| # | Step | Expected | Result |
|---|---|---|---|
| 6.1 | Update mode; site > Restore original; choose the vendor's `.exe` | "matches the original v7789" | |
| 6.2 | Install, switch off and on | the original firmware starts and shows its screens; a stored program plays | |
| 6.3 | Update mode; install the release file again (2.1 to 2.4) | OpenGX Pro starts; theme and brightness of 5.3 are still set | |

## 7. Decision

| | |
|---|---|
| Every row passed? If not: what, and is it a known issue? | |
| Published (date) | |
