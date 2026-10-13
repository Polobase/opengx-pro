/* Key gesture engine (nx_keys.h). Runs in the SysTick handler; the queue is single-producer
 * (nx_keys_feed) / single-consumer (nx_keys_pop), so it needs no locking on the Cortex-M0+. */
#include "nx_keys.h"

#include <string.h>

#define KF_PRESSED 0x01u   /* PRESS reported (or the key belongs to a chord) */
#define KF_HOLD 0x02u
#define KF_LONG 0x04u
#define KF_PENDING 0x08u   /* PRESS delayed for chord detection */

#define DEFAULT_REPEAT (NX_KEY_UP | NX_KEY_DOWN)
#define DEFAULT_CHORD_DELAY (NX_KEY_G1 | NX_KEY_G2)
#define POWER_BIT 7u

struct key_state {
    uint32_t t_down;     /* debounced press */
    uint32_t next_rep;   /* next REPEAT */
    uint16_t rep;        /* REPEATs so far */
    uint8_t flags;       /* KF_* */
};

static struct key_state s_key[8];
static uint8_t s_raw_prev, s_down;
static uint8_t s_ignore;    /* keys down at the very first sample after reset: not a press until
                              * released once (a power-on press is usually still down when the app
                              * starts; docs/nx/design/hardware.md §"Power-on") */
static uint8_t s_seeded;    /* s_ignore has been captured from the first nx_keys_feed() call */
static uint8_t s_repeat_mask = DEFAULT_REPEAT, s_chord_delay_mask = DEFAULT_CHORD_DELAY;
static uint16_t s_rep_first = NX_KEY_REPEAT_FIRST_MS, s_rep_slow = NX_KEY_REPEAT_SLOW_MS;
static uint8_t s_chord;        /* keys of the current chord (for its events) */
static uint8_t s_chord_down;   /* chord keys still down; 0 = no chord */
static uint8_t s_chord_flags;  /* KF_HOLD, KF_LONG */
static uint32_t s_chord_t0;
static uint8_t s_power_off;
static struct nx_kev s_q[NX_KEY_QUEUE];
static volatile uint8_t s_head, s_tail;
static uint32_t s_dropped;

static void push(uint8_t type, uint8_t keys, uint32_t n, uint32_t t)
{
    uint8_t next = (uint8_t)((s_head + 1u) % NX_KEY_QUEUE);
    if (next == s_tail) {
        s_dropped++;
        return;
    }
    s_q[s_head].type = type;
    s_q[s_head].keys = keys;
    s_q[s_head].n = (uint16_t)(n > 0xFFFFu ? 0xFFFFu : n);
    s_q[s_head].t = t;
    s_head = next;
}

void nx_keys_reset(void)
{
    memset(s_key, 0, sizeof s_key);
    s_raw_prev = s_down = 0;
    s_ignore = s_seeded = 0;
    s_repeat_mask = DEFAULT_REPEAT;
    s_chord_delay_mask = DEFAULT_CHORD_DELAY;
    s_rep_first = NX_KEY_REPEAT_FIRST_MS;
    s_rep_slow = NX_KEY_REPEAT_SLOW_MS;
    s_chord = s_chord_down = s_chord_flags = 0;
    s_chord_t0 = 0;
    s_power_off = 0;
    s_head = s_tail = 0;
    s_dropped = 0;
}

void nx_keys_set_repeat(uint16_t first_ms, uint16_t slow_ms)
{
    s_rep_first = first_ms;
    s_rep_slow = slow_ms;
}

void nx_keys_config(uint8_t repeat_mask, uint8_t chord_delay_mask)
{
    s_repeat_mask = repeat_mask;
    s_chord_delay_mask = chord_delay_mask;
}

static void key_down(unsigned i, uint32_t now)
{
    uint8_t k = (uint8_t)(1u << i);
    struct key_state *s = &s_key[i];
    s->t_down = now;
    s->flags = 0;
    s->rep = 0;
    s->next_rep = now + s_rep_first;

    /* A chord: other keys that went down less than NX_KEY_CHORD_MS ago, or a young chord. */
    uint8_t partners = 0;
    for (unsigned j = 0; j < 8u; j++) {
        uint8_t kj = (uint8_t)(1u << j);
        if (j != i && (s_down & kj) && !(s_chord_down & kj) && !(s_ignore & kj)
            && now - s_key[j].t_down <= NX_KEY_CHORD_MS)
            partners |= kj;
    }
    if (s_chord_down && now - s_chord_t0 <= NX_KEY_CHORD_MS)
        partners |= s_chord_down;
    if (!partners) {
        if (k & s_chord_delay_mask) {
            s->flags = KF_PENDING;
        } else {
            s->flags = KF_PRESSED;
            push(NXK_PRESS, k, 0, now);
        }
        return;
    }
    if (!s_chord_down) {                   /* a new chord starts at its first key */
        s_chord = 0;
        s_chord_flags = 0;
        s_chord_t0 = now;
        for (unsigned j = 0; j < 8u; j++)
            if ((partners & (1u << j)) && s_key[j].t_down < s_chord_t0)
                s_chord_t0 = s_key[j].t_down;
    }
    s_chord |= (uint8_t)(partners | k);
    s_chord_down |= (uint8_t)(partners | k);
    for (unsigned j = 0; j < 8u; j++)
        if (s_chord_down & (1u << j))
            s_key[j].flags = KF_PRESSED;   /* a delayed PRESS is dropped: the chord replaces it */
    push(NXK_CHORD, s_chord, 0, now);
}

