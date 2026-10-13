/* Host renderer of the nx engine gallery (src/nx/nxg_sheets.c): the same sheets and screens through
 * nx_lcd onto the panel model (tests/host/nx/nx_panel.c), each written as DIR/<name>.ppm (RGB888
 * with the channel expansion of the emulator's screenshots, Pillow "BGR;16": v * 255 / 31,
 * v * 255 / 63).
 *     tests/host/build/nxg_host DIR
 * Built with NXG_SCREENS_ONLY=1 and the image's engine switches (tests/host/Makefile,
 * gallery-dev), it renders only the screens (nxg_screens_run): build/nxg_host_dev DIR. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nx_lcd.h"
#include "nx_panel.h"
#include "nxg_core.h"
#include "nxg_sheets.h"

static const char *s_dir = ".";
static uint16_t s_us;
static uint16_t s_glass[240 * 320];

uint16_t nx_us16(void)
{
    return s_us++;   /* render slices end by budget sometimes; the pixels do not depend on it */
}

void nx_hw_bl_set(uint16_t on_us, uint16_t period_us)
{
    (void)on_us;
    (void)period_us;
}

void nxg_shot(unsigned id, const char *name)
{
    (void)id;
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", s_dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "nxg_host: cannot write %s\n", path);
        exit(1);
    }
    nxp_glass(s_glass);
    fprintf(f, "P6\n320 240\n255\n");
    for (unsigned i = 0; i < 320u * 240u; i++) {
        uint16_t v = s_glass[i];
        unsigned char rgb[3] = { (unsigned char)((v >> 11) * 255u / 31u), (unsigned char)(((v >> 5) & 63u) * 255u / 63u),
                                 (unsigned char)((v & 31u) * 255u / 31u) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    if (argc > 1)
        s_dir = argv[1];
    nxp_reset();
    nx_lcd_init();
#if NXG_SCREENS_ONLY
    nxg_screens_run();
    unsigned n = g_nxg_shots;
#else
    unsigned n = nxg_run();
#endif
    printf("nxg_host: %u shots in %s, protocol errors %u\n", n, s_dir, nxp_bad);
    return nxp_bad ? 1 : 0;
}
