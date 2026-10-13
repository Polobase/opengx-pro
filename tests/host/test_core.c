/* Host tests of the modules both firmwares share: the program runner's generator lines, the
 * PC-mode sequence, the calibration-menu keys, number formatting, the link hooks and the
 * non-blocking SPI-flash primitives. The runner tests are characterization tests: they pin the
 * exact bytes of the classic runner (docs/spec/protocols.md §5.3). */
#include "harness.h"

#include "app.h"
#include "core_calmenu.h"
#include "core_pcmode.h"
#include "fmt.h"
#include "link.h"
#include "mock_board.h"
#include "runner_seq.h"
#include "spiflash.h"
#include "state.h"

/* ---- program runner ---- */

static void runner_setup(void)
{
    reset_world();
    auto_ack = 1;
    g_ui_state = ST_CH1;
    struct program *p = &g_ch[0].prog;
    p->hdr[HDR_WAVE] = 41;
    p->hdr[HDR_AMPL] = 2000;
    p->hdr[HDR_OFFS] = 120;
    p->hdr[HDR_DWELL] = 180;
    p->hdr[HDR_COUNT] = 2;
    p->freq[0] = 727000000000ull;
    p->freq[1] = 728000000000ull;
    memset(g_ch[0].params, 0xFF, sizeof g_ch[0].params);
    g_ch[0].prog_slot = 1;
}

#define IDLE_727                                                                             \
    "1>:w24=727000000000,\r\n[W]\n1>:w11=0,0,\r\n[W]\n1>:w14=1,\r\n[W]\n1>:w17=0,1,\r\n[W]\n" \
    "1>:w20=41,\r\n[W]\n1>:w21=41,\r\n[W]\n1>:w28=2000,\r\n[W]\n1>:w29=2000,\r\n[W]\n"         \
    "1>:w32=120,\r\n[W]\n1>:w33=120,\r\n[W]\n"
#define PLAY_727_00 "1>:w24=727000000000,\r\n[W]\n1>:w11=1,1,\r\n[W]\n1>:w12=0,0,\r\n[W]\n"

void test_runner(void)
{
    /* first pass after loading: idle configuration of step 1, outputs off */
    runner_setup();
    runner_tick();
    CHECK_BYTES(ev_log, ev_len, IDLE_727);
    CHECK_EQ(g_ch[0].step, 1);
    CHECK_EQ(g_ch[0].step_sent, 1);
    CHECK_EQ(g_ch[0].step_freq, 727000000000ull);
    CHECK(g_ui_flags & UIF_REDRAW);
    CHECK_EQ(g_ch[0].params[0], 0);        /* the step's 0xFFFF pair is zeroed in RAM */
    CHECK_EQ(g_ch[0].params[1], 0);
    CHECK_EQ(g_ch[0].params[2], 0xFFFF);   /* other steps untouched */

    /* nothing changed: nothing sent */
    tx_clear();
    runner_tick();
    CHECK_EQ(ev_len, 0);

    /* START: step 1 playing, pair (0,0) -> gating off */
    tx_clear();
    g_ui_flags |= UIF_RUN_G1;
    g_ch[0].step_sent = 0xFF;
    runner_tick();
    CHECK_BYTES(ev_log, ev_len, PLAY_727_00);

    /* one dwell later (180 s = 18000 units of 10.1 ms): step 2 with gating pair (12, 40) */
    tx_clear();
    g_ch[0].params[2] = 12;
    g_ch[0].params[3] = 40;
    g_ch[0].elapsed = 18000;
    runner_tick();
    CHECK_BYTES(ev_log, ev_len,
                "1>:w24=728000000000,\r\n[W]\n1>:w11=1,1,\r\n[W]\n"
                "1>:w50=00012,00040,\r\n[W]\n1>:w51=00012,00040,\r\n[W]\n1>:w12=1,1,\r\n[W]\n");
    CHECK_EQ(g_ch[0].step, 2);

    /* end without loop: outputs off, stopped (elapsed = 1), next pass idle config of step 1 */
    tx_clear();
    g_ch[0].elapsed = 36000;
    runner_tick();
    CHECK_BYTES(ev_log, ev_len, "1>:w11=0,0,\r\n[W]\n");
    CHECK_EQ(g_ui_flags & UIF_RUN_G1, 0);
    CHECK_EQ(g_ch[0].elapsed, 1);
    tx_clear();
    runner_tick();
    CHECK_BYTES(ev_log, ev_len, IDLE_727);

    /* end with loop: no "outputs off", keeps running and restarts at step 1 */
    tx_clear();
    g_ui_flags |= UIF_RUN_G1 | UIF_LOOP_G1;
    g_ch[0].step_sent = 2;   /* step 2 was playing */
    g_ch[0].elapsed = 36000;
    runner_tick();
    CHECK_EQ(ev_len, 0);
    CHECK(g_ui_flags & UIF_RUN_G1);
    CHECK_EQ(g_ch[0].elapsed, 1);
    runner_tick();
    CHECK_BYTES(ev_log, ev_len, PLAY_727_00);

    /* G1 before G2 in one pass; G2 takes the gating pair of its own step (DEVIATION) */
    runner_setup();
    g_ch[1] = g_ch[0];
    g_ch[1].prog.freq[1] = 900000000000ull;
    g_ch[1].elapsed = 18000;
    g_ch[1].params[2] = 7;
    g_ch[1].params[3] = 9;
    g_ui_flags |= UIF_RUN_G1 | UIF_RUN_G2;
    runner_tick();
    CHECK_BYTES(ev_log, ev_len,
                PLAY_727_00
                "2>:w24=900000000000,\r\n[W]\n2>:w11=1,1,\r\n[W]\n"
                "2>:w50=00007,00009,\r\n[W]\n2>:w51=00007,00009,\r\n[W]\n2>:w12=1,1,\r\n[W]\n");

    /* dwell 0 counts as 1 s (DEVIATION): 100 units = step 2 */
    runner_setup();
    g_ch[0].prog.hdr[HDR_DWELL] = 0;
    g_ch[0].elapsed = 100;
    runner_tick();
    CHECK(ev_len > 20 && strncmp(ev_log, "1>:w24=728000000000,", 20) == 0);

    /* no redraw request while the program list is shown */
    runner_setup();
    g_ui_state = ST_LIST;
    runner_tick();
    CHECK_EQ(g_ui_flags & UIF_REDRAW, 0);
}

