/* Screens of the original UI (docs/spec/application.md §5.2, §8, §9.1; docs/spec/display.md §7.3, §9),
 * in the original's drawing order: ui_render 0x080065D8 with its tail ui_render_incremental
 * 0x0800773E, the panel frames, the program list, the name marquee, splash, goodbye screen and
 * the calibration grid. All coordinates are physical (see gfx.h). */
#include "ui.h"

#include <string.h>

#include "assets.h"
#include "gfx.h"
#include "lcd.h"
#include "state.h"

#define BLACK  COLOR_BLACK
#define WHITE  COLOR_WHITE
#define TEAL   COLOR_BLUE       /* 0x2B12 */
#define GREY   COLOR_GREY
#define RED    COLOR_RED
#define YELLOW COLOR_YELLOW
#define NAVY   COLOR_DARKBLUE   /* 0x0016 */

#define NAME_Y      0x110u      /* channel title / marquee field (G1 x 0x14, G2 x 0x82) */
#define LIST_NAME_Y 0x109u

static const char k_choose[] = "Choose Your Treatment Program";   /* 0x080069EC */
static const char k_program_g1[] = "Program  G1";                  /* 0x08006A10 */
static const char k_program_g2[] = "Program  G2";                  /* 0x08006A1C */

/* ------------------------------------------------------------------ boot and goodbye screens */

void ui_draw_boot_splash(void)
{
    lcd_blit1(0x1E, 0x8D, 0x78, 0x13F, asset_bmp_boot_ghost_logo, sizeof asset_bmp_boot_ghost_logo, WHITE, BLACK);
    lcd_blit1(0x1E, 0x35, 0x83, 0x13F, asset_bmp_red2_small, sizeof asset_bmp_red2_small, RED, BLACK);
    lcd_blit1(0xAD, 0xC4, 0x32, 0x13F, asset_bmp_boot_welcome, sizeof asset_bmp_boot_welcome, WHITE, BLACK);
    lcd_blit1(0xAE, 0xC5, 0x1E, 0x13F, asset_bmp_red2_large, sizeof asset_bmp_red2_large, RED, BLACK);
    lcd_blit1(200, 0xD7, 0x3C, 0x13F, asset_bmp_boot_tagline, sizeof asset_bmp_boot_tagline, WHITE, BLACK);
}

void ui_draw_shutdown_screen(void)
{
    /* the four u16 images: 1-bits black ink, 0-bits white */
    lcd_blit1(0x46, 0x9D, 0x6E, 0x13F, asset_bmp_bye_qrcode, sizeof asset_bmp_bye_qrcode, BLACK, WHITE);
    lcd_blit1(0x1F, 0x3E, 0x86, 0x13F, asset_bmp_bye_logo, sizeof asset_bmp_bye_logo, BLACK, WHITE);
    lcd_blit1(0x1F, 0x36, 0x7D, 0x13F, asset_bmp_red2_small, sizeof asset_bmp_red2_small, RED, BLACK);
    lcd_blit1(0xA5, 0xBC, 0x20, 0x13F, asset_bmp_bye_text, sizeof asset_bmp_bye_text, BLACK, WHITE);
    lcd_blit1(0xBE, 0xCD, 0x53, 0x13F, asset_bmp_bye_url, sizeof asset_bmp_bye_url, BLACK, WHITE);
}

/* ------------------------------------------------------------------ name marquee */

