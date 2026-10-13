/* Command-level model of the Winbond W25Q16 behind spiflash.c (HOST_TEST build): chip select
 * framing, write-enable latch, busy status, NOR semantics (programming only clears bits,
 * page program wraps inside its 256-byte page and takes effect at CS high), 4 KB erase.
 * Busy time: by default a number of status polls (3 after a program, 5 after an erase); with
 * flash_timing_us(1) the chip's typical times in simulated microseconds (simclock.c), each SPI
 * byte taking 1 us. For the nx storage tests: power cuts before or in the middle of any program
 * or erase (torn writes), erase counters per sector and an address guard.
 * Protocol misuse is counted in flash_violations. */
#include <stdlib.h>

#include "harness.h"

uint8_t *flash_mem;
unsigned flash_erases, flash_programs, flash_violations, flash_busy_polls;
unsigned flash_op_count, flash_guard_violations;

static int selected;
static unsigned nbytes;      /* bytes since CS low */
static uint8_t cmd;
static uint32_t addr;
static int wel;              /* write-enable latch */
static unsigned busy;        /* poll timing: status polls until WIP clears */
static int timing_us;        /* 1: busy until busy_until_us of the simulated clock */
static uint64_t busy_until_us;
static uint8_t page_buf[256];
static uint8_t page_used[256];
static unsigned sector_erases[FLASH_SIZE / 0x1000u];
static uint32_t guard_lo, guard_hi = FLASH_SIZE;
static int powered = 1, cut_done;
static unsigned cut_left = ~0u, cut_mode;   /* operations still to complete before the cut; ~0u: none armed */

enum { OP_NONE, OP_FULL, OP_TORN };

void flash_reset(void)
{
    if (!flash_mem)
        flash_mem = malloc(FLASH_SIZE);
    memset(flash_mem, 0xFF, FLASH_SIZE);
    flash_erases = flash_programs = flash_violations = flash_busy_polls = 0;
    flash_op_count = flash_guard_violations = 0;
    memset(sector_erases, 0, sizeof sector_erases);
    guard_lo = 0;
    guard_hi = FLASH_SIZE;
    selected = 0;
    wel = 0;
    busy = 0;
    timing_us = 0;
    busy_until_us = 0;
    powered = 1;
    cut_done = 0;
    cut_left = ~0u;
}

void flash_timing_us(int on)
{
    timing_us = on;
}

void flash_cut_at(unsigned op_index, unsigned mode)
{
    cut_left = op_index;
    cut_mode = mode;
    cut_done = 0;
}

int flash_cut_happened(void)
{
    return cut_done;
}

void flash_power_on(void)
{
    powered = 1;
    cut_left = ~0u;
    selected = 0;
    wel = 0;
    busy = 0;
    busy_until_us = 0;
}

unsigned flash_erase_count(uint32_t a)
{
    return sector_erases[(a & (FLASH_SIZE - 1u)) >> 12];
}

void flash_guard(uint32_t lo, uint32_t hi)
{
    guard_lo = lo;
    guard_hi = hi;
}

static int chip_busy(void)
{
    return timing_us ? sim_us < busy_until_us : busy != 0;
}

static void start_busy(unsigned polls, unsigned us)
{
    busy = polls;
    busy_until_us = sim_us + us;
}

/* A program or erase starts at CS high: count it, check the guard, carry out an armed cut.
 * tear: the FLASH_CUT_TEAR_* flag that fits this operation. */
static int op_begin(uint32_t a, unsigned tear)
{
    flash_op_count++;
    if (a < guard_lo || a >= guard_hi)
        flash_guard_violations++;
    if (cut_left == ~0u)
        return OP_FULL;
    if (cut_left > 0) {
        cut_left--;
        return OP_FULL;
    }
    cut_done = 1;
    powered = 0;
    cut_left = ~0u;
    return (cut_mode & tear) ? OP_TORN : OP_NONE;
}

