/* Independent watchdog (docs/nx/design/core.md §5): started by software after the LCD init (the
 * option bytes are never touched), 2 s timeout. Fed by the main loop, the SPI-flash busy polls
 * (SPIFLASH_POLL_HOOK), the UART transmit waits, the power-off goodbye wait and the power-off
 * signal itself (CFG_IWDG in board.c), so an "off but powered" unit never reboots. NX_IWDG_ENABLE
 * (nx_config.h, a Drop-0 value) can switch it off. The host tests implement nx_wdg_feed() in
 * tests/host/nx/nx_world.c. */
#pragma once

#define NX_IWDG_PR 4u      /* LSI 32 kHz / 64 = 500 Hz */
#define NX_IWDG_RLR 999u   /* 1000 counts = 2.0 s (LSI tolerance: 1.9-2.1 s) */

void nx_wdg_start(void);
void nx_wdg_feed(void);
