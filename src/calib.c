/* Calibration test-signal sequences (docs/spec/protocols.md §5.4, application.md §9.1):
 * cal_apply_test 0x08011468 (calibration menu) and cal_apply_test_jig 0x08011C2C (USART3 ':b').
 * Both send a fixed list of commands chosen by g_cal_index (0..21 and 28; 22..27 and anything
 * above 28 send nothing), each followed by the :ok wait. The lists were traced from the
 * disassembly and reproduce the original byte sequences.
 *
 * Encoding of one list: NUL-separated entries, ending with an empty entry. Each entry starts
 * with the link ('1' = USART1/G1, '2' = USART2/G2), followed by either a literal line or
 *   F<id>  test frequency: ":w<id>=3750007," or, when g_cal_freq_exp = n != 0,
 *          ":w<id>=<(u64)(2^n * 200000.0 + 8.0)>,"   (2^n * 20 kHz, exponent digit 8)
 *   A<id>  test amplitude: ":w<id>=1200," or, when g_cal_ampl_exp = n != 0,
 *          ":w<id>=<(u64)pow(10.0, n / 3.0)>,"  */
#include <stdint.h>

#include "link.h"
#include "state.h"

#define ALL_ON(p) \
    p ":w11=1,1,\0" "2:w11=1,1,\0" \
    "1:w32=120,\0" "1:w33=120,\0" "2:w32=120,\0" "2:w33=120,\0" \
    "1:w24=123456007,\0" "1:w25=123456007,\0" "2:w24=123456007,\0" "2:w25=123456007,\0" \
    "1:w28=1500,\0" "1:w29=1500,\0" "2:w28=1500,\0" "2:w29=1500,\0"

static const char *const k_menu[29] = {
    /*  0 */ ALL_ON("1"),
    /*  1 */ "1:w11=1,0,\0" "2:w11=0,0,\0" "1F24\0" "1A28\0",
    /*  2 */ "1:w28=60,\0",
    /*  3 */ "1:w28=1200,\0",
    /*  4 */ "1:w28=60,\0" "1:w32=120,\0",
    /*  5 */ "1:w11=1,0,\0" "1:w28=600,\0" "1:w32=220,\0",
    /*  6 */ "1:w11=0,1,\0" "1:w32=120,\0" "1F25\0" "1A29\0",
    /*  7 */ "1:w29=60,\0",
    /*  8 */ "1:w29=1200,\0",
    /*  9 */ "1:w11=0,1,\0" "1:w24=3750007,\0" "1:w13=0,\0" "1:w29=60,\0" "1:w33=120,\0",
    /* 10 */ "1:w11=1,1,\0" "1:w24=100000007,\0" "1:w13=1,\0" "2:w11=0,0,\0" "1:w29=600,\0" "1:w33=220,\0",
    /* 11 */ "1:w11=0,0,\0" "1:w24=3750007,\0" "1:w13=0,\0" "2:w11=1,0,\0" "1:w33=120,\0" "2F24\0" "2A28\0",
    /* 12 */ "2:w28=60,\0",
    /* 13 */ "2:w28=1200,\0",
    /* 14 */ "2:w28=60,\0" "2:w32=120,\0",
    /* 15 */ "2:w11=1,0,\0" "2:w28=600,\0" "2:w32=220,\0",
    /* 16 */ "2:w11=0,1,\0" "2:w32=120,\0" "2F25\0" "2A29\0",
    /* 17 */ "2:w29=60,\0",
    /* 18 */ "2:w29=1200,\0",
    /* 19 */ "2:w29=60,\0" "2:w33=120,\0" "2:w11=0,1,\0" "2:w24=3750007,\0" "2:w13=0,\0",
    /* 20 */ "1:w11=0,0,\0" "2:w11=1,1,\0" "2:w24=100000007,\0" "2:w13=1,\0" "2:w29=600,\0" "2:w33=220,\0",
    /* 21 */ "2:w11=0,0,\0" "2:w13=0,\0",
    "", "", "", "", "", "",
    /* 28 */ "1:w11=0,0,\0" "2:w11=0,0,\0",
};