void ui_name_render_bitmap(unsigned slot)
{
    uint8_t *p = g_name_bmp;
    uint16_t width = 0;
    if (slot < NAME_SLOTS) {   /* DEVIATION: the original has no slot check */
        const uint8_t *name = (const uint8_t *)g_names[slot];
        /* Characters 58..0 (59 is never shown), NUL and unknown codes skipped, stored last
         * character first so column 0 is the logical right end of the text.
         * DEVIATION: the original has no length check and overflows the 1998-byte buffer (into the
         * protocol buffer) for names wider than 666 px; here only the leading characters that fit
         * are rendered. Names up to 666 px are rendered exactly as by the original. */
        unsigned end = 0;
        uint32_t bytes = 0;
        for (; end < NAME_LEN - 1u; end++) {
            const uint8_t *g = name[end] ? gfx_glyph_ui24(name[end]) : 0;
            if (!g)
                continue;
            if (bytes + 3u * g[1] > sizeof g_name_bmp)
                break;
            bytes += 3u * g[1];
        }
        while (end--) {
            const uint8_t *g = name[end] ? gfx_glyph_ui24(name[end]) : 0;
            if (!g)
                continue;
            memcpy(p, g + 2, 3u * g[1]);
            p += 3u * g[1];
            width = (uint16_t)(width + g[1]);
        }
    }
    memset(p, 0, (size_t)(g_name_bmp + sizeof g_name_bmp - p));
    /* DEVIATION (race fix): the width is stored once at the end. The original zeroes it first and
     * accumulates, so the TIM3 marquee logic could see a partial width and wrap early. */
    g_name_bmp_width = width;
}

static void marquee_draw(uint16_t x, uint16_t y, uint16_t bg, uint16_t fg, uint16_t pos)
{
    gfx_marquee_draw(x, y, bg, fg, g_name_bmp, sizeof g_name_bmp, g_name_bmp_width, pos);
}

/* Clears UIF_MARQUEE like the original's full renders and restarts the scroll position.
 * DEVIATION: the original leaves the restart to the TIM3 logic (offset = 0 while the flag is
 * clear, once per 25.6 ms), which depends on how long the redraw takes: names drawn in the same
 * redraw may appear frozen mid-scroll, and the new marquee may skip its initial pause. Here every
 * redraw that restarts the marquee draws all names at offset 0 (what the original shows whenever
 * its redraw spans a TIM3 marquee tick). The TIM3 logic writes the same 0 while the flag is clear. */
static uint16_t marquee_restart(void)
{
    flags_clear(&g_ui_flags, UIF_MARQUEE);
    g_marquee_offset = 0;
    return 0;
}

/* ------------------------------------------------------------------ panels */

static void panels_idle(void)   /* 0x080056B8, state 1 */
{
    gfx_fill_half_round_rect(0x14, 0x14, 0x19, 0x118, TEAL, 0);
    gfx_fill_half_round_rect(0x2D, 0x14, 0x46, 0x118, GREY, 1);
    gfx_fill_half_round_rect(0x82, 0x14, 0x19, 0x118, TEAL, 0);
    gfx_fill_half_round_rect(0x9B, 0x14, 0x46, 0x118, GREY, 1);
    gfx_fill_circle(0x20, 0x126, 0x0F, WHITE);
    gfx_fill_circle(0x8E, 0x126, 0x0F, WHITE);
    gfx_gnum_label('1', 0x18, 0x11C, WHITE, NAVY);
    gfx_gnum_label('2', 0x87, 0x11C, WHITE, NAVY);
}

static void panels_g1_active(void)   /* 0x08005A28, state 3 */
{
    gfx_draw_round_rect(0x14, 0x14, 0x5F, 0x118, BLACK, TEAL, WHITE);
    gfx_fill_half_round_rect(0x14, 0x14, 0x19, 0x118, TEAL, 0);
    gfx_fill_circle(0x20, 0x126, 0x11, TEAL);
    gfx_fill_circle(0x20, 0x126, 0x0F, WHITE);
    gfx_gnum_label('1', 0x18, 0x11C, WHITE, NAVY);
    gfx_fill_half_round_rect(0x82, 0x14, 0x19, 0x118, TEAL, 0);
    gfx_fill_half_round_rect(0x9B, 0x14, 0x46, 0x118, GREY, 1);
    gfx_fill_circle(0x8E, 0x126, 0x0F, WHITE);
    gfx_gnum_label('2', 0x87, 0x11C, WHITE, NAVY);
}

