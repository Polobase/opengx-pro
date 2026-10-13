/* Settings > About > Support the project (NX_FEAT_SUPPORT): the QR code of the project's Buy Me a
 * Coffee page with the address in words. The code is a fixed table from the asset tool
 * (assets/nx/qr.json -> nx_qr_support), drawn black on a white card with the standard's quiet zone
 * of 4 modules, in every theme. No auto dim while it shows.
 *
 * Keys: STOP or START back. The screen opens nothing: it sits on the last level of the screen
 * stack (dashboard, Settings, About, this). */
#include "nx_comp.h"
#include "nx_config.h"

#if NX_FEAT_SUPPORT

#define QR_N ((int16_t)NX_QR_SUPPORT_N)
#define QR_QUIET 4   /* modules of white around the code */
#define QR_ROW ((NX_QR_SUPPORT_N + 7u) / 8u)

#define QR_PX 4      /* pixels per module: the card is 148 px, the words stand beside it */
#define QR_X NX_M
#define QR_Y (NX_BAR_H + (NX_HINT_Y - NX_BAR_H - (QR_N + 2 * QR_QUIET) * QR_PX) / 2)
#define QR_SIDE ((QR_N + 2 * QR_QUIET) * QR_PX)

static int dark(unsigned row, unsigned col)
{
    return nx_qr_support[row * QR_ROW + (col >> 3)] >> (7u - (col & 7u)) & 1u;
}

/* The card and the code: one fill per run of dark modules; rows outside the band are skipped. */
static void qr_draw(nx_gc *gc)
{
    nx_fill(gc, nx_rect_xywh(QR_X, QR_Y, QR_SIDE, QR_SIDE), 0xFFFFu);
    for (unsigned r = 0; r < NX_QR_SUPPORT_N; r++) {
        int16_t y = (int16_t)(QR_Y + (QR_QUIET + (int16_t)r) * QR_PX);
        if (y + gc->oy >= gc->clip.y1 || y + gc->oy + QR_PX <= gc->clip.y0)
            continue;
        for (unsigned c = 0; c < NX_QR_SUPPORT_N; c++) {
            if (!dark(r, c))
                continue;
            unsigned c0 = c;
            while (c + 1u < NX_QR_SUPPORT_N && dark(r, c + 1u))
                c++;
            nx_fill(gc, nx_rect_xywh((int16_t)(QR_X + (QR_QUIET + (int16_t)c0) * QR_PX), y,
                                     (int16_t)((c - c0 + 1u) * QR_PX), QR_PX), 0x0000u);
        }
    }
}

static void support_draw(nx_gc *gc, const void *st)
{
    (void)st;
    nx_comp_title(gc, nx_str(NX_S_ABOUT_SUPPORT), NX_LCD_W - NX_M, 1);
    qr_draw(gc);
    int16_t x = QR_X + QR_SIDE + NX_M;
    const char *url = nx_str(NX_S_SUPPORT_URL);
    size_t cut = 0;   /* the address on two lines: after its "/" */
    while (url[cut] && url[cut] != '/')
        cut++;
    cut += url[cut] == '/';
    nx_text(gc, &nx_font_body16, x, (int16_t)(QR_Y + 14), nx_str(NX_S_SUPPORT_COFFEE), nx_col(gc, NX_PAL_TEXT1));
    nx_text(gc, &nx_font_cap13, x, (int16_t)(QR_Y + 50), nx_str(NX_S_SUPPORT_SCAN), nx_col(gc, NX_PAL_TEXT2));
    nx_text_n(gc, &nx_font_cap13, x, (int16_t)(QR_Y + 72), url, cut, nx_col(gc, NX_PAL_TEXT1));
    nx_text(gc, &nx_font_cap13, x, (int16_t)(QR_Y + 90), url + cut, nx_col(gc, NX_PAL_TEXT1));
    nx_text(gc, &nx_font_cap13, x, (int16_t)(QR_Y + 122), nx_str(NX_S_SUPPORT_THANKS), nx_col(gc, NX_PAL_TEXT2));
}

static int support_key(void *st, const struct nx_kev *e)
{
    (void)st;
    if (e->type == NXK_PRESS && (e->keys == NX_KEY_STOP || e->keys == NX_KEY_START))
        nx_ui_pop(NX_TR_BACK);
    return 1;
}

const struct nx_scr nx_scr_support = {
    0, support_key, 0, support_draw, NX_S_HINT_SUPPORT, NX_SF_AWAKE, 0, 0, 0,
};

#endif
