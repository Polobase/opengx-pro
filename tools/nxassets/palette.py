"""Theme palettes: RGB565 conversion and WCAG 2.x contrast checks after quantisation.

The panel shows RGB565, so the checks use the colour the panel really shows: each channel
expanded back to 8 bits by bit replication (what the ST7789 does for 65k colours).
"""

import re

TEXT_MIN = 4.5   # WCAG 1.4.3 normal text
UI_MIN = 3.0     # WCAG 1.4.11 non-text: focus ring, bar fill against its track


class PaletteError(ValueError):
    pass


def parse_hex(s):
    m = re.fullmatch(r"#([0-9A-Fa-f]{6})", s)
    if not m:
        raise PaletteError(f"bad colour {s!r} (want #RRGGBB)")
    v = int(m.group(1), 16)
    return (v >> 16) & 255, (v >> 8) & 255, v & 255


def rgb565(rgb):
    r, g, b = rgb
    return ((r * 31 + 127) // 255) << 11 | ((g * 63 + 127) // 255) << 5 | ((b * 31 + 127) // 255)


def expand565(v):
    r5, g6, b5 = (v >> 11) & 31, (v >> 5) & 63, v & 31
    return (r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2)


def rel_luminance(rgb):
    def lin(c):
        c /= 255
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    r, g, b = (lin(c) for c in rgb)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def contrast(a, b):
    la, lb = rel_luminance(a), rel_luminance(b)
    hi, lo = max(la, lb), min(la, lb)
    return (hi + 0.05) / (lo + 0.05)


def build(doc):
    """doc: parsed palettes.json. Returns (themes, tokens, values, report, errors) where
    values[theme][token] is RGB565 and report rows are (theme, kind, fg, bg, ratio, minimum)."""
    tokens = doc["tokens"]
    if len(set(tokens)) != len(tokens):
        raise PaletteError("duplicate token")
    themes = list(doc["themes"])
    values = {}
    for th in themes:
        spec = doc["themes"][th]
        missing = [t for t in tokens if t not in spec]
        extra = [t for t in spec if t not in tokens]
        if missing or extra:
            raise PaletteError(f"theme {th}: missing {missing}, unknown {extra}")
        values[th] = {t: rgb565(parse_hex(spec[t])) for t in tokens}
    report, errors = [], []
    for kind, minimum in (("text", TEXT_MIN), ("ui", UI_MIN)):
        for fg, bg in doc["checks"][kind]:
            if fg not in tokens or bg not in tokens:
                raise PaletteError(f"check {fg}/{bg}: unknown token")
            for th in themes:
                ratio = contrast(expand565(values[th][fg]), expand565(values[th][bg]))
                report.append((th, kind, fg, bg, ratio, minimum))
                if ratio < minimum:
                    errors.append(f"{th}: {fg} on {bg} = {ratio:.2f}:1 < {minimum}:1 ({kind})")
    return themes, tokens, values, report, errors