static void key_up(unsigned i, uint32_t now)
{
    uint8_t k = (uint8_t)(1u << i);
    struct key_state *s = &s_key[i];
    if (s_chord_down & k) {
        s_chord_down &= (uint8_t)~k;
        if (!s_chord_down)
            push(NXK_RELEASE, s_chord, now - s_chord_t0, now);
        s->flags = 0;
        return;
    }
    if (s->flags & KF_PENDING)             /* tapped faster than the chord delay */
        push(NXK_PRESS, k, 0, s->t_down);
    push(NXK_RELEASE, k, now - s->t_down, now);
    s->flags = 0;
}

static void timers(uint32_t now)
{
    for (unsigned i = 0; i < 8u; i++) {
        uint8_t k = (uint8_t)(1u << i);
        struct key_state *s = &s_key[i];
        if (!(s_down & k) || (s_chord_down & k) || (s_ignore & k))
            continue;
        uint32_t held = now - s->t_down;
        if (s->flags & KF_PENDING) {
            if (held < NX_KEY_CHORD_DELAY_MS)
                continue;
            s->flags = KF_PRESSED;
            push(NXK_PRESS, k, 0, s->t_down);
        }
        if (!(s->flags & KF_HOLD) && held >= NX_KEY_HOLD_MS) {
            s->flags |= KF_HOLD;
            push(NXK_HOLD, k, 0, s->t_down + NX_KEY_HOLD_MS);
        }
        if (!(s->flags & KF_LONG) && held >= NX_KEY_LONG_MS) {
            s->flags |= KF_LONG;
            push(NXK_LONG, k, 0, s->t_down + NX_KEY_LONG_MS);
        }
        if ((k & s_repeat_mask) && (int32_t)(now - s->next_rep) >= 0) {
            s->rep++;
            push(NXK_REPEAT, k, s->rep, s->next_rep);
            s->next_rep += held >= NX_KEY_REPEAT_ACCEL_MS && s_rep_slow > NX_KEY_REPEAT_FAST_MS ? NX_KEY_REPEAT_FAST_MS
                                                                                           : s_rep_slow;
            if ((int32_t)(now - s->next_rep) >= 0)   /* fell behind: no burst of repeats */
                s->next_rep = now + NX_KEY_REPEAT_FAST_MS;
        }
    }
    if (s_chord_down) {
        uint32_t held = now - s_chord_t0;
        if (!(s_chord_flags & KF_HOLD) && held >= NX_KEY_HOLD_MS) {
            s_chord_flags |= KF_HOLD;
            push(NXK_HOLD, s_chord, 0, s_chord_t0 + NX_KEY_HOLD_MS);
        }
        if (!(s_chord_flags & KF_LONG) && held >= NX_KEY_LONG_MS) {
            s_chord_flags |= KF_LONG;
            push(NXK_LONG, s_chord, 0, s_chord_t0 + NX_KEY_LONG_MS);
        }
    }
    if ((s_down & NX_KEY_POWER) && !(s_ignore & NX_KEY_POWER)
        && now - s_key[POWER_BIT].t_down >= NX_KEY_POWER_OFF_MS)
        s_power_off = 1;
}

void nx_keys_feed(uint8_t raw, uint32_t now)
{
    if (!s_seeded) {
        s_seeded = 1;
        /* A key already down at the very first sample was down before this engine started
         * sampling (power-on: POWER is the key that turns the board on, so it is usually still
         * down here); it must be released once before a hold counts as a fresh press. */
        s_ignore = raw;
    }
    /* Debounce: a key changes state once two consecutive samples agree on the new level. */
    uint8_t same = (uint8_t)~(raw ^ s_raw_prev);
    uint8_t down = (uint8_t)((s_down & ~same) | (raw & same));
    s_raw_prev = raw;
    uint8_t pressed = (uint8_t)(down & ~s_down), released = (uint8_t)(s_down & ~down);
    s_down = down;
    for (unsigned i = 0; i < 8u; i++)
        if (released & (1u << i)) {
            if (s_ignore & (1u << i))
                s_ignore &= (uint8_t)~(1u << i);   /* released once: future presses count normally */
            else
                key_up(i, now);
        }
    for (unsigned i = 0; i < 8u; i++)
        if ((pressed & (1u << i)) && !(s_ignore & (1u << i)))
            key_down(i, now);
    timers(now);
}

int nx_keys_pop(struct nx_kev *e)
{
    if (s_tail == s_head)
        return 0;
    *e = s_q[s_tail];
    s_tail = (uint8_t)((s_tail + 1u) % NX_KEY_QUEUE);
    return 1;
}

void nx_keys_flush(void)
{
    s_tail = s_head;
}

int nx_keys_pending(void)
{
    return s_tail != s_head;
}

uint8_t nx_keys_down(void)
{
    return s_down;
}

int nx_keys_power_off_due(void)
{
    return s_power_off;
}

uint32_t nx_keys_dropped(void)
{
    return s_dropped;
}

enum nx_kact nx_keys_act(const struct nx_kev *e, int has_hold)
{
    if (!has_hold)
        return (e->type == NXK_PRESS || e->type == NXK_REPEAT) ? NXKA_SHORT : NXKA_NONE;
    if (e->type == NXK_RELEASE && e->n < NX_KEY_HOLD_MS)
        return NXKA_SHORT;
    if (e->type == NXK_HOLD)
        return NXKA_HOLD;
    return NXKA_NONE;
}
