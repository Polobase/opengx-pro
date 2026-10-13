/* Drawing primitives of the original UI (docs/spec/display.md §6-7).
 *
 * The original draws everything through lcd_set_window + one lcd_write_data16 per pixel (CS
 * toggled for every pixel, corner rows pixel by pixel with lcd_draw_pixel). Only the GRAM
 * contents matter, so the rows here are streamed through one window each; every pixel still
 * gets the colour, and every overlap the drawing order, of the original. */
#include "gfx.h"

#include "assets.h"
#include "lcd.h"

#define FONT_REC 50u          /* {code, width, 48 B} */
#define GNUM_REC 45u          /* {code, 44 B} */
#define OUT_REC 133u          /* {code, 132 B} */
#define ICON_REC 307u         /* {code, 306 B} */

void gfx_pixel(uint16_t x, uint16_t y, uint16_t c)
{
    lcd_fill_rect(x, y, x, y, c);
}

void gfx_hline(uint16_t x, uint16_t y, uint16_t len, uint16_t c)
{
    if (len)   /* the original sets window x..x+len and streams len pixels */
        lcd_fill_rect(x, y, (uint16_t)(x + len - 1u), y, c);
}

void gfx_vline(uint16_t x, uint16_t y0, uint16_t y1, uint16_t c)
{
    lcd_fill_rect(x, y0, x, y1, c);   /* nothing when y1 < y0, like the original */
}

/* Line-by-line port of lcd_fill_circle 0x08009240 (32-bit arithmetic, coordinates masked to 16
 * bits). The corner test uses the current, already decremented radius. */
void gfx_fill_circle(uint16_t cx, uint16_t cy, uint16_t radius, uint16_t c)
{
    uint32_t r = radius;
    uint32_t n = (r * 707u) / 1000u + 1u;
    uint32_t lim = r * r + (r >> 1);
    gfx_hline((uint16_t)(cx - r), cy, (uint16_t)((r & 0x7FFFu) << 1), c);
    for (uint32_t i = 1; i <= n; i++) {
        if (lim < i * i + r * r) {
            if (n < r) {
                uint16_t x = (uint16_t)(cx - i + 1u);
                uint16_t len = (uint16_t)(((i - 1u) & 0x7FFFu) << 1);
                gfx_hline(x, (uint16_t)(cy + r), len, c);
                gfx_hline(x, (uint16_t)(cy - r), len, c);
            }
            r--;
        }
        uint16_t x = (uint16_t)(cx - r);
        uint16_t len = (uint16_t)((r & 0x7FFFu) << 1);
        gfx_hline(x, (uint16_t)(cy + i), len, c);
        gfx_hline(x, (uint16_t)(cy - i), len, c);
    }
}

/* Corner rows k = 0..7 from the edge: pixels outside the corner (i < outer) and, for the
 * bordered box, border pixels up to inner (rows 0 and 1 are all border). */
static const uint8_t k_outer[8] = { 8, 6, 4, 3, 2, 2, 1, 1 };
static const uint8_t k_inner[8] = { 0, 0, 6, 5, 4, 4, 3, 3 };

static void half_round_row(uint16_t x, uint16_t y, uint16_t w, uint16_t c, uint8_t side, int32_t inset)
{
    if (!w)
        return;
    lcd_stream_begin(x, (uint16_t)(x + w - 1u), y, y);
    for (int32_t i = 0; i < (int32_t)w; i++) {
        int corner = side ? i >= (int32_t)w - inset : i < inset;
        lcd_stream_pixel(corner ? COLOR_BLACK : c);
    }
    lcd_stream_end();
}

void gfx_fill_half_round_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t c, uint8_t side)
{
    for (unsigned k = 0; k < 8u; k++)
        half_round_row(x, (uint16_t)(y + k), w, c, side, k_outer[k]);
    uint16_t y0 = (uint16_t)(y + 8u), y1 = (uint16_t)(y + h - 8u);
    gfx_vline(x, y0, y1, c);
    gfx_vline((uint16_t)(x + 1u), y0, y1, c);
    gfx_vline((uint16_t)(x + w - 1u), y0, y1, c);
    gfx_vline((uint16_t)(x + w - 2u), y0, y1, c);
    lcd_fill_rect((uint16_t)(x + 2u), y0, (uint16_t)(x + w - 3u), y1, c);
    for (unsigned k = 0; k < 8u; k++)
        half_round_row(x, (uint16_t)(y + h - k), w, c, side, k_outer[k]);
}

static void round_row(uint16_t x, uint16_t y, uint16_t w, int32_t outer, int32_t inner_left, int32_t inner_right,
                      uint16_t bg, uint16_t border, uint16_t fill)
{
    if (!w)
        return;
    int32_t n = (int32_t)w;
    lcd_stream_begin(x, (uint16_t)(x + w - 1u), y, y);
    for (int32_t i = 0; i < n; i++) {
        uint16_t col;
        if (i < outer || i >= n - outer)
            col = bg;
        else if (i < inner_left || i >= n - inner_right)
            col = border;
        else
            col = fill;
        lcd_stream_pixel(col);
    }
    lcd_stream_end();
}

