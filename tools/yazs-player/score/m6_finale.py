# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
"""
Симфония «А-Г-А-Т», часть VI: Финал. Рок, ля минор -> симфоническая кода,
ля мажор.

Пауэр-аккорд - три счетчика (основа, квинта, октава) с расстройкой на
1/16 полутона друг от друга: биения дают «перегруз». Галоп «восьмая и две
шестнадцатых» - удар D4 коротким гейтом. Соло: подтяжки (лига со
скольжением), вибрато, «тэппинг» шестнадцатыми. Брейк - сбивка по томам:
четыре высоты ударного 6. Кода: орган и струнные из первых частей играют
лейтмотив в увеличении, ля минор светлеет в ля мажор, литавры - тремоло
ударного 6, тарелка - ударный 7 с D3; темп расширяется делителем таймера.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from compose import Song                                     # noqa: E402
from instruments import HF, MF, LF, LOUD, HIT, HOLD, KIT6, KIT7   # noqa: E402
from common import chord, near, voicing, name, check_harmony   # noqa: E402

RIFF = ["A5", "F5", "C5", "G5", "A5", "F5", "G5", "E5"]
THEME_H = ["Am", "F", "C", "G", "Am", "F", "G", "E", "Am", "F", "C", "G", "Dm", "E", "Am", "Am"]
THEME = [
    "o5 a4. g8 a4 b4", "o6 c2 o5 a4 f4", "o5 g4. e8 g4 > c4", "o5 b2 > d2",
    "o5 a4. g8 a4 b4", "o6 c4 d4 c4 o5 a4", "o5 b4 > d4 g4 d4", "o5 g#2. r4",
    "o6 e4. d8 e4 c4", "o6 c4. o5 a8 f4 a4", "o5 g4 e4 g4 > c4", "o5 b2. r4",
    "o5 a4. g8 f4 a4", "o5 g#4 b4 > e4 d4", "o6 c4 o5 b4 a4 g#4", "o5 a2. r4",
]
SOLO_H = ["Am", "F", "C", "G", "Am", "F", "G", "E"] * 2
SOLO = [
    "o5 a16 > c16 d16 e16 g16 e16 d16 c16 < a8 _b8 > _c4",
    "o6 c8 o5 a8 f8 a8 > c4 f4",
    "o6 e16 d16 c16 o5 g16 e16 g16 > c16 d16 e4 _f4",
    "o6 d4. o5 b8 g4 b4",
    "o6 a16 g16 e16 d16 c16 o5 a16 g16 e16 a4 > c4",
    "o6 c8 d8 c8 o5 a8 f4 a4",
    "o5 b16 > d16 g16 b16 a16 g16 d16 o5 b16 > d4 _e4",
    "o5 g#8 b8 > e8 g#8 _a4 _g#4",
    "o6 e2 c4 o5 a4",
    "o6 f16 e16 d16 c16 o5 a16 o6 c16 d16 c16 o5 a4 f4",
    "o5 g8 > c8 e8 g8 e8 c8 o5 g8 e8",
    "o5 d8 g8 b8 > d8 g4 _a4",
    "[o6 a16 e16 c16 o5 a16]4",
    "[o6 a16 f16 c16 o5 a16]4",
    "[o6 g16 d16 o5 b16 g16]4",
    "o6 g#16 e16 o5 b16 g#16 o6 g#16 e16 o5 b16 g#16 o6 e4 {su 4} e4",
]
CODA_H = ["Am", "Dm", "C", "E", "A", "F#m", "A/C#", "E", "A", "D", "Bm7", "E7", "A", "D/A", "A", "A"]
CODA = [
    "o5 a2 g2", "o5 a2 b2", "o6 c1", "o5 b1",
    "o5 a2 g#2", "o5 a2 b2", "o6 c#1", "o6 e1",
    "o6 e2 c#2", "o6 d2 f#2", "o6 d2 c#2", "o5 b2 g#2",
    "o5 a1", "o5 a1", "o5 a1", "o5 a2 r2",
]


def power(sym, interval):
    root, _, _ = chord(sym)
    r = near(root, 31)                  # около G2
    n = r + interval
    one = "o%d%s" % (n // 12, name(n))
    return " ".join([one + "8", one + "16", one + "16"] * 4) + " |"


def held(syms, idx, top=72):
    out = []
    for sym in syms:
        v = voicing(sym, top, 3)
        n = v[idx]
        out.append("o%d%s1 |" % (n // 12, name(n)))
    return " ".join(out)


def roots(syms, low=21, pattern="8"):
    out = []
    prev = low + 5
    for sym in syms:
        _, _, b = chord(sym)
        n = near(b, prev)
        if n < low:
            n += 12
        if n > low + 11:
            n -= 12
        prev = n
        if pattern == "8":
            out.append(" ".join(["o%d%s8" % (n // 12, name(n))] * 8) + " |")
        else:
            out.append("o%d%s1 |" % (n // 12, name(n)))
    return " ".join(out)


def song():
    s = Song("VI. ФИНАЛ", "СИМФОНИЯ А-Г-А-Т ДЛЯ ЯЗС 5/2", bpm=140, rows_per_beat=4)
    I = s.instrument
    gtr = [HOLD | HIT | LOUD | LF | MF | HF]
    I("gtr", "ГИТАРА", gtr, meter=(15, 11, 4))
    I("gtr_u", "ГИТАРА", gtr, detune=1, meter=(15, 11, 4))
    I("gtr_d", "ГИТАРА", [HOLD | HIT | LF | MF | HF], detune=-1, meter=(12, 9, 4))
    I("lead", "СОЛО-ГИТ", [HOLD | HIT | LOUD | MF | HF], vib=(10, 6, 3), meter=(15, 13, 2))
    I("bass", "БАС-ГИТ.", [HIT | LOUD | LF], meter=(15, 0, 6))
    I("organ", "ОРГАН", [HOLD | HIT | LOUD | LF | MF | HF], meter=(15, 12, 2))
    I("strings", "СТРУННЫЕ", [HOLD | LOUD | LF | MF], vib=(18, 4, 2), meter=(13, 12, 1))
    I("brass", "МЕДЬ", [HOLD | HIT | LOUD | MF | HF], vib=(24, 4, 2), meter=(15, 13, 2))

    INTRO, RIFF0 = 0, 4
    TH0 = RIFF0 + 8
    SO0 = TH0 + 16
    BR0 = SO0 + 16
    CO0 = BR0 + 4
    total = CO0 + 16
    rest = lambda n: "r1 | " * n
    riff_th = RIFF * 2                          # под темой: тот же риф
    riff_so = RIFF * 2

    def guitar(interval, ins):
        hits = "@%s q6 " % ins
        stab = (lambda sym: "o%d%s4 r4 r2 |" % (
            (near(chord(sym)[0], 31) + interval) // 12, name(near(chord(sym)[0], 31) + interval)))
        intro = stab("A5") + " " + stab("A5") + " " + stab("F5") + " " + \
            "o%d%s8 r8 r4 r2 |" % ((near(9, 31) + interval) // 12, name(near(9, 31) + interval))
        body = " ".join(power(x, interval) for x in RIFF + riff_th + riff_so)
        brk = " ".join("q8 o%d%s1 |" % ((near(chord(x)[0], 31) + interval) // 12,
                                         name(near(chord(x)[0], 31) + interval)) for x in ["A5", "F5", "G5", "E5"])
        return hits + intro + " " + body + " " + brk

    s.voice(1, guitar(0, "gtr") + " @organ q8 " + held(CODA_H, 2))
    s.voice(2, guitar(7, "gtr_u") + " @organ q8 " + held(CODA_H, 1))
    s.voice(3, guitar(12, "gtr_d") + " @strings q8 " + held(CODA_H, 0, top=64))
    # Соло-гитара: тема, соло; в коде - медь с лейтмотивом
    s.voice(0, "@lead {porta 4} " + rest(4) + rest(8) + " | ".join(THEME) + " | "
            + " | ".join(SOLO) + " | {su 0} o5 a1 | ^1 | g1 | g#1 | @brass "
            + " | ".join(CODA) + " |")
    s.voice(4, "@bass q7 " + "o1 a4 r4 r2 | a4 r4 r2 | f4 r4 r2 | a8 r8 r4 r2 | "
            + roots(RIFF + riff_th + riff_so) + " " + roots(["A5", "F5", "G5", "E5"], pattern="1")
            + " " + roots(CODA_H, low=26, pattern="1"))
    # Ударные
    rock = "k8 k8 s4 k8 k8 s4 | "
    fill = "l16 s s s s t t t t u u u u v v w w | "
    timp = "l16 " + "K " * 16 + "| "
    s.drums(5, "k4 r4 r2 | k4 r4 r2 | k4 r4 r2 | " + fill
            + rock * 7 + fill + rock * 15 + fill + rock * 15 + fill
            + "k2 s2 | k2 s2 | k2 s2 | l16 w w v v u u t t s s s s k k k k | "
            + rest(12) + timp * 2 + "k1 | k4 r4 r2 |", KIT6)
    hats = "h8 h8 h8 h8 h8 h8 h8 h8 | "
    s.drums(6, "c1 | c1 | c1 | r1 | c1 | " + hats * 7 + "c1 | " + hats * 15 + "c1 | " + hats * 15
            + "c1 | r1 | r1 | c1 | " + "c1 | " + rest(11) + "c1 | r1 | c1 | c1 |", KIT7)

    br = s.bar_rows
    s.set_tempo(0, 140)
    s.set_tempo(br * CO0, 104)
    s.ritard(br * (CO0 + 8), br * (CO0 + 12), 104, 92)
    s.ritard(br * (total - 3), br * total - 1, 92, 56, every=2)
    s.text(0, "ПАУЭР-АККОРД: ТРИ СЧЕТЧИКА С РАССТРОЙКОЙ 1/16 ПОЛУТОНА")
    s.text(br * RIFF0, "ГАЛОП: УДАР D4 КОРОТКИМ ГЕЙТОМ, БАС НА НЧ")
    s.text(br * TH0, "ТЕМА НА ЛЕЙТМОТИВЕ: СОЛО-ГИТАРА СЧ+ВЧ С ВИБРАТО")
    s.text(br * SO0, "СОЛО: ПОДТЯЖКИ, ТЭППИНГ 16-МИ")
    s.text(br * BR0, "БРЕЙК: ЧЕТЫРЕ ВЫСОТЫ ТОМОВ УДАРНОГО 6")
    s.text(br * CO0, "КОДА: ОРГАН, СТРУННЫЕ И МЕДЬ - ЛЕЙТМОТИВ В УВЕЛИЧЕНИИ")
    s.text(br * (CO0 + 4), "ЛЯ МИНОР -> ЛЯ МАЖОР")
    s.text(br * (CO0 + 12), "ЛИТАВРЫ И ТАРЕЛКА. КОНЕЦ СИМФОНИИ")
    harm = (["A5"] * 4 + RIFF + THEME_H + SOLO_H + ["A5", "F5", "G5", "E5"] + CODA_H)
    assert len(harm) == total
    s.harmony = [(i * br, h) for i, h in enumerate(harm)]
    return s


if __name__ == "__main__":
    s = song()
    s.build()
    print(s.stats)
    print("вне аккорда:", check_harmony(s, s.harmony, every=s.rpb, label="VI", channels=(0, 4)))