static const char *const k_jig[29] = {
    /*  0 */ "1:w20=0,\0" "2:w21=0,\0" ALL_ON("1"),
    /*  1 */ "1:w11=1,0,\0" "2:w11=0,0,\0" "1F24\0" "1A28\0",
    /*  2 */ "1:w28=60,\0",
    /*  3 */ "1:w28=1200,\0",
    /*  4 */ "1:w20=0,\0" "1:w28=60,\0" "1:w32=120,\0",
    /*  5 */ "1:w11=1,0,\0" "1:w28=600,\0" "1:w32=220,\0" "1:w20=1,\0" "1:w36=0,\0",
    /*  6 */ "1:w20=0,\0" "1:w11=0,1,\0" "1:w32=120,\0" "1F25\0" "1A29\0",
    /*  7 */ "1:w29=60,\0",
    /*  8 */ "1:w29=1200,\0",
    /*  9 */ "1:w21=0,\0" "1:w11=0,1,\0" "1:w24=3750007,\0" "1:w29=60,\0" "1:w33=120,\0",
    /* 10 */ "1:w11=1,1,\0" "1:w24=100000007,\0" "2:w11=0,0,\0" "1:w29=600,\0" "1:w33=220,\0" "1:w21=1,\0" "1:w37=0,\0",
    /* 11 */ "1:w21=0,\0" "1:w11=0,0,\0" "1:w24=3750007,\0" "2:w11=1,0,\0" "1:w33=120,\0" "2F24\0" "2A28\0",
    /* 12 */ "2:w28=60,\0",
    /* 13 */ "2:w28=1200,\0",
    /* 14 */ "2:w20=0,\0" "2:w28=60,\0" "2:w32=120,\0",
    /* 15 */ "2:w11=1,0,\0" "2:w28=600,\0" "2:w32=220,\0" "2:w20=1,\0" "2:w36=0,\0",
    /* 16 */ "2:w20=0,\0" "2:w11=0,1,\0" "2:w32=120,\0" "2F25\0" "2A29\0",
    /* 17 */ "2:w29=60,\0",
    /* 18 */ "2:w29=1200,\0",
    /* 19 */ "2:w21=0,\0" "2:w29=60,\0" "2:w33=120,\0" "2:w11=0,1,\0" "2:w24=3750007,\0",
    /* 20 */ "1:w11=0,0,\0" "2:w11=1,1,\0" "2:w24=100000007,\0" "2:w29=600,\0" "2:w33=220,\0" "2:w21=1,\0" "2:w37=0,\0",
    /* 21 */ "2:w21=0,\0" "2:w11=0,0,\0",
    "", "", "", "", "", "",
    /* 28 */ "1:w11=0,0,\0" "2:w11=0,0,\0",
};

/* Indices after which both test exponents are reset to 0 (same in both functions). */
#define EXP_RESET_MASK ((1u << 0) | (1u << 2) | (1u << 5) | (1u << 7) | (1u << 10) | (1u << 12) \
                        | (1u << 15) | (1u << 17))

/* (u64)pow(10.0, n / 3.0) for n = 0..19 (the menu uses 4..10). */
static const uint32_t k_ampl[20] = {
    1, 2, 4, 10, 21, 46, 100, 215, 464, 1000,
    2154, 4641, 10000, 21544, 46415, 100000, 215443, 464158, 1000000, 2154434,
};

static uint64_t test_freq(void)
{
    unsigned n = g_cal_freq_exp;
    if (n == 0)
        return 3750007u;
    if (n > 46u)   /* DEVIATION (unreachable, menu range 1..9): clamped instead of overflowing */
        n = 46u;
    return ((uint64_t)200000u << n) + 8u;
}

static uint64_t test_ampl(void)
{
    unsigned n = g_cal_ampl_exp;
    if (n == 0)
        return 1200u;
    if (n > 19u)   /* DEVIATION (unreachable, menu range 4..10): clamped */
        n = 19u;
    return k_ampl[n];
}

static void run(const char *const *table)
{
    unsigned idx = g_cal_index;
    if (idx > 28u)
        return;
    for (const char *e = table[idx]; *e; ) {
        unsigned ch = (e[0] == '2') ? 1u : 0u;
        if (e[1] == ':') {
            link_send_line(ch, e + 1);
        } else {
            unsigned id = (unsigned)(e[2] - '0') * 10u + (unsigned)(e[3] - '0');
            link_send_w_u64(ch, id, e[1] == 'F' ? test_freq() : test_ampl());
        }
        link_wait_ok();
        while (*e++) {
        }
    }
    if (EXP_RESET_MASK & (1u << idx)) {
        g_cal_freq_exp = 0;
        g_cal_ampl_exp = 0;
    }
}

void cal_apply_test(void)
{
    run(k_menu);
}

void cal_apply_test_jig(void)
{
    run(k_jig);
}
