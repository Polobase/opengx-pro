/* Prebuilt generator command lines for the classic and nx program runner (runner_seq.h). */
#include "runner_seq.h"

#include <string.h>

#include "link.h"

static void add_text(struct seq *s, const char *text)
{
    unsigned n = (unsigned)strlen(text);
    memcpy(s->line[s->n], text, n);
    memcpy(s->line[s->n] + n, "\r\n", 2);
    s->len[s->n++] = (uint8_t)(n + 2u);
}

static void add_u64(struct seq *s, unsigned id, uint64_t v)
{
    s->len[s->n] = (uint8_t)link_fmt_w_u64(s->line[s->n], id, v);
    s->n++;
}

static void add_pair5(struct seq *s, unsigned id, unsigned a, unsigned b)
{
    s->len[s->n] = (uint8_t)link_fmt_w_pair5(s->line[s->n], id, a, b);
    s->n++;
}

void seq_idle(struct seq *s, uint64_t freq, const struct program *p)
{
    s->n = 0;
    add_u64(s, 24, freq);
    add_text(s, ":w11=0,0,");
    add_text(s, ":w14=1,");
    add_text(s, ":w17=0,1,");
    add_u64(s, 20, p->hdr[HDR_WAVE]);
    add_u64(s, 21, p->hdr[HDR_WAVE]);
    add_u64(s, 28, p->hdr[HDR_AMPL]);
    add_u64(s, 29, p->hdr[HDR_AMPL]);
    add_u64(s, 32, p->hdr[HDR_OFFS]);
    add_u64(s, 33, p->hdr[HDR_OFFS]);
}

void seq_play(struct seq *s, uint64_t freq, uint16_t a, uint16_t b)
{
    s->n = 0;
    add_u64(s, 24, freq);
    add_text(s, ":w11=1,1,");
    if (a == 0 && b == 0) {
        add_text(s, ":w12=0,0,");
    } else {
        add_pair5(s, 50, a, b);
        add_pair5(s, 51, a, b);
        add_text(s, ":w12=1,1,");
    }
}

void seq_off(struct seq *s)
{
    s->n = 0;
    add_text(s, ":w11=0,0,");
}
