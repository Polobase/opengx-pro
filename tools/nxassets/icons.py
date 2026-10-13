"""Rasterise the icon sets: Material Symbols Rounded glyphs through the variable font's axes
(FILL, GRAD, opsz, wght) and the procedural icons (procedural.py) drawn at 8x and box-filtered.
An icon set is stored like a font whose codes are the icon indices (nx_asset_fmt.h)."""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

from . import glyphenc, procedural
from .render import RenderError, crop

SUPERSAMPLE = 8
_AXIS_TAGS = {b"Fill": "FILL", b"Grade": "GRAD", b"Optical Size": "opsz", b"Weight": "wght"}


def load_codepoints(path):
    """The font's `name codepoint` list -> {name: codepoint}."""
    table = {}
    for line in Path(path).read_text().splitlines():
        if line.strip():
            name, cp = line.split()
            table[name] = int(cp, 16)
    return table


def render_material(font_path, codepoint, size, axes, bpp):
    """One Material Symbol as a size x size alpha list; axes: {FILL, GRAD, opsz, wght}."""
    font = ImageFont.truetype(str(font_path), size, layout_engine=ImageFont.Layout.BASIC)
    order = [_AXIS_TAGS[a["name"]] for a in font.get_variation_axes()]
    font.set_variation_by_axes([axes[t] for t in order])
    pad = size // 2
    im = Image.new("L", (size + 2 * pad, size + 2 * pad), 0)
    # the icon box is the em square standing on the baseline
    ImageDraw.Draw(im).text((pad, pad + size), chr(codepoint), font=font, fill=255, anchor="ls")
    full = glyphenc.quantize(im.get_flattened_data(), bpp)
    width = im.width
    for y in range(im.height):
        for x in range(width):
            if full[y * width + x] and not (pad <= x < pad + size and pad <= y < pad + size):
                raise RenderError(f"icon U+{codepoint:04X} has ink outside its {size} px box")
    return [full[(pad + y) * width + pad + x] for y in range(size) for x in range(size)]


def render_procedural(name, size, stroke, bpp):
    """A procedural icon (procedural.DRAW) as a size x size alpha list."""
    big = size * SUPERSAMPLE
    im = Image.new("L", (big, big), 0)
    procedural.draw(name, ImageDraw.Draw(im), big / 24.0, stroke)
    small = im.resize((size, size), Image.Resampling.BOX)
    return glyphenc.quantize(small.get_flattened_data(), bpp)


def render_iconset(cfg, src_dir):
    """cfg: one entry of assets/nx/icons.json "sets"; returns a glyphenc.FontTable."""
    size, bpp = cfg["size"], cfg["bpp"]
    codepoints = None
    glyphs = {}
    for i, icon in enumerate(cfg["icons"]):
        if "material" in icon:
            if codepoints is None:
                codepoints = load_codepoints(Path(src_dir) / cfg["codepoints"])
            if icon["material"] not in codepoints:
                raise RenderError(f"{cfg['symbol']}: no Material Symbol named {icon['material']!r}")
            axes = dict(cfg["axes"], FILL=icon.get("fill", cfg["axes"]["FILL"]))
            box = render_material(Path(src_dir) / cfg["font"], codepoints[icon["material"]], size, axes, bpp)
        else:
            box = render_procedural(icon["procedural"], size, cfg["stroke"], bpp)
        px, x0, y0, w, h = crop(box, size, size)
        glyphs[i] = glyphenc.Glyph(i, w, h, x0, y0, size, px)
    return glyphenc.build_table(cfg["symbol"], glyphs, size, size, bpp, (0, len(cfg["icons"]) - 1))
