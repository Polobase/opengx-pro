/* The nx main loop and core (src/nx/nx_loop.c, nx_core.c) on the host board model: boot gate,
 * boot idle configuration on both links in parallel, the order of link work, PC-mode entry and
 * exit, the calibration boot and menu keys, power-off. */
#include "harness.h"

#include "mock_board.h"
#include "nx_config.h"
#include "nx_core.h"
#include "nx_settings.h"
#include "nx_timer.h"
#include "nx_diag.h"
#include "nx_keys.h"
#include "nx_time.h"
#include "nx_txq.h"
#include "nx_world.h"
#include "state.h"
#include "test_nx.h"

#define PB2 (1u << HANDSHAKE_PIN)

static char lines[2][64][32];
static unsigned nlines[2];

static void grab(void)
{
    for (unsigned ch = 0; ch < 2u; ch++)
        nlines[ch] = nxw_lines(ch, lines[ch], 64);
}

static int line_is(unsigned ch, unsigned i, const char *s)
{
    return i < nlines[ch] && strcmp(lines[ch][i], s) == 0;
}

static const char *const k_idle_empty[10] = {
    ":w24=0,", ":w11=0,0,", ":w14=1,", ":w17=0,1,", ":w20=0,", ":w21=0,", ":w28=0,", ":w29=0,",
    ":w32=120,", ":w33=120,",
};

static void test_boot_gate(void)
{
    /* nothing is sent or answered before NX_GEN_READY_MS: a query waits, like the original's splash */
    nxw_reset();
    nxw_run_until(1000);
    nxw_rx(0, ":n00=$\r\n");
    nxw_run_until(NX_GEN_READY_MS - 1u);
    CHECK_EQ(tx_len[0] + tx_len[1], 0);
    CHECK(!nx_core_ready());
    CHECK(g_ui_flags & UIF_LINE_G1);
    CHECK_EQ(g_ui_state, ST_BOOT);
    /* at the gate: the empty program's idle configuration on both links at once, then the reply
     * on U1 after its own lines (the original's runner comes before link work) */
    nxw_run_until(NX_GEN_READY_MS + 100u);
    CHECK(nx_core_ready());
    CHECK_EQ(g_ui_state, ST_HOME);
    grab();
    CHECK_EQ(nlines[0], 11);
    CHECK_EQ(nlines[1], 10);
    int idle_ok = 1;
    for (unsigned i = 0; i < 10u; i++)
        idle_ok &= line_is(0, i, k_idle_empty[i]) && line_is(1, i, k_idle_empty[i]);
    CHECK(idle_ok);
    CHECK(line_is(0, 10, ":Rev201"));
    CHECK(nxw_line_us[0][0] >= NX_GEN_READY_MS * 1000u && nxw_line_us[0][0] < NX_GEN_READY_MS * 1000u + 1000u);
    CHECK_EQ(nxw_line_us[1][0], nxw_line_us[0][0]);
    /* each line ~1 ms on the wire, :ok 1 ms later, 133 us tail: ten lines in about 25 ms */
    CHECK(nxw_line_us[0][9] - nxw_line_us[0][0] < 30000u);
    CHECK_EQ(g_nx_diag.txq_rejected_pc, 0);
    CHECK(nxw_wdg_feeds > 1000u);

    /* a factory-port command opens the gate early (the original's splash ends at it) */
    nxw_reset();
    nxw_run_until(2000);
    nxw_rx(2, ":a1\n");
    nxw_run_until(2100);
    CHECK(nx_core_ready());
    grab();
    CHECK_EQ(nlines[0], 10);
    CHECK_EQ(tx_len[2], 1);
    CHECK_EQ(tx_log[2][0], 'Z');
}

static void load_and_play(unsigned ch, unsigned slot)
{
    struct nx_action a = { NXA_LOAD, (uint8_t)ch, (uint16_t)slot, 0, 0 };
    CHECK_EQ(nx_core_act(&a), NX_OK);
    a.type = NXA_PLAY;
    CHECK_EQ(nx_core_act(&a), NX_OK);
}

