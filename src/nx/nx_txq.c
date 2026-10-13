/* Non-blocking generator command queue (nx_txq.h). */
#include "nx_txq.h"

#include <string.h>

#include "link.h"
#include "nx_config.h"
#include "nx_diag.h"
#include "nx_time.h"
#include "nx_uart.h"
#include "state.h"

enum { TXQ_IDLE, TXQ_SENDING, TXQ_WAIT_OK, TXQ_TAIL, TXQ_STREAM };

struct nx_txe {
    uint8_t len;
    uint8_t flags;
    char line[NX_TXQ_LINE_MAX];
};

struct txq_link {   /* the entries last: the small fields stay within reach of a short offset */
    uint8_t head;        /* oldest entry: in flight unless state == TXQ_IDLE */
    uint8_t count;
    volatile uint8_t state;
    uint32_t t_sent_ms;  /* the line had left the wire (seen by the main loop) */
    uint16_t t_sent_us;
    uint16_t t_done_us;  /* :ok or timeout: start of the tail */
    struct nx_txq_stats st;
    struct nx_txe e[NX_TXQ_DEPTH];
};

static struct txq_link s_q[2];
static uint8_t s_stream[2];   /* the line in flight is a stream (nx_txq_stream_*): its own :ok wait */
static volatile uint8_t s_ok[2];
static volatile uint16_t s_ok_us[2];

static enum uart_port port_of(unsigned ch)
{
    return ch ? UART_GEN_B : UART_GEN_A;
}

void nx_txq_reset(void)
{
    memset(s_q, 0, sizeof s_q);
    s_stream[0] = s_stream[1] = 0;
    for (unsigned ch = 0; ch < 2u; ch++) {
        s_q[ch].st.lat_min_us = 0xFFFFu;
        s_ok[ch] = 0;
    }
}

static int refused_in_pc_mode(void)
{
    if (g_ui_state != ST_PC)
        return 0;
    g_nx_diag.txq_rejected_pc++;
    return 1;
}

static int push(unsigned ch, const char *line, unsigned len, unsigned flags)
{
    struct txq_link *q = &s_q[ch];
    if ((flags & NX_TXQ_COALESCE) && len >= 5u) {
        unsigned first = q->state == TXQ_IDLE ? 0u : 1u;   /* never touch the line in flight */
        for (unsigned i = first; i < q->count; i++) {
            struct nx_txe *e = &q->e[(q->head + i) % NX_TXQ_DEPTH];
            if ((e->flags & NX_TXQ_COALESCE) && memcmp(e->line, line, 5) == 0) {
                memcpy(e->line, line, len);
                e->len = (uint8_t)len;
                q->st.coalesced++;
                return NX_OK;
            }
        }
    }
    if (q->count >= NX_TXQ_DEPTH)
        return NX_BUSY;
    struct nx_txe *e = &q->e[(q->head + q->count) % NX_TXQ_DEPTH];
    memcpy(e->line, line, len);
    e->len = (uint8_t)len;
    e->flags = (uint8_t)flags;
    q->count++;
    return NX_OK;
}

int nx_txq_submit(unsigned ch, const char *line, unsigned len, unsigned flags)
{
    if (ch > 1u || len == 0 || len > NX_TXQ_LINE_MAX)
        return NX_EINVAL;
    if (refused_in_pc_mode())
        return NX_REFUSED_PC;
    return push(ch, line, len, flags);
}

int nx_txq_submit_seq(unsigned ch, const struct seq *s)
{
    if (ch > 1u)
        return NX_EINVAL;
    if (refused_in_pc_mode())
        return NX_REFUSED_PC;
    for (unsigned i = 0; i < s->n; i++)
        if (s->len[i] == 0 || s->len[i] > NX_TXQ_LINE_MAX)
            return NX_EINVAL;
    if (s->n > NX_TXQ_DEPTH - s_q[ch].count)
        return NX_BUSY;
    for (unsigned i = 0; i < s->n; i++)
        push(ch, s->line[i], s->len[i], 0);
    return NX_OK;
}

static void answered(struct txq_link *q, unsigned ch)
{
    uint16_t lat = (uint16_t)(s_ok_us[ch] - q->t_sent_us);
    if (lat > 0x8000u   /* the :ok came before this loop noticed the end of the line */
        || s_stream[ch])   /* a table's wait is longer than 16 bits of microseconds */
        lat = 0;
    q->st.ok++;
    q->st.lat_sum_us += lat;
    if (lat < q->st.lat_min_us)
        q->st.lat_min_us = lat;
    if (lat > q->st.lat_max_us)
        q->st.lat_max_us = lat;
    q->st.timeout_streak = 0;
    q->t_done_us = s_ok_us[ch];
}

