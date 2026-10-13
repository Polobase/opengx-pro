/* Host unit-test harness: checks, the mocked UARTs, delay loop, LCD/UI stubs and the W25Q16
 * flash model. */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern int g_checks, g_fails;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        g_checks++;                                                                     \
        if (!(cond)) {                                                                  \
            g_fails++;                                                                  \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);             \
        }                                                                               \
    } while (0)

#define CHECK_EQ(got, exp)                                                              \
    do {                                                                                \
        unsigned long long g_ = (unsigned long long)(got), e_ = (unsigned long long)(exp); \
        g_checks++;                                                                     \
        if (g_ != e_) {                                                                 \
            g_fails++;                                                                  \
            printf("%s:%d: %s = %llu, expected %llu\n", __FILE__, __LINE__, #got, g_, e_); \
        }                                                                               \
    } while (0)

/* Compare a byte buffer with an expected C string (exact length). */
#define CHECK_BYTES(buf, len, exp) check_bytes(__FILE__, __LINE__, (const char *)(buf), (len), (exp))
void check_bytes(const char *file, int line, const char *got, size_t len, const char *exp);

/* ---- mocks.c ---- */

/* Everything sent on USART1/2/3, per port, and a combined log: each line is prefixed with
 * "1>", "2>" or "3>", and every link_wait_ok tail (delay_loop(0x855)) adds "[W]". */
extern char tx_log[3][1 << 14];
extern size_t tx_len[3];
extern char ev_log[1 << 14];
extern size_t ev_len;

extern uint64_t delay_total;         /* sum of all delay_loop iterations */
extern unsigned delay_calls;
extern unsigned delay_calls_34155;   /* delay_loop(0x34155) calls (w92) */
extern unsigned reset_calls;         /* NVIC_SystemReset */
extern unsigned lcd_clear_calls;     /* lcd_fill_rect(0,0,239,319,0) */
extern unsigned lcd_other_calls;
extern unsigned ui_values_calls;     /* ui_cal_draw_values */
extern unsigned ui_value_calls;
extern unsigned ui_value_last;

/* Generator model: when set, every line sent on USART1/2 is answered with ":ok\r\n" on the
 * same link, delivered during the next delay_loop call (i.e. inside link_wait_ok). */
extern int auto_ack;
/* Bytes delivered on a link during the next delay_loop call (e.g. inside link_wait_ok). */
void inject_on_delay(unsigned port, const char *s);

/* Called with every byte uart_putc() sends (NULL: none); the nx tests model transmit time with it. */
extern void (*uart_tx_hook)(unsigned port, uint8_t c);

void mocks_reset(void);
void tx_clear(void);
void feed(unsigned port, const char *s);                 /* bytes through uart_rx_hook */
void feed_bytes(unsigned port, const uint8_t *s, size_t n);

/* ---- w25q16.c ---- */
#define FLASH_SIZE 0x200000u
extern uint8_t *flash_mem;   /* FLASH_SIZE bytes */
extern unsigned flash_erases, flash_programs, flash_violations, flash_busy_polls;
void flash_reset(void);   /* erased chip, counters cleared */
int flash_is_erased(uint32_t addr, uint32_t len);

/* ---- test_link.c ---- */
void reset_world(void);   /* mocks, flash model and all firmware state back to power-on values */

/* ---- test_core.c: shared cores ---- */
void test_runner(void);
void test_runner_seq(void);
void test_fmt(void);
void test_link_hooks(void);
void test_pcmode(void);
void test_calmenu(void);
void test_spiflash_async(void);

/* ---- simclock.c: simulated time for the nx tests (microseconds since sim_reset) ---- */
extern uint64_t sim_us;
void sim_reset(void);                     /* time 0, no tick hook */
void sim_advance_us(uint64_t us);         /* runs the tick hook once per millisecond boundary crossed */
void sim_advance_ms(uint32_t ms);
uint32_t sim_ms(void);                    /* whole milliseconds */
void sim_on_ms(void (*fn)(void));         /* e.g. the nx firmware's SysTick handler */

/* ---- w25q16.c: busy time in simulated microseconds ---- */
#define FLASH_T_PP_US 400u                /* page program, W25Q16 typical */
#define FLASH_T_SE_US 45000u              /* 4 KB sector erase, typical */
/* 1: busy for the typical times, each SPI byte taking 1 us of simulated time; 0 (the default
 * after flash_reset): busy for 3 status polls after a program, 5 after an erase. */
void flash_timing_us(int on);

/* ---- w25q16.c: power cuts, wear and an address guard (nx storage tests) ---- */
enum { FLASH_CUT_CLEAN = 0, FLASH_CUT_TEAR_PAGE = 1, FLASH_CUT_TEAR_ERASE = 2 };
extern unsigned flash_op_count;           /* programs + erases started since flash_reset */
extern unsigned flash_guard_violations;   /* programs/erases outside the flash_guard range */
/* Power cut at the op_index-th program/erase from now (0 = the next one). CLEAN: that operation
 * and everything after it never happen. TEAR_PAGE / TEAR_ERASE (may be or-ed): a page program
 * applies the first half of its bytes in the order sent, a sector erase erases the first 2 KB;
 * an operation the flags do not fit is dropped as with CLEAN. Without power the chip ignores
 * everything (status reads 0, data 0xFF) until flash_power_on(). flash_reset() disarms the cut. */
void flash_cut_at(unsigned op_index, unsigned mode);
int flash_cut_happened(void);
void flash_power_on(void);
unsigned flash_erase_count(uint32_t addr);   /* erases of the 4 KB sector containing addr */
void flash_guard(uint32_t lo, uint32_t hi);  /* count programs/erases outside [lo, hi); default: whole chip */
