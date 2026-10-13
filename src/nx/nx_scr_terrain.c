/* The Terrain Protocol (menu > Chains > a Terrain row, LOOP; NX_FEAT_TERRAIN, nx_terrain.h): where
 * a protocol stands and what it needs.
 *
 * Keys: G1/G2 the generator it plays on, UP/DOWN its position one set back / on (the jump the
 * one-generator schedule needs; a generator that plays it goes on at the new set), LOOP the power
 * level (Remote), START plays it from the position there and shows that generator's dashboard,
 * STOP back. */
#include "nx_config.h"

#if NX_FEAT_TERRAIN
#include <string.h>

#include "nx_comp.h"
#include "nx_uifmt.h"

static const nx_rect k_title = { 0, 0, NX_LCD_W, NX_BAR_H };
static const nx_rect k_body = { 0, NX_BAR_H, NX_LCD_W, 140 };
static const char *const k_power[5] = { "5 V", "2.5 V", "1 V", "0.5 V", "0.25 V" };

struct terrain {
    uint8_t id, target;
    uint32_t sum;   /* of what the body shows */
};

static uint32_t view_sum(const struct nx_tview *v)
{
    return (uint32_t)v->item | (uint32_t)v->power << 8 | (uint32_t)v->playing << 12 | v->run_s << 14;
}

void nx_scr_terrain_started(unsigned ch, unsigned id)
{
    struct nx_tview v;
    nx_uc_terrain_view(id, &v);
    char d[4];
    nx_fmt_u(d, v.day);
    nx_ui_set_focus(ch);
    nx_ui_reset(&nx_scr_dash, 0, NX_TR_BACK);
    nx_ui_toast_s(NX_I20_CHAIN, nx_accent(ch), NX_S_TERRAIN_STARTED, v.name, d);
}

static void terrain_enter(void *st, uint32_t arg)
{
    struct terrain *t = st;
    t->id = (uint8_t)(arg & 0xFFu);
    t->target = (uint8_t)((arg >> 8) & 1u);
    t->sum = 0;
}

static void terrain_draw(nx_gc *gc, const void *st)
{
    const struct terrain *t = st;
    struct nx_tview v;
    nx_uc_terrain_view(t->id, &v);
    char a[16], b[16], c[8], line[64];
    const char *args[3] = { a, b, c };
    int remote = v.kind == NX_TP_REMOTE;
    args[0] = v.name;
    nx_str_format(line, sizeof line, remote ? NX_S_TERRAIN_REMOTE : NX_S_TERRAIN_CONTACT, args, 1);
    args[0] = a;
    nx_comp_title_target(gc, line, NX_S_DETAIL_RUN_ON, t->target);
    uint16_t t2 = nx_col(gc, NX_PAL_TEXT2), t3 = nx_col(gc, NX_PAL_TEXT3);
    int16_t rx = NX_LCD_W - NX_M;
    for (unsigned ch = 0; ch < 2u; ch++)   /* the generators that play it */
        if (v.playing & (1u << ch)) {
            rx = (int16_t)(rx - 22);
            nx_icon(gc, &nx_icons20, NX_I20_PLAY, rx, 34, nx_col(gc, nx_accent(ch)));
        }
    if (v.item >= v.n) {
        nx_text(gc, &nx_font_body16, NX_M + 4, 34, nx_str(NX_S_TERRAIN_DONE_ALL), nx_col(gc, NX_PAL_TEXT1));
    } else {
        nx_text_fit(gc, &nx_font_body16, NX_M + 4, 34, (int16_t)(rx - NX_M - 8), v.set, nx_col(gc, NX_PAL_TEXT1));
        char *p = nx_fmt_u(a, v.day);   /* "6", or the half days "6.1" / "6.2" */
        if (remote && v.part) {
            *p++ = '.';
            nx_fmt_u(p, v.part);
        }
        nx_fmt_u(b, v.days);
        nx_fmt_u(c, remote ? v.hours : v.part);
        nx_str_format(line, sizeof line, remote ? NX_S_TERRAIN_POS_REMOTE : NX_S_TERRAIN_POS_CONTACT, args, 3);
        nx_text(gc, &nx_font_cap13, NX_M + 4, 60, line, t2);
        if (remote) {
            nx_fmt_dur(a, v.run_s);
            nx_fmt_dur(b, (uint32_t)v.hours * 3600u);
            nx_str_format(line, sizeof line, NX_S_TERRAIN_RAN, args, 2);
            nx_text(gc, &nx_font_cap13, NX_M + 4, 80, line, t2);
        }
    }
    if (remote) {
        args[0] = k_power[v.power % 5u];
        nx_str_format(line, sizeof line, NX_S_TERRAIN_POWER, args, 1);
        nx_text(gc, &nx_font_cap13, NX_M + 4, 108, line, nx_col(gc, nx_accent(t->target)));
    }
    nx_text(gc, &nx_font_cap13, NX_M + 4, 164, nx_str(remote ? NX_S_TERRAIN_HW_REMOTE : NX_S_TERRAIN_HW_CONTACT), t3);
    nx_text(gc, &nx_font_cap13, NX_M + 4, 184, nx_str(NX_S_TERRAIN_WATER), t3);
}

static int terrain_key(void *st, const struct nx_kev *e)
{
    struct terrain *t = st;
    if (e->type != NXK_PRESS && !(e->type == NXK_REPEAT && (e->keys == NX_KEY_UP || e->keys == NX_KEY_DOWN)))
        return 1;
    switch (e->keys) {
    case NX_KEY_G1:
    case NX_KEY_G2:
        t->target = e->keys == NX_KEY_G2;
        nx_inval(k_title);
        break;
    case NX_KEY_UP:
    case NX_KEY_DOWN:
        nx_uc_terrain_move(t->id, e->keys == NX_KEY_DOWN ? 1 : -1);
        nx_inval(k_body);
        break;
    case NX_KEY_LOOP:
        nx_uc_terrain_power(t->id);
        nx_inval(k_body);
        break;
    case NX_KEY_START:
        if (nx_uc_terrain_play(t->target, t->id) == NX_OK)
            nx_scr_terrain_started(t->target, t->id);
        break;
    case NX_KEY_STOP:
        nx_ui_pop(NX_TR_BACK);
        break;
    default:
        break;
    }
    return 1;
}

static void terrain_tick(void *st, uint32_t now)
{
    (void)now;
    struct terrain *t = st;
    struct nx_tview v;
    nx_uc_terrain_view(t->id, &v);
    uint32_t sum = view_sum(&v);
    if (sum != t->sum) {   /* its time runs, a set ends, another generator starts it */
        t->sum = sum;
        nx_inval(k_body);
    }
}

const struct nx_scr nx_scr_terrain = {
    terrain_enter, terrain_key, terrain_tick, terrain_draw, NX_S_HINT_TERRAIN, 0, NX_KEY_UP | NX_KEY_DOWN, 0,
    sizeof(struct terrain),
};
#endif
