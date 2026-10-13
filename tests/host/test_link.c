/* Host unit tests of the serial protocols (link.c, factory.c, calib.c) and the SPI-flash
 * storage (storage.c, spiflash.c) against docs/spec/protocols.md and application.md.
 * Run: make -C fw/tests/host */
#include <math.h>
#include <stdlib.h>

#include "harness.h"
#include "link.h"
#include "link_internal.h"
#include "mock_board.h"
#include "spiflash.h"
#include "state.h"
#include "storage.h"

static const char *backup_path;

/* ------------------------------------------------------------------------------------------ */

void reset_world(void)
{
    mocks_reset();
    flash_reset();
    memset(g_ch, 0, sizeof g_ch);
    g_ui_flags = UIF_PC_UNLOCK;
    g_pc_flags = 0x3F;   /* all PC fields and names "up to date" */
    g_misc_flags = 0;
    g_factory_cmd_flags = 0;
    g_ui_state = ST_HOME;
    g_cal_index = 0;
    g_cal_freq_exp = 0;
    g_cal_ampl_exp = 0;
    memcpy(g_cal_table, k_cal_defaults, sizeof g_cal_table);
    memset(g_names, 0, sizeof g_names);
    link_init();
}

#define CHECK_TX(p, exp) CHECK_BYTES(tx_log[p], tx_len[p], (exp))

static char big[1 << 14];   /* scratch for long lines */

static char *app_u64(char *p, uint64_t v)
{
    p += sprintf(p, "%llu,", (unsigned long long)v);
    return p;
}

/* ------------------------------------------------------------------------------------------ */

static void test_tx_builders(void)
{
    reset_world();
    link_send_line(0, ":w11=0,0,");
    CHECK_TX(0, ":w11=0,0,\r\n");
    CHECK_EQ(tx_len[1], 0);
    tx_clear();
    link_send_line(1, ":r80=0,");
    CHECK_TX(1, ":r80=0,\r\n");
    CHECK_EQ(tx_len[0], 0);

    static const struct { unsigned ch, id; uint64_t v; const char *exp; } w[] = {
        { 1, 24, 3750007u, ":w24=3750007,\r\n" },
        { 0, 24, 0, ":w24=0,\r\n" },
        { 0, 28, 9, ":w28=9,\r\n" },
        { 0, 28, 10, ":w28=10,\r\n" },
        { 0, 5, 1, ":w05=1,\r\n" },
        { 0, 20, 41, ":w20=41,\r\n" },
        { 0, 24, 999999999999999ull, ":w24=999999999999999,\r\n" },
        { 0, 24, 1000000000000000ull, ":w24=1000000000000000,\r\n" },
        { 0, 24, 9999999999999999ull, ":w24=9999999999999999,\r\n" },
        { 0, 24, 10000000000000000ull, ":w24=10000000000000000,\r\n" },   /* DEVIATION: full u64 */
        { 1, 24, UINT64_MAX, ":w24=18446744073709551615,\r\n" },
    };
    for (unsigned i = 0; i < sizeof w / sizeof w[0]; i++) {
        tx_clear();
        link_send_w_u64(w[i].ch, w[i].id, w[i].v);
        CHECK_TX(w[i].ch, w[i].exp);
        CHECK_EQ(tx_len[!w[i].ch], 0);
    }

    tx_clear();
    link_send_w_pair5(0, 50, 7, 65535);
    CHECK_TX(0, ":w50=00007,65535,\r\n");
    CHECK_EQ(tx_len[0], 19);
    tx_clear();
    link_send_w_pair5(1, 51, 0, 0);
    CHECK_TX(1, ":w51=00000,00000,\r\n");

    tx_clear();
    link_send_cal_value(5, 1662);
    CHECK_TX(0, ":w60=1112111,05,01662,\r\n");
    CHECK_EQ(tx_len[0], 24);
    tx_clear();
    link_send_cal_value(26, (uint16_t)(int16_t)-30681);   /* stored as 34855 on the real unit */
    CHECK_TX(0, ":w60=1112111,26,34855,\r\n");
    tx_clear();
    link_send_cal_value(0, 0xFFFFFFFFu);   /* sign-extended -1: sent as the u16 65535 */
    CHECK_TX(0, ":w60=1112111,00,65535,\r\n");
    CHECK_EQ(tx_len[1], 0);
}

static void test_wait_ok(void)
{
    /* no reply: the full timeout (1000 x 640 iterations = ~40 ms) plus the 0x855 tail */
    reset_world();
    link_wait_ok();
    CHECK_EQ(delay_total, 1000u * 640u + 0x855u);
    CHECK_EQ(delay_calls, 1001u);

    /* the generator answers :ok -> the wait ends after the first slice */
    reset_world();
    auto_ack = 1;
    link_send_line(0, ":w11=0,0,");
    link_wait_ok();
    CHECK_EQ(delay_total, 640u + 0x855u);

    /* shared flag: an :ok on USART2 ends the wait as well */
    reset_world();
    inject_on_delay(1, ":ok\r\n");
    link_wait_ok();
    CHECK_EQ(delay_total, 640u + 0x855u);

    /* an :ok received before the wait started is cleared by it (original) */
    reset_world();
    feed(0, ":ok\r\n");
    link_wait_ok();
    CHECK_EQ(delay_total, 1000u * 640u + 0x855u);

    /* only ':' 'o' 'k' counts (DEVIATION: the original took any 'k' in parser state 2) */
    static const char *const not_ok[] = { ":o\r\n", ":k\r\n", "ok\r\n", ":ook\r\n", ":w1k\r\n", ":OK\r\n" };
    for (unsigned i = 0; i < sizeof not_ok / sizeof not_ok[0]; i++) {
        reset_world();
        inject_on_delay(0, not_ok[i]);
        link_wait_ok();
        CHECK_EQ(delay_total, 1000u * 640u + 0x855u);
    }

    /* DEVIATION: an :ok is seen even while a host line is pending on that link */
    reset_world();
    feed(0, ":n05=$\r\n");
    CHECK(g_ui_flags & UIF_LINE_G1);
    inject_on_delay(0, ":ok\r\n");
    link_wait_ok();
    CHECK_EQ(delay_total, 640u + 0x855u);
    CHECK(g_ui_flags & UIF_LINE_G1);   /* the pending line is untouched */

    /* ":ok" inside a line's payload is text, not an acknowledgement */
    reset_world();
    inject_on_delay(0, ":n05=a:ok");
    link_wait_ok();
    CHECK_EQ(delay_total, 1000u * 640u + 0x855u);
}

/* ------------------------------------------------------------------------------------------ */

static void test_g_frames(void)
{
    reset_world();
    feed(0, ":g05=1,2,,65536,.7,\r\n");
    CHECK(g_ui_flags & UIF_LINE_G1);
    CHECK(!(g_ui_flags & UIF_LINE_G2));
    CHECK_EQ(g_ch[0].params[0], 1);
    CHECK_EQ(g_ch[0].params[1], 2);
    CHECK_EQ(g_ch[0].params[2], 0xFFFF);   /* empty value = all ones */
    CHECK_EQ(g_ch[0].params[3], 0);        /* u16 wraps */
    CHECK_EQ(g_ch[0].params[4], 0xFFFF);   /* '.' separates like ',' */
    CHECK_EQ(g_ch[0].params[5], 7);
    CHECK_EQ(tx_len[0], 0);
    link_process(0);
    CHECK_TX(0, ":ok\r\n");
    CHECK(!(g_ui_flags & UIF_LINE_G1));
    CHECK_EQ(g_ch[0].step_sent, 0xFF);
    CHECK_EQ(flash_erases + flash_programs, 0);   /* :g only changes RAM */

    /* slot 00 / 31: :err, but the values are in RAM anyway (original) */
    g_ch[0].step_sent = 3;
    tx_clear();
    feed(0, ":g00=5,\r\n");
    link_process(0);
    CHECK_TX(0, ":err\r\n");
    CHECK_EQ(g_ch[0].params[0], 5);
    CHECK_EQ(g_ch[0].step_sent, 3);
    tx_clear();
    feed(0, ":g31=6,\r\n");
    link_process(0);
    CHECK_TX(0, ":err\r\n");
    CHECK_EQ(g_ch[0].params[0], 6);
    tx_clear();
    feed(0, ":g30=8,\r\n");
    link_process(0);
    CHECK_TX(0, ":ok\r\n");

    /* USART2 fills G2 */
    tx_clear();
    feed(1, ":g01=9,\r\n");
    CHECK(g_ui_flags & UIF_LINE_G2);
    CHECK_EQ(g_ch[1].params[0], 9);
    CHECK_EQ(g_ch[0].params[0], 8);
    link_process(1);
    CHECK_TX(1, ":ok\r\n");
    CHECK_EQ(tx_len[0], 0);
    CHECK_EQ(g_ch[1].step_sent, 0xFF);

    /* 401 values: the index sticks at 399 */
    char *p = big + sprintf(big, ":g02=");
    for (unsigned i = 0; i <= 400; i++)
        p = app_u64(p, i);
    strcpy(p, "\r\n");
    feed(0, big);
    link_process(0);
    CHECK_EQ(g_ch[0].params[0], 0);
    CHECK_EQ(g_ch[0].params[398], 398);
    CHECK_EQ(g_ch[0].params[399], 400);
}

