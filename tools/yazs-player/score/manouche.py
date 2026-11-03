# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
"""
ЯЗС-мануш: своя пьеса в стиле джаз-мануш (цыганский свинг), ре минор, 192.

Форма AABA по 8 тактов:
- A: Dm6 | Dm6 | Bb7 | A7 | Dm6 | Gm6 | Em7b5 A7 | Dm6 (A7);
- B: D7 | D7 | Gm6 | Gm6 | E7 | E7 | A7 | A7.
Вступление (помп), тема у скрипки (во втором A гитара октавой ниже, в бридже -
вторым голосом), соло гитары на корус, соло скрипки на A A, тема B A, концовка
- хроматический спуск и аккорд Dm6 с си-бекаром наверху.

Состав:
- скрипка: смычок с ударом, СЧ+ВЧ, вибрато, лиги со скольжением;
- соло-гитара: медиатор - первые тики на всех полосах, затухание, тремоло - триолями;
- ритм-гитара «помп»: аккорд на каждую долю коротким гейтом, на 2 и 4 громче
  (D3), с «чиком» ударного 7;
- контрабас: на 1 и 3 в теме, шагающий в соло.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from compose import Song                                     # noqa: E402
from instruments import HF, MF, LF, LOUD, HIT, HOLD, KIT7    # noqa: E402
from common import chord, name, walking, harmonize, shuffle, tp, check_harmony   # noqa: E402

RPB = 3
D_HARM = [2, 4, 5, 7, 9, 11, 1]           # ре минор с си-бекаром и до-диезом (мелодический)
D_HARM = sorted(D_HARM)
HA1 = ["Dm6", "Dm6", "Bb7", "A7", "Dm6", "Gm6", ["Em7b5", "A7"], ["Dm6", "A7"]]
HA2 = HA1[:7] + ["Dm6"]
HB = ["D7", "D7", "Gm6", "Gm6", "E7", "E7", "A7", "A7"]
FORM = HA1 + HA2 + HB + HA2
INTRO = ["Dm6", "Dm6", "A7", "A7"]
TAG = ["Dm6", "Dm6", "Dm6", "Dm6"]

A1 = ["r8 o4 a8 o5 d8 f8 b4 a4", "o5 a2 g8 f8 e8 d8",
      "o5 d8 f8 a-8 f8 d4 o4 b-4", "o4 a12 b-12 a12 g#8 a8 o5 c#2",
      "o5 d4 f8 e8 d8 o4 b8 a4", "o4 g8 b-8 o5 d8 e8 g4 f8 e8",
      "o5 d4 o4 b-8 g8 o5 c#4 e4", "o5 d2 r4 o4 a4"]
A2 = A1[:7] + ["o5 d2 r2"]
BR = ["o4 f#8 a8 o5 c8 d8 f#4 e8 d8", "o5 c4 o4 a4 f#2",
      "o4 g8 b-8 o5 d8 e8 g2", "o5 f8 e8 d8 c8 o4 b-4 g4",
      "o4 g#8 b8 o5 d8 e8 g#4 f8 e8", "o5 d4 o4 b4 g#2",
      "o4 a12 o5 c#12 e12 g12 e12 c#12 o4 a4 g4", "o4 f8 e8 c#8 e8 a4 r4"]
GUITAR = [
    # A1
    "o4 d12 f12 a12 b12 o5 d12 f12 a4 r4", "o5 g#8 a8 f8 d8 o4 b8 a8 f8 d8",
    "o4 b-12 o5 d12 f12 a-12 f12 d12 o4 b-4 a-4", "o4 g8 a8 b-8 b8 o5 c#4 e4",
    "o5 f4 e8 f8 a4 f4", "o5 e8 d8 o4 b-8 g8 e8 g8 b-8 o5 d8",
    "o5 d8 o4 b-8 g8 e8 c#8 e8 g8 b-8", "o4 a2 r8 e8 f8 g8",
    # A2
    "o4 a8 o5 d8 f8 a8 b4 a4", "o5 d12 d12 d12 d12 d12 d12 f4 d4",
    "o5 f8 a-8 f8 d8 o4 b-8 a-8 f8 d8", "o4 c#12 e12 g12 b-12 g12 e12 c#4 o3 a4",
    "o3 a8 o4 d8 f8 a8 b8 a8 f8 d8", "o4 g4 b-8 o5 d8 e4 d4",
    "o4 b-8 a8 g8 e8 f8 e8 c#8 o3 b-8", "o3 a4 o4 d4 r2",
    # B
    "o4 d12 f#12 a12 o5 c12 d12 f#12 a4 r4", "o5 g8 f#8 e8 d8 c8 o4 a8 f#8 d8",
    "o4 g12 b-12 o5 d12 e12 g12 b-12 a4 g4", "o5 f#8 g8 d8 o4 b-8 g8 b-8 o5 d8 e8",
    "o4 e12 g#12 b12 o5 d12 o4 b12 g#12 e4 d4", "o4 g#8 a8 b8 o5 d8 e8 f8 e8 d8",
    "o5 c#4 e4 g4 b-4", "o5 a8 g8 e8 c#8 o4 b-8 a8 g8 e8",
    # A3
    "o4 f8 a8 o5 d8 f8 a2", "o5 g#8 a8 b8 a8 g#8 a8 f8 d8",
    "o5 a-4 f4 d4 o4 b-4", "o4 a12 b-12 a12 g#12 a12 o5 c#12 e4 g4",
    "o5 f8 e8 d8 o4 b8 a8 f8 d8 f8", "o4 g8 b-8 o5 d8 e8 g4 e4",
    "o5 d8 c#8 o4 b-8 g8 e8 g8 o5 c#8 e8", "o5 d2 r2",
]
VIOLIN = [
    "o5 a2 _b4 a4", "o5 f8 e8 d8 e8 f4 d4", "o5 d4 f4 a-2", "o5 g8 f8 e8 d8 c#4 o4 a4",
    "o4 b8 o5 d8 f8 a8 o6 d4 o5 b4", "o5 b-2 a8 g8 e8 d8", "o5 e4 d4 c#4 e4", "o5 d2 r4 o4 a4",
    "o5 d8 f8 a8 o6 d8 c#8 o5 b8 a8 f8", "o5 e8 f8 g#8 a8 b4 a4", "o5 a-8 g8 f8 d8 o4 b-4 o5 d4",
    "o5 c#2 e4 g4", "o5 f4 e8 d8 a4 f4", "o5 g8 f8 e8 d8 o4 b-8 a8 g8 b-8",
    "o5 d4 o4 b-4 o5 c#4 e4", "o5 d1",
]
ENDING = ["o5 d8 c#8 c8 o4 b8 b-8 a8 g#8 a8", "o4 f8 e8 d#8 d8 c#8 d8 f8 a8",
          "o4 d12 f12 a12 b12 o5 d12 f12 a2", "o5 b4 r4 r2"]


def n(x, ln):
    return "o%d %s%s" % (x // 12, name(x), ln)


def bars(lst):
    return " | ".join(lst) + " |"


def near(pc, ref):
    return min((x for x in range(ref - 6, ref + 7) if x % 12 == pc), key=lambda x: abs(x - ref))


def pair(sym):
    """Две ноты помпа: терция и секста/септима"""
    root, q, _ = chord(sym)
    third = (root + q[1]) % 12
    top = (root + q[3]) % 12 if len(q) > 3 else (root + q[2]) % 12
    a = near(third, 45)
    b = near(top, 50)
    return (a, b) if a < b else (b, a)


def song():
    s = Song("ЯЗС-МАНУШ", "ЦЫГАНСКИЙ СВИНГ ДЛЯ ЯЗС 5/2", bpm=192,
             rows_per_beat=RPB, beats_per_bar=4, frame_bars=2)
    I = s.instrument
    I("violin", "СКРИПКА", [HOLD | HIT | LOUD | MF | HF], pitch=[-4, 2, 2, 0], vib=(18, 5, 3),
      meter=(14, 12, 2))
    I("vpad", "СКРИПКА", [HOLD | LF | MF], vib=(24, 5, 3), meter=(9, 8, 1))
    I("gtr", "ГИТАРА", [HIT | LOUD | LF | MF | HF] * 2 + [HIT | LOUD | MF | HF], vib=(22, 6, 2),
      meter=(14, 0, 6))
    I("pmp", "ПОМП", [HIT | LF | MF], meter=(9, 0, 12))
    I("pmpa", "ПОМП", [HIT | LOUD | LF | MF], meter=(12, 0, 12))
    I("bass", "КОНТРАБ.", [HIT | LOUD | LF | MF, HIT | LOUD | LF], meter=(14, 0, 6))

    SECTIONS = [("intro", INTRO), ("head", FORM), ("gtr", FORM), ("vln", HA1 + HA2),
                ("out", HB + HA2), ("tag", TAG)]
    br = s.bar_rows
    at, hm, bar = {}, [], 0
    for nm, h in SECTIONS:
        at[nm] = bar
        for sym in h:
            if isinstance(sym, list):
                hm += [(bar * br, sym[0]), (bar * br + br // 2, sym[1])]
            else:
                hm.append((bar * br, sym))
            bar += 1
    total = bar
    R = lambda nm: at[nm] * br
    ch = {c: [] for c in range(7)}

    def add(c, text, drums=False):
        ch[c].append(shuffle(text, drums, RPB))

    def pompe(harm):
        out = []
        for h in harm:
            syms = h if isinstance(h, list) else [h, h]
            for w in (0, 1):
                beats = []
                for k, sym in enumerate(syms):
                    x = pair(sym)[w]
                    beats.append("@pmp %s @pmpa %s" % (n(x, "4"), n(x, "4")))
                out.append(" ".join(beats) + " |")
        lo = " ".join(out[0::2])
        hi = " ".join(out[1::2])
        return lo, hi

    def bass_two(harm):
        out = []
        for h in harm:
            syms = h if isinstance(h, list) else [h]
            if len(syms) == 1:
                root, q, b = chord(syms[0])
                r = near(b, 26)
                f = near((root + q[2]) % 12, 26)
                out.append("%s r4 %s r4 |" % (n(r, "4"), n(f, "4")))
            else:
                out.append(" ".join("%s r4" % n(near(chord(x)[2], 26), "4") for x in syms) + " |")
        return " ".join(out)

    chick = "r4 x4 r4 x4 | "

    # --- вступление: помп и бас, в 4-м такте - разбег гитары ---
    lo, hi = pompe(INTRO)
    add(0, "r1 | r1 | r1 | @gtr o4 e8 f8 g8 g#8 a8 b-8 b8 o5 c#8 |")
    add(1, "r1 | r1 | r1 | r1 |")
    add(2, "q3 " + lo)
    add(3, "q3 " + hi)
    add(4, "@bass " + bass_two(INTRO))
    add(6, chick * 4, drums=True)

    # --- тема: A1 скрипка; A2 + гитара октавой ниже; B - гитара вторым голосом; A3 октавы ---
    head = bars(A1 + A2 + BR + A2)
    head_sw = shuffle(head, False, RPB)
    add(0, "@violin {porta 10} " + head)
    g_a2 = tp(shuffle(bars(A2), False, RPB), -12)
    g_b = harmonize(s, shuffle(bars(BR), False, RPB), hm, D_HARM, R("head") + 16 * br, lo=3, hi=9)
    add(1, "r1 | " * 8 + "@gtr " + g_a2 + " " + g_b + " " + g_a2)
    lo, hi = pompe(FORM)
    add(2, "q3 " + lo)
    add(3, "q3 " + hi)
    add(4, "@bass " + bass_two(FORM))
    add(6, chick * 32, drums=True)

    # --- соло гитары; скрипка держит ---
    pads = " ".join((n(near((chord(h)[0] + chord(h)[1][1]) % 12, 57), "1") if isinstance(h, str) else
                     " ".join(n(near((chord(x)[0] + chord(x)[1][1]) % 12, 57), "2") for x in h)) + " |"
                    for h in FORM)
    add(0, "@gtr " + bars(GUITAR))
    add(1, "@vpad " + pads)
    lo, hi = pompe(FORM)
    add(2, lo)
    add(3, hi)
    add(4, "@bass " + walking(FORM, low=26))
    add(6, chick * 32, drums=True)

    # --- соло скрипки на A A ---
    add(0, "@violin " + bars(VIOLIN))
    add(1, "r1 | " * 16)
    lo, hi = pompe(HA1 + HA2)
    add(2, lo)
    add(3, hi)
    add(4, "@bass " + walking(HA1 + HA2, low=26))
    add(6, chick * 16, drums=True)

    # --- тема: B A, гитара вторым голосом и октавой ---
    add(0, bars(BR + A2))
    g_b2 = harmonize(s, shuffle(bars(BR), False, RPB), hm, D_HARM, R("out"), lo=3, hi=9)
    add(1, "@gtr " + g_b2 + " " + g_a2)
    lo, hi = pompe(HB + HA2)
    add(2, lo)
    add(3, hi)
    add(4, "@bass " + bass_two(HB + HA2))
    add(6, chick * 16, drums=True)

    # --- концовка: хроматический спуск в октаву, арпеджио, аккорд ---
    add(0, bars(ENDING))
    add(1, tp(shuffle(bars(ENDING[:2]), False, RPB), -12) + " @gtr o4 d12 f12 a12 b12 o5 d12 f12 a4 r4 | o4 b4 r4 r2 |")
    add(2, "@pmpa o3 f2. r4 | o3 f2. r4 | o3 f1 | o3 f4 r4 r2 |")
    add(3, "@pmpa o3 b2. r4 | o3 b2. r4 | o3 b1 | o3 b4 r4 r2 |")
    add(4, "o2 d4 r4 o1 a4 r4 | o2 d4 r4 o1 a4 r4 | o2 d1 | o2 d4 r4 r2 |")
    add(6, chick * 2 + "r1 | x4 r4 r2 |", drums=True)

    for c in range(5):
        end = s.voice(c, " ".join(ch[c]), octave=4, length=4)
        assert end == total * br, (c, end / br, total)
    ch[5] = ["r1 | " * total]
    for c in (5, 6):
        end = s.drums(c, " ".join(ch[c]), KIT7, length=4)
        assert end == total * br, (c, end / br, total)

    s.ritard(R("tag") + 2 * br, R("tag") + 4 * br - 1, 192, 120, every=RPB)
    T = lambda nm, txt, off=0: s.text(R(nm) + off * br + 1, txt)
    T("intro", "ПОМП: АККОРД НА КАЖДУЮ ДОЛЮ, НА 2 И 4 С D3, ЧИК - УДАРНЫЙ 7")
    T("head", "ТЕМА У СКРИПКИ: УДАР, СЧ+ВЧ, ВИБРАТО, ЛИГИ СО СКОЛЬЖЕНИЕМ")
    T("head", "ГИТАРА ОКТАВОЙ НИЖЕ: МЕДИАТОР - ПЕРВЫЕ ТИКИ НА ВСЕХ ПОЛОСАХ", 8)
    T("gtr", "СОЛО ГИТАРЫ: ТРИОЛИ ПО АККОРДАМ, ХРОМАТИЗМЫ, ТРЕМОЛО")
    T("vln", "СОЛО СКРИПКИ")
    T("tag", "КОНЦОВКА: DM6 С СИ-БЕКАРОМ НАВЕРХУ")
    s.harmony = hm
    s.sections = at
    return s


if __name__ == "__main__":
    s = song()
    s.build()
    print(s.stats, s.sections)
    for t in s.texts:
        assert len(t) <= 64, len(t)
    print("вне аккорда:", check_harmony(s, s.harmony, every=RPB, label="мануш", allow=(1,)))
