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

The nx firmware builds without any vendor file. Its fonts and icons are open (Inter, Material
Symbols) and their tables are committed.

## Build

| Command | Result |
|---|---|
| `make release` | The image as it is published, built from scratch in `build/release`: `opengx-pro-<version>.bin` (the update file), its `.sha256` and `manifest.json`. |
| `make APP=nx` | A development build: `build/nx/GxCustomFW.bin`. With a `local.mk` (below) it contains your presets. |
| `make APP=nx nx-size` | Flash and RAM per module, and the image size against the limit. |
| `make flash-release`, `make APP=nx flash` | Flash the release build, or the development build. Put the unit into update mode first ([Install](./install.md)). |
| `make` | The classic firmware (needs your copy of the vendor's image). |

`make release` builds without vendor artwork, preset data, test timing and changed switches,
whatever your `local.mk` says, and `tools/release_check.py` checks the finished image again:
compiler flags, linked files, size and encoding. The image of 0.9.0 is 102,860 bytes. The linker
refuses an nx image above 112,640 bytes (`NX_FLASH_LIMIT` in `ld/nx.ld`), the largest size proven
on a unit; the flash tools refuse it too. The version is `NX_FW_VERSION` in
`src/nx/nx_config.h`.

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

## Your own build with your own Spooky2 presets

The nx firmware can hold two things that are not in the download, because their data comes from
Spooky2's preset files: the Terrain Protocol and one preset with sweeps. If you have a Spooky2
installation, you can build them in for your own unit. Put the paths into a file `local.mk` next
to the `Makefile` (it is not in git):

| Line in `local.mk` | Adds |
|---|---|
| `TERRAIN_DIR = /path/to/Spooky2/Preset Collections/Detox/Terrain Protocol` | the Terrain rows in menu > Chains |
| `PRESET_FILE = /path/to/Spooky2/Preset Collections/Radionics/a preset.txt` | one preset row in menu > Chains |

Then `make APP=nx` reads the presets, writes tables into `build/` and builds them in. The tools
stop the build when a preset needs something the firmware cannot play.

- **Such an image holds data from your Spooky2 installation. It is for your own unit. Do not
  share or publish it.** A release build never contains it.
- Nothing from these files is ever committed; the tables stay in `build/`.
- `make APP=nx NX_TERRAIN=0 NX_PRESETS=0` builds without them although `local.mk` is there.
- These two features have limits of their own and have not been checked on a real unit:
  [`docs/nx/features.md`](../nx/features.md), section 17.

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
| `NX_FEAT_TERRAIN`, `NX_FEAT_PRESET` | follow chains | Only with data from `local.mk` (above) |
| `NX_VENDOR=1` (a `make` variable) | off | The splash artwork of the original, taken from your copy of the vendor's image |

History and the 48-pixel digits are off by default to save space. A build with other switches
than the defaults is your own build, not a release image.

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
