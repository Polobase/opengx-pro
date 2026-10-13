"""UI string compiler: UTF-8 JSON sources -> byte strings in the firmware's character codes.

Codes: 0x20..0x7E ASCII; EXTRA_CHARS map to 0x80.. in order; "{0}".."{2}" become the argument
slots 0x01..0x03 (nx_str_format). Every string is measured with the real glyph advances of its
font (same arithmetic as nx_font_text_width) and must fit its max_px.
"""

import re

EXTRA_CHARS = "äöüÄÖÜß°·…–×µ‹›▸"
EXTRA_BASE = 0x80
# NX_CH_<name> defines (HTML entity names), same order as EXTRA_CHARS
EXTRA_NAMES = ["auml", "ouml", "uuml", "Auml", "Ouml", "Uuml", "szlig", "deg", "middot", "hellip",
               "ndash", "times", "micro", "lsaquo", "rsaquo", "rtri"]
CHAR_CODES = {ch: EXTRA_BASE + i for i, ch in enumerate(EXTRA_CHARS)}
ELLIPSIS_CODE = CHAR_CODES["…"]
MAX_ARGS = 3
_ID = re.compile(r"[a-z][a-z0-9_]*(\.[a-z0-9_]+)*")
_ARG = re.compile(r"\{(\d)\}")

assert len(EXTRA_NAMES) == len(EXTRA_CHARS)


class StringError(ValueError):
    pass


def encode_text(text, where="text"):
    """UTF-8 text -> firmware bytes; placeholders {0}..{2} -> 0x01..0x03."""
    out = bytearray()
    i = 0
    while i < len(text):
        ch = text[i]
        if ch == "{":
            m = _ARG.match(text, i)
            if not m or int(m.group(1)) >= MAX_ARGS:
                raise StringError(f"{where}: bad placeholder at {text[i:i + 4]!r} (use {{0}}..{{{MAX_ARGS - 1}}})")
            out.append(1 + int(m.group(1)))
            i = m.end()
            continue
        if " " <= ch <= "~":
            out.append(ord(ch))
        elif ch in CHAR_CODES:
            out.append(CHAR_CODES[ch])
        else:
            raise StringError(f"{where}: unmapped character {ch!r} (U+{ord(ch):04X}); "
                              f"allowed: printable ASCII and {EXTRA_CHARS}")
        i += 1
    return bytes(out)


def c_ident(sid):
    return "NX_S_" + sid.upper().replace(".", "_")


def substitute(codes, args):
    out = bytearray()
    for c in codes:
        if 1 <= c <= MAX_ARGS:
            out += args[c - 1] if c - 1 < len(args) else b""
        else:
            out.append(c)
    return bytes(out)


def compile_strings(sources, fonts):
    """sources: {lang: {id: {text, font, max_px[, args]}}} (first language = reference order);
    fonts: {font id: FontTable}. Returns (ids, {lang: [bytes per id]}, report rows)."""
    langs = list(sources)
    ref = sources[langs[0]]
    ids = list(ref)
    errors = []
    for sid in ids:
        if not _ID.fullmatch(sid):
            errors.append(f"bad string id {sid!r} (lower case, dots and underscores)")
    for lang in langs[1:]:
        missing = [s for s in ids if s not in sources[lang]]
        extra = [s for s in sources[lang] if s not in ref]
        if missing or extra:
            errors.append(f"{lang}: missing ids {missing}, unknown ids {extra}")
    if errors:
        raise StringError("\n".join(errors))
    compiled = {lang: [] for lang in langs}
    report = []
    for sid in ids:
        font_id, max_px = ref[sid]["font"], ref[sid]["max_px"]
        if font_id not in fonts:
            errors.append(f"{sid}: unknown font {font_id}")
            continue
        table = fonts[font_id]
        slots = None
        for lang in langs:
            e = sources[lang][sid]
            where = f"{lang}:{sid}"
            if e["font"] != font_id or e["max_px"] != max_px:
                errors.append(f"{where}: font/max_px differ from {langs[0]} ({e['font']}/{e['max_px']})")
                continue
            try:
                codes = encode_text(e["text"], where)
            except StringError as err:
                errors.append(str(err))
                continue
            used = sorted({c for c in codes if 1 <= c <= MAX_ARGS})
            if slots is None:
                slots = used
            elif used != slots:
                errors.append(f"{where}: placeholders {used} differ from {langs[0]} {slots}")
            args = [encode_text(a, where) for a in e.get("args", [])]
            if used and len(args) < max(used):
                errors.append(f"{where}: needs sample \"args\" for its placeholders")
                continue
            shown = substitute(codes, args)
            for c in set(shown):
                if table.glyph(c) is None:
                    errors.append(f"{where}: font {font_id} has no glyph for code {c:#04x}")
            width = table.width(shown)
            report.append((lang, sid, font_id, width, max_px))
            if width > max_px:
                errors.append(f"{where}: {e['text']!r} is {width} px wide, max_px {max_px} ({font_id})")
            compiled[lang].append(codes)
    if errors:
        raise StringError("\n".join(errors))
    return ids, compiled, report


def string_table(strings):
    """[bytes] -> (offsets, pool) of NUL-terminated texts. Identical texts are stored once, and a
    text that is the tail of a longer one points into it ("Hz" inside "kHz")."""
    text = bytearray()
    where = {}
    for s in sorted(set(strings), key=lambda b: (-len(b), b)):
        host = next((h for h in where if h.endswith(s)), None)
        if host is not None:
            where[s] = where[host] + len(host) - len(s)
        else:
            where[s] = len(text)
            text += s + b"\0"
    if len(text) > 0xFFFF:
        raise StringError("string table exceeds 64 KB")
    return [where[s] for s in strings], bytes(text)