static uint64_t test_value(unsigned i)
{
    static const uint64_t hdr[5] = { 48, 2000, 120, 90, 200 };
    if (i < 5)
        return hdr[i];
    if (i == 5)
        return UINT64_MAX;
    if (i == 6)
        return 0;
    return (uint64_t)i * 1000003ull * 1000003ull + i % 10u;
}

static void test_p_frames(void)
{
    /* a full upload: 205 values, several KB on one line, parsed on the fly */
    reset_world();
    char *p = big + sprintf(big, ":p07=");
    for (unsigned i = 0; i < 205; i++)
        p = app_u64(p, test_value(i));
    strcpy(p, "\r\n");
    CHECK(strlen(big) > 3000);
    for (unsigned i = 0; i < 400; i++)
        g_ch[0].params[i] = (uint16_t)(i * 3u);
    feed(0, big);
    CHECK(g_ui_flags & UIF_LINE_G1);
    for (unsigned i = 0; i < 5; i++)
        CHECK_EQ(g_ch[0].prog.hdr[i], test_value(i));
    for (unsigned i = 0; i < 200; i++)
        CHECK_EQ(g_ch[0].prog.freq[i], test_value(5 + i));
    link_process(0);
    CHECK_TX(0, ":ok\r\n");
    CHECK_EQ(g_ch[0].step_sent, 0xFF);
    CHECK_EQ(flash_erases, 1);
    CHECK(memcmp(&flash_mem[0x17000], &g_ch[0].prog, 0x668) == 0);
    CHECK(memcmp(&flash_mem[0x17668], g_ch[0].params, 800) == 0);
    CHECK(flash_is_erased(0x17988, 0x1000 - 0x988));
    CHECK(flash_is_erased(0x16000, 0x1000));
    CHECK(flash_is_erased(0x18000, 0x1000));
    CHECK_EQ(flash_violations, 0);

    /* :n07=* dumps exactly the uploaded line */
    tx_clear();
    feed(0, ":n07=*\r\n");
    link_process(0);
    CHECK_TX(0, big);

    /* ... which uploads again as-is (here into G2) */
    feed(1, big);
    link_process(1);
    CHECK(memcmp(&g_ch[1].prog, &g_ch[0].prog, sizeof g_ch[0].prog) == 0);

    /* 206 values: the index sticks at 204 */
    p = big + sprintf(big, ":p08=");
    for (unsigned i = 0; i < 206; i++)
        p = app_u64(p, i);
    strcpy(p, "\r\n");
    feed(0, big);
    link_process(0);
    CHECK_EQ(g_ch[0].prog.freq[198], 203);
    CHECK_EQ(g_ch[0].prog.freq[199], 205);

    /* slot 00 / 31: :err, RAM overwritten, flash untouched */
    reset_world();
    feed(0, ":p00=77,\r\n");
    link_process(0);
    CHECK_TX(0, ":err\r\n");
    CHECK_EQ(g_ch[0].prog.hdr[0], 77);
    tx_clear();
    feed(0, ":p31=78,\r\n");
    link_process(0);
    CHECK_TX(0, ":err\r\n");
    CHECK_EQ(flash_erases + flash_programs, 0);

    /* 26-character value accepted (u64 wraps), 27 characters drop the line */
    reset_world();
    feed(0, ":p01=12345678901234567890123456,\r\n");
    uint64_t v = 0;
    for (const char *s = "12345678901234567890123456"; *s; s++)
        v = v * 10u + (uint64_t)(*s & 0x0F);
    CHECK_EQ(g_ch[0].prog.hdr[0], v);
    link_process(0);
    tx_clear();
    feed(0, ":p01=5,123456789012345678901234567,\r\n");
    CHECK(!(g_ui_flags & UIF_LINE_G1));
    CHECK_EQ(g_ch[0].prog.hdr[0], 5);   /* the first value was stored before the drop */
    feed(0, ":p02=6,\r\n");             /* 1:1 with the original: the aborted line left the index
                                         * at 1, so this value lands at index 1, not 0 */
    CHECK(g_ui_flags & UIF_LINE_G1);
    CHECK_EQ(g_ch[0].prog.hdr[0], 5);   /* untouched, still from the aborted line */
    CHECK_EQ(g_ch[0].prog.hdr[1], 6);   /* stored at the carried-over index */
    link_process(0);
    CHECK_TX(0, ":ok\r\n");

    /* an :n frame does not reset the index (the original's :n exits leave it alone) */
    feed(0, ":p01=5,123456789012345678901234567,\r\n");   /* aborted: index 1 */
    feed(0, ":n00=$\r\n");
    link_process(0);
    feed(0, ":n03=?\r\n");
    link_process(0);
    tx_clear();
    feed(0, ":p02=7,8,\r\n");
    CHECK_EQ(g_ch[0].prog.hdr[0], 5);
    CHECK_EQ(g_ch[0].prog.hdr[1], 7);
    CHECK_EQ(g_ch[0].prog.hdr[2], 8);
    link_process(0);
    CHECK_TX(0, ":ok\r\n");
    feed(0, ":p02=9,\r\n");               /* the handled :p frame did reset it */
    CHECK_EQ(g_ch[0].prog.hdr[0], 9);
    link_process(0);

    /* an index carried past another letter's array keeps counting, as the original's; DEVIATION:
     * the values there are not stored (the original writes them behind the array) */
    feed(0, ":g02=1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,"
            "31,32,33,34,35,36,37,38,39,40,123456789012345678901234567,\r\n");   /* aborted: index 40 */
    feed(0, ":w24=5,6,123456789012345678901234567,\r\n");   /* index 40, 41: past the 32 :w arguments */
    CHECK(!(g_ui_flags & UIF_LINE_G1));
    g_ch[0].prog.freq[35] = g_ch[0].prog.freq[36] = g_ch[0].prog.freq[37] = 0;
    feed(0, ":p02=111,\r\n");             /* lands at word 42 = freq[37] */
    CHECK_EQ(g_ch[0].prog.freq[35], 0);
    CHECK_EQ(g_ch[0].prog.freq[37], 111);
    link_process(0);
    feed(0, ":g02=1,123456789012345678901234567,\r\n");
    for (int i = 0; i < 6; i++) {          /* aborted :g frames: 50 values each, index 301 */
        feed(0, ":g02=1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,"
                "1,1,1,1,1,1,1,1,1,1,123456789012345678901234567,\r\n");
    }
    g_ch[0].prog.freq[199] = 4242;
    feed(0, ":p02=1,2,3,\r\n");           /* index 301..303: past the 205 :p words, not stored */
    CHECK(g_ui_flags & UIF_LINE_G1);
    CHECK_EQ(g_ch[0].prog.freq[199], 4242);
    CHECK_EQ(g_ch[0].prog.hdr[0], 9);
    link_process(0);
    feed(0, ":p02=9,\r\n");
    link_process(0);
    tx_clear();

    /* digits are not validated: (c & 0x0F) */
    reset_world();
    feed(0, ":p01=1a,2:, 9,\r\n");
    CHECK_EQ(g_ch[0].prog.hdr[0], 11);
    CHECK_EQ(g_ch[0].prog.hdr[1], 30);
    CHECK_EQ(g_ch[0].prog.hdr[2], 9);   /* ' ' & 0x0F = 0 */
    link_process(0);

    /* the value before CR/LF needs its trailing ',' */
    g_ch[0].prog.hdr[1] = 1234;
    feed(0, ":p02=5,6\r\n");
    CHECK_EQ(g_ch[0].prog.hdr[0], 5);
    CHECK_EQ(g_ch[0].prog.hdr[1], 1234);
    link_process(0);
    CHECK_EQ(flash_violations, 0);
}

