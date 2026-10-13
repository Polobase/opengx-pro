/* nx text and icon tables (nx_assets.c): glyph lookup, row-band decoding into an alpha
 * buffer, text measurement with ellipsis, and the UI string tables. No drawing: the UI engine
 * blends the decoded alpha (0..15) into its band buffers. Format: nx_asset_fmt.h. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "nx_asset_fmt.h"

/* Glyph of `code` (dense range first, then the sorted extra codes), or NULL. For icon sets the
 * code is the icon index (NX_I20_* ...). */
const nx_glyph_t *nx_font_glyph(const nx_font_t *f, uint8_t code);

/* nx_font_glyph, but a missing code falls back to '?' (NULL if the font has no '?' either). */
const nx_glyph_t *nx_font_glyph_or_fallback(const nx_font_t *f, uint8_t code);

/* Decode glyph rows y0 <= y < y1 (clipped to g->h) into out, g->w bytes per row, alpha 0..15.
 * Rows above y0 are skipped without writing, so a band renderer decodes only what it shows. */
void nx_glyph_decode(const nx_font_t *f, const nx_glyph_t *g, unsigned y0, unsigned y1, uint8_t *out);

/* Sum of the advances of the first n bytes of s (stops early at a NUL); missing codes count
 * as their fallback. */
unsigned nx_font_text_width_n(const nx_font_t *f, const char *s, size_t n);
unsigned nx_font_text_width(const nx_font_t *f, const char *s);

/* What to draw after a cut text: the font's ellipsis glyph, else "...", else "". */
const char *nx_font_ellipsis(const nx_font_t *f);

typedef struct {
    uint16_t n;         /* bytes of the text to draw */
    uint16_t width;     /* pixels, including the ellipsis */
    uint8_t ellipsis;   /* 1: draw nx_font_ellipsis(f) after the n bytes */
} nx_fit_t;

/* Fit s into max_px: all of it if it fits; otherwise the longest prefix (trailing spaces
 * dropped) followed by the ellipsis; {0, 0, 0} if not even the ellipsis fits. */
nx_fit_t nx_font_fit(const nx_font_t *f, const char *s, unsigned max_px);

/* UI strings (nx_str_ids.h ids). The image is English only: the string table of NX_LANG_EN. */
const char *nx_str(unsigned id);       /* "" for an unknown id */

/* nx_str(id) with its argument slots {0}..{2} replaced by args[0..2] (missing or NULL: empty),
 * truncated to size-1 bytes and NUL-terminated. Returns the length written. */
size_t nx_str_format(char *out, size_t size, unsigned id, const char *const *args, unsigned nargs);