static void panels_g2_active(void)   /* 0x0800596C, state 4 (labels 1 px off, as in the original) */
{
    gfx_fill_half_round_rect(0x14, 0x14, 0x19, 0x118, TEAL, 0);
    gfx_fill_half_round_rect(0x2D, 0x14, 0x46, 0x118, GREY, 1);
    gfx_fill_circle(0x20, 0x126, 0x0F, WHITE);
    gfx_gnum_label('1', 0x18, 0x11B, WHITE, NAVY);
    gfx_draw_round_rect(0x82, 0x14, 0x5F, 0x118, BLACK, TEAL, WHITE);
    gfx_fill_half_round_rect(0x82, 0x14, 0x19, 0x118, TEAL, 0);
    gfx_fill_circle(0x8E, 0x126, 0x11, TEAL);
    gfx_fill_circle(0x8E, 0x126, 0x0F, WHITE);
    gfx_gnum_label('2', 0x86, 0x11B, WHITE, NAVY);
}

static void panels_both_active(void)   /* 0x0800B498, state 0x10 */
{
    gfx_draw_round_rect(0x14, 0x14, 0x5F, 0x118, BLACK, TEAL, WHITE);
    gfx_fill_half_round_rect(0x14, 0x14, 0x19, 0x118, TEAL, 0);
    gfx_fill_circle(0x20, 0x126, 0x11, TEAL);
    gfx_fill_circle(0x20, 0x126, 0x0F, WHITE);
    gfx_gnum_label('1', 0x18, 0x11C, WHITE, NAVY);
    gfx_draw_round_rect(0x82, 0x14, 0x5F, 0x118, BLACK, TEAL, WHITE);
    gfx_fill_half_round_rect(0x82, 0x14, 0x19, 0x118, TEAL, 0);
    gfx_fill_circle(0x8E, 0x126, 0x11, TEAL);
    gfx_fill_circle(0x8E, 0x126, 0x0F, WHITE);
    gfx_gnum_label('2', 0x86, 0x11B, WHITE, NAVY);
}

/* Title bar of a channel: program name (marquee field) or "Choose Your Treatment Program".
 * `scrolls`: this is the name the marquee scrolls (sets UIF_MARQUEE before drawing). */
static void channel_title(unsigned ch, uint16_t pos, int scrolls)
{
    uint16_t x = ch ? 0x82u : 0x14u;
    uint8_t slot = g_ch[ch].prog_slot;
    if (!slot) {
        gfx_string_ui24(k_choose, x, NAME_Y, TEAL, WHITE);
        return;
    }
    ui_name_render_bitmap(slot);
    if (scrolls)
        flags_set(&g_ui_flags, UIF_MARQUEE);
    marquee_draw(x, NAME_Y, TEAL, WHITE, pos);
}

/* Status icon and frequency of a loaded channel; active = the channel whose screen is shown. */
static void channel_status(unsigned ch, int active)
{
    const struct channel *c = &g_ch[ch];
    if (c->prog_slot == 0 || c->prog_slot == 31u)
        return;
    uint16_t bg = active ? WHITE : GREY, fg = active ? TEAL : WHITE;
    uint64_t freq = 0;
    uint8_t icon;
    if (g_ui_flags & UIF_RUN(ch)) {
        icon = 'r';
        freq = c->step_freq;
    } else {
        uint32_t elapsed = c->elapsed;
        icon = elapsed == 0 ? 'c' : elapsed == 1u ? 's' : 'p';
    }
    gfx_status_icon(icon, ch ? 0xA5u : 0x37u, 0x1E, bg, fg);
    gfx_frequency(freq, ch ? 0xAFu : 0x41u, 0x122, bg, fg);
}

static void loop_badges(void)
{
    gfx_sync_icon(0x19, 0x19, (g_ui_flags & UIF_LOOP_G1) ? YELLOW : TEAL, TEAL);
    gfx_sync_icon(0x87, 0x19, (g_ui_flags & UIF_LOOP_G2) ? YELLOW : TEAL, TEAL);
}

