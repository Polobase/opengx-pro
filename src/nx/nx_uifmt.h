/* Number and text formatting for the screens (into caller buffers; each returns the end, NUL
 * written) and the waveform map. Pure functions, host-tested. */
#pragma once

#include <stdint.h>

#include "nx_vm.h"

char *nx_fmt_u(char *p, uint32_t v);          /* 1234 */
char *nx_fmt_2d(char *p, uint32_t v);         /* 07 (two digits, v % 100) */
char *nx_fmt_dur(char *p, uint32_t s);        /* 9:05, 1:52:10, 125:00:00 (hours unbounded) */
char *nx_fmt_cv(char *p, uint32_t cv);        /* 0.01 V -> "20.0 V" (0.1 V, rounded) */
char *nx_fmt_pct(char *p, uint32_t pct);      /* "70 %" */
char *nx_fmt_hex(char *p, uint32_t v, unsigned digits);   /* EF4015 (upper case, digits wide) */
/* A settings delay: "Never" (0), "30 s", "5 min" (at most 16 bytes). */
char *nx_fmt_delay(char *p, uint32_t s);
/* Frequency v (w24 encoding) as "1 234.56": the integer part grouped by 3 with spaces, then the
 * decimals: at least min_dec, trailing zeros dropped down to min_dec, at most max_dec (<= 8,
 * truncated); group_dec: the decimals grouped by 3 too ("727.000 000 00"). No unit. */
char *nx_fmt_freq(char *p, uint64_t v, unsigned min_dec, unsigned max_dec, int group_dec);
/* The PC monitor's frequency: v as it is (2 to 8 decimals) for mult 0 or 1. For a wave table of
 * mult cycles (at most NX_PCMON_MAX_CYCLES of nx_diag.h): v * mult, the output frequency, rounded
 * to 6 decimals (Spooky2 sends the frequency / mult rounded to 8 decimals: 36 Hz in 11 cycles is
 * 3.27272727, times 11 35.99999997). */
char *nx_fmt_pc_freq(char *p, uint64_t v, unsigned mult);

/* Waveform map (docs/nx/fw2-design.md "Key findings", Waveforms): the program header's wave code
 * -> scope shape (enum nx_wave), Out2 relation (enum nx_out2) and a name string id. Default
 * seeded from research, to be verified on the scope (Drop 0 T8): 11..19 = Spooky2 waves 1..9,
 * 25 = Out2 inverse; 0 = no waveform set (the empty program). Everything else, e.g. 41..70
 * (the waveform table of stored slot n - 40), is unknown: returns 0 and the UI shows
 * "wave #N ?". */
#define NX_WAVE_NO_NAME 0xFFFFu
struct nx_wave_info {
    uint8_t shape;     /* enum nx_wave */
    uint8_t out2;      /* enum nx_out2 */
    uint16_t name;     /* NX_S_WAVE_* / NX_S_OUT_INVERSE, or NX_WAVE_NO_NAME */
};
int nx_wave_info(uint8_t code, struct nx_wave_info *wi);

/* The program detail's summary of a stored program: "41 steps · 3:00 per step · Square · 20.0 V"
 * (an unknown waveform as "wave #N ?"). No total time: the dashboard shows it. The waveform
 * comes before the amplitude, so a line wider than the detail's 296 px loses the amplitude, not
 * the name. out has room for NX_FMT_SUMMARY_MAX bytes. */
#define NX_FMT_SUMMARY_MAX 96u
char *nx_fmt_summary(char *out, const struct nx_slot_info *si);
