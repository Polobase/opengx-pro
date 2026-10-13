"""The QR codes of the UI (assets/nx/qr.json): the symbol the tool builds is a real QR code of the
size its version says, it packs into the bit rows the firmware reads, and the committed source
holds the address the support page shows in words."""

import json
import unittest
from pathlib import Path

from nxassets import qr

ROOT = Path(__file__).resolve().parents[2]
CFG = {"id": "t", "symbol": "nx_qr_t", "text": "https://example.org/a", "version": 3, "error": "Q", "mask": 0}
FINDER = ["1111111", "1000001", "1011101", "1011101", "1011101", "1000001", "1111111"]


class QrTest(unittest.TestCase):
    def test_symbol_has_its_fixed_patterns(self):
        t = qr.build(CFG)
        self.assertEqual((t.n, t.ident, t.label), (29, "NX_QR_T", "version 3-Q, mask 0"))
        for top, left in ((0, 0), (0, 22), (22, 0)):
            self.assertEqual(["".join(map(str, t.rows[top + r][left:left + 7])) for r in range(7)], FINDER)
        self.assertEqual(t.rows[6][8:21], [1, 0] * 6 + [1])
        self.assertEqual([t.rows[i][6] for i in range(8, 21)], [1, 0] * 6 + [1])
        self.assertEqual(t.rows[21][8], 1)

    def test_rows_pack_most_significant_bit_first(self):
        t = qr.build(CFG)
        blob = t.blob()
        self.assertEqual((t.row_bytes, len(blob), t.size_bytes()), (4, 116, 116))
        self.assertEqual(blob[:4], bytes([0xFE, blob[1], blob[2], blob[3] & 0xF8]))   # finder row, padding 0
        for r, row in enumerate(t.rows):
            bits = "".join(f"{b:08b}" for b in blob[4 * r:4 * r + 4])
            self.assertEqual(bits, "".join(map(str, row)) + "000")
        self.assertEqual(t.dark(), sum(bin(b).count("1") for b in blob))

    def test_the_mask_and_the_level_are_taken_as_given(self):
        self.assertNotEqual(qr.build(CFG).rows, qr.build(dict(CFG, mask=3)).rows)
        self.assertNotEqual(qr.build(CFG).rows, qr.build(dict(CFG, error="M")).rows)

    def test_bad_sources_are_refused(self):
        with self.assertRaises(qr.QrError):
            qr.build(dict(CFG, text="x" * 200))       # does not fit version 3
        with self.assertRaises(qr.QrError):
            qr.build(dict(CFG, mask=8))
        with self.assertRaises(qr.QrError):
            qr.build({k: v for k, v in CFG.items() if k != "version"})

    def test_the_committed_code_is_the_address_shown_beside_it(self):
        codes = json.loads((ROOT / "assets/nx/qr.json").read_text(encoding="utf-8"))["codes"]
        en = json.loads((ROOT / "assets/nx/strings/en.json").read_text(encoding="utf-8"))
        for c in codes:
            self.assertEqual(c["text"], "https://" + en[c["shown_as"]]["text"])
            self.assertEqual(qr.build(c).n, 17 + 4 * c["version"])


if __name__ == "__main__":
    unittest.main()
