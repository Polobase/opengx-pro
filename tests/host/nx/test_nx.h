/* Shared by the nx host tests (tests/host/nx/). */
#pragma once

/* Mocks, the W25Q16 model (poll timing, no power cut armed, whole-chip guard) and the simulated
 * clock back to their power-on state. */
void nx_reset_world(void);

/* The nx test groups (tests/host/nx/test_nx_*.c), run by main() in test_nx.c. */
void test_nx_time(void);
void test_nx_keys(void);
void test_nx_bl(void);
void test_nx_fault(void);
void test_nx_txq(void);
void test_nx_runner(void);
void test_nx_core(void);
void test_nx_flash(void);
void test_nx_store(void);
void test_nx_settings(void);
void test_nx_ui(void);
void test_nx_lcd(void);
void test_nx_gfx(void);
void test_nx_anim(void);
void test_nx_render(void);
void test_nx_trans(void);
void test_nx_hero(void);
void test_nx_scope(void);
void test_nx_ui_engine(void);
void test_nx_uiset(void);
void test_nx_uicore(void);
void test_nx_comp(void);
void test_nx_ui_set(void);
void test_nx_resume(void);
void test_nx_hist(void);
void test_nx_slots(void);
void test_nx_chain(void);
void test_nx_terrain(void);
void test_nx_manual(void);
void test_nx_preset(void);
