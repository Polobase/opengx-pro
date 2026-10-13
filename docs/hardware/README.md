# GX Pro (GenX Pro) – hardware

Board photos plus a low-fidelity schematic drawn from them and from the display
firmware (`docs/spec/hardware.md`, `docs/spec/protocols.md`).

**All IC markings are removed** (laser/sanded). Parts are identified from footprint, silkscreen,
and, on the display board, from what the firmware does with each pin.

## Schematic

| Sheet | Content |
|---|---|
| [schematic/sheet1_system.svg](schematic/sheet1_system.svg) | the four boards and how they connect |
| [schematic/sheet2_display_board.svg](schematic/sheet2_display_board.svg) | display board, pin level (STM32G070 pin map verified from firmware) |
| [schematic/sheet3_main_board.svg](schematic/sheet3_main_board.svg) | main board blocks (power, USB, MCU, FPGA, generators A/B), GxExt, amp board |

The sheets are generated: edit `schematic/build_schematic.py`, then run
`python3 schematic/build_schematic.py`.
Solid lines mean the connection was seen on the board or verified in firmware. Dashed lines are
inferred. Tags: `[FW]` from the firmware RE, `[PCB]` from silkscreen or footprint, `[I]` inferred,
`?` unknown.

## Photos

The photos are kept locally in `photos/` (not in git).

| File | Shows |
|---|---|
| `photos/display_board_rear.jpg` | display board component side (MCU, SPI flash, crystal, LDO, connectors, LCD flex) |
| `photos/main_board_assembled.jpg` | main board in the case with GxExt and amp board fitted |
| `photos/main_board_top.jpg` | main board with both daughterboards removed (best overall view) |
| `photos/main_board_bottom.jpg` | main board underside (P/N label 2622232388) |
| `photos/main_board_closeup_power_usb.jpg` | power section, USB, generator A (readable designators) |
| `photos/main_board_closeup_fpga_genB.jpg` | U201, MCU, generator B, rail LEDs |
| `photos/gxext_board_top.jpg` / `photos/gxext_board_bottom.jpg` | GxExt daughterboard (silk `GxExt26090599`) |
| `photos/amp_board_top.jpg` / `photos/amp_board_bottom.jpg` | output amplifier board (heatsink side / header side) |

## What was identified

| Board | Part | Evidence | Confidence |
|---|---|---|---|
| display | STM32G070RBT6, LQFP-64 | 16 pins/side counted; firmware | high |
| display | 8.000 MHz crystal (HC-49/S) | marking readable; firmware HSE_VALUE | high |
| display | SPI NOR flash, SOIC-8, 25-series, ≥512 KB | firmware commands and addresses | high (part number unknown) |
| display | 240×320 LCD, ST7789-type, 18-pin FPC | firmware init sequence | high (FPC pinout unknown) |
| display | J1 3.3 V LDO (SOT-223), T2 backlight switch (SOT-23) | footprint, firmware PB12 PWM | medium |
| display | 8 tactile switches | 4-pad THT groups, 8 key inputs in firmware | high |
| main | U201 BGA ≈17×17 mm = FPGA | "Done" LED silk, SO-8 config flash, 2 buttons | medium |
| main | MCU, LQFP-48 | footprint, own crystal, 5xx designators | part unknown |
| main | USB-UART bridge (QFN-24) + ESD (D602) | USB-B next to it | medium |
| main | two resistor banks next to U201 (≈48 × 0402 each) | layout, one per generator | function inferred (R-2R DAC?) |
| main | generator A (3xx) / B (4xx), each ≈7–9 op-amp stages | designator ranges, clamp diodes D301/D302, D401/D402 at the jacks | medium |
| main | 4 empty SO-8 PowerPAD footprints | R343–R350, R443–R450 | high (unpopulated) |
| amp board | 4 power op-amps under heatsinks | 2 × 2×5 headers, one amp per output | part unknown |
| GxExt | 2 mirrored analog sections + TSSOP-20 | layout | function unknown |

## Open questions (what would raise the fidelity)

1. The pin order of the 2-, 4- and 6-pin cables between display and main board: a continuity
   check against the MCU pins on sheet 2 would settle it.
2. The main-board MCU and FPGA part numbers. The ball count of U201 and an SWD ID read of the
   MCU would identify them.
3. Which SMPS makes which rail, and the DC input voltage.
4. What the GxExt board does and which signals its 2×8 headers carry.
5. The pin order of the 18-pin LCD FPC.
