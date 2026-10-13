/* nx engine gallery sheets (nxg_sheets.h). Layouts are 320 x 240 with 12 px margins. */
#include "nxg_sheets.h"

#include <string.h>

#include "nx_anim.h"
#include "nx_font.h"
#include "nx_hero.h"
#include "nx_render.h"
#include "nx_scope.h"
#include "nx_time.h"
#include "nx_trans.h"
#include "nxg_core.h"

static void advance_to(uint32_t t)
{
    while (g_nxg_ms < t) {
        g_nxg_ms++;
        nxg_settle();
    }
}

static void begin(uint8_t theme, nx_draw_fn fn, void *ctx)
{
    nx_render_init();
    nx_render_theme(theme);
    nx_render_layer(NX_LAYER_SCREEN, fn, ctx);
}

static uint16_t tok(nx_gc *gc, uint8_t t)
{
    return nx_col(gc, t);
}

static void caption(nx_gc *gc, int16_t x, int16_t y, const char *s)
{
    nx_text(gc, &nx_font_cap13, x, y, s, tok(gc, NX_PAL_TEXT3));
}

/* ---- primitives ---- */

static int32_t wave_y(void *ctx, int16_t i)
{
    (void)ctx;
    return (int32_t)(218 * 16) - ((nx_sin1024((uint32_t)i * 14u) * 14) >> 10);   /* 14 px amplitude, Q4 */
}

static void draw_primitives(nx_gc *gc, void *ctx)
{
    (void)ctx;
    caption(gc, 12, 2, "primitives");
    nx_rrect(gc, (nx_rect){ 12, 22, 92, 62 }, 12, tok(gc, NX_PAL_SURFACE1));
    nx_rrect(gc, (nx_rect){ 100, 22, 160, 62 }, 8, tok(gc, NX_PAL_SURFACE2));
    nx_rrect_line(gc, (nx_rect){ 168, 22, 228, 62 }, 12, 2, tok(gc, NX_PAL_OUTLINE));
    nx_rrect_line(gc, (nx_rect){ 172, 26, 224, 58 }, 8, 1, tok(gc, NX_PAL_FOCUS));
    nx_pill(gc, (nx_rect){ 236, 22, 308, 44 }, tok(gc, NX_PAL_G1));
    nx_text(gc, &nx_font_cap13, 262, 24, "G1", tok(gc, NX_PAL_ON_ACCENT));
    nx_pill(gc, (nx_rect){ 236, 48, 272, 62 }, tok(gc, NX_PAL_G2));
    nx_pill(gc, (nx_rect){ 278, 49, 308, 61 }, tok(gc, NX_PAL_SUCCESS));
    nx_bar(gc, (nx_rect){ 12, 70, 308, 76 }, 16384u, tok(gc, NX_PAL_G1), tok(gc, NX_PAL_TRACK));
    nx_bar(gc, (nx_rect){ 12, 80, 308, 86 }, 32768u + 74u, tok(gc, NX_PAL_G2), tok(gc, NX_PAL_TRACK));
    nx_bar(gc, (nx_rect){ 12, 90, 308, 96 }, 65470u, tok(gc, NX_PAL_SUCCESS), tok(gc, NX_PAL_TRACK));
    static const uint16_t k_dur[6] = { 3, 1, 2, 2, 5, 1 };
    nx_segbar(gc, (nx_rect){ 12, 102, 308, 110 }, k_dur, 6, 2, 26000u, tok(gc, NX_PAL_G1), tok(gc, NX_PAL_G1),
              tok(gc, NX_PAL_TRACK));
    nx_ring(gc, 50, 152, 34, 6, 0, 1024, 0, tok(gc, NX_PAL_TRACK));
    nx_ring(gc, 50, 152, 34, 6, 0, 700, 1, tok(gc, NX_PAL_G1));
    nx_ring(gc, 132, 152, 34, 4, 128, 896, 0, tok(gc, NX_PAL_G2));
    nx_ring(gc, 132, 152, 20, 2, 0, 1024, 0, tok(gc, NX_PAL_TEXT2));
    nx_disc(gc, 196 * 16 + 8, 130 * 16 + 8, 3 * 16 + 8, tok(gc, NX_PAL_WARNING));
    nx_disc(gc, 214 * 16 + 5, 131 * 16 + 3, 6 * 16 + 4, tok(gc, NX_PAL_ERROR));
    nx_disc(gc, 236 * 16, 131 * 16, 9 * 16, tok(gc, NX_PAL_SUCCESS));
    nx_text(gc, &nx_font_body16, 186, 150, "Aa 0123 \x88 \x8D\x8E", tok(gc, NX_PAL_TEXT1));
    nx_text_fit(gc, &nx_font_body16, 186, 170, 120, "Ellipsis of a long line", tok(gc, NX_PAL_TEXT2));
    nx_vgrad(gc, (nx_rect){ 12, 196, 120, 236 }, tok(gc, NX_PAL_BG_TOP), tok(gc, NX_PAL_SURFACE2));
    nx_trace(gc, 128, 116, wave_y, 0, 32, tok(gc, NX_PAL_G2), 6);
    nx_trace(gc, 128, 116, wave_y, 0, 16, tok(gc, NX_PAL_G2), 32);
    nx_icon(gc, &nx_icons20, NX_I20_PLAY, 250, 206, tok(gc, NX_PAL_G1));
    nx_icon(gc, &nx_icons20, NX_I20_PAUSE, 272, 206, tok(gc, NX_PAL_G2));
    nx_icon(gc, &nx_icons20, NX_I20_WARNING, 294, 206, tok(gc, NX_PAL_WARNING));
}

