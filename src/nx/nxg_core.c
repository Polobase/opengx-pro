/* The gallery's core (nxg_core.h): the clock, the shots and nx_uicore.h from the fixture g_nxg. */
#include "nxg_core.h"

#include <string.h>

#include "nx_config.h"
#include "nx_lcd.h"
#include "nx_render.h"
#include "nx_time.h"
#include "nx_uiset.h"

volatile uint32_t g_nxg_ms;
unsigned g_nxg_shots;
struct nxg_fix g_nxg;

uint32_t nx_ms(void)
{
    return g_nxg_ms;
}

void nxg_settle(void)
{
    for (unsigned k = 0; k < 400u; k++) {
        nx_render_step(2000u);
        nx_lcd_sync();
        if (!nx_render_pending() && k >= 2u)
            break;
    }
}

void nxg_take(const char *name)
{
    nxg_settle();
    nxg_shot(g_nxg_shots++, name);
}

static const char *const k_names[30] = {
    "Detox", "Lyme Disease", "Pain Relief (general) - long program name", "Sleep", 0, "Immune Boost",
    "Detox Liver", "Energy", "Test Upload", 0, "Candida", "Parasites", 0, "Headache", "Allergy",
    "Circulation", 0, "Anxiety", "Muscle Relax", 0, "Back Pain", 0, "Digestion", "Skin", 0,
    "Cold and Flu", "Tinnitus", 0, "Focus", "Evening Wind-down",
};

void nxg_fix_defaults(void)
{
    memset(&g_nxg, 0, sizeof g_nxg);
    g_nxg.resume_on = 1;
    g_nxg.vm.s.ready = 1;
    g_nxg.vm.s.uptime_s = 4523;   /* 1:15:23: the goodbye's session line */
    for (unsigned i = 0; i < 30u; i++) {
        g_nxg.names[i + 1u] = k_names[i];
        struct nx_slot_info *s = &g_nxg.slots[i];
        if (!k_names[i])
            continue;
        s->used = 1;
        s->count = (uint16_t)(4u + (i * 37u) % 180u);
        s->dwell_s = i % 3u ? 180u : 60u;
        s->wave = (uint8_t)(i % 4u ? 11u + i % 9u : 41u + i % 30u);
        s->ampl_cv = 2000;
    }
    g_nxg.names[0] = "Biofeedback Scan (General)";
    g_nxg.names[31] = "Detox Liver";
    struct nx_gen_vm *g = &g_nxg.vm.g[0];
    g->state = NXG_RUNNING;
    g->slot = 2;
    g->loop = 0;
    g->wave = 11;
    g->step = 12;
    g->count = 180;
    g->ampl_cv = 2000;
    g->dwell_s = 180;
    g->total_s = 180u * 180u;
    g->left_s = g->total_s - 11u * 180u - 108u;
    g->step_left_s = 72;
    g->frac = (uint16_t)((uint32_t)(g->total_s - g->left_s) * 65535u / g->total_s);
    g->freq = 7278u;           /* 727 Hz (m * 10 + e: Hz = m * 10^(e - 8)) */
    g->next = 7288u;
    g = &g_nxg.vm.g[1];
    g->state = NXG_PAUSED;
    g->slot = 7;
    g->wave = 12;
    g->step = 3;
    g->count = 16;
    g->ampl_cv = 1000;
    g->dwell_s = 180;
    g->total_s = 16u * 180u;
    g->left_s = 13u * 180u + 120u;
    g->step_left_s = 120;
    g->frac = (uint16_t)((uint32_t)(g->total_s - g->left_s) * 65535u / g->total_s);
    g->freq = 1234566u;        /* 1 234.56 Hz */
    g->next = 15008u;
    struct nx_sys_vm *s = &g_nxg.vm.s;
    s->name_sum[0] = 2;
    s->name_sum[1] = 7;
    s->pc_out = 0x0Bu;         /* G1 Out1 + Out2, G2 Out1 */
    s->pc_sync = 0;
    s->pc_freq[0][0] = 41234567893u;   /* 41 234.56789 Hz */
    s->pc_freq[0][1] = 41234567893u;
    s->pc_freq[1][0] = 7278u;
    s->pc_freq[1][1] = 7288u;
    s->cal_index = 5;
    for (unsigned i = 0; i < 29u; i++)
        g_nxg.cal[i] = (int16_t)(i >= 21u ? 31990 + (i % 2u) * 20 : i ? 1234 + i * 111 : 32000);
    g_nxg.panel = 'A';
    g_nxg.presets = 1;   /* as the released image: one built-in preset */
    struct nx_uiset u;
    nx_uiset_defaults(&u);
    u.dim = 0;                /* never: the gallery's virtual time must not dim a shot */
    u.splash = NX_SPLASH_OFF;
    u.guide = 1;   /* the key guide was seen: the scenes start on the dashboard */
    nx_uiset_to_blob(&u, g_nxg.blob);
}

void nx_uc_vm(struct nx_vm *vm)
{
    *vm = g_nxg.vm;
}

