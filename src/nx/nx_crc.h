/* CRC-32 of the nx storage (docs/nx/design/storage-runner-features.md §1.1 item 5): the same
 * value as zlib / Python's binascii.crc32, so the Mac tools can check every record. On the
 * device the STM32 CRC unit computes it (polynomial 0x04C11DB7, byte-reversed input, reversed
 * output); nx_crc_init() checks the unit against "123456789" = 0xCBF43926 and falls back to the
 * software version (fmt_crc32) if it disagrees. The host tests always use the software version. */
#pragma once

#include <stdint.h>

/* crc: 0 to start, or the result of the previous chunk (like zlib's crc32()). */
uint32_t nx_crc32(uint32_t crc, const void *buf, uint32_t len);
/* Device: enable and self-test the CRC unit. Returns 1 when it is used, 0 for the software path. */
int nx_crc_init(void);