/* ---- runner command sequences ---- */

void test_runner_seq(void)
{
    struct program p;
    memset(&p, 0, sizeof p);
    p.hdr[HDR_WAVE] = 43;
    p.hdr[HDR_AMPL] = 500;
    p.hdr[HDR_OFFS] = 120;
    struct seq s;

    seq_idle(&s, 36000000000ull, &p);
    static const char *const idle[10] = {
        ":w24=36000000000,\r\n", ":w11=0,0,\r\n", ":w14=1,\r\n", ":w17=0,1,\r\n", ":w20=43,\r\n",
        ":w21=43,\r\n", ":w28=500,\r\n", ":w29=500,\r\n", ":w32=120,\r\n", ":w33=120,\r\n",
    };
    CHECK_EQ(s.n, 10);
    for (unsigned i = 0; i < 10; i++)
        CHECK_BYTES(s.line[i], s.len[i], idle[i]);

    seq_play(&s, 5, 0, 0);
    CHECK_EQ(s.n, 3);
    CHECK_BYTES(s.line[0], s.len[0], ":w24=5,\r\n");
    CHECK_BYTES(s.line[1], s.len[1], ":w11=1,1,\r\n");
    CHECK_BYTES(s.line[2], s.len[2], ":w12=0,0,\r\n");

    seq_play(&s, 5, 0, 7);   /* any nonzero value switches gating on */
    CHECK_EQ(s.n, 5);
    CHECK_BYTES(s.line[2], s.len[2], ":w50=00000,00007,\r\n");
    CHECK_BYTES(s.line[3], s.len[3], ":w51=00000,00007,\r\n");
    CHECK_BYTES(s.line[4], s.len[4], ":w12=1,1,\r\n");

    seq_off(&s);
    CHECK_EQ(s.n, 1);
    CHECK_BYTES(s.line[0], s.len[0], ":w11=0,0,\r\n");

    seq_idle(&s, UINT64_MAX, &p);   /* the longest line still fits */
    CHECK_BYTES(s.line[0], s.len[0], ":w24=18446744073709551615,\r\n");
}

/* ---- number formatting ---- */