static void test_link_order(void)
{
    /* a line waits while its own link sends a sequence; the other link answers at once */
    nxw_reset();
    nxw_gen(0, NXW_NOACK, 0);                 /* G1 without :ok: 40 ms per line */
    nxw_run_until(NX_GEN_READY_MS + 10u);
    nxw_rx(0, ":n00=$\r\n");
    nxw_rx(1, ":n00=$\r\n");
    nxw_run_ms(20);
    grab();
    CHECK(line_is(1, 10, ":Rev201"));
    CHECK(!line_is(0, 10, ":Rev201"));
    CHECK(g_ui_flags & UIF_LINE_G1);
    nxw_run_ms(420);                          /* G1's ten lines take ten timeouts */
    grab();
    CHECK(line_is(0, 10, ":Rev201"));
    CHECK_EQ(g_nx_diag.ok_timeouts[0], 10);
    CHECK_EQ(g_nx_diag.ok_timeouts[1], 0);
}

static void test_pc_mode(void)
{
    nxw_reset();
    nxw_run_until(NX_GEN_READY_MS + 100u);
    load_and_play(0, 4);
    nxw_run_ms(3);                            /* G1's sequence has started */
    CHECK(!nx_txq_idle(0));
    nxw_clear();
    nxw_pb1 = 1;
    nxw_run_ms(14);
    CHECK_EQ(g_ui_state, ST_HOME);            /* PB1 not yet stable for 15 ms */
    nxw_run_ms(200);
    CHECK_EQ(g_ui_state, ST_PC);
    grab();
    /* the entry comes after the running sequences, and ends them: nothing but ":w11=0,0," after it */
    unsigned n0 = nlines[0];
    CHECK(n0 >= 1u && line_is(0, n0 - 1u, ":w11=0,0,"));
    CHECK(line_is(1, nlines[1] - 1u, ":w11=0,0,"));
    CHECK_EQ(mock_gpiob.ODR & PB2, 0);        /* PB2 back low: PC connected to the links */
    CHECK_EQ(g_ui_flags & (UIF_RUN_G1 | UIF_RUN_G2), 0);
    /* PC traffic: answered at once; the runner and the queue stay silent */
    nxw_clear();
    nxw_rx(0, ":w24=3750007,\r\n");
    nxw_rx(1, ":n05=?\r\n");
    nxw_run_ms(5);
    CHECK_EQ(g_ch[0].pc_freq[0], 3750007u);
    grab();
    CHECK_EQ(nlines[0], 0);
    CHECK(nlines[1] == 1u && strncmp(lines[1][0], ":n05=", 5) == 0);
    /* an upload in PC mode fills G1's buffers and forces a resend after the exit */
    nxw_rx(0, ":g09=0,0,5,6,\r\n");
    nxw_run_ms(5);
    nxw_rx(0, ":p09=41,2000,120,1,2,3750007,1000000007,\r\n");
    nxw_run_ms(100);
    nxw_clear();
    nxw_pb1 = 0;
    nxw_run_ms(100);
    CHECK_EQ(g_ui_state, ST_HOME);
    CHECK(mock_gpiob.ODR & PB2);              /* stays high after the exit, as in the original */
    grab();
    CHECK(line_is(0, 0, ":w15=0,0,") && line_is(1, 0, ":w15=0,0,"));
    CHECK_EQ(nlines[1], 1);
    static const char *const idle_upload[10] = {
        ":w24=3750007,", ":w11=0,0,", ":w14=1,", ":w17=0,1,", ":w20=41,", ":w21=41,", ":w28=2000,",
        ":w29=2000,", ":w32=120,", ":w33=120,",
    };
    int resent = nlines[0] == 11u;
    for (unsigned i = 0; i < 10u; i++)
        resent &= line_is(0, 1u + i, idle_upload[i]);
    CHECK(resent);
    CHECK_EQ(g_nx_diag.txq_rejected_pc, 0);
}

