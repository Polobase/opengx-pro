/* Host model of the board around the nx firmware (nx_world.h). */
#include "nx_resume.h"
#include "nx_world.h"

#include <stdio.h>
#include <stdlib.h>

#include "harness.h"
#include "link.h"
#include "mock_board.h"
#include "nx_bl.h"
#include "nx_core.h"
#include "nx_diag.h"
#include "nx_fault.h"
#include "nx_hw.h"
#include "nx_keys.h"
#include "nx_lcd.h"
#include "nx_loop.h"
#include "nx_chain.h"
#include "nx_manual.h"
#include "nx_preset.h"
#include "nx_terrain.h"
#include "nx_hist.h"
#include "nx_panel.h"
#include "nx_store.h"
#include "nx_time.h"
#include "nx_txq.h"
#include "nx_uart.h"
#include "nx_ui.h"
#include "nx_wdg.h"
#include "state.h"
#include "test_nx.h"

#define BYTE_US 87u   /* 10 bits at 115200 baud */
#define RXQ 64u

uint8_t nxw_keys;
uint32_t nxw_pb1;
uint16_t nxw_bl_on_us, nxw_bl_period_us;
unsigned nxw_bl_calls;
unsigned nxw_wdg_feeds;
uint64_t nxw_line_us[2][64];
uint64_t nxw_line_end_us[2][64];
unsigned nxw_line_n[2];
jmp_buf nxw_power_off_jmp;
int nxw_power_off_armed;
uint64_t nxw_power_off_us;

struct rx_ev {
    uint64_t t;
    unsigned ch;
    char s[160];   /* a :w80 reply is ~110 bytes */
};
static struct rx_ev rxq[RXQ];
static unsigned rxn;
static uint64_t key_at_us[8];
static uint8_t key_at_mask[8];
static unsigned key_at_n;
static uint64_t tx_done_us[3];
static char tx_line[2][64];
static unsigned tx_line_len[2];
static int gen_mode[2];
static uint32_t gen_ack_us[2];
uint32_t nxw_table_ack_us;

/* ---- what the firmware reads from the board ---- */

uint8_t nx_hw_keys(void)
{
    return nxw_keys;
}

uint32_t nx_hw_pb1(void)
{
    return nxw_pb1;
}

void nx_hw_bl_set(uint16_t on_us, uint16_t period_us)
{
    nxw_bl_on_us = on_us;
    nxw_bl_period_us = period_us;
    nxw_bl_calls++;
}

void nx_hw_uid(uint8_t out[12])
{
    for (unsigned i = 0; i < 12u; i++)
        out[i] = (uint8_t)(0xA0u + i);
}

uint16_t nx_hw_adc(unsigned channel)   /* 30 degC at VDDA 3.3 V, as the emulator's ADC */
{
    return channel == NX_ADC_TEMP ? 943u : channel == NX_ADC_VREF ? 1505u : 0u;
}

void nx_hw_adc_cal(uint16_t *ts_cal, uint16_t *vref_cal)
{
    *ts_cal = 1037u;
    *vref_cal = 1656u;
}

uint32_t nx_ms(void)
{
    return sim_ms();
}

uint16_t nx_us16(void)
{
    return (uint16_t)sim_us;
}

void nx_wdg_feed(void)
{
    nxw_wdg_feeds++;
    sim_advance_us(5);   /* a busy wait takes time; interrupts (the tick) keep running */
    nxw_poll();
}

unsigned uart_tx_room(enum uart_port port)   /* the transmit ring of nx_uart.c: 512 bytes for a link */
{
    uint64_t wait = tx_done_us[port] > sim_us ? (tx_done_us[port] - sim_us + BYTE_US - 1u) / BYTE_US : 0u;
    return wait >= 512u ? 0u : 512u - (unsigned)wait;
}

int uart_tx_idle(enum uart_port port)
{
    return sim_us >= tx_done_us[port];
}

