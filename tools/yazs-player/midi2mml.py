# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
"""
MIDI -> текст голосов партитуры (MML compose.py): разбор SMF, сведение дорожек в
одноголосные каналы, квантование на сетку строк.

    events = reduce_voice(notes_by_track, [(8, "top"), (5, "top")], beat0, beat1,
                          transpose=-24, sustain=True)
    text = to_mml(events, rpb=48, bar_rows=96, row0, row1)

Ноты событий - в долях (четвертях) от начала; строки - round(доли * rpb).
"""

import struct

NAMES = ["c", "c#", "d", "d#", "e", "f", "f#", "g", "g#", "a", "a#", "b"]


# --- SMF ---

def _vlq(d, i):
    v = 0
    while True:
        b = d[i]
        i += 1
        v = (v << 7) | (b & 0x7F)
        if not b & 0x80:
            return v, i


def parse_midi(path):
    """-> (тиков на долю, [(мета, ноты)]); нота = (начало, длина в долях, высота MIDI,
    громкость, канал); мета: ("name", s), ("tempo", доля, bpm), ("time", доля, n, d),
    ("prog", доля, программа)"""
    d = open(path, "rb").read()
    if d[:4] != b"MThd":
        raise ValueError("не SMF")
    _fmt, ntr, div = struct.unpack(">HHH", d[8:14])
    if div & 0x8000:
        raise ValueError("SMPTE-время не поддерживается")
    i = 14 + struct.unpack(">I", d[4:8])[0] - 6
    tracks = []
    for _ in range(ntr):
        while d[i:i + 4] != b"MTrk":
            i += 8 + struct.unpack(">I", d[i + 4:i + 8])[0]
        ln = struct.unpack(">I", d[i + 4:i + 8])[0]
        t = d[i + 8:i + 8 + ln]
        i += 8 + ln
        notes, meta, on = [], [], {}
        p, tick, run = 0, 0, 0
        while p < len(t):
            dt, p = _vlq(t, p)
            tick += dt
            st = t[p]
            if st & 0x80:
                p += 1
                run = st
            else:
                st = run
            if st == 0xFF:
                typ = t[p]
                l, p = _vlq(t, p + 1)
                data = t[p:p + l]
                p += l
                if typ == 0x03:
                    meta.append(("name", data.decode("latin-1")))
                elif typ == 0x51:
                    meta.append(("tempo", tick / div, 60e6 / int.from_bytes(data, "big")))
                elif typ == 0x58:
                    meta.append(("time", tick / div, data[0], 2 ** data[1]))
            elif st in (0xF0, 0xF7):
                l, p = _vlq(t, p)
                p += l
            else:
                hi = st & 0xF0
                if hi in (0xC0, 0xD0):
                    if hi == 0xC0:
                        meta.append(("prog", tick / div, t[p]))
                    p += 1
                    continue
                a, b = t[p], t[p + 1]
                p += 2
                key = (st & 0x0F, a)
                if hi == 0x90 and b:
                    on.setdefault(key, []).append((tick, b))
                elif hi == 0x80 or hi == 0x90:
                    if on.get(key):
                        s, v = on[key].pop(0)
                        notes.append((s / div, (tick - s) / div, a, v, st & 0x0F))
        notes.sort()
        tracks.append((meta, notes))
    return div, tracks


def tempo_map(tracks):
    return sorted(m[1:] for meta, _ in tracks for m in meta if m[0] == "tempo")


# --- сведение в одноголосный канал ---

def _pick(chord, how):
    chord = sorted(chord, key=lambda n: n[2])
    if how == "top":
        return chord[-1]
    if how == "bottom":
        return chord[0]
    if isinstance(how, tuple) and how[0] == "rank":      # ("rank", k): k-я сверху
        k = min(how[1], len(chord) - 1)
        return chord[-1 - k]
    if isinstance(how, tuple) and how[0] == "rankx":     # то же, но только в аккорде
        return chord[-1 - how[1]] if len(chord) > how[1] else None
    raise ValueError(how)


def reduce_voice(tracks, sources, beat0, beat1, transpose=0, rpb=48, sustain=False,
                 max_hold=None, lo=None, hi=None):
    """Источники по убыванию важности: [(дорожка, "top"|"bottom"|("rank",k)
    [, свой сдвиг в полутонах]), ...].
    Нота источника берется, если ее начало не занято нотой важнее; конец
    обрезается по следующей взятой ноте. sustain: нота держится до следующей
    (щипковые - звенят), но не дольше max_hold долей. lo/hi - окно высот MIDI:
    ноты вне окна переносятся октавами.
    -> [(строка начала, строка конца, нота compose, дорожка, доля начала)]"""
    taken = []                       # (r0, r1, note, track, beat)
    for src in sources:
        tr, how = src[:2]
        shift = transpose + (src[2] if len(src) > 2 else 0)
        notes = [n for n in tracks[tr][1] if beat0 <= n[0] < beat1]
        groups = {}
        for n in notes:
            groups.setdefault(int(round(n[0] * rpb)), []).append(n)
        for r0 in sorted(groups):
            n = _pick(groups[r0], how)
            if n is None:
                continue
            r1 = int(round((n[0] + n[1]) * rpb))
            if any(a <= r0 < b for a, b, _, _, _ in taken):
                continue
            p = n[2] + shift
            while lo is not None and p < lo:
                p += 12
            while hi is not None and p > hi:
                p -= 12
            taken.append((r0, max(r1, r0 + 1), p - 12, tr, n[0]))
    taken.sort()
    out = []
    end_row = int(round(beat1 * rpb))
    for i, (r0, r1, n, tr, b) in enumerate(taken):
        nxt = taken[i + 1][0] if i + 1 < len(taken) else end_row
        if sustain:
            r1 = nxt if max_hold is None else min(nxt, r0 + int(round(max_hold * rpb)))
        r1 = min(r1, nxt)
        out.append((r0, r1, n, tr, b))
    return out


