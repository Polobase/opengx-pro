"""QR codes of the UI (assets/nx/qr.json): each is built here once, with segno, and stored as bit
rows, so the firmware only draws squares (src/nx/nx_scr_support.c).

Version, error-correction level and mask are fixed in the source file, so the symbol does not
depend on the library's choices: byte mode, no Micro QR, no automatic raise of the level.
"""

from dataclasses import dataclass

import segno

LEVELS = "LMQH"


class QrError(ValueError):
    pass


@dataclass
class QrTable:
    name: str        # C symbol
    ident: str       # NX_QR_<ID>: prefix of its defines
    text: str
    label: str       # "version 3-Q, mask 0"
    n: int           # modules per side
    rows: list       # n rows of n values, 1 = dark

    @property
    def row_bytes(self):
        return (self.n + 7) // 8

    def blob(self):
        """Rows top to bottom, each (n + 7) / 8 bytes, most significant bit first, 1 = dark."""
        out = bytearray()
        for row in self.rows:
            for i in range(0, self.n, 8):
                bits = row[i:i + 8] + [0] * (8 - len(row[i:i + 8]))
                out.append(sum(b << (7 - k) for k, b in enumerate(bits)))
        return bytes(out)

    def size_bytes(self):
        return self.n * self.row_bytes

    def dark(self):
        return sum(map(sum, self.rows))


def build(cfg):
    """One entry of assets/nx/qr.json "codes" -> QrTable."""
    where = f"qr {cfg.get('id', '?')}"
    for key in ("id", "symbol", "text", "version", "error", "mask"):
        if key not in cfg:
            raise QrError(f"{where}: '{key}' is missing")
    if cfg["error"] not in LEVELS or not 0 <= cfg["mask"] <= 7:
        raise QrError(f"{where}: error is one of {LEVELS}, mask 0..7")
    try:
        code = segno.make(cfg["text"], version=cfg["version"], error=cfg["error"].lower(), mode="byte",
                          mask=cfg["mask"], micro=False, boost_error=False)
    except segno.DataOverflowError as e:
        raise QrError(f"{where}: {e}") from e
    rows = [[1 if m else 0 for m in row] for row in code.matrix]
    n = len(rows)
    if n != 17 + 4 * cfg["version"] or any(len(r) != n for r in rows):
        raise QrError(f"{where}: not a version {cfg['version']} symbol")
    return QrTable(cfg["symbol"], "NX_QR_" + cfg["id"].upper(), cfg["text"],
                   f"version {cfg['version']}-{cfg['error']}, mask {cfg['mask']}", n, rows)


def save_png(cfg, path, scale=8):
    """The same symbol as a picture (black on white, the quiet zone of 4 modules), for the site."""
    code = segno.make(cfg["text"], version=cfg["version"], error=cfg["error"].lower(), mode="byte",
                      mask=cfg["mask"], micro=False, boost_error=False)
    code.save(str(path), kind="png", scale=scale, border=4)
