/* Dashboard, "Focus + mini strip" (docs/nx/design/ui.md W2, §4.2, §5.2): the focused generator
 * large (status bar, name, hero frequency with the odometer roll, meta lines, live scope,
 * progress bar with the time left, next frequency) and the other one as a strip at the bottom.
 * Variants: no program, ready, running, paused, finished, not responding.
 *
 * Keys: UP quick settings, DOWN the menu; START play / pause / resume / again (no program: the
 * library); STOP released within 0.5 s stops the focused generator, held past 1 s and then
 * released stops both (acting on the release keeps the key-to-line time short);
 * LOOP tapped: repeat on/off while running, play looped otherwise; LOOP held: the program detail
 * of the loaded program (nothing without one); G1/G2: focus that generator (hardware-scroll
 * push), the focused one again: its library. */
#include <string.h>

#include "nx_anim.h"
#include "nx_comp.h"
#include "nx_core.h"
#include "nx_hero.h"
#include "nx_scope.h"
#include "nx_uifmt.h"

/* line top of the big frequency: the 48 px digits, or the 28 px ones around the same middle */
#define HERO_Y (NX_FEAT_HERO48 ? 54 : 63)
#define STOP_ALL_MS 1000u
#define ROLL_LIVE_WIN_MS 1000u

static const nx_rect k_status = { 0, 0, NX_LCD_W, NX_BAR_H };
static const nx_rect k_name = { NX_M, 30, 308, 50 };   /* one text line, its baseline 8 px above the hero */
static const nx_rect k_hero = { 0, 52, NX_LCD_W, 112 };
static const nx_rect k_meta = { 0, 112, 168, 168 };
static const nx_rect k_scope = { 172, 114, 308, 166 };
static const nx_rect k_bar = { 0, 168, NX_LCD_W, 188 };
static const nx_rect k_next = { 0, 188, NX_LCD_W, 208 };
static const nx_rect k_strip = { 0, 208, NX_LCD_W, NX_LCD_H };
static const nx_rect k_focus = { 0, NX_BAR_H, NX_LCD_W, 208 };

struct dash {
    char from[14], to[14];   /* the hero's big digits before and after the last change */
    uint32_t roll_t0, roll_win;
    uint32_t mq_t0;          /* the name marquee's start */
    int16_t mq_last;
    uint16_t scope_last;
    uint8_t roll_n, roll_live;
    uint8_t stop_held;       /* STOP past its HOLD: the release may stop both */
    uint8_t rsv;
};

enum { CL_NONE, CL_ERROR, CL_FINISHED, CL_PROGRAM };

static unsigned cls(const struct nx_gen_vm *g)
{
    if (g->state == NXG_NONE)
        return CL_NONE;
    if (g->link_down)
        return CL_ERROR;
    return g->state == NXG_FINISHED ? CL_FINISHED : CL_PROGRAM;
}

static const char *gname(unsigned ch)
{
    return nx_str(ch ? NX_S_GEN_G2 : NX_S_GEN_G1);
}

static uint8_t state_tok(const struct nx_gen_vm *g, unsigned ch)
{
    if (g->link_down && g->state != NXG_NONE)
        return NX_PAL_ERROR;
    switch (g->state) {
    case NXG_RUNNING:
        return nx_accent(ch);
    case NXG_FINISHED:
        return NX_PAL_SUCCESS;
    case NXG_NONE:
        return NX_PAL_TEXT3;
    default:
        return NX_PAL_TEXT2;
    }
}

static void state_icon_label(const struct nx_gen_vm *g, uint8_t *icon, uint16_t *label)
{
    /* the original's status icons: c ready, r running, p paused, s stopped */
    static const uint8_t k_icon[5] = { NX_I20_ADD, NX_I20_CHECK_CIRCLE, NX_I20_PLAY, NX_I20_PAUSE,
                                       NX_I20_CHECK_CIRCLE };
    static const uint16_t k_label[5] = { NX_S_STATE_NO_PROGRAM, NX_S_STATE_READY, NX_S_STATE_RUNNING,
                                         NX_S_STATE_PAUSED, NX_S_STATE_FINISHED };
    *icon = k_icon[g->state];
    *label = k_label[g->state];
    if (g->state == NXG_READY && g->stopped) {
        *icon = NX_I20_STOP;
        *label = NX_S_STATE_STOPPED;
    }
    if (g->link_down && g->state != NXG_NONE) {
        *icon = NX_I20_WARNING;
        *label = NX_S_STATE_NO_RESPONSE;
    }
}

