#include "fmt.h"

char *fmt_uint(char *p, uint32_t v)
{
    char tmp[10];
    int n = 0;
    do {
        tmp[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        *p++ = tmp[--n];
    *p = 0;
    return p;
}

char *fmt_hex(char *p, uint32_t v, int digits)
{
    static const char hex[] = "0123456789ABCDEF";
    while (digits--)
        *p++ = hex[(v >> (4 * digits)) & 15u];
    *p = 0;
    return p;
}

char *fmt_str(char *p, const char *s)
{
    while (*s)
        *p++ = *s++;
    *p = 0;
    return p;
}

char *fmt_put_fixed(char *p, uint32_t v, unsigned digits)
{
    for (unsigned i = digits; i-- > 0;) {
        p[i] = (char)('0' + v % 10u);
        v /= 10u;
    }
    return p + digits;
}

/* DEVIATION: all 20 digits of a u64; the original's u64_pow10_floor stops at 10^16, which
 * garbles values >= 10^16 (never used by real programs). */
char *fmt_put_u64(char *p, uint64_t v)
{
    char tmp[20];
    unsigned n = 0;
    do {
        tmp[n++] = (char)('0' + (unsigned)(v % 10u));
        v /= 10u;
    } while (v);
    while (n)
        *p++ = tmp[--n];
    return p;
}

uint32_t fmt_crc32(uint32_t crc, const uint8_t *data, uint32_t len)
{
    crc = ~crc;
    while (len--) {
        crc ^= *data++;
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
