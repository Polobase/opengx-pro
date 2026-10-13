#include "keys.h"

#include "board.h"

enum key_code keys_read(void)
{
    uint32_t b = ~GPIOB->IDR, d = ~GPIOD->IDR;   /* 1 = pressed */
    int g1 = b & (1u << 3), g2 = d & (1u << 2), stop = b & (1u << 5), power = b & (1u << 11);

    if (g1 && g2)
        return KEY_G1_G2;
    if (power && stop)
        return KEY_POWER_STOP;
    if (d & (1u << 0))
        return KEY_LOOP;
    if (d & (1u << 1))
        return KEY_START;
    if (stop)
        return KEY_STOP;
    if (b & (1u << 4))
        return KEY_UP;
    if (d & (1u << 3))
        return KEY_DOWN;
    if (g1)
        return KEY_G1;
    if (g2)
        return KEY_G2;
    if (power)
        return KEY_POWER;
    return KEY_NONE;
}

const char *key_name(enum key_code code)
{
    static const char *const names[] = {
        "-", "LOOP", "START", "STOP", "UP", "DOWN", "G1", "G2", "G1+G2", "POWER", "POWER+STOP",
    };
    return (unsigned)code < sizeof names / sizeof names[0] ? names[code] : "?";
}
