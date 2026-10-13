/* Resume after a power cut (nx_resume.h). */
#include "nx_resume.h"

#include <string.h>

#include "link_internal.h"
#include "nx_config.h"
#include "nx_core.h"
#include "nx_crc.h"
#include "nx_diag.h"
#include "nx_fault.h"
#include "nx_chain.h"
#include "nx_hist.h"
#include "nx_hw.h"
#include "nx_kv.h"
#include "nx_runner.h"
#include "nx_settings.h"
#include "nx_src.h"
#include "nx_preset.h"
#include "nx_terrain.h"
#include "nx_time.h"
#include "state.h"

_Static_assert(sizeof(struct nx_resume_rec) == 28u, "resume snapshot layout");

#define ENT_BYTES (NX_KV_ENT_BYTES + 32u)   /* one put: header + the record padded to 16 bytes */

static uint8_t s_offer;       /* bit ch: offered at boot, not answered yet */
static uint8_t s_stored;      /* the KV store holds a snapshot */
static uint32_t s_written_t;  /* last write or delete */
static struct nx_resume_rec s_rec;    /* the stored snapshot (the offer) */
static struct nx_resume_rec s_seen;   /* the capture of the last write, for the change check */
#if NX_FEAT_TIMERS
static uint8_t s_auto;                /* the offer is the auto-start */
static struct nx_last_rec s_last;     /* the stored last programs */
#endif

static uint32_t slot_crc(unsigned slot)   /* the stored program record, in small reads */
{
    uint8_t buf[64];
    uint32_t crc = 0;
    for (uint32_t off = 0; off < PROG_RECORD_BYTES; off += sizeof buf) {
        uint32_t n = PROG_RECORD_BYTES - off < sizeof buf ? PROG_RECORD_BYTES - off : sizeof buf;
        storage_slot_read(slot, off, buf, n);
        crc = nx_crc32(crc, buf, n);
    }
    return crc;
}

void nx_resume_capture(struct nx_resume_rec *r)
{
    memset(r, 0, sizeof *r);
    r->ver = NX_RESUME_VER;
    for (unsigned ch = 0; ch < 2u; ch++) {
        const struct channel *c = &g_ch[ch];
        int run = (g_ui_flags & UIF_RUN(ch)) != 0;
        int set = nx_src_is_set(c->prog_slot);
        if (!(nx_src_is_slot(c->prog_slot) || nx_src_timed(c->prog_slot)) || (!run && c->elapsed <= 1u))
            continue;   /* nothing loaded, stopped (1) or not started yet (0) */
        r->g[ch].slot = c->prog_slot;
        r->g[ch].flags = (uint8_t)((run ? NX_RES_RUN : 0u) | ((g_ui_flags & UIF_LOOP(ch)) ? NX_RES_LOOP : 0u));
        r->g[ch].elapsed_ms = c->elapsed;
        if (set) {   /* a Terrain set: its protocol position; the item's time goes in crc (resume_task) */
            struct nx_trun t;
            nx_terrain_run(ch, &t);
            if (t.id)
                r->g[ch].chain = (uint16_t)(t.id | (t.item & 31u) << 3 | (t.power & 7u) << 8);
            continue;
        }
        if (nx_src_is_preset(c->prog_slot))   /* a built-in preset: no chain */
            continue;
        struct nx_chain_pos p;
        nx_chain_pos(ch, &p);
        if (p.id)
            r->g[ch].chain = (uint16_t)(p.id | (p.item & 15u) << 3 | (p.plan & 3u) << 7 | (uint16_t)(p.pass > 127u ? 127u : p.pass) << 9);
    }
}

static int same_state(const struct nx_resume_rec *a, const struct nx_resume_rec *b)
{
    for (unsigned ch = 0; ch < 2u; ch++)
        if (a->g[ch].slot != b->g[ch].slot || a->g[ch].flags != b->g[ch].flags || a->g[ch].chain != b->g[ch].chain)
            return 0;
    return 1;
}

/* A built-in set or preset of this image (nx_src.h): it plays the same as before the power cut. */
static int resumable_set(unsigned id)
{
#if NX_FEAT_TERRAIN
    if (nx_src_is_set(id))
        return nx_tset_steps(id - NX_SRC_SET) != 0;
#endif
#if NX_FEAT_PRESET
    if (nx_src_is_preset(id))
        return nx_preset_info(id - NX_SRC_PRESET) != 0;
#endif
    (void)id;
    return 0;
}

