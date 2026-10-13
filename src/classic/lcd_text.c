/* Text in the vendor font font_ui24 (extracted at build time from the user's original image),
 * for the classic apps (bring-up, probe, link test). */
#include "lcd.h"

#include "assets.h"

/* font_ui24: records {code, width, 48 bytes}; physical 24 px wide, `width` rows. */
static const uint8_t *find_glyph(char ch)
{
    const uint8_t *rec = asset_font_ui24;
    for (uint32_t i = 0; i < ASSET_FONT_UI24_RECORDS; i++, rec += 50) {
        if (rec[0] == (uint8_t)ch)
            return rec;
    }
    return 0;
}

uint16_t lcd_draw_char_ui24(char ch, uint16_t x, uint16_t y, uint16_t fg, uint16_t bg)
{
    const uint8_t *g = find_glyph(ch);
    if (!g)
        return 0;
    uint16_t w = g[1];
    lcd_blit1(x, x + 23u, y - w + 1u, y, g + 2, 3u * w, fg, bg);
    return w;
}

uint16_t gfx_text(uint16_t X, uint16_t Y, const char *s, uint16_t fg, uint16_t bg)
{
    uint16_t y = (uint16_t)(LCD_H - 1u - X);
    while (*s) {
        uint16_t w = lcd_draw_char_ui24(*s++, Y, y, fg, bg);
        if (w > y)
            break;
        y -= w;
    }
    return (uint16_t)(LCD_H - 1u - y);
}

uint16_t gfx_text_width(const char *s)
{
    uint16_t w = 0;
    for (; *s; s++) {
        const uint8_t *g = find_glyph(*s);
        if (g)
            w += g[1];
    }
    return w;
}
