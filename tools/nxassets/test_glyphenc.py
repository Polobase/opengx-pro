"""Tests of the glyph encoding (format: src/nx/nx_asset_fmt.h)."""

import random
import unittest

from nxassets import glyphenc as ge


def glyph(code, w, h, adv, px=None, x=0, y=0):
    return ge.Glyph(code, w, h, x, y, adv, px if px is not None else [15] * (w * h))


class QuantizeTest(unittest.TestCase):
    def test_four_bpp_rounds_and_snaps_the_extremes(self):
        self.assertEqual(ge.quantize([0, 8, 9, 17, 128, 238, 247, 255], 4), [0, 0, 0, 0, 8, 15, 15, 15])
        self.assertEqual(ge.quantize([26, 34], 4), [2, 2])   # levels 2..13 are kept

    def test_two_bpp_keeps_four_levels(self):
        self.assertEqual(ge.quantize([0, 42, 43, 128, 212, 213, 255], 2), [0, 0, 5, 10, 10, 15, 15])

    def test_other_depths_are_rejected(self):
        with self.assertRaises(ge.EncodeError):
            ge.quantize([0], 3)


class RawTest(unittest.TestCase):
    def test_four_bpp_is_msb_first_without_row_padding(self):
        self.assertEqual(ge.pack_raw([1, 2, 3], 4), bytes([0x12, 0x30]))

    def test_two_bpp_packs_four_pixels_per_byte(self):
        self.assertEqual(ge.pack_raw([0, 5, 10, 15, 15], 2), bytes([0b00011011, 0b11000000]))

    def test_two_bpp_rejects_levels_it_cannot_hold(self):
        with self.assertRaises(ge.EncodeError):
            ge.pack_raw([7], 2)

    def test_roundtrip(self):
        rnd = random.Random(1)
        for bpp, levels in ((4, range(16)), (2, (0, 5, 10, 15))):
            px = [rnd.choice(list(levels)) for _ in range(77)]
            self.assertEqual(ge.raw_decode(ge.pack_raw(px, bpp), len(px), bpp), px)


class RleTest(unittest.TestCase):
    def test_runs_of_transparent_and_opaque(self):
        self.assertEqual(ge.rle_encode([0] * 20), bytes([0x0F, 0x03]))       # 16 + 4
        self.assertEqual(ge.rle_encode([15, 15, 15, 7, 0]), bytes([0xF2, 0x70, 0x00]))

    def test_single_levels_take_one_nibble_and_odd_streams_are_padded(self):
        self.assertEqual(ge.rle_encode([3, 4, 5]), bytes([0x34, 0x50]))

    def test_roundtrip_random(self):
        rnd = random.Random(2)
        for _ in range(200):
            n = rnd.randint(1, 300)
            px = [rnd.choice([0, 0, 0, 15, 15, rnd.randint(0, 15)]) for _ in range(n)]
            self.assertEqual(ge.rle_decode(ge.rle_encode(px), n), px)

    def test_best_of_prefers_raw_on_a_tie(self):
        data, rle = ge.encode_best([0] * 64, 4)
        self.assertTrue(rle)
        self.assertEqual(data, bytes([0x0F, 0x0F, 0x0F, 0x0F]))
        data, rle = ge.encode_best([1, 2, 3, 4], 4)   # raw 2 bytes == RLE 2 bytes
        self.assertFalse(rle)


class TableTest(unittest.TestCase):
    def table(self, **kw):
        glyphs = {0x20: glyph(0x20, 0, 0, 3, []), 0x21: glyph(0x21, 2, 2, 4), 0x22: glyph(0x22, 2, 2, 4),
                  0x80: glyph(0x80, 1, 3, 5, [1, 2, 3]), 0x89: glyph(0x89, 3, 1, 9, [15, 0, 15])}
        glyphs.update(kw)
        return ge.build_table("t", glyphs, height=4, baseline=3, bpp=4, dense=(0x20, 0x22))

    def test_dense_codes_then_sorted_extras(self):
        t = self.table()
        self.assertEqual((t.first, t.last, t.extras), (0x20, 0x22, [0x80, 0x89]))
        self.assertEqual([t.index(c) for c in (0x20, 0x22, 0x80, 0x89, 0x7F)], [0, 2, 3, 4, None])

    def test_identical_bitmaps_are_stored_once(self):
        t = self.table()
        self.assertEqual(t.records[1][0], t.records[2][0])
        self.assertEqual(t.records[0][:3], (0, 0, 0))            # empty glyph: no bitmap

    def test_rle_flag_marks_encoded_glyphs(self):
        t = self.table()
        big = ge.build_table("b", {0x41: glyph(0x41, 8, 4, 8, [0] * 32)}, 4, 3, 4, (0x41, 0x41))
        self.assertTrue(big.records[0][0] & ge.RLE_FLAG)
        self.assertFalse(t.records[3][0] & ge.RLE_FLAG)

    def test_missing_dense_code_and_bad_boxes_are_errors(self):
        with self.assertRaisesRegex(ge.EncodeError, "lacks code 0x21"):
            ge.build_table("t", {0x20: glyph(0x20, 0, 0, 3, [])}, 4, 3, 4, (0x20, 0x21))
        with self.assertRaisesRegex(ge.EncodeError, "below the line box"):
            ge.build_table("t", {0x20: glyph(0x20, 1, 3, 3, y=2)}, 4, 3, 4, (0x20, 0x20))

    def test_size_counts_header_extras_records_and_data(self):
        t = self.table()
        self.assertEqual(t.size_bytes(), 20 + 2 + 8 * 5 + len(t.data))

    def test_width_uses_question_mark_for_missing_codes(self):
        t = self.table()
        self.assertEqual(t.width(b"!\x80"), 9)
        self.assertEqual(t.width(b"\x7f"), 0)                    # no '?' in this font
        q = ge.build_table("q", {0x3F: glyph(0x3F, 1, 1, 6)}, 4, 3, 4, (0x3F, 0x3F))
        self.assertEqual(q.width(b"\x7f\x3f"), 12)

    def test_fit_whole_text_ellipsis_trailing_space_and_too_narrow(self):
        t = self.table()
        self.assertEqual(t.fit(b"!!!", 12, 0x89), (3, 12, 0))
        self.assertEqual(t.fit(b"!!!", 11, 0x89), (0, 9, 1))        # 4 + 9 > 11
        self.assertEqual(t.fit(b"!!!!!", 17, 0x89), (2, 17, 1))
        self.assertEqual(t.fit(b"! !!!", 16, 0x89), (1, 13, 1))     # "! " + ellipsis -> "!" + ellipsis
        self.assertEqual(t.fit(b"!!!", 5, 0x89), (0, 0, 0))         # not even the ellipsis
        self.assertEqual(t.ellipsis(0x89), b"\x89")


if __name__ == "__main__":
    unittest.main()
