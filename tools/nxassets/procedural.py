"""Own icons drawn from code (no third-party artwork): waveforms, gate, sweeps, output modes and
the brand mark of the splash. Coordinates use a 24-unit grid like Material Symbols (content
inside 3..21); strokes have round caps and joins.
"""

import math


def _stroke(d, pts, u, width):
    p = [(x * u, y * u) for x, y in pts]
    w = width * u
    d.line(p, fill=255, width=max(1, round(w)), joint="curve")
    r = w / 2
    for x, y in (p[0], p[-1]):
        d.ellipse((x - r, y - r, x + r, y + r), fill=255)


def _curve(f, x0, x1, n=96):
    return [(x0 + (x1 - x0) * i / n, f(i / n)) for i in range(n + 1)]


def _arrow_head(d, tip, angle, u, width, length=4.5):
    pts = []
    for side in (+1, -1):
        a = angle + math.pi - side * math.radians(40)
        pts.append((tip[0] + length * math.cos(a), tip[1] + length * math.sin(a)))
    _stroke(d, [pts[0], tip, pts[1]], u, width)


def _sine(d, u, w):
    _stroke(d, _curve(lambda t: 12 - 6 * math.sin(2 * math.pi * t), 3, 21), u, w)


def _square(d, u, w):
    _stroke(d, [(3, 18), (3, 6), (12, 6), (12, 18), (21, 18), (21, 6)], u, w)


def _saw(d, u, w):
    _stroke(d, [(3, 18), (12, 6), (12, 18), (21, 6), (21, 18)], u, w)


def _inv_saw(d, u, w):
    _stroke(d, [(3, 6), (12, 18), (12, 6), (21, 18), (21, 6)], u, w)


def _triangle(d, u, w):
    _stroke(d, [(3, 17), (7.5, 7), (12, 17), (16.5, 7), (21, 17)], u, w)


def _damped_sine(d, u, w):
    _stroke(d, _curve(lambda t: 12 - 6.5 * math.exp(-2.2 * t) * math.sin(5 * math.pi * t), 3, 21), u, w)


def _damped_square(d, u, w):
    pts, x, amp, sign = [(3, 12)], 3.0, 6.0, -1
    for _ in range(5):
        pts += [(x, 12 + sign * amp), (x + 3.6, 12 + sign * amp)]
        x += 3.6
        amp *= 0.62
        sign = -sign
    pts.append((21, 12))
    _stroke(d, pts, u, w)


def _hbomb(d, u, w):
    pts = [(3, 17)]
    for x in (6, 12, 18):
        pts += [(x - 1.5, 17), (x, 5), (x + 1.5, 17)]
    pts.append((21, 17))
    _stroke(d, pts, u, w)


def _custom(d, u, w):
    _stroke(d, [(3, 15), (6, 8), (9, 13), (12, 5), (15, 17), (18, 9), (21, 12)], u, w)


def _gate(d, u, w):
    burst = lambda x0: _curve(lambda t: 12 - 5 * math.sin(4 * math.pi * t), x0, x0 + 6, 48)  # noqa: E731
    _stroke(d, [(3, 12), (5, 12)] + burst(5) + [(13, 12)] + burst(13)[1:-1] + [(19, 12), (21, 12)], u, w)


def _sweep_lin(d, u, w):
    _stroke(d, [(4, 19), (19, 5)], u, w)
    _arrow_head(d, (19, 5), math.atan2(5 - 19, 19 - 4), u, w)


def _sweep_log(d, u, w):
    f = lambda t: 19 - 14 * math.log10(1 + 9 * t)  # noqa: E731
    pts = _curve(f, 4, 19)
    _stroke(d, pts, u, w)
    (xa, ya), (xb, yb) = pts[-4], pts[-1]
    _arrow_head(d, (xb, yb), math.atan2(yb - ya, xb - xa), u, w)


def _out_sync(d, u, w):
    for yc in (7.5, 16.5):
        _stroke(d, _curve(lambda t, yc=yc: yc - 3.2 * math.sin(2 * math.pi * t), 3, 21), u, w)


def _out_inverse(d, u, w):
    for yc, s in ((7.5, 1), (16.5, -1)):
        _stroke(d, _curve(lambda t, yc=yc, s=s: yc - s * 3.2 * math.sin(2 * math.pi * t), 3, 21), u, w)


def _brand(d, u, w):
    r = 9.6 * u
    c = 12 * u
    d.ellipse((c - r, c - r, c + r, c + r), outline=255, width=max(1, round(w * u)))
    _stroke(d, _curve(lambda t: 12 - 3.8 * math.sin(2 * math.pi * t), 6, 18), u, w)


DRAW = {
    "wave_sine": _sine, "wave_square": _square, "wave_saw": _saw, "wave_inv_saw": _inv_saw,
    "wave_triangle": _triangle, "wave_damped_sine": _damped_sine, "wave_damped_square": _damped_square,
    "wave_hbomb": _hbomb, "wave_custom": _custom, "gate": _gate, "sweep_lin": _sweep_lin,
    "sweep_log": _sweep_log, "out_sync": _out_sync, "out_inverse": _out_inverse, "brand": _brand,
}


def draw(name, d, u, stroke):
    """Draw icon `name` onto ImageDraw d; u = pixels per grid unit, stroke in grid units."""
    if name not in DRAW:
        raise KeyError(f"unknown procedural icon {name!r}; known: {', '.join(sorted(DRAW))}")
    DRAW[name](d, u, stroke)
