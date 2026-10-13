/* The nx main loop (docs/nx/fw2-design.md, "Main loop"): a cooperative scheduler in the order of
 * the original's pass (PC mode -> runner -> link 0, link 1 -> factory), with the generator queue
 * serviced in between and the UI last; WFI when nothing is pending. */
#pragma once

void nx_loop_pass(void);   /* one pass (the host tests drive it directly) */
void nx_loop_reset(void);  /* power-on state (host tests) */
__attribute__((noreturn)) void nx_run(void);