/* ---- hero with the odometer roll (ui.md §2.7): same layout before and after, changed cells
 * roll up over NX_ROLL_MS, 20 ms apart from the right, tinted to the accent for NX_TINT_MS ---- */

static int same_layout(const char *a, const char *b)
{
    const char *pa = strchr(a, '.'), *pb = strchr(b, '.');
    return strlen(a) == strlen(b) && (pa ? pa - a : -1) == (pb ? pb - b : -1);
}

static uint32_t roll_len(const struct dash *d)
{
    unsigned k = 0;
    for (unsigned i = 0; d->to[i]; i++)
        k += d->from[i] != d->to[i];
    return (k ? k - 1u : 0u) * NX_ROLL_STAGGER_MS + NX_TINT_MS;
}

static void hero_draw(nx_gc *gc, const struct dash *d, uint64_t v, uint16_t c_text, uint16_t c_acc, uint32_t now)
{
    nx_hero h;
    nx_hero_layout(v, &h);
    int16_t x = (int16_t)((NX_LCD_W - h.w) / 2);
    int rolling = !d->roll_live && same_layout(d->from, d->to) && strcmp(h.big, d->to) == 0
               && now - d->roll_t0 < roll_len(d);
    if (!rolling) {
        nx_hero_draw(gc, &h, x, HERO_Y, c_text, nx_col(gc, NX_PAL_TEXT3), nx_col(gc, NX_PAL_TEXT2));
        return;
    }
    /* the big digits cell by cell, then the tail and "Hz" as nx_hero_draw sets them */
    const nx_font_t *bf = nx_hero_font(&h);
    int16_t gap = NX_HERO_IS_D1(h.font) ? NX_HERO_GAP_D1 : NX_HERO_GAP_D2, px = x;
    unsigned n = (unsigned)strlen(h.big), order = 0;
    uint8_t delay[14];
    for (unsigned i = n; i-- > 0;)
        delay[i] = d->from[i] != d->to[i] ? (uint8_t)order++ : 0xFFu;
    for (unsigned i = 0; i < n; i++) {
        if ((h.big_gaps >> i) & 1u)
            px = (int16_t)(px + gap);
        int16_t w = (int16_t)nx_font_text_width_n(bf, &h.big[i], 1);
        if (delay[i] == 0xFFu) {
            nx_text_n(gc, bf, px, HERO_Y, &h.big[i], 1, c_text);
        } else {
            uint32_t t0 = d->roll_t0 + delay[i] * NX_ROLL_STAGGER_MS;
            uint16_t p = (int32_t)(now - t0) <= 0 ? 0u : nx_anim_p(t0, now, NX_ROLL_MS, NX_EASE_OUT);
            uint32_t tt = (int32_t)(now - t0) <= 0 ? 0u : now - t0;
            unsigned tint = tt >= NX_TINT_MS ? 0u : 32u - tt * 32u / NX_TINT_MS;
            uint16_t c = nx_mix(c_text, c_acc, tint);
            int16_t off = (int16_t)(((int32_t)bf->height * p) >> 16);
            nx_rect prev = nx_clip_push(gc, (nx_rect){ px, HERO_Y, (int16_t)(px + w), (int16_t)(HERO_Y + bf->height) });
            nx_text_n(gc, bf, px, (int16_t)(HERO_Y - off), &d->from[i], 1, c);
            nx_text_n(gc, bf, px, (int16_t)(HERO_Y + bf->height - off), &d->to[i], 1, c);
            nx_clip_pop(gc, prev);
        }
        px = (int16_t)(px + w);
    }
    nx_hero tail = h;   /* the tail and the unit: nx_hero_draw with the big part clipped away */
    nx_rect prev = nx_clip_push(gc, (nx_rect){ px, 0, NX_LCD_W, NX_LCD_H });
    nx_hero_draw(gc, &tail, x, HERO_Y, c_text, nx_col(gc, NX_PAL_TEXT3), nx_col(gc, NX_PAL_TEXT2));
    nx_clip_pop(gc, prev);
}

static void roll_set(struct dash *d, uint64_t v, uint32_t now)
{
    nx_hero h;
    nx_hero_layout(v, &h);
    if (strcmp(h.big, d->to) == 0)
        return;
    if (now - d->roll_win >= ROLL_LIVE_WIN_MS) {   /* live rule: > 3 changes a second swap instantly */
        d->roll_win = now;
        d->roll_n = 0;
    }
    d->roll_n++;
    d->roll_live = d->roll_n > NX_ROLL_LIVE_N || nx_anim_reduced() || !nx_ui_motion();
    memcpy(d->from, d->to, sizeof d->from);
    strncpy(d->to, h.big, sizeof d->to - 1u);
    d->to[sizeof d->to - 1u] = 0;
    d->roll_t0 = now;
}

