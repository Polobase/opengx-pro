"""The committed UI texts (assets/nx/strings) against the real fonts: every English string (the
image's only language) compiles and fits, and the Drop-1 screens have their texts."""

import json
import unittest
from pathlib import Path

import gen_nx_assets as gen
from nxassets import render, strings

ROOT = Path(__file__).resolve().parents[2]
THIRD = ROOT / "assets/nx/third_party"
DROP1_PREFIXES = ["common.", "gen.", "state.", "dash.", "strip.", "unit.", "quick.", "theme.", "lib.", "detail.",
                  "wave.", "out.", "set.", "about.", "pc.", "cal.", "bye.", "power.", "lock.", "hold.", "err."]
STATES = ["ready", "running", "paused", "finished", "no_program", "no_response", "pc_mode", "loop"]


def load(lang):
    return json.loads((ROOT / f"assets/nx/strings/{lang}.json").read_text(encoding="utf-8"))


@unittest.skipUnless((THIRD / "Inter-Regular.ttf").exists(), "run `make nx-assets-fetch` first")
class ContentTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        fonts = json.loads((ROOT / "assets/nx/fonts.json").read_text(encoding="utf-8"))["fonts"]
        cls.fonts = {c["id"]: render.render_font(c, THIRD) for c in fonts}
        cls.en = load("en")
        cls.ids, cls.compiled, cls.report = strings.compile_strings({"en": cls.en}, cls.fonts)

    def test_all_strings_compile_and_fit(self):
        self.assertEqual(len(self.ids), len(self.en))
        self.assertTrue(all(width <= max_px for _, _, _, width, max_px in self.report))

    def test_every_drop1_area_has_texts(self):
        for prefix in DROP1_PREFIXES:
            self.assertTrue(any(i.startswith(prefix) for i in self.ids), prefix)
        for s in STATES:
            self.assertIn(f"state.{s}", self.ids)

    def test_only_english_is_built(self):
        self.assertEqual(gen.LANGS, ["en"])
        self.assertFalse([i for i, e in self.en.items() if any(c in e["text"] for c in "äöüÄÖÜß")])

    def test_only_the_two_text_fonts_carry_strings(self):
        self.assertEqual({e["font"] for e in self.en.values()}, {"F_CAP13", "F_BODY16"})


if __name__ == "__main__":
    unittest.main()
