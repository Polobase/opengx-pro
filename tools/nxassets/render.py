"""Rasterise the text and number fonts (Inter, Inter Display) with Pillow/FreeType/raqm.

Every glyph is drawn alone at an integer pen position, quantised (glyphenc.quantize) and cropped
to its ink box. OpenType features (tnum, ss04) need the raqm layout engine; digits get the
widest digit's advance in any case, so numbers stay tabular even without raqm.
"""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont, features

from . import glyphenc, strings

DIGITS = "0123456789"
ASCII = "".join(chr(c) for c in range(0x20, 0x7F))


class RenderError(ValueError):
    pass


def have_raqm():
    return bool(features.check("raqm"))


def _layout(basic):
    return ImageFont.Layout.BASIC if basic else ImageFont.Layout.RAQM


def crop(px, w, h):
    """(cropped pixels, x0, y0, cw, ch) of the non-zero area of a w x h alpha list; empty: w = h = 0."""
    im = Image.new("L", (w, h))
    im.putdata(px)
    bb = im.getbbox()
    if bb is None:
        return [], 0, 0, 0, 0
    x0, y0, x1, y1 = bb
    return list(im.crop(bb).get_flattened_data()), x0, y0, x1 - x0, y1 - y0


def _half_up(v):
    return int(v + 0.5)


def render_char(font, ch, feats, bpp):
    """One character at the pen origin: (pixels, w, h, x_off, top from baseline, advance float)."""
    size = font.size
    w = h = size * 3
    ox, oy = size, size * 2
    kw = {"features": feats} if feats else {}
    im = Image.new("L", (w, h), 0)
    ImageDraw.Draw(im).text((ox, oy), ch, font=font, fill=255, anchor="ls", **kw)
    px, x0, y0, cw, chh = crop(glyphenc.quantize(im.get_flattened_data(), bpp), w, h)
    adv = font.getlength(ch, **kw)
    if not cw:
        return [], 0, 0, 0, 0, adv
    return px, cw, chh, x0 - ox, y0 - oy, adv


def _render_substitute(font, cfg, sub, src_dir, basic, bpp):
    """A character the font lacks, drawn from another of its glyphs scaled down and centred on
    the x-height, with a small side bearing (▸ from ▶)."""
    small = ImageFont.truetype(str(Path(src_dir) / cfg["file"]), max(1, round(cfg["size"] * sub["scale"])),
                               layout_engine=_layout(basic))
    px, w, h, x, top, adv = render_char(small, sub["from"], [], bpp)
    _, _, xh, _, xtop, _ = render_char(font, "x", [], bpp)
    shift = round((xtop + xh / 2) - (top + h / 2))
    side = max(1, round(cfg["size"] * 0.08))
    return px, w, h, x + side, top + shift, adv + 2 * side


def render_font(cfg, src_dir, basic=False):
    """cfg: one entry of assets/nx/fonts.json "fonts"; returns a glyphenc.FontTable.
    basic=True renders without raqm (no OpenType features)."""
    if not basic and not have_raqm():
        raise RenderError("Pillow has no raqm layout engine (OpenType features): reinstall the pinned "
                          "Pillow wheel (tools/requirements.txt) or pass --basic-layout")
    font = ImageFont.truetype(str(Path(src_dir) / cfg["file"]), cfg["size"], layout_engine=_layout(basic))
    feats = [] if basic else cfg.get("features", [])
    bpp = cfg["bpp"]
    unknown = [c for c in cfg.get("extras", "") if c not in strings.CHAR_CODES]
    if unknown:
        raise RenderError(f"{cfg['id']}: extras {unknown} are not in strings.EXTRA_CHARS")
    chars = ASCII + cfg["extras"] if cfg["chars"] == "text" else cfg["chars"]
    subst = cfg.get("substitutes", {})
    rendered = {}
    for ch in chars:
        code = ord(ch) if ord(ch) < 0x7F else strings.CHAR_CODES[ch]
        if ch in subst:
            rendered[code] = _render_substitute(font, cfg, subst[ch], src_dir, basic, bpp)
        else:
            rendered[code] = render_char(font, ch, feats, bpp)
    # tabular digits: every digit gets the widest digit's advance (centred), with or without tnum
    digit_codes = [ord(d) for d in DIGITS if ord(d) in rendered]
    if digit_codes:
        widest = max(_half_up(rendered[c][5]) for c in digit_codes)
        for c in digit_codes:
            px, w, h, x, top, adv = rendered[c]
            rendered[c] = (px, w, h, x + (widest - _half_up(adv)) // 2, top, widest)
    tops = [r[4] for r in rendered.values() if r[2]]
    bottoms = [r[4] + r[2] for r in rendered.values() if r[2]]
    if cfg["line"] == "tight":      # number fonts: the box of their own glyphs
        baseline, below = max(-t for t in tops), max(bottoms)
    else:                           # text fonts: the font's line metrics, grown to fit every glyph
        ascent, descent = font.getmetrics()
        baseline, below = max([ascent] + [-t for t in tops]), max([descent] + bottoms)
    glyphs = {}
    for code, (px, w, h, x, top, adv) in rendered.items():
        glyphs[code] = glyphenc.Glyph(code, w, h, x, baseline + top if h else 0, _half_up(adv), px)
    dense = cfg.get("dense")
    rng = (ord(dense[0]), ord(dense[-1])) if dense else (0x20, 0x7E)
    return glyphenc.build_table(cfg["symbol"], glyphs, baseline + below, baseline, bpp, rng)
