/* Generator command lines of the classic program runner (runner.c; docs/spec/protocols.md §5.3)
 * as prebuilt text, so the classic and the nx runner send exactly the original bytes. */
#pragma once

#include <stdint.h>

#include "state.h"

#define SEQ_MAX_LINES 10u
#define SEQ_LINE_MAX  32u   /* longest line ":w24=<20 digits>,\r\n" = 28 bytes */

struct seq {
    uint8_t n;                               /* lines in use */
    uint8_t len[SEQ_MAX_LINES];              /* bytes per line, including "\r\n" */
    char line[SEQ_MAX_LINES][SEQ_LINE_MAX];
};

/* Timing assumption: seq_idle/seq_play capture the program values they are given (waveform,
 * amplitude, offset, gating pair) into the lines below when the sequence is built, before the
 * first line goes out; sending the lines and waiting for each :ok then no longer touches them.
 * The old code re-read the values between lines instead, so an upload into the same channel
 * arriving during one step's :ok waits could have produced a mix of old and new values. Both are
 * valid; this makes the timing assumption explicit. */

/* Idle configuration of a step, outputs off: :w24=f, :w11=0,0, :w14=1, :w17=0,1,
 * :w20/:w21=waveform, :w28/:w29=amplitude, :w32/:w33=offset (10 lines). */
void seq_idle(struct seq *s, uint64_t freq, const struct program *p);
/* Playing step: :w24=f, :w11=1,1, then :w12=0,0, when (a, b) == (0, 0), otherwise
 * :w50=aaaaa,bbbbb, :w51=aaaaa,bbbbb, :w12=1,1, (3 or 5 lines). */
void seq_play(struct seq *s, uint64_t freq, uint16_t a, uint16_t b);
/* Program finished without loop: :w11=0,0, (1 line). */
void seq_off(struct seq *s);
