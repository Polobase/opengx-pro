/* PC-mode entry/exit shared by the classic and nx main loops (core_pcmode.h). */
#include "core_pcmode.h"

#include "link.h"
#include "link_hw.h"
#include "state.h"

enum pcm_event core_pcmode_update(uint32_t pb1)
{
    if (pb1 && g_ui_state != ST_PC) {
        pin_high(GPIOB, HANDSHAKE_PIN);
        g_ui_state = ST_PC;
        flags_clear(&g_ui_flags, UIF_RUN_G1 | UIF_RUN_G2 | UIF_LOOP_G1 | UIF_LOOP_G2);
        for (unsigned ch = 0; ch < 2; ch++) {
            g_ch[ch].elapsed = 0;
            g_ch[ch].prog_slot = 0;
        }
        flags_set(&g_ui_flags, UIF_REDRAW);
        delay_loop(CORE_DELAY_13MS);
        for (unsigned ch = 0; ch < 2; ch++) {
            link_send_line(ch, ":w11=0,0,");
            link_wait_ok();
        }
        delay_loop(CORE_DELAY_13MS);
        pin_low(GPIOB, HANDSHAKE_PIN);
        return PCM_ENTERED;
    }
    if (!pb1 && g_ui_state == ST_PC) {
        g_ui_state = ST_HOME;
        pin_high(GPIOB, HANDSHAKE_PIN);
        for (unsigned ch = 0; ch < 2; ch++) {
            link_send_line(ch, ":w15=0,0,");
            link_wait_ok();
        }
        flags_clear(&g_pc_flags, 0x03C0u);   /* stored w15 values */
        for (unsigned ch = 0; ch < 2; ch++)
            g_ch[ch].pc_freq[0] = g_ch[ch].pc_freq[1] = 0;
        flags_set(&g_ui_flags, UIF_REDRAW);
        return PCM_LEFT;
    }
    return PCM_NONE;
}
