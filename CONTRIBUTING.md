# Contributing to OpenGX Pro

Thank you for helping. This firmware runs on other people's devices, so a few rules are strict.
Everything else is ordinary: small changes, the checks below, a clear commit message.

## Rules that always hold

**The device**

- Never write the option bytes (read protection) and never write `0x08000000`–`0x08003FFF`. The
  first 16 KB hold the vendor bootloader, the unit is at read-protection level 1, lowering it
  erases the bootloader, and nobody has a copy of it. Nothing in this repository touches either.
- Never flash an image above `FLASH_PROVEN` (112,640 bytes). That is the largest image the
  bootloader is proven to take on a unit; `make flash`, `tools/flash_gx_pro.py` and the browser
  flasher all refuse more.
- The firmware writes its own data only in `0x080000`–`0x1FFFFF` of the SPI flash and never in PC
  mode. The one exception is copying, moving and erasing a program slot on the device, which
  writes the slots and the name table the way an upload from the PC does.

**Vendor material**

- No vendor material in git, in a pull request, in a release file or on the site: no fonts or
  bitmaps of the original firmware, no Spooky2 presets or databases, no serial captures, no vendor
  documents. Everyone uses their own copy (`original/`, never committed).
- The classic firmware and its test apps take the original's fonts and bitmaps from your copy of
  the vendor's update file at build time; the result stays in `build/`. That is why only the nx
  firmware is released as a file.
- A release image holds nothing from Spooky2 presets either. `make release` refuses such a build,
  and `tools/release_check.py` checks the finished image again.

**Behaviour**

- `make` (`APP=fw`, the classic firmware) stays byte-identical unless a change is meant to alter
  it; then `DEVIATIONS.md` says what changed and why. It is the reference the nx firmware is
  compared with.
- What Spooky2 sees on the serial ports stays the same: same replies, same order. No new serial
  commands.
- The nx firmware keeps the original's flows (program list, start, stop, PC mode), so someone who
  knows the device is not surprised. Every deliberate difference goes into `DEVIATIONS.md`.
- The interface is English only.

## Setup

- Arm GNU Toolchain 15.3.Rel1 (`arm-none-eabi-gcc`); on macOS `brew install --cask gcc-arm-embedded`.
- Python 3 for the tools (the committed asset tables were built with 3.14):
  `python3 -m venv .venv && .venv/bin/python -m pip install -r tools/requirements.txt`. The asset
  tool also needs FriBiDi, which Pillow loads at run time for its text layout: on macOS
  `brew install fribidi` with Homebrew's Python (its loader looks in Homebrew's folder).
- Node.js (a current LTS or newer) for the site in `site/`: `npm ci`.
- For the classic firmware and the comparison with the original: your own copy of the vendor's
  update file at `original/GxDisplayFW.bin` (see `README.md`). The nx firmware, its tests and the
  site need none of that.

Personal build settings go into `local.mk` (not in git), for example the folders of your own
Spooky2 presets for a private build:

```make
TERRAIN_DIR = /path/to/Spooky2/Preset Collections/Detox/Terrain Protocol
PRESET_FILE = /path/to/Spooky2/Preset Collections/Radionics/a preset.txt
```

## Checks

Without any vendor file (this is what CI runs):

```sh
make -C tests/host                         # three test binaries, 0 failed
make release                               # the nx image as it is published, size and content checked
make nx-assets-check && make nx-assets-test    # the committed asset tables match a fresh build
.venv/bin/python tools/nx_gallery.py run --host && .venv/bin/python tools/nx_gallery.py compare --source host
.venv/bin/python tools/nx_gallery.py run --host-dev && .venv/bin/python tools/nx_gallery.py compare --source host-dev
.venv/bin/python tools/nx_gallery.py run --emu && .venv/bin/python tools/nx_gallery.py compare --source emu
(cd site && npm test && npm run build)
```

Every feature switch must still build when it is off, for example
`make APP=nx NX_LCD_OPTS=-DNX_FEAT_MANUAL=0 BUILD=build/nx-off-MANUAL`.

With your own copy of the original image and an SPI-flash backup of your unit
(`backups/spi_flash_*.bin`, made with `tools/gx_link.py dump`):

```sh
make                                       # the classic firmware, byte-identical
.venv/bin/python tools/diff.py --jobs 6    # 18 scenarios on the original and on build/fw, compared
# the nx firmware against the original, serial side:
.venv/bin/python tools/diff.py --jobs 6 --candidate build/release/gxpro.elf --serial-only --timing one-sided \
  --out build/sim/diff-nx queries serial_storage serial_adversarial serial_maint cal_trim pc_mode boot_home \
  cal_boot power_off pc_dual_session
.venv/bin/python tools/diff.py --jobs 5 --candidate build/release/gxpro.elf --serial-only --keymap nx \
  --timing anchor --out build/sim/diff-nx-intents ch1_program g2_switch upload_run pc_upload_exit nx_keys
```

After a change of the UI, look at the gallery report (`tools/nx_gallery.py report`) and approve the
screens that changed on purpose: `tools/nx_gallery.py approve --source host <names>`. Baselines
are approved from the host renderer only; `host-dev` and `emu` must then match them.

`make clean` deletes all of `build/`, including results you recorded from your device. To rebuild
one app use `rm -rf build/<APP>`.

On macOS, if the host tests fail to link with "unknown architecture", the Command Line Tools
default to an SDK newer than the installed linker reads; point `SDKROOT` at the previous one, e.g.
`export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk`.

## Releases

1. Set `NX_FW_VERSION` in `src/nx/nx_config.h`; a version below 1.0.0 or with a suffix
   (`1.2.0-rc1`) is published as beta.
2. Tag the commit `v<version>` and push the tag. The release workflow runs `make release STRICT=1`
   and attaches `opengx-pro-<version>.bin`, its SHA-256 and `manifest.json` to a draft release.
3. Flash exactly that file on a real unit and go through `docs/release-checklist.md`, including
   the way back to the original firmware.
4. Publish the release. The site is rebuilt and offers the new version; older versions stay.

A file that has not been on a real unit is not published.

## Style

- C11, the surrounding code's naming and comment density. Comments say why, and where a behaviour
  comes from.
- Plain words in documents and commit messages; say what was checked and how.
- One topic per commit.
