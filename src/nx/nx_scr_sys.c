/* System screens (docs/nx/design/ui.md W1, W22, §2.13, §5.6): the splash (the wordmark "OpenGX
 * Pro" whose "O" is a ring with a sine in it, the firmware line and the version; any key skips
 * it), the key guide (after the first boot's splash, and from Settings > Keys & lock), the goodbye
 * with the session time and the night view. No bitmaps in the repository: the ring and the sine
 * are drawn, the letters come from the brand font (assets/nx/fonts.json F_BRAND). A build with the
 * user's own original image (NX_VENDOR_SPLASH, nx_vendor.h) shows the original's Spooky2 ghost
 * splash instead, fading in. */
#include "nx_anim.h"
#include "nx_bl.h"
#include "nx_comp.h"
#include "nx_config.h"
#include "nx_runner.h"
#include "nx_uifmt.h"
#if NX_VENDOR_SPLASH
#include "nx_vendor.h"
#endif

/* ---- splash (full 1.2 s, short 0.5 s: the same timeline compressed) ---- */

#define SPLASH_FULL_MS 1200u
#define SPLASH_SHORT_MS 500u
#define SPLASH_HOLD_MS 300u   /* the finished picture stays a moment */
/* The ring and the lines under it (the name and the firmware line, "Goodbye" and the session)
 * stand as one block a little above the screen's centre, the same on the splash and the
 * goodbye. */
#define RING_CX 160
#define RING_CY 96
#define WORDS_Y 148
#define LINE2_Y (WORDS_Y + 24)
#define FOOT_Y 218            /* the splash's version and "any key: skip" */
static const nx_rect k_mark = { 116, RING_CY - 44, 204, RING_CY + 44 };
/* The splash is black with white and red ink in every theme, as the original's (decided on
 * 2026-09-25); the rest of the screen follows the theme. */
#define SPLASH_BG 0x0000u
#define SPLASH_INK 0xFFFFu
#define SPLASH_FOOT 0x8410u   /* mid grey */
static const nx_rect k_full = { 0, 0, NX_LCD_W, NX_LCD_H };

#if NX_DWELL_UNITS != 1000u   /* make NX_TIME_X=...: never to be mistaken for a normal build */
#define FW_LINE "v" NX_FW_VERSION " TEST: fast time"
#else
#define FW_LINE "v" NX_FW_VERSION
#endif

static void splash_foot(nx_gc *gc)
{
    nx_text(gc, &nx_font_cap13, NX_M, FOOT_Y, FW_LINE, SPLASH_FOOT);
    nx_text_r(gc, &nx_font_cap13, NX_LCD_W - NX_M, FOOT_Y, nx_str(NX_S_SPLASH_SKIP), SPLASH_FOOT);
}
#if !NX_VENDOR_SPLASH
/* The wordmark in the brand font, centred: its "O" is the ring (as tall as the capitals), the
 * letters "penGX Pro" follow; the line under it names the device. */
#define WM_TEXT "penGX Pro"
#define WM_Y 78               /* line top of the wordmark */
#define WM_R 15               /* the ring's outer radius */
#define WM_W 5                /* its stroke, as the letters' */
#define WM_GAP 4              /* ring to "p" */
#define WM_SINE_N 14          /* columns of the sine inside the ring */
#define WM_SINE_AMP 72        /* its amplitude, Q4 */
#define WM_LINE2_Y (WM_Y + 50)
static const nx_rect k_words = { 0, WM_Y - 4, NX_LCD_W, WM_LINE2_Y + 20 };
#endif

struct splash {
    uint32_t t0;
    uint16_t dur;
};

static uint32_t splash_t(const struct splash *s, uint32_t now)   /* on the 1.2 s timeline */
{
    return (now - s->t0) * SPLASH_FULL_MS / s->dur;
}

#if !NX_VENDOR_SPLASH
static int32_t sine_y(void *ctx, int16_t i)   /* one period across the ring's inside; ctx: its centre y */
{
    return (int32_t)(intptr_t)ctx * 16 - ((nx_sin1024((uint32_t)(i + 1) * 1024u / WM_SINE_N) * WM_SINE_AMP) >> 14);
}