static void test_n_frames(void)
{
    reset_world();
    feed(0, ":n05=$\r\n");
    CHECK(g_ui_flags & UIF_LINE_G1);
    link_process(0);
    CHECK_TX(0, ":Rev201\r\n");
    tx_clear();
    feed(1, ":n00=$\r\n");
    link_process(1);
    CHECK_TX(1, ":Rev201\r\n");
    tx_clear();
    feed(0, ":n31=$\r\n");
    link_process(0);
    CHECK_TX(0, ":err\r\n");

    /* store a name: RAM + whole table to 0x5000 (slots 0 and 31 as zeros) */
    reset_world();
    strcpy(g_names[0], "RAM only 0");
    strcpy(g_names[31], "RAM only 31");
    feed(0, ":n03=Hello World\r\n");
    link_process(0);
    CHECK_TX(0, ":ok\r\n");
    CHECK(strcmp(g_names[3], "Hello World") == 0);
    CHECK(memcmp(&flash_mem[0x5000 + 3 * 60], "Hello World\0\0", 13) == 0);
    for (unsigned i = 0; i < 60; i++) {
        CHECK_EQ(flash_mem[0x5000 + i], 0);
        CHECK_EQ(flash_mem[0x5000 + 31 * 60 + i], 0);
    }
    CHECK(flash_is_erased(0x5780, 0x1000 - 0x780));
    CHECK_EQ(g_pc_flags, 0x3F & ~0x10);   /* PCF_NAME(0) */

    tx_clear();
    feed(0, ":n03=?\r\n");
    link_process(0);
    CHECK_TX(0, ":n03=Hello World\r\n");

    /* 59 characters accepted, 60 drop the line; CR ends the text */
    tx_clear();
    feed(0, ":n04=12345678901234567890123456789012345678901234567890123456789\r\n");
    CHECK(g_ui_flags & UIF_LINE_G1);
    link_process(0);
    CHECK_TX(0, ":ok\r\n");
    CHECK_EQ(strlen(g_names[4]), 59);
    tx_clear();
    feed(0, ":n04=123456789012345678901234567890123456789012345678901234567890\r\n");
    CHECK(!(g_ui_flags & UIF_LINE_G1));
    CHECK_EQ(tx_len[0], 0);
    CHECK_EQ(strlen(g_names[4]), 59);
    feed(0, ":n06=ab\rcd\r\n");
    link_process(0);
    CHECK(strcmp(g_names[6], "ab") == 0);
    /* many CRs: the line is accepted like the original's (an empty name); DEVIATION: the CRs
     * that do not fit the buffer are left out (the original writes them behind it) */
    tx_clear();
    memset(big, '\r', 100);
    strcpy(big + 100, "\n");
    feed(0, ":n06=");
    feed(0, big);
    CHECK(g_ui_flags & UIF_LINE_G1);
    link_process(0);
    CHECK_TX(0, ":ok\r\n");
    CHECK_EQ(strlen(g_names[6]), 0);
    /* only a character directly after 59 others drops the line: after a CR there, text is taken
     * again (the original's b[0x41..0x43]), and what does not fit the buffer is left out */
    tx_clear();
    feed(0, ":n06=12345678901234567890123456789012345678901234567890123456789\rBCD\r\n");
    CHECK(g_ui_flags & UIF_LINE_G1);
    link_process(0);
    CHECK_TX(0, ":ok\r\n");
    CHECK_EQ(strlen(g_names[6]), 59);
    tx_clear();
    feed(0, ":n06=1234567890123456789012345678901234567890123456789012345678\r\rBCDEFGHIJKLMNOP\r\n");
    CHECK(g_ui_flags & UIF_LINE_G1);
    link_process(0);
    CHECK_TX(0, ":ok\r\n");
    CHECK_EQ(strlen(g_names[6]), 58);
    feed(0, ":n06=1234567890123456789012345678901234567890123456789012345678\rB\r\n");
    CHECK(!(g_ui_flags & UIF_LINE_G1));   /* 58 characters and a CR: 'B' is the one at b[0x40] */
    CHECK_EQ(strlen(g_names[6]), 58);
    tx_clear();
    feed(0, ":n06=$\r\n");
    link_process(0);
    CHECK_TX(0, ":Rev201\r\n");

    /* ? replies at most 57 characters and stops at a byte outside 0x20..0x7F */
    tx_clear();
    feed(0, ":n04=?\r\n");
    link_process(0);
    CHECK_TX(0, ":n04=123456789012345678901234567890123456789012345678901234567\r\n");
    tx_clear();
    g_names[9][0] = 'x';
    g_names[9][1] = (char)0x80;
    g_names[9][2] = 'y';
    feed(0, ":n09=?\r\n");
    link_process(0);
    CHECK_TX(0, ":n09=x\r\n");

    /* USART1 slot 00 = PC name G1 (RAM only), USART2 slot 00 = slot 31 */
    reset_world();
    feed(0, ":n00=PC One\r\n");
    link_process(0);
    CHECK_TX(0, ":ok\r\n");
    CHECK(strcmp(g_names[0], "PC One") == 0);
    CHECK_EQ(flash_erases + flash_programs, 0);
    CHECK_EQ(g_pc_flags, 0x3F & ~0x10);
    feed(1, ":n00=PC Two\r\n");
    link_process(1);
    CHECK_TX(1, ":ok\r\n");
    CHECK(strcmp(g_names[31], "PC Two") == 0);
    CHECK(g_names[0][0] == 'P' && g_names[0][3] == 'O');
    CHECK_EQ(g_pc_flags, 0x3F & ~0x30);
    CHECK_EQ(flash_erases + flash_programs, 0);
    tx_clear();
    feed(1, ":n00=?\r\n");
    link_process(1);
    CHECK_TX(1, ":n00=PC Two\r\n");
    tx_clear();
    feed(0, ":n00=?\r\n");
    link_process(0);
    CHECK_TX(0, ":n00=PC One\r\n");
    /* empty text clears the name */
    feed(0, ":n00=\r\n");
    link_process(0);
    CHECK_EQ(g_names[0][0], 0);

    /* , erases the slot (name kept); * and # dump the stored slot */
    reset_world();
    feed(0, ":n04=Name four\r\n");
    link_process(0);
    feed(0, ":g04=1,2,3,65535,\r\n");
    link_process(0);
    feed(0, ":p04=10,20,30,40,3,1000,2000,3000,\r\n");
    link_process(0);
    tx_clear();
    feed(0, ":n04=*\r\n");
    link_process(0);
    CHECK_TX(0, ":p04=10,20,30,40,3,1000,2000,3000,\r\n");
    tx_clear();
    feed(0, ":n04=#\r\n");
    link_process(0);
    char *p = big + sprintf(big, ":g04=1,2,3,65535,");
    for (unsigned i = 4; i < 200; i++)
        p += sprintf(p, "0,");
    strcpy(p, "\r\n");
    CHECK_TX(0, big);   /* first 200 of 400 values, exactly like the original (wire format kept) */
    tx_clear();
    g_pc_flags = 0x3F;
    feed(0, ":n04=,\r\n");
    link_process(0);
    CHECK_TX(0, ":ok\r\n");
    CHECK(flash_is_erased(0x14000, 0x1000));
    CHECK(strcmp(g_names[4], "Name four") == 0);
    CHECK_EQ(g_pc_flags, 0x3F & ~0x10);
    tx_clear();
    feed(1, ":n04=*\r\n");   /* erased: count invalid -> header only */
    link_process(1);
    CHECK_TX(1, ":p04=\r\n");
    tx_clear();
    feed(1, ":n04=#\r\n");   /* erased: g[0] == 0xFFFF -> nothing */
    link_process(1);
    CHECK_TX(1, ":g04=\r\n");
    tx_clear();
    feed(0, ":n31=?\r\n");
    link_process(0);
    CHECK_TX(0, ":err\r\n");
    /* DEVIATION: ,:n00 does not erase the unused sector 0x10000 / 0x2F000 */
    unsigned erases = flash_erases;
    feed(0, ":n00=,\r\n");
    link_process(0);
    feed(1, ":n00=,\r\n");
    link_process(1);
    CHECK_EQ(flash_erases, erases);
    /* NN is not checked to be digits: 'D' & 0x0F = 4, echoed as (4 | 0x30) */
    tx_clear();
    feed(0, ":n0D=*\r\n");
    link_process(0);
    CHECK_TX(0, ":p04=\r\n");
    CHECK_EQ(flash_violations, 0);
}

/* ------------------------------------------------------------------------------------------ */