/* The page program's first `limit` bytes in the order they were sent (they wrap inside the page). */
static void program_page(unsigned limit)
{
    unsigned sent = nbytes - 4u > 256u ? 256u : nbytes - 4u;
    uint32_t page = addr & (FLASH_SIZE - 1u) & ~0xFFu;
    for (unsigned i = 0; i < sent && i < limit; i++) {
        unsigned col = (addr + i) & 0xFFu;
        if ((flash_mem[page | col] & page_buf[col]) != page_buf[col])
            flash_violations++;   /* would need a 0 -> 1 transition: not erased */
        flash_mem[page | col] &= page_buf[col];
    }
}

int flash_is_erased(uint32_t a, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++)
        if (flash_mem[a + i] != 0xFF)
            return 0;
    return 1;
}

void spiflash_hw_select(void)
{
    if (selected)
        flash_violations++;
    selected = 1;
    nbytes = 0;
    cmd = 0;
    addr = 0;
    memset(page_used, 0, sizeof page_used);
}

void spiflash_hw_deselect(void)
{
    if (!selected)
        flash_violations++;
    selected = 0;
    if (nbytes == 0 || !powered)
        return;
    switch (cmd) {
    case 0x06:
        if (chip_busy())
            flash_violations++;
        else
            wel = 1;
        break;
    case 0x02:
        if (chip_busy() || !wel || nbytes < 4) {
            flash_violations++;
            break;
        }
        switch (op_begin(addr & (FLASH_SIZE - 1u), FLASH_CUT_TEAR_PAGE)) {
        case OP_FULL:
            program_page(256u);
            break;
        case OP_TORN:
            program_page((nbytes - 4u) / 2u);
            break;
        default:
            return;
        }
        flash_programs++;
        wel = 0;
        start_busy(3u, FLASH_T_PP_US);
        break;
    case 0x20: {
        if (chip_busy() || !wel || nbytes != 4) {
            flash_violations++;
            break;
        }
        uint32_t base = addr & (FLASH_SIZE - 1u) & ~0xFFFu;
        int how = op_begin(base, FLASH_CUT_TEAR_ERASE);
        if (how == OP_NONE)
            return;
        memset(&flash_mem[base], 0xFF, how == OP_TORN ? 0x800u : 0x1000u);
        sector_erases[base >> 12]++;
        flash_erases++;
        wel = 0;
        start_busy(5u, FLASH_T_SE_US);
        break;
    }
    default:
        break;
    }
}

uint8_t spiflash_hw_xfer(uint8_t out)
{
    if (!selected) {
        flash_violations++;
        return 0xFF;
    }
    if (timing_us)
        sim_advance_us(1);
    unsigned n = nbytes++;
    if (!powered) {                          /* no supply: never busy, no data */
        if (n == 0)
            cmd = out;
        return n > 0 && cmd == 0x05 ? 0x00 : 0xFF;
    }
    if (n == 0) {
        cmd = out;
        if (chip_busy() && cmd != 0x05)
            flash_violations++;
        return 0xFF;
    }
    switch (cmd) {
    case 0x05: {
        uint8_t status = (uint8_t)((chip_busy() ? 1u : 0u) | (wel ? 2u : 0u));
        flash_busy_polls++;
        if (busy)
            busy--;
        return status;
    }
    case 0x9F: {
        static const uint8_t id[3] = { 0xEF, 0x40, 0x15 };
        return n <= 3 ? id[n - 1] : 0xFF;
    }
    case 0x03:
    case 0x02:
    case 0x20:
        if (n <= 3) {
            addr = (addr << 8) | out;
            return 0xFF;
        }
        if (cmd == 0x03) {
            uint8_t v = flash_mem[addr & (FLASH_SIZE - 1u)];
            addr = (addr + 1u) & (FLASH_SIZE - 1u);
            return v;
        }
        if (cmd == 0x02) {
            unsigned col = (addr + (n - 4u)) & 0xFFu;   /* wraps inside the page */
            if (page_used[col])
                flash_violations++;                        /* more than 256 bytes */
            page_used[col] = 1;
            page_buf[col] = out;
        } else {
            flash_violations++;
        }
        return 0xFF;
    default:
        return 0xFF;
    }
}