/* ---- fonts: every glyph ---- */

static int16_t glyphs(nx_gc *gc, const nx_font_t *f, int16_t y)
{
    int16_t x = 12;
    unsigned dense = f->first <= f->last ? (unsigned)(f->last - f->first) + 1u : 0u;
    for (unsigned i = 0; i < dense + f->n_extra; i++) {
        uint8_t code = i < dense ? (uint8_t)(f->first + i) : f->extra[i - dense];
        const nx_glyph_t *g = nx_font_glyph(f, code);
        if (x + g->advance > 308) {
            x = 12;
            y = (int16_t)(y + f->height + 2);
        }
        char s[2] = { (char)code, 0 };
        nx_text(gc, f, x, y, s, tok(gc, NX_PAL_TEXT1));
        x = (int16_t)(x + g->advance + 3);
    }
    return (int16_t)(y + f->height + 4);
}

static void draw_fonts_text(nx_gc *gc, void *ctx)
{
    (void)ctx;
    caption(gc, 12, 2, "F_CAP13 (Inter Medium 13)");
    int16_t y = glyphs(gc, &nx_font_cap13, 20);
    caption(gc, 12, y, "F_BODY16 (Inter Regular 16)");
    glyphs(gc, &nx_font_body16, (int16_t)(y + 18));
}

static void draw_fonts_num(nx_gc *gc, void *ctx)
{
    (void)ctx;
    caption(gc, 12, 2, "F_NUM28 (Inter Display Medium 28)");
    int16_t y = glyphs(gc, &nx_font_num28, 20);
    caption(gc, 12, y, "F_NUM48 (Inter Display SemiBold 48)");
    glyphs(gc, &nx_font_num48, (int16_t)(y + 18));
}

/* ---- icons ---- */

static void draw_icons(nx_gc *gc, void *ctx)
{
    (void)ctx;
    static const uint8_t k_tint[4] = { NX_PAL_TEXT1, NX_PAL_G1, NX_PAL_G2, NX_PAL_TEXT2 };
    for (unsigned i = 0; i < NX_I20_COUNT; i++)
        nx_icon(gc, &nx_icons20, (uint8_t)i, (int16_t)(12 + (i % 12u) * 25u), (int16_t)(4 + (i / 12u) * 24u),
                tok(gc, k_tint[(i / 12u) % 4u]));
    for (unsigned i = 0; i < NX_I32_COUNT; i++)
        nx_icon(gc, &nx_icons32, (uint8_t)i, (int16_t)(12 + i * 37u), 152, tok(gc, NX_PAL_TEXT1));
}

