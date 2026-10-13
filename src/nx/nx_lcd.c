/* nx panel driver (nx_lcd.h): the transfer queue, the band buffers and the power states. Portable:
 * the bus is nx_lcd_port.h. The queue is a ring of NX_LCD_QUEUE entries; the head entry is the
 * one in flight. pump() advances it from the main context (DMA interrupt masked) or from the DMA
 * interrupt, so the next window goes out within microseconds of the previous transfer's end
 * while the CPU draws the next band. */
#include "nx_lcd.h"

#include "nx_lcd_port.h"
#include "nx_time.h"

/* DMA: one transfer carries up to 65535 pixels. Polled: one call sends at most one band's worth
 * (1.28 ms of SPI), so a render slice never holds the main loop longer. */
#define CHUNK (NX_LCD_DMA ? 65535u : NX_BAND_PX)
#define SYNC_MAX_US 200000u   /* nx_lcd_sync: longer than any queue (a polled full frame: 90 ms),
                                 far below the 2 s watchdog */
#define SYNC_SPINS 4000000u   /* ... and a bound that holds even if the microsecond clock stood still */

enum { E_BAND = 1, E_FILL, E_SCROLL, E_CMD };
enum { BUF_FREE = 0, BUF_OWNED, BUF_QUEUED };

struct entry {
    uint8_t kind;
    uint8_t arg8;               /* E_CMD: the command; E_BAND: buffer index */
    uint16_t arg;               /* E_FILL: the halfword the transfer repeats; E_SCROLL: VSCSAD */
    nx_rect r;                  /* E_BAND, E_FILL: window */
    const uint16_t *src;        /* E_BAND: next pixel to send */
    uint32_t left;              /* pixels still to send */
};

struct nx_lcd_stats g_nx_lcd_stats;

static uint16_t s_buf[2][NX_BAND_PX];
static volatile uint8_t s_buf_state[2];
static struct entry s_q[NX_LCD_QUEUE];
static volatile uint8_t s_head, s_count, s_active;
static uint16_t s_scroll;
static uint8_t s_pw, s_pw_target, s_awake_wait;
static uint32_t s_pw_t;

static void window(const nx_rect *r)
{
    uint16_t c[2] = { (uint16_t)r->x0, (uint16_t)(r->x1 - 1) };
    uint16_t w[2] = { (uint16_t)r->y0, (uint16_t)(r->y1 - 1) };
    nx_lcd_port_cmd(NX_ST_CASET, c, 2);
    nx_lcd_port_cmd(NX_ST_RASET, w, 2);
    nx_lcd_port_cmd(NX_ST_RAMWR, 0, 0);
}

static void begin(struct entry *e)
{
    switch (e->kind) {
    case E_BAND:
    case E_FILL:
        window(&e->r);
        e->left = (uint32_t)(e->r.x1 - e->r.x0) * (uint32_t)(e->r.y1 - e->r.y0);
        g_nx_lcd_stats.pixels += e->left;
        if (e->kind == E_BAND)
            g_nx_lcd_stats.bands++;
        else
            g_nx_lcd_stats.fills++;
        break;
    case E_SCROLL:
        nx_lcd_port_cmd(NX_ST_VSCSAD, &e->arg, 1);
        e->left = 0;
        g_nx_lcd_stats.scrolls++;
        break;
    default:
        nx_lcd_port_cmd(e->arg8, 0, 0);
        e->left = 0;
        g_nx_lcd_stats.cmds++;
        break;
    }
}

static void send_chunk(struct entry *e)
{
    uint32_t n = e->left < CHUNK ? e->left : CHUNK;
    e->left -= n;
    if (e->kind == E_BAND) {
        const uint16_t *p = e->src;
        e->src += n;
        nx_lcd_port_pixels(p, n, 1);
    } else {
        nx_lcd_port_pixels(&e->arg, n, 0);
    }
}

