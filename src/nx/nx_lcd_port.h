/* The bus below nx_lcd.c: SPI2 and DMA1 channel 1 on the device (nx_lcd_port.c), the ST7789 model
 * on the host (tests/host/nx/nx_panel.c). nx_lcd.c calls these with the DMA interrupt masked
 * (nx_lcd_port_lock) or from the interrupt itself. */
#pragma once

#include <stdint.h>

/* After lcd_init(): MADCTL NX_LCD_MADCTL (one parameter byte, sent in 8-bit mode), then SPI2 in
 * 16-bit frames if NX_LCD_DS16, LCD CS held low from here on, DMA channel and its interrupt. */
void nx_lcd_port_init(void);
/* One command (DC low) with n 16-bit parameters (DC high, each high byte first). Waits until the
 * bus is idle first, returns when the last frame is in the SPI FIFO (bounded waits). */
void nx_lcd_port_cmd(uint8_t cmd, const uint16_t *args, unsigned n);
/* n pixels (1..65535) from px, incrementing (inc) or the same halfword n times, DC high. DMA:
 * starts the transfer and returns; its end calls nx_lcd_irq(). Polled: returns when all pixels
 * are in the SPI FIFO. In the 8-bit mode the halfwords must be byte-swapped (packed writes send
 * the low byte first). */
void nx_lcd_port_pixels(const uint16_t *px, uint32_t n, int inc);
int nx_lcd_port_busy(void);   /* a DMA transfer is running */
void nx_lcd_port_lock(void);  /* mask the DMA interrupt (main context) */
void nx_lcd_port_unlock(void);