/* ---- palettes with WCAG contrast labels (integer maths: host and device agree exactly) ---- */

static const char *const k_tok_name[] = {
    "bg", "bg_top", "surface1", "surface2", "outline", "focus", "track", "grid",
    "text1", "text2", "text3", "text_off", "ghost", "on_accent",
    "g1", "g1_dim", "g2", "g2_dim",
    "success", "success_dim", "warning", "warning_dim", "error", "error_dim",
};
_Static_assert(sizeof k_tok_name / sizeof k_tok_name[0] == NX_PAL_COUNT, "one name per palette token");

/* sRGB channel -> linear light, Q16, for the 5- and 6-bit channels expanded by bit replication
 * (tools/nxassets/palette.py expand565) */
static const uint16_t k_lin5[32] = { 0, 159, 340, 599, 997, 1453, 2013, 2681, 3570, 4488, 5530, 6700, 8177,
                                     9635, 11235, 12980, 15122, 17187, 19407, 21787, 24658, 27386, 30282,
                                     33350, 37008, 40449, 44069, 47871, 52369, 56567, 60955, 65535 };
static const uint16_t k_lin6[64] = { 0, 80, 159, 241, 340, 458, 599, 761, 947, 1156, 1391, 1651, 1937, 2250,
                                     2592, 2961, 3464, 3900, 4366, 4864, 5392, 5953, 6547, 7174, 7834, 8528,
                                     9258, 10022, 10822, 11658, 12530, 13440, 14629, 15623, 16656, 17727, 18837,
                                     19987, 21177, 22407, 23678, 24990, 26344, 27739, 29176, 30656, 32179,
                                     33745, 35764, 37429, 39138, 40891, 42690, 44534, 46423, 48359, 50341,
                                     52369, 54445, 56567, 58737, 60955, 63221, 65535 };

static uint32_t luminance(uint16_t c)   /* Q16 */
{
    return (13933u * k_lin5[c >> 11] + 46871u * k_lin6[(c >> 5) & 63u] + 4732u * k_lin5[c & 31u]) >> 16;
}

/* contrast ratio x 10 */
static unsigned contrast10(uint16_t a, uint16_t b)
{
    uint32_t la = luminance(a) + 3277u, lb = luminance(b) + 3277u;   /* + 0.05 */
    return la > lb ? (unsigned)((la * 10u + lb / 2u) / lb) : (unsigned)((lb * 10u + la / 2u) / la);
}

static void draw_palette(nx_gc *gc, void *ctx)
{
    (void)ctx;
    const uint16_t *pal = gc->pal;
    for (unsigned i = 0; i < NX_PAL_COUNT; i++) {
        int16_t x = (int16_t)(12 + (i % 3u) * 100u), y = (int16_t)(6 + (i / 3u) * 29u);
        nx_rrect(gc, (nx_rect){ x, y, (int16_t)(x + 26), (int16_t)(y + 26) }, 6, pal[i]);
        nx_rrect_line(gc, (nx_rect){ x, y, (int16_t)(x + 26), (int16_t)(y + 26) }, 6, 1, pal[NX_PAL_OUTLINE]);
        nx_text_fit(gc, &nx_font_cap13, (int16_t)(x + 30), (int16_t)(y - 2), 68, k_tok_name[i], pal[NX_PAL_TEXT1]);
        unsigned c = contrast10(pal[i], pal[NX_PAL_BG]);
        char s[12], *p = s;
        if (c >= 100u)
            *p++ = (char)('0' + c / 100u);
        *p++ = (char)('0' + c / 10u % 10u);
        *p++ = '.';
        *p++ = (char)('0' + c % 10u);
        memcpy(p, ":1", 3);
        nx_text(gc, &nx_font_cap13, (int16_t)(x + 30), (int16_t)(y + 11), s, pal[NX_PAL_TEXT2]);
    }
}

