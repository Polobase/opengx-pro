/* Minimal string formatting (no printf). Each function writes at p, NUL-terminates and
 * returns the position of the terminator so calls can be chained. */
#pragma once

#include <stdint.h>

char *fmt_uint(char *p, uint32_t v);
char *fmt_hex(char *p, uint32_t v, int digits);
char *fmt_str(char *p, const char *s);

/* Without NUL terminator, for building wire lines: */
char *fmt_put_fixed(char *p, uint32_t v, unsigned digits);   /* last `digits` digits, zero padded */
char *fmt_put_u64(char *p, uint64_t v);                      /* all digits of a u64 */
/* CRC-32 as zlib/binascii.crc32: start with 0, feed chunks by passing the previous result. */
uint32_t fmt_crc32(uint32_t crc, const uint8_t *data, uint32_t len);
