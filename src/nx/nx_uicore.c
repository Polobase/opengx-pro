/* The UI's door to the real core (nx_uicore.h): the snapshot from g_ch[], the flags, the
 * runner's times and the queues' health, generator actions through nx_core_act, slot headers
 * and frequencies through storage_slot_read (never through g_ch, which doubles as the upload
 * buffer), the calibration menu, the settings record and the panel variant. */
#include "nx_uicore.h"

#include <string.h>

#include "link_internal.h"
#include "nx_config.h"
#include "nx_chain.h"
#include "nx_diag.h"
#include "nx_fault.h"
#include "nx_hw.h"
#include "nx_kv.h"
#include "nx_preset.h"
#include "nx_slots.h"
#include "nx_store.h"
#include "nx_timer.h"
#include "nx_resume.h"
#include "nx_runner.h"
#include "nx_settings.h"
#include "nx_src.h"
#include "nx_terrain.h"
#include "nx_time.h"
#include "nx_txq.h"
#include "spiflash.h"
#include "state.h"

static uint16_t name_sum(unsigned slot)
{
    if (slot >= NAME_SLOTS)   /* a source's name (nx_src.h) changes with the source */
        return (uint16_t)slot;
    uint16_t h = 0x1D0Fu;
    for (unsigned i = 0; i < NAME_LEN; i++)
        h = (uint16_t)((h << 5) + h + (uint8_t)g_names[slot][i]);
    return h;
}

static uint16_t clamp16(uint64_t v)
{
    return v > 0xFFFFu ? 0xFFFFu : (uint16_t)v;
}

static void gen_vm(unsigned ch, struct nx_gen_vm *g)
{
    const struct channel *c = &g_ch[ch];
    struct nx_run_times t;
    nx_runner_times(ch, &t);
    memset(g, 0, sizeof *g);
    g->slot = c->prog_slot;
    g->loop = (g_ui_flags & UIF_LOOP(ch)) ? 1u : 0u;
    g->link_down = (uint8_t)nx_txq_link_down(ch);
    g->wave = c->prog.hdr[HDR_WAVE] > 255u ? 255u : (uint8_t)c->prog.hdr[HDR_WAVE];
    g->ampl_cv = clamp16(c->prog.hdr[HDR_AMPL]);
    g->count = (uint16_t)t.count;
    g->step = (uint16_t)t.step;
    g->dwell_s = t.dwell_ms / 1000u;
    g->total_s = t.total_ms / 1000u;
    g->left_s = (t.left_ms + 999u) / 1000u;   /* 0 only at the very end */
    g->step_left_s = (t.step_left_ms + 999u) / 1000u;
    g->frac = t.total_ms ? (uint16_t)((uint64_t)t.elapsed_ms * 65535u / t.total_ms) : 0u;
    if (t.step >= 1u) {
        g->freq = c->prog.freq[t.step - 1u];
        if (t.step < t.count)
            g->next = c->prog.freq[t.step];
        else if (g->loop)
            g->next = c->prog.freq[0];
    }
#if NX_FEAT_PRESET
    nx_preset_shown(ch, &t, &g->freq, &g->next);   /* a preset: the output frequency, not the wire's */
#endif
    g->timer_min = (uint16_t)((nx_timer_left_ms(ch) + 59999u) / 60000u);
#if NX_FEAT_CHAINS
    struct nx_chain_pos cp;
    nx_chain_pos(ch, &cp);
    if (cp.id) {
        g->chain = cp.id;
        g->chain_item = (uint8_t)(cp.item + 1u);
        g->chain_n = g_nx_chains.c[cp.id - 1u].n;
#if NX_FEAT_PLANS
        if (cp.plan) {
            g->plan = cp.plan;
            g->plan_day = (uint8_t)(g_nx_plans.p[cp.plan - 1u].day + 1u);
            g->plan_n = g_nx_plans.p[cp.plan - 1u].n;
        }
#endif
    }
#endif
#if NX_FEAT_TERRAIN
    struct nx_trun tr;
    nx_terrain_run(ch, &tr);
    if (tr.id) {
        uint8_t part, hours;
        g->terrain = tr.id;
        nx_terrain_where(tr.id, tr.item, &g->t_day, &part, &hours);
        g->t_days = nx_terrain_proto(tr.id)->days;
    }
#endif
    if (g_ui_flags & UIF_RUN(ch))
        g->state = NXG_RUNNING;
    else if (!c->prog_slot)
        g->state = NXG_NONE;
    else {
        g->state = c->elapsed > 1u ? NXG_PAUSED : NXG_READY;   /* 1 = stopped (state.h) */
        g->stopped = c->elapsed == 1u;
    }
}