void power_off_signal(void)
{
    if (!nxw_power_off_armed) {
        printf("power_off_signal() reached without nxw_power_off_armed\n");
        abort();
    }
    nxw_power_off_armed = 0;
    nxw_power_off_us = sim_us;
    longjmp(nxw_power_off_jmp, 1);
}

/* ---- received bytes and key changes at given times ---- */

void nxw_rx_at(uint64_t t_us, unsigned ch, const char *s)
{
    if (rxn >= RXQ) {
        printf("nx_world: receive queue full\n");
        abort();
    }
    unsigned i = rxn;
    while (i > 0 && rxq[i - 1].t > t_us) {   /* keep the queue sorted, stable for equal times */
        rxq[i] = rxq[i - 1];
        i--;
    }
    rxq[i].t = t_us;
    rxq[i].ch = ch;
    snprintf(rxq[i].s, sizeof rxq[i].s, "%s", s);
    rxn++;
}

void nxw_rx(unsigned ch, const char *s)
{
    feed(ch, s);
}

void nxw_keys_at(uint64_t t_us, uint8_t keys)
{
    if (key_at_n < 8u) {
        key_at_us[key_at_n] = t_us;
        key_at_mask[key_at_n++] = keys;
    }
}

void nxw_poll(void)
{
    while (key_at_n && key_at_us[0] <= sim_us) {
        nxw_keys = key_at_mask[0];
        key_at_n--;
        memmove(key_at_us, key_at_us + 1, key_at_n * sizeof key_at_us[0]);
        memmove(key_at_mask, key_at_mask + 1, key_at_n);
    }
    while (rxn && rxq[0].t <= sim_us) {
        struct rx_ev e = rxq[0];
        memmove(rxq, rxq + 1, --rxn * sizeof rxq[0]);
        feed(e.ch, e.s);
    }
}

/* ---- generators and transmit time ---- */

void nxw_gen(unsigned ch, int mode, uint32_t ack_us)
{
    gen_mode[ch] = mode;
    gen_ack_us[ch] = ack_us;
}

static void on_tx(unsigned port, uint8_t c)
{
    uint64_t start = tx_done_us[port] > sim_us ? tx_done_us[port] : sim_us;
    tx_done_us[port] = start + BYTE_US;
    if (port > 1u)
        return;
    if (tx_line_len[port] == 0 && nxw_line_n[port] < 64u)
        nxw_line_us[port][nxw_line_n[port]] = start;
    if (tx_line_len[port] < sizeof tx_line[port] - 1u)
        tx_line[port][tx_line_len[port]++] = (char)c;
    if (c == '\n') {
        if (nxw_line_n[port] < 64u)
            nxw_line_end_us[port][nxw_line_n[port]++] = tx_done_us[port];
        if (gen_mode[port] != NXW_NOACK && tx_line[port][0] == ':' && tx_line[port][1] == 'w')
            nxw_rx_at(tx_done_us[port] + gen_ack_us[port], port, ":ok\r\n");
        if (gen_mode[port] == NXW_ACK && tx_line[port][0] == ':' && tx_line[port][1] == 'a')   /* a wave table */
            nxw_rx_at(tx_done_us[port] + nxw_table_ack_us, port, ":ok\r\n");
        tx_line_len[port] = 0;
    }
}

void nxw_tx_model(int on)
{
    uart_tx_hook = on ? on_tx : NULL;
    rxn = 0;
}

void nxw_clear(void)
{
    tx_clear();
    nxw_line_n[0] = nxw_line_n[1] = 0;
}

unsigned nxw_lines(unsigned ch, char lines[][32], unsigned max)
{
    unsigned n = 0, k = 0;
    for (size_t i = 0; i < tx_len[ch] && n < max; i++) {
        char c = tx_log[ch][i];
        if (c == '\n') {
            lines[n][k] = 0;
            n++;
            k = 0;
        } else if (c != '\r' && k < 31u) {
            lines[n][k++] = c;
        }
    }
    return n;
}

