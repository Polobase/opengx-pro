/* nx motion (nx_anim.h). Cubic easing on Q15 intermediates: 32-bit products, no division. */
#include "nx_anim.h"

#include <string.h>

static uint8_t s_reduced;

void nx_anim_set_reduced(int on)
{
    s_reduced = (uint8_t)(on != 0);
}

int nx_anim_reduced(void)
{
    return s_reduced;
}

static uint32_t cube15(uint32_t q)   /* q^3 in Q15, q <= 32768 */
{
    uint32_t q2 = (q * q) >> 15;
    return (q2 * q) >> 15;
}

uint16_t nx_ease(uint8_t ease, uint16_t p)
{
    if (p == 0 || p == 65535u)   /* exact end points (the Q15 cubes round near them) */
        return p;
    uint32_t p15 = p >> 1, r;
    switch (ease) {
    case NX_EASE_OUT:
        r = 32768u - cube15(32768u - p15);
        break;
    case NX_EASE_IN:
        r = cube15(p15);
        break;
    case NX_EASE_IN_OUT:
        r = p15 < 16384u ? 4u * cube15(p15) : 32768u - cube15(65536u - 2u * p15) / 2u;
        break;
    default:
        return p;
    }
    r <<= 1;
    return (uint16_t)(r > 65534u ? 65534u : r);   /* 65535 only when the time is up */
}

uint16_t nx_anim_p(uint32_t t0, uint32_t now, uint16_t dur, uint8_t ease)
{
    uint32_t t = now - t0;
    if (s_reduced || dur == 0 || t >= dur)
        return 65535u;
    return nx_ease(ease, (uint16_t)(t * 65535u / dur));
}

void nx_tween_start(nx_tween *tw, int16_t from, int16_t to, uint16_t dur, uint8_t ease, uint32_t now)
{
    tw->t0 = now;
    tw->dur = dur;
    tw->from = from;
    tw->to = to;
    tw->ease = ease;
}

int16_t nx_tween_at(const nx_tween *tw, uint32_t now)
{
    uint16_t p = nx_anim_p(tw->t0, now, tw->dur, tw->ease);
    if (p == 65535u)
        return tw->to;
    return (int16_t)(tw->from + (((int32_t)tw->to - tw->from) * (int32_t)p >> 16));
}

int nx_tween_running(const nx_tween *tw, uint32_t now)
{
    return !s_reduced && now - tw->t0 < tw->dur;
}

/* ---- odometer roll ---- */

void nx_roll_init(nx_roll *r, const char *s)
{
    memset(r, 0, sizeof *r);
    strncpy(r->to, s, NX_ROLL_MAX);
    memcpy(r->from, r->to, sizeof r->from);
    memset(r->delay, 0xFF, sizeof r->delay);
}

int nx_roll_set(nx_roll *r, const char *s, uint32_t now)
{
    char next[NX_ROLL_MAX + 1] = { 0 };
    strncpy(next, s, NX_ROLL_MAX);
    if (strcmp(next, r->to) == 0)
        return 0;
    if (now - r->win_t0 >= 1000u) {       /* live rule: a new 1 s window; stays live only after a busy one */
        r->live = (uint8_t)(now - r->win_t0 < 2000u && r->win_n > NX_ROLL_LIVE_N);
        r->win_t0 = now;
        r->win_n = 0;
    }
    if (++r->win_n > NX_ROLL_LIVE_N)
        r->live = 1;
    size_t n = strlen(next);
    memcpy(r->from, r->to, sizeof r->from);
    memcpy(r->to, next, sizeof r->to);
    memset(r->delay, 0xFF, sizeof r->delay);
    r->t0 = now;
    r->rolling = 0;
    if (r->live || s_reduced || strlen(r->from) != n) {   /* instant swap (or a new layout) */
        memcpy(r->from, r->to, sizeof r->from);
        return 1;
    }
    unsigned k = 0;
    for (size_t i = n; i-- > 0;) {        /* right to left */
        if (r->from[i] != r->to[i] && r->to[i] >= '0' && r->to[i] <= '9' && r->from[i] >= '0' && r->from[i] <= '9') {
            r->delay[i] = (uint8_t)k++;
            r->rolling = 1;
        }
    }
    return 1;
}

static int32_t cell_elapsed(const nx_roll *r, unsigned i, uint32_t now)
{
    return (int32_t)(now - r->t0) - (int32_t)(r->delay[i] * NX_ROLL_STAGGER_MS);
}

uint16_t nx_roll_cell_p(const nx_roll *r, unsigned i, uint32_t now)
{
    if (i >= NX_ROLL_MAX || r->delay[i] == 0xFF || !r->rolling)
        return 65535u;
    int32_t t = cell_elapsed(r, i, now);
    if (t <= 0)
        return 0;
    return nx_anim_p(0, (uint32_t)t, NX_ROLL_MS, NX_EASE_OUT);
}

unsigned nx_roll_cell_tint(const nx_roll *r, unsigned i, uint32_t now)
{
    if (i >= NX_ROLL_MAX || r->delay[i] == 0xFF || !r->rolling)
        return 0;
    int32_t t = cell_elapsed(r, i, now);
    if (t <= 0 || t >= (int32_t)NX_TINT_MS)
        return 0;
    return 32u - (uint32_t)t * 32u / NX_TINT_MS;
}

int nx_roll_active(nx_roll *r, uint32_t now)
{
    if (!r->rolling)
        return 0;
    unsigned last = 0;
    for (unsigned i = 0; i < NX_ROLL_MAX; i++)
        if (r->delay[i] != 0xFF && r->delay[i] > last)
            last = r->delay[i];
    if (now - r->t0 < last * NX_ROLL_STAGGER_MS + NX_TINT_MS)
        return 1;
    r->rolling = 0;
    memcpy(r->from, r->to, sizeof r->from);
    return 0;
}

void nx_roll_draw(nx_gc *gc, const nx_roll *r, const nx_font_t *f, int16_t x, int16_t y, uint32_t now,
                  uint16_t c_text, uint16_t c_accent)
{
    int16_t cx[NX_ROLL_MAX + 1];
    unsigned n = nx_text_cells(f, r->to, x, cx, NX_ROLL_MAX);
    for (unsigned i = 0; i < n; i++) {
        uint16_t c = nx_mix(c_text, c_accent, nx_roll_cell_tint(r, i, now));
        uint16_t p = nx_roll_cell_p(r, i, now);
        if (p == 65535u) {
            nx_text_n(gc, f, cx[i], y, &r->to[i], 1, c);
            continue;
        }
        int16_t off = (int16_t)(((int32_t)f->height * p) >> 16);
        nx_rect prev = nx_clip_push(gc, (nx_rect){ cx[i], y, cx[i + 1], (int16_t)(y + f->height) });
        nx_text_n(gc, f, cx[i], (int16_t)(y - off), &r->from[i], 1, c);
        nx_text_n(gc, f, cx[i], (int16_t)(y + f->height - off), &r->to[i], 1, c);
        nx_clip_pop(gc, prev);
    }
}