# --- MML ---

def _lengths(rpb):
    """{строк: «длительность»}: целые и с точкой, которые допускает compose"""
    whole = rpb * 4
    out = {}
    for num in (1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64):
        if whole % num:
            continue
        rows = whole // num
        out.setdefault(rows, str(num))
        if rows % 2 == 0:
            out.setdefault(rows * 3 // 2, "%d." % num)
    return out


def _split(n, lens):
    """n строк - суммой длительностей (меньше слагаемых); None - не разложить"""
    best = {0: []}
    sizes = sorted(lens, reverse=True)
    for k in range(1, n + 1):
        cand = None
        for s in sizes:
            if s <= k and (k - s) in best:
                c = best[k - s] + [s]
                if cand is None or len(c) < len(cand):
                    cand = c
        if cand is not None:
            best[k] = cand
    return best.get(n)


def to_mml(events, rpb, bar_rows, row0, row1, instr=None):
    """События [(r0, r1, нота, ...)] на строках row0..row1 -> MML с явными октавами и
    чертами тактов (row0 - начало такта). instr(событие) -> имя инструмента: перед
    нотой ставится «@имя», когда оно меняется"""
    lens = _lengths(rpb)
    # Границы - к узлам сеток длительностей (при 48 строках на долю: 64-е и
    # секстоли), иначе между ними остаются куски в 1-2 строки
    grids = [g for g in (rpb // 16, rpb // 12) if g and rpb % g == 0] or [1]

    def snap(r):
        return min((round(r / g) * g for g in grids), key=lambda x: abs(x - r))
    segs = []                         # (длина, нота или None, инструмент)
    pos = row0
    for ev in events:
        r0, r1, n = snap(ev[0]), snap(ev[1]), ev[2]
        if r1 <= r0:
            r1 = r0 + min(grids)
        if r1 <= row0 or r0 >= row1:
            continue
        r0, r1 = max(r0, row0), min(r1, row1)
        if r0 > pos:
            segs.append([r0 - pos, None, None])
        if r0 < pos:                  # наложение после квантования
            r0 = pos
            if r1 <= r0:
                continue
        segs.append([r1 - r0, n, instr(ev) if instr else None])
        pos = r1
    if pos < row1:
        segs.append([row1 - pos, None, None])
    # Нераскладываемые длины - сдвинуть границу с соседним отрезком; пауза,
    # которую не спасти, отходит соседу
    ok = lambda n: n > 0 and _split(n, lens) is not None
    i = 0
    while i < len(segs):
        if ok(segs[i][0]):
            i += 1
            continue
        fixed = False
        for j in ((i, i + 1), (i - 1, i)):
            a, b = j
            if a < 0 or b >= len(segs):
                continue
            for d in (1, -1, 2, -2, 3, -3, 4, -4):
                if ok(segs[a][0] + d) and ok(segs[b][0] - d):
                    segs[a][0] += d
                    segs[b][0] -= d
                    fixed = True
                    break
            if fixed:
                break
        if not fixed and segs[i][1] is None and len(segs) > 1:
            k = i - 1 if i > 0 else i + 1
            segs[k][0] += segs[i][0]
            del segs[i]
            i = max(0, i - 1)
            continue
        if not fixed:
            raise ValueError("длительность %d строк не разложить" % segs[i][0])
        i = max(0, i - 1)
    out = []
    pos = row0
    cur = None
    for ln, n, ins in segs:
        first = True
        if n is not None and ins and ins != cur:
            out.append("@" + ins)
            cur = ins
        while ln:
            room = bar_rows - (pos - row0) % bar_rows
            part = min(ln, room)
            pieces = _split(part, lens)
            if pieces is None:
                raise ValueError("кусок %d строк" % part)
            for s in pieces:
                if n is None:
                    out.append("r" + lens[s])
                elif first:
                    out.append("o%d %s%s" % (n // 12, NAMES[n % 12], lens[s]))
                    first = False
                else:
                    out.append("^" + lens[s])
            pos += part
            ln -= part
            if (pos - row0) % bar_rows == 0:
                out.append("|")
    return " ".join(out)


def program_at(tracks, tr, beat):
    """Программа (тембр GM) дорожки на этой доле"""
    p = None
    for m in tracks[tr][0]:
        if m[0] == "prog" and m[1] <= beat + 1e-6:
            p = m[2]
    return p