/* ---- hero frequency fixtures (docs/nx/design/ui.md §2.8) ---- */

struct hero_sheet {
    uint64_t v[3];
    const char *label[3];
};

static void draw_hero(nx_gc *gc, void *ctx)
{
    const struct hero_sheet *hs = ctx;
    for (unsigned i = 0; i < 3u; i++) {
        int16_t y = (int16_t)(6 + i * 78u);
        caption(gc, 12, y, hs->label[i]);
        nx_hero h;
        nx_hero_layout(hs->v[i], &h);
        nx_hero_draw(gc, &h, 12, (int16_t)(y + 22), tok(gc, NX_PAL_TEXT1), tok(gc, NX_PAL_TEXT3), tok(gc, NX_PAL_TEXT2));
    }
}

/* ---- scope variants ---- */

static const int8_t k_custom[64] = { 0,   40,  80,  110, 127, 110, 80,  40,  0,   -20, -30, -20, 0,  60, 120, 60,
                                     0,   -60, -120, -60, 0,  20,  30,  20,  0,   -40, -80, -110, -127, -110, -80, -40,
                                     0,   10,  20,  30,  40,  50,  60,  70,  80,  90,  100, 110,  120,  127,  0,   -127,
                                     -120, -100, -80, -60, -40, -20, 0,  20,  40,  20,  0,   -20, -40, -20, 0,   0 };

static void draw_scopes(nx_gc *gc, void *ctx)
{
    (void)ctx;
    static const nx_scope_cfg k_cfg[8] = {
        { NX_WAVE_SINE, NX_WAVE_SINE, NX_OUT2_SYNC, NX_SCOPE_RUNNING, 0, NX_PAL_G1, 0, 0, 256, 2000, 0, 0 },
        { NX_WAVE_SQUARE, NX_WAVE_SQUARE, NX_OUT2_INVERSE, NX_SCOPE_RUNNING, 0, NX_PAL_G2, 0, 0, 256, 1000, 0, 0 },
        { NX_WAVE_SAW, NX_WAVE_SINE, NX_OUT2_INDEPENDENT, NX_SCOPE_RUNNING, 0, NX_PAL_G1, 0, 0, 384, 500, 0, 0 },
        { NX_WAVE_TRIANGLE, NX_WAVE_TRIANGLE, NX_OUT2_OFF, NX_SCOPE_RUNNING, 50, NX_PAL_G2, 0, 0, 256, 2000, 0, 0 },
        { NX_WAVE_DAMPED_SINE, NX_WAVE_SINE, NX_OUT2_SYNC, NX_SCOPE_PAUSED, 0, NX_PAL_G1, 0, 0, 256, 1200, 0, 0 },
        { NX_WAVE_CUSTOM, NX_WAVE_SINE, NX_OUT2_OFF, NX_SCOPE_RUNNING, 0, NX_PAL_G2, 1, 41, 256, 0, k_custom, 0 },
        { NX_WAVE_HBOMB_EDGE, NX_WAVE_INV_SAW, NX_OUT2_INDEPENDENT, NX_SCOPE_RUNNING, 25, NX_PAL_G1, 0, 0, 3200,
          2000, 0, 0 },
        { NX_WAVE_SINE, NX_WAVE_SINE, NX_OUT2_SYNC, NX_SCOPE_STOPPED, 0, NX_PAL_G2, 0, 0, 256, 2000, 0, 0 },
    };
    for (unsigned i = 0; i < 8u; i++) {
        int16_t x = (int16_t)(12 + (i % 2u) * 160u), y = (int16_t)(6 + (i / 2u) * 58u);
        nx_scope_draw(gc, (nx_rect){ x, y, (int16_t)(x + 136), (int16_t)(y + 52) }, &k_cfg[i],
                      (uint16_t)(i * 8192u));
    }
}