/* The ring's centre and the letters' left edge: ring, gap and letters centred as one word. */
static void splash_layout(int16_t *cx, int16_t *cy, int16_t *text_x)
{
    int16_t tw = (int16_t)nx_font_text_width(&nx_font_brand, WM_TEXT);
    int16_t x0 = (int16_t)((NX_LCD_W - (2 * WM_R + WM_GAP + tw)) / 2);
    *cx = (int16_t)(x0 + WM_R);
    *cy = (int16_t)(WM_Y + nx_font_brand.baseline - WM_R + 1);
    *text_x = (int16_t)(x0 + 2 * WM_R + WM_GAP);
}
#endif

static void splash_enter(void *st, uint32_t arg)
{
    struct splash *s = st;
    s->t0 = nx_ms();
    s->dur = arg == NX_SPLASH_SHORT ? SPLASH_SHORT_MS : SPLASH_FULL_MS;
    nx_bl_fade_to(nx_bl_user(), 300);   /* from dark at power-on */
}

#if NX_VENDOR_SPLASH
static const nx_rect k_ghost = { 58, 30, 290, 216 };   /* the original's splash artwork */

static void splash_draw(nx_gc *gc, const void *st)
{
    const struct splash *s = st;
    uint32_t t = splash_t(s, nx_ui_now());
    unsigned f = t >= 600u ? 32u : t * 32u / 600u;   /* fades in over 600 ms */
    uint16_t ink = nx_mix(SPLASH_BG, SPLASH_INK, f), red = nx_mix(SPLASH_BG, 0xF800u, f);
    nx_fill(gc, k_full, SPLASH_BG);
    for (unsigned i = 0; i < NX_VENDOR_SPLASH_N; i++) {
        const struct nx_vmask *m = &k_nx_vendor_splash[i];
        nx_mask1(gc, nx_rect_xywh(m->x, m->y, m->w, m->h), m->bits, m->red ? red : ink);
    }
    splash_foot(gc);
}
#else
static void splash_draw(nx_gc *gc, const void *st)
{
    const struct splash *s = st;
    uint32_t t = splash_t(s, nx_ui_now());
    uint16_t acc = SPLASH_INK;
    uint16_t a = t >= 500u ? 1024u : (uint16_t)(t * 1024u / 500u);   /* ring 0 -> 360 in 500 ms */
    int16_t cx, cy, tx;
    splash_layout(&cx, &cy, &tx);
    nx_fill(gc, k_full, SPLASH_BG);
    nx_ring(gc, cx, cy, WM_R, WM_W, 0, 1024, 0, 0x2104u);
    if (a)
        nx_ring(gc, cx, cy, WM_R, WM_W, 0, a, 1, acc);
    if (t > 200u) {   /* the sine traces left to right, 200..600 ms */
        int16_t n = t >= 600u ? WM_SINE_N : (int16_t)((t - 200u) * WM_SINE_N / 400u);
        nx_trace(gc, (int16_t)(cx - WM_SINE_N / 2), n, sine_y, (void *)(intptr_t)cy, 16, 0xF800u, 32);
    }
    unsigned f = t <= 500u ? 0u : t >= 800u ? 32u : (t - 500u) * 32u / 300u;   /* the words fade in */
    nx_text(gc, &nx_font_brand, tx, WM_Y, WM_TEXT, nx_mix(SPLASH_BG, SPLASH_INK, f));
    char buf[48];
    const char *args[1] = { "GeneratorX Pro" };
    nx_str_format(buf, sizeof buf, NX_S_SPLASH_FIRMWARE, args, 1);
    nx_text_c(gc, &nx_font_cap13, NX_LCD_W / 2, WM_LINE2_Y, buf, nx_mix(SPLASH_BG, 0xC618u, f));
    splash_foot(gc);
}
#endif

/* The splash ends (time or a key): at the first boot the key guide follows. */
static void splash_done(void)
{
    nx_ui_pop(NX_TR_NONE);
    if (!g_uiset.guide)
        nx_ui_push(&nx_scr_guide_boot, 0, NX_TR_NONE);
}

