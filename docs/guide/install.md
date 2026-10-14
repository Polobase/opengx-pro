# Install, update, go back to the original

OpenGX Pro is installed over USB, through the update mode the unit already has. A page of the
project site does it from the browser: <https://polobase.github.io/opengx-pro/>. The same site
puts the vendor's original firmware back. Read [Safety and recovery](./safety.md) first; version
0.9.1 is a beta ([Known issues](./known-issues.md)).

## What you need

- A Spooky2 GeneratorX Pro. Not the original GeneratorX: it looks the same from the front, but it
  differs inside (Spooky2's change log of 2022-04-28: another frequency resolution), and OpenGX
  Pro is neither made nor tested for it. Spooky2 publishes display firmware updates for the GX Pro
  only, so on an original GeneratorX there would be no file for the way back.
- A desktop computer with Chrome, Edge, Opera or Brave. The page talks to the unit with Web Serial,
  which these browsers have. Firefox and Safari do not have it. An older Brave has it switched
  off: update Brave, or open `brave://flags`, search for "serial", set "Web Serial API" to
  Enabled and relaunch.
- The USB cable of the unit. Close Spooky2 first, so that it does not use the USB port at the same
  time.
- For the way back: the vendor's own updater file `GX_Pro_Firmware_v7789.exe`, from Spooky2's
  download page <https://www.spooky2.com/downloadspage/>. Get it before you install. The
  bootloader cannot read the installed firmware back, so there is no "backup of what is
  installed": going back always uses the vendor's file.

Your programs, their names and the calibration are not in the firmware. They stay where they are.

## Install

1. **Choose a version** on the site. The page loads the file and checks its SHA-256 against the
   value published with the release. It goes on only if they match.
2. **Put the unit into update mode.** Connect it by USB. Switch it off. Then hold STOP and press
   POWER. The screen shows "Firmware Update" (on some units it stays dark): the unit now waits for an
   update. START leaves update mode again without changing anything.
3. **Choose the USB port.** Click "Find the unit" and choose a port in the browser's list. The
   unit has two USB serial ports, and in update mode only one of them answers for the display.
   Their names differ from unit to unit and from computer to computer (Windows: "USB Dual_Serial"
   with two COM numbers; a Mac may list each port twice), so there is no name to look for. Choose
   one. If it is the other port, the page says so and writes nothing: click "Choose another port"
   and take a different entry.
4. **Install.** It takes about a minute. Keep the cable connected and the page open until it says
   "Done".
5. **Restart.** Switch the unit off and on again. The splash shows "OpenGX Pro" and the version.
   On the first start the key guide follows.

![The splash after the restart](../../tests/gallery/nx/splash_1100.png)

The splash of OpenGX Pro 0.9.1.

## What the page checks

- **The right port.** Before it writes anything, the page asks the port for its hardware ID. The
  display answers "X2". The other port answers "X1"; the page refuses it and asks you to choose
  a different entry. A port that does not answer is refused too: the unit is not in update mode.
  After you have chosen once, the page finds the unit by itself.
- **The file.** It must carry the firmware ID of the display board, and it must not be larger than
  112,640 bytes, the largest size proven on a unit. The release file of 0.9.1 is 108,676 bytes and
  is sent in 54 blocks of 2 KB.
- **Every block.** The unit confirms each block. A block that is not confirmed within 2 seconds is
  sent again. After 4 resends in one update the page stops and tells you to start again.

The firmware does not say over USB which one it is: the original firmware and OpenGX Pro both
answer the version query with `:Rev201`. So neither the page nor Spooky2 can tell which firmware
is installed. Look at the splash, or at Settings > About.

## Update to a newer version

Do the same five steps with the newer version. An update writes the firmware only. Settings,
chains, plans and the positions in the Terrain protocols are in the SPI flash of the unit and are
not touched by it.

## Go back to the original firmware

The vendor's firmware is not part of this project and is never offered on the site. You supply
your own copy.

1. Take `GX_Pro_Firmware_v7789.exe` from Spooky2's download page and unpack the .zip.
2. Open "Restore original" on the site and choose that file, or the image inside it,
   `GxDisplayFW.bin`. The page takes the firmware out of the updater in the browser; nothing is
   uploaded. It shows the size and the SHA-256 and says when the file matches the original v7789.
3. Go on as for an install: update mode, port, install, restart.

On Windows the vendor's updater itself works as well: it uses the same update mode.

The data OpenGX Pro stored (settings, chains, plans, Terrain positions) stays in the SPI flash.
The original firmware does not use that area. If you install OpenGX Pro again later, it finds
them.

## From the command line

For those who build from source ([Build from source](./build.md)). The unit must be in update mode
first, as above.

| Command | What it flashes |
|---|---|
| `make release && make flash-release` | the release image, built on your computer |
| `.venv/bin/python tools/flash_gx_pro.py -f opengx-pro-0.9.1.bin` | a release file you downloaded |
| `make APP=nx flash` | your own build of OpenGX Pro |
| `make flash-original` | the original firmware, from your copy at `original/GxDisplayFW.bin` |

`make flash` without `APP=nx` builds and flashes the re-implementation of the original firmware,
which needs your copy of the vendor's image. The tool looks for the port that answers with the
right hardware ID, sends the same blocks as the page and refuses an image above 112,640 bytes.

## If it stops half way

An update can stop: the cable comes loose, the browser closes, the power fails.

- The update writes the application area only. The bootloader, which provides the update mode, is
  not written. So the update mode still works after a stopped update.
- Until you finish an update the firmware in the unit is incomplete, and the unit may show nothing
  when you switch it on.
- Switch the unit off, hold STOP, press POWER, and install again: OpenGX Pro, or the original with
  the vendor's file.

An interrupted update has not been tried on purpose. The points above follow from how the update
works; they are not the result of a test.

| The page says | What to do |
|---|---|
| "No answer." | Put the unit into update mode again: off, hold STOP, press POWER. If it is in update mode, choose the other port. |
| "That is the unit's other channel" | Click "Choose another port" and take a different entry. |
| "The unit did not accept the start of the update." | Switch the unit off, put it into update mode again and retry. |
| "Too many transfer errors." | The same. Try another USB cable or port of the computer if it happens again. |
