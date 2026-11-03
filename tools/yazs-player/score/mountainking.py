# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
"""
Э. Григ, «Пер Гюнт» (1875): В пещере горного короля. Переложение для ЯЗС.

Тема (си минор) и ее ответ на доминанте (фа-диез) - по 4 такта; проведение -
A A B B, 16 тактов. Четыре проведения, каждое громче и выше, темп растет
непрерывно (по 4 такта, скорость строки подбирается заново) - с 72 до 200, и
кода до 232:
1. фагот стаккато без D3, бас пиццикато на 1 и 3;
2. фагот и скрипки пиццикато в октаву, аккорды на слабые доли, тихие литавры;
3. струнные arco и скрипки, аккорды на доли, литавры на 1 и 3;
4. тутти: медь на всех полосах с D3, аккорды восьмыми, бас октавами, тарелки;
кода: начало темы и ответа, потом октавой выше, аккорды восьмыми с дробью литавр, удары.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from compose import Song                                     # noqa: E402
from instruments import HF, MF, LF, LOUD, HIT, HOLD, KIT7    # noqa: E402
from common import name, check_harmony                       # noqa: E402

# Полутона от си, длительность в восьмых
LINE_A = [[(0, 1), (2, 1), (3, 1), (5, 1), (7, 1), (3, 1), (7, 2)],
          [(6, 1), (2, 1), (6, 2), (5, 1), (1, 1), (5, 2)],
          [(0, 1), (2, 1), (3, 1), (5, 1), (7, 1), (3, 1), (7, 1), (12, 1)],
          [(10, 1), (7, 1), (3, 1), (7, 1), (10, 4)]]
LINE_B = [[(7, 1), (9, 1), (11, 1), (12, 1), (14, 1), (11, 1), (14, 2)],
          [(15, 1), (11, 1), (15, 2), (14, 1), (11, 1), (14, 2)],
          [(7, 1), (9, 1), (11, 1), (12, 1), (14, 1), (11, 1), (14, 1), (19, 1)],
          [(17, 1), (14, 1), (11, 1), (14, 1), (17, 4)]]
THEME = LINE_A + LINE_A + LINE_B + LINE_B
LEN = {1: "8", 2: "4", 4: "2"}
B2, B3, B4 = 35, 47, 59
# Гармония по тактам темы: си минор на педали си, ответ - фа-диез, последний такт - фа-диез 7
HARM = ["Bm"] * 8 + ["F#"] * 3 + ["F#7"] + ["F#"] * 3 + ["F#7"]
STAB = {"Bm": (38, 42), "F#": (34, 37), "F#7": (37, 40)}      # ре-фа#, ля#-до#, до#-ми
ROOT = {"Bm": (23, 30), "F#": (30, 25), "F#7": (30, 25)}     # корень и квинта баса
GKIT6 = {"T": 0x09, "t": 0x01, "k": 0x08, "K": 0x00}         # литавры, тихие, бочка


def n(x, ln):
    return "o%d %s%s" % (x // 12, name(x), ln)


def theme(base, bars=THEME):
    return " ".join(" ".join(n(base + o, LEN[d]) for o, d in bar) + " |" for bar in bars)


def per_bar(fn):
    return " ".join(fn(h) + " |" for h in HARM)


def song():
    s = Song("В ПЕЩЕРЕ ГОРНОГО КОРОЛЯ", "Э. ГРИГ, ПЕР ГЮНТ (1875)", bpm=72,
             rows_per_beat=4, beats_per_bar=4, frame_bars=1)
    I = s.instrument
    I("bsn", "ФАГОТ", [HOLD | HIT | LF | MF], meter=(10, 8, 3))
    I("pizz", "ПИЦЦ.", [HIT | LF | MF], meter=(10, 0, 10))
    I("pizzl", "СКРИПКИ", [HIT | LOUD | LF | MF], meter=(12, 0, 10))
    I("pizzb", "КОНТРАБ.", [HIT | LOUD | LF], meter=(12, 0, 8))
    I("pizzb_s", "КОНТРАБ.", [HIT | LF], meter=(9, 0, 8))
    I("str", "СТРУННЫЕ", [HOLD | HIT | LOUD | LF | MF], vib=(20, 5, 2), meter=(13, 11, 2))
    I("vln", "СКРИПКИ", [HOLD | HIT | LOUD | MF | HF], vib=(20, 5, 2), meter=(13, 11, 2))
    I("brass", "МЕДЬ", [HOLD | HIT | LOUD | LF | MF | HF] * 2 + [HOLD | HIT | LOUD | LF | MF],
      pitch=[-6, 2, 2, 2, 0], meter=(15, 13, 2))
    I("trb", "ТРОМБОНЫ", [HOLD | HIT | LOUD | LF | MF], pitch=[-6, 2, 2, 2, 0], meter=(14, 12, 2))
    I("stab", "АККОРДЫ", [HIT | LOUD | LF | MF], meter=(12, 0, 10))
    I("bassf", "БАС", [HIT | LOUD | LF | MF, HIT | LOUD | LF], meter=(14, 0, 8))

    ch = {c: [] for c in range(7)}
    rest = lambda k: "r1 | " * k
    stab_off = lambda w: per_bar(lambda h: " ".join("r8 " + n(STAB[h][w], "8") for _ in range(4)))
    stab_on = lambda w: per_bar(lambda h: " ".join(n(STAB[h][w], "4") for _ in range(4)))
    stab_8 = lambda w: per_bar(lambda h: " ".join(n(STAB[h][w], "8") for _ in range(8)))
    bass2 = per_bar(lambda h: "%s r4 %s r4" % (n(ROOT[h][0], "4"), n(ROOT[h][1], "4")))
    bass4 = per_bar(lambda h: " ".join(n(ROOT[h][k % 2], "4") for k in range(4)))
    bass8 = per_bar(lambda h: " ".join(n(ROOT[h][0] + 12 * (k % 2), "8") for k in range(8)))

    # 1: фагот, бас пиццикато
    ch[0].append(rest(16))
    ch[1].append("@bsn q4 " + theme(B2))
    ch[2].append(rest(16))
    ch[3].append(rest(16))
    ch[4].append("@pizzb_s q4 " + bass2)
    ch[5].append(rest(16))
    ch[6].append(rest(16))
    # 2: фагот и скрипки пиццикато в октаву
    ch[0].append("@bsn q4 " + theme(B2))
    ch[1].append("@pizzl q8 " + theme(B3))
    ch[2].append("@pizz " + stab_off(1))
    ch[3].append("@pizz " + stab_off(0))
    ch[4].append("@pizzb " + bass4)
    ch[5].append("t4 r4 r2 | " * 16)
    ch[6].append(rest(16))
    # 3: струнные arco
    ch[0].append("@str q6 " + theme(B3))
    ch[1].append("@vln q6 " + theme(B4))
    ch[2].append("@stab q4 " + stab_on(1))
    ch[3].append("@stab q4 " + stab_on(0))
    ch[4].append("@bassf q6 " + bass4)
    ch[5].append("T4 r4 T4 r4 | " * 16)
    ch[6].append(("H4 r4 r2 | " + rest(3)) * 4)
    # 4: тутти
    ch[0].append("@brass q7 " + theme(B4))
    ch[1].append("@trb q7 " + theme(B3))
    ch[2].append("@stab q6 " + stab_8(1))
    ch[3].append("@stab q6 " + stab_8(0))
    ch[4].append("@bassf q6 " + bass8)
    ch[5].append("k4 T4 k4 T4 | " * 16)
    ch[6].append("H4 r4 r2 | r1 | " * 8)

    # Кода: обрывки темы, аккорды восьмыми с дробью, удары
    frag = lambda base: theme(base, [LINE_A[0], LINE_B[0]]) + " " + theme(base + 12, [LINE_A[0], LINE_B[0]])
    ham = lambda a, b: " ".join(n(a, "8") for _ in range(8)) + " | " + " ".join(n(b, "8") for _ in range(8)) + " |"
    ch[0].append(frag(B3) + " " + ham(59, 59) + " " + " ".join(n(58, "8") for _ in range(8))
                 + " | o4 b4 r4 b2 |")
    ch[1].append(frag(B2) + " " + ham(54, 54) + " " + " ".join(n(52, "8") for _ in range(8))
                 + " | o4 f#4 r4 f#2 |")
    ch[2].append("@stab " + " ".join(" ".join(n(x, "8") for _ in range(8)) + " |" for x in (50, 49, 50, 49))
                 + " " + ham(50, 50) + " " + " ".join(n(49, "8") for _ in range(8)) + " | o4 d4 r4 d2 |")
    ch[3].append("@stab " + " ".join(" ".join(n(x, "8") for _ in range(8)) + " |" for x in (47, 46, 47, 46))
                 + " " + ham(47, 47) + " " + " ".join(n(42, "8") for _ in range(8)) + " | o3 b4 r4 b2 |")
    ch[4].append(" ".join(" ".join(n(x + 12 * (k % 2), "8") for k in range(8)) + " |"
                          for x in (23, 30, 23, 30, 23, 23, 30)) + " o1 b4 r4 b2 |")
    ch[5].append("k4 T4 k4 T4 | " * 4 + ("T16 " * 16 + "| ") * 3 + "k4 r4 k2 |")
    ch[6].append("H4 r4 r2 | r1 | H4 r4 r2 | r1 | H4 r4 r2 | r1 | r1 | H4 r4 H2 |")

    bars = 16 * 4 + 8
    br = s.bar_rows
    for c in range(5):
        end = s.voice(c, " ".join(ch[c]), octave=3, length=4)
        assert end == bars * br, (c, end / br)
    for c in (5, 6):
        end = s.drums(c, " ".join(ch[c]), GKIT6 if c == 5 else KIT7, length=4)
        assert end == bars * br, (c, end / br)

    # Темп: непрерывно растет, по 4 такта
    t0, t1 = 72.0, 200.0
    for k in range(16):
        a = t0 * (t1 / t0) ** (k / 16.0)
        b = t0 * (t1 / t0) ** ((k + 1) / 16.0)
        s.ritard(k * 4 * br, (k + 1) * 4 * br - 1, a, b, every=4)
    s.ritard(64 * br, 70 * br - 1, 200, 232, every=4)

    T = lambda bar, txt: s.text(bar * br + 2, txt)
    T(0, "ФАГОТ СТАККАТО БЕЗ D3, БАС ПИЦЦИКАТО. ТЕМП - С 72 ДО 232")
    T(16, "ФАГОТ И СКРИПКИ ПИЦЦИКАТО В ОКТАВУ, ТИХИЕ ЛИТАВРЫ")
    T(32, "СТРУННЫЕ ARCO, АККОРДЫ НА ДОЛИ, ЛИТАВРЫ - УДАРНЫЙ 6 С D3")
    T(48, "ТУТТИ: МЕДЬ НА ВСЕХ ПОЛОСАХ, АККОРДЫ ВОСЬМЫМИ, ТАРЕЛКИ")
    T(64, "КОДА: ДРОБЬ ЛИТАВР ШЕСТНАДЦАТЫМИ")

    hm = []
    for k in range(4):
        for i, h in enumerate(HARM):
            hm.append(((k * 16 + i) * br, h))
    for i, h in enumerate(["Bm", "F#", "Bm", "F#", "Bm", "Bm", "F#7", "Bm"]):
        hm.append(((64 + i) * br, h))
    s.harmony = hm
    return s


if __name__ == "__main__":
    s = song()
    s.build()
    print(s.stats)
    for t in s.texts:
        assert len(t) <= 64
    print("вне аккорда:", check_harmony(s, s.harmony, channels=[2, 3, 4], every=s.rpb, label="григ"))