void test_fmt(void)
{
    char buf[32];
    char *p = fmt_put_fixed(buf, 7, 5);
    CHECK_BYTES(buf, (size_t)(p - buf), "00007");
    p = fmt_put_fixed(buf, 123456, 5);   /* the last five digits, as the wire format needs */
    CHECK_BYTES(buf, (size_t)(p - buf), "23456");
    p = fmt_put_u64(buf, 0);
    CHECK_BYTES(buf, (size_t)(p - buf), "0");
    p = fmt_put_u64(buf, 10000000000000000ull);
    CHECK_BYTES(buf, (size_t)(p - buf), "10000000000000000");
    p = fmt_put_u64(buf, UINT64_MAX);
    CHECK_BYTES(buf, (size_t)(p - buf), "18446744073709551615");
    CHECK_EQ(fmt_crc32(0, (const uint8_t *)"123456789", 9), 0xCBF43926u);
    CHECK_EQ(fmt_crc32(fmt_crc32(0, (const uint8_t *)"1234", 4), (const uint8_t *)"56789", 5), 0xCBF43926u);
}

/* ---- link extension points ---- */

#define CHECK_TX(p, exp) CHECK_BYTES(tx_log[p], tx_len[p], (exp))

static unsigned tap_count[2], ok_hook_count[2];
static char tap_seen[2][64];

void test_rx_tap(unsigned ch, uint8_t c)   /* -DLINK_RX_TAP=test_rx_tap */
{
    if (tap_count[ch] < sizeof tap_seen[ch])
        tap_seen[ch][tap_count[ch]] = (char)c;
    tap_count[ch]++;
}

void test_ok_hook(unsigned ch)             /* -DLINK_OK_HOOK=test_ok_hook */
{
    ok_hook_count[ch]++;
}

void test_link_hooks(void)
{
    reset_world();
    memset(tap_count, 0, sizeof tap_count);
    memset(ok_hook_count, 0, sizeof ok_hook_count);

    /* the tap sees every byte of its own link; a '#' line leaves the parser idle */
    feed(1, "#hi\r\n");
    CHECK_EQ(tap_count[1], 5);
    CHECK_EQ(tap_count[0], 0);
    CHECK_BYTES(tap_seen[1], tap_count[1], "#hi\r\n");
    CHECK(link_rx_idle(1));
    CHECK_EQ(g_ui_flags & UIF_LINE_G2, 0);

    /* ":ok" runs the hook of its link */
    feed(0, ":ok\r\n");
    CHECK_EQ(ok_hook_count[0], 1);
    CHECK_EQ(ok_hook_count[1], 0);

    /* while a line is pending the parser drops bytes; the tap still sees them */
    feed(0, ":n00=$\r\n");
    CHECK(g_ui_flags & UIF_LINE_G1);
    CHECK(link_rx_idle(0));
    unsigned before = tap_count[0];
    feed(0, ":w24=5,\r\n");
    CHECK_EQ(tap_count[0], before + 9);
    CHECK_EQ(g_ch[0].pc_freq[0], 0);   /* dropped by the parser */
    link_process(0);
    CHECK_TX(0, ":Rev201\r\n");

    /* inside a frame the parser is not idle */
    feed(0, ":w24=");
    CHECK(!link_rx_idle(0));
    feed(0, "7,\r\n");
    CHECK(link_rx_idle(0));
    CHECK_EQ(g_ch[0].pc_freq[0], 7);

    /* pure builders and raw send */
    char buf[32];
    unsigned n = link_fmt_w_u64(buf, 24, UINT64_MAX);
    CHECK_BYTES(buf, n, ":w24=18446744073709551615,\r\n");
    n = link_fmt_w_pair5(buf, 50, 12, 40);
    CHECK_BYTES(buf, n, ":w50=00012,00040,\r\n");
    tx_clear();
    link_send_raw(1, ":w11=0,0,\r\n", 11);
    CHECK_TX(1, ":w11=0,0,\r\n");
    CHECK_EQ(tx_len[0], 0);
}

/* ---- PC-mode entry and exit ---- */

