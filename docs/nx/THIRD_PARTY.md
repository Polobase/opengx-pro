# Third-party material in the nx firmware

The nx firmware (`make APP=nx`) contains no vendor material. Its UI assets come from the open
sources below; `tools/gen_nx_assets.py` rasterises them into the tables in `src/nx/nx_assets.c`
(declared in `src/nx/nx_assets.h`).
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

## Own work

The splash shows the wordmark "OpenGX Pro": its "O", a ring with a sine in it, is drawn by the
firmware at run time (`src/nx/nx_scr_sys.c`), and the letters come from the F_BRAND table above;
`tools/nxassets/procedural.py` can draw own icons (waveforms, gate, sweep, output modes, a splash
mark), but the Drop-1 icon sets (`assets/nx/icons.json`) list none of them. The palettes
(`assets/nx/palettes.json`) and the UI texts (`assets/nx/strings/`) are written for this project.
None of them uses Spooky2 logos or artwork. A few texts use Spooky2 product names to say what the
firmware is for and what to connect: "Open firmware for the GeneratorX Pro" on the splash, and
"Spooky Remote on the Boost BN port or Out 1" and "Pads on Boost HIGH PWR CONTACT, or Out 1" on
the Terrain screen.
