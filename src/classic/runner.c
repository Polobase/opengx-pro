/* Program runner: program_runner_tick 0x0800B564 (docs/spec/application.md §6.2,
 * protocols.md §5.3). Called every main-loop pass unless in PC mode or the calibration menu. */
#include "app.h"
#include "link.h"
#include "runner_seq.h"
#include "state.h"

static uint8_t current_step(const struct channel *c)
{
    uint64_t count = c->prog.hdr[HDR_COUNT], dwell = c->prog.hdr[HDR_DWELL];
    if (count == 0)
        return 0;
    /* DEVIATION: the original divides by zero when a serial upload leaves dwell = 0 (Keil's
     * runtime then returns garbage); a dwell of 0 is treated as 1 second instead. */
    if (dwell == 0)
        dwell = 1;
    return (uint8_t)(c->elapsed / (dwell * 100u) + 1u);
}

static void send_seq(unsigned ch, const struct seq *s)
{
    for (unsigned i = 0; i < s->n; i++) {
        link_send_raw(ch, s->line[i], s->len[i]);
        link_wait_ok();
    }
}

static void send_step(unsigned ch, uint8_t step)
{
    struct channel *c = &g_ch[ch];
    c->step_freq = c->prog.freq[step - 1u];
    c->step_sent = step;

    /* DEVIATION: the original looks up G2's per-step pair with G1's step index (0x0800B92A);
     * each channel uses its own step here. */
    uint16_t *pair = &c->params[2u * step - 2u];
    if (pair[0] == 0xFFFFu)
        pair[0] = 0;
    if (pair[1] == 0xFFFFu)
        pair[1] = 0;

    /* seq_idle/seq_play capture step_freq, prog and pair[] into s right here, before send_seq's
     * first line goes out (see the timing note in runner_seq.h). */
    struct seq s;
    if (!(g_ui_flags & UIF_RUN(ch)))   /* idle configuration, outputs off */
        seq_idle(&s, c->step_freq, &c->prog);
    else                               /* playing */
        seq_play(&s, c->step_freq, pair[0], pair[1]);
    send_seq(ch, &s);
    if (g_ui_state != ST_LIST && g_ui_state != ST_PC)
        flags_set(&g_ui_flags, UIF_REDRAW);
}

void runner_tick(void)
{
    for (unsigned ch = 0; ch < 2; ch++)
        g_ch[ch].step = current_step(&g_ch[ch]);

    for (unsigned ch = 0; ch < 2; ch++) {
        struct channel *c = &g_ch[ch];
        uint64_t count = c->prog.hdr[HDR_COUNT];
        if ((count >> 32) == 0 && c->step > (uint32_t)count) {   /* program finished */
            if (!(g_ui_flags & UIF_LOOP(ch))) {
                if (g_ui_flags & UIF_RUN(ch)) {
                    struct seq s;
                    seq_off(&s);
                    send_seq(ch, &s);
                }
                flags_clear(&g_ui_flags, UIF_RUN(ch));
            }
            if (g_ui_state != ST_LIST)
                flags_set(&g_ui_flags, UIF_REDRAW);
            c->elapsed = 1;
        } else if (c->step != c->step_sent && c->step != 0) {
            send_step(ch, c->step);
        }
    }
}