void gfx_draw_round_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t bg, uint16_t border,
                         uint16_t fill)
{
    for (unsigned k = 0; k < 8u; k++) {
        int32_t inner = k_inner[k] ? k_inner[k] : (int32_t)w;
        round_row(x, (uint16_t)(y + k), w, k_outer[k], inner, inner, bg, border, fill);
    }
    uint16_t y0 = (uint16_t)(y + 8u), y1 = (uint16_t)(y + h - 8u);
    gfx_vline(x, y0, y1, border);
    gfx_vline((uint16_t)(x + 1u), y0, y1, border);
    gfx_vline((uint16_t)(x + w - 1u), y0, y1, border);
    gfx_vline((uint16_t)(x + w - 2u), y0, y1, border);
    lcd_fill_rect((uint16_t)(x + 2u), y0, (uint16_t)(x + w - 3u), y1, fill);
    for (unsigned k = 0; k < 8u; k++) {
        int32_t inner = k_inner[k] ? k_inner[k] : (int32_t)w;
        /* Original quirk (kept, 1 px, harmless): on row y+h-7 the left border is only 2 px, so
         * (x+2, y+h-7) gets the fill colour. */
        int32_t inner_left = k == 7u ? 2 : inner;
        round_row(x, (uint16_t)(y + h - k), w, k_outer[k], inner_left, inner, bg, border, fill);
    }
}

static const uint8_t *find_record(const uint8_t *table, uint32_t records, uint32_t size, uint8_t code)
{
    for (uint32_t i = 0; i < records; i++, table += size) {
        if (table[0] == code)
            return table;
    }
    return 0;
}

const uint8_t *gfx_glyph_ui24(uint8_t code)
{
    return find_record(asset_font_ui24, ASSET_FONT_UI24_RECORDS, FONT_REC, code);
}

static uint16_t draw_glyph(const uint8_t *g, uint16_t x, uint16_t y, uint16_t bg, uint16_t fg)
{
    if (!g)
        return 0;
    uint16_t w = g[1];
    lcd_blit1(x, (uint16_t)(x + 23u), (uint16_t)(y - w + 1u), y, g + 2, 3u * w, fg, bg);
    return w;
}

uint16_t gfx_char_ui24(uint8_t ch, uint16_t x, uint16_t y, uint16_t bg, uint16_t fg)
{
    return draw_glyph(gfx_glyph_ui24(ch), x, y, bg, fg);
}

uint16_t gfx_char_freq24(uint8_t ch, uint16_t x, uint16_t y, uint16_t bg, uint16_t fg)
{
    return draw_glyph(find_record(asset_font_freq24, ASSET_FONT_FREQ24_RECORDS, FONT_REC, ch), x, y, bg, fg);
}

void gfx_string_ui24(const char *s, uint16_t x, uint16_t y, uint16_t bg, uint16_t fg)
{
    for (; *s; s++)
        y = (uint16_t)(y - gfx_char_ui24((uint8_t)*s, x, y, bg, fg));
}

uint16_t gfx_uint5_ui24(uint16_t v, uint16_t x, uint16_t y, uint16_t bg, uint16_t fg)
{
    static const uint16_t k_div[5] = { 10000, 1000, 100, 10, 1 };
    uint32_t rest = v;
    for (unsigned i = 0; i < 5u; i++) {
        uint32_t d = rest / k_div[i];
        rest %= k_div[i];
        y = (uint16_t)(y - gfx_char_ui24((uint8_t)('0' + d), x, y, bg, fg));
    }
    return y;
}

void gfx_frequency(uint64_t v, uint16_t x, uint16_t y, uint16_t bg, uint16_t fg)
{
    uint64_t q = v / 10u;                    /* mantissa, times 10^exponent (wraps like the original) */
    for (unsigned e = (unsigned)(v % 10u); e; e--)
        q *= 10u;
    char buf[16];
    for (int i = 15; i >= 0; i--) {          /* q mod 10^16 as 8 integer + 8 fraction digits */
        buf[i] = (char)('0' + (unsigned)(q % 10u));
        q /= 10u;
    }

    int started = 0;
    unsigned skipped = 0;
    for (unsigned i = 0; i < 8u; i++) {
        if (buf[i] == '0' && !started && i != 7u) {
            if (i & 1u) {                    /* odd leading zero: no slot, a blank goes to the end */
                skipped++;
                continue;
            }
            gfx_char_freq24('0', x, y, bg, bg);   /* even leading zero: blank 11 px slot */
        } else {
            gfx_char_freq24((uint8_t)buf[i], x, y, bg, fg);
            started = 1;
        }
        y = (uint16_t)(y - 11u);
    }
    gfx_char_freq24('.', x, y, bg, fg);
    y = (uint16_t)(y - 6u);
    for (unsigned i = 8; i < 16u; i++) {
        gfx_char_freq24((uint8_t)buf[i], x, y, bg, fg);
        y = (uint16_t)(y - 11u);
    }
    gfx_char_freq24(' ', x, y, bg, fg);
    y = (uint16_t)(y - 3u);
    gfx_char_freq24('H', x, y, bg, fg);
    y = (uint16_t)(y - 10u);
    gfx_char_freq24('z', x, y, bg, fg);
    y = (uint16_t)(y - 8u);
    while (skipped--) {                      /* trailing blanks erase stale digits */
        gfx_char_freq24('0', x, y, bg, bg);
        y = (uint16_t)(y - 11u);
    }
}