static void test_w_mirror(void)
{
    /* w11: output-enable bits and field redraw; handled in the interrupt, never answered */
    reset_world();
    feed(0, ":w11=1,0,\r\n");
    CHECK(!(g_ui_flags & UIF_LINE_G1));
    CHECK_EQ(g_misc_flags, 0x02);
    CHECK_EQ(g_pc_flags, 0x3F & ~0x03);
    g_pc_flags = 0x3F;
    feed(0, ":w11=2,1,\r\n");   /* 2 is neither on nor off */
    CHECK_EQ(g_misc_flags, 0x06);
    CHECK_EQ(g_pc_flags, 0x3F & ~0x02);
    g_pc_flags = 0x3F;
    feed(1, ":w11=1,1,\r\n");
    CHECK_EQ(g_misc_flags, 0x1E);
    CHECK_EQ(g_pc_flags, 0x3F & ~0x0C);
    feed(1, ":w11=0,0,\r\n");
    CHECK_EQ(g_misc_flags, 0x06);
    /* DEVIATION: a missing argument is "empty", not the previous line's value */
    feed(0, ":w11=0,\r\n");
    CHECK_EQ(g_misc_flags, 0x04);
    CHECK_EQ(tx_len[0] + tx_len[1], 0);

    /* w14: sync flag */
    reset_world();
    feed(0, ":w14=1,\r\n");
    CHECK(g_ui_flags & UIF_SYNC_G1);
    CHECK_EQ(g_pc_flags, 0x3F & ~0x03);
    feed(1, ":w14=1,\r\n");
    CHECK(g_ui_flags & UIF_SYNC_G2);
    feed(0, ":w14=5,\r\n");
    CHECK(g_ui_flags & UIF_SYNC_G1);
    feed(0, ":w14=0,\r\n");
    CHECK(!(g_ui_flags & UIF_SYNC_G1));
    CHECK(g_ui_flags & UIF_SYNC_G2);

    /* w15: stored bits 6/7 (G1), 8/9 (G2) */
    reset_world();
    g_pc_flags = 0;
    feed(0, ":w15=1,0,\r\n");
    CHECK_EQ(g_pc_flags, 0x40);
    feed(0, ":w15=0,1,\r\n");
    CHECK_EQ(g_pc_flags, 0x80);
    g_ui_flags |= UIF_SYNC_G1;   /* sync: bit 7 follows bit 6 */
    feed(0, ":w15=1,0,\r\n");
    CHECK_EQ(g_pc_flags, 0xC0);
    feed(0, ":w15=0,1,\r\n");
    CHECK_EQ(g_pc_flags, 0x00);
    feed(1, ":w15=1,1,\r\n");
    CHECK_EQ(g_pc_flags, 0x300);

    /* w24 / w25: PC-mode frequencies */
    reset_world();
    feed(0, ":w24=3750007,\r\n");
    CHECK_EQ(g_ch[0].pc_freq[0], 3750007);
    CHECK_EQ(g_ch[0].pc_freq[1], 0);
    CHECK_EQ(g_pc_flags, 0x3F & ~0x01);
    g_pc_flags = 0x3F;
    feed(0, ":w25=100000007,\r\n");
    CHECK_EQ(g_ch[0].pc_freq[1], 100000007);
    CHECK_EQ(g_pc_flags, 0x3F & ~0x02);
    g_pc_flags = 0x3F;
    g_ui_flags |= UIF_SYNC_G2;
    feed(1, ":w24=123456007,\r\n");
    CHECK_EQ(g_ch[1].pc_freq[0], 123456007);
    CHECK_EQ(g_ch[1].pc_freq[1], 123456007);
    CHECK_EQ(g_pc_flags, 0x3F & ~0x0C);
    g_pc_flags = 0x3F;
    feed(1, ":w25=5,\r\n");   /* sync: ignored, and (DEVIATION) not left pending */
    CHECK_EQ(g_ch[1].pc_freq[1], 123456007);
    CHECK_EQ(g_pc_flags, 0x3F);
    CHECK(!(g_ui_flags & UIF_LINE_G2));
    CHECK_EQ(g_ch[0].pc_freq[0], 3750007);

    /* numbers that are neither mirrored nor deferred are dropped in the interrupt */
    reset_world();
    feed(0, ":w12=1,1,\r\n");
    CHECK(!(g_ui_flags & UIF_LINE_G1));
    feed(0, ":w60=1112111,1,5,\r\n");   /* w60 is a USART2 command */
    CHECK(!(g_ui_flags & UIF_LINE_G1));
    feed(1, ":w62=3336333,\r\n");       /* w62 is a USART1 command */
    CHECK(!(g_ui_flags & UIF_LINE_G2));
    CHECK_EQ(g_cal_table[1], k_cal_defaults[1]);
    CHECK_EQ(tx_len[0] + tx_len[1], 0);

    /* a pending line blocks the link: bytes are discarded until link_process */
    reset_world();
    feed(0, ":n05=$\r\n");
    feed(0, ":w24=5,\r\n");
    CHECK_EQ(g_ch[0].pc_freq[0], 0);
    feed(1, ":w24=6,\r\n");   /* the other link is independent */
    CHECK_EQ(g_ch[1].pc_freq[0], 6);
    link_process(0);
    CHECK_TX(0, ":Rev201\r\n");
    feed(0, ":w24=5,\r\n");
    CHECK_EQ(g_ch[0].pc_freq[0], 5);

    /* malformed headers */
    reset_world();
    feed(0, ":w24:5,\r\n");      /* no '=' */
    feed(0, ":x24=5,\r\n");      /* unknown letter */
    feed(0, ":w2\n");            /* LF in the header */
    feed(0, "w24=5,\r\n");       /* no ':' */
    feed(0, "::w24=5,\r\n");     /* ':' in the letter position drops the line (original) */
    CHECK_EQ(g_ch[0].pc_freq[0], 0);
    CHECK(!(g_ui_flags & UIF_LINE_G1));
    feed(0, ":w24=7,\r\n");
    CHECK_EQ(g_ch[0].pc_freq[0], 7);
    link_process(0);   /* nothing pending: no effect */
    CHECK_EQ(tx_len[0], 0);
}

static void test_w_deferred(void)
{
    /* w00 is acknowledged without any effect (DEVIATION: no bootloader-page write) */
    reset_world();
    feed(0, ":w00=12345,\r\n");
    CHECK(g_ui_flags & UIF_LINE_G1);
    link_process(0);
    CHECK(!(g_ui_flags & UIF_LINE_G1));
    CHECK_EQ(tx_len[0], 0);
    CHECK_EQ(flash_erases + flash_programs, 0);
    feed(0, ":w16=1,\r\n");
    CHECK(g_ui_flags & UIF_LINE_G1);
    link_process(0);
    CHECK(!(g_ui_flags & UIF_LINE_G1));

    /* w60 on USART2: DEVIATION: its own arguments, index checked; misc_flags = 0 (original) */
    reset_world();
    g_misc_flags = 0x1F;
    feed(1, ":w60=1112111,5,777,\r\n");
    CHECK(g_ui_flags & UIF_LINE_G2);
    link_process(1);
    CHECK_EQ(g_cal_table[5], 777);
    CHECK_EQ(g_misc_flags, 0);
    g_misc_flags = 0x1F;
    feed(1, ":w60=1112111,40,778,\r\n");
    link_process(1);
    CHECK_EQ(g_misc_flags, 0);
    feed(1, ":w60=1112112,6,779,\r\n");
    link_process(1);
    CHECK_EQ(g_cal_table[6], k_cal_defaults[6]);
    CHECK_EQ(tx_len[1], 0);

    /* w63: reference trim +-1 within 31900..32100, saved to 0x74000 */
    reset_world();
    g_cal_table[0] = 32099;
    feed(0, ":w63=111999,\r\n");
    link_process(0);
    CHECK_EQ(g_cal_table[0], 32100);
    feed(1, ":w63=111999,\r\n");
    link_process(1);
    CHECK_EQ(g_cal_table[0], 32100);
    CHECK(memcmp(&flash_mem[0x74000], g_cal_table, 64) == 0);
    CHECK(flash_is_erased(0x75000, 0x1000));
    g_cal_table[0] = 31901;
    feed(0, ":w63=111000,\r\n");
    link_process(0);
    feed(0, ":w63=111000,\r\n");
    link_process(0);
    CHECK_EQ(g_cal_table[0], 31900);
    g_cal_table[0] = 20000;   /* below the window: jumps to the floor (original) */
    feed(0, ":w63=111000,\r\n");
    link_process(0);
    CHECK_EQ(g_cal_table[0], 31900);
    feed(0, ":w63=5,\r\n");
    link_process(0);
    CHECK_EQ(g_cal_table[0], 31900);

    /* w62 (USART1): restore the backup copy */
    reset_world();
    g_cal_table[3] = 999;
    cal_save(1);
    g_cal_table[3] = 5;
    g_cal_table[4] = 6;
    feed(0, ":w62=3336333,\r\n");
    link_process(0);
    CHECK_EQ(g_cal_table[3], 999);
    CHECK_EQ(g_cal_table[4], k_cal_defaults[4]);
    feed(0, ":w62=1,\r\n");
    g_cal_table[3] = 5;
    link_process(0);
    CHECK_EQ(g_cal_table[3], 5);

    /* w64: backlight */
    reset_world();
    feed(1, ":w64=10000001,\r\n");
    link_process(1);
    CHECK_EQ(g_backlight_duty, 0);
    feed(1, ":w64=18888881,\r\n");
    link_process(1);
    CHECK_EQ(g_backlight_duty, 8);
    g_backlight_duty = 3;
    feed(0, ":w64=1,\r\n");
    link_process(0);
    CHECK_EQ(g_backlight_duty, 3);

    /* w80: cal_table[0..20] from the generator's answer to :r80=0, */
    reset_world();
    char *p = big + sprintf(big, ":w80=13583792,");
    for (unsigned i = 0; i < 21; i++)
        p += sprintf(p, "%u,", 100u + i);
    strcpy(p, "\r\n");
    feed(0, big);
    link_process(0);
    for (unsigned i = 0; i < 21; i++)
        CHECK_EQ(g_cal_table[i], 100 + i);
    CHECK_EQ(g_cal_table[21], 21);
    CHECK_EQ(flash_erases + flash_programs, 0);
    big[5] = '2';   /* wrong key */
    g_cal_table[0] = 1;
    feed(1, big);
    link_process(1);
    CHECK_EQ(g_cal_table[0], 1);

    /* w91: any value, PB2 = 0; w92: PB2 = 1 and :w91=0, back on the same link */
    reset_world();
    mock_gpiob.ODR = 1u << HANDSHAKE_PIN;
    feed(1, ":w91=0,\r\n");
    link_process(1);
    CHECK_EQ(mock_gpiob.ODR, 0);
    CHECK(g_ui_flags & UIF_PC_UNLOCK);
    feed(1, ":w92=0,\r\n");
    link_process(1);
    CHECK_EQ(mock_gpiob.ODR, 1u << HANDSHAKE_PIN);
    CHECK_EQ(delay_calls_34155, 2);
    CHECK_TX(1, ":w91=0,\r\n");
    CHECK_EQ(tx_len[0], 0);
    feed(0, ":w91=160110947,\r\n");
    link_process(0);
    CHECK_EQ(mock_gpiob.ODR, 0);

    /* w95: system reset */
    reset_world();
    feed(0, ":w95=1,\r\n");
    link_process(0);
    CHECK_EQ(reset_calls, 0);
    feed(0, ":w95=12021,\r\n");
    link_process(0);
    CHECK_EQ(reset_calls, 1);

    /* w96: erase slots 1..30 and the names, once per power cycle */
    reset_world();
    for (unsigned s = 1; s <= 30; s++) {
        g_ch[0].prog.hdr[HDR_COUNT] = s;
        g_ch[0].prog.hdr[HDR_DWELL] = 1;
        program_save(s, 0);
        g_names[s][0] = (char)('A' + s);
    }
    names_save();
    memset(&flash_mem[0x74000], 0x12, 64);   /* must survive */
    feed(0, ":w96=12320,\r\n");
    link_process(0);
    CHECK(!flash_is_erased(0x11000, 8));
    feed(0, ":w96=12321,\r\n");
    link_process(0);
    CHECK(g_ui_flags & UIF_ERASED_ALL);
    CHECK(flash_is_erased(0x11000, 30 * 0x1000));
    CHECK(flash_is_erased(0x5000, 0x1000));
    CHECK_EQ(g_names[1][0], 0);
    CHECK_EQ(flash_mem[0x74000], 0x12);
    program_save(3, 0);
    feed(1, ":w96=12321,\r\n");
    link_process(1);
    CHECK(!flash_is_erased(0x13000, 8));

    /* w97: reset the PC display of that link, names 0 and 31 cleared */
    reset_world();
    g_ch[0].pc_freq[0] = g_ch[0].pc_freq[1] = 5;
    g_ch[1].pc_freq[0] = 6;
    strcpy(g_names[0], "zero");
    strcpy(g_names[31], "thirty-one");
    strcpy(g_names[1], "one");
    g_pc_flags = 0x3FF;
    feed(0, ":w97=12620,\r\n");
    link_process(0);
    CHECK_EQ(g_ch[0].pc_freq[0], 5);
    feed(0, ":w97=12621,\r\n");
    link_process(0);
    CHECK_EQ(g_ch[0].pc_freq[0], 0);
    CHECK_EQ(g_ch[0].pc_freq[1], 0);
    CHECK_EQ(g_ch[1].pc_freq[0], 6);
    CHECK(g_ui_flags & UIF_SYNC_G1);
    CHECK(!(g_ui_flags & UIF_SYNC_G2));
    CHECK_EQ(g_pc_flags, 0x3FF & ~(0x03 | 0xC0 | 0x30));   /* + names (DEVIATION) */
    CHECK_EQ(g_names[0][0], 0);
    CHECK_EQ(g_names[31][0], 0);
    CHECK(strcmp(g_names[1], "one") == 0);
    g_pc_flags = 0x3FF;
    feed(1, ":w97=12621,\r\n");
    link_process(1);
    CHECK(g_ui_flags & UIF_SYNC_G2);
    CHECK_EQ(g_ch[1].pc_freq[0], 0);
    CHECK_EQ(g_pc_flags, 0x3FF & ~(0x0C | 0x300 | 0x30));
    CHECK_EQ(tx_len[0] + tx_len[1], 0);
    CHECK_EQ(flash_violations, 0);
}

