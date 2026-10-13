/* PC monitor (docs/nx/design/ui.md W19), the only screen of PC mode: what the PC set, from the
 * mirrors the protocol core already keeps (the names of slots 0 and 31, the four output
 * frequencies from w24/w25, the outputs from w11, Out2 following Out1 from w14). This screen
 * never transmits: START, STOP and LOOP do nothing, as in the original (the core refuses
 * generator actions in PC mode anyway); UP/DOWN set the brightness; POWER works as everywhere.
 * As the original: the two names take turns scrolling, frequencies show up to 8 decimals (a
 * smaller font when the big digits do not fit), a synced Out2 shows Out1's value. A changed
 * frequency redraws its line (no roll), at most every PC_REDRAW_MS, so a fast sweep reads as a
 * sweep. nx adds what the PC set per output: the waveforms and the gating at the end of the
 * generator's name line (the name scrolls in the rest), the amplitude under "OUT 1" / "OUT 2",
 * and the output frequency of a wave table with several cycles: the PC sends the frequency
 * divided by their number (Spooky2's Wave Cycle Multiplier), the line shows the product and the
 * number ("396.00 Hz x11"). */
#include <string.h>

#include "nx_comp.h"
#include "nx_uifmt.h"

#define BLOCK_Y(ch) ((int16_t)(44 + (ch) * 86))
#define FREQ_X 92

#define NAME_X 52
#define NAME_W 256
#define FREQ_BIG_W 170   /* the big digits' room */
#define INFO_W 168       /* the most the waveforms take of the name line; a longer text ends in "..." */
#define INFO_MAX 48      /* "Inverse sawtooth / Damped square · gated" and the end */
#define PC_REDRAW_MS 100u
#define ROW_H 26         /* what a frequency line covers: the digits, and the amplitude under its label */

struct pc {
    uint8_t turn;       /* the name that scrolls now (0: G1's slot 0, 1: G2's slot 31) */
    uint8_t dirty;      /* bit ch * 2 + out: that line changed and waits for its redraw */
    int16_t mq_last;
    uint32_t t0;        /* its scroll started */
    uint32_t fr_t;      /* the last redraw of changed lines */
};

/* "Sine / Inverted · gated": the waveforms the PC set for Out1 and, when it is another one,
 * Out2, and the gating (empty: nothing yet) */
static void info_text(const struct nx_sys_vm *s, char *out, unsigned ch)
{
    char *p = out;
    *p = 0;
    for (unsigned o = 0; o < 2u; o++) {
        unsigned seen = s->pc_seen[ch];
        if (!(seen & (o ? NX_PCS_WAVE2 : NX_PCS_WAVE1)))
            continue;
        if (o && (seen & NX_PCS_WAVE1)) {
            if (s->pc_wave[ch][1] == s->pc_wave[ch][0])
                continue;
            *p++ = ' ';
            *p++ = '/';
            *p++ = ' ';
        }
        struct nx_wave_info wi;
        if (nx_wave_info(s->pc_wave[ch][o], &wi) && wi.name != NX_WAVE_NO_NAME) {
            strcpy(p, nx_str(wi.name));
            p += strlen(p);
        } else {
            *p++ = '#';
            p = nx_fmt_u(p, s->pc_wave[ch][o]);
        }
    }
    if ((s->pc_gate >> ch) & 1u) {
        if (p != out) {
            *p++ = ' ';
            *p++ = (char)NX_CH_middot;
            *p++ = ' ';
        }
        strcpy(p, nx_str(NX_S_PC_GATED));
    }
}

/* the info's width on the name line (0: none), at most INFO_W */
static int16_t info_w(const struct nx_sys_vm *s, unsigned ch, char *info)
{
    info_text(s, info, ch);
    int16_t w = (int16_t)nx_font_text_width(&nx_font_cap13, info);
    return w > INFO_W ? INFO_W : w;
}

/* the name's box: what the info leaves of the line */
static int16_t name_w(const struct nx_sys_vm *s, unsigned ch)
{
    char info[INFO_MAX];
    int16_t w = info_w(s, ch, info);
    return (int16_t)(w ? NAME_W - 12 - w : NAME_W);
}

static void pc_name(unsigned ch, char *name)
{
    if (!nx_uc_name(ch ? 31u : 0u, name))
        strcpy(name, nx_str(NX_S_PC_NO_NAME));
}

static void pc_enter(void *st, uint32_t arg)
{
    (void)arg;
    ((struct pc *)st)->t0 = nx_ms();
}

