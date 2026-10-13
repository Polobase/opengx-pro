/* The UI settings blob (src/nx/nx_uiset.c), the number formatting, the detail's summary line and
 * the waveform map (src/nx/nx_uifmt.c): pure functions. */
#include "harness.h"

#include "nx_assets.h"
#include "nx_font.h"
#include "nx_scope.h"
#include "nx_uifmt.h"
#include "nx_uiset.h"
#include "test_nx.h"

static void test_blob(void)
{
    struct nx_uiset u, v;
    uint8_t blob[NX_UISET_BYTES];

    /* a fresh settings record has an all-zero blob: version 0 -> the defaults */
    memset(blob, 0, sizeof blob);
    nx_uiset_from_blob(&u, blob);
    CHECK_EQ(u.ver, NX_UISET_VER);
    CHECK_EQ(u.theme, NX_THEME_DARK);
    CHECK_EQ(u.bright, 7);
    CHECK_EQ(u.dim, 2);
    CHECK_EQ(u.off, 2);
    CHECK_EQ(u.night, 0);
    CHECK_EQ(u.marquee, 1);
    CHECK_EQ(u.repeat, 1);
    CHECK_EQ(u.splash, NX_SPLASH_FULL);

    /* round trip, byte for byte in the documented layout */
    u.theme = NX_THEME_HC;
    u.bright = 3;
    u.night = 1;
    u.motion = 1;
    u.autolock = 1;
    u.splash = NX_SPLASH_SHORT;
    nx_uiset_to_blob(&u, blob);
    CHECK_EQ(blob[0], 2);
    CHECK_EQ(blob[1], NX_THEME_HC);
    CHECK_EQ(blob[2], 3);
    CHECK_EQ(blob[5], 1);
    CHECK_EQ(blob[6], 1);
    CHECK_EQ(blob[7], 1);
    CHECK_EQ(blob[8], 1);
    CHECK_EQ(blob[9], 1);
    CHECK_EQ(blob[10], NX_SPLASH_SHORT);
    nx_uiset_from_blob(&v, blob);
    CHECK(memcmp(&u, &v, sizeof u) == 0);

    /* out-of-range fields fall back to their default, the others stay */
    blob[1] = 9;      /* theme */
    blob[2] = 0;      /* brightness 0 % */
    blob[3] = 200;    /* dim index */
    blob[10] = 5;     /* splash */
    nx_uiset_from_blob(&v, blob);
    CHECK_EQ(v.theme, NX_THEME_DARK);
    CHECK_EQ(v.bright, 7);
    CHECK_EQ(v.dim, 2);
    CHECK_EQ(v.splash, NX_SPLASH_FULL);
    CHECK_EQ(v.night, 1);
    CHECK_EQ(v.autolock, 1);

    /* a blob of the development layout 1 (light theme, 30 %, and its "ambient" idle mode at byte
     * 5, where layout 2 keeps the night palette): all defaults, nothing misread */
    nx_uiset_defaults(&u);
    memset(blob, 0, sizeof blob);
    blob[0] = 1;
    blob[1] = NX_THEME_LIGHT;
    blob[2] = 3;
    blob[5] = 1;
    nx_uiset_from_blob(&v, blob);
    CHECK(memcmp(&u, &v, sizeof u) == 0);

    /* a blob of a later layout 3 with fields appended at bytes 14..15: the fields of layout 2
     * read (clamped, idle, guide and origkeys too); saved back, the version and the appended
     * fields are unchanged */
    uint8_t out[NX_UISET_BYTES];
    memset(blob, 0, sizeof blob);
    blob[0] = 3;
    blob[1] = NX_THEME_LIGHT;
    blob[2] = 3;
    blob[3] = 200;   /* out of range: the default */
    blob[4] = 1;
    blob[10] = NX_SPLASH_OFF;
    blob[11] = 0xA5;   /* idle: out of range, the default */
    blob[12] = 1;      /* guide seen */
    blob[13] = 0xA5;   /* origkeys: out of range, off */
    blob[14] = 0xA5;
    blob[15] = 0x5A;
    nx_uiset_from_blob(&v, blob);
    CHECK_EQ(v.idle, NX_IDLE_OFF);
    CHECK_EQ(v.guide, 1);
    CHECK_EQ(v.origkeys, 0);
    CHECK_EQ(v.theme, NX_THEME_LIGHT);
    CHECK_EQ(v.bright, 3);
    CHECK_EQ(v.dim, 2);
    CHECK_EQ(v.off, 1);
    CHECK_EQ(v.splash, NX_SPLASH_OFF);
    v.bright = 9;    /* changed on this firmware */
    nx_uiset_to_blob(&v, out);
    CHECK_EQ(out[0], 3);
    CHECK_EQ(out[1], NX_THEME_LIGHT);
    CHECK_EQ(out[2], 9);
    CHECK_EQ(out[3], 2);
    CHECK_EQ(out[12], 1);
    CHECK(memcmp(out + 14, blob + 14, NX_UISET_BYTES - 14u) == 0);

    /* brightness steps map to backlight levels (nx_bl applies the gamma) */
    u.bright = 10;
    CHECK_EQ(nx_uiset_level(&u), 255);
    u.bright = 1;
    CHECK_EQ(nx_uiset_level(&u), 25);
    CHECK_EQ(k_nx_dim_s[0], 0);
    CHECK_EQ(k_nx_off_s[NX_OFF_N - 1u], 1800);
}

