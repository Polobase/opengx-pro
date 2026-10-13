/* Application state of the display firmware, mirroring the original's RAM variables
 * (docs/spec/application.md §4). The original is built around these globals and many features
 * touch them, so every module shares this one definition. Comments give the original address.
 *
 * Variables shared with interrupt handlers are volatile. Channel index: 0 = G1 (USART1,
 * generator board A), 1 = G2 (USART2, board B). */
#pragma once

#include <stdint.h>

/* ui_state (0x2000001C) */
enum ui_state {
    ST_BOOT = 0x00,
    ST_HOME = 0x01,
    ST_LIST = 0x02,
    ST_CH1 = 0x03,
    ST_CH2 = 0x04,
    ST_PC = 0x10,
    ST_CAL = 0xFF,
};

/* ui_flags (0x2000000E) */
#define UIF_BACKLIGHT_OFF 0x0001u   /* never set by the original */
#define UIF_REDRAW        0x0002u   /* full redraw request, cleared by ui_render */
#define UIF_MARQUEE       0x0004u   /* name marquee active */
#define UIF_LIST_G2       0x0008u   /* LIST target is G2 (else G1) */
#define UIF_LIST_UP       0x0010u   /* last LIST move was "up" */
#define UIF_PC_NAME_G2    0x0020u   /* PC mode: marquee shows slot 31 (G2) instead of slot 0 */
#define UIF_RUN_G1        0x0040u
#define UIF_RUN_G2        0x0080u
#define UIF_LOOP_G1       0x0100u
#define UIF_LOOP_G2       0x0200u
#define UIF_LINE_G1       0x0400u   /* complete line pending from USART1 */
#define UIF_LINE_G2       0x0800u   /* complete line pending from USART2 */
#define UIF_SYNC_G1       0x1000u   /* PC mode: out2 shows out1's frequency (w14) */
#define UIF_SYNC_G2       0x2000u
#define UIF_PC_UNLOCK     0x4000u   /* reimplementation: always set (no access gate) */
#define UIF_ERASED_ALL    0x8000u   /* w96 erase-all done this power cycle */

#define UIF_RUN(ch)  ((ch) ? UIF_RUN_G2 : UIF_RUN_G1)
#define UIF_LOOP(ch) ((ch) ? UIF_LOOP_G2 : UIF_LOOP_G1)
#define UIF_LINE(ch) ((ch) ? UIF_LINE_G2 : UIF_LINE_G1)
#define UIF_SYNC(ch) ((ch) ? UIF_SYNC_G2 : UIF_SYNC_G1)

/* pc_flags (0x20000010): bits 0..3 = PC-mode frequency field up to date (G1o1, G1o2, G2o1, G2o2),
 * 0x10/0x20 = name slot 0 / 31 up to date, 0x40..0x200 = stored w15 values (never read).
 * 0x400 (vendor licence failure) is never set in the reimplementation. */
#define PCF_FREQ(ch, out) (1u << ((ch) * 2u + (out)))
#define PCF_NAME(ch)      ((ch) ? 0x20u : 0x10u)

/* misc_flags (0x20000012): bit0 cal-edit direction inverted; bits 1..4 PC-mode output enabled
 * (G1o1, G1o2, G2o1, G2o2) */
#define MISC_CAL_INVERT   0x0001u
#define MISC_OUT_EN(ch, out) (2u << ((ch) * 2u + (out)))

/* Key events (key_event 0x2000001A): press = code 1..10 for one iteration, held = code|0x10,
 * release = code|0x30 (short) or code|0x50 (long). Codes: see keys.h. */
#define KEV_HELD 0x10u

#define PROGRAM_STEPS 200u
#define PROGRAM_SLOTS 30u
#define NAME_LEN 60u
#define NAME_SLOTS 32u   /* slot 0 = PC G1 name, 1..30 = programs, 31 = PC G2 name */

/* One stored/active program record (0x668 bytes, layout identical to SPI flash). */
struct program {
    uint64_t hdr[5];                 /* 0 waveform?, 1 amplitude (0.01 V), 2 offset (120), 3 dwell s, 4 count */
    uint64_t freq[PROGRAM_STEPS];    /* encoded: Hz = (v/10) * 10^((v%10) - 8) */
};
#define HDR_WAVE 0
#define HDR_AMPL 1
#define HDR_OFFS 2
#define HDR_DWELL 3
#define HDR_COUNT 4

