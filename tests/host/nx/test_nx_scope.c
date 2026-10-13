/* Waveform preview (src/nx/nx_scope.c): the shapes' pure samples, the phase clock, and the card
 * drawing: everything inside its rectangle, the Out2 relations, paused and stopped states. */
#include "harness.h"

#include "nx_anim.h"
#include "nx_scope.h"
#include "test_nx.h"

static void test_samples(void)
{
    CHECK_EQ(nx_wave_sample(NX_WAVE_SINE, NULL, 0), 0);
    CHECK_EQ(nx_wave_sample(NX_WAVE_SINE, NULL, 16384), 4096);
    CHECK_EQ(nx_wave_sample(NX_WAVE_SINE, NULL, 49152), -4096);
    CHECK_EQ(nx_wave_sample(NX_WAVE_SQUARE, NULL, 100), 4096);
    CHECK_EQ(nx_wave_sample(NX_WAVE_SQUARE, NULL, 40000), -4096);
    CHECK_EQ(nx_wave_sample(NX_WAVE_SAW, NULL, 0), -4096);
    CHECK_EQ(nx_wave_sample(NX_WAVE_SAW, NULL, 32768), 0);
    CHECK_EQ(nx_wave_sample(NX_WAVE_INV_SAW, NULL, 0), 4096);
    CHECK_EQ(nx_wave_sample(NX_WAVE_TRIANGLE, NULL, 0), -4096);
    CHECK_EQ(nx_wave_sample(NX_WAVE_TRIANGLE, NULL, 32768), 4096);
    CHECK_EQ(nx_wave_sample(NX_WAVE_FLAT, NULL, 12345), 0);
    static int8_t table[64];
    for (int i = 0; i < 64; i++)
        table[i] = (int8_t)(i < 32 ? 127 : -127);
    CHECK_EQ(nx_wave_sample(NX_WAVE_CUSTOM, table, 0), 4064);
    CHECK_EQ(nx_wave_sample(NX_WAVE_CUSTOM, table, 63u << 10), -4064);
    CHECK_EQ(nx_wave_sample(NX_WAVE_CUSTOM, NULL, 0), 0);
    int bounded = 1;
    for (uint8_t w = 0; w < NX_WAVE_COUNT; w++)
        for (uint32_t ph = 0; ph < 65536u; ph += 97u) {
            int32_t s = nx_wave_sample(w, table, ph);
            if (s < -4096 || s > 4096)
                bounded = 0;
        }
    CHECK(bounded);
    /* damped shapes decay over the period */
    int32_t early = 0, late = 0;
    for (uint32_t ph = 0; ph < 8192u; ph += 64u) {
        int32_t a = nx_wave_sample(NX_WAVE_DAMPED_SINE, NULL, ph), b = nx_wave_sample(NX_WAVE_DAMPED_SINE, NULL, ph + 49152u);
        early += a < 0 ? -a : a;
        late += b < 0 ? -b : b;
    }
    CHECK(early > 4 * late);
}

static void test_phase(void)
{
    nx_anim_set_reduced(0);
    CHECK_EQ(nx_scope_phase(0), 0);
    CHECK_EQ(nx_scope_phase(82), 0);                   /* 12 fps steps */
    CHECK_EQ(nx_scope_phase(83), 83u * 4096u / 125u);
    CHECK_EQ(nx_scope_phase(1000), 996u * 4096u / 125u);   /* half a cycle per second */
    CHECK_EQ(nx_scope_phase(2075), 75u * 4096u / 125u);    /* 2075 - 2075 % 83 = 2075 */
    nx_anim_set_reduced(1);
    CHECK_EQ(nx_scope_phase(1000), 0);
    nx_anim_set_reduced(0);
}

static uint16_t fb[240 * 320];

static void draw(const nx_scope_cfg *cfg, uint16_t phase)
{
    memset(fb, 0, sizeof fb);
    nx_gc gc = { fb, { 0, 0, 320, 240 }, { 0, 0, 320, 240 }, nx_palettes[0], 0, 0, 0, 0 };
    nx_scope_draw(&gc, (nx_rect){ 100, 50, 236, 102 }, cfg, phase);
}

static unsigned count(uint16_t c)
{
    unsigned n = 0;
    for (unsigned i = 0; i < 320u * 240u; i++)
        n += fb[i] == c;
    return n;
}

static void test_draw(void)
{
    const uint16_t *pal = nx_palettes[0];
    nx_scope_cfg cfg = { NX_WAVE_SINE, NX_WAVE_SINE, NX_OUT2_SYNC, NX_SCOPE_RUNNING, 0, NX_PAL_G1, 0, 0, 256, 2000,
                         NULL, NULL };
    draw(&cfg, 0);
    int inside = 1;
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < 320; x++)
            if (fb[y * 320 + x] && (x < 100 || x >= 236 || y < 50 || y >= 102))
                inside = 0;
    CHECK(inside);
    unsigned accent = count(pal[NX_PAL_G1]);
    CHECK(accent > 100);                               /* the 2 px trace */
    static uint16_t sync[240 * 320];
    memcpy(sync, fb, sizeof fb);

    cfg.out2 = NX_OUT2_INVERSE;                        /* the mirrored Out2 adds pixels */
    draw(&cfg, 0);
    CHECK(memcmp(sync, fb, sizeof fb) != 0);
    cfg.out2 = NX_OUT2_INDEPENDENT;
    cfg.ratio_q8 = 384;
    draw(&cfg, 0);
    CHECK(memcmp(sync, fb, sizeof fb) != 0);

    cfg.out2 = NX_OUT2_SYNC;
    draw(&cfg, 16384);                                 /* another phase moves the trace */
    CHECK(memcmp(sync, fb, sizeof fb) != 0);

    cfg.state = NX_SCOPE_PAUSED;                       /* paused: text2 instead of the accent */
    draw(&cfg, 0);
    CHECK_EQ(count(pal[NX_PAL_G1]), 0);
    CHECK(count(pal[NX_PAL_TEXT2]) > 100);

    cfg.state = NX_SCOPE_STOPPED;                      /* stopped: a flat line on the zero line */
    draw(&cfg, 0);
    unsigned rows_with_trace = 0;
    for (int y = 54; y < 98; y++) {
        int any = 0;
        for (int x = 110; x < 220; x++)
            any |= fb[y * 320 + x] == pal[NX_PAL_TEXT3];
        rows_with_trace += any;
    }
    CHECK(rows_with_trace <= 3);

    cfg.state = NX_SCOPE_RUNNING;
    cfg.gate_pct = 50;                                 /* gate bands in surface2 */
    draw(&cfg, 0);
    CHECK(count(pal[NX_PAL_SURFACE2]) > 200);
    cfg.unverified = 1;
    cfg.wave_code = 41;
    draw(&cfg, 0);
    CHECK(count(pal[NX_PAL_WARNING]) > 10);            /* "wave #41 ?" */
}

void test_nx_scope(void)
{
    test_samples();
    test_phase();
    test_draw();
}