/* States 3 and 4; returns the marquee offset used. */
static uint16_t render_channel(unsigned active, uint16_t pos)
{
    unsigned other = active ^ 1u;
    if (g_ui_prev_state != (active ? ST_CH2 : ST_CH1)) {
        pos = marquee_restart();
        lcd_fill_rect(0x73, 0x14, 0x82, 0x13B, BLACK);
        if (active)
            panels_g2_active();
        else
            panels_g1_active();
        /* DEVIATION: the original writes PB12 (backlight) after this frame and after the HOME and PC
         * frames; TIM3 overwrites it within 100 us, so these writes are left out. */
        channel_title(other, pos, 0);
        channel_title(active, pos, 1);
    }
    channel_status(other, 0);
    channel_status(active, 1);
    loop_badges();
    return pos;
}

/* ------------------------------------------------------------------ program list (state 2) */

static void list_row_bar(uint8_t row, uint16_t c, int same)
{
    switch (row) {
    case 1:
        /* Original quirk (kept): h = 0x50 when new == old (never happens from ui_render). */
        gfx_fill_half_round_rect(0x28, 0x14, 0x19, same ? 0x50 : 0x118, c, 0);
        break;
    case 2: case 3: case 4: case 5: case 6:
        lcd_fill_rect((uint16_t)(0x28u + 25u * (row - 1u)), 0x14, (uint16_t)(0x28u + 25u * row - 1u), 300, c);
        break;
    case 7:
        gfx_fill_half_round_rect(0xBE, 0x14, 0x19, 0x118, c, 1);
        break;
    default:
        break;
    }
}

static void list_highlight(uint8_t new_row, uint8_t old_row)   /* 0x08005760 */
{
    if (new_row == old_row) {
        list_row_bar(new_row, TEAL, 1);
        return;
    }
    list_row_bar(new_row, TEAL, 0);
    list_row_bar(old_row, WHITE, 0);
}

static void program_list(uint16_t pos)   /* 0x0800620C */
{
    for (unsigned i = 0; i < 7u; i++) {
        unsigned n = g_list_top + i;
        int sel = (int)g_list_sel - (int)g_list_top == (int)i;
        uint16_t bg = sel ? TEAL : WHITE, fg = sel ? WHITE : BLACK;
        uint16_t x = (uint8_t)(0x28u + 25u * i);
        gfx_char_ui24((uint8_t)('0' + n / 10u), x, 0x122, bg, fg);
        gfx_char_ui24((uint8_t)('0' + n % 10u), x, 0x119, bg, fg);
        gfx_char_ui24('.', x, 0x110, bg, fg);
        ui_name_render_bitmap(n);
        marquee_draw(x, LIST_NAME_Y, bg, fg, pos);
    }
}

static uint16_t render_list(void)   /* returns the marquee offset used */
{
    uint16_t pos = marquee_restart();
    if (g_ui_prev_state != ST_LIST) {
        lcd_fill_rect(10, 0x14, 0x27, 0x138, BLACK);
        lcd_fill_rect(0x28, 0x119, 0xA0, 0x13B, BLACK);
        lcd_fill_rect(0xD7, 0x14, 0xE6, 300, BLACK);
        gfx_draw_round_rect(0x28, 0x14, 0xAF, 0x118, BLACK, WHITE, WHITE);
        /* DEVIATION: the original switches PB12 (backlight) off/on around this; TIM3 overwrites it
         * within 100 us. */
    }
    gfx_string_ui24((g_ui_flags & UIF_LIST_G2) ? k_program_g2 : k_program_g1, 0x0F, 200, BLACK, WHITE);
    uint8_t row = (uint8_t)(g_list_sel - g_list_top + 1u);
    list_highlight(row, (g_ui_flags & UIF_LIST_UP) ? (uint8_t)(row + 1u) : (uint8_t)(row - 1u));
    flags_set(&g_ui_flags, UIF_MARQUEE);
    program_list(pos);
    return pos;
}

/* ------------------------------------------------------------------ PC mode (state 0x10) */

static const uint8_t k_pc_freq_x[2][2] = { { 0x3A, 0x55 }, { 0xA8, 0xC3 } };

static unsigned pc_scrolling_channel(void)
{
    return (g_ui_flags & UIF_PC_NAME_G2) ? 1u : 0u;
}