static void block(nx_gc *gc, const struct pc *p, unsigned ch)
{
    const struct nx_sys_vm *s = &g_vm_draw.s;
    int16_t y = BLOCK_Y(ch);
    char name[NX_UC_NAME_MAX + 1u], buf[INFO_MAX];   /* the info line, a frequency, an amplitude */
    nx_comp_badge(gc, NX_M, (int16_t)(y + 2), ch, 1);
    pc_name(ch, name);
    int16_t iw = info_w(s, ch, buf), nw = (int16_t)(iw ? NAME_W - 12 - iw : NAME_W);
    nx_text_box(gc, &nx_font_body16, (nx_rect){ NAME_X, y, (int16_t)(NAME_X + nw), (int16_t)(y + 20) }, name,
                nx_col(gc, NX_PAL_TEXT1), p->turn == ch, nx_ui_now() - p->t0);
    if (iw)
        nx_text_fit(gc, &nx_font_cap13, (int16_t)(NAME_X + NAME_W - iw), (int16_t)(y + 3), iw, buf,
                    nx_col(gc, NX_PAL_TEXT2));
    for (unsigned out = 0; out < 2u; out++) {
        int16_t ry = (int16_t)(y + 26 + out * 30);
        int on = (s->pc_out >> (ch * 2u + out)) & 1u;
        uint16_t acc = nx_col(gc, nx_accent(ch));
        if (on)
            nx_disc(gc, (NX_M + 6) * 16, (ry + 12) * 16, 5 * 16, acc);
        else
            nx_ring(gc, NX_M + 6, ry + 12, 6, 2, 0, 1024, 0, nx_col(gc, NX_PAL_TEXT3));
        int ampl = s->pc_seen[ch] & (out ? NX_PCS_AMPL2 : NX_PCS_AMPL1);   /* its amplitude under the label */
        nx_text(gc, &nx_font_cap13, 28, (int16_t)(ry + (ampl ? 0 : 3)), nx_str(out ? NX_S_GEN_OUT2 : NX_S_GEN_OUT1),
                nx_col(gc, NX_PAL_TEXT2));
        if (ampl) {
            nx_fmt_cv(buf, s->pc_ampl[ch][out]);
            nx_text(gc, &nx_font_cap13, 28, (int16_t)(ry + 12), buf, nx_col(gc, NX_PAL_TEXT3));
        }
        int sync = out == 1u && ((s->pc_sync >> ch) & 1u);   /* Out2 follows Out1: its value */
        unsigned mult = s->pc_mult[ch][sync ? 0u : out];
        nx_fmt_pc_freq(buf, s->pc_freq[ch][sync ? 0u : out], mult);
        char tag[8];   /* after "Hz": "= OUT 1", or the cycles ("x11") */
        tag[0] = (char)NX_CH_times;
        nx_fmt_u(tag + 1, mult);
        const char *note = sync ? nx_str(NX_S_PC_SYNC) : mult > 1u ? tag : "";
        const char *hz = nx_str(NX_S_UNIT_HZ);
        /* the big digits when they, "Hz" and the note end at the margin */
        int room = NX_LCD_W - NX_M - FREQ_X - 6 - (int)nx_font_text_width(&nx_font_cap13, hz);
        if (note[0])
            room -= 8 + (int)nx_font_text_width(&nx_font_cap13, note);
        if (room > FREQ_BIG_W)
            room = FREQ_BIG_W;
        uint16_t c = nx_col(gc, on ? NX_PAL_TEXT1 : NX_PAL_TEXT3);
        int16_t x;
        if ((int)nx_font_text_width(&nx_font_num28, buf) <= room)
            x = nx_text(gc, &nx_font_num28, FREQ_X, ry, buf, c);
        else
            x = nx_text(gc, &nx_font_body16, FREQ_X, (int16_t)(ry + 5), buf, c);
        x = nx_text(gc, &nx_font_cap13, (int16_t)(x + 6), (int16_t)(ry + 7), hz, nx_col(gc, NX_PAL_TEXT2));
        nx_text(gc, &nx_font_cap13, (int16_t)(x + 8), (int16_t)(ry + 7), note, nx_col(gc, NX_PAL_TEXT3));
    }
}

static void pc_draw(nx_gc *gc, const void *st)
{
    const struct pc *p = st;
    nx_fill(gc, (nx_rect){ 0, 0, NX_LCD_W, NX_BAR_H }, nx_col(gc, NX_PAL_SURFACE1));
    nx_icon(gc, &nx_icons20, NX_I20_USB, NX_M, 1, nx_col(gc, NX_PAL_TEXT1));
    nx_text(gc, &nx_font_cap13, 38, 1, nx_str(NX_S_STATE_PC_MODE), nx_col(gc, NX_PAL_TEXT1));
    block(gc, p, 0);
    nx_fill(gc, (nx_rect){ NX_M, 128, 308, 129 }, nx_col(gc, NX_PAL_OUTLINE));
    block(gc, p, 1);
}

