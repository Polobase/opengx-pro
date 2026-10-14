# Build from source

Everything needed to build OpenGX Pro is in the repository
<https://github.com/Polobase/opengx-pro>. This page is for developers. It uses the code names of
the source: `nx` is the firmware that is released as OpenGX Pro (`APP=nx`), `classic` is the
re-implementation of the original firmware v7789 (`APP=fw`, the default of `make`).

## What you need

| Tool | For |
|---|---|
| Arm GNU Toolchain 15.3.Rel1 (`arm-none-eabi-gcc`); on macOS `brew install --cask gcc-arm-embedded` | the firmware |
| `make`, a C compiler for your computer, Python 3 | the build steps and the host tests |
| `python3 -m venv .venv && .venv/bin/python -m pip install -r tools/requirements.txt` | the flasher, the emulator and the gallery tool |
| Your own copy of the vendor's image at `original/GxDisplayFW.bin` | only the classic firmware and the comparison with the original |

The nx firmware builds from the repository alone: no vendor file and no Spooky2 installation is
needed. Its fonts and icons are open (Inter, Material Symbols) and their tables are committed. So
are the two tables of the built-in Terrain Protocol and the built-in preset (`src/nx/data/`),
which hold data taken from Spooky2's preset files (below).

## Build

| Command | Result |
|---|---|
| `make release` | The image as it is published, built from scratch in `build/release`: `opengx-pro-<version>.bin` (the update file), its `.sha256` and `manifest.json`. |
| `make APP=nx` | A development build: `build/nx/GxCustomFW.bin`. With the default switches it has the same features as the release image. |
| `make APP=nx nx-size` | Flash and RAM per module, and the image size against the limit. |
| `make flash-release`, `make APP=nx flash` | Flash the release build, or the development build. Put the unit into update mode first ([Install](./install.md)). |
| `make` | The classic firmware (needs your copy of the vendor's image). |

`make release` builds with the two built-in tables and without the original's splash artwork,
test timing and changed switches, whatever your `local.mk` says. `tools/release_check.py` checks
the finished image again: compiler flags, linked files, size and encoding. The image of 0.9.0 is
108,456 bytes; since the two text fonts went to 2 bits per pixel (2026-10-14) the default build
is 105,632 bytes, 7,008 below the limit. The linker refuses an nx image above 112,640 bytes
(`NX_FLASH_LIMIT` in `ld/nx.ld`), the largest size proven on a unit; the flash tools refuse it
too. The version is `NX_FW_VERSION` in `src/nx/nx_config.h`.

You can also install a file you built with the site: its "Restore original" page takes any
display firmware file with the right ID and size.

## Tests

- **Host tests**: `make -C tests/host` builds and runs three test programs on your computer: the
  protocol and storage code, the nx modules on a model of the board, and the asset tables. State
  on 2026-10-13: 971 + 19,198 + 22,592 checks, 0 failed.
- **Emulator**: `tools/emu.py` runs a build on an emulated display board (processor, SPI flash,
  panel, serial ports), presses keys, feeds serial input and renders the screen to PNG.
  `tools/diff.py` runs the scenarios of `tools/scenarios/` on the original and on a build and
  compares screens and serial traffic; it needs your copy of the vendor's image.
- **Gallery**: `.venv/bin/python tools/nx_gallery.py run --host` renders every screen of the nx
  firmware from sample data. `.venv/bin/python tools/nx_gallery.py compare --source host` checks
  the result against the 122 approved pictures in `tests/gallery/nx/`, pixel for pixel.

The full list of checks, and the rules for changes, are in
[`CONTRIBUTING.md`](../../CONTRIBUTING.md). How the firmware is compared with the original:
[`docs/parity.md`](../parity.md).

## The built-in Terrain Protocol and preset

The Terrain Protocol and the preset with sweeps are built into every nx build and into the
download. Their data comes from Spooky2's preset files: frequencies, times and names. It is
committed as two generated tables, so a normal build needs nothing from Spooky2:

| Table | Holds |
|---|---|
| `src/nx/data/nx_terrain_data.c` | Terrain, Terrain-Mercury and Terrain-Nonviolent, each as Remote and as Contact: 6 protocols, 31 sets |
| `src/nx/data/nx_preset_data.c` | the preset "Radionics (Boost) Manifestation": 18 steps, 198 s |

These tables are not this project's own work, and the MIT licence does not cover them:
[`docs/nx/THIRD_PARTY.md`](../nx/THIRD_PARTY.md), "Spooky2 preset data".

**The tables from your own Spooky2 version.** If you have a Spooky2 installation and want the
tables made from its preset files, `make nx-data` writes both again:

```sh
make nx-data TERRAIN_DIR="/path/to/Spooky2/Preset Collections/Detox/Terrain Protocol" \
             PRESET_FILE="/path/to/Spooky2/Preset Collections/Radionics/Spooky Radionics (Boost) Manifestation - AW.txt"
make APP=nx
```

- The two paths can also stand in a file `local.mk` next to the `Makefile` (it is not in git).
  Only `make nx-data` uses them; a normal build does not.
- `make nx-data` overwrites the two files in `src/nx/data/`; `git diff` shows what changed.
- A tool stops when a preset needs something the firmware cannot play, and leaves its table as
  it was. The Terrain tool expects the three presets with the sets and days it knows; of the
  Radionics presets only this one is accepted.
- An image with tables of your own is your own build, not a release image.

**Without the tables.** `make APP=nx NX_TERRAIN=0` builds without the Terrain rows,
`make APP=nx NX_PRESETS=0` without the preset row, and both together without any data from
Spooky2's presets. A release image always has both tables: `make release` builds with them, and
the build and `tools/release_check.py` stop when one is missing.

| Build | Image |
|---|---|
| default (the release image) | 105,632 bytes (0.9.0, with 4 bpp text fonts: 108,456) |
| `NX_PRESETS=0` | 104,424 bytes |
| `NX_TERRAIN=0` | 101,252 bytes |
| `NX_TERRAIN=0 NX_PRESETS=0` | 100,044 bytes |

Neither feature has been checked on a real unit yet: [Known issues](./known-issues.md).

## Optional features and their switches

Each feature beyond the original has a switch in `src/nx/nx_config.h`. Switching one off removes
its code and its menu cell. Set a switch on the command line, for example
`make APP=nx NX_LCD_OPTS=-DNX_FEAT_HIST=1` or
`make APP=nx NX_LCD_OPTS=-DNX_FEAT_MANUAL=0 BUILD=build/nx-off-MANUAL`.

| Switch | Default | Feature |
|---|---|---|
| `NX_FEAT_DIAG` | on | Diagnostics and the key test |
| `NX_FEAT_SLOTS` | on | Play on both, copy, move and erase a program slot |
| `NX_FEAT_CHAINS` | on | Chains |
| `NX_FEAT_PLANS` | follows chains | Plans |
| `NX_FEAT_MANUAL` | on | Manual generator and sweeps |
| `NX_FEAT_TIMERS` | on | Timers and auto-start |
| `NX_FEAT_SUPPORT` | on | Settings > About > Support the project |
| `NX_FEAT_HIST` | off | History: a list of what was played, and totals |
| `NX_FEAT_HERO48` | off | The dashboard frequency in 48-pixel digits when it fits |
| `NX_FEAT_BFB` | off | Biofeedback scan on the unit (menu > Bio scan). About 4.5 KB; fits with every other feature on since the text fonts went to 2 bpp (110,088 bytes): `make APP=nx NX_LCD_OPTS=-DNX_FEAT_BFB=1 BUILD=build/nx-bfb` |
| `NX_FEAT_TERRAIN`, `NX_FEAT_PRESET` | follow chains | The code of the Terrain Protocol and of the preset with sweeps |
| `NX_TERRAIN`, `NX_PRESETS` (`make` variables) | on | Their two tables (above); `NX_TERRAIN=0` or `NX_PRESETS=0` leaves the rows out of menu > Chains |
| `NX_VENDOR=1` (a `make` variable) | off | The splash artwork of the original, taken from your copy of the vendor's image |

History, the 48-pixel digits and the biofeedback scan are off by default to save space: the image
of 0.9.0 leaves 4,184 bytes (a build of this source about 3.8 KB), the first two cost about 2 KB
each and the scan about 4.5 KB. The linker stops when an image is too large.
The splash artwork of the original needs your own copy of the vendor's image and is never in a
release. A build with other switches than the defaults is your own build, not a release image.

## The classic firmware

`make` builds the classic firmware: a re-implementation of the original v7789 from the behaviour
specification in `docs/spec/`, with the differences listed in
[`DEVIATIONS.md`](../../DEVIATIONS.md). It exists as source only. Its
screens use the fonts and bitmaps of the original, which the build takes from **your own copy**
of the vendor's image at `original/GxDisplayFW.bin`; the result stays in `build/`. That file is
the image inside the vendor's updater `GX_Pro_Firmware_v7789.exe`
([`README.md`](../../README.md), "Build and test").

The classic firmware is the reference for the nx firmware. With the vendor's image and an
SPI-flash backup of your unit, `.venv/bin/python tools/diff.py --jobs 6` runs 18 scenarios on the
original and on the classic build and compares them.