static void pc_name(unsigned ch, uint16_t pos)
{
    ui_name_render_bitmap(ch ? 31u : 0u);
    marquee_draw(ch ? 0x82u : 0x14u, NAME_Y, TEAL, WHITE, pos);
}

static uint16_t render_pc(void)   /* returns the marquee offset used */
{
    uint16_t pos = marquee_restart();
    lcd_fill_rect(0x73, 0x14, 0x82, 0x13C, BLACK);
    gfx_usb_icon(4, 5, WHITE, BLACK);   /* PC-link icon, always white in this firmware */
    panels_both_active();
    unsigned scrolling = pc_scrolling_channel();
    pc_name(scrolling ^ 1u, pos);
    flags_set(&g_ui_flags, UIF_MARQUEE);
    pc_name(scrolling, pos);
    for (unsigned ch = 0; ch < 2u; ch++) {
        gfx_out_label('1', k_pc_freq_x[ch][0], 0xFA, WHITE, TEAL);
        gfx_out_label('2', k_pc_freq_x[ch][1], 0xFA, WHITE, TEAL);
    }
    /* The full render shows each output's own value in grey, also in sync mode. */
    for (unsigned ch = 0; ch < 2u; ch++) {
        for (unsigned out = 0; out < 2u; out++) {
            /* DEVIATION (race fix): the up-to-date bit is set before the value is read, so an update
             * from the UART interrupt during the drawing is not lost (the original sets it after). */
            flags_set(&g_pc_flags, (uint16_t)PCF_FREQ(ch, out));
            gfx_frequency(g_ch[ch].pc_freq[out], k_pc_freq_x[ch][out], 0xF5, WHITE, GREY);
        }
    }
    return pos;
}

static void pc_incremental(uint16_t pos)
{
    for (unsigned ch = 0; ch < 2u; ch++) {
        for (unsigned out = 0; out < 2u; out++) {
            if (g_pc_flags & PCF_FREQ(ch, out))
                continue;
            flags_set(&g_pc_flags, (uint16_t)PCF_FREQ(ch, out));
            uint64_t f = (out && (g_ui_flags & UIF_SYNC(ch))) ? g_ch[ch].pc_freq[0] : g_ch[ch].pc_freq[out];
            uint16_t fg = (g_misc_flags & MISC_OUT_EN(ch, out)) ? TEAL : GREY;
            gfx_frequency(f, k_pc_freq_x[ch][out], 0xF5, WHITE, fg);
        }
    }
    unsigned scrolling = pc_scrolling_channel(), last = scrolling;
    for (unsigned ch = 0; ch < 2u; ch++) {
        if (g_pc_flags & PCF_NAME(ch))
            continue;
        flags_set(&g_pc_flags, (uint16_t)PCF_NAME(ch));
        /* DEVIATION: the name that does not scroll is drawn static (offset 0); the original draws it
         * at the other name's current scroll offset, i.e. frozen mid-scroll. */
        pc_name(ch, ch == scrolling ? pos : 0);
        last = ch;
    }
    /* DEVIATION: the scrolling name is left in the bitmap buffer (the TIM3 wrap test reads its
     * width); the original leaves slot 31's there until the next marquee step. */
    if (last != scrolling)
        ui_name_render_bitmap(scrolling ? 31u : 0u);
}

/* ------------------------------------------------------------------ calibration grid (state 0xFF) */

static const uint16_t k_cal_pos[29][2] = {
    { 0x46, 0xB2 },
    { 0x6E, 0x12A }, { 0x87, 0x12A }, { 0xA0, 0x12A }, { 0xB9, 0x12A }, { 0xD2, 0x12A },
    { 0x6E, 0xDA }, { 0x87, 0xDA }, { 0xA0, 0xDA }, { 0xB9, 0xDA }, { 0xD2, 0xDA },
    { 0x6E, 0x8A }, { 0x87, 0x8A }, { 0xA0, 0x8A }, { 0xB9, 0x8A }, { 0xD2, 0x8A },
    { 0x6E, 0x3A }, { 0x87, 0x3A }, { 0xA0, 0x3A }, { 0xB9, 0x3A }, { 0xD2, 0x3A },
    { 0x0A, 0x12A }, { 0x23, 0x12A }, { 0x0A, 0xDA }, { 0x23, 0xDA },
    { 0x0A, 0x8A }, { 0x23, 0x8A }, { 0x0A, 0x3A }, { 0x23, 0x3A },
};

