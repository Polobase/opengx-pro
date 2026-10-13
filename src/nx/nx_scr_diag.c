/* Diagnostics (menu > Diagnostics, NX_FEAT_DIAG) and the key test. The values come from
 * nx_uc_diag once a second: firmware and panel, uptime and boots, reset reason and faults, the
 * two generator links (lines sent, errors, :ok latency), PC-mode entries / exits and the :w91
 * lines held after a :w92, the SPI flash, the main loop and the MCU's temperature and supply.
 *
 * Diagnostics keys: UP/DOWN scroll, LOOP the key test, START refresh, STOP back.
 * Key test: every key event is shown (the keys act on nothing), STOP held 1.5 s leaves. */
#include "nx_config.h"

#if NX_FEAT_DIAG
#include <string.h>

#include "nx_comp.h"
#include "nx_fault.h"
#include "nx_store.h"
#include "nx_uifmt.h"

#define DROW_H 26

enum { D_FW, D_UPTIME, D_RESET, D_LINK1, D_LINK2, D_PC, D_FLASH, D_LOOP, D_MCU, D_N };

static struct nx_diag_vm s_d;   /* read once a second */
static uint32_t s_d_t;

struct diag {
    struct nx_list l;
};

static char *sep(char *p)
{
    *p++ = ' ';
    *p++ = (char)NX_CH_middot;
    *p++ = ' ';
    *p = 0;
    return p;
}

static char *put(char *p, const char *s)
{
    while (*s)
        *p++ = *s++;
    *p = 0;
    return p;
}

/* "31.2" / "-4.5" from tenths; "3.30" from mV */
static char *fmt_t10(char *p, int32_t t10)
{
    if (t10 < 0) {
        *p++ = '-';
        t10 = -t10;
    }
    p = nx_fmt_u(p, (uint32_t)t10 / 10u);
    *p++ = '.';
    *p++ = (char)('0' + (uint32_t)t10 % 10u);
    *p = 0;
    return p;
}

static void diag_value(unsigned row, char *v)
{
    static const uint16_t k_reset[8] = { NX_S_DIAG_RESET_UNKNOWN, NX_S_DIAG_RESET_POWER, NX_S_DIAG_RESET_PIN,
                                         NX_S_DIAG_RESET_SOFTWARE, NX_S_DIAG_RESET_WATCHDOG, NX_S_DIAG_RESET_FAULT,
                                         NX_S_DIAG_RESET_UNKNOWN, NX_S_DIAG_RESET_UNKNOWN };
    char a[12], b[12], c[12];
    const char *args[3] = { a, b, c };
    char *p = v;
    *p = 0;
    switch (row) {
    case D_FW:
        p = sep(put(p, NX_FW_VERSION));
        p = put(p, "ST7789 ");
        *p++ = s_d.panel;
        *p = 0;
        break;
    case D_UPTIME:
        p = sep(nx_fmt_dur(p, s_d.uptime_s));
        nx_fmt_u(a, s_d.boots);
        nx_str_format(p, 32, NX_S_DIAG_BOOT_N, args, 1);
        break;
    case D_RESET:
        p = put(p, nx_str(k_reset[s_d.reset_reason & 7u]));
        if (s_d.crashes) {
            nx_fmt_u(a, s_d.crashes);
            p = sep(p);
            p += nx_str_format(p, 32, NX_S_DIAG_FAULTS_N, args, 1);
        }
        if (s_d.safe_mode)
            put(sep(p), nx_str(NX_S_DIAG_SAFE_MODE));
        break;
    case D_LINK1:
    case D_LINK2: {   /* the link's state is the dot in front (diag_row) */
        unsigned ch = row == D_LINK2;
        nx_fmt_u(a, s_d.sent[ch]);
        nx_fmt_u(b, s_d.timeouts[ch] + s_d.uart_errors[ch]);
        char *q = nx_fmt_u(c, s_d.lat_avg_us[ch] / 1000u);
        *q++ = '.';
        *q++ = (char)('0' + s_d.lat_avg_us[ch] / 100u % 10u);
        *q = 0;
        nx_str_format(p, 64, NX_S_DIAG_LINK_LINE, args, 3);
        break;
    }
    case D_PC:
        nx_fmt_u(a, s_d.pc_entries);
        nx_fmt_u(b, s_d.pc_exits);
        nx_fmt_u(c, s_d.w91_held);
        nx_str_format(p, 64, NX_S_DIAG_PC_LINE, args, 3);
        break;
    case D_FLASH:   /* the store's state is the dot in front, spelled out unless it is OK */
        p = sep(nx_fmt_hex(p, (uint32_t)s_d.jedec[0] << 16 | (uint32_t)s_d.jedec[1] << 8 | s_d.jedec[2], 6));
        if (s_d.store_state == NX_STORE_EMPTY || s_d.store_state == NX_STORE_READONLY)
            p = sep(put(p, nx_str(s_d.store_state == NX_STORE_EMPTY ? NX_S_DIAG_STORE_EMPTY : NX_S_DIAG_STORE_RO)));
        nx_fmt_u(a, s_d.kv_free / 1024u);
        nx_fmt_u(b, s_d.flash_errors);
        nx_str_format(p, 48, NX_S_DIAG_FLASH_LINE, args, 2);
        break;
    case D_LOOP:
        nx_fmt_u(a, s_d.loop_gap_ms);
        nx_str_format(p, 48, NX_S_DIAG_LOOP_LINE, args, 1);
        break;
    default:
        if (!s_d.sensors) {
            put(p, "-");
            break;
        }
        fmt_t10(a, s_d.t10);
        nx_fmt_u(b, s_d.vdd_mv / 1000u);
        b[1] = '.';
        b[2] = (char)('0' + s_d.vdd_mv / 100u % 10u);
        b[3] = (char)('0' + s_d.vdd_mv / 10u % 10u);
        b[4] = 0;
        nx_str_format(p, 64, NX_S_DIAG_MCU_LINE, args, 2);
        break;
    }
}