/* ---- drawing ---- */

static void draw_status(nx_gc *gc, const struct nx_gen_vm *g, unsigned ch)
{
    nx_fill(gc, k_status, nx_col(gc, NX_PAL_SURFACE1));
    int16_t x = nx_comp_badge(gc, NX_M, 2, ch, 1);
    uint8_t icon;
    uint16_t label;
    state_icon_label(g, &icon, &label);
    uint16_t c = nx_col(gc, state_tok(g, ch));
    nx_icon(gc, &nx_icons20, icon, (int16_t)(x + 8), 1, c);
    nx_text(gc, &nx_font_cap13, (int16_t)(x + 32), 1, nx_str(label), c);
    int16_t rx = 308;
    if (nx_ui_locked()) {
        nx_icon(gc, &nx_icons20, NX_I20_LOCK, (int16_t)(rx - 20), 1, nx_col(gc, NX_PAL_TEXT2));
        rx = (int16_t)(rx - 26);
    }
    if (!g_vm_draw.s.ready) {   /* the first ~4.4 s: the generators start (nothing is sent yet) */
        rx = nx_text_r(gc, &nx_font_cap13, rx, 1, nx_str(NX_S_STATE_STARTING), nx_col(gc, NX_PAL_TEXT2));
        nx_icon(gc, &nx_icons20, NX_I20_HOURGLASS, (int16_t)(rx - 22), 1, nx_col(gc, NX_PAL_TEXT2));
        return;
    }
#if NX_FEAT_TIMERS
    if (g->timer_min) {   /* "stop after": the minutes left */
        char m[8], t[16];
        const char *args[1] = { m };
        nx_fmt_u(m, g->timer_min);
        nx_str_format(t, sizeof t, NX_S_SET_MINUTES, args, 1);
        rx = nx_text_r(gc, &nx_font_cap13, rx, 1, t, nx_col(gc, NX_PAL_TEXT2));
        nx_icon(gc, &nx_icons20, NX_I20_TIMER, (int16_t)(rx - 22), 1, nx_col(gc, NX_PAL_TEXT2));
        rx = (int16_t)(rx - 30);
    }
#endif
#if NX_FEAT_CHAINS
    if (g->chain || g->terrain) {   /* "Chain 2/5", or a plan's or the Terrain Protocol's "Day 3/11" */
        char i[4], n[4], t[24];
        const char *args[2] = { i, n };
        nx_fmt_u(i, g->terrain ? g->t_day : g->plan ? g->plan_day : g->chain_item);
        nx_fmt_u(n, g->terrain ? g->t_days : g->plan ? g->plan_n : g->chain_n);
        nx_str_format(t, sizeof t, g->plan || g->terrain ? NX_S_PLAN_STATUS : NX_S_CHAIN_STATUS, args, 2);
        rx = nx_text_r(gc, &nx_font_cap13, rx, 1, t, nx_col(gc, nx_accent(ch)));
        nx_icon(gc, &nx_icons20, NX_I20_CHAIN, (int16_t)(rx - 22), 1, nx_col(gc, nx_accent(ch)));
        rx = (int16_t)(rx - 30);
    }
#endif
    if (g->loop && g->state != NXG_NONE) {
        char buf[16], *p = buf;
        for (const char *l = nx_str(NX_S_STATE_LOOP); *l && p < buf + 8; )
            *p++ = *l++;
        *p = 0;
        if (g->passes) {
            *p++ = ' ';
            *p++ = (char)NX_CH_times;
            nx_fmt_u(p, (uint32_t)g->passes + 1u);
        }
        rx = nx_text_r(gc, &nx_font_cap13, rx, 1, buf, nx_col(gc, nx_accent(ch)));
        nx_icon(gc, &nx_icons20, NX_I20_REPEAT, (int16_t)(rx - 22), 1, nx_col(gc, nx_accent(ch)));
    }
}

static void draw_name(nx_gc *gc, const struct dash *d, const struct nx_gen_vm *g)
{
    char name[NX_UC_NAME_MAX + 1u];
    if (!nx_uc_name(g->slot, name)) {
        char n[4];
        nx_fmt_2d(n, g->slot);
        const char *args[1] = { n };
        nx_str_format(name, sizeof name, NX_S_LIB_SLOT_N, args, 1);
    }
    nx_text_box(gc, &nx_font_body16, k_name, name, nx_col(gc, NX_PAL_TEXT1), 1, nx_ui_now() - d->mq_t0);
}