static void test_cal_boot(void)
{
    /* G1+G2 held at power-on until 1500 ms: calibration menu, not gated by NX_GEN_READY_MS */
    nxw_reset();
    nxw_keys = NX_KEY_G1 | NX_KEY_G2;
    nxw_keys_at(1500000u, 0);
    nxw_rx_at(1560000u, 0, ":w80=13583792,32001,1601,301,401,1001,601,1602,302,402,1002,602,1603,303,"
                           "403,1003,603,1604,304,404,1004,604,\r\n");
    sim_advance_ms(300);                      /* main_nx calls it after lcd_init */
    CHECK_EQ(nx_core_boot(), 1);
    CHECK_EQ(g_ui_state, ST_CAL);
    CHECK(nx_core_ready());
    CHECK(mock_gpiob.ODR & PB2);
    grab();
    CHECK(line_is(0, 0, ":r80=0,"));
    CHECK(nxw_line_us[0][0] >= 1500000u && nxw_line_us[0][0] < 1502000u);
    CHECK_EQ(g_cal_table[0], 32001);          /* G1's reply was processed before the test signal */
    CHECK(line_is(0, 1, ":w11=1,1,") && line_is(1, 0, ":w11=1,1,"));   /* index 0: all outputs on */
    CHECK(nxw_line_us[0][1] >= 1560000u);    /* the wait ends with the reply (at most 110 ms) */
    /* the menu: DOWN = next index and its test signal, G1 = value +1 on :w60, START = save */
    nxw_clear();
    nxw_press(NX_KEY_DOWN, 150);
    CHECK_EQ(g_cal_index, 1);
    grab();
    CHECK(line_is(0, 0, ":w11=1,0,"));
    nxw_clear();
    nxw_press(NX_KEY_G1, 150);                /* no chord delay in the menu */
    grab();
    CHECK(line_is(0, 0, ":w60=1112111,01,01602,"));
    CHECK(nxw_line_us[0][0] - (sim_us - 250000u) < 30000u);
    nxw_clear();
    nxw_press(NX_KEY_START, 150);
    grab();
    CHECK(line_is(0, 0, ":w61=1113111,"));
    /* the runner stays off in the menu */
    CHECK(nx_txq_idle(0) && nx_txq_idle(1));

    /* without the chord at boot: no calibration */
    nxw_reset();
    nxw_keys = NX_KEY_G1;
    CHECK_EQ(nx_core_boot(), 0);
    CHECK_EQ(g_ui_state, ST_BOOT);
}

static void test_power_off(void)
{
    /* POWER held 0.8 s: goodbye for NX_GOODBYE_MS, then the power-off signal; nothing else runs */
    nxw_reset();
    nxw_run_until(5000);
    volatile uint64_t pressed = sim_us;
    nxw_power_off_armed = 1;
    if (setjmp(nxw_power_off_jmp) == 0) {
        nxw_keys = NX_KEY_POWER;
        nxw_run_ms(900);
        CHECK(0);                             /* not reached: nx_core_power_off() does not return */
    } else {
        uint64_t dt = nxw_power_off_us - pressed;
        CHECK(dt >= (NX_KEY_POWER_OFF_MS + NX_GOODBYE_MS) * 1000u);
        CHECK(dt < (NX_KEY_POWER_OFF_MS + NX_GOODBYE_MS + 40u) * 1000u);
    }
    /* a query arriving during the goodbye is never answered */
    nxw_reset();
    nxw_run_until(5000);
    nxw_power_off_armed = 1;
    nxw_clear();
    if (setjmp(nxw_power_off_jmp) == 0) {
        struct nx_action off = { NXA_POWER_OFF, 0, 0, 0, 0 };
        nx_core_act(&off);
        nxw_rx_at(sim_us + 500000u, 0, ":n00=$\r\n");
        nxw_run_ms(10);
        CHECK(0);
    }
    CHECK_EQ(tx_len[0], 0);
}

/* Power-off with a program loaded: as the original's POWER press, both generators get their
 * idle configuration (outputs off) before the goodbye. */
static void test_power_off_stops_both(void)
{
    nxw_reset();
    nxw_run_until(NX_GEN_READY_MS + 100u);
    load_and_play(0, 4);
    nxw_run_ms(200);
    CHECK(g_ui_flags & UIF_RUN_G1);
    nxw_clear();
    nxw_power_off_armed = 1;
    if (setjmp(nxw_power_off_jmp) == 0) {
        struct nx_action off = { NXA_POWER_OFF, 0, 0, 0, 0 };
        nx_core_act(&off);
        nxw_run_ms(5);
        CHECK(0);
    }
    grab();
    CHECK_EQ(g_ui_flags & (UIF_RUN_G1 | UIF_RUN_G2), 0);
    int off0 = 0, off1 = 0;
    for (unsigned i = 0; i < nlines[0]; i++)
        off0 |= line_is(0, i, ":w11=0,0,");
    for (unsigned i = 0; i < nlines[1]; i++)
        off1 |= line_is(1, i, ":w11=0,0,");
    CHECK(off0 && off1);
    CHECK(nlines[0] >= 10u && nlines[1] >= 10u);   /* the whole idle configuration, both links */
}