static void cal_value(unsigned idx, int inverted)
{
    gfx_uint5_ui24((uint16_t)g_cal_table[idx], k_cal_pos[idx][0], k_cal_pos[idx][1], inverted ? WHITE : BLACK,
                   inverted ? BLACK : WHITE);
}

void ui_cal_draw_values(void)   /* 0x0800DED4 */
{
    for (unsigned i = 0; i < 29u; i++)
        cal_value(i, 0);
}

void ui_cal_draw_value(unsigned idx)
{
    /* As the original's USART3 'e'..'l' commands: values 21..24 are drawn inverted, 25..28 normal. */
    if (idx < 29u)
        cal_value(idx, idx >= 21u && idx <= 24u);
}

static void render_cal(void)   /* switch at 0x08006F56: neighbours normal, selected value inverted */
{
    unsigned i = g_cal_index;
    if (i > 28u)
        return;
    if (i == 0) {
        cal_value(28, 0);
        cal_value(20, 0);   /* also redrawn by the original (unchanged pixels) */
    } else {
        cal_value(i - 1u, 0);
    }
    cal_value(i, 1);
    if (i == 28u) {
        cal_value(0, 0);
    } else {
        cal_value(i + 1u, 0);
        if (i == 20u)
            cal_value(0, 0);   /* also redrawn by the original (unchanged pixels) */
    }
}

/* ------------------------------------------------------------------ ui_render */

void ui_render(void)
{
    /* DEVIATION (race fix): one snapshot of the TIM3-driven offset per render, used for drawing and
     * stored as the last drawn offset; the original reads it several times, so a step that happens
     * during the render could be recorded as drawn without being drawn. */
    uint16_t pos = g_marquee_offset;

    if (g_ui_flags & UIF_REDRAW) {
        switch (g_ui_state) {
        case ST_HOME:
            lcd_fill_rect(0x73, 0x14, 0x82, 0xB4, BLACK);
            gfx_usb_icon(4, 5, BLACK, BLACK);   /* erase the PC-link icon */
            panels_idle();
            gfx_string_ui24(k_choose, 0x14, NAME_Y, TEAL, WHITE);
            gfx_string_ui24(k_choose, 0x82, NAME_Y, TEAL, WHITE);
            break;
        case ST_LIST:
            pos = render_list();
            break;
        case ST_CH1:
        case ST_CH2:
            pos = render_channel(g_ui_state == ST_CH2, pos);
            break;
        case ST_PC:
            pos = render_pc();
            break;
        case ST_CAL:
            render_cal();
            break;
        default:
            break;
        }
    }

    /* ui_render_incremental 0x0800773E (also the tail of every full render) */
    if ((g_ui_flags & UIF_MARQUEE) && pos != g_marquee_prev_offset) {
        switch (g_ui_state) {
        case ST_LIST:
            ui_name_render_bitmap(g_list_sel);
            marquee_draw((uint8_t)((g_list_sel - g_list_top) * 25u + 40u), LIST_NAME_Y, TEAL, WHITE, pos);
            break;
        case ST_CH1:
            marquee_draw(0x14, NAME_Y, TEAL, WHITE, pos);
            break;
        case ST_CH2:
            marquee_draw(0x82, NAME_Y, TEAL, WHITE, pos);
            break;
        case ST_PC:
            pc_name(pc_scrolling_channel(), pos);
            break;
        default:
            break;
        }
    }
    if (g_ui_state == ST_PC)
        pc_incremental(pos);
    g_marquee_prev_offset = pos;
    g_ui_prev_state = g_ui_state;
    flags_clear(&g_ui_flags, UIF_REDRAW);
}