void nx_uc_vm(struct nx_vm *vm)
{
    gen_vm(0, &vm->g[0]);
    gen_vm(1, &vm->g[1]);
    struct nx_sys_vm *s = &vm->s;
    s->ready = (uint8_t)nx_core_ready();
    s->pc = g_ui_state == ST_PC;
    s->cal = g_ui_state == ST_CAL;
    s->pc_out = (uint8_t)((g_misc_flags >> 1) & 0x0Fu);
    s->pc_sync = (uint8_t)(((g_ui_flags & UIF_SYNC_G1) ? 1u : 0u) | ((g_ui_flags & UIF_SYNC_G2) ? 2u : 0u));
    s->cal_index = g_cal_index;
    s->cal_fexp = g_cal_freq_exp;
    s->cal_aexp = g_cal_ampl_exp;
    uint16_t sum = 0;
    for (unsigned i = 0; i < 29u; i++)
        sum = (uint16_t)(sum * 31u + (uint16_t)g_cal_table[i]);
    s->cal_sum = sum;
    s->name_sum[0] = g_ch[0].prog_slot ? name_sum(g_ch[0].prog_slot) : 0u;
    s->name_sum[1] = g_ch[1].prog_slot ? name_sum(g_ch[1].prog_slot) : 0u;
    s->name_sum[2] = name_sum(0);
    s->name_sum[3] = name_sum(NAME_SLOTS - 1u);
    s->uptime_s = nx_ms() / 1000u;
    /* The receive interrupt writes these: a frequency is two words, so it is read with the
     * interrupts masked (a value of 2^32 or more could show half old, half new otherwise). */
#if defined(__arm__)
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
#endif
    for (unsigned ch = 0; ch < 2u; ch++)
        for (unsigned out = 0; out < 2u; out++)
            s->pc_freq[ch][out] = g_ch[ch].pc_freq[out];
#if defined(__arm__)
    __set_PRIMASK(primask);
#endif
    memcpy(s->pc_wave, g_nx_pcmon.wave, sizeof s->pc_wave);
    memcpy(s->pc_ampl, g_nx_pcmon.ampl_cv, sizeof s->pc_ampl);
    s->pc_gate = g_nx_pcmon.gate;
    s->pc_seen[0] = g_nx_pcmon.seen[0];
    s->pc_seen[1] = g_nx_pcmon.seen[1];
    for (unsigned ch = 0; ch < 2u; ch++)
        for (unsigned out = 0; out < 2u; out++) {
            unsigned t = (unsigned)s->pc_wave[ch][out] - NX_PCMON_TAB0;
            s->pc_mult[ch][out] = t < NX_PCMON_TABS ? g_nx_pcmon.cycles[ch][t] : 0u;
        }
}

int nx_uc_act(const struct nx_action *a)
{
    return nx_core_act(a);
}

unsigned nx_uc_name(unsigned slot, char *out)
{
    unsigned n = 0;
    if (slot >= NAME_SLOTS)
        return nx_src_name(slot, out);
    {
        for (; n < NAME_LEN && g_names[slot][n]; n++) {
            uint8_t c = (uint8_t)g_names[slot][n];
            out[n] = (char)(c >= 0x20u && c < 0x7Fu ? c : '?');   /* the fonts' ASCII only */
        }
    }
    out[n] = 0;
    return n;
}

int nx_uc_slot_info(unsigned slot, struct nx_slot_info *out)
{
    uint64_t hdr[5];
    memset(out, 0, sizeof *out);
    if (slot < 1u || slot > PROGRAM_SLOTS)
        return 0;
    storage_slot_read(slot, 0, hdr, sizeof hdr);
    /* program_load's test: an erased or foreign slot loads as the empty program */
    if (hdr[HDR_COUNT] == 0 || hdr[HDR_COUNT] > PROGRAM_STEPS || hdr[HDR_DWELL] == 0)
        return 0;
    out->used = 1;
    out->count = (uint16_t)hdr[HDR_COUNT];
    out->dwell_s = hdr[HDR_DWELL] > 86400u ? 86400u : (uint32_t)hdr[HDR_DWELL];
    out->wave = hdr[HDR_WAVE] > 255u ? 255u : (uint8_t)hdr[HDR_WAVE];
    out->ampl_cv = clamp16(hdr[HDR_AMPL]);
    return 1;
}