static void diag_row(nx_gc *gc, const void *ctx, unsigned i, nx_rect r, int sel, uint32_t sel_ms)
{
    (void)ctx;
    (void)sel;
    (void)sel_ms;
    static const uint16_t k_label[D_N] = { NX_S_DIAG_L_FW,   NX_S_DIAG_L_UPTIME, NX_S_DIAG_L_RESET,
                                           NX_S_DIAG_L_LINK, NX_S_DIAG_L_LINK,   NX_S_DIAG_L_PC,
                                           NX_S_DIAG_L_FLASH, NX_S_DIAG_L_LOOP,  NX_S_DIAG_L_MCU };
    char label[32], v[96];
    const char *args[1] = { nx_str(i == D_LINK2 ? NX_S_GEN_G2 : NX_S_GEN_G1) };
    nx_str_format(label, sizeof label, k_label[i], args, 1);
    int16_t ty = (int16_t)(r.y0 + 4), vx = 108;
    nx_text_fit(gc, &nx_font_cap13, NX_M, ty, 92, label, nx_col(gc, NX_PAL_TEXT3));
    diag_value(i, v);
    if (i == D_LINK1 || i == D_LINK2 || i == D_FLASH) {   /* green: fine, red: not, grey: nothing yet */
        uint8_t tok = i == D_FLASH ? (s_d.store_state == NX_STORE_OK ? NX_PAL_SUCCESS
                                      : s_d.store_state == NX_STORE_EMPTY ? NX_PAL_TEXT3 : NX_PAL_WARNING)
                    : s_d.link_down[i == D_LINK2] ? NX_PAL_ERROR : NX_PAL_SUCCESS;
        nx_disc(gc, (vx + 4) * 16, (ty + 9) * 16, 4 * 16, nx_col(gc, tok));
        vx = (int16_t)(vx + 14);
    }
    nx_text_fit(gc, &nx_font_cap13, vx, ty, (int16_t)(NX_LCD_W - NX_M - vx), v, nx_col(gc, NX_PAL_TEXT1));
}

static void refresh(void)
{
    nx_uc_diag(&s_d);
    s_d_t = nx_ms();
}

static void diag_enter(void *st, uint32_t arg)
{
    (void)arg;
    struct diag *d = st;
    refresh();
    nx_list_init(&d->l, NX_BAR_H + 4, DROW_H, 7, D_N, 0, nx_ms());
}

static void diag_draw(nx_gc *gc, const void *st)
{
    const struct diag *d = st;
    nx_comp_title(gc, nx_str(NX_S_DIAG_TITLE), NX_LCD_W - NX_M, 1);
    nx_list_draw(gc, &d->l, NX_PAL_FOCUS, diag_row, d);
}

static int diag_key(void *st, const struct nx_kev *e)
{
    struct diag *d = st;
    if (nx_list_key(&d->l, e, nx_ms()))
        return 1;
    if (e->type != NXK_PRESS)
        return 1;
    switch (e->keys) {
    case NX_KEY_LOOP:
        nx_ui_push(&nx_scr_keytest, 0, NX_TR_PUSH);
        break;
    case NX_KEY_START:
        refresh();
        nx_inval(nx_list_view(&d->l));
        break;
    case NX_KEY_STOP:
        nx_ui_pop(NX_TR_BACK);
        break;
    default:
        break;
    }
    return 1;
}

