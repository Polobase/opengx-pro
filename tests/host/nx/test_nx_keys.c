/* Key gesture engine (src/nx/nx_keys.c), fed directly like the SysTick handler does. */
#include "harness.h"

#include "nx_keys.h"
#include "test_nx.h"

static uint32_t t;

/* Feeds `raw` every 10 ms for ms milliseconds. */
static void hold(uint8_t raw, uint32_t ms)
{
    for (uint32_t end = t + ms; t < end; t += 10u)
        nx_keys_feed(raw, t);
}

static int next(struct nx_kev *e)
{
    return nx_keys_pop(e);
}

static unsigned count(uint8_t type, uint8_t keys)
{
    struct nx_kev e;
    unsigned n = 0;
    while (next(&e))
        if (e.type == type && e.keys == keys)
            n++;
    return n;
}

void test_nx_keys(void)
{
    struct nx_kev e;

    /* debounce: a level counts on the second equal sample; a one-sample glitch is ignored */
    nx_keys_reset();
    t = 0;
    hold(0, 20);
    nx_keys_feed(NX_KEY_START, t);
    t += 10;
    nx_keys_feed(0, t);
    t += 10;
    CHECK_EQ(nx_keys_pending(), 0);
    CHECK_EQ(nx_keys_down(), 0);
    hold(NX_KEY_START, 20);
    CHECK(next(&e));
    CHECK_EQ(e.type, NXK_PRESS);
    CHECK_EQ(e.keys, NX_KEY_START);
    CHECK_EQ(e.t, 50);                        /* the second sample, 10 ms after the first */
    CHECK_EQ(nx_keys_down(), NX_KEY_START);
    hold(NX_KEY_START, 200);
    hold(0, 20);
    CHECK(next(&e));
    CHECK_EQ(e.type, NXK_RELEASE);
    CHECK_EQ(e.n, 220);
    CHECK(!next(&e));

    /* HOLD at 500 ms, LONG at 1500 ms, stamped with their due time */
    nx_keys_reset();
    t = 0;
    nx_keys_feed(0, t);                       /* boot-ignore seed (nx_keys.h): an idle first sample */
    t += 10;
    hold(NX_KEY_STOP, 1600);
    CHECK(next(&e) && e.type == NXK_PRESS);
    CHECK(next(&e) && e.type == NXK_HOLD && e.t == 520u);
    CHECK(next(&e) && e.type == NXK_LONG && e.t == 1520u);
    CHECK(!next(&e));
    hold(0, 20);
    CHECK(next(&e) && e.type == NXK_RELEASE && e.n == 1600u);

    /* REPEAT of UP (default repeat mask): 400 ms, then every 110 ms, from 1.5 s every 50 ms */
    nx_keys_reset();
    t = 0;
    nx_keys_feed(0, t);                       /* boot-ignore seed (nx_keys.h): an idle first sample */
    t += 10;
    uint32_t times[64];
    unsigned n = 0;
    for (; t < 3000u; t += 10u) {             /* popped as the main loop would */
        nx_keys_feed(NX_KEY_UP, t);
        while (next(&e))
            if (e.type == NXK_REPEAT && n < 64u)
                times[n++] = e.t;
    }
    CHECK_EQ(times[0], 20u + 400u);
    CHECK_EQ(times[1] - times[0], 110);
    CHECK_EQ(times[2] - times[1], 110);
    int fast = 0;
    for (unsigned i = 1; i < n; i++)
        if (times[i] - times[i - 1] == 50u)
            fast++;
    CHECK(fast >= 20);
    CHECK_EQ(times[n - 1] <= 3000u, 1);
    /* STOP does not repeat by default */
    nx_keys_reset();
    t = 0;
    nx_keys_feed(0, t);                       /* boot-ignore seed (nx_keys.h): an idle first sample */
    t += 10;
    hold(NX_KEY_STOP, 1000);
    CHECK_EQ(count(NXK_REPEAT, NX_KEY_STOP), 0);

    /* G1/G2 wait 60 ms for a chord: G1 then G2 40 ms later is one CHORD, no PRESS */
    nx_keys_reset();
    t = 0;
    nx_keys_feed(0, t);                       /* boot-ignore seed (nx_keys.h): an idle first sample */
    t += 10;
    hold(NX_KEY_G1, 40);
    hold(NX_KEY_G1 | NX_KEY_G2, 1600);
    CHECK(next(&e) && e.type == NXK_CHORD && e.keys == (NX_KEY_G1 | NX_KEY_G2));
    CHECK(next(&e) && e.type == NXK_HOLD && e.keys == (NX_KEY_G1 | NX_KEY_G2));
    CHECK(next(&e) && e.type == NXK_LONG && e.keys == (NX_KEY_G1 | NX_KEY_G2) && e.t == 20u + 1500u);
    CHECK(!next(&e));
    hold(NX_KEY_G2, 20);                      /* the chord ends when its last key is up */
    CHECK(!next(&e));
    hold(0, 20);
    CHECK(next(&e) && e.type == NXK_RELEASE && e.keys == (NX_KEY_G1 | NX_KEY_G2));
    CHECK(!next(&e));
    /* G1 alone: PRESS 60 ms after the key went down, stamped with the down time */
    nx_keys_reset();
    t = 0;
    nx_keys_feed(0, t);                       /* boot-ignore seed (nx_keys.h): an idle first sample */
    t += 10;
    hold(NX_KEY_G1, 50);
    CHECK(!next(&e));
    hold(NX_KEY_G1, 30);
    CHECK(next(&e) && e.type == NXK_PRESS && e.keys == NX_KEY_G1 && e.t == 20u);
    /* a tap shorter than the chord delay still reports PRESS (at release), then RELEASE */
    nx_keys_reset();
    t = 0;
    nx_keys_feed(0, t);                       /* boot-ignore seed (nx_keys.h): an idle first sample */
    t += 10;
    hold(NX_KEY_G2, 30);
    hold(0, 20);
    CHECK(next(&e) && e.type == NXK_PRESS && e.keys == NX_KEY_G2);
    CHECK(next(&e) && e.type == NXK_RELEASE && e.keys == NX_KEY_G2);
    /* without the chord delay (e.g. the calibration menu) G1 reports PRESS at once */
    nx_keys_reset();
    nx_keys_config(NX_KEY_G1 | NX_KEY_G2, 0);
    t = 0;
    nx_keys_feed(0, t);                       /* boot-ignore seed (nx_keys.h): an idle first sample */
    t += 10;
    hold(NX_KEY_G1, 20);
    CHECK(next(&e) && e.type == NXK_PRESS && e.keys == NX_KEY_G1 && e.t == 20u);
    hold(NX_KEY_G1, 500);
    CHECK(count(NXK_REPEAT, NX_KEY_G1) >= 1u);

    /* START then STOP within 80 ms: PRESS START, then the chord (START+STOP owns hold/long) */
    nx_keys_reset();
    t = 0;
    nx_keys_feed(0, t);                       /* boot-ignore seed (nx_keys.h): an idle first sample */
    t += 10;
    hold(NX_KEY_START, 30);
    hold(NX_KEY_START | NX_KEY_STOP, 1600);
    CHECK(next(&e) && e.type == NXK_PRESS && e.keys == NX_KEY_START);
    CHECK(next(&e) && e.type == NXK_CHORD && e.keys == (NX_KEY_START | NX_KEY_STOP));
    CHECK(next(&e) && e.type == NXK_HOLD && e.keys == (NX_KEY_START | NX_KEY_STOP));
    CHECK(next(&e) && e.type == NXK_LONG && e.keys == (NX_KEY_START | NX_KEY_STOP));
    CHECK(!next(&e));
    /* a second key 200 ms later is not a chord */
    nx_keys_reset();
    t = 0;
    nx_keys_feed(0, t);                       /* boot-ignore seed (nx_keys.h): an idle first sample */
    t += 10;
    hold(NX_KEY_UP, 200);
    hold(NX_KEY_UP | NX_KEY_START, 20);
    CHECK_EQ(count(NXK_CHORD, NX_KEY_UP | NX_KEY_START), 0);

    /* POWER already down at the very first sample after reset (power-on: POWER is the key that
     * turns the board on, so it is usually still down here) is ignored until released once:
     * holding it continuously from reset never sets the power-off request
     * (docs/nx/design/hardware.md "Power-on"). */
    nx_keys_reset();
    t = 0;
    hold(NX_KEY_POWER, 2000);
    CHECK_EQ(nx_keys_power_off_due(), 0);
    /* released once: a later, fresh hold counts normally -- 800 ms: power-off request, sticky; not before */
    hold(0, 50);
    CHECK_EQ(nx_keys_power_off_due(), 0);
    hold(NX_KEY_POWER, 800);
    CHECK_EQ(nx_keys_power_off_due(), 0);
    hold(NX_KEY_POWER, 20);
    CHECK_EQ(nx_keys_power_off_due(), 1);
    hold(0, 50);
    CHECK_EQ(nx_keys_power_off_due(), 1);
    /* also inside a chord, once released and pressed again */
    nx_keys_reset();
    t = 0;
    nx_keys_feed(0, t);                       /* boot-ignore seed (nx_keys.h): an idle first sample */
    t += 10;
    hold(NX_KEY_POWER | NX_KEY_STOP, 900);
    CHECK_EQ(nx_keys_power_off_due(), 1);

    /* the act-on rule */
    struct nx_kev press = { NXK_PRESS, NX_KEY_STOP, 0, 0 }, rep = { NXK_REPEAT, NX_KEY_UP, 1, 0 };
    struct nx_kev tap = { NXK_RELEASE, NX_KEY_STOP, 499, 0 }, slow = { NXK_RELEASE, NX_KEY_STOP, 500, 0 };
    struct nx_kev held = { NXK_HOLD, NX_KEY_STOP, 0, 0 };
    CHECK_EQ(nx_keys_act(&press, 0), NXKA_SHORT);
    CHECK_EQ(nx_keys_act(&rep, 0), NXKA_SHORT);
    CHECK_EQ(nx_keys_act(&tap, 0), NXKA_NONE);
    CHECK_EQ(nx_keys_act(&press, 1), NXKA_NONE);
    CHECK_EQ(nx_keys_act(&tap, 1), NXKA_SHORT);
    CHECK_EQ(nx_keys_act(&slow, 1), NXKA_NONE);
    CHECK_EQ(nx_keys_act(&held, 1), NXKA_HOLD);

    /* a full queue drops the newest events and counts them */
    nx_keys_reset();
    t = 0;
    nx_keys_feed(0, t);                       /* boot-ignore seed (nx_keys.h): an idle first sample */
    t += 10;
    for (int i = 0; i < 12; i++) {
        hold(NX_KEY_START, 20);
        hold(0, 20);
    }
    CHECK_EQ(nx_keys_dropped(), 24u - (NX_KEY_QUEUE - 1u));
    nx_keys_flush();
    CHECK_EQ(nx_keys_pending(), 0);
}