unsigned nxw_line(unsigned ch, unsigned n, char *out, unsigned cap)
{
    unsigned len = 0;
    for (size_t i = 0; i < tx_len[ch]; i++) {
        char c = tx_log[ch][i];
        if (c == '\n') {
            if (!n--) {
                out[len < cap ? len : cap - 1u] = 0;
                return len;
            }
            len = 0;
        } else if (c != '\r') {
            if (len + 1u < cap)
                out[len] = c;
            len++;
        }
    }
    out[0] = 0;
    return 0;
}

static void tick(void)   /* every simulated millisecond: the SysTick handler */
{
    nxw_poll();
    nx_tick_1ms();
}

/* ---- power-on ---- */

void nxw_reset(void)
{
    nx_reset_world();
    memset(g_ch, 0, sizeof g_ch);
    for (unsigned ch = 0; ch < 2u; ch++) {   /* main_nx.c: the empty program */
        g_ch[ch].prog.hdr[HDR_OFFS] = 120;
        g_ch[ch].prog.hdr[HDR_DWELL] = 1;
        g_ch[ch].prog.hdr[HDR_COUNT] = 1;
    }
    g_ui_flags = UIF_PC_UNLOCK;
    g_pc_flags = g_misc_flags = g_factory_cmd_flags = 0;
    g_ui_state = ST_BOOT;
    memset(g_names, 0, sizeof g_names);
    g_cal_index = g_cal_freq_exp = g_cal_ampl_exp = 0;
    memcpy(g_cal_table, k_cal_defaults, sizeof g_cal_table);
    link_init();

    nxw_keys = 0;
    nxw_pb1 = 0;
    nxw_bl_on_us = nxw_bl_period_us = 0;
    nxw_bl_calls = 0;
    rxn = 0;
    key_at_n = 0;
    nxw_wdg_feeds = 0;
    memset(tx_done_us, 0, sizeof tx_done_us);
    tx_line_len[0] = tx_line_len[1] = 0;
    nxw_line_n[0] = nxw_line_n[1] = 0;
    nxw_gen(0, NXW_ACK, 1000);
    nxw_gen(1, NXW_ACK, 1000);
    nxw_table_ack_us = 300000;
    uart_tx_hook = on_tx;
    nxw_power_off_armed = 0;

    /* the nx modules, as after a reset */
    nx_diag_reset();
    nx_time_reset();
    nx_keys_reset();
    nx_bl_init();
    nx_txq_reset();
    nx_core_reset();
    nx_loop_reset();
    nx_store_reset();
    memset(&g_nx_noinit, 0, sizeof g_nx_noinit);
    nxp_reset();         /* the panel as lcd_init() leaves it, then the nx driver (main_nx.c) */
    nx_lcd_init();
    nx_hist_boot();      /* the store is not mounted yet: nothing found, the module's state reset */
    nx_chain_boot();
    nx_terrain_boot();
    nx_manual_boot();
    nx_preset_boot();
    nx_resume_boot();    /* ... no offer, the module's state reset */
    nx_ui_init();
    sim_on_ms(tick);
}

/* ---- running the firmware ---- */

void nxw_step(uint32_t us)
{
    nx_loop_pass();
    nxw_poll();
    sim_advance_us(us);
    nxw_poll();
}

void nxw_run_ms(uint32_t ms)
{
    uint64_t end = sim_us + (uint64_t)ms * 1000u;
    while (sim_us < end)
        nxw_step(50);
}

void nxw_run_until(uint32_t t_ms)
{
    while (sim_ms() < t_ms)
        nxw_step(50);
}

void nxw_press(uint8_t keys, uint32_t hold_ms)
{
    nxw_keys |= keys;
    nxw_run_ms(hold_ms);
    nxw_keys &= (uint8_t)~keys;
    nxw_run_ms(100);
}