/* The three lines centred in the space between the status bar and the strip: the message, then
 * the two hints closer together. */
/* A dashed ring with a "+" over the three lines: the message, then the two hints closer
 * together, all centred between the status bar and the strip. */
static void draw_none(nx_gc *gc, unsigned ch)
{
    uint16_t c = nx_col(gc, NX_PAL_TEXT3);
    for (unsigned i = 0; i < 12u; i++)
        nx_ring(gc, 160, 66, 26, 3, (uint16_t)(i * 1024u / 12u), (uint16_t)(i * 1024u / 12u + 50u), 0, c);
    nx_fill(gc, (nx_rect){ 151, 65, 169, 68 }, c);
    nx_fill(gc, (nx_rect){ 159, 57, 162, 76 }, c);
    nx_text_c(gc, &nx_font_body16, 160, 104, nx_str(NX_S_DASH_NO_PROGRAM), nx_col(gc, NX_PAL_TEXT1));
    char buf[64];
    const char *args[1] = { gname(ch) };
    nx_str_format(buf, sizeof buf, NX_S_DASH_CHOOSE_HINT, args, 1);
    nx_text_c(gc, &nx_font_body16, 160, 132, buf, nx_col(gc, NX_PAL_TEXT2));
    nx_text_c(gc, &nx_font_cap13, 160, 156, nx_str(NX_S_DASH_KEYS_HINT), nx_col(gc, NX_PAL_TEXT3));
}

static void draw_card(nx_gc *gc, uint8_t icon, uint8_t tok, const char *title, const char *line1,
                      const char *line2)
{
    nx_card(gc, (nx_rect){ NX_M, 60, 308, 160 });
    nx_icon(gc, &nx_icons32, icon, 22, 74, nx_col(gc, tok));
    nx_text_fit(gc, &nx_font_body16, 62, 76, 238, title, nx_col(gc, NX_PAL_TEXT1));
    nx_text_fit(gc, &nx_font_body16, 62, 100, 238, line1, nx_col(gc, NX_PAL_TEXT2));
    nx_text_fit(gc, &nx_font_cap13, 62, 126, 238, line2, nx_col(gc, NX_PAL_TEXT3));
}

/* The shape and the Out2 relation only: amplitude and wave code are on the info line. */
static void scope_cfg(nx_scope_cfg *cfg, const struct nx_gen_vm *g, unsigned ch)
{
    struct nx_wave_info wi;
    nx_wave_info(g->wave, &wi);
    memset(cfg, 0, sizeof *cfg);
    cfg->wave = wi.shape;
    cfg->out2 = wi.out2;
    cfg->state = g->state == NXG_RUNNING ? NX_SCOPE_RUNNING
               : g->state == NXG_PAUSED || g->state == NXG_READY ? NX_SCOPE_PAUSED : NX_SCOPE_STOPPED;
    cfg->accent = nx_accent(ch);
}

static void wave_text(char *out, uint8_t wave)
{
    struct nx_wave_info wi;
    if (nx_wave_info(wave, &wi) && wi.name != NX_WAVE_NO_NAME) {
        strcpy(out, nx_str(wi.name));
    } else {
        char n[4];
        nx_fmt_u(n, wave);
        const char *args[1] = { n };
        nx_str_format(out, 24, NX_S_DASH_WAVE_UNKNOWN, args, 1);
    }
}