static void test_power_on_hold(void)
{
    /* POWER already down at power-on (POWER is the key that turns the board on, so it is usually
     * still down when the app starts, docs/nx/design/hardware.md) must not power the unit off by
     * itself: nx_keys.c ignores a key already down at the very first sample until it has been
     * released once. */
    nxw_reset();
    nxw_keys = NX_KEY_POWER;
    nxw_run_ms(1500);
    CHECK(!nx_core_power_off_due());
    nxw_keys = 0;
    nxw_run_ms(50);
    CHECK(!nx_core_power_off_due());

    /* released once: a later, fresh 0.8 s hold still powers off normally, exactly as a press that
     * starts well after boot (test_power_off() above) */
    volatile uint64_t pressed = sim_us;
    nxw_power_off_armed = 1;
    if (setjmp(nxw_power_off_jmp) == 0) {
        nxw_keys = NX_KEY_POWER;
        nxw_run_ms(900);
        CHECK(0);                             /* not reached: nx_core_power_off() does not return */
    } else {
        uint64_t dt = nxw_power_off_us - pressed;
        CHECK(dt >= (NX_KEY_POWER_OFF_MS + NX_GOODBYE_MS) * 1000u);
        CHECK(dt < (NX_KEY_POWER_OFF_MS + NX_GOODBYE_MS + 40u) * 1000u);
    }
}

/* Two session handshakes close together (Spooky2 opening G1 and G2): each :w92 raises PB2 and
 * sends :w91=0, to its generator, whose :w91 answer lowers PB2 again. Like the original, which
 * handles that answer only after its next 13.3 ms key-scan delay, nx holds a :w91 for
 * NX_W91_HOLD_MS after the last :w92 sequence, so PB2 stays high (the PC cut off) until the
 * second generator's answer is over. */
static void test_w92_pair(void)
{
    nxw_reset();
    nxw_gen(0, NXW_NOACK, 0);
    nxw_gen(1, NXW_NOACK, 0);
    nxw_pb1 = 1;
    nxw_run_until(NX_GEN_READY_MS + 300u);
    CHECK_EQ(g_ui_state, ST_PC);
    CHECK_EQ(mock_gpiob.ODR & PB2, 0);
    nxw_clear();
    uint64_t t0 = sim_us;
    nxw_rx_at(t0, 0, ":w92=111111111.\r\n");
    nxw_rx_at(t0 + 5000u, 1, ":w92=222222222.\r\n");
    uint64_t low_at = 0, seen0 = 0, seen1 = 0;
    for (uint64_t end = t0 + 150000u; sim_us < end;) {
        nxw_step(50);
        if (!seen0 && nxw_line_n[0] >= 1u) {   /* G1's generator answers its :w91=0, 2 ms later */
            seen0 = nxw_line_end_us[0][0];
            nxw_rx_at(seen0 + 2000u, 0, ":w91=1,\r\n");
        }
        if (!seen1 && nxw_line_n[1] >= 1u) {
            seen1 = nxw_line_end_us[1][0];
            nxw_rx_at(seen1 + 2000u, 1, ":w91=1,\r\n");
        }
        if (seen0 && !low_at && !(mock_gpiob.ODR & PB2))
            low_at = sim_us;
    }
    grab();
    CHECK(line_is(0, 0, ":w91=0,") && line_is(1, 0, ":w91=0,"));
    CHECK(seen1 > seen0);
    CHECK(low_at != 0);
    /* PB2 goes low only after the second sequence: at least the original's 13.3 ms after G2's
     * :w91=0, (its generator answered 2 ms after it) */
    CHECK(low_at >= seen1 + 13300u);
    CHECK(low_at < seen1 + 40000u);
    CHECK(g_nx_diag.w91_held >= 1u);
    nxw_pb1 = 0;
    nxw_run_ms(200);
}