/* The access gate is always open in the reimplementation (UIF_PC_UNLOCK stays set); the
 * original expression is kept, so check it once with the flag cleared and PB1 high. */
static void test_gate(void)
{
    reset_world();
    g_ui_flags = 0;
    mock_gpiob.IDR = 1u << HOST_DETECT_PIN;
    CHECK(!link_gate_open());
    feed(0, ":g05=1,\r\n");
    link_process(0);
    CHECK_TX(0, ":err\r\n");
    tx_clear();
    feed(0, ":p05=1,\r\n");
    link_process(0);
    CHECK_TX(0, ":err\r\n");
    tx_clear();
    feed(0, ":n05=$\r\n");   /* allowed with a closed gate */
    link_process(0);
    CHECK_TX(0, ":Rev201\r\n");
    tx_clear();
    feed(0, ":n05=?\r\n");
    link_process(0);
    CHECK_TX(0, ":err\r\n");
    feed(0, ":w24=5,\r\n");  /* mirror: stays pending, then dropped */
    CHECK(g_ui_flags & UIF_LINE_G1);
    CHECK_EQ(g_ch[0].pc_freq[0], 0);
    link_process(0);
    CHECK(!(g_ui_flags & UIF_LINE_G1));
    CHECK_EQ(g_ch[0].pc_freq[0], 0);
    feed(0, ":w63=111999,\r\n");
    link_process(0);
    CHECK_EQ(g_cal_table[0], k_cal_defaults[0]);
    feed(0, ":w97=12621,\r\n");
    link_process(0);
    CHECK(!(g_ui_flags & UIF_SYNC_G1));
    feed(0, ":w64=10000001,\r\n");   /* not gated */
    link_process(0);
    CHECK_EQ(g_backlight_duty, 0);
    mock_gpiob.IDR = 0;   /* PB1 low (standalone) opens the gate */
    CHECK(link_gate_open());
    g_ui_flags = UIF_PC_UNLOCK;
    mock_gpiob.IDR = 1u << HOST_DETECT_PIN;
    CHECK(link_gate_open());

    /* DEVIATION check: USART2 w60 does not look at USART1's arguments */
    reset_world();
    feed(0, ":w60=1112111,3,555,\r\n");   /* dropped on USART1, arguments stored there */
    feed(1, ":w60=1,3,556,\r\n");
    link_process(1);
    CHECK_EQ(g_cal_table[3], k_cal_defaults[3]);
}

/* ------------------------------------------------------------------------------------------ */

static void test_factory(void)
{
    reset_world();
    g_cal_index = 5;
    feed(2, ":a0\n");
    CHECK_EQ(g_factory_cmd_flags, 1);
    CHECK_EQ(tx_len[2], 0);
    link_factory_execute();
    CHECK_EQ(g_factory_cmd_flags, 0);
    CHECK_TX(2, "Z");
    CHECK_EQ(g_ui_state, ST_CAL);
    CHECK_EQ(g_cal_index, 0);
    CHECK(g_ui_flags & UIF_REDRAW);
    CHECK_EQ(lcd_clear_calls, 1);
    CHECK_EQ(ui_values_calls, 1);

    tx_clear();
    g_ui_flags = UIF_PC_UNLOCK;
    feed(2, ":a1\r\n");   /* CR is skipped */
    link_factory_execute();
    CHECK_TX(2, "Z");
    CHECK_EQ(g_ui_state, ST_BOOT);
    CHECK(g_ui_flags & UIF_REDRAW);
    CHECK_EQ(lcd_clear_calls, 2);
    CHECK_EQ(ui_values_calls, 1);

    /* :b: index, jig signals, PB7 duty */
    static const struct { const char *cmd; unsigned idx, duty; } b[] = {
        { ":b3\n", 3, 1 }, { ":b5\n", 5, 1 }, { ":b6\n", 6, 6 }, { ":b10\n", 10, 6 },
        { ":b11\n", 11, 10 }, { ":b15\n", 15, 10 }, { ":b16\n", 16, 15 }, { ":b20\n", 20, 15 },
        { ":b21\n", 21, 1 }, { ":b300\n", 44, 1 },
    };
    for (unsigned i = 0; i < sizeof b / sizeof b[0]; i++) {
        reset_world();
        g_pb7_duty = 9;
        feed(2, b[i].cmd);
        link_factory_execute();
        CHECK_EQ(g_cal_index, b[i].idx);
        CHECK_EQ(g_pb7_duty, b[i].duty);
        CHECK(g_ui_flags & UIF_REDRAW);
        CHECK_TX(2, "Z");
    }
    reset_world();
    feed(2, ":b3\n");
    link_factory_execute();
    CHECK_BYTES(ev_log, ev_len, "1>:w28=1200,\r\n[W]\n3>Z\n");

    /* :c: value of the current index, sent to G1 */
    reset_world();
    g_cal_index = 5;
    feed(2, ":c1234\n");
    link_factory_execute();
    CHECK_EQ(g_cal_table[5], 1234);
    CHECK_BYTES(ev_log, ev_len, "1>:w60=1112111,05,01234,\r\n3>Z\n");
    CHECK(g_ui_flags & UIF_REDRAW);
    reset_world();
    g_cal_index = 44;   /* DEVIATION: out-of-range index does not write past the table */
    feed(2, ":c7\n");
    link_factory_execute();
    CHECK_TX(0, ":w60=1112111,44,00007,\r\n");
    CHECK(memcmp(g_cal_table, k_cal_defaults, 64) == 0);

    /* :d: save (+ backup) and confirm to G1 */
    reset_world();
    g_cal_table[2] = 42;
    feed(2, ":d11131\n");
    link_factory_execute();
    CHECK_BYTES(ev_log, ev_len, "1>:w61=1113111,\r\n3>Z\n");
    CHECK(memcmp(&flash_mem[0x74000], g_cal_table, 64) == 0);
    CHECK(flash_is_erased(0x75000, 0x1000));
    tx_clear();
    feed(2, ":d11141\n");
    link_factory_execute();
    CHECK_BYTES(ev_log, ev_len, "1>:w61=1114111,\r\n3>Z\n");
    CHECK(memcmp(&flash_mem[0x75000], g_cal_table, 64) == 0);
    tx_clear();
    feed(2, ":d5\n");
    link_factory_execute();
    CHECK_BYTES(ev_log, ev_len, "3>Z\n");

    /* :e..:l: cal_table[21..28], drawn at once */
    reset_world();
    const char letters[] = "efghijkl";
    for (unsigned i = 0; i < 8; i++) {
        char cmd[16];
        sprintf(cmd, ":%c%u\n", letters[i], 60000u + i);
        feed(2, cmd);
        link_factory_execute();
        CHECK_EQ((uint16_t)g_cal_table[21 + i], 60000u + i);
        CHECK_EQ(ui_value_last, 21 + i);
    }
    CHECK_EQ(ui_value_calls, 8);
    CHECK_TX(2, "ZZZZZZZZ");

    /* parser */
    reset_world();
    feed(2, ":a0\n");
    feed(2, ":a1\n");   /* dropped while the first is pending */
    link_factory_execute();
    CHECK_EQ(g_ui_state, ST_CAL);
    link_factory_execute();   /* nothing pending */
    CHECK_TX(2, "Z");
    feed(2, ":x5\n");         /* letter outside a..l */
    feed(2, "::a1\n");        /* ':' in the letter position */
    feed(2, "a1\n");          /* no ':' */
    feed(2, ":b12345678\r\n"); /* 8 digits then CR: frame dropped (original) */
    CHECK_EQ(g_factory_cmd_flags, 0);
    feed(2, ":a\n");          /* no number = 0 */
    CHECK_EQ(g_factory_cmd_flags, 1);
    link_factory_execute();
    feed(2, ":a 1\r\n");      /* skipped characters, number 1 */
    link_factory_execute();
    CHECK_EQ(g_ui_state, ST_BOOT);
    feed(2, ":c65537\n");     /* the number wraps at 16 bits */
    link_factory_execute();
    CHECK_EQ(g_cal_table[0], 1);
    memset(big, '1', 400);    /* DEVIATION: state saturates instead of wrapping */
    big[400] = 0;
    feed(2, ":e");
    feed(2, big);
    feed(2, "\n");
    CHECK_EQ(g_factory_cmd_flags, 1);
    link_factory_execute();
    CHECK_EQ(tx_len[0] + tx_len[1], 24);   /* the :c line */
}

