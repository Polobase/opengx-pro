"""Emulated GX Pro display board for running and comparing display firmwares.

Modules:
  machine   CPU (Unicorn Cortex-M0), memory map, NVIC/SCB/SysTick, time base, run loop
  periph    STM32G070 peripherals: RCC, FLASH, PWR/SYSCFG (plain registers), GPIO, SPI, TIM, USART
  devices   board devices: ST7789 panel, W25Q16 SPI NOR, generator boards on USART1/USART2
  firmware  loaders: our ELF builds and the original image (reference mode)
  scenario  scenario files (JSON timelines) and the runner that produces checkpoints and logs

Entry points: tools/emu.py (single run), tools/scenario.py (run scenarios),
tools/diff.py (reference vs. candidate).
"""

from .machine import HCLK, Board, BoardConfig, Inputs  # noqa: F401
from .firmware import load_elf, load_reference  # noqa: F401
