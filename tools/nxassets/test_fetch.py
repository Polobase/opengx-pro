"""Offline tests of nxassets.fetch (file:// URLs stand in for the real downloads)."""

import hashlib
import io
import json
import tempfile
import unittest
import zipfile
from pathlib import Path

from nxassets import fetch


def sha(b):
    return hashlib.sha256(b).hexdigest()


class FetchTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        buf = io.BytesIO()
        with zipfile.ZipFile(buf, "w") as z:
            z.writestr("pkg/a.ttf", b"font-a")
            z.writestr("LICENSE.txt", b"licence")
        self.zip = self.root / "pkg.zip"
        self.zip.write_bytes(buf.getvalue())
        (self.root / "plain.bin").write_bytes(b"plain")
        self.logs = []

    def tearDown(self):
        self.tmp.cleanup()

    def write_sources(self, zip_sha=None, member_sha=None):
        doc = {"dir": "third", "downloads": [
            {"url": self.zip.as_uri(), "sha256": zip_sha or sha(self.zip.read_bytes()),
             "extract": [{"member": "pkg/a.ttf", "save_as": "a.ttf", "sha256": member_sha or sha(b"font-a")},
                         {"member": "LICENSE.txt", "save_as": "A-LICENSE.txt", "sha256": sha(b"licence")}]},
            {"url": (self.root / "plain.bin").as_uri(), "sha256": sha(b"plain"), "save_as": "plain.bin"}]}
        (self.root / "sources.json").write_text(json.dumps(doc))

    def test_downloads_extracts_and_verifies(self):
        self.write_sources()
        fetch.fetch(self.root, "sources.json", self.logs.append)
        self.assertEqual((self.root / "third/a.ttf").read_bytes(), b"font-a")
        self.assertEqual((self.root / "third/A-LICENSE.txt").read_bytes(), b"licence")
        self.assertEqual((self.root / "third/plain.bin").read_bytes(), b"plain")
        self.assertTrue(any(line.startswith("fetch") for line in self.logs))

    def test_second_run_downloads_nothing(self):
        self.write_sources()
        fetch.fetch(self.root, "sources.json", self.logs.append)
        self.logs.clear()
        fetch.fetch(self.root, "sources.json", self.logs.append)
        self.assertTrue(all(line.startswith("ok") for line in self.logs), self.logs)

    def test_wrong_archive_hash_writes_nothing(self):
        self.write_sources(zip_sha="0" * 64)
        with self.assertRaisesRegex(fetch.FetchError, "SHA-256"):
            fetch.fetch(self.root, "sources.json", self.logs.append)
        self.assertFalse((self.root / "third/a.ttf").exists())

    def test_wrong_member_hash_writes_nothing(self):
        self.write_sources(member_sha="1" * 64)
        with self.assertRaisesRegex(fetch.FetchError, "pkg/a.ttf"):
            fetch.fetch(self.root, "sources.json", self.logs.append)
        self.assertFalse((self.root / "third/A-LICENSE.txt").exists())


if __name__ == "__main__":
    unittest.main()