static void splash_tick(void *st, uint32_t now)
{
    struct splash *s = st;
    uint32_t t = splash_t(s, now);
#if NX_VENDOR_SPLASH
    if (t < 700u)
        nx_inval(k_ghost);
#else
    if (t < 900u) {
        int16_t cx, cy, tx;
        splash_layout(&cx, &cy, &tx);
        nx_inval((nx_rect){ (int16_t)(cx - WM_R - 1), (int16_t)(cy - WM_R - 1), (int16_t)(cx + WM_R + 1),
                            (int16_t)(cy + WM_R + 1) });
        if (t > 480u)
            nx_inval(k_words);
    }
#endif
    if (now - s->t0 >= (uint32_t)s->dur + SPLASH_HOLD_MS)
        splash_done();
}

static int splash_key(void *st, const struct nx_kev *e)
{
    (void)st;
    if (e->type == NXK_PRESS || e->type == NXK_CHORD)
        splash_done();   /* any key skips; it does nothing else */
    return 1;
}

const struct nx_scr nx_scr_splash = {
    splash_enter, splash_key, splash_tick, splash_draw, NX_NONE, NX_SF_AWAKE, 0, 0, sizeof(struct splash),
};

/* ---- key guide: after the first boot's splash, and from Settings > Keys & lock ---- */

struct guide {
    uint8_t from_settings;
};

static void guide_enter(void *st, uint32_t arg)
{
    ((struct guide *)st)->from_settings = (uint8_t)arg;
}

static void guide_draw(nx_gc *gc, const void *st)
{
    (void)st;
    static const uint16_t k_lines[7] = { NX_S_GUIDE_UPDOWN, NX_S_GUIDE_START, NX_S_GUIDE_STOP, NX_S_GUIDE_LOOP,
                                         NX_S_GUIDE_G1G2, NX_S_GUIDE_POWER, NX_S_GUIDE_LOCK };
    nx_icon(gc, &nx_icons20, NX_I20_KEYBOARD, NX_M, 10, nx_col(gc, NX_PAL_G1));
    nx_text(gc, &nx_font_body16, 40, 8, nx_str(NX_S_GUIDE_TITLE), nx_col(gc, NX_PAL_TEXT1));
    for (unsigned i = 0; i < 7u; i++)
        nx_text_fit(gc, &nx_font_body16, NX_M, (int16_t)(40 + i * 24), 296, nx_str(k_lines[i]),
                    nx_col(gc, i & 1u ? NX_PAL_TEXT2 : NX_PAL_TEXT1));
}

static void guide_seen(void)
{
    if (!g_uiset.guide) {
        g_uiset.guide = 1;
        nx_ui_settings_dirty();
    }
}

static int guide_key(void *st, const struct nx_kev *e)
{
    struct guide *g = st;
    if (e->type != NXK_PRESS || (e->keys != NX_KEY_START && e->keys != NX_KEY_STOP))
        return 1;
    guide_seen();
    nx_ui_pop(g->from_settings ? NX_TR_DIP : NX_TR_NONE);
    return 1;
}

const struct nx_scr nx_scr_guide = {
    guide_enter, guide_key, 0, guide_draw, NX_S_GUIDE_CONTINUE, NX_SF_AWAKE, 0, 0, sizeof(struct guide),
};

/* At the first boot any key closes the guide and then does what it does on the dashboard
 * (NX_SF_PASS: the framework pops it), so the first press is never lost. */
static int guide_boot_key(void *st, const struct nx_kev *e)
{
    (void)st;
    (void)e;
    guide_seen();
    return 1;
}

const struct nx_scr nx_scr_guide_boot = {
    guide_enter, guide_boot_key, 0, guide_draw, NX_S_GUIDE_ANY_KEY, NX_SF_AWAKE | NX_SF_PASS, 0, 0,
    sizeof(struct guide),
};

/* ---- goodbye (W22): the ring closes 360 -> 0 over 600 ms ---- */