struct channel {
    struct program prog;             /* G1 0x20000978, G2 0x20000FE0 */
    uint16_t params[2u * PROGRAM_STEPS];   /* (a, b) per step, 0xFFFF = none; G1 0x20001648, G2 0x20001968 */
    uint8_t prog_slot;               /* 0x2000001D/1E: loaded program 1..30, 0 = none */
    uint8_t step;                    /* 0x20000060/62: current step, 1-based, 0 = none */
    uint8_t step_sent;               /* 0x20000061/63: last sent step, 0xFF forces a resend */
    uint64_t step_freq;              /* 0x20000050/58 */
    volatile uint32_t elapsed;       /* 0x20000024/28: 10.1 ms units; 0 = fresh, 1 = stopped */
    uint64_t pc_freq[2];             /* 0x20000030.. : PC-mode out1/out2 frequency sniffed from w24/w25 */
};

extern struct channel g_ch[2];

extern volatile uint16_t g_ui_flags;         /* 0x2000000E */
extern volatile uint16_t g_pc_flags;         /* 0x20000010 */
extern volatile uint16_t g_misc_flags;       /* 0x20000012 */
extern volatile uint16_t g_factory_cmd_flags;   /* 0x20000014: bit0 = USART3 command pending */
extern volatile uint8_t g_ui_state;          /* 0x2000001C */
/* Classic UI only (ui_state.c): */
extern uint8_t g_ui_prev_state;              /* 0x20000000 */
extern volatile uint8_t g_key_event;         /* 0x2000001A */
extern volatile uint8_t g_key_hold_count;    /* 0x20000009: 25.6 ms units while a key is held */
extern volatile uint8_t g_elapsed_prescaler; /* 0x2000000A: 0..100 */
extern volatile uint16_t g_marquee_offset;   /* 0x20000018 */
extern uint16_t g_marquee_prev_offset;       /* 0x20000002 */
extern uint8_t g_list_top;                   /* 0x2000001F: first visible LIST row, 1..24 */
extern uint8_t g_list_sel;                   /* 0x20000020: selected LIST entry, 1..30 */
extern uint16_t g_name_bmp_width;            /* 0x20001C88 */
extern uint8_t g_name_bmp[1998];             /* 0x20001C8A: rendered name, 3 bytes per column */
/* Shared with the protocol core (state.c): */
extern uint8_t g_cal_index;                  /* 0x2000007C: 0..28 */
extern uint8_t g_cal_freq_exp;               /* 0x2000006E */
extern uint8_t g_cal_ampl_exp;               /* 0x2000006F */
extern int16_t g_cal_table[32];              /* 0x2000007E: [31] == 31 marks a valid block */
extern char g_names[NAME_SLOTS][NAME_LEN];   /* 0x200001F8 */

/* Factory defaults of the calibration table (original RW data). */
extern const int16_t k_cal_defaults[32];

/* DEVIATION (race fix): g_ui_flags, g_pc_flags, g_misc_flags and g_factory_cmd_flags are also changed by interrupt
 * handlers (TIM3 marquee toggle, UART "line pending"). Main-loop code must change them only
 * with these helpers, or a bit set by an interrupt between the read and the write is lost
 * (the original has exactly this race). Interrupt handlers may use plain |= / &= / ^=. */
#if defined(__arm__)
#include "cmsis_compiler.h"
static inline void flags_set(volatile uint16_t *flags, uint16_t mask)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    *flags |= mask;
    __set_PRIMASK(primask);
}
static inline void flags_clear(volatile uint16_t *flags, uint16_t mask)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    *flags &= (uint16_t)~mask;
    __set_PRIMASK(primask);
}
#else   /* host unit tests */
static inline void flags_set(volatile uint16_t *flags, uint16_t mask) { *flags |= mask; }
static inline void flags_clear(volatile uint16_t *flags, uint16_t mask) { *flags &= (uint16_t)~mask; }
#endif
