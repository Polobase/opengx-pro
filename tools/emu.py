#!/usr/bin/env python3
"""Run a firmware on the emulated GX Pro display board and render what the LCD shows.

The board model is tools/sim/ (see `emu.py -h`): NVIC with real interrupt entry/return,
TIM3/SysTick/USART interrupts, a full W25Q16 SPI NOR, the ST7789 panel, timed UART transmit, and
'reference' as target for the original firmware (your copy, original/GxDisplayFW.bin).

Examples:
  emu.py build/fw/gxpro.elf --until 4000 --shot 1500:splash.png --shot 3900:screen.png
  emu.py build/fw/gxpro.elf --key 4000-5000:POWER --until 12000 --shot 11000:off.png
  emu.py build/fw/gxpro.elf --allow-tx --rx '3000:U1:#ping\\r\\n' --until 4000 --tx-log tx.txt
  emu.py reference --allow-tx --generators 1 --spi-image backup --until 7000 --shot 6500:home.png
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from sim.emu_cli import main  # noqa: E402

if __name__ == "__main__":
    sys.exit(main())
