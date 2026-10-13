/* Application core (docs/spec/application.md §1-3, §5.1, §6.2, §8, §9.1): boot flow, TIM3 tick
 * logic, key events, state machine, program runner, PC mode, calibration menu, power off. */
#pragma once

void runner_tick(void);   /* program_runner_tick 0x0800B564 */

/* Boot tail (app_state_init, splash, boot-time key check) and the endless main loop. */
__attribute__((noreturn)) void app_run(void);
