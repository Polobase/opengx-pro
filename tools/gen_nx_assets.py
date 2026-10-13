#!/usr/bin/env python3
"""Build the nx firmware's UI assets from open sources (docs/nx/fw2-design.md, "UI / UX").

  gen_nx_assets.py fetch     download the pinned third-party fonts (assets/nx/sources.json) into
                             assets/nx/third_party/ (git-ignored) and verify every SHA-256
  gen_nx_assets.py build     render, check and write src/nx/{nx_assets.c,nx_assets.h,
                             nx_str_ids.h}, tests/host/nx/nx_asset_vectors.h and
                             assets/nx/assets.lock.json; prints the budget report
  gen_nx_assets.py check     regenerate in memory; fail if a committed file or the lock differs
  gen_nx_assets.py preview   contact sheet of every font and icon: build/nx-assets/preview.png
  gen_nx_assets.py qr-png    the QR codes of assets/nx/qr.json as pictures for the site:
                             site/src/assets/qr-<id>.png (committed; site/test checks what they say)

The build fails (and writes nothing) on: a WCAG contrast below its minimum after RGB565
quantisation, a string wider than its max_px or with a character its font lacks, a QR code whose
text is not the address shown beside it, or more than NX_ASSET_MAX bytes of tables. Needs Pillow
with FreeType and raqm, and segno for the QR codes (.venv, tools/requirements.txt);
--basic-layout renders without raqm (no OpenType features; digits still tabular by forced
advances), which gives different tables than the committed ones.
"""

import argparse
import hashlib
import json
import platform
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from nxassets import emit, fetch, glyphenc, icons, palette, qr, render, strings  # noqa: E402

NX_ASSET_MAX = 36864
SOFT_BUDGETS = {"fonts": 19 * 1024, "icons": 6656}   # design targets: warnings only
OUT_C = "src/nx/nx_assets.c"
OUT_H = "src/nx/nx_assets.h"
OUT_IDS = "src/nx/nx_str_ids.h"
OUT_VEC = "tests/host/nx/nx_asset_vectors.h"
LOCK = "assets/nx/assets.lock.json"
SOURCES = "assets/nx/sources.json"
QR_SOURCE = "assets/nx/qr.json"
# String tables built into the image, first = reference order. The nx image is English only.
LANGS = ["en"]


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def load_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def environment(basic):
    from PIL import __version__ as pillow_version, features
    env = {"python": platform.python_version(), "pillow": pillow_version,
           "freetype2": features.version("freetype2"), "layout": "basic" if basic else "raqm"}
    for lib in ("raqm", "harfbuzz", "fribidi"):
        env[lib] = features.version(lib) if features.check(lib) else None
    env["segno"] = qr.segno.__version__
    return env


def tool_sha():
    here = Path(__file__).resolve().parent
    files = [here / "gen_nx_assets.py"] + sorted(p for p in (here / "nxassets").glob("*.py")
                                                 if not p.name.startswith("test_"))
    return sha256(b"".join(f.name.encode() + b"\0" + f.read_bytes() for f in files))