/* ---- screens for the transitions ---- */

static void draw_screen_a(nx_gc *gc, void *ctx)
{
    (void)ctx;
    nx_fill(gc, (nx_rect){ 0, 0, 320, 22 }, tok(gc, NX_PAL_SURFACE1));
    nx_pill(gc, (nx_rect){ 12, 2, 48, 20 }, tok(gc, NX_PAL_G1));
    nx_text(gc, &nx_font_cap13, 21, 2, "G1", tok(gc, NX_PAL_ON_ACCENT));
    nx_text(gc, &nx_font_cap13, 56, 2, nx_str(NX_S_STATE_RUNNING), tok(gc, NX_PAL_G1));
    nx_text(gc, &nx_font_body16, 12, 30, "Lyme Borreliosis", tok(gc, NX_PAL_TEXT1));
    nx_hero h;
    nx_hero_layout(7278u, &h);
    nx_hero_draw(gc, &h, 12, 62, tok(gc, NX_PAL_TEXT1), tok(gc, NX_PAL_TEXT3), tok(gc, NX_PAL_TEXT2));
    nx_bar(gc, (nx_rect){ 12, 176, 308, 182 }, 40000u, tok(gc, NX_PAL_G1), tok(gc, NX_PAL_TRACK));
}

static void draw_screen_b(nx_gc *gc, void *ctx)
{
    (void)ctx;
    nx_fill(gc, (nx_rect){ 0, 0, 320, 22 }, tok(gc, NX_PAL_SURFACE1));
    nx_pill(gc, (nx_rect){ 12, 2, 48, 20 }, tok(gc, NX_PAL_G2));
    nx_text(gc, &nx_font_cap13, 21, 2, "G2", tok(gc, NX_PAL_ON_ACCENT));
    nx_text(gc, &nx_font_cap13, 56, 2, nx_str(NX_S_STATE_PAUSED), tok(gc, NX_PAL_TEXT2));
    nx_text(gc, &nx_font_body16, 12, 30, "Detox Liver", tok(gc, NX_PAL_TEXT1));
    nx_hero h;
    nx_hero_layout(123456786u, &h);
    nx_hero_draw(gc, &h, 12, 62, tok(gc, NX_PAL_TEXT2), tok(gc, NX_PAL_TEXT3), tok(gc, NX_PAL_TEXT2));
    nx_bar(gc, (nx_rect){ 12, 176, 308, 182 }, 12000u, tok(gc, NX_PAL_G2), tok(gc, NX_PAL_TRACK));
}

static void draw_sheet_layer(nx_gc *gc, void *ctx)
{
    (void)ctx;
    static const uint8_t k_ic[4] = { NX_I20_PLAY, NX_I20_TUNE, NX_I20_BRIGHTNESS, NX_I20_LOCK };
    nx_rrect(gc, (nx_rect){ 0, 120, 320, 252 }, 12, tok(gc, NX_PAL_SURFACE1));
    for (unsigned i = 0; i < 4u; i++) {
        int16_t x = (int16_t)(16 + i * 76u);
        nx_rrect(gc, (nx_rect){ x, 136, (int16_t)(x + 64), 188 }, 8,
                 tok(gc, i == 0 ? NX_PAL_SURFACE2 : NX_PAL_SURFACE1));
        nx_icon(gc, &nx_icons20, k_ic[i], (int16_t)(x + 22), 146, tok(gc, i == 0 ? NX_PAL_G1 : NX_PAL_TEXT2));
    }
    nx_rrect_line(gc, (nx_rect){ 14, 134, 82, 190 }, 10, 2, tok(gc, NX_PAL_G1));
}