static void draw_program(nx_gc *gc, const struct dash *d, const struct nx_gen_vm *g, unsigned ch, uint32_t now)
{
    int run = g->state == NXG_RUNNING;
    uint16_t c1 = nx_col(gc, run ? NX_PAL_TEXT1 : NX_PAL_TEXT2), c2 = nx_col(gc, NX_PAL_TEXT2);
    uint16_t acc = nx_col(gc, nx_accent(ch));
    hero_draw(gc, d, g->freq, c1, acc, now);

    char a[32], b[16], line[64];
    const char *args[2];
    if (g->state == NXG_READY) {   /* ready: what the program will do */
        nx_fmt_u(b, g->count);
        args[0] = b;
        nx_str_format(line, sizeof line, NX_S_DASH_STEPS_B, args, 1);
        nx_text(gc, &nx_font_body16, NX_M, 116, line, nx_col(gc, NX_PAL_TEXT1));
        nx_fmt_dur(b, g->dwell_s);
        args[0] = b;
        nx_str_format(line, sizeof line, NX_S_DASH_PER_STEP, args, 1);
        nx_text_fit(gc, &nx_font_cap13, NX_M, 136, 150, line, c2);
    } else {
        char s[8], n[8];
        nx_fmt_u(s, g->step);
        nx_fmt_u(n, g->count);
        args[0] = s;
        args[1] = n;
        nx_str_format(line, sizeof line, NX_S_DASH_STEP_OF, args, 2);
        nx_text(gc, &nx_font_body16, NX_M, 116, line, nx_col(gc, NX_PAL_TEXT1));
        nx_fmt_dur(a, g->dwell_s);
        nx_fmt_dur(b, g->step_left_s);
        args[0] = a;
        args[1] = b;
        nx_str_format(line, sizeof line, NX_S_DASH_DWELL_LINE, args, 2);
        nx_text_fit(gc, &nx_font_cap13, NX_M, 136, 150, line, c2);
    }
    char *p = nx_fmt_cv(line, g->ampl_cv);
    *p++ = ' ';
    *p++ = (char)NX_CH_middot;
    *p++ = ' ';
    wave_text(p, g->wave);
    nx_text_fit(gc, &nx_font_cap13, NX_M, 152, 150, line, c2);

    nx_scope_cfg cfg;
    scope_cfg(&cfg, g, ch);
    nx_scope_draw(gc, k_scope, &cfg, run && nx_ui_motion() ? nx_scope_phase(now) : 0);

    nx_bar(gc, (nx_rect){ NX_M, 176, 180, 182 }, g->state == NXG_READY ? 0u : g->frac,
           run ? acc : nx_col(gc, NX_PAL_TEXT2), nx_col(gc, NX_PAL_TRACK));
    nx_fmt_dur(a, g->state == NXG_READY ? g->total_s : g->left_s);
    args[0] = a;
    nx_str_format(line, sizeof line, g->state == NXG_READY ? NX_S_DASH_TOTAL : NX_S_DASH_LEFT_B, args, 1);
    nx_text_r(gc, &nx_font_body16, 308, 170, line, nx_col(gc, NX_PAL_TEXT1));

    if (g->state == NXG_READY) {   /* chips: START play, LOOP play looped */
        nx_icon(gc, &nx_icons20, NX_I20_PLAY, NX_M, 188, acc);
        int16_t x = nx_text(gc, &nx_font_cap13, 34, 189, nx_str(NX_S_DASH_CHIP_PLAY), c2);
        nx_icon(gc, &nx_icons20, NX_I20_REPEAT, (int16_t)(x + 16), 188, acc);
        nx_text(gc, &nx_font_cap13, (int16_t)(x + 38), 189, nx_str(NX_S_DASH_CHIP_LOOP), c2);
        return;
    }
    if (g->next) {
        nx_fmt_freq(a, g->next, 3, 8, 0);
        args[0] = a;
        nx_str_format(line, sizeof line, NX_S_DASH_NEXT_LINE, args, 1);
        nx_text_fit(gc, &nx_font_cap13, NX_M, 189, 220, line, nx_col(gc, NX_PAL_TEXT3));
    }
    if (g->loop && g->passes) {
        nx_fmt_u(b, (uint32_t)g->passes + 1u);
        args[0] = b;
        nx_str_format(line, sizeof line, NX_S_DASH_PASS_N, args, 1);
        nx_text_r(gc, &nx_font_cap13, 308, 189, line, nx_col(gc, NX_PAL_TEXT3));
    }
}

