/* Polled drawing on the ST7789 with the original's semantics (docs/spec/display.md), plus the
 * logical-coordinate fill used by the test apps. No vendor assets. */
#include "lcd.h"

#include "lcd_bus.h"

static void stream_color(uint16_t color, uint32_t count)
{
    uint8_t hi = (uint8_t)(color >> 8), lo = (uint8_t)color;
    lcd_bus_begin(1);
    while (count--) {
        lcd_bus_put(hi);
        lcd_bus_put(lo);
    }
    lcd_bus_end();
}

void lcd_fill_rect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
{
    lcd_bus_window(x0, x1, y0, y1);
    if (x1 >= x0 && y1 >= y0)
        stream_color(color, (uint32_t)(x1 - x0 + 1u) * (uint32_t)(y1 - y0 + 1u));
}

void lcd_stream_begin(uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1)
{
    lcd_bus_window(x0, x1, y0, y1);
    lcd_bus_begin(1);
}

void lcd_stream_pixel(uint16_t color)
{
    lcd_bus_put((uint8_t)(color >> 8));
    lcd_bus_put((uint8_t)color);
}

void lcd_stream_end(void)
{
    lcd_bus_end();
}

void lcd_blit1(uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1, const uint8_t *bits, uint32_t len,
               uint16_t fg, uint16_t bg)
{
    lcd_bus_window(x0, x1, y0, y1);
    lcd_bus_begin(1);
    for (uint32_t i = 0; i < len; i++) {
        uint8_t b = bits[i];
        for (uint8_t mask = 0x80; mask; mask >>= 1) {
            uint16_t c = (b & mask) ? fg : bg;
            lcd_bus_put((uint8_t)(c >> 8));
            lcd_bus_put((uint8_t)c);
        }
    }
    lcd_bus_end();
}

void gfx_fill(uint16_t X0, uint16_t Y0, uint16_t X1, uint16_t Y1, uint16_t color)
{
    lcd_fill_rect_landscape(X0, Y0, X1, Y1, color);
}