void test_pcmode(void)
{
    reset_world();
    auto_ack = 1;
    g_ui_state = ST_CH1;
    g_ui_flags |= UIF_RUN_G1 | UIF_LOOP_G2;
    g_ch[0].elapsed = 500;
    g_ch[0].prog_slot = 3;
    g_ch[1].elapsed = 7;
    g_ch[1].prog_slot = 9;

    CHECK_EQ(core_pcmode_update(0), PCM_NONE);
    CHECK_EQ(ev_len, 0);

    /* entry: runs stopped, 13.3 ms, outputs off on G1 then G2 (each acknowledged), 13.3 ms, PB2 low */
    CHECK_EQ(core_pcmode_update(1), PCM_ENTERED);
    CHECK_EQ(g_ui_state, ST_PC);
    CHECK_EQ(g_ui_flags & (UIF_RUN_G1 | UIF_RUN_G2 | UIF_LOOP_G1 | UIF_LOOP_G2), 0);
    CHECK(g_ui_flags & UIF_REDRAW);
    CHECK_EQ(g_ch[0].elapsed, 0);
    CHECK_EQ(g_ch[0].prog_slot, 0);
    CHECK_EQ(g_ch[1].elapsed, 0);
    CHECK_EQ(g_ch[1].prog_slot, 0);
    CHECK_BYTES(ev_log, ev_len, "1>:w11=0,0,\r\n[W]\n2>:w11=0,0,\r\n[W]\n");
    CHECK_EQ(delay_calls_34155, 2);
    CHECK_EQ(mock_gpiob.ODR & (1u << HANDSHAKE_PIN), 0);

    tx_clear();
    CHECK_EQ(core_pcmode_update(1), PCM_NONE);
    CHECK_EQ(ev_len, 0);

    /* exit: PB2 high (stays high), w15=0,0 on G1 then G2, PC mirrors cleared, HOME */
    g_ui_flags &= (uint16_t)~UIF_REDRAW;
    g_ch[0].pc_freq[0] = 5;
    g_ch[1].pc_freq[1] = 6;
    g_pc_flags |= 0x03C0u;
    CHECK_EQ(core_pcmode_update(0), PCM_LEFT);
    CHECK_EQ(g_ui_state, ST_HOME);
    CHECK(g_ui_flags & UIF_REDRAW);
    CHECK_EQ(g_pc_flags & 0x03C0u, 0);
    CHECK_EQ(g_ch[0].pc_freq[0], 0);
    CHECK_EQ(g_ch[1].pc_freq[1], 0);
    CHECK_BYTES(ev_log, ev_len, "1>:w15=0,0,\r\n[W]\n2>:w15=0,0,\r\n[W]\n");
    CHECK(mock_gpiob.ODR & (1u << HANDSHAKE_PIN));
}

/* ---- calibration-menu keys ---- */

/* TX of cal_apply_test() for the current g_cal_* state, as a C string in out. */
static void cal_test_tx(char *out, size_t cap)
{
    tx_clear();
    cal_apply_test();
    size_t n = ev_len < cap - 1u ? ev_len : cap - 1u;
    memcpy(out, ev_log, n);
    out[n] = 0;
}

