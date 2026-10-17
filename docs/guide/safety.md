# Safety and recovery

This page says what installing OpenGX Pro can change in your unit and what it cannot, what stays,
how you get back to the original firmware, and what the project does not promise. Version 0.9.1
is a beta: read the last two sections before you install.

## What the installer can write

The display board has a small processor with 128 KB of flash. The first 16 KB hold the vendor's
bootloader. The rest is the application area: the firmware of the display.

- The installer writes **the application area only**. It uses the update mode of the vendor's
  bootloader and the same update protocol as the vendor's updater.
- That protocol has three steps: read the hardware ID, start, send the numbered blocks of the
  application area. It has no command for the bootloader or for the option bytes of the chip, and
  neither the page nor the command-line tool contains anything that writes them.
- The firmware itself never writes the processor's flash either: not the bootloader area, not the
  option bytes.
- The two generator boards have their own firmware. Nothing here changes it.

Because the bootloader is not written, the update mode stays, whatever firmware is installed.
That is the way back, also after an update that stopped half way; such a stop has not been tried
on purpose ([Install, update, go back](./install.md)).

## Do not change the option bytes

The unit is at read-protection level 1. Lowering that level erases the whole chip, the
bootloader included, and there is no copy of the bootloader. Neither OpenGX Pro nor its tools
ever change the option bytes. If you connect a hardware programmer to the board yourself: do not
lower the read protection.

## The size limit

No tool of the project sends an image above **112,640 bytes**. That is the largest size proven on
a real unit: test images of 96, 104 and 110 KB each arrived byte for byte and ran (2026-09-25).
The application area is slightly larger, but a bigger image has never been tried. The image of
0.9.1 is 108,664 bytes.

## What the page checks before it writes

| Check | What happens when it fails |
|---|---|
| The port answers with the hardware ID of the display ("X2") | The other port answers "X1" and is refused; a silent port is refused |
| The file carries the firmware ID of the display board | The file is refused |
| The file is not larger than 112,640 bytes | The file is refused |
| The SHA-256 of a release file matches the published value | The file is not offered for install |
| The unit confirms every block | The block is sent again; after 4 resends the update stops |

## What stays in the unit

Your programs, their names and the calibration are not part of the firmware. They are in a
separate memory chip on the display board, the SPI flash. Installing OpenGX Pro, updating it or
going back to the original does not write that chip.

While it runs, OpenGX Pro uses this chip as follows:

- **Programs, names, calibration**: same places and formats as with the original firmware. They
  change when Spooky2 uploads or erases programs or sends a calibration command, when you save in
  the calibration menu, and when you copy, move or erase a program on the unit yourself.
- **Its own data** (settings, chains, plans, the positions in the Terrain protocols, the resume
  position, the values of the manual generator): only in the address range 0x080000 to 0x1FFFFF,
  which the original firmware does not use. A write outside that range is refused by the code.
  Every write is read back and compared.
- **In PC mode** it writes none of its own data.

After going back to the original firmware, the data of OpenGX Pro stays in that range, unused.

## Back up the SPI flash

You can copy the whole SPI flash (2 MB) to a file before you try anything. This needs the build
tools and a test firmware of this repository, the link probe, which only reads
([`README.md`](../../README.md), "Tools"):

1. Build and flash the probe: `make APP=probe && make APP=probe flash`.
2. Read the backup: `.venv/bin/python tools/gx_link.py dump`. It takes about 7 minutes and writes
   `backups/spi_flash_*.bin`.
3. Flash the firmware you want to use.

The probe is built with your own copy of the vendor's image ([Build from source](./build.md)).

The project has a tool to read this backup. It has no tool to write a backup into the unit.
Programs can be uploaded again from Spooky2.

## No warranty

OpenGX Pro is free software under the MIT licence, without any warranty. You install it at your
own risk. It is an independent project and is not made, checked or endorsed by Spooky2. Ask the
vendor what a third-party firmware means for their warranty and support; this project cannot
answer that.

## What this firmware is not

OpenGX Pro is firmware for the display board of a frequency generator. It shows screens, reads
keys and sends the generator boards the commands for frequency, waveform, amplitude and on or
off. The project makes no statement about any use of the device and no health or treatment claim
of any kind. A program name on the screen is the name stored with the program, nothing more.

The same holds for the two built-in programmes, the Terrain Protocol and the preset with sweeps.
Their frequencies, times and names are taken from Spooky2's preset files. The project did not
write them and makes no statement about what they are for. The names on the screen are the names
in those files ([`docs/nx/THIRD_PARTY.md`](../nx/THIRD_PARTY.md)). The amplitudes are those of
the presets too: the Contact protocols play at 20 V, the Remote protocols at 5 V or less.

## What "beta" means here

0.9.1 is the second public version, after 0.9.0. Its code is tested on a computer and on an
emulated board. On a real unit only a part has been checked so far: installing, starting, and
Spooky2 running programs on both generators in PC mode. The rest of the device checks are open; the list is in
[Known issues](./known-issues.md).

- Keep the vendor's updater file at hand, so you can go back at any time.
- For a first try, leave the generator outputs unconnected, as the project's own device checks do.
- If something looks wrong, switch off, go back to the original firmware and report it on
  GitHub: <https://github.com/Polobase/opengx-pro/issues>.