/* ------------------------------------------------------------------------------------------ */

/* Expected sequences, generated from the disassembly of cal_apply_test 0x08011468 and
 * cal_apply_test_jig 0x08011C2C (literal strings resolved per call site, exponents 0). */
static const char *const k_trace_menu[29][17] = {
    /*  0 */ {"1:w11=1,1,", "2:w11=1,1,", "1:w32=120,", "1:w33=120,", "2:w32=120,", "2:w33=120,", "1:w24=123456007,", "1:w25=123456007,", "2:w24=123456007,", "2:w25=123456007,", "1:w28=1500,", "1:w29=1500,", "2:w28=1500,", "2:w29=1500,"},
    /*  1 */ {"1:w11=1,0,", "2:w11=0,0,", "1:w24=3750007,", "1:w28=1200,"},
    /*  2 */ {"1:w28=60,"},
    /*  3 */ {"1:w28=1200,"},
    /*  4 */ {"1:w28=60,", "1:w32=120,"},
    /*  5 */ {"1:w11=1,0,", "1:w28=600,", "1:w32=220,"},
    /*  6 */ {"1:w11=0,1,", "1:w32=120,", "1:w25=3750007,", "1:w29=1200,"},
    /*  7 */ {"1:w29=60,"},
    /*  8 */ {"1:w29=1200,"},
    /*  9 */ {"1:w11=0,1,", "1:w24=3750007,", "1:w13=0,", "1:w29=60,", "1:w33=120,"},
    /* 10 */ {"1:w11=1,1,", "1:w24=100000007,", "1:w13=1,", "2:w11=0,0,", "1:w29=600,", "1:w33=220,"},
    /* 11 */ {"1:w11=0,0,", "1:w24=3750007,", "1:w13=0,", "2:w11=1,0,", "1:w33=120,", "2:w24=3750007,", "2:w28=1200,"},
    /* 12 */ {"2:w28=60,"},
    /* 13 */ {"2:w28=1200,"},
    /* 14 */ {"2:w28=60,", "2:w32=120,"},
    /* 15 */ {"2:w11=1,0,", "2:w28=600,", "2:w32=220,"},
    /* 16 */ {"2:w11=0,1,", "2:w32=120,", "2:w25=3750007,", "2:w29=1200,"},
    /* 17 */ {"2:w29=60,"},
    /* 18 */ {"2:w29=1200,"},
    /* 19 */ {"2:w29=60,", "2:w33=120,", "2:w11=0,1,", "2:w24=3750007,", "2:w13=0,"},
    /* 20 */ {"1:w11=0,0,", "2:w11=1,1,", "2:w24=100000007,", "2:w13=1,", "2:w29=600,", "2:w33=220,"},
    /* 21 */ {"2:w11=0,0,", "2:w13=0,"},
    /* 22 */ {0}, {0}, {0}, {0}, {0}, {0},
    /* 28 */ {"1:w11=0,0,", "2:w11=0,0,"},
};
static const char *const k_trace_jig[29][17] = {
    /*  0 */ {"1:w20=0,", "2:w21=0,", "1:w11=1,1,", "2:w11=1,1,", "1:w32=120,", "1:w33=120,", "2:w32=120,", "2:w33=120,", "1:w24=123456007,", "1:w25=123456007,", "2:w24=123456007,", "2:w25=123456007,", "1:w28=1500,", "1:w29=1500,", "2:w28=1500,", "2:w29=1500,"},
    /*  1 */ {"1:w11=1,0,", "2:w11=0,0,", "1:w24=3750007,", "1:w28=1200,"},
    /*  2 */ {"1:w28=60,"},
    /*  3 */ {"1:w28=1200,"},
    /*  4 */ {"1:w20=0,", "1:w28=60,", "1:w32=120,"},
    /*  5 */ {"1:w11=1,0,", "1:w28=600,", "1:w32=220,", "1:w20=1,", "1:w36=0,"},
    /*  6 */ {"1:w20=0,", "1:w11=0,1,", "1:w32=120,", "1:w25=3750007,", "1:w29=1200,"},
    /*  7 */ {"1:w29=60,"},
    /*  8 */ {"1:w29=1200,"},
    /*  9 */ {"1:w21=0,", "1:w11=0,1,", "1:w24=3750007,", "1:w29=60,", "1:w33=120,"},
    /* 10 */ {"1:w11=1,1,", "1:w24=100000007,", "2:w11=0,0,", "1:w29=600,", "1:w33=220,", "1:w21=1,", "1:w37=0,"},
    /* 11 */ {"1:w21=0,", "1:w11=0,0,", "1:w24=3750007,", "2:w11=1,0,", "1:w33=120,", "2:w24=3750007,", "2:w28=1200,"},
    /* 12 */ {"2:w28=60,"},
    /* 13 */ {"2:w28=1200,"},
    /* 14 */ {"2:w20=0,", "2:w28=60,", "2:w32=120,"},
    /* 15 */ {"2:w11=1,0,", "2:w28=600,", "2:w32=220,", "2:w20=1,", "2:w36=0,"},
    /* 16 */ {"2:w20=0,", "2:w11=0,1,", "2:w32=120,", "2:w25=3750007,", "2:w29=1200,"},
    /* 17 */ {"2:w29=60,"},
    /* 18 */ {"2:w29=1200,"},
    /* 19 */ {"2:w21=0,", "2:w29=60,", "2:w33=120,", "2:w11=0,1,", "2:w24=3750007,"},
    /* 20 */ {"1:w11=0,0,", "2:w11=1,1,", "2:w24=100000007,", "2:w29=600,", "2:w33=220,", "2:w21=1,", "2:w37=0,"},
    /* 21 */ {"2:w21=0,", "2:w11=0,0,"},
    /* 22 */ {0}, {0}, {0}, {0}, {0}, {0},
    /* 28 */ {"1:w11=0,0,", "2:w11=0,0,"},
};

/* cases that reset both test exponents (decompilation of both functions) */
static int resets_exps(unsigned idx)
{
    return idx == 0 || idx == 2 || idx == 5 || idx == 7 || idx == 10 || idx == 12 || idx == 15 || idx == 17;
}

/* Expected combined log; for fexp/aexp != 0 the variable entries (the last two of cases
 * 1, 6, 11, 16) use the original's double arithmetic. */
static void expect_seq(char *out, const char *const *seq, unsigned idx, unsigned fexp, unsigned aexp)
{
    unsigned n = 0;
    while (n < 17 && seq[n])
        n++;
    int variable = idx == 1 || idx == 6 || idx == 11 || idx == 16;
    out[0] = 0;
    for (unsigned i = 0; i < n; i++) {
        const char *e = seq[i];
        char line[40];
        if (variable && i == n - 2 && fexp)
            sprintf(line, "%.5s%llu,", e + 1, (unsigned long long)(pow(2.0, fexp) * 200000.0 + 8.0));
        else if (variable && i == n - 1 && aexp)
            sprintf(line, "%.5s%llu,", e + 1, (unsigned long long)pow(10.0, aexp / 3.0));
        else
            strcpy(line, e + 1);
        out += sprintf(out, "%c>%s\r\n[W]\n", e[0], line);
    }
}