static void service_link(unsigned ch)
{
    struct txq_link *q = &s_q[ch];
    enum uart_port port = port_of(ch);
    if (q->state == TXQ_STREAM)   /* its writer feeds the UART and ends the line */
        return;
    for (;;) {
        switch (q->state) {
        case TXQ_IDLE: {
            if (!q->count)
                return;
            const struct nx_txe *e = &q->e[q->head];
            if (uart_tx_room(port) < e->len)
                return;
            s_ok[ch] = 0;   /* an earlier :ok does not answer this line */
            q->state = TXQ_SENDING;
            for (unsigned i = 0; i < e->len; i++)
                uart_putc(port, (uint8_t)e->line[i]);
            q->st.sent++;
        }
            /* fall through */
        case TXQ_SENDING:
            if (!uart_tx_idle(port))
                return;
            q->t_sent_ms = nx_ms();
            q->t_sent_us = nx_us16();
            q->state = TXQ_WAIT_OK;
            /* fall through */
        case TXQ_WAIT_OK:
            if (s_ok[ch]) {
                answered(q, ch);
            } else if (nx_ms() - q->t_sent_ms > (s_stream[ch] ? NX_TXQ_STREAM_ACK_MS : NX_TXQ_ACK_TIMEOUT_MS)) {
                q->st.timeouts++;
                if (q->st.timeout_streak < 255u)
                    q->st.timeout_streak++;
                g_nx_diag.ok_timeouts[ch]++;
                q->t_done_us = nx_us16();
            } else {
                return;
            }
            q->state = TXQ_TAIL;
            /* fall through */
        default:   /* TXQ_TAIL */
            if ((uint16_t)(nx_us16() - q->t_done_us) < NX_TXQ_TAIL_US)
                return;
            q->head = (uint8_t)((q->head + 1u) % NX_TXQ_DEPTH);
            q->count--;
            q->state = TXQ_IDLE;
            s_stream[ch] = 0;
            break;   /* the next line goes out in the same pass */
        }
    }
}

int nx_txq_submit_freq(unsigned ch, uint64_t code)
{
    char line[NX_TXQ_LINE_MAX];
    return nx_txq_submit(ch, line, link_fmt_w_u64(line, 24, code), NX_TXQ_COALESCE);
}

int nx_txq_stream_begin(unsigned ch)
{
    struct txq_link *q = &s_q[ch];
    if (refused_in_pc_mode())
        return NX_REFUSED_PC;
    if (q->count || q->state != TXQ_IDLE)
        return NX_BUSY;
    q->count = 1;   /* the stream's place: the head, dropped after its :ok */
    s_stream[ch] = 1;
    s_ok[ch] = 0;
    q->state = TXQ_STREAM;
    q->st.sent++;
    return NX_OK;
}

int nx_txq_stream_put(unsigned ch, const char *p, unsigned n)
{
    enum uart_port port = port_of(ch);
    if (s_q[ch].state != TXQ_STREAM || uart_tx_room(port) < n)
        return 0;
    while (n--)
        uart_putc(port, (uint8_t)*p++);
    return 1;
}

void nx_txq_stream_end(unsigned ch)
{
    if (s_q[ch].state == TXQ_STREAM)
        s_q[ch].state = TXQ_SENDING;   /* from here as any line: off the wire, its :ok, the tail */
}

void nx_txq_service(void)
{
    service_link(0);
    service_link(1);
}

int nx_txq_idle(unsigned ch)
{
    const struct txq_link *q = &s_q[ch & 1u];
    return q->count == 0 && q->state == TXQ_IDLE;
}

int nx_txq_can_sleep(void)
{
    for (unsigned ch = 0; ch < 2u; ch++) {
        uint8_t st = s_q[ch].state;
        if (st == TXQ_SENDING || st == TXQ_TAIL || st == TXQ_STREAM || (st == TXQ_IDLE && s_q[ch].count))
            return 0;
    }
    return 1;
}

void nx_txq_on_ok(unsigned ch)
{
    ch &= 1u;
    uint8_t st = s_q[ch].state;
    /* Like link_wait_ok, which clears the flag after sending: only an :ok that arrives once the
     * line has left the wire answers it. */
    if ((st == TXQ_SENDING || st == TXQ_WAIT_OK) && uart_tx_idle(port_of(ch))) {
        s_ok_us[ch] = nx_us16();
        s_ok[ch] = 1;
    }
}

const struct nx_txq_stats *nx_txq_stats(unsigned ch)
{
    return &s_q[ch & 1u].st;
}

int nx_txq_link_down(unsigned ch)
{
    return NX_GEN_ACKS && s_q[ch & 1u].st.timeout_streak >= NX_TXQ_DOWN_AFTER;
}
