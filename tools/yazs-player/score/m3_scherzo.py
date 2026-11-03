# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
"""
Симфония «А-Г-А-Т», часть III: Скерцо-вальс, ре мажор, 3/4.

Вальс начинается лейтмотивом ля-соль-ля-си у флейты. Трель - макрос
арпеджио (нота и соседняя по 6 тиков), форшлаг - макрос высоты (подъезд на
полутон снизу за 5 тиков). Аккомпанемент «ум-па-па» - удар D4 коротким
гейтом. Трио в си миноре - пиццикато, в репризе скрипки идут терцией ниже
флейты, треугольник - ударный 7 на верхней высоте. В коде - accelerando.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from compose import Song                                     # noqa: E402
from instruments import HF, MF, LF, LOUD, HIT, HOLD, KIT7     # noqa: E402
from common import check_harmony, harmonize, oompah         # noqa: E402

D_MAJOR = [2, 4, 6, 7, 9, 11, 13]       # ре, ми, фа#, соль, ля, си, до#

A_HARM = ["D", "D", "G", "A7", "A7", "A7", "A7", "D", "D", "D", "G", "A7", "Bm", "E7", "A7", "D"]
A_MEL = [
    "o5 a4 g8 a8 b4", "o6 d2 o5 a4", "o5 b4 a8 g8 d4", "o5 e2.",
    "o5 g4 f#8 g8 a4", "o6 c#2 o5 g4", "o5 a4 g8 f#8 e4", "o5 d2.",
    "o5 a4 g8 a8 b4", "o6 d2 o5 a4", "o5 b4 a8 g8 d4", "o5 e2.",
    "o5 f#4 e8 f#8 g4", "o5 g#2 b4", "o5 a4 e4 c#4", "o5 d2 r4",
]
# Вариация: форшлаги на сильных долях, трели на долгих
A_VAR = [
    "@grace o5 a4 @flute g8 a8 b4", "@grace o6 d2 @flute o5 a4", "o5 b4 a8 g8 d4", "@trill2 o5 e2. @flute",
    "@grace o5 g4 @flute f#8 g8 a4", "@grace o6 c#2 @flute o5 g4", "o5 a4 g8 f#8 e4", "@trill2 o5 d2. @flute",
    "@grace o5 a4 @flute g8 a8 b4", "o6 d4 c#8 d8 o5 a4", "o5 b4 a8 g8 d4", "@trill2 o5 e2. @flute",
    "o5 f#4 e8 f#8 g4", "@trill1 o5 g#2 @flute b4", "o5 a4 e4 c#4", "o5 d2 r4",
]
B_HARM = ["Bm", "Bm", "F#7", "F#7", "F#7", "F#7", "Bm", "Bm",
          "G", "G", "D", "D", "Em", "F#7", "Bm", "Bm"]
B_MEL = [
    "o5 f#8 e8 d8 c#8 d4", "o4 b4 b4 r4", "o5 e8 d8 c#8 < b8 > c#4", "o4 a#4 f#4 r4",
    "o5 c#8 d8 e8 f#8 e4", "o5 c#4 o4 a#4 r4", "o5 d8 c#8 < b8 > c#8 d4", "o4 b2 r4",
    "o5 d8 e8 d8 < b8 g4", "o4 b4 g4 r4", "o5 a8 b8 a8 f#8 d4", "o5 f#4 d4 r4",
    "o5 g8 f#8 e8 d8 e4", "o5 c#8 d8 e8 c#8 o4 a#4", "o5 d4 o4 b4 f#4", "o4 b2 r4",
]
C_HARM = ["D", "G", "A7", "D", "D", "A7", "D", "D"]
C_MEL = [
    "o5 d8 e8 f#8 g8 a8 b8", "o5 b8 a8 g8 a8 b8 > d8", "o6 c#8 < b8 a8 g8 f#8 e8", "o5 d4 f#4 a4",
    "o6 d4 o5 a4 f#4", "o5 e4 c#4 o4 a4", "o5 d4 r4 o6 d4", "o5 d4 r4 r4",
]
INTRO_HARM = ["D", "D", "A7", "A7"]


def song():
    s = Song("III. СКЕРЦО-ВАЛЬС", "СИМФОНИЯ А-Г-А-Т ДЛЯ ЯЗС 5/2", bpm=156,
             rows_per_beat=4, beats_per_bar=3)
    I = s.instrument
    flute = [HOLD | HIT | LOUD | MF | HF]
    I("flute", "ФЛЕЙТА", flute, vib=(14, 5, 2), meter=(13, 11, 2))
    I("grace", "ФЛЕЙТА", flute, pitch=[-16, 4, 4, 4, 4, 0], meter=(13, 11, 2))
    I("trill2", "ТРЕЛЬ", flute, arp=[0] * 6 + [2] * 6, arp_loop=0, meter=(13, 11, 2))
    I("trill1", "ТРЕЛЬ", flute, arp=[0] * 6 + [1] * 6, arp_loop=0, meter=(13, 11, 2))
    I("violins", "СКРИПКИ", [HOLD | HIT | MF | LF], vib=(16, 5, 2), meter=(11, 9, 2))
    I("pizz", "ПИЦЦИКАТ", [HIT | LOUD | MF | LF], meter=(14, 0, 14))
    I("bass", "БАС", [HIT | LOUD | LF], meter=(14, 0, 8))
    I("chord", "АККОРД", [HIT | MF | LF], meter=(10, 0, 14))

    harm = INTRO_HARM + A_HARM + A_HARM + B_HARM + A_HARM + C_HARM
    bars = {}

    def at(name):
        return bars[name]
    pos = 0
    for name, n in (("intro", 4), ("A", 16), ("Avar", 16), ("B", 16), ("A2", 16), ("coda", 8)):
        bars[name] = pos
        pos += n

    rest = lambda n: "r2. | " * n
    # Мелодия: флейта; трио - пиццикато
    s.voice(0, "@flute " + rest(4) + " | ".join(A_MEL) + " | " + " | ".join(A_VAR) + " | "
            "@pizz " + " | ".join(B_MEL) + " | @flute " + " | ".join(A_MEL) + " | "
            + " | ".join(C_MEL) + " |")
    # Второй голос: скрипки терцией ниже в репризе и коде
    br = s.bar_rows
    hm = [(i * br, h) for i, h in enumerate(harm)]
    s.voice(1, "@violins " + rest(4 + 16 + 16 + 16)
            + harmonize(s, " | ".join(A_MEL), hm, D_MAJOR, br * at("A2")) + " | "
            + harmonize(s, " | ".join(C_MEL), hm, D_MAJOR, br * at("coda")) + " |")
    # Ум-па-па
    b, u, l = oompah(harm)
    s.voice(4, "@bass q6 " + b)
    s.voice(2, "@chord q4 " + u)
    s.voice(3, "@chord q4 " + l)
    # Треугольник: на каждом втором такте в вариации и репризе
    tri = ("H2. | r2. | " * 8)
    s.drums(6, rest(4 + 16) + tri + rest(16) + tri + "H4 r4 r4 | " * 6 + "H2. | r2. |", KIT7)

    br = s.bar_rows
    s.set_tempo(0, 156)
    s.ritard(br * (at("B") - 1), br * at("B"), 156, 132)
    s.set_tempo(br * at("B"), 138)
    s.ritard(br * (at("A2") - 1), br * at("A2"), 138, 156)
    s.ritard(br * at("coda"), br * (at("coda") + 5), 156, 184, every=3)
    s.ritard(br * (at("coda") + 6), br * (at("coda") + 8) - 1, 184, 120, every=3)
    s.text(0, "УМ-ПА-ПА: УДАР D4 КОРОТКИМ ГЕЙТОМ, БАС НА НЧ")
    s.text(br * at("A"), "ФЛЕЙТА (СЧ+ВЧ): ЛЕЙТМОТИВ В МАЖОРЕ")
    s.text(br * at("Avar"), "ФОРШЛАГИ - МАКРОС ВЫСОТЫ, ТРЕЛИ - МАКРОС АРПЕДЖИО")
    s.text(br * at("B"), "ТРИО: ПИЦЦИКАТО, СИ МИНОР")
    s.text(br * at("A2"), "СКРИПКИ ТЕРЦИЕЙ НИЖЕ, ТРЕУГОЛЬНИК - УДАРНЫЙ 7")
    s.text(br * at("coda"), "КОДА: ACCELERANDO - ДЕЛИТЕЛЬ ТАЙМЕРА ЯЗС")
    s.harmony = [(i * br, h) for i, h in enumerate(harm)]
    return s


if __name__ == "__main__":
    s = song()
    s.build()
    print(s.stats)
    print("вне аккорда:", check_harmony(s, s.harmony, every=s.rpb, label="III"))