#if NX_FEAT_TIMERS
static void auto_boot(void)
{
    memset(&s_last, 0, sizeof s_last);
    s_auto = 0;
    if (nx_kv_get(NX_KEY_LAST, &s_last, sizeof s_last) != (int)sizeof s_last || s_last.ver != NX_RESUME_VER)
        memset(&s_last, 0, sizeof s_last);
    if (s_offer || !g_nx_settings.autostart || g_nx_diag.safe_mode || nx_pb1()
        || (nx_hw_keys() & (uint8_t)~NX_KEY_POWER)
        || (g_nx_diag.reset_reason != NX_RST_POWER && g_nx_diag.reset_reason != NX_RST_PIN))
        return;
    memset(&s_rec, 0, sizeof s_rec);
    for (unsigned ch = 0; ch < 2u; ch++) {
        if (!nx_src_is_slot(s_last.slot[ch]) && !resumable_set(s_last.slot[ch]))
            continue;
        s_rec.g[ch].slot = s_last.slot[ch];
        s_rec.g[ch].flags = (uint8_t)(NX_RES_RUN | ((s_last.loop >> ch) & 1u ? NX_RES_LOOP : 0u));
        s_offer = (uint8_t)(s_offer | (1u << ch));
    }
    s_auto = s_offer != 0;
}

int nx_resume_auto(void)
{
    return s_offer && s_auto;
}

/* The slot and loop flag of each generator that runs; 1 = they differ from the stored ones. */
static int last_changed(struct nx_last_rec *l)
{
    *l = s_last;
    l->ver = NX_RESUME_VER;
    for (unsigned ch = 0; ch < 2u; ch++)
        if ((g_ui_flags & UIF_RUN(ch)) && (nx_src_is_slot(g_ch[ch].prog_slot) || nx_src_timed(g_ch[ch].prog_slot))) {
            l->slot[ch] = g_ch[ch].prog_slot;
            l->loop = (uint8_t)((l->loop & ~(1u << ch)) | ((g_ui_flags & UIF_LOOP(ch)) ? 1u << ch : 0u));
        }
    return memcmp(l, &s_last, sizeof *l) != 0;
}
#else
int nx_resume_auto(void)
{
    return 0;
}
#endif

void nx_resume_boot(void)
{
    s_offer = s_stored = 0;
    s_written_t = 0;
    memset(&s_rec, 0, sizeof s_rec);
    memset(&s_seen, 0, sizeof s_seen);
    struct nx_resume_rec r;
    int len = nx_kv_get(NX_KEY_RESUME, &r, sizeof r);
    if (len > 0) {
        s_stored = 1;   /* anything under the key goes when it is not offered */
        if (len == (int)sizeof r && r.ver == NX_RESUME_VER)
            for (unsigned ch = 0; ch < 2u; ch++)   /* what the power cut ended */
                nx_hist_add(ch, r.g[ch].slot, 0, r.g[ch].elapsed_ms / 1000u, NX_HEND_CUT);
        if (len == (int)sizeof r && r.ver == NX_RESUME_VER && g_nx_settings.resume_mode != NX_RESUME_OFF
            && !g_nx_diag.safe_mode) {
            for (unsigned ch = 0; ch < 2u; ch++) {
                const struct nx_resume_gen *g = &r.g[ch];
                if (nx_src_is_slot(g->slot) ? slot_crc(g->slot) == g->crc : resumable_set(g->slot))
                    s_offer = (uint8_t)(s_offer | (1u << ch));
            }
            s_rec = r;
        }
    }
#if NX_FEAT_TIMERS
    auto_boot();   /* only when nothing is resumed */
#endif
}

unsigned nx_resume_offer(void)
{
    return s_offer;
}

