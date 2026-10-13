/* Non-blocking generator queue (src/nx/nx_txq.c) against the generator model of nx_world.c. */
#include "harness.h"

#include "nx_config.h"
#include "nx_diag.h"
#include "nx_txq.h"
#include "nx_world.h"
#include "state.h"
#include "test_nx.h"

/* Services the queue every 10 us for us microseconds, delivering received bytes on time. */
static void run_us(uint64_t us)
{
    uint64_t end = sim_us + us;
    while (sim_us < end) {
        nx_txq_service();
        nxw_poll();
        sim_advance_us(10);
        nxw_poll();
    }
}

static void until_idle(void)
{
    for (int i = 0; i < 400000 && !(nx_txq_idle(0) && nx_txq_idle(1)); i++)
        run_us(10);
}

static int submit(unsigned ch, const char *s, unsigned flags)
{
    return nx_txq_submit(ch, s, (unsigned)strlen(s), flags);
}

/* Writes s as a stream on ch, as fast as the UART takes it; returns the simulated time it took. */
static uint64_t stream(unsigned ch, const char *s)
{
    uint64_t t0 = sim_us;
    unsigned len = (unsigned)strlen(s), at = 0, stalls = 0;
    while (at < len) {
        unsigned n = len - at < 16u ? len - at : 16u;
        if (nx_txq_stream_put(ch, s + at, n))
            at += n;
        else {
            stalls++;
            run_us(10);
        }
    }
    CHECK(stalls > 0 || len <= 512u);   /* a long line fills the transmit ring */
    nx_txq_stream_end(ch);
    return sim_us - t0;
}