#if NX_FEAT_TIMERS
static void run_s(unsigned s)   /* one main-loop pass per simulated millisecond */
{
    for (unsigned i = 0; i < s * 1000u; i++)
        nxw_step(1000);
}

/* "Stop after": running time counts, paused time does not; at the limit the generator stops.
 * "Power off when all finished": then the countdown card powers off after NX_DONE_OFF_MS, and
 * a key cancels it. */
static void test_timers(void)
{
    nxw_reset();
    g_nx_settings.session_limit_min[0] = 1;
    nxw_run_until(NX_GEN_READY_MS + 100u);
    struct nx_action a = { NXA_LOAD, 0, 4, 0, 0 };
    CHECK_EQ(nx_core_act(&a), NX_OK);
    a.type = NXA_PLAY;
    a.v = 1;   /* looped: only the timer ends it */
    CHECK_EQ(nx_core_act(&a), NX_OK);
    run_s(30);
    CHECK(g_ui_flags & UIF_RUN_G1);
    uint32_t left = nx_timer_left_ms(0);
    CHECK(left >= 29500u && left <= 30000u);   /* nx_ms(): the passes' transmit time counts too */
    CHECK_EQ(nx_timer_left_ms(1), 0);
    a.type = NXA_PAUSE;
    CHECK_EQ(nx_core_act(&a), NX_OK);
    run_s(20);
    CHECK(nx_timer_left_ms(0) == left || nx_timer_left_ms(0) + 2u >= left);
    a.type = NXA_PLAY;
    CHECK_EQ(nx_core_act(&a), NX_OK);
    run_s(29);
    CHECK(g_ui_flags & UIF_RUN_G1);
    run_s(2);
    CHECK(!(g_ui_flags & UIF_RUN_G1));
    CHECK_EQ(nx_timer_left_ms(0), 0);

    /* a limit set while it runs counts from then on, not from the program's start */
    g_nx_settings.session_limit_min[0] = 0;
    a.type = NXA_PLAY;
    CHECK_EQ(nx_core_act(&a), NX_OK);
    run_s(70);
    g_nx_settings.session_limit_min[0] = 1;
    run_s(2);
    CHECK(g_ui_flags & UIF_RUN_G1);
    CHECK(nx_timer_left_ms(0) > 57000u);
    run_s(60);
    CHECK(!(g_ui_flags & UIF_RUN_G1));

    /* power off when all finished: the card, then the power-off */
    nxw_reset();
    g_nx_settings.off_when_done = 1;
    nxw_run_until(NX_GEN_READY_MS + 100u);
    a.type = NXA_LOAD;
    CHECK_EQ(nx_core_act(&a), NX_OK);
    a.type = NXA_PLAY;
    CHECK_EQ(nx_core_act(&a), NX_OK);
    run_s(62);
    CHECK(!(g_ui_flags & UIF_RUN_G1));
    run_s(NX_DONE_OFF_MS / 1000u - 3u);
    CHECK(!nx_core_power_off_due());
    volatile int off = 0;
    nxw_power_off_armed = 1;
    if (setjmp(nxw_power_off_jmp) == 0)
        run_s(8);
    else
        off = 1;
    nxw_power_off_armed = 0;
    CHECK(off);

    /* ... and a key cancels it */
    nxw_reset();
    nxw_run_until(NX_GEN_READY_MS + 100u);
    a.type = NXA_LOAD;
    CHECK_EQ(nx_core_act(&a), NX_OK);
    a.type = NXA_PLAY;
    CHECK_EQ(nx_core_act(&a), NX_OK);
    run_s(62);
    nxw_press(NX_KEY_STOP, 100);
    run_s(NX_DONE_OFF_MS / 1000u + 2u);
    CHECK(!nx_core_power_off_due());

    /* ... POWER (short) too */
    nxw_reset();
    nxw_run_until(NX_GEN_READY_MS + 100u);
    a.type = NXA_LOAD;
    CHECK_EQ(nx_core_act(&a), NX_OK);
    a.type = NXA_PLAY;
    CHECK_EQ(nx_core_act(&a), NX_OK);
    run_s(62);
    nxw_press(NX_KEY_POWER, 100);
    run_s(NX_DONE_OFF_MS / 1000u + 2u);
    CHECK(!nx_core_power_off_due());
    g_nx_settings.session_limit_min[0] = 0;
    g_nx_settings.off_when_done = 0;
}
#endif