def select_vectors(fonts, iconsets, ids, compiled, pal):
    """Goldens for tests/host/nx/test_assets.c: per table the first and last glyph, the first raw
    and the first RLE glyph (and 'g', a-umlaut, ellipsis where present); widths and fits per
    font; a few strings per language; one formatted string; two colours per theme."""
    sel = []
    for cfg, t in fonts + iconsets:
        picks = [0, len(t.records) - 1]
        picks.append(next((i for i, r in enumerate(t.records) if r[1] and not r[0] & glyphenc.RLE_FLAG), 0))
        picks.append(next((i for i, r in enumerate(t.records) if r[0] & glyphenc.RLE_FLAG), 0))
        for c in (0x67, strings.CHAR_CODES["ä"], strings.ELLIPSIS_CODE):
            if t.index(c) is not None:
                picks.append(t.index(c))
        codes = list(range(t.first, t.last + 1)) + t.extras
        for i in dict.fromkeys(picks):
            label = f"{cfg.get('id', t.name)} code 0x{codes[i]:02X}"
            sel.append((t.name, label, codes[i], t.glyphs[i], bool(t.records[i][0] & glyphenc.RLE_FLAG)))
    widths, fits = [], []
    for cfg, t in fonts:
        text_font, own = cfg["chars"] == "text", cfg.get("sample")   # "sample": a font of a few letters
        samples = ([own] if own
                   else ["Hello, World", "Äußerst gründlich… 50 °C", "Il1 O0 (x)", "0123456789"] if text_font
                   else ["0123456789", "12,345.67", "-0.5%", "12a"])
        for s in samples:
            codes = strings.encode_text(s)
            widths.append((t.name, codes, t.width(codes)))
        long = strings.encode_text(own or ("Radionics Manifestation Boost 1.2" if text_font else "123456789.00"))
        full = t.width(long)
        for mx in (full, full - 1, full // 2, 5):
            n, w, ell = t.fit(long, mx, strings.ELLIPSIS_CODE)
            fits.append((t.name, long, mx, n, w, ell))
    strs = []
    for li, lang in enumerate(LANGS):
        extra = next(i for i, c in enumerate(compiled[lang]) if any(b >= 0x80 for b in c))
        for i in dict.fromkeys([0, 1, len(ids) - 1, extra]):
            strs.append((li, strings.c_ident(ids[i]), compiled[lang][i]))
    fi = next(i for i, c in enumerate(compiled["en"]) if any(1 <= b <= strings.MAX_ARGS for b in c))
    args = [b"A", b"BB", b"CCC"]
    fmt = (strings.c_ident(ids[fi]), args, strings.substitute(compiled["en"][fi], args))
    themes, tokens, values = pal
    pals = [(ti, ki, values[th][tok]) for ti, th in enumerate(themes) for ki, tok in enumerate(tokens)
            if ki in (0, len(tokens) - 1)]
    return sel, widths, fits, strs, fmt, pals


def generate(root, basic=False):
    """Everything in memory: (outputs {relpath: text}, lock, report lines, errors)."""
    third = root / load_json(root / SOURCES)["dir"]
    fonts_cfg = load_json(root / "assets/nx/fonts.json")["fonts"]
    icons_cfg = load_json(root / "assets/nx/icons.json")["sets"]
    fonts = [(c, render.render_font(c, third, basic)) for c in fonts_cfg]
    iconsets = [(c, icons.render_iconset(c, third)) for c in icons_cfg]
    qr_cfg = load_json(root / QR_SOURCE)["codes"]
    qrs = [qr.build(c) for c in qr_cfg]

    themes, tokens, values, contrast, errors = palette.build(load_json(root / "assets/nx/palettes.json"))
    pal = (themes, tokens, values)

    sources = {lang: load_json(root / f"assets/nx/strings/{lang}.json") for lang in LANGS}
    try:
        ids, compiled, _ = strings.compile_strings(sources, {c["id"]: t for c, t in fonts})
    except strings.StringError as e:
        return None, None, [], errors + str(e).splitlines()
    for c in qr_cfg:   # the address written beside a code is the one the code holds
        shown = sources["en"].get(c.get("shown_as", ""), {}).get("text")
        if "shown_as" in c and c["text"] != "https://" + str(shown):
            errors.append(f"qr {c['id']}: the code holds {c['text']!r}, the string {c['shown_as']} says {shown!r}")
    flat, pool = strings.string_table([s for lang in LANGS for s in compiled[lang]])
    offsets = {lang: flat[i * len(ids):(i + 1) * len(ids)] for i, lang in enumerate(LANGS)}
    owner = {}
    for lang in LANGS:
        for sid, off in zip(ids, offsets[lang]):
            owner.setdefault(off, f"{lang} {sid}")

    sizes = emit.table_sizes(fonts, iconsets, pal, pool, offsets, qrs)
    total = sum(sizes.values())
    if total > NX_ASSET_MAX:
        errors.append(f"the tables take {total} bytes, more than NX_ASSET_MAX = {NX_ASSET_MAX}")

    outputs = {
        OUT_C: emit.assets_c(fonts, iconsets, pal, LANGS, offsets, pool, owner, qrs),
        OUT_H: emit.assets_h(fonts, iconsets, pal, LANGS, total, NX_ASSET_MAX, qrs),
        OUT_IDS: emit.str_ids_h(ids, compiled["en"]),
        OUT_VEC: emit.vectors_h(*select_vectors(fonts, iconsets, ids, compiled, pal), qrs),
    }

    tables = {t.name: {"bytes": sizes[t.name], "sha256": sha256(t.blob())} for _, t in fonts + iconsets}
    tables["nx_palettes"] = {"bytes": sizes["nx_palettes"], "sha256": sha256(
        b"".join(struct.pack("<H", values[th][tok]) for th in themes for tok in tokens))}
    tables["nx_str_pool"] = {"bytes": sizes["nx_str_pool"], "sha256": sha256(pool)}
    tables["nx_str_off"] = {"bytes": sizes["nx_str_off"], "sha256": sha256(
        b"".join(struct.pack("<H", o) for lang in LANGS for o in offsets[lang]))}
    for q in qrs:
        tables[q.name] = {"bytes": sizes[q.name], "sha256": sha256(q.blob())}
    src_files = [SOURCES, "assets/nx/fonts.json", "assets/nx/icons.json", "assets/nx/palettes.json", QR_SOURCE]
    src_files += [f"assets/nx/strings/{lang}.json" for lang in LANGS]
    src_files += sorted({(third / c["file"]).relative_to(root).as_posix() for c in fonts_cfg})
    src_files += sorted({(third / c[k]).relative_to(root).as_posix()
                         for c in icons_cfg for k in ("font", "codepoints") if k in c})
    lock = {
        "tool": {"sha256": tool_sha()},
        "environment": environment(basic),
        "sources": {f: sha256((root / f).read_bytes()) for f in src_files},
        "tables": tables,
        "outputs": {p: sha256(t.encode()) for p, t in outputs.items()},
        "total_bytes": total,
        "max_bytes": NX_ASSET_MAX,
    }
    return outputs, lock, report(fonts, iconsets, pal, sizes, ids, compiled, contrast, total, qrs), errors


def report(fonts, iconsets, pal, sizes, ids, compiled, contrast, total, qrs=()):
    lines = ["nx assets (bytes in flash)"]
    groups = {"fonts": 0, "icons": 0}
    for cfg, t in fonts:
        lines.append(f"  {cfg['id']:<10} {t.name:<16} {sizes[t.name]:>6}   {len(t.records):>3} glyphs, "
                     f"{t.rle_count():>3} RLE, line {t.height}/{t.baseline}")
        groups["fonts"] += sizes[t.name]
    for cfg, t in iconsets:
        lines.append(f"  icons{cfg['size']:<5} {t.name:<16} {sizes[t.name]:>6}   {len(t.records):>3} icons, "
                     f"{t.rle_count():>3} RLE, {cfg['bpp']} bpp")
        groups["icons"] += sizes[t.name]
    for g, s in groups.items():
        flag = "  over the design target (warning)" if s > SOFT_BUDGETS[g] else ""
        lines.append(f"  {g:<27} {s:>6}   target {SOFT_BUDGETS[g]}{flag}")
    for lang in LANGS:
        own = sum(len(c) + 1 for c in compiled[lang])
        lines.append(f"  strings {lang:<19} {own:>6}   ({len(ids)} strings before sharing)")
    lines.append(f"  strings pool + offsets      {sizes['nx_str_pool'] + sizes['nx_str_off']:>6}")
    themes, tokens, _ = pal
    lines.append(f"  palettes                    {sizes['nx_palettes']:>6}   {len(themes)} themes x {len(tokens)}")
    for q in qrs:
        lines.append(f"  qr {q.name:<24} {sizes[q.name]:>6}   {q.n} x {q.n}, {q.label}")
    lines.append(f"  total                       {total:>6}   NX_ASSET_MAX {NX_ASSET_MAX}")
    worst = {}
    for th, kind, fg, bg, ratio, minimum in contrast:
        if (th, kind) not in worst or ratio < worst[(th, kind)][0]:
            worst[(th, kind)] = (ratio, fg, bg, minimum)
    for (th, kind), (ratio, fg, bg, minimum) in sorted(worst.items()):
        lines.append(f"  contrast {th:<6} {kind:<4} min {ratio:5.2f}:1 ({fg} on {bg}; needs {minimum})")
    return lines


def cmd_build(root, basic):
    outputs, lock, lines, errors = generate(root, basic)
    print("\n".join(lines))
    if errors:
        print("\n".join(["FAILED:"] + errors), file=sys.stderr)
        return 1
    for rel, text in outputs.items():
        (root / rel).parent.mkdir(parents=True, exist_ok=True)
        (root / rel).write_text(text)
    (root / LOCK).write_text(json.dumps(lock, indent=2, sort_keys=True) + "\n")
    print(f"wrote {', '.join(outputs)} and {LOCK}")
    return 0


def cmd_check(root, basic):
    outputs, lock, _, errors = generate(root, basic)
    if errors:
        print("\n".join(["FAILED:"] + errors), file=sys.stderr)
        return 1
    problems = [f"{rel}: differs from a fresh build" for rel, text in outputs.items()
                if not (root / rel).exists() or (root / rel).read_text() != text]
    old = json.loads((root / LOCK).read_text()) if (root / LOCK).exists() else {}
    problems += [f"{LOCK}: '{k}' is stale" for k in ("tool", "sources", "tables", "outputs", "total_bytes",
                                                    "max_bytes") if old.get(k) != lock[k]]
    if problems:
        env_old, env_new = old.get("environment", {}), lock["environment"]
        diff = [f"  {k}: lock {env_old.get(k)} / here {v}" for k, v in env_new.items() if env_old.get(k) != v]
        print("\n".join(["nx assets drifted:"] + problems
                        + (["environment differs (likely cause):"] + diff if diff else [])
                        + ["run `make nx-assets` and commit the results"]), file=sys.stderr)
        return 1
    print(f"nx assets up to date ({lock['total_bytes']} bytes)")
    return 0


def cmd_preview(root, basic):
    from nxassets import preview
    third = root / load_json(root / SOURCES)["dir"]
    tables, lines = {}, []
    for c in load_json(root / "assets/nx/fonts.json")["fonts"]:
        t = tables[c["id"]] = render.render_font(c, third, basic)
        text = c.get("sample") or ("READY 12:34 Größe 50 °C … ‹›▸ Il1 O0" if c["chars"] == "text" else "12,345.67 -0.5%")
        lines.append((c["id"], bytes(b for b in strings.encode_text(text) if t.index(b) is not None)))
    for c in load_json(root / "assets/nx/icons.json")["sets"]:
        t = tables[c["symbol"]] = icons.render_iconset(c, third)
        per_line = max(1, 312 // c["size"])
        n = len(c["icons"])
        lines += [(c["symbol"], bytes(range(i, min(n, i + per_line)))) for i in range(0, n, per_line)]
    _, _, values, _, _ = palette.build(load_json(root / "assets/nx/palettes.json"))
    out = root / "build/nx-assets/preview.png"
    out.parent.mkdir(parents=True, exist_ok=True)
    preview.sheet(tables, lines, values["dark"]).save(out)
    print(f"wrote {out.relative_to(root)}")
    return 0


def cmd_qr_png(root):
    out = root / "site/src/assets"
    out.mkdir(parents=True, exist_ok=True)
    for c in load_json(root / QR_SOURCE)["codes"]:
        qr.save_png(c, out / f"qr-{c['id']}.png")
        print(f"wrote {(out / ('qr-' + c['id'] + '.png')).relative_to(root)}")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["fetch", "build", "check", "preview", "qr-png"])
    ap.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent, help="repository root")
    ap.add_argument("--basic-layout", action="store_true", help="render without raqm (see above)")
    a = ap.parse_args()
    root = a.root.resolve()
    try:
        if a.command == "fetch":
            fetch.fetch(root, SOURCES)
            return 0
        if a.command == "qr-png":
            return cmd_qr_png(root)
        return {"build": cmd_build, "check": cmd_check, "preview": cmd_preview}[a.command](root, a.basic_layout)
    except (fetch.FetchError, render.RenderError, glyphenc.EncodeError, palette.PaletteError, qr.QrError) as e:
        print(f"FAILED: {e}", file=sys.stderr)
        return 1
    except FileNotFoundError as e:
        print(f"FAILED: {e}\n(third-party fonts missing? run `make nx-assets-fetch`)", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