static void draw_toast(nx_gc *gc, void *ctx)
{
    (void)ctx;
    nx_rrect(gc, (nx_rect){ 12, 6, 308, 42 }, 10, tok(gc, NX_PAL_SURFACE2));
    nx_icon(gc, &nx_icons20, NX_I20_CHECK_CIRCLE, 22, 14, tok(gc, NX_PAL_SUCCESS));
    nx_text(gc, &nx_font_body16, 50, 14, nx_str(NX_S_STATE_FINISHED), tok(gc, NX_PAL_TEXT1));
}

static void draw_brand(nx_gc *gc, void *ctx)   /* the splash's ring and name (nx_scr_sys.c) */
{
    (void)ctx;
    nx_ring(gc, 160, 92, 36, 6, 0, 1024, 0, tok(gc, NX_PAL_G1));
    const char *t = "OpenGX Pro";
    nx_text(gc, &nx_font_body16, (int16_t)(160 - (int16_t)nx_font_text_width(&nx_font_body16, t) / 2), 140, t,
            tok(gc, NX_PAL_TEXT1));
}

static nx_roll s_roll[2];

static void draw_roll(nx_gc *gc, void *ctx)
{
    (void)ctx;
    caption(gc, 12, 6, "odometer roll: 160 ms, 20 ms stagger, 400 ms tint");
    nx_roll_draw(gc, &s_roll[0], &nx_font_num48, 12, 30, nx_ms(), tok(gc, NX_PAL_TEXT1), tok(gc, NX_PAL_G1));
    nx_roll_draw(gc, &s_roll[1], &nx_font_num28, 12, 110, nx_ms(), tok(gc, NX_PAL_TEXT1), tok(gc, NX_PAL_G2));
}

static void name_ms(char *out, const char *prefix, uint32_t ms)
{
    size_t n = strlen(prefix);
    memcpy(out, prefix, n);
    out[n] = (char)('0' + ms / 100u % 10u);
    out[n + 1] = (char)('0' + ms / 10u % 10u);
    out[n + 2] = (char)('0' + ms % 10u);
    out[n + 3] = 0;
}