/* Lines cut off by noise leave their value index for the next frame (src/link.c). nx stores
 * nothing past the array a frame fills: the 32 :w arguments (behind them lies the other link's
 * receive state: here G2's name text) and the 205 :p words (behind them the step parameters).
 * Every frame it handles starts the next one at index 0, an :n frame included (the original and
 * the classic firmware keep the index over an :n frame). */
static void test_malformed_lines(void)
{
    static const char k_long[] = "123456789012345678901234567";   /* 27 digits: the frame is dropped */
    static const char k_name[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123";   /* 56 */
    char line[400], got[96];
    nxw_reset();
    nxw_run_until(NX_GEN_READY_MS + 200u);
    nxw_rx(1, ":n05=");                      /* G2 is in the middle of a name frame */
    nxw_rx(1, k_name);
    /* a :g frame dropped at index 40, then :w values at index 40 and 41: not stored */
    char *q = line + sprintf(line, ":g02=");
    for (int i = 1; i <= 40; i++)
        q += sprintf(q, "%d,", i);
    sprintf(q, "%s,\r\n", k_long);
    nxw_rx(0, line);
    sprintf(line, ":w24=5,6,%s,\r\n", k_long);
    nxw_rx(0, line);
    nxw_run_ms(5);
    CHECK(!(g_ui_flags & UIF_LINE_G1));
    nxw_rx(1, "\r\n");                       /* G2's frame is whole: the name is stored as sent */
    nxw_run_ms(200);
    nxw_clear();
    nxw_rx(1, ":n05=?\r\n");
    nxw_run_ms(50);
    CHECK(nxw_line(1, 0, got, sizeof got) > 0u);
    CHECK(strncmp(got, ":n05=", 5) == 0 && strcmp(got + 5, k_name) == 0);
    /* a handled :n frame starts the next frame at index 0 */
    nxw_clear();
    nxw_rx(0, ":n00=$\r\n");
    nxw_run_ms(20);
    grab();
    CHECK(line_is(0, 0, ":Rev201"));
    nxw_rx(0, ":p02=7,8,\r\n");
    nxw_run_ms(20);
    CHECK_EQ(g_ch[0].prog.hdr[0], 7);
    CHECK_EQ(g_ch[0].prog.hdr[1], 8);
    /* :g frames dropped at index 301, then :p values there: past the 205 words, not stored */
    for (int n = 0; n < 7; n++) {
        q = line + sprintf(line, ":g02=");
        for (int i = 0; i < (n ? 50 : 1); i++)
            q += sprintf(q, "1,");
        sprintf(q, "%s,\r\n", k_long);
        nxw_rx(0, line);
        nxw_run_ms(5);
    }
    g_ch[0].prog.freq[199] = 4242;
    for (unsigned i = 301; i < 2u * PROGRAM_STEPS; i++)   /* what lies behind the program words */
        g_ch[0].params[i] = 0xFFFFu;
    nxw_rx(0, ":p02=1,2,3,\r\n");
    nxw_run_ms(20);
    CHECK_EQ(g_ch[0].prog.freq[199], 4242);
    CHECK_EQ(g_ch[0].prog.hdr[0], 7);
    int behind = 1;
    for (unsigned i = 301; i < 2u * PROGRAM_STEPS; i++)
        behind &= g_ch[0].params[i] == 0xFFFFu;
    CHECK(behind);
    nxw_rx(0, ":p02=9,\r\n");              /* the frame before was handled: index 0 again */
    nxw_run_ms(20);
    CHECK_EQ(g_ch[0].prog.hdr[0], 9);
}

void test_nx_core(void)
{
    test_malformed_lines();
    test_w92_pair();
    test_boot_gate();
    test_link_order();
    test_pc_mode();
    test_cal_boot();
    test_power_off();
    test_power_off_stops_both();
    test_power_on_hold();
#if NX_FEAT_TIMERS
    test_timers();
#endif
}
