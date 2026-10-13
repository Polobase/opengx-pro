"""Tests of the icon rasteriser: Material Symbols through FreeType variation axes and the
procedural icons (nxassets.render, nxassets.procedural)."""

import json
import unittest
from pathlib import Path

from nxassets import icons, procedural, render

ROOT = Path(__file__).resolve().parents[2]
THIRD = ROOT / "assets/nx/third_party"
SETS = {s["symbol"]: s for s in json.loads((ROOT / "assets/nx/icons.json").read_text())["sets"]}


class ProceduralTest(unittest.TestCase):
    def test_every_procedural_icon_draws_something_inside_its_box(self):
        for name in procedural.DRAW:
            px = icons.render_procedural(name, 20, 2.1, 4)
            self.assertEqual(len(px), 400)
            self.assertGreater(sum(1 for a in px if a == 15), 10, name)
            edge = [px[i] for i in range(20)] + [px[380 + i] for i in range(20)]
            self.assertEqual(max(edge), 0, f"{name} touches the top or bottom row")

    def test_unknown_names_fail(self):
        with self.assertRaises(KeyError):
            icons.render_procedural("nope", 20, 2.0, 4)


@unittest.skipUnless((THIRD / "MaterialSymbolsRounded.ttf").exists(), "run `make nx-assets-fetch` first")
class MaterialTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tables = {name: icons.render_iconset(cfg, THIRD) for name, cfg in SETS.items()}

    def test_sets_have_one_glyph_per_icon_inside_the_box(self):
        for name, t in self.tables.items():
            size = SETS[name]["size"]
            self.assertEqual(len(t.records), len(SETS[name]["icons"]))
            self.assertEqual((t.height, t.baseline, t.first), (size, size, 0))
            for g in t.glyphs:
                self.assertGreater(g.w * g.h, 0)
                self.assertLessEqual(g.x_off + g.w, size)
                self.assertLessEqual(g.y_off + g.h, size)
                self.assertEqual(g.advance, size)

    def test_fill_axis_fills_the_stop_square(self):
        cfg = SETS["nx_icons20"]
        ids = [i["id"] for i in cfg["icons"]]
        filled = self.tables["nx_icons20"].glyphs[ids.index("stop")]
        outline = icons.render_iconset(dict(cfg, icons=[{"id": "stop", "material": "stop"}]), THIRD).glyphs[0]
        self.assertGreater(sum(filled.px), sum(outline.px) + 200)

    def test_depth_follows_the_set(self):
        self.assertEqual(self.tables["nx_icons20"].bpp, 2)
        self.assertTrue(all(a % 5 == 0 for g in self.tables["nx_icons20"].glyphs for a in g.px))
        self.assertEqual(self.tables["nx_icons32"].bpp, 4)

    def test_unknown_material_names_fail(self):
        cfg = dict(SETS["nx_icons32"], icons=[{"id": "x", "material": "no_such_symbol"}])
        with self.assertRaisesRegex(render.RenderError, "no_such_symbol"):
            icons.render_iconset(cfg, THIRD)


if __name__ == "__main__":
    unittest.main()
