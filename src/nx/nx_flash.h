/* Guarded SPI-flash access of the nx storage (docs/nx/design/storage-runner-features.md §1.1,
 * §1.3). nx writes only 0x080000-0x1FFFFF, which the vendor firmware never touches; a write
 * elsewhere is refused and counted, never executed. Every program is read back and compared. No
 * nx write happens in PC mode (the PC may be uploading). The blocking spiflash.c calls poll
 * through SPIFLASH_POLL_HOOK = nx_flash_poll, which feeds the watchdog, and start with
 * SPIFLASH_ACQUIRE = nx_flash_acquire, which waits out an operation still in progress (a legacy
 * read during an erase would return garbage). The legacy areas stay storage.c's alone. */
#pragma once

#include <stdint.h>

#define NX_FLASH_LO 0x080000u
#define NX_FLASH_HI 0x200000u
#define NX_FLASH_SECTOR 0x1000u
#define NX_FLASH_ACQUIRE_MS 1000u   /* > the W25Q16's 400 ms sector-erase maximum */

int nx_flash_read(uint32_t addr, void *buf, uint32_t len);          /* any address; NX_OK */
int nx_flash_erase(uint32_t addr);                                   /* the 4 KB sector at addr */
int nx_flash_program(uint32_t addr, const void *buf, uint32_t len);  /* program + read-back */
int nx_flash_is_erased(uint32_t addr, uint32_t len);                 /* 1 = all 0xFF */
/* Writes return NX_OK, NX_REFUSED_PC, NX_EGUARD (outside the nx area) or NX_EFLASH (busy
 * timeout or read-back mismatch). */

void nx_flash_acquire(void);   /* SPIFLASH_ACQUIRE */
void nx_flash_poll(void);      /* SPIFLASH_POLL_HOOK: runs with the chip selected, no SPI1 use */