static void test_format(void)
{
    char b[48];
    nx_fmt_u(b, 0);
    CHECK(strcmp(b, "0") == 0);
    nx_fmt_u(b, 4294967295u);
    CHECK(strcmp(b, "4294967295") == 0);
    nx_fmt_2d(b, 7);
    CHECK(strcmp(b, "07") == 0);
    nx_fmt_dur(b, 65);
    CHECK(strcmp(b, "1:05") == 0);
    nx_fmt_dur(b, 3600u + 52u * 60u + 10u);
    CHECK(strcmp(b, "1:52:10") == 0);
    nx_fmt_dur(b, 125u * 3600u);
    CHECK(strcmp(b, "125:00:00") == 0);
    nx_fmt_hex(b, 0xEF4015u, 6);
    CHECK(strcmp(b, "EF4015") == 0);
    nx_fmt_hex(b, 0xAu, 2);
    CHECK(strcmp(b, "0A") == 0);
    nx_fmt_cv(b, 2000);
    CHECK(strcmp(b, "20.0 V") == 0);
    nx_fmt_cv(b, 1234);   /* 12.34 V -> 12.3 V */
    CHECK(strcmp(b, "12.3 V") == 0);
    nx_fmt_cv(b, 5);
    CHECK(strcmp(b, "0.1 V") == 0);
    nx_fmt_pct(b, 70);
    CHECK(strcmp(b, "70 %") == 0);

    /* frequencies: w24 encoding m * 10 + e, Hz = m * 10^(e - 8) */
    nx_fmt_freq(b, 7278u, 3, 8, 0);             /* 727 Hz */
    CHECK(strcmp(b, "727.000") == 0);
    nx_fmt_freq(b, 1234566u, 2, 3, 0);          /* 1 234.56 Hz */
    CHECK(strcmp(b, "1 234.56") == 0);
    nx_fmt_freq(b, 1234566u, 0, 8, 0);
    CHECK(strcmp(b, "1 234.56") == 0);
    nx_fmt_freq(b, 12345678126u, 3, 8, 1);      /* 12 345 678.12 Hz, decimals grouped */
    CHECK(strcmp(b, "12 345 678.120") == 0);
    nx_fmt_freq(b, 43200000001u, 3, 8, 1);      /* 432.000 000 00 -> trailing zeros dropped */
    CHECK(strcmp(b, "432.000") == 0);
    nx_fmt_freq(b, 43212345671u, 3, 8, 1);      /* 432.1234567 Hz */
    CHECK(strcmp(b, "432.123 456 7") == 0);
    nx_fmt_freq(b, 41234567893u, 2, 3, 0);      /* 41 234.56789 Hz, cut after 3 decimals */
    CHECK(strcmp(b, "41 234.567") == 0);

    /* the PC monitor: the wire value, or with a table of several cycles the output frequency */
    nx_fmt_pc_freq(b, 846423193440u, 0);        /* 846.42319344 Hz */
    CHECK(strcmp(b, "846.42319344") == 0);
    nx_fmt_pc_freq(b, 846423193440u, 1);
    CHECK(strcmp(b, "846.42319344") == 0);
    nx_fmt_pc_freq(b, 846423193440u, 11);       /* x 11 = 9310.65512784 */
    CHECK(strcmp(b, "9 310.655128") == 0);
    nx_fmt_pc_freq(b, 9090909090u, 11);         /* 100 Hz / 11 rounded to 8 decimals, x 11 = 99.99999999 */
    CHECK(strcmp(b, "100.00") == 0);
    nx_fmt_pc_freq(b, 458u, 11);                /* 45 Hz x 11 */
    CHECK(strcmp(b, "495.00") == 0);
    nx_fmt_pc_freq(b, 454545454550u, 11);       /* 5000 Hz / 11 = 454.54545455: x 11 = 5000.00000005 */
    CHECK(strcmp(b, "5 000.00") == 0);
    nx_fmt_pc_freq(b, 4113333330u, 3);          /* 12.34 Hz / 3 = 4.11333333 */
    CHECK(strcmp(b, "12.34") == 0);
    nx_fmt_pc_freq(b, 9999999999999990u, 180);  /* 99 999 999.99999999 Hz in the most cycles: clamped like every frequency */
    CHECK(strcmp(b, "99 999 999.999999") == 0);
    nx_fmt_pc_freq(b, 1234567890u, 180);        /* 1.23456789 Hz x 180 = 222.2222202 */
    CHECK(strcmp(b, "222.22222") == 0);

    nx_fmt_delay(b, 0);
    CHECK(strcmp(b, "Never") == 0);
    nx_fmt_delay(b, 30);
    CHECK(strcmp(b, "30 s") == 0);
    nx_fmt_delay(b, 300);
    CHECK(strcmp(b, "5 min") == 0);

    /* the program detail's summary line, measured in its font (cap13) against its 296 px: a
     * usual program fits whole; a 200-step one with the longest waveform name keeps the name,
     * only the amplitude after it may be cut */
    char line[NX_FMT_SUMMARY_MAX];
    struct nx_slot_info si = { 1, 12, 41, 2000, 180 };
    nx_fmt_summary(line, &si);
    CHECK(strcmp(line, "41 steps \x88 3:00 per step \x88 Square \x88 20.0 V") == 0);
    CHECK(nx_font_text_width(&nx_font_cap13, line) <= 296u);
    for (uint32_t dwell = 180; dwell <= 3600u; dwell *= 10u) {   /* 3:00, 30:00 per step */
        si = (struct nx_slot_info){ 1, 14, 200, 2000, dwell };
        nx_fmt_summary(line, &si);
        const char *name = nx_str(NX_S_WAVE_INV_SAW), *at = strstr(line, name);
        CHECK(at != 0);
        CHECK(nx_font_text_width_n(&nx_font_cap13, line, (size_t)(at - line) + strlen(name)) <= 296u);
    }
    /* the longest fields stay inside the buffer; an unknown waveform by its code */
    si = (struct nx_slot_info){ 1, 255, 65535, 65535, 4294967295u };
    char *end = nx_fmt_summary(line, &si);
    CHECK((size_t)(end - line) < NX_FMT_SUMMARY_MAX);
    CHECK(strcmp(line, "65535 steps \x88 1193046:28:15 per step \x88 wave #255 ? \x88 655.4 V") == 0);
}