static void draw_strip(nx_gc *gc, unsigned ch)
{
    const struct nx_gen_vm *g = &g_vm_draw.g[ch];
    nx_fill(gc, (nx_rect){ 0, 208, NX_LCD_W, 209 }, nx_col(gc, NX_PAL_OUTLINE));
    nx_fill(gc, (nx_rect){ 0, 209, NX_LCD_W, NX_LCD_H }, nx_col(gc, NX_PAL_SURFACE1));
    nx_fill(gc, (nx_rect){ 0, 212, 4, 236 }, nx_col(gc, nx_accent(ch)));
    int16_t x = nx_comp_badge(gc, 10, 215, ch, 1);
    uint8_t icon;
    uint16_t label;
    state_icon_label(g, &icon, &label);
    nx_icon(gc, &nx_icons20, icon, (int16_t)(x + 6), 214, nx_col(gc, state_tok(g, ch)));
    x = (int16_t)(x + 32);
    if (g->loop && g->state != NXG_NONE) {   /* the other generator's loop badge, as the original */
        nx_icon(gc, &nx_icons20, NX_I20_REPEAT, x, 214, nx_col(gc, nx_accent(ch)));
        x = (int16_t)(x + 26);
    }
    char buf[64], t[16];
    if (g->state == NXG_NONE) {
        const char *args[1] = { gname(ch) };
        nx_str_format(buf, sizeof buf, NX_S_STRIP_NONE, args, 1);
        nx_text_fit(gc, &nx_font_cap13, x, 215, (int16_t)(308 - x), buf, nx_col(gc, NX_PAL_TEXT3));
        return;
    }
    int16_t rx = 308;
    if (g->link_down) {
        rx = nx_text_r(gc, &nx_font_cap13, rx, 215, nx_str(NX_S_STRIP_NO_RESPONSE), nx_col(gc, NX_PAL_ERROR));
    } else if (g->state == NXG_FINISHED) {
        rx = nx_text_r(gc, &nx_font_cap13, rx, 215, nx_str(NX_S_STATE_FINISHED), nx_col(gc, NX_PAL_SUCCESS));
    } else {
        nx_fmt_dur(t, g->state == NXG_READY ? g->total_s : g->left_s);
        rx = nx_text_r(gc, &nx_font_cap13, rx, 215, t, nx_col(gc, NX_PAL_TEXT2));
        char *p = nx_fmt_freq(buf, g->freq, 2, 3, 0);
        *p++ = ' ';
        strcpy(p, nx_str(NX_S_UNIT_HZ));
        rx = nx_text_r(gc, &nx_font_body16, (int16_t)(rx - 12), 213, buf,
                       nx_col(gc, g->state == NXG_RUNNING ? NX_PAL_TEXT1 : NX_PAL_TEXT2));
    }
    char name[NX_UC_NAME_MAX + 1u];
    if (!nx_uc_name(g->slot, name)) {
        char n[4];
        nx_fmt_2d(n, g->slot);
        const char *args[1] = { n };
        nx_str_format(name, sizeof name, NX_S_LIB_SLOT_N, args, 1);
    }
    nx_text_fit(gc, &nx_font_body16, x, 213, (int16_t)(rx - x - 10), name, nx_col(gc, NX_PAL_TEXT1));
}

static void dash_draw(nx_gc *gc, const void *st)
{
    const struct dash *d = st;
    unsigned ch = nx_ui_focus();
    const struct nx_gen_vm *g = &g_vm_draw.g[ch];
    uint32_t now = nx_ui_now();
    draw_status(gc, g, ch);
    switch (cls(g)) {
    case CL_NONE:
        draw_none(gc, ch);
        break;
    case CL_ERROR: {
        char t[48], s[8], n[8], line[48];
        const char *args[2] = { gname(ch), 0 };
        nx_str_format(t, sizeof t, NX_S_DASH_NO_RESPONSE, args, 1);
        nx_fmt_u(s, g->step);
        nx_fmt_u(n, g->count);
        args[0] = s;
        args[1] = n;
        nx_str_format(line, sizeof line, NX_S_DASH_STEP_N, args, 2);
        draw_name(gc, d, g);
        draw_card(gc, NX_I32_WARNING, NX_PAL_ERROR, t, nx_str(NX_S_DASH_NO_RESPONSE_HINT), line);
        break;
    }
    case CL_FINISHED: {
        char a[8], b[16], line[64];
        draw_name(gc, d, g);
        nx_fmt_u(a, g->count);
        nx_fmt_dur(b, g->total_s);
        const char *args[2] = { a, b };
        nx_str_format(line, sizeof line, NX_S_DASH_FINISHED_INFO, args, 2);
        draw_card(gc, NX_I32_CHECK_CIRCLE, NX_PAL_SUCCESS, nx_str(NX_S_DASH_FINISHED), line,
                  nx_str(NX_S_DASH_FINISHED_HINT));
        break;
    }
    default:
        draw_name(gc, d, g);
        draw_program(gc, d, g, ch, now);
        break;
    }
    draw_strip(gc, ch ^ 1u);
}

/* ---- ticks: VM diff -> dirty rectangles ---- */

static void dash_enter(void *st, uint32_t arg)
{
    (void)arg;
    struct dash *d = st;
    nx_hero h;
    nx_hero_layout(g_vm.g[nx_ui_focus()].freq, &h);
    strncpy(d->to, h.big, sizeof d->to - 1u);
    memcpy(d->from, d->to, sizeof d->from);
    d->mq_t0 = nx_ms();
}

