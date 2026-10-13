/* Chains (menu > Chains, NX_FEAT_CHAINS): up to NX_CHAINS lists of up to NX_CHAIN_ITEMS program
 * slots with a repeat count, played one after the other on one generator.
 *
 * Playing loads and plays the first slot (the lines of loading it and pressing START). When that
 * program ends by itself, the next one is loaded and played; after the last one the chain starts
 * again until it has run `repeat` times (0: until stopped). Stopping the generator, loading
 * another program, its "stop after" timer or PC mode end the chain. The chains are kept in the KV
 * store (NX_KEY_CHAINS), written like the settings (quiet, never in PC mode). A power cut keeps
 * the position in the resume snapshot (nx_resume.h), so a resumed program goes on with its chain. */
#pragma once

#include <stdint.h>

#define NX_KEY_CHAINS 0x0004u
#define NX_CHAINS 4u
#define NX_CHAIN_ITEMS 12u
#define NX_CHAIN_VER 1u

struct nx_chain {
    uint8_t n;                       /* items used */
    uint8_t repeat;                  /* runs of the whole chain, 0 = until stopped */
    uint8_t slot[NX_CHAIN_ITEMS];    /* 1..30 */
};

struct nx_chains_rec {
    uint8_t ver, rsv;
    struct nx_chain c[NX_CHAINS];
};

struct nx_chain_pos {
    uint8_t id;      /* 1..NX_CHAINS, 0 = no chain */
    uint8_t item;    /* 0-based */
    uint8_t pass;    /* completed runs of the whole chain */
    uint8_t plan;    /* 1..NX_PLANS: the chain is that plan's day, 0 = none */
};

extern struct nx_chains_rec g_nx_chains;

/* Multi-day plans (NX_FEAT_PLANS): a chain per day. Playing a plan plays the chain of its current
 * day; when that chain has run its repeats to the end, the plan moves to the next day (kept in the
 * KV store, NX_KEY_PLANS). A day's chain that is stopped does not count. After the last day the
 * plan is complete (day = n) until its editor starts it again at day 1. */
#define NX_KEY_PLANS 0x0006u
#define NX_PLANS 2u
#define NX_PLAN_DAYS 14u
struct nx_plan {
    uint8_t n;                    /* days */
    uint8_t day;                  /* 0-based current day; n = complete */
    uint8_t chain[NX_PLAN_DAYS];  /* 1..NX_CHAINS */
};
struct nx_plans_rec {
    uint8_t ver, rsv;
    struct nx_plan p[NX_PLANS];
};
extern struct nx_plans_rec g_nx_plans;
int nx_plan_play(unsigned ch, unsigned id);   /* NX_OK, NX_EINVAL (empty, complete, empty chain), refusal */

void nx_chain_boot(void);                         /* after nx_store_mount: loads the chains */
int nx_chain_play(unsigned ch, unsigned id);      /* NX_OK, NX_EINVAL (empty) or the core's refusal */
void nx_chain_finished(unsigned ch);              /* runner: ch's program ended by itself */
void nx_chain_task(uint32_t now);                 /* main loop, after the runner, before the timers */
void nx_chain_save(void);                         /* g_nx_chains changed: written when quiet */
void nx_chain_pos(unsigned ch, struct nx_chain_pos *p);
void nx_chain_resume(unsigned ch, const struct nx_chain_pos *p);   /* the resume snapshot's position */
