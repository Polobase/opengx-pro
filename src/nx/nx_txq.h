/* Non-blocking generator command queue, one per link (docs/nx/fw2-design.md, "Core decisions";
 * docs/nx/design/storage-runner-features.md §2.1).
 *
 * Each entry is one prebuilt line (the exact bytes of runner_seq / link_fmt_*). The main loop
 * calls nx_txq_service() every pass; per link it sends the next line, waits until it has left the
 * wire, then waits for that link's own :ok (LINK_OK_HOOK = nx_txq_on_ok) or NX_TXQ_ACK_TIMEOUT_MS,
 * then the original's 133 us tail, then the next line. So every link keeps the original per-line
 * timing and order while both links run in parallel and nothing blocks. Without any :ok the queue
 * behaves like the original runner: NX_TXQ_ACK_TIMEOUT_MS per line.
 *
 * A line submitted with NX_TXQ_COALESCE replaces a queued, not yet sent line with the same
 * ":wNN=" head that was also coalescable ("latest wins" for w24/w25 frequency updates).
 * Nothing is accepted in PC mode: the PC owns the generators then (g_nx_diag.txq_rejected_pc
 * counts attempts and must stay 0). Pure logic on top of uart.h/nx_uart.h: host-tested.
 *
 * A line longer than an entry (a wave table: ":a11=" and 1024 samples, about 4.1 KB) is written
 * as a stream: nx_txq_stream_begin once the link is idle, nx_txq_stream_put piece by piece as the
 * UART takes them, nx_txq_stream_end after its "\r\n". The stream holds the head of the queue
 * from its begin until the line's :ok (or NX_TXQ_STREAM_ACK_MS), so the link is not idle and no
 * other line, PC-mode entry or flash write gets into it. */
#pragma once

#include <stdint.h>

#include "runner_seq.h"

#define NX_TXQ_DEPTH 12u      /* lines per link: a full idle configuration (10) plus two */
#define NX_TXQ_LINE_MAX 28u   /* ":w24=<20 digits>,\r\n" */
#define NX_TXQ_TAIL_US 133u   /* link_wait_ok's delay_loop(0x855) after the :ok */
#define NX_TXQ_COALESCE 0x01u
#define NX_TXQ_STREAM_ACK_MS 2000u   /* a table's :ok: Spooky2 captures show up to 0.7 s after its line */
#define NX_TXQ_DOWN_AFTER 3u  /* consecutive timeouts before a link counts as down */

struct nx_txq_stats {
    uint32_t sent;            /* lines sent */
    uint32_t ok;              /* lines answered with :ok */
    uint32_t timeouts;        /* lines without :ok in NX_TXQ_ACK_TIMEOUT_MS */
    uint32_t coalesced;       /* queued lines replaced by a newer one */
    uint32_t lat_sum_us;      /* sum of :ok latencies (end of line -> :ok) */
    uint16_t lat_min_us, lat_max_us;
    uint8_t timeout_streak;   /* consecutive timeouts */
};

void nx_txq_reset(void);
/* Queue one line (with its "\r\n", 1..NX_TXQ_LINE_MAX bytes) on link ch (0 = G1, 1 = G2).
 * NX_OK, NX_REFUSED_PC, NX_BUSY (queue full), NX_EINVAL. */
int nx_txq_submit(unsigned ch, const char *line, unsigned len, unsigned flags);
/* Queue all lines of s, or none (NX_BUSY when they do not fit). */
int nx_txq_submit_seq(unsigned ch, const struct seq *s);
/* ":w24=<code>," on link ch; a ":w24" still waiting in the queue is replaced by it. */
int nx_txq_submit_freq(unsigned ch, uint64_t code);
/* A long line, in pieces. begin: NX_OK, NX_BUSY (the link is not idle), NX_REFUSED_PC. put: 1 =
 * the n bytes are on their way, 0 = no room for them now (again on a later pass). end: the line
 * is complete. It counts as one line sent; its wait for the :ok is not a latency. */
int nx_txq_stream_begin(unsigned ch);
int nx_txq_stream_put(unsigned ch, const char *p, unsigned n);
void nx_txq_stream_end(unsigned ch);
void nx_txq_service(void);          /* main loop, every pass */
int nx_txq_idle(unsigned ch);       /* nothing queued, nothing in flight */
int nx_txq_can_sleep(void);         /* no link needs polling before the next interrupt */
void nx_txq_on_ok(unsigned ch);     /* LINK_OK_HOOK: ":ok" received on link ch (interrupt) */
const struct nx_txq_stats *nx_txq_stats(unsigned ch);
int nx_txq_link_down(unsigned ch);  /* NX_GEN_ACKS and NX_TXQ_DOWN_AFTER timeouts in a row */
