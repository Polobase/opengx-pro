"""Tests of the RGB565 conversion and the WCAG contrast checks (nxassets.palette)."""

import json
import unittest
from pathlib import Path

from nxassets import palette as pl

ROOT = Path(__file__).resolve().parents[2]


def doc(themes, text=(), ui=()):
    tokens = sorted({t for th in themes.values() for t in th})
    return {"tokens": tokens, "themes": themes, "checks": {"text": [list(p) for p in text], "ui": [list(p) for p in ui]}}


class ConversionTest(unittest.TestCase):
    def test_rgb565_rounds_each_channel(self):
        self.assertEqual(pl.rgb565(pl.parse_hex("#0B0F14")), 0x0882)
        self.assertEqual(pl.rgb565((255, 255, 255)), 0xFFFF)
        self.assertEqual(pl.rgb565((0, 0, 0)), 0x0000)

    def test_expand_replicates_the_high_bits(self):
        self.assertEqual(pl.expand565(0x0882), (8, 16, 16))
        self.assertEqual(pl.expand565(0xFFFF), (255, 255, 255))

    def test_bad_colours_are_rejected(self):
        for s in ("0B0F14", "#0B0F1", "#GGGGGG"):
            with self.assertRaises(pl.PaletteError):
                pl.parse_hex(s)

    def test_contrast_of_black_and_white_is_21(self):
        self.assertAlmostEqual(pl.contrast((0, 0, 0), (255, 255, 255)), 21.0, places=6)
        self.assertEqual(pl.contrast((10, 20, 30), (200, 100, 50)), pl.contrast((200, 100, 50), (10, 20, 30)))


class CheckTest(unittest.TestCase):
    def test_low_text_contrast_is_an_error_after_quantisation(self):
        d = doc({"dark": {"bg": "#000000", "fg": "#595959"}}, text=[("fg", "bg")])
        *_, report, errors = pl.build(d)
        self.assertEqual(len(errors), 1)
        self.assertIn("dark: fg on bg", errors[0])
        ratio = report[0][4]
        self.assertAlmostEqual(ratio, pl.contrast(pl.expand565(pl.rgb565((0x59,) * 3)), (0, 0, 0)))

    def test_ui_pairs_need_three_to_one(self):
        d = doc({"t": {"bg": "#000000", "ring": "#5A5A5A"}}, ui=[("ring", "bg")])
        self.assertEqual(pl.build(d)[-1], [])
        d = doc({"t": {"bg": "#000000", "ring": "#3A3A3A"}}, ui=[("ring", "bg")])
        self.assertEqual(len(pl.build(d)[-1]), 1)

    def test_every_theme_needs_every_token(self):
        d = doc({"a": {"bg": "#000000", "fg": "#FFFFFF"}, "b": {"bg": "#000000"}})
        with self.assertRaisesRegex(pl.PaletteError, "theme b: missing"):
            pl.build(d)

    def test_repository_palettes_pass(self):
        themes, tokens, values, report, errors = pl.build(json.loads((ROOT / "assets/nx/palettes.json").read_text()))
        self.assertEqual(errors, [])
        self.assertEqual(themes, ["dark", "light", "hc", "night"])
        self.assertEqual(len(tokens), 24)
        self.assertEqual(values["dark"]["bg"], 0x0882)
        self.assertGreater(len(report), 4 * 40)


if __name__ == "__main__":
    unittest.main()
