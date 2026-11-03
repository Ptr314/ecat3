# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
"""
Э. Григ, «Пер Гюнт», ор. 23 (1875): Песня Сольвейг. Переложение для ЯЗС.

Мелодия и гармония - по изданию C.F. Peters (Solvejgs Lied, ор. 23 № 19, ля
минор, общественное достояние). Песня строфическая: вступление, куплет
(ля минор, 4/4, Un poco Andante), припев-вокализ (ля мажор, 3/4, Allegretto con
moto) на бурдоне ля-ми, два такта Tempo I; второй куплет и припев; постлюдия.
Сетка - 12 строк на долю (пунктир 9 + 3, триоли по 4, форшлаги по 1); счет
тактов в партитуре свой (4/4 и 3/4 сменяются), черт тактов в тексте нет.

Оркестровка своя:
- вступление и постлюдия: скрипки с сурдиной на D5, флейта;
- куплет 1: «сопрано» - полоса НЧ (выше ля1 почти синус), удар только в первый тик,
  вибрато через 0.3 с; арфа на 2 и 4, виолончели;
- припев 1: вокализ того же голоса, форшлаги короткими нотами, бурдон ля-ми (D5, НЧ);
- куплет 2: скрипка соло на СЧ, подкладка струнных на D5;
- припев 2: флейта и скрипки октавой ниже с расстройкой на 1/16 полутона.
Ферматы - замедлением темпа на их долю.
"""

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from compose import Song                                     # noqa: E402
from instruments import HF, MF, LF, LOUD, HIT, HOLD          # noqa: E402
from common import chord, name, check_harmony                 # noqa: E402
import midi2mml                                               # noqa: E402

RPB = 12
LENS = midi2mml._lengths(RPB)
SEMI = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def P(s):
    """'F#5' -> номер ноты compose (C4 = 48)"""
    m = re.match(r"([A-G])([#b]?)(\d)$", s)
    return int(m.group(3)) * 12 + SEMI[m.group(1)] + {"": 0, "#": 1, "b": -1}[m.group(2)]


