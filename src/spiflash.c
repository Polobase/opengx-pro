#include "spiflash.h"

#ifdef HOST_TEST
/* Host unit tests: the byte transfer and chip select come from a W25Q16 model. */
uint8_t spiflash_hw_xfer(uint8_t out);
void spiflash_hw_select(void);
void spiflash_hw_deselect(void);
#define xfer spiflash_hw_xfer
#define select spiflash_hw_select
#define deselect spiflash_hw_deselect

void spiflash_init(void)
{
}
#else
#include "board.h"

static uint8_t xfer(uint8_t out)
{
    while (!(SPI1->SR & SPI_SR_TXE)) {
    }
    *(volatile uint8_t *)&SPI1->DR = out;
    while (!(SPI1->SR & SPI_SR_RXNE)) {
    }
    return *(volatile uint8_t *)&SPI1->DR;
}

static void select(void)
{
    pin_low(FLASH_CS_PORT, FLASH_CS_PIN);
}

static void deselect(void)
{
    while (SPI1->SR & SPI_SR_BSY) {
    }
    pin_high(FLASH_CS_PORT, FLASH_CS_PIN);
}

void spiflash_init(void)
{
    RCC->APBENR2 |= RCC_APBENR2_SPI1EN;
    SPI1->CR1 = 0;
    SPI1->CR2 = (7u << SPI_CR2_DS_Pos) | SPI_CR2_FRXTH;   /* 8-bit frames, RXNE per byte */
    SPI1->CR1 = SPI_CR1_MSTR | SPI_CR1_CPOL | SPI_CR1_CPHA | SPI_CR1_SSM | SPI_CR1_SSI;   /* mode 3, 32 MHz */
    SPI1->CR1 |= SPI_CR1_SPE;
    while (SPI1->SR & SPI_SR_RXNE)
        (void)*(volatile uint8_t *)&SPI1->DR;
}
#endif

enum {
    CMD_PAGE_PROGRAM = 0x02,
    CMD_READ = 0x03,
    CMD_READ_STATUS = 0x05,
    CMD_WRITE_ENABLE = 0x06,
    CMD_SECTOR_ERASE = 0x20,
    CMD_JEDEC_ID = 0x9F,
};

#ifdef SPIFLASH_ACQUIRE
void SPIFLASH_ACQUIRE(void);
#define ACQUIRE() SPIFLASH_ACQUIRE()
#else
#define ACQUIRE() ((void)0)
#endif
#ifdef SPIFLASH_POLL_HOOK
void SPIFLASH_POLL_HOOK(void);
#define POLL_HOOK() SPIFLASH_POLL_HOOK()
#else
#define POLL_HOOK() ((void)0)
#endif

/* A W25Q16 sector erase takes at most 400 ms, a page program 3 ms. One status poll costs well
 * above 0.25 us, so this bound is several seconds. */
#define BUSY_POLLS 8000000u

static void send_cmd_addr(uint8_t cmd, uint32_t addr)
{
    xfer(cmd);
    xfer((uint8_t)(addr >> 16));
    xfer((uint8_t)(addr >> 8));
    xfer((uint8_t)addr);
}

void spiflash_read_jedec_id(uint8_t id[3])
{
    ACQUIRE();
    select();
    xfer(CMD_JEDEC_ID);
    for (unsigned i = 0; i < 3; i++)
        id[i] = xfer(0);
    deselect();
}

void spiflash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    ACQUIRE();
    select();
    send_cmd_addr(CMD_READ, addr);
    while (len--)
        *buf++ = xfer(0xFF);
    deselect();
}

/* spiflash_wait_ready 0x0800BF3C. DEVIATION: bounded; the original polls forever. */
static int wait_ready(void)
{
    uint32_t n = BUSY_POLLS;
    uint8_t status;
    select();
    xfer(CMD_READ_STATUS);
    do {
        POLL_HOOK();
        status = xfer(0xFF);
    } while ((status & 1u) && --n);
    deselect();
    return (status & 1u) ? -1 : 0;
}

static void write_enable(void)   /* spiflash_write_enable 0x0800BF6A */
{
    select();
    xfer(CMD_WRITE_ENABLE);
    deselect();
}

/* One sector-erase / page-program command without waiting; the chip must be idle. */
static void erase_cmd(uint32_t addr)
{
    write_enable();
    select();
    send_cmd_addr(CMD_SECTOR_ERASE, addr & ~(SPIFLASH_SECTOR_SIZE - 1u));
    deselect();
}

static void program_cmd(uint32_t addr, const uint8_t *buf, uint32_t len)   /* len within one page */
{
    write_enable();
    select();
    send_cmd_addr(CMD_PAGE_PROGRAM, addr);
    while (len--)
        xfer(*buf++);
    deselect();
}

static uint32_t page_room(uint32_t addr)   /* bytes from addr to the end of its 256-byte page */
{
    return SPIFLASH_PAGE_SIZE - (addr & (SPIFLASH_PAGE_SIZE - 1u));
}

int spiflash_sector_erase(uint32_t addr)
{
    ACQUIRE();
    if (wait_ready())
        return -1;
    erase_cmd(addr);
    return wait_ready();
}

/* spiflash_page_program 0x0800BE38: len bytes, never crossing a page boundary. */
static int page_program(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    program_cmd(addr, buf, len);
    return wait_ready();
}

int spiflash_write(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    ACQUIRE();
    if (wait_ready())
        return -1;
    while (len) {
        uint32_t room = page_room(addr);
        uint32_t n = len < room ? len : room;
        if (page_program(addr, buf, n))
            return -1;
        addr += n;
        buf += n;
        len -= n;
    }
    return 0;
}

int spiflash_busy(void)
{
    select();
    xfer(CMD_READ_STATUS);
    uint8_t status = xfer(0xFF);
    deselect();
    return status & 1u;
}

void spiflash_erase_begin(uint32_t addr)
{
    erase_cmd(addr);
}

uint32_t spiflash_program_begin(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    uint32_t room = page_room(addr);
    uint32_t n = len < room ? len : room;
    program_cmd(addr, buf, n);
    return n;
}
