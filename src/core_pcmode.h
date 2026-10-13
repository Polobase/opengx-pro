/* PC-mode entry and exit of the original main loop (docs/spec/application.md §5.3). The PB2
 * levels and the generator lines are visible to the PC and the generator boards, so the classic
 * and the nx firmware share this code. */
#pragma once

#include <stdint.h>

#define CORE_DELAY_13MS 0x34155u   /* delay_loop count of the original key scan, ~13.3 ms */

enum pcm_event { PCM_NONE, PCM_ENTERED, PCM_LEFT };

/* Call once per main-loop pass with the PB1 level (1 = PC connected).
 * Entry: PB2 high, ST_PC, runs and loops stopped, elapsed and loaded slot cleared, redraw,
 * 13.3 ms, ":w11=0,0," + :ok wait on G1 then G2, 13.3 ms, PB2 low.
 * Exit: ST_HOME, PB2 high (it stays high), ":w15=0,0," + :ok wait on G1 then G2, stored w15
 * values and PC frequencies cleared, redraw. Blocks for the whole sequence. */
enum pcm_event core_pcmode_update(uint32_t pb1);