void gfx_gnum_label(uint8_t code, uint16_t x, uint16_t y, uint16_t bg, uint16_t fg)
{
    const uint8_t *rec = find_record(asset_font_gnum_16x22, ASSET_FONT_GNUM_RECORDS, GNUM_REC, code);
    if (rec)   /* unknown code: the original only sets the window */
        lcd_blit1(x, (uint16_t)(x + 15u), y, 0x13F, rec + 1, 44, fg, bg);
}

void gfx_out_label(uint8_t code, uint16_t x, uint16_t y, uint16_t bg, uint16_t fg)
{
    /* Original quirk (kept): the record index is looked up in the G1/G2 table. Both tables hold
     * '1', '2' in the same order, so the result is the same for every code the UI uses. */
    const uint8_t *g = find_record(asset_font_gnum_16x22, ASSET_FONT_GNUM_RECORDS, GNUM_REC, code);
    if (!g)
        return;   /* DEVIATION: the original has no range check and would blit 132 bytes of the QR code. */
    uint32_t idx = (uint32_t)(g - asset_font_gnum_16x22) / GNUM_REC;
    lcd_blit1(x, (uint16_t)(x + 23u), y, 0x13F, asset_font_out_24x44 + idx * OUT_REC + 1u, 132, fg, bg);
}

void gfx_status_icon(uint8_t code, uint16_t x, uint16_t y, uint16_t bg, uint16_t fg)
{
    const uint8_t *rec = find_record(asset_icons_status_48x51, ASSET_ICONS_STATUS_RECORDS, ICON_REC, code);
    if (rec)
        lcd_blit1(x, (uint16_t)(x + 47u), y, 0x118, rec + 1, 306, fg, bg);
}

void gfx_usb_icon(uint16_t x, uint16_t y, uint16_t c0, uint16_t c1)
{
    lcd_blit1(x, (uint16_t)(x + 15u), y, 0x13F, asset_bmp_usb_icon, sizeof asset_bmp_usb_icon, c1, c0);
}

void gfx_sync_icon(uint16_t x, uint16_t y, uint16_t c0, uint16_t c1)
{
    lcd_blit1(x, (uint16_t)(x + 15u), y, 0x13F, asset_bmp_sync_icon, sizeof asset_bmp_sync_icon, c1, c0);
}

static void stream_byte(uint8_t b, uint16_t bg, uint16_t fg)
{
    for (uint8_t mask = 0x80; mask; mask >>= 1)
        lcd_stream_pixel((b & mask) ? fg : bg);
}

#define MARQUEE_COLS 230u
#define MARQUEE_BYTES (3u * MARQUEE_COLS)   /* 690 */

void gfx_marquee_draw(uint16_t x, uint16_t y, uint16_t bg, uint16_t fg, const uint8_t *bmp, uint16_t size,
                      uint16_t width, uint16_t pos)
{
    lcd_stream_begin(x, (uint16_t)(x + 23u), (uint16_t)(y - (MARQUEE_COLS - 1u)), y);
    if (width <= MARQUEE_COLS) {
        /* padding first: the window starts at the logical right end, so the text is left-aligned */
        uint32_t pad = 3u * (MARQUEE_COLS - width);
        for (uint32_t i = 0; i < pad; i++)
            stream_byte(0, bg, fg);
        for (uint32_t i = pad; i < MARQUEE_BYTES; i++)
            stream_byte(bmp[i - pad], bg, fg);
    } else if (pos < 100u) {
        const uint8_t *p = bmp + 3u * (width - MARQUEE_COLS);   /* beginning of the text, static */
        for (uint32_t i = 0; i < MARQUEE_BYTES; i++)
            stream_byte(p[i], bg, fg);
    } else {
        /* circular scroll with a 30 px gap: row j shows column (off + j) mod (W + 30) */
        int32_t period = (int32_t)width + 30;
        int32_t off = (2 * (int32_t)width - (int32_t)pos - 100) % period;
        if (off < 0)   /* only for offsets the TIM3 logic never produces (the original would read before the buffer) */
            off += period;
        uint32_t n = 3u * (uint32_t)period, idx = 3u * (uint32_t)off;
        for (uint32_t i = 0; i < MARQUEE_BYTES; i++) {
            /* DEVIATION: columns past the end of the buffer read as blank. The original's gap reads up
             * to 90 bytes past the 1998-byte buffer (into the protocol buffer) for names wider than
             * 636 px; for all narrower names this is the same zero gap. */
            stream_byte(idx < size ? bmp[idx] : 0, bg, fg);
            if (++idx == n)
                idx = 0;
        }
    }
    lcd_stream_end();
}
