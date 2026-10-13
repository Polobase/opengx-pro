"""Tests of the UI string compiler (nxassets.strings)."""

import unittest

from nxassets import glyphenc as ge
from nxassets import strings as st


def font(adv=None):
    """A fake text font: ASCII and a-umlaut, every glyph `adv` px wide (space 3)."""
    adv = adv or {}
    glyphs = {c: ge.Glyph(c, 1, 1, 0, 0, adv.get(c, 3 if c == 0x20 else 6), [15]) for c in range(0x20, 0x7F)}
    glyphs[st.CHAR_CODES["ä"]] = ge.Glyph(0x80, 1, 1, 0, 0, 6, [15])
    return ge.build_table("f", glyphs, 4, 3, 4, (0x20, 0x7E))


def entry(text, max_px=100, fnt="F", **kw):
    return dict(text=text, font=fnt, max_px=max_px, **kw)


class EncodeTest(unittest.TestCase):
    def test_ascii_extras_and_placeholders(self):
        self.assertEqual(st.encode_text("Zurück {1}/{0}"), b"Zur\x82ck \x02/\x01")
        self.assertEqual(st.encode_text("…"), bytes([st.ELLIPSIS_CODE]))
        self.assertEqual(st.CHAR_CODES["▸"], 0x8F)

    def test_unmapped_characters_and_bad_placeholders_fail(self):
        with self.assertRaisesRegex(st.StringError, "U\\+20AC"):
            st.encode_text("5 €")
        with self.assertRaisesRegex(st.StringError, "placeholder"):
            st.encode_text("{3}")
        with self.assertRaisesRegex(st.StringError, "placeholder"):
            st.encode_text("{x}")

    def test_identifiers(self):
        self.assertEqual(st.c_ident("state.no_program"), "NX_S_STATE_NO_PROGRAM")


class CompileTest(unittest.TestCase):
    def compile(self, en, de, fonts=None):
        return st.compile_strings({"en": en, "de": de}, fonts or {"F": font()})

    def test_ids_follow_the_english_order(self):
        ids, comp, report = self.compile({"b": entry("B"), "a": entry("ä")}, {"a": entry("ä"), "b": entry("B")})
        self.assertEqual(ids, ["b", "a"])
        self.assertEqual(comp["de"], [b"B", b"\x80"])
        self.assertEqual(len(report), 4)

    def test_too_wide_is_an_error_with_the_measured_width(self):
        with self.assertRaisesRegex(st.StringError, "de:x: 'Sehr lang' is 51 px wide, max_px 50"):
            self.compile({"x": entry("Long", 50)}, {"x": entry("Sehr lang", 50)})

    def test_placeholders_are_measured_with_sample_args(self):
        ok = entry("Step {0}", 60, args=["99"])
        self.compile({"s": ok}, {"s": entry("Schritt {0}", 60, args=["9"])})
        with self.assertRaisesRegex(st.StringError, "needs sample"):
            self.compile({"s": entry("Step {0}", 60)}, {"s": ok})
        with self.assertRaisesRegex(st.StringError, "placeholders"):
            self.compile({"s": ok}, {"s": entry("Schritt", 60)})

    def test_languages_must_agree(self):
        with self.assertRaisesRegex(st.StringError, "missing ids"):
            self.compile({"a": entry("A"), "b": entry("B")}, {"a": entry("A")})
        with self.assertRaisesRegex(st.StringError, "font/max_px differ"):
            self.compile({"a": entry("A")}, {"a": entry("A", 99)})

    def test_glyphs_missing_from_the_font_fail(self):
        with self.assertRaisesRegex(st.StringError, "no glyph for code 0x8f"):
            self.compile({"a": entry("A ▸")}, {"a": entry("A")})


class TableTest(unittest.TestCase):
    def test_duplicates_and_tails_share_storage(self):
        offsets, pool = st.string_table([b"kHz", b"Hz", b"kHz", b"Aus"])
        self.assertEqual(pool, b"Aus\0kHz\0")
        self.assertEqual(offsets, [4, 5, 4, 0])


if __name__ == "__main__":
    unittest.main()