static void transitions(void)
{
    char name[32];
    static const uint32_t k_push_at[3] = { 60, 120, 180 };
    /* push left: G1 -> G2 */
    begin(NX_THEME_DARK, draw_screen_a, 0);
    g_nxg_ms = 10000u;
    nxg_settle();
    nx_render_layer(NX_LAYER_SCREEN, draw_screen_b, 0);
    nx_trans_push(NX_PUSH_LEFT);
    for (unsigned i = 0; i < 3u; i++) {
        advance_to(10000u + k_push_at[i]);
        name_ms(name, "push_left_", k_push_at[i]);
        nxg_take(name);
    }
    advance_to(10300u);
    nxg_take("push_left_end");
    /* push right: back to G1 */
    nx_render_layer(NX_LAYER_SCREEN, draw_screen_a, 0);
    nx_trans_push(NX_PUSH_RIGHT);
    advance_to(10420u);
    nxg_take("push_right_120");
    advance_to(10600u);

    /* sheet reveal over the scrimmed dashboard */
    nx_render_scrim(NX_SCRIM_LEVEL);
    nx_render_layer(NX_LAYER_SHEET, draw_sheet_layer, 0);
    nx_trans_reveal(NX_LAYER_SHEET, (nx_rect){ 0, 120, 320, 240 });
    nxg_settle();                                /* the base */
    uint32_t t0 = g_nxg_ms;
    static const uint32_t k_rev_at[4] = { 0, 60, 120, 180 };
    for (unsigned i = 0; i < 4u; i++) {
        advance_to(t0 + k_rev_at[i]);
        name_ms(name, "reveal_", k_rev_at[i]);
        nxg_take(name);
    }
    nx_render_layer(NX_LAYER_SHEET, 0, 0);
    nx_render_scrim(0);
    nx_inval_all();
    advance_to(t0 + 400u);

    /* toast sliding in from above */
    nx_render_layer(NX_LAYER_TOAST, draw_toast, 0);
    t0 = g_nxg_ms;
    nx_trans_slide(NX_LAYER_TOAST, (nx_rect){ 12, 6, 308, 42 }, -40, 0, 200);
    static const uint32_t k_toast_at[4] = { 0, 60, 120, 200 };
    for (unsigned i = 0; i < 4u; i++) {
        advance_to(t0 + k_toast_at[i]);
        name_ms(name, "toast_", k_toast_at[i]);
        nxg_take(name);
    }
    advance_to(t0 + 300u);

    /* splash wordmark fading in (6 steps) */
    begin(NX_THEME_DARK, 0, 0);
    nx_render_layer(NX_LAYER_OVERLAY, draw_brand, 0);
    nxg_settle();
    t0 = g_nxg_ms;
    nx_trans_fade(NX_LAYER_OVERLAY, (nx_rect){ 0, 56, 320, 170 }, 1);
    static const uint32_t k_fade_at[3] = { 50, 150, 300 };
    for (unsigned i = 0; i < 3u; i++) {
        advance_to(t0 + k_fade_at[i]);
        name_ms(name, "fade_", k_fade_at[i]);
        nxg_take(name);
    }

    /* odometer roll */
    begin(NX_THEME_DARK, draw_roll, 0);
    nx_roll_init(&s_roll[0], "727.000");
    nx_roll_init(&s_roll[1], "0199");
    nxg_settle();
    t0 = g_nxg_ms = 20000u;
    nx_roll_set(&s_roll[0], "728.000", t0);
    nx_roll_set(&s_roll[1], "0200", t0);
    static const uint32_t k_roll_at[6] = { 0, 40, 80, 120, 160, 400 };
    for (unsigned i = 0; i < 6u; i++) {
        advance_to(t0 + k_roll_at[i]);
        nx_inval((nx_rect){ 0, 30, 320, 140 });
        name_ms(name, "roll_", k_roll_at[i]);
        nxg_take(name);
    }
}

unsigned nxg_run(void)
{
    static const char *const k_theme[4] = { "dark", "light", "hc", "night" };
    char name[32];
    g_nxg_shots = 0;
    g_nxg_ms = 0;
    nx_anim_set_reduced(0);
    for (uint8_t t = 0; t < NX_THEME_COUNT; t++) {
        begin(t, draw_primitives, 0);
        memcpy(name, "primitives_", 11);
        strcpy(name + 11, k_theme[t]);
        nxg_take(name);
    }
    begin(NX_THEME_DARK, draw_fonts_text, 0);
    nxg_take("fonts_text");
    begin(NX_THEME_DARK, draw_fonts_num, 0);
    nxg_take("fonts_num");
    begin(NX_THEME_DARK, draw_icons, 0);
    nxg_take("icons");
    for (uint8_t t = 0; t < NX_THEME_COUNT; t++) {
        begin(t, draw_palette, 0);
        memcpy(name, "palette_", 8);
        strcpy(name + 8, k_theme[t]);
        nxg_take(name);
    }
    static const struct hero_sheet k_hero_a = { { 7278u, 3750007u, 123456786u },
                                                { "727 Hz", "37 500 Hz", "123 456.78 Hz" } };
    static const struct hero_sheet k_hero_b = { { 12345678126u, 10u, 10000000009u },
                                                { "12 345 678.12 Hz", "0.00000001 Hz", "10^10 Hz: clamped" } };
    begin(NX_THEME_DARK, draw_hero, (void *)&k_hero_a);
    nxg_take("hero_a");
    begin(NX_THEME_DARK, draw_hero, (void *)&k_hero_b);
    nxg_take("hero_b");
    begin(NX_THEME_DARK, draw_scopes, 0);
    nxg_take("scopes");
    transitions();
    nxg_screens_run();   /* the UI's screens (src/nx/nxg_screens.c) */
    return g_nxg_shots;
}