/* Advances the queue as far as the bus allows. Runs with the DMA interrupt masked or inside it.
 * Polled mode: returns after one chunk, the next nx_lcd_poll() continues. */
static void pump(void)
{
    int sent = 0;
    while (s_count) {
        struct entry *e = &s_q[s_head];
        if (!s_active) {
            if (!NX_LCD_DMA && sent && (e->kind == E_BAND || e->kind == E_FILL))
                return;
            begin(e);
            s_active = 1;
        }
        if (nx_lcd_port_busy())
            return;                       /* the DMA interrupt comes back here */
        if (e->left) {
            if (!NX_LCD_DMA && sent)
                return;
            send_chunk(e);
            sent = 1;
            continue;
        }
        if (e->kind == E_BAND)
            s_buf_state[e->arg8] = BUF_FREE;
        s_active = 0;
        s_head = (uint8_t)((s_head + 1u) % NX_LCD_QUEUE);
        s_count--;
    }
}

static int push(const struct entry *e)
{
    int ok = 0;
    nx_lcd_port_lock();
    if (s_count < NX_LCD_QUEUE) {
        s_q[(s_head + s_count) % NX_LCD_QUEUE] = *e;
        s_count++;
        ok = 1;
        pump();
    }
    nx_lcd_port_unlock();
    return ok;
}

void nx_lcd_irq(void)
{
    pump();
}

void nx_lcd_poll(void)
{
    nx_lcd_port_lock();
    pump();
    nx_lcd_port_unlock();
}

void nx_lcd_init(void)
{
    s_head = s_count = s_active = 0;
    s_buf_state[0] = s_buf_state[1] = BUF_FREE;
    s_scroll = 0;
    s_pw = s_pw_target = NX_LCD_ON;   /* lcd_init() ended with SLPOUT and DISPON */
    s_awake_wait = 0;
    s_pw_t = nx_ms();
    g_nx_lcd_stats = (struct nx_lcd_stats){ 0 };
    nx_lcd_port_init();
    /* the whole panel height scrolls (in landscape the ST7789's vertical scroll pans along X) */
    static const uint16_t k_area[3] = { 0, 320, 0 };
    static const uint16_t k_zero = 0;
    nx_lcd_port_cmd(NX_ST_VSCRDEF, k_area, 3);
    nx_lcd_port_cmd(NX_ST_VSCSAD, &k_zero, 1);
}

uint16_t *nx_lcd_buf_get(void)
{
    uint16_t *b = 0;
    nx_lcd_port_lock();
    for (unsigned i = 0; i < 2u; i++) {
        if (s_buf_state[i] == BUF_FREE) {
            s_buf_state[i] = BUF_OWNED;
            b = s_buf[i];
            break;
        }
    }
    nx_lcd_port_unlock();
    return b;
}

static int buf_index(const uint16_t *buf)
{
    return buf == s_buf[0] ? 0 : buf == s_buf[1] ? 1 : -1;
}

void nx_lcd_buf_put(uint16_t *buf)
{
    int i = buf_index(buf);
    if (i >= 0 && s_buf_state[i] == BUF_OWNED)
        s_buf_state[i] = BUF_FREE;
}

int nx_lcd_band(uint16_t *buf, const nx_rect *r)
{
    int i = buf_index(buf);
    uint32_t n = (uint32_t)(r->x1 - r->x0) * (uint32_t)(r->y1 - r->y0);
    if (i < 0 || s_buf_state[i] != BUF_OWNED || r->x1 <= r->x0 || r->y1 <= r->y0 || n > NX_BAND_PX)
        return 0;
    if (!nx_lcd_room())
        return 0;
#if !NX_LCD_DS16
    for (uint32_t k = 0; k < n; k++)   /* packed 8-bit writes send the low byte first */
        buf[k] = __builtin_bswap16(buf[k]);
#endif
    struct entry e = { E_BAND, (uint8_t)i, 0, *r, buf, 0 };
    s_buf_state[i] = BUF_QUEUED;
    if (!push(&e)) {
        s_buf_state[i] = BUF_OWNED;
        return 0;
    }
    return 1;
}

