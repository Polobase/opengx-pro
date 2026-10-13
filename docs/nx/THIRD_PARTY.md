# Third-party material in the nx firmware

The nx firmware (`make APP=nx`) contains no firmware, font, picture or document of the vendor.
One part of it comes from Spooky2: the data of the built-in Terrain Protocol and of the built-in
preset, two tables made from Spooky2's preset files ("Spooky2 preset data" below).

Its UI assets come from the open sources below; `tools/gen_nx_assets.py` rasterises them into the
tables in `src/nx/nx_assets.c` (declared in `src/nx/nx_assets.h`).
The source files themselves are not committed: `make nx-assets-fetch` downloads them into
`assets/nx/third_party/` (git-ignored) and refuses any file whose SHA-256 differs from
`assets/nx/sources.json`. `assets/nx/assets.lock.json` records the hashes of the sources, the
tool, the rasteriser versions and every generated table.

## Inter 4.1 (text and number fonts)

| | |
|---|---|
| Project | https://github.com/rsms/inter (release v4.1, 2024-11-16) |
| Download | https://github.com/rsms/inter/releases/download/v4.1/Inter-4.1.zip |
| Archive SHA-256 | `9883fdd4a49d4fb66bd8177ba6625ef9a64aa45899767dde3d36aa425756b11e` |
| Copyright | Copyright (c) 2016 The Inter Project Authors (https://github.com/rsms/inter); name table: "Copyright 2016 The Inter Project Authors", version 4.001;git-9221beed3 |
| Licence | SIL Open Font License 1.1: `LICENSES/OFL-1.1.txt` (the archive's `LICENSE.txt`, verbatim) |
| Reserved Font Name | none: the copyright notice declares no Reserved Font Name, so the derived tables may carry the name Inter; they are named by role anyway (`nx_font_cap13`, ...) |

Files used (from `extras/ttf/`, the static hinted TrueType instances): Inter (Regular, Medium)
for the texts, Inter Display (Medium, SemiBold) for the numbers and for the letters of the splash
wordmark:

| File | Used as | SHA-256 |
|---|---|---|
| `Inter-Medium.ttf` | F_CAP13, 13 px | `97ad806f526e41546d46365bb3a393145f75b7b1568913db74549ad8b8dba872` |
| `Inter-Regular.ttf` | F_BODY16, 16 px | `40d692fce188e4471e2b3cba937be967878f631ad3ebbbdcd587687c7ebe0c82` |
| `InterDisplay-Medium.ttf` | F_NUM28, 28 px | `99665c256585aeaab2d06950c2d7715cca8fd7883c2d0e7a421b8b287e1f17de` |
| `InterDisplay-SemiBold.ttf` | F_NUM48, 48 px; F_BRAND, 40 px (the letters "penGX Pro" of the splash wordmark) | `0310d7a325896129730c6c8cf9a6e0f81ee258bedf77b1ff059b2a7b75f74e02` |
| `LICENSE.txt` | `LICENSES/OFL-1.1.txt` | `262481e844521b326f5ecd053e59b98c8b2da78c8ee1bdbb6e8174305e54935a` |

The font tables in `src/nx/nx_assets.c` are bitmaps rendered from these fonts, i.e. a Modified
Version of the Font Software in OFL terms: they are distributed under the OFL 1.1 only, with the
copyright notice in the headers of the generated `src/nx/nx_assets.c` and `src/nx/nx_assets.h`.
The firmware image bundles them, which the OFL allows (see "Distributing a firmware image"
below).

## Material Symbols Rounded (icons)

| | |
|---|---|
| Project | https://github.com/google/material-design-icons, directory `variablefont/`, commit `27e9ef1dbeedc13d682fece4a58e1eda4cb0961a` (2026-09-18) |
| Font | https://raw.githubusercontent.com/google/material-design-icons/27e9ef1dbeedc13d682fece4a58e1eda4cb0961a/variablefont/MaterialSymbolsRounded%5BFILL,GRAD,opsz,wght%5D.ttf |
| Font SHA-256 | `7f3a16d9aa1b6f367eadc1614ad27308ae2c0e91e617e2069f122afd9f0c3c58` (git blob `a2f44a7d7200ace6a9a0186e9cdee22480d8f6a4`) |
| Codepoints | https://raw.githubusercontent.com/google/material-design-icons/27e9ef1dbeedc13d682fece4a58e1eda4cb0961a/variablefont/MaterialSymbolsRounded%5BFILL,GRAD,opsz,wght%5D.codepoints |
| Codepoints SHA-256 | `c18564f64d7d92dd3a6895a2c59ea69adfb56d6f553bcbbc88811c328159d715` |
| Copyright | name table: "Copyright 2026 Google LLC.  All Rights Reserved.", version 2.971 |
| Licence | Apache License 2.0: `LICENSES/Apache-2.0.txt` (the repository's `LICENSE`, verbatim, SHA-256 `58d1e17ffe5109a7ae296caafcadfdbe6a7d176f0bc4ab01e12a689b0499d8bd`); the repository has no NOTICE file |

The icons are rendered from the variable font through FreeType's variation axes (FILL 0 or 1,
GRAD 0, opsz 20 for the 20 px set and 24 for the 32 px set, wght 500); `assets/nx/icons.json`
names every symbol used. The generated tables state that they were rasterised (changed) from
this font (Apache-2.0 section 4).

## segno (QR code, build time only)

| | |
|---|---|
| Project | https://github.com/heuer/segno (Python package `segno`, version 1.6.6 in `tools/requirements.txt`) |
| Copyright | Copyright (c) 2016 - 2025, Lars Heuer |
| Licence | BSD-3-Clause |

The asset tool uses segno at build time to make the table of the QR code on the "Support the
project" page (`tools/nxassets/qr.py`, `assets/nx/qr.json` → `nx_qr_support`). No segno code is in
the firmware: the image holds only the code's modules as a bit table, and the firmware draws them
as squares (`src/nx/nx_scr_support.c`).

## Spooky2 preset data

Two tables in the repository hold data taken from Spooky2's preset collection. They are built
into every nx build and into the released image (since 2026-10-13; before, they were made at
build time from the user's own Spooky2 installation and were not committed).

| Table | For | Holds |
|---|---|---|
| `src/nx/data/nx_terrain_data.c` | the built-in Terrain Protocol (menu > Chains) | Terrain, Terrain-Mercury and Terrain-Nonviolent, each as a Remote and as a Contact protocol: 6 protocols, 31 sets |
| `src/nx/data/nx_preset_data.c` | the built-in preset with sweeps (menu > Chains) | the preset "Radionics (Boost) Manifestation": 18 steps, 198 s, repeating until stopped |

**This data is not this project's own work.** The frequencies, times, names, amplitudes and
schedules in the two tables are material of Spooky2 and of the authors of its presets. The
project does not claim them. The MIT licence of this repository (`LICENSE`) covers the project's
own work, the two generator tools and the table formats included; it does not cover the data in
these two tables.

Where the data comes from: the folder "Preset Collections" of a Spooky2 installation, read by
two tools of this repository.

| Tool | Reads | Takes |
|---|---|---|
| `tools/gen_nx_terrain.py` | the folder "Detox/Terrain Protocol": `Remote/Terrain (R) - BY.txt`, `Remote/Terrain-Mercury (R) - BY.txt` and `Remote/Terrain-Nonviolent (R) - BY.txt`; the files of `Contact/` that the notes of those three presets name in their Contact schedule; the shells in "Detox/Shells" that these presets name as their base preset | per set: its name, its frequencies with the time of each (`Loaded_Frequencies`), its waveform number and the time of the 0 Hz end step of a Contact set. Per protocol: its name and its schedule (Remote: the order of the sets, their day numbers and the 12 or 24 hours each runs; Contact: the two sessions of each day). From the shells: the amplitude (Remote 5 V, Contact 20 V) |
| `tools/gen_nx_preset.py` | "Radionics/Spooky Radionics (Boost) Manifestation - AW.txt" and the shell it names as its base preset | its name (without "Spooky"), its 18 steps (a frequency or a sweep from one frequency to another, and the seconds of each), the cycles per wave table (`Sine_WCM`, 11) and that it repeats. The tool accepts the preset only with the waveform, amplitude and offset settings the firmware plays (Out 1 sine, Out 2 its inverse, 20 V, offsets −100 % and +100 %) and writes them as fixed values |

What is not taken:

- The text of the preset files: their notes and descriptions. The tool reads the notes of the
  three Remote presets only for the lines of the Contact schedule.
- The other versions of the Terrain Protocol (Plasma, Scalar, Laser, Coil) and every other preset
  of the collection. The low-power Remote presets are not read either: the power levels 2.5, 1,
  0.5 and 0.25 V of the Terrain screen are fixed values in `src/nx/nx_terrain.c` that follow them.
- Wave tables. The firmware computes the two tables the preset needs (sine cycles) and the
  standard sine, to the values Spooky2 sends; none is stored.

Nothing else of Spooky2 is in the repository or in a release: none of its software, databases,
waveform files, documents, pictures or logos, and no image, font or bitmap of the original display
firmware. The serial captures of Spooky2 that were used to check the firmware are not included
either.

The tables are generated files and are not edited by hand. `make nx-data` writes them again from
a Spooky2 installation:

```sh
make nx-data TERRAIN_DIR=".../Preset Collections/Detox/Terrain Protocol" \
             PRESET_FILE=".../Preset Collections/Radionics/Spooky Radionics (Boost) Manifestation - AW.txt"
```

To build without this data: `make APP=nx NX_TERRAIN=0` leaves out the Terrain table,
`make APP=nx NX_PRESETS=0` the preset table, and `make APP=nx NX_TERRAIN=0 NX_PRESETS=0` both.
Such an image has no Terrain rows and no preset row in menu > Chains and holds neither table.
The released image always has both: `make release` builds with them and `tools/release_check.py`
checks that they are linked.

The project makes no statement about what these protocols and this preset are for, and no health
or treatment claim. The names on the screen are the names in the preset files.

## Distributing a firmware image

The device shows the notices in short form: its About page (Settings > About) shows the project,
the firmware version, the developer and the panel, the row "Support the project", and then the
licence lines: "Font: Inter", "Copyright 2016 The Inter Project Authors", "SIL Open Font License
1.1", "Icons: Material Symbols", "Copyright Google LLC", "Apache License 2.0", "Free software
without any warranty" and "Not made or endorsed by the device maker". The full notices are in the
headers of the generated `src/nx/nx_assets.c` and `src/nx/nx_assets.h`, and the licence texts in
`LICENSES/OFL-1.1.txt` and `LICENSES/Apache-2.0.txt`. The image itself (`GxCustomFW.bin`, released
as `opengx-pro-<version>.bin`) does not hold the licence texts, so a released image must be
distributed together with those two licence files and the copyright notices above (this file),
e.g. in the same archive or on the same release page:

- OFL 1.1, condition 2: each copy of the (modified) font software, also bundled with other
  software, contains the copyright notice and the licence, as stand-alone text files,
  human-readable headers or metadata the user can easily view. The About page shows the copyright
  notice and the licence's name; the licence text has no place in a firmware image, so the text
  files go with it.
- Apache-2.0, section 4: recipients of the icons get a copy of the licence, and changed files
  carry a notice that they were changed (the generated tables state that they were rasterised
  from the font).

A released image also holds the two tables of "Spooky2 preset data" above. The About page does
not name them; this file, which goes with a release, says what they are and where they come from.

## Own work

The splash shows the wordmark "OpenGX Pro": its "O", a ring with a sine in it, is drawn by the
firmware at run time (`src/nx/nx_scr_sys.c`), and the letters come from the F_BRAND table above;
`tools/nxassets/procedural.py` can draw own icons (waveforms, gate, sweep, output modes, a splash
mark), but the Drop-1 icon sets (`assets/nx/icons.json`) list none of them. The palettes
(`assets/nx/palettes.json`) and the UI texts (`assets/nx/strings/`) are written for this project.
None of them uses Spooky2 logos or artwork. A few texts use Spooky2 product names to say what the
firmware is for and what to connect: "Open firmware for the GeneratorX Pro" on the splash, and
"Spooky Remote on the Boost BN port or Out 1" and "Pads on Boost HIGH PWR CONTACT, or Out 1" on
the Terrain screen. The names of the Terrain protocols, of their sets and of the built-in preset
are not among these texts: they come from the tables of "Spooky2 preset data" above.
