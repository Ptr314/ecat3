# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
"""
Помощники партитур: аккорды по символам, ноты в текст MML, арпеджио,
шагающий бас. Нота - число полутонов от C0 (C4 = 48, A4 = 57).
"""

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

NAMES = ["c", "c#", "d", "d#", "e", "f", "f#", "g", "g#", "a", "a#", "b"]
PC = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}

QUALITY = {
    "": [0, 4, 7], "m": [0, 3, 7], "7": [0, 4, 7, 10], "m7": [0, 3, 7, 10],
    "maj7": [0, 4, 7, 11], "dim": [0, 3, 6], "dim7": [0, 3, 6, 9], "m7b5": [0, 3, 6, 10],
    "sus4": [0, 5, 7], "sus2": [0, 2, 7], "6": [0, 4, 7, 9], "m6": [0, 3, 7, 9],
    "9": [0, 4, 7, 10, 14], "aug": [0, 4, 8], "5": [0, 7], "add9": [0, 4, 7, 14],
    "7b9": [0, 4, 7, 10, 13], "m9": [0, 3, 7, 10, 14], "maj9": [0, 4, 7, 11, 14],
    "13": [0, 4, 7, 10, 14, 21], "7#9": [0, 4, 7, 10, 15], "m11": [0, 3, 7, 10, 14, 17],
}


def chord(sym):
    """'Am7' -> (корень pc, интервалы), 'C/E' - бас отдельно"""
    m = re.match(r"([A-G])([#b]?)(.*?)(?:/([A-G][#b]?))?$", sym)
    root = PC[m.group(1)] + (1 if m.group(2) == "#" else -1 if m.group(2) == "b" else 0)
    q = QUALITY[m.group(3)]
    bass = None
    if m.group(4):
        b = m.group(4)
        bass = PC[b[0]] + (1 if b[1:] == "#" else -1 if b[1:] == "b" else 0)
    return root % 12, q, (bass if bass is not None else root) % 12


def near(pc, ref):
    """Нота класса pc ближе всего к ref"""
    best = None
    for n in range(ref - 7, ref + 7):
        if n % 12 == pc % 12:
            if best is None or abs(n - ref) < abs(best - ref):
                best = n
    return best


def voicing(sym, top, n=3):
    """n нот аккорда под top (сверху вниз), плотно"""
    root, q, _ = chord(sym)
    pcs = [(root + i) % 12 for i in q]
    out = []
    cur = top
    while len(out) < n:
        if cur % 12 in pcs and cur not in out:
            out.append(cur)
        cur -= 1
    return sorted(out)


def name(n):
    return NAMES[n % 12]