int nx_lcd_fill(const nx_rect *r, uint16_t color)
{
    if (r->x1 <= r->x0 || r->y1 <= r->y0)
        return 1;
#if !NX_LCD_DS16
    color = __builtin_bswap16(color);
#endif
    struct entry e = { E_FILL, 0, color, *r, 0, 0 };
    return push(&e);
}

int nx_lcd_scroll(uint16_t s)
{
    s %= NX_LCD_W;
    /* The glass shows GRAM column (X + s) at X. In the ST7789 model (NX_LCD_SCROLL_SIGN +1) a
     * VSCSAD of v shows column X - v at X, so v = -s; the panel of sign -1 scrolls the other way. */
    uint16_t v = NX_LCD_SCROLL_SIGN > 0 ? (uint16_t)((NX_LCD_W - s) % NX_LCD_W) : s;
    struct entry e = { E_SCROLL, 0, v, { 0, 0, 0, 0 }, 0, 0 };
    if (!push(&e))
        return 0;
    s_scroll = s;
    return 1;
}

int nx_lcd_cmd(uint8_t cmd)
{
    struct entry e = { E_CMD, cmd, 0, { 0, 0, 0, 0 }, 0, 0 };
    return push(&e);
}

unsigned nx_lcd_room(void)
{
    return NX_LCD_QUEUE - s_count;
}

int nx_lcd_idle(void)
{
    return s_count == 0;
}

uint16_t nx_lcd_scroll_pos(void)
{
    return s_scroll;
}

int nx_lcd_sync(void)
{
    uint32_t spent = 0;
    uint16_t last = nx_us16();
    for (uint32_t n = 0; n < SYNC_SPINS && spent < SYNC_MAX_US; n++) {
        if (nx_lcd_idle())
            return 1;
        nx_lcd_poll();
        uint16_t now = nx_us16();
        spent += (uint16_t)(now - last);
        last = now;
    }
    return 0;
}

void nx_lcd_power(uint8_t target)
{
    s_pw_target = target;
}

uint8_t nx_lcd_power_state(void)
{
    return s_pw;
}

int nx_lcd_ready(void)
{
    return s_pw != NX_LCD_SLEEP && !s_awake_wait;
}

static void wake(uint32_t now)
{
    if (now - s_pw_t < NX_LCD_SLEEP_GAP_MS)
        return;
    nx_lcd_cmd(NX_ST_SLPOUT);
    s_pw = NX_LCD_BLANK;
    s_pw_t = now;
    s_awake_wait = 1;
}

void nx_lcd_power_step(uint32_t now)
{
    if (s_awake_wait && now - s_pw_t >= NX_LCD_SLPOUT_MS)
        s_awake_wait = 0;
    if (s_pw == s_pw_target || s_awake_wait || !nx_lcd_idle())
        return;
    switch (s_pw_target) {
    case NX_LCD_SLEEP:
        if (s_pw == NX_LCD_ON) {
            nx_lcd_cmd(NX_ST_DISPOFF);
            s_pw = NX_LCD_BLANK;
        } else if (now - s_pw_t >= NX_LCD_SLEEP_GAP_MS) {
            nx_lcd_cmd(NX_ST_SLPIN);
            s_pw = NX_LCD_SLEEP;
            s_pw_t = now;
        }
        break;
    case NX_LCD_BLANK:
        if (s_pw == NX_LCD_ON) {
            nx_lcd_cmd(NX_ST_DISPOFF);
            s_pw = NX_LCD_BLANK;
        } else {
            wake(now);
        }
        break;
    default:
        if (s_pw == NX_LCD_SLEEP) {
            wake(now);
        } else {
            nx_lcd_cmd(NX_ST_DISPON);
            s_pw = NX_LCD_ON;
        }
        break;
    }
}
