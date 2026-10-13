"""Glyph bitmap encoding shared by fonts and icon sets (layout: src/nx/nx_asset_fmt.h).

Pure Python (no Pillow): pixels are lists of alpha values 0..15, row-major.
"""

import struct
from dataclasses import dataclass, field

RLE_FLAG = 0x8000
OFF_MASK = 0x7FFF
ALPHA_MAX = 15
RECORD = struct.Struct("<HBBbBBB")   # off|RLE, w, h, x_off, y_off, advance, reserved (nx_glyph_t)
HEADER_BYTES = 8 + 3 * 4             # nx_font_t on the Cortex-M0+: 8 bytes + 3 pointers


class EncodeError(ValueError):
    pass


def quantize(alpha8, bpp):
    """8-bit coverage -> alpha 0..15 at the given depth (2 bpp keeps 0, 5, 10, 15). At 4 bpp the
    levels 1 and 14 snap to 0 and 15: not visible on the panel, but boxes get tighter and RLE
    runs longer (about 4 % fewer font bytes)."""
    if bpp not in (2, 4):
        raise EncodeError(f"bpp must be 2 or 4, not {bpp}")
    levels = (1 << bpp) - 1
    step = ALPHA_MAX // levels
    snap = {1: 0, 14: ALPHA_MAX} if bpp == 4 else {}
    out = []
    for a in alpha8:
        q = ((a * levels + 127) // 255) * step
        out.append(snap.get(q, q))
    return out


def pack_raw(px, bpp):
    """Pack alpha values (multiples of 5 for 2 bpp) MSB first, no row padding."""
    per_byte = 8 // bpp
    step = ALPHA_MAX // ((1 << bpp) - 1)
    out = bytearray((len(px) + per_byte - 1) // per_byte)
    for i, a in enumerate(px):
        if a % step:
            raise EncodeError(f"alpha {a} is not representable at {bpp} bpp")
        shift = 8 - bpp * (i % per_byte + 1)
        out[i // per_byte] |= (a // step) << shift
    return bytes(out)


def raw_decode(data, n, bpp):
    per_byte = 8 // bpp
    mask = (1 << bpp) - 1
    step = ALPHA_MAX // mask
    return [((data[i // per_byte] >> (8 - bpp * (i % per_byte + 1))) & mask) * step for i in range(n)]


def rle_encode(px):
    """Nibble RLE: 0,n / F,n = n+1 transparent / opaque pixels, 1..E = one pixel of that alpha."""
    nib = []
    i = 0
    while i < len(px):
        a = px[i]
        if a in (0, ALPHA_MAX):
            j = i
            while j < len(px) and px[j] == a and j - i < 16:
                j += 1
            nib += [a, j - i - 1]
            i = j
        else:
            nib.append(a)
            i += 1
    if len(nib) % 2:
        nib.append(0)   # pad nibble: the decoder stops after w*h pixels
    return bytes((nib[k] << 4) | nib[k + 1] for k in range(0, len(nib), 2))


def rle_decode(data, n):
    """Reference decoder (the C decoder in src/nx/nx_font.c must agree)."""
    out = []
    k = 0

    def nibble():
        nonlocal k
        b = data[k // 2]
        v = b >> 4 if k % 2 == 0 else b & 15
        k += 1
        return v

    while len(out) < n:
        c = nibble()
        if c in (0, ALPHA_MAX):
            out += [c] * (nibble() + 1)
        else:
            out.append(c)
    return out[:n]


def encode_best(px, bpp):
    """(bytes, is_rle): the smaller of raw and RLE (raw on a tie: faster to decode)."""
    raw = pack_raw(px, bpp)
    rle = rle_encode(px)
    return (rle, True) if len(rle) < len(raw) else (raw, False)


@dataclass
class Glyph:
    code: int
    w: int
    h: int
    x_off: int      # bitmap left relative to the pen position
    y_off: int      # bitmap top relative to the line top
    advance: int
    px: list = field(default_factory=list)   # w*h alpha values 0..15


@dataclass
class FontTable:
    name: str
    height: int
    baseline: int
    bpp: int
    first: int
    last: int
    extras: list          # sorted codes outside the dense range
    glyphs: list          # Glyph per index: dense first, then extras
    records: list         # (off|RLE, w, h, x_off, y_off, advance) per index
    data: bytes

    def index(self, code):
        if self.first <= code <= self.last:
            return code - self.first
        if code in self.extras:
            return self.last - self.first + 1 + self.extras.index(code)
        return None

    def glyph(self, code):
        i = self.index(code)
        return None if i is None else self.glyphs[i]

    def advance_fb(self, code):
        """Advance of code; a missing code is drawn as '?' (or nothing when the font has none)."""
        g = self.glyph(code) or self.glyph(0x3F)
        return g.advance if g else 0

    def width(self, codes):
        return sum(self.advance_fb(c) for c in codes)

    def ellipsis(self, ellipsis_code):
        if self.glyph(ellipsis_code):
            return bytes([ellipsis_code])
        if self.glyph(0x2E):
            return b"..."
        return b""

    def fit(self, codes, max_px, ellipsis_code):
        """(n, width, ellipsis) exactly as nx_font_fit() in src/nx/nx_font.c."""
        full = self.width(codes)
        if full <= max_px:
            return len(codes), full, 0
        ell = self.width(self.ellipsis(ellipsis_code))
        if ell == 0 or ell > max_px:
            return 0, 0, 0
        n = w = 0
        for i, c in enumerate(codes):
            a = self.advance_fb(c)
            if w + a + ell > max_px:
                break
            w += a
            n = i + 1
        while n > 0 and codes[n - 1] == 0x20:
            n -= 1
            w -= self.advance_fb(0x20)
        return n, w + ell, 1

    def rle_count(self):
        return sum(1 for r in self.records if r[0] & RLE_FLAG)

    def size_bytes(self):
        return HEADER_BYTES + len(self.extras) + RECORD.size * len(self.records) + len(self.data)

    def blob(self):
        """Canonical bytes of the whole table (hashed into assets.lock.json)."""
        head = bytes([self.height, self.baseline, self.bpp, self.first, self.last, len(self.extras), 0, 0])
        recs = b"".join(RECORD.pack(*r, 0) for r in self.records)
        return head + bytes(self.extras) + recs + self.data


def build_table(name, glyphs, height, baseline, bpp, dense):
    """Lay out a font/icon table. glyphs: {code: Glyph}; dense: (first, last), every code of the
    range must be present (first > last: no dense range). Identical bitmaps are stored once."""
    first, last = dense
    for c in range(first, last + 1):
        if c not in glyphs:
            raise EncodeError(f"{name}: dense range {first:#x}..{last:#x} lacks code {c:#x}")
    extras = sorted(c for c in glyphs if not first <= c <= last)
    if len(extras) > 255 or (last - first + 1) + len(extras) > 256:
        raise EncodeError(f"{name}: too many glyphs")
    order = [glyphs[c] for c in range(first, last + 1)] + [glyphs[c] for c in extras]
    data = bytearray()
    seen = {}
    records = []
    for g in order:
        if len(g.px) != g.w * g.h:
            raise EncodeError(f"{name}: glyph {g.code:#x} has {len(g.px)} pixels, expected {g.w}x{g.h}")
        if not (0 <= g.w <= 255 and 0 <= g.h <= 255 and -128 <= g.x_off <= 127
                and 0 <= g.y_off <= 255 and 0 <= g.advance <= 255):
            raise EncodeError(f"{name}: glyph {g.code:#x} metrics out of range")
        if g.y_off + g.h > height:
            raise EncodeError(f"{name}: glyph {g.code:#x} extends below the line box")
        if g.w * g.h == 0:
            records.append((0, 0, 0, g.x_off, 0, g.advance))
            continue
        blob, is_rle = encode_best(g.px, bpp)
        key = (blob, is_rle)
        if key not in seen:
            seen[key] = len(data)
            data += blob
        off = seen[key]
        if off > OFF_MASK:
            raise EncodeError(f"{name}: bitmap data exceeds {OFF_MASK} bytes")
        records.append(((RLE_FLAG if is_rle else 0) | off, g.w, g.h, g.x_off, g.y_off, g.advance))
    return FontTable(name, height, baseline, bpp, first, last, extras, order, records, bytes(data))