def mml(notes, length=8):
    """[(нота или None, длина)] -> MML с явными октавами"""
    out = []
    for n, ln in notes:
        if n is None:
            out.append("r%s" % ln)
        else:
            out.append("o%d%s%s" % (n // 12, name(n), ln))
    return " ".join(out)


def arpeggio(syms, top, pattern, length=16, octaves=1):
    """Каждый аккорд - по такту, нота за нотой по pattern (индексы снизу)"""
    out = []
    for s in syms:
        v = voicing(s, top, 4 if len(chord(s)[1]) > 3 else 3)
        ext = v + [x + 12 for x in v] + [x + 24 for x in v]
        out.append(" ".join("o%d%s%d" % (ext[i] // 12, name(ext[i]), length) for i in pattern))
        out.append("|")
    return " ".join(out)


def tp(text, semis, octave=4):
    """Фрагмент MML (с октавы octave) на semis полутонов, ноты с явными октавами"""
    import compose
    toks = compose.expand_repeats(compose.tokenize(text))
    out = []
    o = octave

    def ln(v):
        return "" if not v else "%d%s" % (v[0], "." * v[1])
    for kind, val in toks:
        if kind == "oct":
            o = val
        elif kind == "up":
            o += 1
        elif kind == "down":
            o -= 1
        elif kind == "note":
            (legato, pitch), l = val
            letter, acc = pitch
            n = o * 12 + compose.NOTE_BASE[letter] + acc + semis
            out.append("o%d %s%s%s" % (n // 12, "_" if legato else "", NAMES[n % 12], ln(l)))
        elif kind == "rest":
            out.append("r" + ln(val[1]))
        elif kind == "len":
            out.append("l%d%s" % (val[0], "." * val[1]))
        elif kind == "tie":
            out.append("^" + ln(val))
        elif kind == "bar":
            out.append("|")
        else:
            raise ValueError("tp: %s" % kind)
    return " ".join(out)


def dshift(text, steps, key, octave=4):
    """Фрагмент MML на steps ступеней гаммы key (классы 0-11 по возрастанию):
    терции, сексты для второго голоса"""
    import compose
    toks = compose.expand_repeats(compose.tokenize(text))
    out = []
    o = octave

    def ln(v):
        return "" if not v else "%d%s" % (v[0], "." * v[1])
    for kind, val in toks:
        if kind == "oct":
            o = val
        elif kind == "up":
            o += 1
        elif kind == "down":
            o -= 1
        elif kind == "note":
            (legato, pitch), l = val
            letter, acc = pitch
            n = o * 12 + compose.NOTE_BASE[letter] + acc
            # ближайшая ступень гаммы
            deg = min(range(len(key)), key=lambda i: min((n - key[i]) % 12, (key[i] - n) % 12))
            octs = (n - key[deg]) // 12
            d = deg + steps
            m = (octs + d // len(key)) * 12 + key[d % len(key)]
            out.append("o%d %s%s%s" % (m // 12, "_" if legato else "", NAMES[m % 12], ln(l)))
        elif kind == "rest":
            out.append("r" + ln(val[1]))
        elif kind == "len":
            out.append("l%d%s" % (val[0], "." * val[1]))
        elif kind == "tie":
            out.append("^" + ln(val))
        elif kind == "bar":
            out.append("|")
        elif kind in ("instr", "fx", "gate"):
            pass
        else:
            raise ValueError("dshift: %s" % kind)
    return " ".join(out)


def harmonize(song, text, harmony, key, start_row, octave=4, lo=3, hi=9):
    """Второй голос под мелодией: на тоне аккорда - ближайший тон аккорда на
    lo..hi полутонов ниже, на проходящей - диатоническая терция вниз"""
    import compose
    toks = compose.expand_repeats(compose.tokenize(text))
    marks = sorted(harmony)
    out = []
    o = octave
    row = start_row
    deflen = song.length(4, 0)

    def ln(v):
        return "" if not v else "%d%s" % (v[0], "." * v[1])

    def chord_at(r):
        sym = None
        for mr, s in marks:
            if mr <= r:
                sym = s
        return sym
    for kind, val in toks:
        if kind == "oct":
            o = val
        elif kind == "up":
            o += 1
        elif kind == "down":
            o -= 1
        elif kind == "len":
            deflen = song.length(*val)
            out.append("l%d%s" % (val[0], "." * val[1]))
        elif kind == "note":
            (legato, pitch), l = val
            letter, acc = pitch
            n = o * 12 + compose.NOTE_BASE[letter] + acc
            sym = chord_at(row)
            m = None
            if sym and sym != "-":
                root, q, _ = chord(sym)
                pcs = set((root + i) % 12 for i in q)
                if n % 12 in pcs:
                    cands = [x for x in range(n - hi, n - lo + 1) if x % 12 in pcs]
                    if cands:
                        m = max(cands)
            if m is None:
                deg = min(range(len(key)), key=lambda i: min((n - key[i]) % 12, (key[i] - n) % 12))
                octs = (n - key[deg]) // 12
                d = deg - 2
                m = (octs + d // len(key)) * 12 + key[d % len(key)]
            out.append("o%d %s%s%s" % (m // 12, "_" if legato else "", NAMES[m % 12], ln(l)))
            row += song.length(*l) if l else deflen
        elif kind == "rest":
            out.append("r" + ln(val[1]))
            row += song.length(*val[1]) if val[1] else deflen
        elif kind == "tie":
            out.append("^" + ln(val))
            row += song.length(*val)
        elif kind == "bar":
            out.append("|")
        elif kind in ("instr", "fx", "gate"):
            pass
        else:
            raise ValueError("harmonize: %s" % kind)
    return " ".join(out)


def oompah(harm, bass_low=38, top=69, beats=3, rows_per_beat=4):
    """Вальс: бас на 1, аккорд на 2 и 3 (два голоса); -> (бас, верх, низ)"""
    b, u, l = [], [], []
    prev = bass_low + 4
    for sym in harm:
        root, q, bs = chord(sym)
        n = near(bs, prev)
        if n < bass_low - 2:
            n += 12
        if n > bass_low + 10:
            n -= 12
        prev = n
        v = voicing(sym, top, 2)
        b.append("o%d%s4 r4 r4 |" % (n // 12, NAMES[n % 12]))
        u.append("r4 o%d%s4 o%d%s4 |" % (v[1] // 12, NAMES[v[1] % 12], v[1] // 12, NAMES[v[1] % 12]))
        l.append("r4 o%d%s4 o%d%s4 |" % (v[0] // 12, NAMES[v[0] % 12], v[0] // 12, NAMES[v[0] % 12]))
    return " ".join(b), " ".join(u), " ".join(l)


def sounding(song, ch):
    """{строка: нота} - какая нота звучит в канале на каждой строке"""
    out = {}
    cur = None
    for r in range(song.end_row + 1):
        for c in song.rows[ch].get(r, []):
            if c[0] < 0x60:
                cur = c[0]
            elif c[0] == 0x63:
                cur = c[1]
            elif c[0] in (0x60, 0x61):
                cur = None
        if cur is not None:
            out[r] = cur
    return out


def check_harmony(song, harmony, channels=range(5), allow=(), every=None, label=""):
    """harmony: [(строка, 'Am'), ...] по возрастанию. На каждой доле (every строк)
    печатает ноты каналов, которых нет в аккорде; allow - лишние классы (0-11)"""
    every = every or song.rpb
    snd = {c: sounding(song, c) for c in channels}
    marks = sorted(harmony)
    bad = 0
    for r in range(0, song.end_row, every):
        sym = None
        for row, s in marks:
            if row <= r:
                sym = s
        if sym is None or sym == "-":
            continue
        root, q, bass = chord(sym)
        pcs = set((root + i) % 12 for i in q) | set(allow) | {bass}
        for c in channels:
            n = snd[c].get(r)
            if n is not None and n % 12 not in pcs:
                bad += 1
                print("%s такт %d доля %.2f канал %d: %s%d не в %s" % (
                    label, r // song.bar_rows, (r % song.bar_rows) / float(song.rpb) + 1,
                    c + 1, NAMES[n % 12], n // 12, sym))
    return bad


def walking(syms, low=36, beats=4):
    """Шагающий бас четвертями: корень, тона аккорда, подход к следующему.
    Такт со списком из двух аккордов - по половине: корень и подход"""
    out = []
    flat = [x for s in syms for x in (s if isinstance(s, list) else [s])]
    k = 0
    prev = low + 5
    for s in syms:
        hs = s if isinstance(s, list) else [s]
        line = []
        for sym in hs:
            k += 1
            root, q, bass = chord(sym)
            nxt = chord(flat[k % len(flat)])[2]
            r = near(bass, prev)
            if r < low:
                r += 12
            if r > low + 12:
                r -= 12
            target = near(nxt, r)
            approach = target - 1 if target > r else target + 1
            if len(hs) == 1:
                third = near(root + q[1], r + 3)
                fifth = near(root + q[2], r + 6)
                line += [r, third, fifth, approach]
            else:
                line += [r, approach]
            prev = r
        out.append(" ".join("o%d%s4" % (n // 12, name(n)) for n in line[:beats]) + " |")
    return " ".join(out)


# --- свинг ---

_SW_TOK = re.compile(r"\{[^}]*\}|\[|\]\d*|o\d(?=[a-gr_])|[^\s\[\]{}]+")
_SW_NOTE = re.compile(r"^(_?)([a-g][#+-]?|r|\^)(\d+)(\.?)$")
_SW_HIT = re.compile(r"^([A-Za-z])(\d+)(\.?)$")


def _expand(tokens):
    out, stack = [], [[]]
    for t in tokens:
        if t == "[":
            stack.append([])
        elif t.startswith("]"):
            body = stack.pop()
            stack[-1].extend(body * int(t[1:] or 1))
        else:
            stack[-1].append(t)
    return stack[0]


def shuffle(text, drums=False, rpb=3):
    """Восьмые «8» - свингом 2:1: на доле 2/3 доли («6»), после - 1/3 («12»).
    rpb - строк на долю (кратно 3). Остальные длительности - как есть; длина у
    каждой ноты явно. drums: буквы набора ударных вместо нот"""
    whole = 4 * rpb
    pos = 0
    out = []
    for t in _expand(_SW_TOK.findall(text)):
        m = _SW_HIT.match(t) if drums else _SW_NOTE.match(t)
        if drums and m and t[0] == "l":
            m = None
        if not m:
            out.append(t)
            continue
        if drums:
            head, ln, dot = m.group(1), m.group(2), m.group(3)
        else:
            head, ln, dot = m.group(1) + m.group(2), m.group(3), m.group(4)
        if ln == "8" and not dot:
            if pos % rpb == 0:
                ln, rows = "6", 2 * rpb // 3
            elif pos % rpb == 2 * rpb // 3:
                ln, rows = "12", rpb // 3
            else:
                raise ValueError("восьмая свинга не на месте: %r" % text[:60])
        else:
            rows = whole / float(ln) * (1.5 if dot else 1)
            if rows != int(rows):
                raise ValueError("длительность %s%s не в сетке" % (ln, dot))
            rows = int(rows)
        pos += rows
        out.append(head + ln + dot)
    return " ".join(out)