static void test_cal_sequences(void)
{
    static char exp[4096];
    for (int jig = 0; jig < 2; jig++) {
        const char *const(*table)[17] = jig ? k_trace_jig : k_trace_menu;
        for (unsigned idx = 0; idx < 29; idx++) {
            reset_world();
            auto_ack = 1;
            g_cal_index = (uint8_t)idx;
            jig ? cal_apply_test_jig() : cal_apply_test();
            expect_seq(exp, table[idx], idx, 0, 0);
            CHECK_BYTES(ev_log, ev_len, exp);
        }
        /* stepped frequency / amplitude on points 1, 6, 11, 16 */
        static const unsigned points[] = { 1, 6, 11, 16 };
        for (unsigned k = 0; k < 4; k++) {
            for (unsigned f = 0; f <= 9; f++) {
                for (unsigned a = 0; a <= 10; a += (a == 0 ? 4 : 1)) {
                    reset_world();
                    auto_ack = 1;
                    g_cal_index = (uint8_t)points[k];
                    g_cal_freq_exp = (uint8_t)f;
                    g_cal_ampl_exp = (uint8_t)a;
                    jig ? cal_apply_test_jig() : cal_apply_test();
                    expect_seq(exp, table[points[k]], points[k], f, a);
                    CHECK_BYTES(ev_log, ev_len, exp);
                    CHECK_EQ(g_cal_freq_exp, f);
                    CHECK_EQ(g_cal_ampl_exp, a);
                }
            }
        }
        /* exponent reset and out-of-range indices */
        for (unsigned idx = 0; idx < 40; idx++) {
            reset_world();
            auto_ack = 1;
            g_cal_index = (uint8_t)idx;
            g_cal_freq_exp = 3;
            g_cal_ampl_exp = 7;
            jig ? cal_apply_test_jig() : cal_apply_test();
            CHECK_EQ(g_cal_freq_exp, resets_exps(idx) ? 0 : 3);
            CHECK_EQ(g_cal_ampl_exp, resets_exps(idx) ? 0 : 7);
            if (idx > 28)
                CHECK_EQ(ev_len, 0);
        }
    }
    /* the documented values: 40 kHz .. 10.24 MHz, 21 .. 2154 */
    reset_world();
    auto_ack = 1;
    g_cal_index = 1;
    g_cal_freq_exp = 9;
    g_cal_ampl_exp = 10;
    cal_apply_test();
    CHECK_TX(0, ":w11=1,0,\r\n:w24=102400008,\r\n:w28=2154,\r\n");
    /* without :ok replies every command waits for the full timeout */
    reset_world();
    g_cal_index = 28;
    cal_apply_test();
    CHECK_EQ(delay_total, 2u * (1000u * 640u + 0x855u));
}

/* ------------------------------------------------------------------------------------------ */

static void test_storage(void)
{
    /* names: slots 0 and 31 are stored as zeros, 0xFF reads as 0 */
    reset_world();
    for (unsigned s = 0; s < 32; s++)
        for (unsigned i = 0; i < 60; i++)
            g_names[s][i] = (char)(0x20 + (s * 7 + i) % 90);
    names_save();
    CHECK_EQ(flash_erases, 1);
    for (unsigned s = 0; s < 32; s++) {
        int zero = (s == 0 || s == 31);
        for (unsigned i = 0; i < 60; i++)
            CHECK_EQ(flash_mem[0x5000 + s * 60 + i], zero ? 0 : (uint8_t)g_names[s][i]);
    }
    char saved[32][60];
    memcpy(saved, g_names, sizeof saved);
    memset(g_names, 0x55, sizeof g_names);
    names_load();
    CHECK(memcmp(g_names[1], saved[1], 30 * 60) == 0);
    CHECK_EQ(g_names[0][0], 0);
    CHECK_EQ(g_names[31][59], 0);
    flash_mem[0x5000 + 60 + 3] = 0xFF;
    names_load();
    CHECK_EQ(g_names[1][3], 0);
    flash_reset();
    names_load();
    for (unsigned i = 0; i < sizeof g_names; i++)
        CHECK_EQ(((uint8_t *)g_names)[i], 0);

    /* name_set: <= 60 bytes, stops at NUL, zero-fills, ignores bad slots */
    memset(g_names, 'x', sizeof g_names);
    name_set(2, "abc", 60);
    CHECK(memcmp(g_names[2], "abc\0\0", 5) == 0);
    CHECK_EQ(g_names[2][59], 0);
    memset(big, 'y', 100);
    name_set(3, big, 100);
    CHECK_EQ(g_names[3][59], 'y');
    CHECK_EQ(g_names[4][0], 'x');
    name_set(4, "abcdef", 2);
    CHECK(memcmp(g_names[4], "ab\0", 3) == 0);
    name_set(32, "bad", 3);
    name_set(1000, "bad", 3);

    /* programs: round trip, defaults for invalid records */
    reset_world();
    struct channel *c = &g_ch[1];
    for (unsigned i = 0; i < 5; i++)
        c->prog.hdr[i] = 1000u + i;
    c->prog.hdr[HDR_COUNT] = 200;
    for (unsigned i = 0; i < 200; i++)
        c->prog.freq[i] = 0x0123456789ABCDEFull + i;
    for (unsigned i = 0; i < 400; i++)
        c->params[i] = (uint16_t)(i * 31u);
    program_save(30, 1);
    CHECK_EQ(flash_mem[0x2E000], 1000 & 0xFF);
    CHECK_EQ(flash_mem[0x2E000 + 0x28], 0xEF);   /* little endian */
    CHECK_EQ(flash_mem[0x2E668 + 2], 31);
    struct channel copy = *c;
    memset(&g_ch[0], 0, sizeof g_ch[0]);
    program_load(30, 0);
    CHECK(memcmp(&g_ch[0].prog, &copy.prog, sizeof copy.prog) == 0);
    CHECK(memcmp(g_ch[0].params, copy.params, sizeof copy.params) == 0);
    CHECK_EQ(g_ch[1].prog.hdr[0], 1000);

    static const struct { uint64_t count, dwell; int valid; } hdrs[] = {
        { 1, 1, 1 }, { 200, 5, 1 }, { 0, 5, 0 }, { 201, 5, 0 }, { 5, 0, 0 },
        { 0x100000001ull, 5, 0 }, { 5, 0x100000000ull, 1 },
    };
    for (unsigned k = 0; k < sizeof hdrs / sizeof hdrs[0]; k++) {
        reset_world();
        g_ch[0].prog.hdr[0] = 7;
        g_ch[0].prog.hdr[HDR_COUNT] = hdrs[k].count;
        g_ch[0].prog.hdr[HDR_DWELL] = hdrs[k].dwell;
        g_ch[0].prog.freq[0] = 99;
        g_ch[0].params[0] = 1234;
        program_save(12, 0);
        memset(&g_ch[1], 0xAB, sizeof g_ch[1]);
        program_load(12, 1);
        if (hdrs[k].valid) {
            CHECK_EQ(g_ch[1].prog.hdr[0], 7);
            CHECK_EQ(g_ch[1].prog.freq[0], 99);
        } else {
            CHECK_EQ(g_ch[1].prog.hdr[0], 0);
            CHECK_EQ(g_ch[1].prog.hdr[1], 0);
            CHECK_EQ(g_ch[1].prog.hdr[2], 120);
            CHECK_EQ(g_ch[1].prog.hdr[3], 1);
            CHECK_EQ(g_ch[1].prog.hdr[4], 1);
            CHECK_EQ(g_ch[1].prog.freq[0], 0);
            CHECK_EQ(g_ch[1].prog.freq[199], 0);
        }
        CHECK_EQ(g_ch[1].params[0], 1234);   /* read regardless */
    }
    reset_world();
    memset(&g_ch[0], 0xAB, sizeof g_ch[0]);
    program_load(9, 0);   /* erased slot */
    CHECK_EQ(g_ch[0].prog.hdr[2], 120);
    CHECK_EQ(g_ch[0].params[0], 0xFFFF);

    /* slots outside 1..30 are never written; program_erase */
    reset_world();
    program_save(0, 0);
    program_save(31, 0);
    program_save(3, 2);
    CHECK_EQ(flash_erases + flash_programs, 0);
    program_save(1, 0);
    CHECK(!flash_is_erased(0x11000, 0x988));
    program_erase(1);
    CHECK(flash_is_erased(0x11000, 0x1000));
    program_erase(0);
    program_erase(31);
    CHECK_EQ(flash_erases, 2);

    /* calibration table */
    reset_world();
    int16_t tbl[32];
    for (unsigned i = 0; i < 32; i++)
        tbl[i] = (int16_t)(i * 1000 - 5000);
    tbl[31] = 30;   /* invalid marker */
    memcpy(&flash_mem[0x74000], tbl, 64);
    cal_load();
    CHECK(memcmp(g_cal_table, k_cal_defaults, 64) == 0);
    tbl[31] = 31;
    memcpy(&flash_mem[0x74000], tbl, 64);
    cal_load();
    CHECK(memcmp(g_cal_table, tbl, 64) == 0);

    reset_world();
    cal_save(0);
    CHECK_EQ(flash_erases, 1);
    CHECK(memcmp(&flash_mem[0x74000], k_cal_defaults, 64) == 0);
    CHECK(flash_is_erased(0x74040, 0x1000 - 0x40));
    cal_save(0);   /* unchanged: no write */
    CHECK_EQ(flash_erases, 1);
    cal_save(1);   /* DEVIATION: backup written although the working copy is unchanged */
    CHECK_EQ(flash_erases, 2);
    CHECK(memcmp(&flash_mem[0x75000], k_cal_defaults, 64) == 0);
    cal_save(1);
    CHECK_EQ(flash_erases, 2);
    g_cal_table[7] = -2;
    cal_save(1);
    CHECK_EQ(flash_erases, 4);
    CHECK(memcmp(&flash_mem[0x74000], g_cal_table, 64) == 0);
    CHECK(memcmp(&flash_mem[0x75000], g_cal_table, 64) == 0);

    /* restore: only if the backup's word 0 is not 0xFFFF */
    reset_world();
    g_cal_table[1] = 11;
    cal_restore_backup();
    CHECK_EQ(g_cal_table[1], 11);
    CHECK_EQ(flash_erases, 0);
    memcpy(&flash_mem[0x75000], tbl, 64);
    cal_restore_backup();
    CHECK(memcmp(g_cal_table, tbl, 64) == 0);
    CHECK(memcmp(&flash_mem[0x74000], tbl, 64) == 0);
    CHECK_EQ(flash_erases, 1);
    cal_restore_backup();   /* already equal: no write */
    CHECK_EQ(flash_erases, 1);

    /* erase all */
    reset_world();
    program_save(1, 0);
    program_save(30, 0);
    names_save();
    strcpy(g_names[5], "x");
    storage_erase_all();
    CHECK(flash_is_erased(0x5000, 0x1000));
    CHECK(flash_is_erased(0x10000, 0x20000));
    CHECK_EQ(g_names[5][0], 0);
    CHECK_EQ(flash_erases, 3 + 30 + 1);
    CHECK_EQ(flash_violations, 0);

    /* driver: page splitting at unaligned addresses, JEDEC ID */
    reset_world();
    uint8_t data[700], back[700];
    for (unsigned i = 0; i < sizeof data; i++)
        data[i] = (uint8_t)(i * 7u + 1u);
    CHECK_EQ(spiflash_sector_erase(0x40123), 0);
    CHECK(flash_is_erased(0x40000, 0x1000));
    CHECK_EQ(spiflash_write(0x400F0, data, sizeof data), 0);
    CHECK_EQ(flash_programs, 4);   /* 16 + 256 + 256 + 172 */
    spiflash_read(0x400F0, back, sizeof back);
    CHECK(memcmp(back, data, sizeof data) == 0);
    CHECK(flash_is_erased(0x40000, 0xF0));
    CHECK(flash_is_erased(0x400F0 + 700, 0x1000 - 0xF0 - 700));
    uint8_t id[3];
    spiflash_read_jedec_id(id);
    CHECK(id[0] == 0xEF && id[1] == 0x40 && id[2] == 0x15);
    CHECK_EQ(flash_violations, 0);
    CHECK(flash_busy_polls > 0);
}

