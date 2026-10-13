/* The Spooky2 boot splash of the original (docs/spec/display.md §9) as landscape 1-bit masks:
 * the ghost logo, the red "2", "Welcome to Spooky", the red "2", the tagline. tools/gen_nx_vendor.py
 * makes them from the user's own original image into the build directory (vendor artwork, never
 * committed); only builds with NX_VENDOR_SPLASH=1 (the Makefile, when original/GxDisplayFW.bin is
 * there) have them. Rows are MSB first, (w + 7) / 8 bytes each. */
#pragma once

#include <stdint.h>

struct nx_vmask {
    int16_t x, y, w, h;   /* landscape position and size */
    uint8_t red;          /* 1: the red of the "2"s, 0: the ink colour */
    const uint8_t *bits;
};

#define NX_VENDOR_SPLASH_N 5
extern const struct nx_vmask k_nx_vendor_splash[NX_VENDOR_SPLASH_N];
