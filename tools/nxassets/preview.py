"""PNG contact sheets of the generated tables, decoded from the encoded bytes (what the firmware
sees), for a visual check: `gen_nx_assets.py preview` writes build/nx-assets/*.png."""

from PIL import Image

from . import glyphenc, palette


def decode(table, g_index):
    off, w, h, _, _, _ = table.records[g_index]
    if not w * h:
        return []
    data = table.data[off & glyphenc.OFF_MASK:]
    if off & glyphenc.RLE_FLAG:
        return glyphenc.rle_decode(data, w * h)
    return glyphenc.raw_decode(data, w * h, table.bpp)


def _blend(fg, bg, a):
    return tuple((f * a + b * (15 - a) + 7) // 15 for f, b in zip(fg, bg))


def draw_text(img, table, x, y, codes, fg, bg):
    """Pen at x, line top at y; returns the pen position after the text."""
    for c in codes:
        i = table.index(c)
        if i is None:
            i = table.index(0x3F)
            if i is None:
                continue
        _, w, h, xo, yo, adv = table.records[i]
        px = decode(table, i)
        for yy in range(h):
            for xx in range(w):
                a = px[yy * w + xx]
                if a:
                    X, Y = x + xo + xx, y + yo + yy
                    if 0 <= X < img.width and 0 <= Y < img.height:
                        img.putpixel((X, Y), _blend(fg, bg, a))
        x += adv
    return x


def sheet(tables, lines, colours, scale=3):
    """lines: [(table name, codes)], one per row, in text1 on bg of the given palette; at least as
    wide as the panel (320 px), wider when a line needs it."""
    fg = palette.expand565(colours["text1"])
    bg = palette.expand565(colours["bg"])
    height = sum(tables[name].height + 4 for name, _ in lines) + 8
    width = max([320] + [tables[name].width(codes) + 8 for name, codes in lines])
    img = Image.new("RGB", (width, height), bg)
    y = 4
    for name, codes in lines:
        draw_text(img, tables[name], 4, y, codes, fg, bg)
        y += tables[name].height + 4
    return img.resize((img.width * scale, img.height * scale), Image.Resampling.NEAREST)