def mml(events):
    """[(нота|None, доли) | ('~', доли) - продолжение лигой | ('g', нота) - форшлаг
    в 1 строку, отнимается у следующей ноты] -> MML"""
    out = []
    steal = 0
    for ev in events:
        if ev[0] == "g":
            x = P(ev[1])
            out.append("o%d %s%s" % (x // 12, name(x), LENS[1]))
            steal += 1
            continue
        what, beats = ev
        rows = int(round(beats * RPB)) - steal
        steal = 0
        parts = midi2mml._split(rows, LENS)
        assert parts, (ev, rows)
        if what is None:
            out += ["r" + LENS[p] for p in parts]
        elif what == "~":
            out += ["^" + LENS[p] for p in parts]
        else:
            x = P(what)
            out.append("o%d %s%s" % (x // 12, name(x), LENS[parts[0]]))
            out += ["^" + LENS[p] for p in parts[1:]]
    return " ".join(out)


def beats_of(events):
    return sum(ev[1] for ev in events if ev[0] != "g")


def ev(text):
    """'E4 2, C4 .5, r 1, ~ 1, g:A4' -> события"""
    out = []
    for tok in [t.strip() for t in text.split(",") if t.strip()]:
        if tok.startswith("g:"):
            out.append(("g", tok[2:]))
            continue
        p, b = tok.split()
        b = eval(b)
        out.append((None if p == "r" else p, b))
    return out


INTRO = ["E4 2, C4 .5, E4 .5, A4 .5, B4 .5", "g:A4, G4 4", "G4 2, E4 .5, G4 .5, C5 .5, D5 .5",
         "B4 1, E5 2, r 1", "B3 1, E4 2, r 1", "r 4", "r 4"]
VERSE = ["r 4", "r 3, E4 1",
         "A4 1, B4 .5, C5 .5, D5 1, E5 .5, F5 .5", "F5 .5, E5 .5, E5 .5, C5 .5, A4 1, A4 .5, C5 .5",
         "C5 .5, B4 .5, B4 .5, G#4 .5, G#4 .25, E4 .75, E4 1", "~ 1, r 2, E4 1",
         "A4 .5, A4 .5, B4 .5, C5 .5, D5 1, E5 .5, F5 .5", "F5 .5, E5 .5, E5 .5, C5 .5, A4 1, A4 .5, C5 .5",
         "C5 .5, B4 .5, B4 .5, G4 .5, G4 .25, E4 .75, E4 1", "~ 1, r 2.5, E4 .5",
         "E4 .5, G#4 .5, B4 .5, C5 .5, A4 1, E5 .5, D#5 .5", "B4 1, D5 .5, C#5 .5, A4 1.5, A4 .5",
         "C5 .5, B4 .5, B4 .5, A4 .5, E5 1, r .5, E4 .5",
         "E4 .5, G#4 .5, B4 .5, C5 .5, A4 1, E5 .5, D#5 .5", "B4 .5, B4 .5, D5 .5, C#5 .5, A4 1.5, A4 .5",
         "C5 1.5, B4 .5, g:C5, B4 1.5, A4 .5", "g:A4, g:G4, A4 3, r 1", "E5 4"]
REFRAIN = ["~ .75, F#5 .25, E5 .75, D5 .25, C#5 .75, D5 .25", "E5 1, E5 2",
           "~ .75, F#5 .25, E5 .75, D5 .25, C#5 .75, D5 .25", "~ 3",
           "~ .5, r .5, F#5 2", "~ .5, E5 .5, g:F#5, E5 .5, C#5 .5, A4 .5, r .5",
           "C#5 .5, B4 .5, g:C#5, B4 .5, G#4 .5, E4 .5, G#4 .5", "A4 .5, C#5 .5, E5 2",
           "~ .5, r .5, G#5 1.5, F#5 .5", "F#5 .5, E5 .5, g:F#5, E5 .5, C#5 .5, A4 .5, r .5",
           "C#5 .5, B4 .5, g:C#5, B4 .5, G#4 .5, E4 .5, G#4 .5",
           "B4 .5, E4 .5, G#4 .5, B4 .5, E4 1/3, G#4 1/3, B4 1/3"]
TEMPO1 = ["A4 2, A5 2", "~ 3, r 1"]
POST = ["E4 2, C4 .5, E4 .5, A4 .5, B4 .5", "g:A4, G4 3, E4 1", "C4 2, C4 .5, E4 .5, A4 1",
        "E4 1, A4 2, r 1", "r 4", "r 4", "r 4"]

# Гармония по половинам тактов 4/4 (или по такту 3/4)
H_INTRO = [["Am", "Am"]] * 5 + [["E", "E"], ["E7", "E7"]]
H_VERSE = [["Am", "Am"], ["Am", "Am"], ["Am", "E"], ["Am", "Am/C"], ["E", "Am"], ["Am", "Am"],
           ["Am", "G"], ["C", "F"], ["G7", "C"], ["C", "C"], ["E7/D", "Am/C"], ["B7", "A7"],
           ["Dm6", "E"], ["E7/D", "Am/C"], ["B7/F", "E7"], ["Am/E", "E7"], ["Am", "Am"], ["Am", "Am"]]
H_REFRAIN = ["E7/A", "A"] * 5 + ["E7/A", "E7/A"]
H_TEMPO1 = [["A", "A"], ["A", "A"]]
H_POST = [["Am", "Am"]] * 4 + [["F", "F"], ["E", "E"], ["Am", "Am"]]


def n(x, ln):
    return "o%d %s%s" % (x // 12, name(x), ln)


def near(pc, ref):
    return min((x for x in range(ref - 6, ref + 7) if x % 12 == pc), key=lambda x: abs(x - ref))


def upper(sym, k):
    """k-я сверху нота аккорда в окне до1-ля1 (без баса)"""
    root, q, _ = chord(sym)
    pcs = sorted(set((root + i) % 12 for i in q))
    xs = sorted(near(pc, 54) for pc in pcs)
    return xs[-1 - min(k, len(xs) - 1)]


def song():
    s = Song("ПЕСНЯ СОЛЬВЕЙГ", "Э. ГРИГ, ПЕР ГЮНТ, ОР. 23", bpm=70,
             rows_per_beat=RPB, beats_per_bar=12, frame_bars=1)
    I = s.instrument
    I("voice", "СОПРАНО", [HOLD | HIT | LOUD | LF, HOLD | LOUD | LF], pitch=[-3, 1, 1, 1, 0],
      vib=(30, 4, 3), meter=(13, 12, 1))
    I("violin", "СКРИПКА", [HOLD | HIT | LOUD | MF, HOLD | LOUD | MF], vib=(20, 5, 3), meter=(13, 12, 1))
    I("mute", "СКРИПКИ", [HOLD | LOUD | MF], vib=(26, 4, 2), meter=(11, 10, 1))
    I("flute", "ФЛЕЙТА", [HOLD | HIT | LOUD | LF, HOLD | LOUD | LF], vib=(24, 5, 2), meter=(13, 12, 1))
    I("vln2", "СКРИПКИ", [HOLD | LF | MF], vib=(22, 5, 3), detune=1, meter=(10, 9, 1))
    I("pad", "СТРУННЫЕ", [HOLD | LF | MF], vib=(30, 4, 2), meter=(9, 8, 1))
    I("harp", "АРФА", [HIT | LF | MF | HF], meter=(10, 0, 8))
    I("cello", "ВИОЛОНЧ.", [HOLD | LOUD | LF], meter=(12, 11, 1))
    I("drone", "БУРДОН", [HOLD | LF], meter=(9, 9, 0))
    I("pizz", "ПИЦЦ.", [HIT | LOUD | LF], meter=(12, 0, 6))

    # Разметка: (имя, такты мелодии, гармония, долей в такте, темп)
    secs = [("intro", INTRO, H_INTRO, 4, 70), ("v1", VERSE, H_VERSE, 4, 70),
            ("r1", REFRAIN, H_REFRAIN, 3, 112), ("t1", TEMPO1, H_TEMPO1, 4, 70),
            ("v2", VERSE, H_VERSE, 4, 70), ("r2", REFRAIN, H_REFRAIN, 3, 112),
            ("t2", TEMPO1, H_TEMPO1, 4, 70), ("post", POST, H_POST, 4, 70)]
    at, hm, row = {}, [], 0
    bar_rows = {}
    for nm, mel, harm, bpb, bpm in secs:
        at[nm] = row
        rows = []
        for bar_text, h in zip(mel, harm):
            evs = ev(bar_text)
            assert abs(beats_of(evs) - bpb) < 1e-6, (nm, bar_text, beats_of(evs))
            rows.append(row)
            if isinstance(h, list):
                hm += [(row, h[0]), (row + bpb * RPB // 2, h[1])]
            else:
                hm.append((row, h))
            row += bpb * RPB
        bar_rows[nm] = rows
    total = row
    ch = {c: [] for c in range(7)}

    def melody(nm, mel):
        return mml([e for b in mel for e in ev(b)])

    def rest_beats(b):
        return mml([(None, b)])

    # --- вступление: скрипки с сурдиной, потом низкое ми и аккорд E7 ---
    ch[0].append("@mute " + mml([e for b in INTRO[:5] for e in ev(b)]) + " " + rest_beats(4) +
                 " @pad " + n(P("E5"), "2") + " " + rest_beats(2))
    ch[1].append(rest_beats(24) + " @pad " + n(P("D5"), "2") + " " + rest_beats(2))
    ch[2].append(rest_beats(24) + " @pad " + n(P("B4"), "2") + " " + rest_beats(2))
    ch[3].append(rest_beats(24) + " @pad " + n(P("G#4"), "2") + " " + rest_beats(2))
    ch[4].append(rest_beats(20) + " @pizz " + n(P("E2"), "4") + " @cello " + n(P("E3"), "2") + " "
                 + rest_beats(1) + " " + n(P("E2"), "2") + " " + rest_beats(2))

    # Аккомпанемент куплета: арфа на 2 и 4 (две ноты), виолончели - корень и квинта
    def verse_acc(harp=True):
        a1, a2, b1, b2 = [], [], [], []
        for h in H_VERSE:
            for half in h:
                root, q, bass = chord(half)
                if harp:
                    a1.append("r4 " + n(upper(half, 0), "4"))
                    a2.append("r4 " + n(upper(half, 1), "4"))
                else:
                    a1.append(n(upper(half, 0), "2"))
                    a2.append("r4 " + n(upper(half, 1), "4"))
                b1.append(n(near((root + q[2]) % 12, 40), "2"))
                b2.append(n(near(bass, 33), "2"))
        return " ".join(a1), " ".join(a2), " ".join(b1), " ".join(b2)

    # --- куплет 1: сопрано, арфа, виолончели ---
    a1, a2, b1, b2 = verse_acc()
    ch[0].append("@voice " + melody("v1", VERSE))
    ch[1].append("@harp " + a1)
    ch[2].append("@harp " + a2)
    ch[3].append("@cello " + b1)
    ch[4].append("@cello " + b2)

    # Припев: бурдон ля-ми, арфа - аккорд на 1 (половинная) и 3
    def refrain_acc(w):
        out = []
        for h in H_REFRAIN:
            x = upper(h, w)
            out.append(n(x, "2") + " " + n(x, "4"))
        return " ".join(out)
    drone_a = " ".join(n(P("A2"), "2.") for _ in H_REFRAIN)
    drone_e = " ".join(n(P("E3"), "2.") for _ in H_REFRAIN)

    def tempo1_acc():
        return ([n(P("C#5"), "1") + " " + n(P("C#5"), "2.") + " " + rest_beats(1)],
                [n(P("E4"), "1") + " " + n(P("E4"), "2.") + " " + rest_beats(1)],
                [n(P("E3"), "1") + " " + n(P("E3"), "2.") + " " + rest_beats(1)],
                [n(P("A2"), "1") + " " + n(P("A2"), "2.") + " " + rest_beats(1)])

    # --- припев 1: вокализ того же голоса ---
    ch[0].append(melody("r1", REFRAIN))
    ch[1].append("@harp " + refrain_acc(0))
    ch[2].append("@harp " + refrain_acc(1))
    ch[3].append("@drone " + drone_e)
    ch[4].append("@drone " + drone_a)
    # Tempo I: голос, аккорд ля мажор у струнных
    t = tempo1_acc()
    ch[0].append(melody("t1", TEMPO1))
    ch[1].append("@pad " + t[0][0])
    ch[2].append("@pad " + t[1][0])
    ch[3].append("@drone " + t[2][0])
    ch[4].append("@drone " + t[3][0])

    # --- куплет 2: скрипка соло, подкладка струнных, арфа ---
    a1, a2, b1, b2 = verse_acc(harp=False)
    ch[0].append("@violin " + melody("v2", VERSE))
    ch[1].append("@pad " + a1)
    ch[2].append("@harp " + a2)
    ch[3].append("@cello " + b1)
    ch[4].append("@cello " + b2)
    # --- припев 2: флейта и скрипки октавой ниже с расстройкой ---
    r2 = melody("r2", REFRAIN)
    ch[0].append("@flute " + r2)
    from common import tp
    ch[1].append("@vln2 " + tp(r2, -12))
    ch[2].append("@harp " + refrain_acc(1))
    ch[3].append("@drone " + drone_e)
    ch[4].append("@drone " + drone_a)
    t = tempo1_acc()
    ch[0].append(melody("t2", TEMPO1))
    ch[1].append("@pad " + t[0][0])
    ch[2].append("@pad " + t[1][0])
    ch[3].append("@drone " + t[2][0])
    ch[4].append("@drone " + t[3][0])

    # --- постлюдия: скрипки с сурдиной, низы, последний аккорд ---
    ch[0].append("@mute " + mml([e for b in POST[:4] for e in ev(b)]) + " " + rest_beats(8)
                 + " @pad " + n(P("A4"), "1"))
    ch[1].append(rest_beats(16) + " @pad " + n(P("C5"), "1") + " " + rest_beats(4) + " " + n(P("E4"), "1"))
    ch[2].append(rest_beats(16) + " @pad " + n(P("A4"), "1") + " " + n(P("G#4"), "2.") + " "
                 + rest_beats(1) + " " + n(P("C4"), "1"))
    ch[3].append(rest_beats(16) + " @cello " + n(P("F3"), "1") + " " + n(P("B2"), "2.") + " "
                 + rest_beats(1) + " " + n(P("A3"), "1"))
    ch[4].append(rest_beats(16) + " @cello " + n(P("F2"), "1") + " @pizz " + n(P("E2"), "4") + " @cello "
                 + n(P("E2"), "2") + " " + rest_beats(1) + " " + n(P("A2"), "1"))

    for c in range(5):
        end = s.voice(c, " ".join(ch[c]), octave=4, length=4)
        assert end == total, (c, end, total)
    s.drums(5, rest_beats(total / RPB), {}, length=4)
    s.drums(6, rest_beats(total / RPB), {}, length=4)

    # Темп: разделы, ферматы - замедлением на свою долю
    for nm, mel, harm, bpb, bpm in secs[1:]:
        s.set_tempo(at[nm], bpm)
    for v in ("v1", "v2"):
        r13 = bar_rows[v][12]
        s.set_tempo(r13 + 2 * RPB, 32)
        s.set_tempo(r13 + 3 * RPB, 70)
        s.set_tempo(bar_rows[v][17], 46)                 # вокализ «А» перед припевом
    for t_ in ("t1", "t2"):
        s.set_tempo(bar_rows[t_][1] + 2 * RPB, 30)        # фермата на ля2
        s.set_tempo(bar_rows[t_][1] + 3 * RPB, 70)
    s.ritard(bar_rows["post"][4], bar_rows["post"][6] - 1, 70, 48, every=RPB)

    T = lambda nm, txt, k=0: s.text(bar_rows[nm][k] + 2, txt)
    T("intro", "ВСТУПЛЕНИЕ: СКРИПКИ С СУРДИНОЙ - СЧ, ПЛАВНАЯ АТАКА D5")
    T("v1", "СОПРАНО: НЧ ПОЧТИ СИНУС, УДАР ТОЛЬКО В ПЕРВЫЙ ТИК, ВИБРАТО")
    T("r1", "ВОКАЛИЗ: ФОРШЛАГИ КОРОТКИМИ НОТАМИ, БУРДОН ЛЯ-МИ")
    T("v2", "СКРИПКА СОЛО НА СЧ, ПОДКЛАДКА СТРУННЫХ")
    T("r2", "ФЛЕЙТА И СКРИПКИ В ОКТАВУ: СКРИПКИ РАССТРОЕНЫ НА 1/16")
    T("post", "ПОСТЛЮДИЯ")
    s.harmony = hm
    s.sections = at
    return s


if __name__ == "__main__":
    s = song()
    s.build()
    print(s.stats)
    for t in s.texts:
        assert len(t) <= 64, len(t)
    print("вне аккорда:", check_harmony(s, s.harmony, channels=[1, 2, 3, 4], every=RPB, label="сольвейг"))