static void dash_tick(void *st, uint32_t now)
{
    struct dash *d = st;
    unsigned ch = nx_ui_focus();
    const struct nx_gen_vm *g = &g_vm.g[ch], *o = &g_vm_seen.g[ch];
    if (cls(g) != cls(o) || g->slot != o->slot || g_vm.s.name_sum[ch] != g_vm_seen.s.name_sum[ch]) {
        nx_inval(k_focus);
        d->mq_t0 = now;
        d->mq_last = 0;
    }
    if (g->state != o->state || g->loop != o->loop || g->passes != o->passes || g->link_down != o->link_down
        || g->stopped != o->stopped || g->timer_min != o->timer_min || g->chain != o->chain
        || g->chain_item != o->chain_item || g->plan_day != o->plan_day || g->terrain != o->terrain
        || g->t_day != o->t_day || g_vm.s.ready != g_vm_seen.s.ready)
        nx_inval(k_status);
    if (g->freq != o->freq) {
        roll_set(d, g->freq, now);
        nx_inval(k_hero);
    }
    if (strcmp(d->from, d->to) && now - d->roll_t0 < roll_len(d) + 40u)
        nx_inval(k_hero);   /* the roll is moving */
    if (g->state != o->state)
        nx_inval((nx_rect){ 0, 52, NX_LCD_W, 208 });
    if (g->step != o->step || g->count != o->count || g->step_left_s != o->step_left_s || g->ampl_cv != o->ampl_cv
        || g->wave != o->wave || g->dwell_s != o->dwell_s)
        nx_inval(k_meta);
    if (g->wave != o->wave)
        nx_inval(k_scope);
    if (g->state == NXG_RUNNING && nx_ui_motion()) {
        uint16_t ph = nx_scope_phase(now);
        if (ph != d->scope_last) {
            d->scope_last = ph;
            nx_inval(k_scope);
        }
    }
    if ((g->frac >> 8) != (o->frac >> 8) || g->left_s != o->left_s || g->total_s != o->total_s)
        nx_inval(k_bar);
    if (g->next != o->next || g->passes != o->passes)
        nx_inval(k_next);
    if (cls(g) == CL_PROGRAM || cls(g) == CL_ERROR || cls(g) == CL_FINISHED) {
        char name[NX_UC_NAME_MAX + 1u];
        nx_uc_name(g->slot, name);
        nx_marquee_tick(&d->mq_last, (int16_t)nx_font_text_width(&nx_font_body16, name), k_name.x1 - k_name.x0,
                        now - d->mq_t0, k_name);
    }
    const struct nx_gen_vm *s = &g_vm.g[ch ^ 1u], *so = &g_vm_seen.g[ch ^ 1u];
    if (s->state != so->state || s->slot != so->slot || s->freq != so->freq || s->left_s != so->left_s
        || s->link_down != so->link_down || s->total_s != so->total_s || s->loop != so->loop
        || s->stopped != so->stopped
        || g_vm.s.name_sum[ch ^ 1u] != g_vm_seen.s.name_sum[ch ^ 1u])
        nx_inval(k_strip);
}

/* ---- keys ---- */

static void act_start(unsigned ch, const struct nx_gen_vm *g)
{
    switch (g->state) {
    case NXG_NONE:
        nx_ui_push(&nx_scr_lib, ch, NX_TR_PUSH);
        break;
    case NXG_RUNNING:   /* pausing keeps the repeat setting (the original's keys: clears it) */
        nx_ui_act(NXA_PAUSE, (uint8_t)ch, 0, g_uiset.origkeys ? 0u : g->loop);
        break;
    case NXG_FINISHED:
        nx_ui_act(NXA_PLAY, (uint8_t)ch, 0, 0);
        break;
    default:
        nx_ui_act(NXA_PLAY, (uint8_t)ch, 0, g_uiset.origkeys ? 0u : g->loop);
        break;
    }
}

