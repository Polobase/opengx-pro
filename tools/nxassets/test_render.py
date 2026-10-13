"""Tests of the font rasteriser (nxassets.render) with the pinned Inter files; skipped until
`make nx-assets-fetch` has downloaded them."""

import json
import unittest
from pathlib import Path

from nxassets import render, strings

ROOT = Path(__file__).resolve().parents[2]
THIRD = ROOT / "assets/nx/third_party"
FONTS = {c["id"]: c for c in json.loads((ROOT / "assets/nx/fonts.json").read_text(encoding="utf-8"))["fonts"]}


@unittest.skipUnless((THIRD / "Inter-Regular.ttf").exists(), "run `make nx-assets-fetch` first")
class FontTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cap = render.render_font(FONTS["F_CAP13"], THIRD)
        cls.body = render.render_font(FONTS["F_BODY16"], THIRD)
        cls.num = render.render_font(FONTS["F_NUM48"], THIRD)

    def test_text_font_has_ascii_and_its_extras(self):
        t = self.body
        self.assertEqual((t.first, t.last), (0x20, 0x7E))
        self.assertEqual(t.extras, sorted(strings.CHAR_CODES[c] for c in FONTS["F_BODY16"]["extras"]))
        self.assertEqual(t.glyph(0x20).w, 0)
        self.assertGreater(t.glyph(0x20).advance, 0)

    def test_digits_are_tabular(self):
        for t in (self.body, self.num):
            self.assertEqual({t.glyph(c).advance for c in b"0123456789"}, {t.glyph(0x30).advance})

    def test_every_glyph_lies_inside_the_line_box(self):
        for t in (self.body, self.num):
            for g in t.glyphs:
                self.assertGreaterEqual(g.y_off, 0)
                self.assertLessEqual(g.y_off + g.h, t.height)
            self.assertLess(t.baseline, t.height)

    def test_number_font_is_tight_and_limited_to_its_characters(self):
        t = self.num
        self.assertEqual((t.first, t.last), (0x30, 0x39))
        self.assertEqual([chr(c) for c in t.extras], [" ", ",", "-", "."])
        self.assertEqual(t.glyph(0x38).y_off, 0)        # '8' reaches the top of the tight box
        self.assertIsNone(t.glyph(0x41))

    def test_opentype_features_are_applied(self):
        plain = render.render_font(FONTS["F_BODY16"], THIRD, basic=True)
        self.assertNotEqual(plain.glyph(ord("l")).px, self.body.glyph(ord("l")).px)   # ss04: l with a tail

    def test_triangle_is_drawn_from_the_scaled_play_glyph(self):
        tri = self.cap.glyph(strings.CHAR_CODES["▸"])
        cap = self.cap.glyph(ord("H"))
        self.assertGreater(tri.h, 2)
        self.assertLess(tri.h, cap.h)

    def test_missing_raqm_is_reported(self):
        saved = render.have_raqm
        render.have_raqm = lambda: False
        try:
            with self.assertRaisesRegex(render.RenderError, "raqm"):
                render.render_font(FONTS["F_CAP13"], THIRD)
        finally:
            render.have_raqm = saved

    def test_unknown_extras_are_rejected(self):
        with self.assertRaisesRegex(render.RenderError, "extras"):
            render.render_font(dict(FONTS["F_CAP13"], extras="€"), THIRD)


if __name__ == "__main__":
    unittest.main()