void test_nx_txq(void)
{
    char lines[16][32];

    /* three lines on G1, each answered 1 ms after it left the wire: sent in order, the next one
     * 133 us after the :ok (link_wait_ok's tail) */
    nxw_reset();
    g_ui_state = ST_HOME;
    CHECK(nx_txq_idle(0));
    CHECK_EQ(submit(0, ":w24=5,\r\n", 0), NX_OK);
    CHECK_EQ(submit(0, ":w11=0,0,\r\n", 0), NX_OK);
    CHECK_EQ(submit(0, ":w14=1,\r\n", 0), NX_OK);
    CHECK(!nx_txq_idle(0));
    CHECK(!nx_txq_can_sleep());
    until_idle();
    CHECK_EQ(nxw_lines(0, lines, 16), 3);
    CHECK(strcmp(lines[0], ":w24=5,") == 0 && strcmp(lines[1], ":w11=0,0,") == 0 && strcmp(lines[2], ":w14=1,") == 0);
    CHECK_EQ(tx_len[1], 0);
    for (unsigned i = 1; i < 3u; i++) {
        uint64_t gap = nxw_line_us[0][i] - nxw_line_end_us[0][i - 1];
        CHECK(gap >= 1000u + NX_TXQ_TAIL_US);
        CHECK(gap < 1000u + NX_TXQ_TAIL_US + 40u);
    }
    const struct nx_txq_stats *st = nx_txq_stats(0);
    CHECK_EQ(st->sent, 3);
    CHECK_EQ(st->ok, 3);
    CHECK_EQ(st->timeouts, 0);
    CHECK(st->lat_min_us >= 980u && st->lat_max_us <= 1010u);
    CHECK(st->lat_sum_us >= 3u * 980u);
    CHECK(nx_txq_can_sleep());

    /* both links in parallel: each starts at once, independent of the other's :ok */
    nxw_reset();
    g_ui_state = ST_HOME;
    nxw_gen(1, NXW_ACK, 5000);
    for (unsigned i = 0; i < 3u; i++) {
        submit(0, ":w11=0,0,\r\n", 0);
        submit(1, ":w11=0,0,\r\n", 0);
    }
    until_idle();
    CHECK_EQ(nxw_line_n[0], 3);
    CHECK_EQ(nxw_line_n[1], 3);
    CHECK_EQ(nxw_line_us[0][0], nxw_line_us[1][0]);
    CHECK(nxw_line_us[1][1] - nxw_line_end_us[1][0] >= 5000u + NX_TXQ_TAIL_US);
    CHECK(nxw_line_us[0][2] < nxw_line_us[1][1]);   /* G1 is not held up by G2's slow acks */

    /* no :ok at all: NX_TXQ_ACK_TIMEOUT_MS per line, like the original runner */
    nxw_reset();
    g_ui_state = ST_HOME;
    nxw_gen(0, NXW_NOACK, 0);
    for (unsigned i = 0; i < 4u; i++)
        submit(0, ":w11=0,0,\r\n", 0);
    until_idle();
    CHECK_EQ(nxw_line_n[0], 4);
    for (unsigned i = 1; i < 4u; i++) {
        uint64_t gap = nxw_line_us[0][i] - nxw_line_end_us[0][i - 1];
        CHECK(gap >= NX_TXQ_ACK_TIMEOUT_MS * 1000u + NX_TXQ_TAIL_US);
        CHECK(gap < (NX_TXQ_ACK_TIMEOUT_MS + 2u) * 1000u);
    }
    CHECK_EQ(nx_txq_stats(0)->timeouts, 4);
    CHECK_EQ(g_nx_diag.ok_timeouts[0], 4);
    CHECK_EQ(nx_txq_link_down(0), NX_GEN_ACKS);
    CHECK_EQ(nx_txq_link_down(1), 0);

    /* an :ok that arrives while the line is still on the wire does not answer it */
    nxw_reset();
    g_ui_state = ST_HOME;
    nxw_gen(0, NXW_NOACK, 0);
    submit(0, ":w24=1234567890,\r\n", 0);
    run_us(20);                               /* the line is being sent (18 bytes, 1.6 ms) */
    nxw_rx(0, ":ok\r\n");
    run_us(3000);
    CHECK_EQ(nx_txq_stats(0)->ok, 0);
    nxw_rx(0, ":ok\r\n");                     /* after the end of the line: its answer */
    run_us(500);
    CHECK_EQ(nx_txq_stats(0)->ok, 1);
    CHECK(nx_txq_idle(0));

    /* coalescing: a queued w24 takes the newest value, in its place; the line in flight stays */
    nxw_reset();
    g_ui_state = ST_HOME;
    submit(0, ":w24=1,\r\n", NX_TXQ_COALESCE);
    submit(0, ":w25=1,\r\n", NX_TXQ_COALESCE);
    submit(0, ":w24=2,\r\n", NX_TXQ_COALESCE);
    submit(0, ":w24=33,\r\n", NX_TXQ_COALESCE);
    CHECK_EQ(nx_txq_stats(0)->coalesced, 2);
    run_us(20);                               /* ":w24=33," is in flight now */
    submit(0, ":w24=4,\r\n", NX_TXQ_COALESCE);
    submit(0, ":w11=1,1,\r\n", 0);
    submit(0, ":w24=5,\r\n", NX_TXQ_COALESCE);
    until_idle();
    CHECK_EQ(nxw_lines(0, lines, 16), 4);
    CHECK(strcmp(lines[0], ":w24=33,") == 0);
    CHECK(strcmp(lines[1], ":w25=1,") == 0);
    CHECK(strcmp(lines[2], ":w24=5,") == 0);
    CHECK(strcmp(lines[3], ":w11=1,1,") == 0);

    /* a sequence is queued completely or not at all */
    nxw_reset();
    g_ui_state = ST_HOME;
    struct seq s;
    struct program p;
    memset(&p, 0, sizeof p);
    seq_idle(&s, 5, &p);
    for (unsigned i = 0; i < 5u; i++)
        CHECK_EQ(submit(1, ":w11=0,0,\r\n", 0), NX_OK);
    CHECK_EQ(nx_txq_submit_seq(1, &s), NX_BUSY);
    until_idle();
    CHECK_EQ(nxw_line_n[1], 5);
    CHECK_EQ(nx_txq_submit_seq(1, &s), NX_OK);
    until_idle();
    CHECK_EQ(nxw_line_n[1], 15);
    for (unsigned i = 0; i < NX_TXQ_DEPTH; i++)
        CHECK_EQ(submit(0, ":w12=0,0,\r\n", 0), NX_OK);
    CHECK_EQ(submit(0, ":w12=0,0,\r\n", 0), NX_BUSY);

    /* a long line as a stream (a wave table): the link is busy from its begin to its :ok, other
     * lines wait behind it, and it arrives whole */
    nxw_reset();
    g_ui_state = ST_HOME;
    static char big[2100], got[2100];
    {
        unsigned n = (unsigned)snprintf(big, sizeof big, ":a11=");
        for (unsigned i = 0; n < 2000u; i++)
            n += (unsigned)snprintf(big + n, sizeof big - n, "%u,", (i * 37u) & 1023u);
        snprintf(big + n, sizeof big - n, "\r\n");
    }
    nxw_table_ack_us = 200000;                              /* the generator takes 200 ms over a table */
    CHECK_EQ(submit(0, ":w11=0,0,\r\n", 0), NX_OK);
    CHECK_EQ(nx_txq_stream_begin(0), NX_BUSY);             /* only on an idle link */
    CHECK_EQ(nx_txq_stream_put(0, "x", 1), 0);
    until_idle();
    nxw_clear();
    CHECK_EQ(nx_txq_stream_begin(0), NX_OK);
    CHECK(!nx_txq_idle(0) && nx_txq_idle(1));
    CHECK(!nx_txq_can_sleep());
    CHECK_EQ(submit(0, ":w20=11,\r\n", 0), NX_OK);         /* queued behind the stream */
    uint64_t took = stream(0, big);
    CHECK(took >= (strlen(big) - 512u) * 87u && took < strlen(big) * 87u + 2000u);   /* at wire speed */
    CHECK(!nx_txq_idle(0));
    run_us(150000);                                         /* the table is out, its :ok has not come */
    CHECK(!nx_txq_idle(0));
    CHECK_EQ(nxw_line_n[0], 1);
    until_idle();
    CHECK_EQ(nxw_line_n[0], 2);
    CHECK_EQ(nxw_line(0, 0, got, sizeof got), strlen(big) - 2u);
    CHECK(strncmp(got, big, strlen(big) - 2u) == 0);
    CHECK_EQ(nxw_lines(0, lines, 16), 2);
    CHECK(strcmp(lines[1], ":w20=11,") == 0);
    CHECK(nxw_line_us[0][1] >= nxw_line_end_us[0][0] + 200000u);   /* after the table's :ok */
    st = nx_txq_stats(0);
    CHECK_EQ(st->sent, 3);
    CHECK_EQ(st->ok, 3);
    CHECK_EQ(st->timeouts, 0);
    CHECK(st->lat_max_us <= 1010u);                         /* the table's wait is not a latency */

    /* a table the generator does not answer: its own wait, then one timeout */
    nxw_gen(0, NXW_NOTABLE, 1000);
    nxw_clear();
    CHECK_EQ(nx_txq_stream_begin(0), NX_OK);
    stream(0, ":a11=1,2,3,\r\n");
    uint64_t t0 = sim_us;
    until_idle();
    CHECK(sim_us - t0 >= NX_TXQ_STREAM_ACK_MS * 1000u && sim_us - t0 < (NX_TXQ_STREAM_ACK_MS + 5u) * 1000u);
    CHECK_EQ(nx_txq_stats(0)->timeouts, 1);
    CHECK_EQ(nx_txq_stats(0)->timeout_streak, 1);
    CHECK_EQ(submit(0, ":w11=0,0,\r\n", 0), NX_OK);        /* ":w" lines are still answered */
    until_idle();
    CHECK_EQ(nx_txq_stats(0)->timeout_streak, 0);
    nx_txq_stream_end(0);                                   /* no stream open: nothing happens */
    CHECK(nx_txq_idle(0));

    /* bad arguments */
    CHECK_EQ(nx_txq_submit(2, ":w11=0,0,\r\n", 11, 0), NX_EINVAL);
    CHECK_EQ(nx_txq_submit(0, "", 0, 0), NX_EINVAL);
    CHECK_EQ(nx_txq_submit(0, ":w24=123456789012345678901,\r\n", 29, 0), NX_EINVAL);

    /* PC mode: nothing is accepted, and every attempt is counted (must stay 0 in practice) */
    nxw_reset();
    g_ui_state = ST_PC;
    CHECK_EQ(submit(0, ":w11=0,0,\r\n", 0), NX_REFUSED_PC);
    CHECK_EQ(nx_txq_submit_seq(1, &s), NX_REFUSED_PC);
    CHECK_EQ(nx_txq_stream_begin(0), NX_REFUSED_PC);
    CHECK_EQ(g_nx_diag.txq_rejected_pc, 3);
    CHECK(nx_txq_idle(0) && nx_txq_idle(1));
    run_us(1000);
    CHECK_EQ(tx_len[0] + tx_len[1], 0);
}