void test_calmenu(void)
{
    static char expected[2048];

    /* DOWN / UP: next / previous index (wrapping 0..28), that index's test signal, redraw */
    reset_world();
    auto_ack = 1;
    g_ui_state = ST_CAL;
    g_cal_index = 1;
    cal_test_tx(expected, sizeof expected);
    g_cal_index = 0;
    tx_clear();
    CHECK_EQ(core_calmenu_key(KEY_DOWN, 0), 1);
    CHECK_EQ(g_cal_index, 1);
    CHECK_BYTES(ev_log, ev_len, expected);
    g_cal_index = 28;
    CHECK_EQ(core_calmenu_key(KEY_DOWN, 0), 1);
    CHECK_EQ(g_cal_index, 0);
    CHECK_EQ(core_calmenu_key(KEY_UP, 0), 1);
    CHECK_EQ(g_cal_index, 28);
    CHECK_EQ(core_calmenu_key(KEY_UP, 0), 1);
    CHECK_EQ(g_cal_index, 27);

    /* held UP/DOWN do nothing: only G1/G2 auto-repeat */
    tx_clear();
    CHECK_EQ(core_calmenu_key(KEY_DOWN, 1), 0);
    CHECK_EQ(g_cal_index, 27);
    CHECK_EQ(ev_len, 0);

    /* G1 +1 / G2 -1, sent at once without :ok wait; the 3rd and 4th value of each group of
     * five count the other way round */
    g_cal_index = 1;
    tx_clear();
    CHECK_EQ(core_calmenu_key(KEY_G1, 0), 1);
    CHECK_EQ(g_cal_table[1], 1663);
    CHECK_BYTES(ev_log, ev_len, "1>:w60=1112111,01,01663,\r\n");
    CHECK_EQ(g_misc_flags & MISC_CAL_INVERT, 0);
    CHECK_EQ(core_calmenu_key(KEY_G2, 1), 1);   /* auto-repeat */
    CHECK_EQ(g_cal_table[1], 1662);
    g_cal_index = 3;
    tx_clear();
    CHECK_EQ(core_calmenu_key(KEY_G1, 0), 1);
    CHECK(g_misc_flags & MISC_CAL_INVERT);
    CHECK_EQ(g_cal_table[3], 495);
    CHECK_BYTES(ev_log, ev_len, "1>:w60=1112111,03,00495,\r\n");

    /* no key: only the direction flag follows the index */
    g_cal_index = 21;
    tx_clear();
    CHECK_EQ(core_calmenu_key(KEY_NONE, 0), 0);
    CHECK_EQ(g_misc_flags & MISC_CAL_INVERT, 0);
    CHECK_EQ(ev_len, 0);

    /* START saves; index 26 also writes the backup copy */
    g_cal_index = 26;
    tx_clear();
    CHECK_EQ(core_calmenu_key(KEY_START, 0), 0);
    CHECK_BYTES(ev_log, ev_len, "1>:w61=1114111,\r\n");
    g_cal_index = 5;
    tx_clear();
    CHECK_EQ(core_calmenu_key(KEY_START, 0), 0);
    CHECK_BYTES(ev_log, ev_len, "1>:w61=1113111,\r\n");

    /* LOOP cycles the amplitude exponent (0 -> 4 .. 10 -> 4), STOP the frequency exponent
     * (0 -> 1 .. 9 -> 1); each is followed by the test signal */
    static const struct { enum key_code key; uint8_t f0, a0, f1, a1; } cyc[] = {
        { KEY_LOOP, 0, 0, 0, 4 }, { KEY_LOOP, 0, 5, 0, 6 }, { KEY_LOOP, 0, 10, 0, 4 },
        { KEY_STOP, 0, 0, 1, 0 }, { KEY_STOP, 3, 0, 4, 0 }, { KEY_STOP, 9, 0, 1, 0 },
    };
    for (unsigned k = 0; k < sizeof cyc / sizeof cyc[0]; k++) {
        g_cal_index = 1;
        g_cal_freq_exp = cyc[k].f1;
        g_cal_ampl_exp = cyc[k].a1;
        cal_test_tx(expected, sizeof expected);
        uint8_t f_after = g_cal_freq_exp, a_after = g_cal_ampl_exp;
        g_cal_freq_exp = cyc[k].f0;
        g_cal_ampl_exp = cyc[k].a0;
        tx_clear();
        CHECK_EQ(core_calmenu_key(cyc[k].key, 0), 0);
        CHECK_BYTES(ev_log, ev_len, expected);
        CHECK_EQ(g_cal_freq_exp, f_after);
        CHECK_EQ(g_cal_ampl_exp, a_after);
    }
}

/* ---- non-blocking SPI-flash primitives and hooks ---- */

static unsigned flash_acquire_calls, flash_poll_calls;

void test_flash_acquire(void)   /* -DSPIFLASH_ACQUIRE=test_flash_acquire */
{
    flash_acquire_calls++;
}

void test_flash_poll(void)      /* -DSPIFLASH_POLL_HOOK=test_flash_poll */
{
    flash_poll_calls++;
}

void test_spiflash_async(void)
{
    reset_world();
    CHECK_EQ(spiflash_busy(), 0);
    spiflash_erase_begin(0x80123);   /* the sector containing the address */
    CHECK_EQ(flash_erases, 1);
    unsigned polls = 0;
    while (spiflash_busy())
        polls++;
    CHECK_EQ(polls, 5);              /* the model stays busy for 5 status reads after an erase */
    CHECK(flash_is_erased(0x80000, 0x1000));

    static const uint8_t data[4] = { 1, 2, 3, 4 };
    CHECK_EQ(spiflash_program_begin(0x800FE, data, 4), 2);   /* stops at the page end */
    while (spiflash_busy()) {
    }
    CHECK_EQ(spiflash_program_begin(0x80100, data + 2, 2), 2);
    while (spiflash_busy()) {
    }
    uint8_t back[4];
    flash_acquire_calls = flash_poll_calls = 0;
    spiflash_read(0x800FE, back, sizeof back);
    CHECK(memcmp(back, data, sizeof back) == 0);
    CHECK_EQ(flash_acquire_calls, 1);

    /* blocking calls: acquire first, poll hook while waiting */
    CHECK_EQ(spiflash_sector_erase(0x81000), 0);
    CHECK_EQ(flash_acquire_calls, 2);
    CHECK(flash_poll_calls >= 5);
    CHECK_EQ(flash_violations, 0);
}