int nx_uc_act(const struct nx_action *a)
{
    (void)a;
    g_nxg.acts++;
    return NX_OK;
}

unsigned nx_uc_name(unsigned slot, char *out)
{
    const char *s = slot < 32u ? g_nxg.names[slot] : slot == NX_UC_PRESET ? "Radionics (Boost) Manifestation"
        : slot == NXG_SLOT_TSET ? "Remove Intestinal Toxins" : 0;
    unsigned n = 0;
    for (; s && s[n] && n < NX_UC_NAME_MAX; n++)
        out[n] = s[n];
    out[n] = 0;
    return n;
}

int nx_uc_slot_info(unsigned slot, struct nx_slot_info *out)
{
    memset(out, 0, sizeof *out);
    if (slot >= 1u && slot <= 30u)
        *out = g_nxg.slots[slot - 1u];
    return out->used;
}

unsigned nx_uc_slot_freqs(unsigned slot, unsigned first, uint64_t *out, unsigned n)
{
    for (unsigned i = 0; i < n; i++)   /* a made-up program: 432 Hz, 727 Hz, 728 Hz, ... */
        out[i] = (uint64_t)(432000u + (slot * 1000u + (first + i) * 73730u) % 30000000u) * 10u + 5u;
    return n;
}

int nx_uc_cal_key(const struct nx_kev *e)
{
    (void)e;
    return 0;
}

int32_t nx_uc_cal_value(unsigned idx)
{
    return idx < 29u ? g_nxg.cal[idx] : 0;
}

uint8_t *nx_uc_settings_blob(void)
{
    return g_nxg.blob;
}

int nx_uc_settings_save(void)
{
    return NX_OK;
}

int nx_uc_quiet(uint32_t need_ms)
{
    (void)need_ms;
    return 1;
}

unsigned nx_uc_resume_offer(void)
{
    return g_nxg.resume_offer;
}

void nx_uc_resume_answer(int resume)
{
    (void)resume;
    g_nxg.resume_offer = 0;
}

int nx_uc_resume_auto(void)
{
    return g_nxg.resume_auto;
}

int nx_uc_resume_on(void)
{
    return g_nxg.resume_on;
}

void nx_uc_resume_set(int on)
{
    g_nxg.resume_on = (uint8_t)on;
}

char nx_uc_panel(void)
{
    return g_nxg.panel;
}

void nx_uc_diag(struct nx_diag_vm *d)   /* a unit after an hour: one power-on, both links fine */
{
    memset(d, 0, sizeof *d);
    d->uptime_s = 3725u;
    d->boots = 1u;
    d->reset_reason = 1u;   /* NX_RST_POWER */
    d->store_state = 2u;    /* NX_STORE_OK */
    d->panel = g_nxg.panel;
    d->jedec[0] = 0xEF;
    d->jedec[1] = 0x40;
    d->jedec[2] = 0x15;
    d->sensors = 1u;
    d->t10 = 312;
    d->vdd_mv = 3301u;
    d->sent[0] = 1284u;
    d->sent[1] = 1190u;
    d->lat_avg_us[0] = 1840u;
    d->lat_avg_us[1] = 1790u;
    d->pc_entries = 2u;
    d->pc_exits = 2u;
    d->kv_free = 3712u;
    d->loop_gap_ms = 11u;
}

static uint16_t s_opt[NX_OPT_N];

unsigned nx_uc_opt(unsigned id)
{
    return id < NX_OPT_N ? s_opt[id] : 0u;
}

void nx_uc_opt_set(unsigned id, unsigned v)
{
    if (id < NX_OPT_N)
        s_opt[id] = (uint16_t)v;
}

/* History: a few days of use */
static const struct {
    uint8_t ch, slot, end;
    uint16_t session;
    uint32_t start_s, run_s;
    const char *name;
} k_hist[] = {
    { 0, 2, NX_HEND_FINISHED, 14, 65u * 60u, 45u * 60u, "Lyme Disease" },
    { 1, 7, NX_HEND_TIMER, 14, 62u * 60u, 30u * 60u, "Detox Liver" },
    { 0, 5, NX_HEND_STOPPED, 13, 12u * 60u, 12u * 60u + 40u, "Immune Boost" },
    { 0, 2, NX_HEND_CUT, 12, 0, 21u * 60u, "Lyme Disease" },
    { 1, 11, NX_HEND_PC, 11, 5u * 60u, 3u * 60u + 5u, "Parasites General" },
};

unsigned nx_uc_hist_count(void)
{
    return sizeof k_hist / sizeof k_hist[0];
}

int nx_uc_hist_get(unsigned i, struct nx_hist_rec *r)
{
    if (i >= nx_uc_hist_count())
        return 0;
    memset(r, 0, sizeof *r);
    r->seq = 100u - i;
    r->ch = k_hist[i].ch;
    r->slot = k_hist[i].slot;
    r->end = k_hist[i].end;
    r->session = k_hist[i].session;
    r->start_s = k_hist[i].start_s;
    r->run_s = k_hist[i].run_s;
    strncpy(r->name, k_hist[i].name, sizeof r->name);
    return 1;
}