static int dash_key(void *st, const struct nx_kev *e)
{
    struct dash *d = st;
    unsigned ch = nx_ui_focus();
    const struct nx_gen_vm *g = &g_vm.g[ch];
    switch (e->keys) {
    case NX_KEY_UP:
        if (e->type == NXK_PRESS)
            nx_ui_push(&nx_scr_quick, 0, NX_TR_SHEET);
        return 1;
    case NX_KEY_DOWN:   /* the menu (programs, settings, and the other screens of this build) */
        if (e->type == NXK_PRESS)
            nx_ui_push(&nx_scr_menu, 0, NX_TR_SHEET);
        return 1;
    case NX_KEY_START:
        switch (nx_keys_act(e, 1)) {   /* short: the generator's; held: the biofeedback scan on it */
        case NXKA_SHORT:
            act_start(ch, g);
            break;
        case NXKA_HOLD:
            nx_ui_push(&nx_scr_bfb, 1u + ch, NX_TR_PUSH);
            break;
        default:
            break;
        }
        return 1;
    case NX_KEY_STOP:
        if (e->type == NXK_HOLD) {
            d->stop_held = 1;
            nx_ui_hold_show(NX_S_HOLD_STOP_BOTH, NX_UI_ICON20 | NX_I20_STOP, e->t - NX_KEY_HOLD_MS, NX_KEY_HOLD_MS, STOP_ALL_MS);
        }
        if (e->type == NXK_RELEASE) {
            if (d->stop_held)
                nx_ui_hold_hide();
            d->stop_held = 0;
            if (e->n < NX_KEY_HOLD_MS && g->state != NXG_NONE) {
                if (nx_ui_act(NXA_STOP, (uint8_t)ch, 0, 0) == NX_OK)
                    nx_ui_toast_s(NX_I20_STOP, NX_PAL_TEXT2, NX_S_TOAST_STOPPED, gname(ch), 0);
            } else if (e->n >= STOP_ALL_MS) {
                if (nx_ui_act(NXA_STOP_ALL, (uint8_t)ch, 0, 0) == NX_OK)
                    nx_ui_toast_s(NX_I20_STOP, NX_PAL_TEXT2, NX_S_TOAST_BOTH_STOPPED, 0, 0);
            }
        }
        return 1;
    case NX_KEY_LOOP:
        switch (nx_keys_act(e, 1)) {
        case NXKA_SHORT:
            if (g->state == NXG_RUNNING && g_uiset.origkeys) {   /* the original: pause, repeat on */
                nx_ui_act(NXA_PAUSE, (uint8_t)ch, 0, 1);
            } else if (g->state == NXG_RUNNING) {
                if (nx_ui_act(NXA_LOOP, (uint8_t)ch, 0, g->loop ? 0u : 1u) == NX_OK)
                    nx_ui_toast_s(NX_I20_REPEAT, nx_accent(ch), g->loop ? NX_S_TOAST_LOOP_OFF : NX_S_TOAST_LOOP_ON,
                                  0, 0);
            } else if (g->state != NXG_NONE) {
                nx_ui_act(NXA_PLAY, (uint8_t)ch, 0, 1);
            }
            break;
        case NXKA_HOLD:
            if (g->state != NXG_NONE && g->slot <= NX_UC_SLOTS)   /* a slot's detail (nx_src.h) */
                nx_ui_push(&nx_scr_detail, (uint32_t)g->slot | ((uint32_t)ch << 8), NX_TR_PUSH);
#if NX_FEAT_MANUAL
            else if (g->slot == NX_UC_MANUAL)   /* the manual generator's values */
                nx_ui_push(&nx_scr_manual, 0x100u | ch, NX_TR_PUSH);
#endif
#if NX_FEAT_TERRAIN
            else if (g->terrain)   /* the Terrain protocol it plays */
                nx_ui_push(&nx_scr_terrain, g->terrain | (uint32_t)ch << 8, NX_TR_PUSH);
#endif
            break;
        default:
            break;
        }
        return 1;
    case NX_KEY_G1:
    case NX_KEY_G2:
        if (e->type == NXK_PRESS) {
            unsigned want = e->keys == NX_KEY_G2;
            if (want == ch || g_vm.g[want].state == NXG_NONE) {
                /* as the original: the shown generator's key, or the key of a generator without
                 * a program, opens that generator's program list */
                nx_ui_push(&nx_scr_lib, want, NX_TR_PUSH);
            } else {
                nx_ui_set_focus((uint8_t)want);
                nx_ui_reset(&nx_scr_dash, 0, want ? NX_TR_PUSH : NX_TR_BACK);
            }
        }
        return 1;
    default:
        return 0;
    }
}

const struct nx_scr nx_scr_dash = {
    dash_enter, dash_key, dash_tick, dash_draw, NX_NONE, 0, 0, 0, sizeof(struct dash),
};

_Static_assert(sizeof(struct dash) <= NX_UI_ST_MAX, "dashboard state fits a stack level");