/* ------------------------------------------------------------------------------------------ */

static uint8_t *orig;

static void load_backup(void)
{
    memcpy(flash_mem, orig, FLASH_SIZE);
}

static void test_real_backup(void)
{
    FILE *f = fopen(backup_path, "rb");   /* read only */
    if (!f) {
        printf("NOTE: %s not found, real-backup test skipped\n", backup_path);
        return;
    }
    orig = malloc(FLASH_SIZE);
    size_t n = fread(orig, 1, FLASH_SIZE, f);
    fclose(f);
    CHECK_EQ(n, FLASH_SIZE);
    if (n != FLASH_SIZE)
        return;

    reset_world();
    load_backup();
    names_load();
    static const char *const names[8] = {
        "", "Schumann Resonance (CAFL)", "Schumann Resonance (CAFL)", "Schumann Resonance (CAFL)",
        "Plant Growth (CUST)", "Abiotrophia defectiva (DNA) (R) - JW (CUST)",
        "Abiotrophia elegans (DNA) (R) - JW (CUST)",
        "Ornithonyssus Bird Mite_1 (HC), Ornithonyssus Bird Mite_2 (",
    };
    for (unsigned s = 0; s < 32; s++)
        CHECK(strncmp(g_names[s], s < 8 ? names[s] : "", 60) == 0);
    CHECK_EQ(strlen(g_names[7]), 59);

    cal_load();
    CHECK_EQ(g_cal_table[0], 31947);
    CHECK_EQ(g_cal_table[1], 1686);
    CHECK_EQ(g_cal_table[20], 635);
    CHECK_EQ((uint16_t)g_cal_table[25], 34855);
    CHECK_EQ(g_cal_table[31], 31);

    for (unsigned s = 1; s <= 30; s++) {
        program_load(s, s & 1u);
        const struct program *p = &g_ch[s & 1u].prog;
        CHECK_EQ(p->hdr[HDR_WAVE], s <= 8 ? 40 + s : 48);
        CHECK_EQ(p->hdr[HDR_AMPL], 2000);
        CHECK_EQ(p->hdr[HDR_OFFS], 120);
        CHECK(p->hdr[HDR_DWELL] >= 90 && p->hdr[HDR_DWELL] <= 2700);
        CHECK(p->hdr[HDR_COUNT] >= 1 && p->hdr[HDR_COUNT] <= 16);
        for (unsigned i = 0; i < 400; i++)
            CHECK_EQ(g_ch[s & 1u].params[i], 0);
    }
    program_load(1, 0);
    CHECK_EQ(g_ch[0].prog.freq[0], 7836);   /* 7.83 Hz */
    CHECK_EQ(g_ch[0].prog.hdr[HDR_DWELL], 600);
    program_load(8, 1);
    CHECK_EQ(g_ch[1].prog.hdr[HDR_COUNT], 16);
    CHECK_EQ(g_ch[1].prog.hdr[HDR_DWELL], 90);
    CHECK_EQ(g_ch[1].prog.freq[0], 2710411881177891ull);

    /* host queries against the real data */
    tx_clear();
    feed(0, ":n01=*\r\n");
    link_process(0);
    CHECK_TX(0, ":p01=41,2000,120,600,1,7836,\r\n");
    tx_clear();
    feed(1, ":n07=*\r\n");
    link_process(1);
    {
        static const char prefix[] = ":p07=47,2000,120,180,6,5481257,548758,8564455,";
        CHECK(tx_len[1] > sizeof prefix && memcmp(tx_log[1], prefix, sizeof prefix - 1) == 0);
        CHECK(memcmp(tx_log[1] + tx_len[1] - 3, ",\r\n", 3) == 0);
    }
    tx_clear();
    feed(1, ":n05=?\r\n");
    link_process(1);
    CHECK_TX(1, ":n05=Abiotrophia defectiva (DNA) (R) - JW (CUST)\r\n");
    tx_clear();
    feed(0, ":n07=?\r\n");
    link_process(0);
    CHECK_TX(0, ":n07=Ornithonyssus Bird Mite_1 (HC), Ornithonyssus Bird Mite_2\r\n");
    tx_clear();
    feed(0, ":n02=#\r\n");
    link_process(0);
    char *p = big + sprintf(big, ":g02=");
    for (unsigned i = 0; i < 200; i++)
        p += sprintf(p, "0,");
    strcpy(p, "\r\n");
    CHECK_TX(0, big);

    /* the dump of slot 8 re-uploaded to slot 8 of a blank chip gives the same sector */
    tx_clear();
    feed(0, ":n08=*\r\n");
    link_process(0);
    memcpy(big, tx_log[0], tx_len[0]);
    big[tx_len[0]] = 0;
    uint8_t sector[0x1000];
    memcpy(sector, &flash_mem[0x18000], sizeof sector);
    program_load(8, 1);
    memcpy(g_ch[0].params, g_ch[1].params, sizeof g_ch[0].params);   /* :g upload of the zeros */
    memset(&g_ch[0].prog, 0, sizeof g_ch[0].prog);
    flash_reset();
    feed(0, big);
    link_process(0);
    CHECK(memcmp(&flash_mem[0x18000], sector, sizeof sector) == 0);

    /* re-saving everything that was loaded rebuilds the used sectors byte for byte */
    load_backup();
    static struct channel slots[31];
    for (unsigned s = 1; s <= 30; s++) {
        program_load(s, 0);
        slots[s] = g_ch[0];
    }
    names_load();
    cal_load();
    flash_reset();
    names_save();
    for (unsigned s = 1; s <= 30; s++) {
        g_ch[0] = slots[s];
        program_save(s, 0);
    }
    cal_save(0);
    CHECK(memcmp(&flash_mem[0x5000], &orig[0x5000], 0x1000) == 0);
    CHECK(memcmp(&flash_mem[0x11000], &orig[0x11000], 30 * 0x1000) == 0);
    CHECK(memcmp(&flash_mem[0x74000], &orig[0x74000], 0x1000) == 0);
    CHECK_EQ(flash_violations, 0);

    /* w62 restores the factory copy of the real unit */
    reset_world();
    load_backup();
    cal_load();
    feed(0, ":w62=3336333,\r\n");
    link_process(0);
    CHECK(memcmp(g_cal_table, &orig[0x75000], 64) == 0);
    CHECK(memcmp(&flash_mem[0x74000], &orig[0x75000], 64) == 0);
    CHECK(memcmp(&flash_mem[0x75000], &orig[0x75000], 0x1000) == 0);
    free(orig);
}

int main(int argc, char **argv)
{
    backup_path = argc > 1 ? argv[1] : "../../../backups/spi_flash_20260923-132645.bin";
    test_tx_builders();
    test_wait_ok();
    test_g_frames();
    test_p_frames();
    test_n_frames();
    test_w_mirror();
    test_w_deferred();
    test_gate();
    test_factory();
    test_cal_sequences();
    test_storage();
    test_real_backup();
    test_runner();
    test_fmt();
    test_link_hooks();
    test_runner_seq();
    test_pcmode();
    test_calmenu();
    test_spiflash_async();
    printf("%d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
