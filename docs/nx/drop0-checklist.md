# Drop 0: device session (answers for the nx design)

Two test apps answer the remaining hardware questions of `docs/nx/fw2-design.md`: `sizeprobe`
(optional: a larger proven image size would give room for more UI) and `nxbring` (display and
backlight). The generator probe, the PB1 test and the Spooky2 captures were cancelled with Drops 3
and 4 (2026-09-24). Each app is flashed like any firmware (power off, hold **Stop**, press
**Power**, then the `make` command); nothing here writes the SPI flash or the bootloader, and
`make flash-original` / `make flash` bring back the original or the classic firmware at any time.
Photograph every result screen and fill in the tables.

## 1. Image size (sizeprobe)

Check the recovery path once first: `make flash-original`, boot, then `make flash`.

For N in 76, 80, 88, 96, 104, 110: `make APP=sizeprobe PAD_KB=N flash-padded`, power-cycle.

| N (KB) | all blocks "C"? | screen shows N KB? | CRC OK? | behind image erased? | notes |
|---|---|---|---|---|---|
| 76 | yes (38 blocks) | – | – | – | 2026-09-25: not the size probe, but the nx image of that day (77,380-byte update file, 38 blocks) was accepted and runs; `FLASH_PROVEN` = 77,816 |
| 80 | – | – | – | – | not run: the larger sizes passed |
| 88 | – | – | – | – | not run: the larger sizes passed |
| 96 | yes (49 blocks) | yes | yes ("every byte arrived") | erased | 2026-09-25; before it the 97,300-byte nx update file (48 blocks) was also accepted |
| 104 | yes (53 blocks) | yes | yes | erased | 2026-09-25 |
| 110 | yes (56 blocks) | yes | yes | erased | 2026-09-25: the largest size tried; `FLASH_PROVEN` = 112,640 |

"screen shows N KB?" must equal the N you just flashed: pad_image.py seeds its filler with the
image length, so a lower N here (a stale trailer left behind by a bootloader that acked blocks it
never wrote) will now show CRC FAIL rather than a false "CRC OK" — but check the number anyway.

Stop at the first failure (black screen, CRC FAIL, refused blocks): Stop+Power, `make flash`.
The largest good N becomes `NX_FLASH_LIMIT` (`make APP=nx NX_FLASH_LIMIT=<N*1024>`).

Erase extent: after the largest good N, flash `make APP=sizeprobe PAD_KB=76 flash-padded` once more
and note the "behind image" line: `erased` means the bootloader erases the whole application region,
`data at 0x…` means it erases only the blocks it writes. Result: not determined (each probe was
larger than the image before it, so "erased" behind it proves nothing).

On the largest good image also record:

| Check | Result |
|---|---|
| reset flags after power-on | SFT PWR PIN (the bootloader adds a software reset); RAM kept: none. nx now takes PWR as a power-on before SFT (`nx_fault.c`) |
| press START (software reset): reset flags / RAM kept | |
| press LOOP (watchdog, ~2 s): reset flags / RAM kept / booted normally? | IWDG PIN; RAM kept at 0x20002000, 0x20004000, 0x20006000, 0x20007000 (the crash record is at 0x20007000); booted normally |
| after the watchdog reset: Stop+Power still enters programming mode? | |

## 2. Display and board (nxbring)

`make APP=nxbring flash`. LOOP steps through the pages.

| Page | Question | Result |
|---|---|---|
| 1 | which MADCTL (UP/DOWN) shows red top, green left, blue bottom-right, F upright? | 0xA0 (seen with `APP=nx` on 2026-09-24: 0x60 showed the landscape picture turned 180°) |
| 2 | START: times for 8-bit / 16-bit / DMA / DMA bands; gradient visible? | |
| 3 | START: white stripe moves left or right? smooth or torn? | |
| 4 | START: stripes intact after sleep and display-off? | |
| 5 | lowest usable level; flicker or whine at 625 Hz / 1 kHz / 2 kHz; 255 = brighter than today? | |
| 6 | MCU temperature and VDD plausible? each key shows its name? physical position of G1 vs G2 (left/right/above)? PB6/PB10 ever set? | |

## Results

The filled tables (and photos) set `NX_FLASH_LIMIT`, the LCD driver mode and the backlight
frequency.
