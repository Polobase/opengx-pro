/* nx engine gallery (docs/nx/design/ui.md §7): sheets that show every primitive, every glyph of
 * every font, the icon sets, the four palettes with contrast labels, the hero-frequency fixtures,
 * scope variants and frames of the transitions and the digit roll, all drawn by the real engine
 * through nx_lcd onto the glass, then the UI's screens. The same code runs as APP=nxgallery on the
 * emulator (screenshots with `tools/emu.py --shot-hook nxg_shot:DIR`) and in the host renderer
 * (tests/host/gallery), and tools/nx_gallery.py compares both with the approved baselines in
 * tests/gallery/nx/. Time is virtual (nxg_core.h), so every animation frame is deterministic. */
#pragma once

/* Renders every sheet and frame, then the screens, calling nxg_shot for each; returns the number
 * of shots. The panel must be initialised (lcd_init + nx_lcd_init, or the host panel +
 * nx_lcd_init). */
unsigned nxg_run(void);

/* The UI screens' fixtures (nxg_screens.c): the end of nxg_run, and all that the host renderer of
 * the image's engine configuration runs (tests/host/Makefile, gallery-dev): the sheets need the
 * engine features that configuration switches off. */
void nxg_screens_run(void);
