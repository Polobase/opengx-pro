/* ST7789 bus access (SPI2 8-bit, CS PA8, DC PC6) for drawing code: lcd_draw.c and later the nx
 * panel driver. lcd.c owns the controller initialisation. */
#pragma once

#include <stdint.h>

#include "board.h"

/* DEVIATION: bytes are streamed through the SPI FIFO (wait for TXE, bus idle only before CS goes
 * high). The original waits for the bus to be idle after every byte and toggles CS per pixel, so
 * the same screens take about twice as long there. The byte stream to the panel is identical. */
static inline void lcd_bus_put(uint8_t b)
{
    while (!(SPI2->SR & SPI_SR_TXE)) {
    }
    *(volatile uint8_t *)&SPI2->DR = b;
}

void lcd_bus_begin(int data);   /* wait until idle, DC = data, CS low */
void lcd_bus_end(void);         /* wait until idle, CS high */
/* CASET x0..x1, RASET y0..y1 (physical coordinates), then RAMWR */
void lcd_bus_window(uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1);
