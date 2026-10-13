/* External SPI NOR flash on SPI1, CS = PA1 (docs/spec/hardware.md §6): Winbond W25Q16 (2 MB,
 * 256-byte pages, 4 KB sectors) on the measured unit. Same command set as the original driver
 * (0x03 read, 0x06 write enable, 0x05 status, 0x20 sector erase, 0x02 page program).
 *
 * Only storage.c and the nx flash layer may call the erase/write functions. */
#pragma once

#include <stdint.h>

#define SPIFLASH_SIZE        0x200000u   /* W25Q16 */
#define SPIFLASH_PAGE_SIZE   0x100u
#define SPIFLASH_SECTOR_SIZE 0x1000u

void spiflash_init(void);
void spiflash_read_jedec_id(uint8_t id[3]);
void spiflash_read(uint32_t addr, uint8_t *buf, uint32_t len);

/* Erase the 4 KB sector containing addr (spiflash_sector_erase 0x0800BE9A).
 * Returns 0, or -1 if the chip stayed busy (the original waits forever). */
int spiflash_sector_erase(uint32_t addr);
/* Program len bytes at addr, split at 256-byte page boundaries like spiflash_write 0x0800BD14.
 * No erase inside: the caller erases first. Returns 0, or -1 on a busy timeout. */
int spiflash_write(uint32_t addr, const uint8_t *buf, uint32_t len);

/* Non-blocking use (nx flash jobs): start an operation, then poll spiflash_busy() until it
 * returns 0 before starting the next one or calling a blocking function. */
int spiflash_busy(void);                        /* 1 while an erase or program is in progress */
void spiflash_erase_begin(uint32_t addr);       /* erase the 4 KB sector containing addr */
/* Program from addr up to the end of its 256-byte page; returns the number of bytes started.
 * len must be >= 1: even for len == 0 this sends WREN and a page-program header. */
uint32_t spiflash_program_begin(uint32_t addr, const uint8_t *buf, uint32_t len);

/* Extension hooks, defined on the compiler command line by an app (undefined = no code):
 *   SPIFLASH_ACQUIRE()    first thing in every blocking call, e.g. to finish a pending
 *                         non-blocking operation
 *   SPIFLASH_POLL_HOOK()  on every busy poll of a blocking wait (e.g. to feed a watchdog);
 *                         runs with the chip selected, so it must not use SPI1 */