static void diag_tick(void *st, uint32_t now)
{
    struct diag *d = st;
    nx_list_tick(&d->l, now);
    if (now - s_d_t >= 1000u) {
        refresh();
        nx_inval(nx_list_view(&d->l));
    }
}

const struct nx_scr nx_scr_diag = {
    diag_enter, diag_key, diag_tick, diag_draw, NX_S_HINT_DIAG, 0, NX_KEY_UP | NX_KEY_DOWN, 0, sizeof(struct diag),
};

/* ---- key test ---- */

struct keytest {
    uint8_t down, last_type, last_keys;
    uint8_t rsv;
    uint16_t last_n;
};

static const struct {
    uint8_t key;
    uint16_t label;
    int16_t x, y;
} k_caps[8] = {
    { NX_KEY_G1, NX_S_KEY_G1, 16, 32 },      { NX_KEY_G2, NX_S_KEY_G2, 116, 32 },
    { NX_KEY_POWER, NX_S_KEY_POWER, 216, 32 }, { NX_KEY_UP, NX_S_KEY_UP, 66, 82 },
    { NX_KEY_DOWN, NX_S_KEY_DOWN, 166, 82 }, { NX_KEY_LOOP, NX_S_KEY_LOOP, 16, 132 },
    { NX_KEY_START, NX_S_KEY_START, 116, 132 }, { NX_KEY_STOP, NX_S_KEY_STOP, 216, 132 },
};

#define KT_LINE_Y 186

static nx_rect cap_rect(unsigned i)
{
    return (nx_rect){ k_caps[i].x, k_caps[i].y, (int16_t)(k_caps[i].x + 88), (int16_t)(k_caps[i].y + 40) };
}

static void kt_draw(nx_gc *gc, const void *st)
{
    const struct keytest *k = st;
    nx_comp_title(gc, nx_str(NX_S_DIAG_KEY_TEST), NX_LCD_W - NX_M, 1);
    for (unsigned i = 0; i < 8u; i++) {
        nx_rect r = cap_rect(i);
        int on = (k->down & k_caps[i].key) != 0;
        if (on)
            nx_rrect(gc, r, 10, nx_col(gc, NX_PAL_FOCUS));
        else
            nx_rrect_line(gc, r, 10, 2, nx_col(gc, NX_PAL_OUTLINE));
        nx_text_c(gc, &nx_font_body16, (int16_t)(r.x0 + 44), (int16_t)(r.y0 + 10), nx_str(k_caps[i].label),
                  nx_col(gc, on ? NX_PAL_ON_ACCENT : NX_PAL_TEXT1));
    }
    if (!k->last_type)
        return;
    static const uint16_t k_ev[7] = { 0, NX_S_KT_PRESS, NX_S_KT_RELEASE, NX_S_KT_HOLD, NX_S_KT_LONG, NX_S_KT_REPEAT,
                                      NX_S_KT_CHORD };
    char keys[48], n[8], line[80];
    keys[0] = 0;
    for (unsigned i = 0; i < 8u; i++)
        if (k->last_keys & k_caps[i].key) {
            if (keys[0])
                strcat(keys, "+");
            strcat(keys, nx_str(k_caps[i].label));
        }
    nx_fmt_u(n, k->last_n);
    const char *args[2] = { keys, n };
    nx_str_format(line, sizeof line, k_ev[k->last_type % 7u], args, 2);
    nx_text_c(gc, &nx_font_body16, NX_LCD_W / 2, KT_LINE_Y, line, nx_col(gc, NX_PAL_TEXT1));
}

static int kt_key(void *st, const struct nx_kev *e)
{
    struct keytest *k = st;
    k->last_type = e->type;
    k->last_keys = e->keys;
    k->last_n = e->n;
    nx_inval((nx_rect){ 0, KT_LINE_Y - 2, NX_LCD_W, NX_HINT_Y });
    if (e->keys == NX_KEY_STOP && e->type == NXK_LONG)
        nx_ui_pop(NX_TR_BACK);
    return 1;
}

static void kt_tick(void *st, uint32_t now)
{
    (void)now;
    struct keytest *k = st;
    uint8_t down = nx_keys_down(), ch = (uint8_t)(down ^ k->down);
    for (unsigned i = 0; i < 8u; i++)
        if (ch & k_caps[i].key)
            nx_inval(cap_rect(i));
    k->down = down;
}

const struct nx_scr nx_scr_keytest = {
    0, kt_key, kt_tick, kt_draw, NX_S_HINT_KEYTEST, NX_SF_RAW | NX_SF_AWAKE, 0, 0, sizeof(struct keytest),
};
#endif
