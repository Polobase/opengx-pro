/* Host ST7789 model and nx_lcd_port.h (nx_panel.h). */
#include "nx_panel.h"

#include <string.h>

#include "nx_config.h"
#include "nx_lcd.h"
#include "nx_lcd_port.h"

uint16_t nxp_gram[320][240];
uint8_t nxp_madctl, nxp_sleep, nxp_disp_on, nxp_scrolling;
uint16_t nxp_vscrdef[3], nxp_vsp;
uint32_t nxp_pixels;
uint8_t nxp_log[NXP_LOG_MAX];
unsigned nxp_nlog;
unsigned nxp_bad;
int nxp_async;

static uint16_t s_win[4];   /* column start/end, row start/end */
static uint16_t s_c, s_r;
static int s_ramwr, s_busy;

void nxp_reset(void)
{
    memset(nxp_gram, 0, sizeof nxp_gram);
    nxp_madctl = 0;
    nxp_sleep = 0;
    nxp_disp_on = 1;
    nxp_scrolling = 0;
    nxp_vscrdef[0] = 0;
    nxp_vscrdef[1] = 320;
    nxp_vscrdef[2] = 0;
    nxp_vsp = 0;
    nxp_pixels = 0;
    nxp_nlog = 0;
    nxp_bad = 0;
    nxp_async = 0;
    s_win[0] = 0;
    s_win[1] = 239;
    s_win[2] = 0;
    s_win[3] = 319;
    s_c = s_r = 0;
    s_ramwr = s_busy = 0;
}

void nx_lcd_port_init(void)
{
    nxp_madctl = NX_LCD_MADCTL;
    s_ramwr = 0;
}

void nx_lcd_port_cmd(uint8_t cmd, const uint16_t *a, unsigned n)
{
    if (s_busy)
        nxp_bad++;
    if (nxp_nlog < NXP_LOG_MAX)
        nxp_log[nxp_nlog++] = cmd;
    s_ramwr = 0;
    switch (cmd) {
    case NX_ST_CASET:
    case NX_ST_RASET:
        if (n == 2) {
            unsigned k = cmd == NX_ST_CASET ? 0u : 2u;
            s_win[k] = a[0];
            s_win[k + 1] = a[1];
        }
        break;
    case NX_ST_RAMWR:
        s_c = s_win[0];
        s_r = s_win[2];
        s_ramwr = 1;
        break;
    case NX_ST_VSCRDEF:
        if (n == 3)
            memcpy(nxp_vscrdef, a, sizeof nxp_vscrdef);
        break;
    case NX_ST_VSCSAD:
        if (n == 1) {
            nxp_vsp = a[0];
            nxp_scrolling = 1;
        }
        break;
    case NX_ST_NORON:
        nxp_scrolling = 0;
        break;
    case NX_ST_SLPIN:
        nxp_sleep = 1;
        break;
    case NX_ST_SLPOUT:
        nxp_sleep = 0;
        break;
    case NX_ST_DISPON:
        nxp_disp_on = 1;
        break;
    case NX_ST_DISPOFF:
        nxp_disp_on = 0;
        break;
    default:
        break;
    }
}

/* MADCTL MY/MX/MV as the ST7789 datasheet §8.12 (and tools/sim/devices.py): MV exchanges the
 * counters, then MX / MY mirror the physical column / row. */
static void put(uint16_t v)
{
    int mv = nxp_madctl & 0x20;
    int cmax = mv ? 319 : 239, rmax = mv ? 239 : 319;
    int c = s_c, r = s_r;
    if (c <= cmax && r <= rmax) {
        int x = mv ? r : c, y = mv ? c : r;
        if (nxp_madctl & 0x40)
            x = 239 - x;
        if (nxp_madctl & 0x80)
            y = 319 - y;
        nxp_gram[y][x] = v;
    }
    nxp_pixels++;
    if (c >= s_win[1]) {
        s_c = s_win[0];
        s_r = (uint16_t)(r < s_win[3] ? r + 1 : s_win[2]);
    } else {
        s_c = (uint16_t)(c + 1);
    }
}

void nx_lcd_port_pixels(const uint16_t *px, uint32_t n, int inc)
{
    if (!s_ramwr || s_busy)
        nxp_bad++;
    for (uint32_t i = 0; i < n; i++) {
        uint16_t v = px[inc ? i : 0];
#if !NX_LCD_DS16
        v = (uint16_t)(v << 8 | v >> 8);   /* the 8-bit mode keeps pixels byte-swapped in memory */
#endif
        put(v);
    }
    if (nxp_async)
        s_busy = 1;
}

int nx_lcd_port_busy(void)
{
    return s_busy;
}

void nx_lcd_port_lock(void)
{
}

void nx_lcd_port_unlock(void)
{
}

int nxp_complete(void)
{
    if (!s_busy)
        return 0;
    s_busy = 0;
    nx_lcd_irq();
    return 1;
}

uint16_t nxp_at(int x, int y)
{
    if (!nxp_disp_on || nxp_sleep || x < 0 || x >= 320 || y < 0 || y >= 240)
        return 0;
    int row = 319 - x;   /* the glass shows physical row 319 - X at logical column X */
    unsigned tfa = nxp_vscrdef[0], vsa = nxp_vscrdef[1];
    if (nxp_scrolling && vsa && tfa + vsa <= 320u && (unsigned)row >= tfa && (unsigned)row < tfa + vsa) {
        int k = ((int)nxp_vsp - (int)tfa + (row - (int)tfa)) % (int)vsa;   /* the area starts at row VSP */
        row = (int)tfa + (k < 0 ? k + (int)vsa : k);
    }
    return nxp_gram[row][y];
}

void nxp_glass(uint16_t *out)
{
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < 320; x++)
            out[y * 320 + x] = nxp_at(x, y);
}