unsigned nx_uc_slot_freqs(unsigned slot, unsigned first, uint64_t *out, unsigned n)
{
    if (slot < 1u || slot > PROGRAM_SLOTS || first >= PROGRAM_STEPS)
        return 0;
    if (n > PROGRAM_STEPS - first)
        n = PROGRAM_STEPS - first;
    storage_slot_read(slot, 8u * (5u + first), out, 8u * n);   /* after the 5 header words */
    return n;
}

int nx_uc_cal_key(const struct nx_kev *e)
{
    return nx_core_cal_key(e);
}

int32_t nx_uc_cal_value(unsigned idx)
{
    return idx < 29u ? g_cal_table[idx] : 0;
}

uint8_t *nx_uc_settings_blob(void)
{
    return g_nx_settings.ui;
}

int nx_uc_settings_save(void)
{
    if (g_ui_state == ST_PC)
        return NX_REFUSED_PC;   /* no nx-initiated SPI writes while the PC may upload */
    return nx_settings_save(&g_nx_settings);
}

int nx_uc_quiet(uint32_t need_ms)
{
    return nx_runner_quiet(need_ms);
}

int nx_uc_resume_auto(void)
{
    return nx_resume_auto();
}

unsigned nx_uc_resume_offer(void)
{
    return nx_core_ready() && g_ui_state != ST_PC && g_ui_state != ST_CAL ? nx_resume_offer() : 0u;
}

void nx_uc_resume_answer(int resume)
{
    if (resume)
        nx_resume_accept();
    else
        nx_resume_discard();
}

int nx_uc_resume_on(void)
{
    return g_nx_settings.resume_mode != NX_RESUME_OFF;
}

void nx_uc_resume_set(int on)   /* saved with the UI's settings (the same record) */
{
    g_nx_settings.resume_mode = on ? NX_RESUME_AUTO_COUNTDOWN : NX_RESUME_OFF;
}

char nx_uc_panel(void)
{
    return g_nx_diag.panel ? g_nx_diag.panel : '?';
}

unsigned nx_uc_opt(unsigned id)
{
    switch (id) {
    case NX_OPT_STOP_G1:
    case NX_OPT_STOP_G2:
        return g_nx_settings.session_limit_min[id - NX_OPT_STOP_G1];
    case NX_OPT_OFF_DONE:
        return g_nx_settings.off_when_done;
    case NX_OPT_AUTOSTART:
        return g_nx_settings.autostart;
    default:
        return 0;
    }
}

void nx_uc_opt_set(unsigned id, unsigned v)
{
    switch (id) {
    case NX_OPT_STOP_G1:
    case NX_OPT_STOP_G2:
        g_nx_settings.session_limit_min[id - NX_OPT_STOP_G1] = (uint16_t)v;
        break;
    case NX_OPT_OFF_DONE:
        g_nx_settings.off_when_done = (uint8_t)(v != 0);
        break;
    case NX_OPT_AUTOSTART:
        g_nx_settings.autostart = (uint8_t)(v != 0);
        break;
    default:
        break;
    }
}

#if NX_FEAT_CHAINS
struct nx_chain *nx_uc_chains(void)
{
    return g_nx_chains.c;
}

void nx_uc_chains_save(void)
{
    nx_chain_save();
}

int nx_uc_chain_play(unsigned ch, unsigned id)
{
    return nx_chain_play(ch, id);
}
#endif

#if NX_FEAT_PLANS
struct nx_plan *nx_uc_plans(void)
{
    return g_nx_plans.p;
}

int nx_uc_plan_play(unsigned ch, unsigned id)
{
    return nx_plan_play(ch, id);
}
#endif

#if NX_FEAT_PRESET
unsigned nx_uc_preset_count(void)
{
    return nx_preset_count();
}

unsigned nx_uc_preset_info(unsigned n)
{
    return nx_preset_info(n);
}
#endif

#if NX_FEAT_MANUAL
struct nx_manual *nx_uc_manual(void)
{
    return &g_nx_manual;
}

void nx_uc_manual_changed(void)
{
    nx_manual_changed();
}

int nx_uc_manual_play(unsigned ch)
{
    return nx_manual_play(ch);
}
#endif

struct nx_bfb_cfg *nx_uc_bfb_cfg(void)
{
    return &g_nx_bfb_cfg;
}

int nx_uc_bfb_start(void)
{
    return nx_bfb_start();
}

void nx_uc_bfb_stop(void)
{
    nx_bfb_stop();
}

