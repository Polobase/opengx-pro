"""Download the pinned third-party sources (assets/nx/sources.json) and verify every SHA-256.

The files land in the git-ignored directory named by sources.json ("dir"); nothing is written
unless its hash matches. Files already present with the right hash are not downloaded again.
"""

import hashlib
import io
import json
import urllib.request
import zipfile
from pathlib import Path


class FetchError(RuntimeError):
    pass


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def _download(url):
    with urllib.request.urlopen(url, timeout=120) as r:
        return r.read()


def fetch(root, sources="assets/nx/sources.json", log=print):
    src = json.loads((Path(root) / sources).read_text(encoding="utf-8"))
    dest = Path(root) / src["dir"]
    dest.mkdir(parents=True, exist_ok=True)
    for item in src["downloads"]:
        targets = [(e["save_as"], e["sha256"]) for e in item.get("extract", [])] or \
                  [(item["save_as"], item["sha256"])]
        if all((dest / n).exists() and sha256((dest / n).read_bytes()) == h for n, h in targets):
            log(f"ok      {', '.join(n for n, _ in targets)}")
            continue
        log(f"fetch   {item['url']}")
        data = _download(item["url"])
        if sha256(data) != item["sha256"]:
            raise FetchError(f"{item['url']}: SHA-256 {sha256(data)}, expected {item['sha256']}")
        if "extract" in item:
            with zipfile.ZipFile(io.BytesIO(data)) as z:
                blobs = []
                for e in item["extract"]:
                    blob = z.read(e["member"])
                    if sha256(blob) != e["sha256"]:
                        raise FetchError(f"{e['member']}: SHA-256 {sha256(blob)}, expected {e['sha256']}")
                    blobs.append((e["save_as"], blob))
            for name, blob in blobs:
                (dest / name).write_bytes(blob)
                log(f"  saved {name}")
        else:
            (dest / item["save_as"]).write_bytes(data)
            log(f"  saved {item['save_as']}")