/* The names take turns: the one whose turn it is scrolls once through (a name that fits passes
 * its turn at once), then the other one. */
static void names_tick(struct pc *p, uint32_t now)
{
    char name[NX_UC_NAME_MAX + 1u];
    pc_name(p->turn, name);
    int16_t w = (int16_t)nx_font_text_width(&nx_font_body16, name), nw = name_w(&g_vm.s, p->turn);
    int16_t y = BLOCK_Y(p->turn);
    nx_rect r = { NAME_X, y, (int16_t)(NAME_X + nw), (int16_t)(y + 20) };
    uint32_t cycle = w > nw ? 1000u + (uint32_t)(w + NX_MARQUEE_GAP) * 25u : 0u;
    if (now - p->t0 >= cycle) {
        char other[NX_UC_NAME_MAX + 1u];
        pc_name(p->turn ^ 1u, other);
        if (!cycle && nx_font_text_width(&nx_font_body16, other) <= (unsigned)name_w(&g_vm.s, p->turn ^ 1u))
            return;   /* both fit: nothing scrolls */
        p->turn ^= 1u;
        p->t0 = now;
        p->mq_last = 0;
        nx_inval(r);
        return;
    }
    nx_marquee_tick(&p->mq_last, w, nw, now - p->t0, r);
}

static void pc_tick(void *st, uint32_t now)
{
    struct pc *p = st;
    names_tick(p, now);
    const struct nx_sys_vm *s = &g_vm.s, *o = &g_vm_seen.s;
    for (unsigned ch = 0; ch < 2u; ch++) {
        int16_t y = BLOCK_Y(ch);
        int seen = s->pc_seen[ch] != o->pc_seen[ch];
        if (seen || s->name_sum[2u + ch] != o->name_sum[2u + ch] || memcmp(s->pc_wave[ch], o->pc_wave[ch], 2)
            || (((s->pc_gate ^ o->pc_gate) >> ch) & 1u))
            nx_inval((nx_rect){ NAME_X, y, NAME_X + NAME_W, (int16_t)(y + 20) });
        if (((s->pc_sync ^ o->pc_sync) >> ch) & 1u)
            nx_inval((nx_rect){ 0, (int16_t)(y + 56), NX_LCD_W, (int16_t)(y + 56 + ROW_H) });
        for (unsigned out = 0; out < 2u; out++) {
            int line = (((s->pc_out ^ o->pc_out) >> (ch * 2u + out)) & 1u) || s->pc_freq[ch][out] != o->pc_freq[ch][out]
                       || (out == 1u && s->pc_freq[ch][0] != o->pc_freq[ch][0] && ((s->pc_sync >> ch) & 1u))
                       || seen || s->pc_ampl[ch][out] != o->pc_ampl[ch][out];
            if (line)
                p->dirty = (uint8_t)(p->dirty | 1u << (ch * 2u + out));
        }
    }
    if (memcmp(s->pc_mult, o->pc_mult, sizeof s->pc_mult))   /* another table: every line */
        p->dirty = 0x0Fu;
    if (!p->dirty || now - p->fr_t < PC_REDRAW_MS)
        return;
    for (unsigned i = 0; i < 4u; i++)
        if ((p->dirty >> i) & 1u) {
            int16_t ry = (int16_t)(BLOCK_Y(i >> 1) + 26 + (i & 1u) * 30);
            nx_inval((nx_rect){ 0, ry, NX_LCD_W, (int16_t)(ry + ROW_H) });
        }
    p->dirty = 0;
    p->fr_t = now;
}

static int pc_key(void *st, const struct nx_kev *e)
{
    (void)st;
    if ((e->type != NXK_PRESS && e->type != NXK_REPEAT) || (e->keys != NX_KEY_UP && e->keys != NX_KEY_DOWN))
        return 1;
    int d = e->keys == NX_KEY_UP ? 1 : -1;
    if ((int)g_uiset.bright + d >= 1 && (int)g_uiset.bright + d <= 10) {
        char v[8];
        g_uiset.bright = (uint8_t)(g_uiset.bright + d);
        nx_ui_apply();
        nx_ui_settings_dirty();   /* saved after PC mode */
        nx_fmt_pct(v, g_uiset.bright * 10u);
        nx_ui_toast_s(NX_I20_BRIGHTNESS, NX_PAL_TEXT2, NX_S_PC_BRIGHTNESS, v, 0);
    }
    return 1;
}

const struct nx_scr nx_scr_pc = {
    pc_enter, pc_key, pc_tick, pc_draw, NX_S_PC_WATCH_ONLY, NX_SF_AWAKE, NX_KEY_UP | NX_KEY_DOWN, 0,
    sizeof(struct pc),
};