void nx_uc_bfb_pause(void)
{
    nx_bfb_pause(nx_ms());
}

const struct nx_bfb_view *nx_uc_bfb(void)
{
    return nx_bfb_view();
}

#if NX_FEAT_TERRAIN
unsigned nx_uc_terrain_count(void)
{
    return nx_terrain_count();
}

int nx_uc_terrain_view(unsigned id, struct nx_tview *v)
{
    const struct nx_tproto *p = nx_terrain_proto(id);
    memset(v, 0, sizeof *v);
    if (!p)
        return 0;
    const struct nx_tpos *pos = &g_nx_terrain.p[id - 1u];
    strncpy(v->name, nx_terrain_name(id), sizeof v->name - 1u);
    v->kind = p->kind;
    v->days = p->days;
    v->n = (uint8_t)nx_terrain_items(id);
    v->item = pos->item;
    v->power = pos->power;
    v->run_s = pos->run_s;
    for (unsigned ch = 0; ch < 2u; ch++) {   /* a generator that plays it: what it plays */
        struct nx_trun t;
        nx_terrain_run(ch, &t);
        if (t.id == id) {
            v->playing = (uint8_t)(v->playing | (1u << ch));
            v->item = t.item;
            v->power = t.power;
            v->run_s = t.run_s;
        }
    }
    if (v->item < v->n) {
        strncpy(v->set, nx_tset_name(nx_terrain_set_of(id, v->item)), sizeof v->set - 1u);
        nx_terrain_where(id, v->item, &v->day, &v->part, &v->hours);
    }
    return 1;
}

int nx_uc_terrain_play(unsigned ch, unsigned id)
{
    return nx_terrain_play(ch, id);
}

void nx_uc_terrain_move(unsigned id, int delta)
{
    nx_terrain_move(id, delta);
}

void nx_uc_terrain_power(unsigned id)
{
    nx_terrain_power(id);
}
#endif

int nx_uc_slot_op(uint8_t op, unsigned from, unsigned to)
{
    return nx_slots_request(op, from, to);
}

int nx_uc_slot_used(unsigned slot)
{
    return nx_slot_used(slot);
}

#if NX_FEAT_HIST
unsigned nx_uc_hist_count(void)
{
    return nx_hist_count();
}

int nx_uc_hist_get(unsigned i, struct nx_hist_rec *r)
{
    return nx_hist_get(i, r);
}

void nx_uc_hist_totals(struct nx_totals *t)
{
    *t = *nx_hist_totals();
}
#endif

#if NX_FEAT_DIAG
void nx_uc_diag(struct nx_diag_vm *d)
{
    memset(d, 0, sizeof *d);
    d->uptime_s = nx_ms() / 1000u;
    d->boots = g_nx_noinit.boots;
    d->crashes = g_nx_noinit.crash_count;
    d->reset_reason = g_nx_diag.reset_reason;
    d->safe_mode = g_nx_diag.safe_mode;
    d->store_state = (uint8_t)nx_store_state();
    d->panel = nx_uc_panel();
    for (unsigned ch = 0; ch < 2u; ch++) {
        const struct nx_txq_stats *st = nx_txq_stats(ch);
        d->sent[ch] = st->sent;
        d->timeouts[ch] = st->timeouts;
        d->lat_avg_us[ch] = st->ok ? (uint16_t)(st->lat_sum_us / st->ok) : 0u;
        d->link_down[ch] = (uint8_t)nx_txq_link_down(ch);
        d->uart_errors[ch] = g_nx_diag.uart_ore[ch] + g_nx_diag.uart_fe[ch] + g_nx_diag.uart_ne[ch];
    }
    d->pc_entries = g_nx_diag.pc_entries;
    d->pc_exits = g_nx_diag.pc_exits;
    d->w91_held = g_nx_diag.w91_held;
    d->flash_errors = g_nx_diag.flash_timeouts + g_nx_diag.flash_verify_errors + g_nx_diag.flash_guard_refusals;
    d->kv_free = nx_kv_free();
    d->loop_gap_ms = g_nx_diag.loop_gap_max_ms;
    uint16_t ts_cal, vref_cal;
    nx_hw_adc_cal(&ts_cal, &vref_cal);
    d->sensors = (uint8_t)nx_diag_sensors(nx_hw_adc(NX_ADC_TEMP), nx_hw_adc(NX_ADC_VREF), ts_cal, vref_cal, &d->t10,
                                          &d->vdd_mv);
    spiflash_read_jedec_id(d->jedec);
}
#endif