static void test_wavemap(void)
{
    struct nx_wave_info w;
    CHECK_EQ(nx_wave_info(0, &w), 1);            /* the empty program: nothing to name */
    CHECK_EQ(w.shape, NX_WAVE_FLAT);
    CHECK_EQ(w.name, NX_WAVE_NO_NAME);
    CHECK_EQ(nx_wave_info(11, &w), 1);           /* Spooky2 waveform 1 */
    CHECK_EQ(w.shape, NX_WAVE_SINE);
    CHECK_EQ(w.name, NX_S_WAVE_SINE);
    CHECK_EQ(nx_wave_info(12, &w), 1);
    CHECK_EQ(w.shape, NX_WAVE_SQUARE);
    CHECK_EQ(nx_wave_info(18, &w), 1);           /* waveform 8: Spooky2's Sine_Hbomb */
    CHECK_EQ(w.shape, NX_WAVE_HBOMB_SINE);
    CHECK_EQ(w.name, NX_S_WAVE_HBOMB_SINE);
    CHECK_EQ(nx_wave_info(19, &w), 1);           /* waveform 9: Square_Hbomb */
    CHECK_EQ(w.shape, NX_WAVE_HBOMB_EDGE);
    CHECK_EQ(w.name, NX_S_WAVE_HBOMB_SQUARE);
    CHECK_EQ(nx_wave_info(25, &w), 1);           /* Out2 inverse */
    CHECK_EQ(w.out2, NX_OUT2_INVERSE);
    CHECK_EQ(w.name, NX_S_OUT_INVERSE);
    for (unsigned code = 41; code <= 70u; code++)   /* the stored slots' tables: unknown shape */
        CHECK_EQ(nx_wave_info((uint8_t)code, &w), 0);
    CHECK_EQ(nx_wave_info(20, &w), 0);
    CHECK_EQ(w.name, NX_WAVE_NO_NAME);
    CHECK_EQ(nx_wave_info(255, &w), 0);
}

void test_nx_uiset(void)
{
    test_blob();
    test_format();
    test_wavemap();
}