void nx_uc_hist_totals(struct nx_totals *t)
{
    memset(t, 0, sizeof *t);
    t->sessions = 14;
    t->runs = 57;
    t->run_s = 38u * 3600u + 25u * 60u;
}

int nx_uc_slot_op(uint8_t op, unsigned from, unsigned to)
{
    (void)op;
    (void)from;
    (void)to;
    return NX_OK;
}

int nx_uc_slot_used(unsigned slot)
{
    return slot >= 1u && slot <= 30u && g_nxg.slots[slot - 1u].used;
}

static struct nx_chain s_chains[NX_CHAINS] = {
    { 3, 2, { 2, 7, 5 } },
    { 5, 0, { 11, 12, 13, 14, 15 } },
};

struct nx_chain *nx_uc_chains(void)
{
    return s_chains;
}

void nx_uc_chains_save(void)
{
}

int nx_uc_chain_play(unsigned ch, unsigned id)
{
    (void)ch;
    (void)id;
    g_nxg.acts++;
    return NX_OK;
}

#if NX_FEAT_MANUAL
static struct nx_manual s_manual = { NX_MANUAL_VER, NX_MAN_SWEEP, 1, 1, NX_SW_PINGPONG, 0, 50, 100000u, 10000u, 100000u, 60u };

struct nx_manual *nx_uc_manual(void)
{
    return &s_manual;
}

void nx_uc_manual_changed(void)
{
}

int nx_uc_manual_play(unsigned ch)
{
    (void)ch;
    g_nxg.acts++;
    return NX_OK;
}
#endif

#if NX_FEAT_BFB
static struct nx_bfb_cfg s_bfb_cfg = { 0, 30, 2000u, 180u };
static struct nx_bfb_view s_bfb_view;

struct nx_bfb_cfg *nx_uc_bfb_cfg(void)
{
    return &s_bfb_cfg;
}

int nx_uc_bfb_start(void)
{
    g_nxg.acts++;
    return NX_OK;
}

void nx_uc_bfb_stop(void)
{
}

const struct nx_bfb_view *nx_uc_bfb(void)
{
    return &s_bfb_view;
}
#endif

#if NX_FEAT_TERRAIN
/* The six protocols as the built-in table has them (src/nx/data/nx_terrain_data.c): names, days,
 * items and the set at each position. The positions are made up: the first two some days in, the
 * others at their start. */
#define NXG_TERRAINS 6u
static const struct nx_tview k_tview[NXG_TERRAINS] = {
    { "Terrain", "Remove Intestinal Toxins", NX_TP_REMOTE, 11, 15, 5, 6, 1, 12, 0, 0, 15180 },
    { "Terrain", "Remove Chemical Materials", NX_TP_CONTACT, 11, 22, 5, 3, 2, 0, 0, 0, 0 },
    { "Terrain-Mercury", "Remove Metals Minus Mercury", NX_TP_REMOTE, 11, 15, 0, 1, 0, 24, 0, 0, 0 },
    { "Terrain-Mercury", "Remove Metals Minus Mercury", NX_TP_CONTACT, 11, 22, 0, 1, 1, 0, 0, 0, 0 },
    { "Terrain-Nonviolent", "Remove Metals", NX_TP_REMOTE, 10, 13, 0, 1, 0, 24, 0, 0, 0 },
    { "Terrain-Nonviolent", "Remove Metals", NX_TP_CONTACT, 10, 20, 0, 1, 1, 0, 0, 0, 0 },
};

unsigned nx_uc_terrain_count(void)
{
    return NXG_TERRAINS;
}

int nx_uc_terrain_view(unsigned id, struct nx_tview *v)
{
    if (id < 1u || id > NXG_TERRAINS)
        return 0;
    *v = k_tview[id - 1u];
    for (unsigned ch = 0; ch < 2u; ch++)
        if (g_nxg.vm.g[ch].terrain == id)
            v->playing = (uint8_t)(v->playing | (1u << ch));
    return 1;
}

int nx_uc_terrain_play(unsigned ch, unsigned id)
{
    (void)ch;
    (void)id;
    g_nxg.acts++;
    return NX_OK;
}

void nx_uc_terrain_move(unsigned id, int delta)
{
    (void)id;
    (void)delta;
}

void nx_uc_terrain_power(unsigned id)
{
    (void)id;
}
#endif

#if NX_FEAT_PRESET
unsigned nx_uc_preset_count(void)   /* the built-in preset's name and shape (src/nx/data/nx_preset_data.c) */
{
    return g_nxg.presets;
}

unsigned nx_uc_preset_info(unsigned n)
{
    return n < g_nxg.presets ? 18u | NX_UC_PRESET_LOOP : 0u;
}
#endif

static struct nx_plan s_plans[NX_PLANS] = {
    { 5, 2, { 1, 2, 1, 2, 1 } },
};

struct nx_plan *nx_uc_plans(void)
{
    return s_plans;
}

int nx_uc_plan_play(unsigned ch, unsigned id)
{
    (void)ch;
    (void)id;
    g_nxg.acts++;
    return NX_OK;
}