int nx_resume_accept(void)
{
    for (unsigned ch = 0; ch < 2u; ch++) {
        if (!(s_offer & (1u << ch)))
            continue;
        const struct nx_resume_gen *g = &s_rec.g[ch];
        int set = nx_src_is_set(g->slot);
#if NX_FEAT_TIMERS
        if (set && s_auto) {   /* auto-start of a Terrain set: its protocol goes on */
            nx_terrain_continue(ch);
            s_offer = (uint8_t)(s_offer & ~(1u << ch));
            continue;
        }
#endif
        struct nx_action a = { NXA_LOAD, (uint8_t)ch, g->slot, set ? nx_terrain_ampl((g->chain >> 8) & 7u) : 0u, 0 };
        int rc = nx_core_act(&a);
        if (rc != NX_OK)
            return rc;
        if (g->elapsed_ms) {   /* an auto-start plays from the start */
            a.type = NXA_SEEK;
            a.v = g->elapsed_ms;
            nx_core_act(&a);
        }
        a.type = (g->flags & NX_RES_RUN) ? NXA_PLAY : NXA_PAUSE;
        a.v = (g->flags & NX_RES_LOOP) ? 1u : 0u;
        nx_core_act(&a);
        if (set) {
            struct nx_trun t = { (uint8_t)(g->chain & 7u), (uint8_t)((g->chain >> 3) & 31u), (uint8_t)((g->chain >> 8) & 7u),
                                 g->crc };
            nx_terrain_resume(ch, &t);
        } else {
            struct nx_chain_pos p = { (uint8_t)(g->chain & 7u), (uint8_t)((g->chain >> 3) & 15u), (uint8_t)(g->chain >> 9),
                                      (uint8_t)((g->chain >> 7) & 3u) };
            nx_chain_resume(ch, &p);
        }
        s_offer = (uint8_t)(s_offer & ~(1u << ch));
    }
    return NX_OK;
}

void nx_resume_discard(void)
{
    s_offer = 0;   /* nothing plays: the next quiet task deletes the snapshot */
}

void nx_resume_task(uint32_t now)
{
    if (g_ui_state == ST_PC) {   /* the PC owns the generators and may rewrite the slots */
        s_offer = 0;
        return;
    }
    if (g_ui_state == ST_CAL || !nx_core_ready() || s_offer)
        return;   /* an open offer keeps the stored snapshot until it is answered */
#if NX_FEAT_TIMERS
    struct nx_last_rec last;
    if (g_nx_settings.autostart && last_changed(&last) && now - s_written_t >= NX_RESUME_MIN_GAP_MS
        && nx_runner_quiet(nx_kv_free() < 2u * ENT_BYTES ? 1000u : NX_RESUME_QUIET_MS)) {
        s_written_t = now;
        if (nx_kv_put(NX_KEY_LAST, &last, sizeof last) == NX_OK)
            s_last = last;
        return;
    }
#endif
    struct nx_resume_rec want;
    nx_resume_capture(&want);
    if (g_nx_settings.resume_mode == NX_RESUME_OFF)
        memset(want.g, 0, sizeof want.g);
    int none = !want.g[0].slot && !want.g[1].slot;
    int due = ((want.g[0].flags | want.g[1].flags) & NX_RES_RUN) && now - s_written_t >= NX_RESUME_EVERY_MS;
    if (none && !s_stored) {
        s_seen = want;   /* the next program counts as a change, even the same slot again */
        return;
    }
    if (!none && same_state(&want, &s_seen) && !due)
        return;
    if (now - s_written_t < NX_RESUME_MIN_GAP_MS
        || !nx_runner_quiet(nx_kv_free() < 2u * ENT_BYTES ? 1000u : NX_RESUME_QUIET_MS))
        return;
    s_seen = want;
    s_written_t = now;
    for (unsigned ch = 0; ch < 2u; ch++) {   /* only a program that is the slot's own */
        struct nx_resume_gen *g = &want.g[ch];
        if (!g->slot)
            continue;
        if (nx_src_is_set(g->slot)) {   /* a Terrain set: the item's running time */
            struct nx_trun t;
            nx_terrain_run(ch, &t);
            g->crc = t.run_s;
            continue;
        }
        if (nx_src_is_preset(g->slot))   /* part of the image: nothing to compare */
            continue;
        g->crc = nx_crc32(0, &g_ch[ch].prog, PROG_RECORD_BYTES);
        if (slot_crc(g->slot) != g->crc)
            memset(g, 0, sizeof *g);
    }
    if (!want.g[0].slot && !want.g[1].slot) {
        if (s_stored && nx_kv_del(NX_KEY_RESUME) == NX_OK)
            s_stored = 0;
        return;
    }
    if (nx_kv_put(NX_KEY_RESUME, &want, sizeof want) == NX_OK)
        s_stored = 1;
}

void nx_resume_power_off(void)
{
    s_offer = 0;
    if (s_stored && g_ui_state != ST_PC && nx_kv_del(NX_KEY_RESUME) == NX_OK)
        s_stored = 0;
}
