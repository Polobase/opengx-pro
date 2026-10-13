"""Tests of the C emitter and of `gen_nx_assets.py build/check` on a scratch copy of assets/nx/."""

import contextlib
import io
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

import gen_nx_assets as gen
from nxassets import emit

ROOT = Path(__file__).resolve().parents[2]
THIRD = ROOT / "assets/nx/third_party"


class CStringTest(unittest.TestCase):
    def test_escapes_quotes_backslashes_trigraph_marks_and_non_ascii(self):
        self.assertEqual(emit.c_string(b'a"b\\c?d\x80\x01'), 'a\\"b\\\\c\\?d\\200\\001')

    def test_octal_escapes_never_swallow_following_digits(self):
        self.assertEqual(emit.c_string(b"\x011"), "\\0011")   # three octal digits, then '1'


@unittest.skipUnless((THIRD / "MaterialSymbolsRounded.ttf").exists(), "run `make nx-assets-fetch` first")
class CliTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        shutil.copytree(ROOT / "assets/nx", self.root / "assets/nx", ignore=shutil.ignore_patterns("third_party"))
        (self.root / "assets/nx/third_party").symlink_to(THIRD)
        (self.root / "src/nx").mkdir(parents=True)
        shutil.copy(ROOT / "src/nx/nx_asset_fmt.h", self.root / "src/nx/nx_asset_fmt.h")

    def tearDown(self):
        self.tmp.cleanup()

    def run_cmd(self, fn):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            rc = fn(self.root, False)
        return rc, out.getvalue() + err.getvalue()

    def test_build_then_check_passes_and_drift_fails(self):
        rc, text = self.run_cmd(gen.cmd_build)
        self.assertEqual(rc, 0, text)
        self.assertIn("NX_ASSET_MAX 36864", text)
        for rel in (gen.OUT_C, gen.OUT_H, gen.OUT_IDS, gen.OUT_VEC, gen.LOCK):
            self.assertTrue((self.root / rel).exists(), rel)
        self.assertEqual(self.run_cmd(gen.cmd_check)[0], 0)
        c = self.root / gen.OUT_C
        c.write_text(c.read_text().replace("0x", "0X", 1))
        rc, text = self.run_cmd(gen.cmd_check)
        self.assertEqual(rc, 1)
        self.assertIn("nx_assets.c: differs", text)

    def test_generated_c_compiles_warning_free(self):
        self.assertEqual(self.run_cmd(gen.cmd_build)[0], 0)
        cc = shutil.which("cc")
        if not cc:
            self.skipTest("no host C compiler")
        r = subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
                            str(self.root / gen.OUT_C)], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_over_budget_fails_and_writes_nothing(self):
        saved = gen.NX_ASSET_MAX
        gen.NX_ASSET_MAX = 1000
        try:
            rc, text = self.run_cmd(gen.cmd_build)
        finally:
            gen.NX_ASSET_MAX = saved
        self.assertEqual(rc, 1)
        self.assertIn("more than NX_ASSET_MAX = 1000", text)
        self.assertFalse((self.root / gen.OUT_C).exists())

    def test_a_string_too_wide_fails_the_build(self):
        en = self.root / "assets/nx/strings/en.json"
        en.write_text(en.read_text(encoding="utf-8").replace('"text": "READY"', '"text": "READY READY READY"'),
                      encoding="utf-8")
        rc, text = self.run_cmd(gen.cmd_build)
        self.assertEqual(rc, 1)
        self.assertIn("en:state.ready", text)


if __name__ == "__main__":
    unittest.main()