#define BYE_RING_MS 600u

struct bye {
    uint32_t t0;
};

static void bye_enter(void *st, uint32_t arg)
{
    ((struct bye *)st)->t0 = arg;
}

static void bye_draw(nx_gc *gc, const void *st)
{
    const struct bye *b = st;
    uint32_t t = nx_ui_now() - b->t0;
    uint16_t a = t >= BYE_RING_MS ? 0u : (uint16_t)(1024u - t * 1024u / BYE_RING_MS);
    if (a)
        nx_ring(gc, RING_CX, RING_CY, 36, 6, 0, a, 1, nx_col(gc, NX_PAL_G1));
    nx_icon(gc, &nx_icons32, NX_I32_POWER, RING_CX - 16, RING_CY - 16, nx_col(gc, NX_PAL_TEXT2));
    nx_text_c(gc, &nx_font_body16, RING_CX, WORDS_Y, nx_str(NX_S_BYE_TITLE), nx_col(gc, NX_PAL_TEXT1));
    char d[16], line[48];
    nx_fmt_dur(d, g_vm_draw.s.uptime_s);
    const char *args[1] = { d };
    nx_str_format(line, sizeof line, NX_S_BYE_SESSION, args, 1);
    nx_text_c(gc, &nx_font_cap13, RING_CX, LINE2_Y, line, nx_col(gc, NX_PAL_TEXT2));
}

static void bye_tick(void *st, uint32_t now)
{
    if (now - ((struct bye *)st)->t0 < BYE_RING_MS + 40u)
        nx_inval(k_mark);
}

const struct nx_scr nx_scr_bye = {
    bye_enter, 0, bye_tick, bye_draw, NX_NONE, NX_SF_AWAKE, 0, 0, sizeof(struct bye),
};

/* ---- night view (W22): night palette, 3 % backlight, the two generators at a glance ---- */

void nx_ambient_draw(nx_gc *gc)
{
    static const uint8_t k_icon[5] = { NX_I20_ADD, NX_I20_STOP, NX_I20_PLAY, NX_I20_PAUSE, NX_I20_CHECK_CIRCLE };
    for (unsigned ch = 0; ch < 2u; ch++) {
        const struct nx_gen_vm *g = &g_vm_draw.g[ch];
        int16_t y = (int16_t)(40 + ch * 80);
        char buf[NX_UC_NAME_MAX + 1u];
        nx_text(gc, &nx_font_body16, NX_M, y, nx_str(ch ? NX_S_GEN_G2 : NX_S_GEN_G1), nx_col(gc, NX_PAL_TEXT2));
        nx_icon(gc, &nx_icons20, k_icon[g->state < 5u ? g->state : 0u], 40, y, nx_col(gc, NX_PAL_TEXT1));
        if (g->state == NXG_NONE)
            continue;
        if (!nx_uc_name(g->slot, buf)) {
            char n[4];
            nx_fmt_2d(n, g->slot);
            const char *args[1] = { n };
            nx_str_format(buf, sizeof buf, NX_S_LIB_SLOT_N, args, 1);
        }
        nx_text_fit(gc, &nx_font_body16, 68, y, 130, buf, nx_col(gc, NX_PAL_TEXT1));
        nx_fmt_dur(buf, g->left_s);
        nx_text_r(gc, &nx_font_num28, 308, (int16_t)(y - 4), buf, nx_col(gc, NX_PAL_TEXT1));
        char s[8], n[8], line[40];
        nx_fmt_u(s, g->step);
        nx_fmt_u(n, g->count);
        const char *args[2] = { s, n };
        nx_str_format(line, sizeof line, NX_S_DASH_STEP_N, args, 2);
        nx_text(gc, &nx_font_cap13, 68, (int16_t)(y + 24), line, nx_col(gc, NX_PAL_TEXT2));
    }
    nx_text_r(gc, &nx_font_cap13, NX_LCD_W - NX_M, FOOT_Y, nx_str(NX_S_AMBIENT_WAKE), nx_col(gc, NX_PAL_TEXT3));
}
