# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
"""
Симфония «А-Г-А-Т», часть IV: Блюз 1987, си-бемоль, свинг.

Свинг - неравные строки: долгая восьмая 34 тика, короткая 18 (SPEED на
каждой строке управляющего канала). Труба с сурдиной - СЧ+ВЧ, подтяжки на
блюзовую терцию - лига со скольжением (PORTA). Хаммонд - удар с удержанием и
быстрое мелкое вибрато («лесли»). Комп органа - третья и седьмая аккорда
ритмом «чарльстон», шагающий бас, райд и щетки на ударных. Лейтмотив ля-соль-
ля-си проходит в соло органа.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from compose import Song                                     # noqa: E402
from instruments import HF, MF, LF, LOUD, HIT, HOLD, KIT6, KIT7   # noqa: E402
from common import chord, near, name, walking, harmonize, check_harmony   # noqa: E402

BLUES = ["Bb7", "Eb7", "Bb7", "Bb7", "Eb7", "Eb7", "Bb7", "G7", "Cm7", "F7",
         ["Bb7", "G7"], ["Cm7", "F7"]]
BB_MIXO = [10, 12, 14, 15, 17, 19, 20]  # си-бемоль миксолидийский, по возрастанию

HEAD = [
    "r8 o4 f8 a-8 _a8 b-4 f4", "o4 a-8 f8 d8 f8 r2", "r8 o4 f8 a-8 _a8 b-4 > d4",
    "o5 c8 < b-8 a-8 f8 r2", "r8 o4 b-8 > d-8 _d8 e-4 < b-4", "o5 d-8 < b-8 g8 b-8 r2",
    "r8 o4 f8 a-8 _a8 b-4 f4", "o4 a-8 f8 d8 o3 b8 r2", "o5 e-4. c8 _d8 c8 o4 b-4",
    "o4 a4 > c4 e-4 c4", "o4 b-4 d8 f8 g4 b4", "o5 c8 e-8 g8 e-8 f4 r4",
]
SOLO = [
    "o5 d8 f8 _f#8 _g8 f8 d8 o4 b-8 a-8", "o4 b-4 r8 f8 a-8 _a8 b-8 > d8",
    "o5 f4. d8 _e-8 d8 c8 o4 b-8", "o4 a-4 f4 r2",
    "o5 e-8 g8 b-8 > d-8 c8 < b-8 g8 e-8", "o5 f8 _f#8 _g4 e-8 d-8 o4 b-8 g8",
    "o5 d4 f8 d8 _d-8 c8 o4 b-4", "o4 b8 > d8 f8 a-8 g4 r4",
    "o5 c8 e-8 g8 b-8 _b8 > c8 < b-8 g8", "o5 a8 f8 e-8 c8 o4 a8 > c8 e-8 f8",
    "o5 d4 o4 b-4 b4 > d4", "o5 e-8 d8 c8 o4 b-8 a8 f8 e-8 c8",
]
ORGAN = [
    "o4 a8 g8 a8 b8 > c8 < a-8 f8 d8", "o4 f2. r4", "o5 d8 f8 a-8 f8 d8 c8 o4 b-8 a-8",
    "o4 f4 d4 r2", "o4 g8 b-8 > d-8 _d8 e-8 d-8 o4 b-8 g8", "o4 b-2. r4",
    "o5 d8 c8 o4 b-8 a-8 f8 a-8 _a8 b-8", "o4 b4 > d4 f4 r4",
    "o5 e-8 g8 _a-8 g8 e-8 c8 o4 b-8 g8", "o4 a8 > c8 e-8 f8 e-8 c8 o4 a8 f8",
    "o4 b-4 > d4 o4 g4 b4", "o5 c4 e-4 f4 r4",
]
SHOUT = [
    "r2 o5 d8 d8 r4", "r2 o5 d8 c8 r4", "r2 o5 f8 f8 r4", "o5 a-4 f4 r2",
    "r2 o5 g8 g8 r4", "r2 o5 g8 e-8 r4", "r2 o5 f8 f8 r4", "o5 f4 d4 r2",
    "r2 o5 g8 g8 r4", "r2 o5 a8 a8 r4", "o5 f4 r4 d4 r4", "o5 g4 r4 a4 r4",
]
INTRO = [["Cm7", "F7"], ["Bb7", "G7"], ["Cm7", "F7"], "F7"]
INTRO_MEL = ["o5 c8 e-8 g8 e-8 f4 r4", "o4 b-4 d8 f8 g4 b4", "o5 c8 e-8 g8 e-8 f4 r4",
             "o4 a4 > c4 e-4 f4"]
END = ["Bb7", "Bb7"]
END_MEL = ["o5 d8 c8 o4 b-8 a-8 _a8 b-4.", "{vib 0x63} o4 b-2. r4"]


def halves(h):
    return [h, h] if isinstance(h, str) else h


def guide(harm, a0=62, b0=56):
    """Терция и септима каждого аккорда, ближе к прошлым"""
    pa, pb = a0, b0
    out = []
    for h in harm:
        bar = []
        for sym in halves(h):
            root, q, _ = chord(sym)
            third, seventh = (root + q[1]) % 12, (root + q[3]) % 12
            x = [near(third, pa), near(seventh, pa)]
            hi = min(x, key=lambda n: abs(n - pa))
            lo_pc = seventh if hi % 12 == third else third
            lo = near(lo_pc, hi - 4)
            if lo >= hi:
                lo -= 12
            bar.append((hi, lo))
            pa, pb = hi, lo
        out.append(bar)
    return out


def comp(harm, which):
    """Чарльстон: одна гармония - q. + 8; две - по половине"""
    out = []
    for bar, h in zip(guide(harm), harm):
        if isinstance(h, str):
            n = bar[0][which]
            out.append("o%d%s4. o%d%s8 r2 |" % (n // 12, name(n), n // 12, name(n)))
        else:
            a, b = bar[0][which], bar[1][which]
            out.append("o%d%s4 r4 o%d%s4 r4 |" % (a // 12, name(a), b // 12, name(b)))
    return " ".join(out)


def song():
    s = Song("IV. БЛЮЗ 1987", "СИМФОНИЯ А-Г-А-Т ДЛЯ ЯЗС 5/2", bpm=112, rows_per_beat=4)
    I = s.instrument
    I("tpt", "ТРУБА", [HOLD | HIT | LOUD | MF | HF], vib=(26, 5, 2), meter=(14, 12, 2))
    I("comp", "ХАММОНД", [HOLD | HIT | MF | LF], vib=(0, 11, 1), meter=(11, 9, 3))
    I("organ", "ХАММОНД", [HOLD | HIT | LOUD | MF | LF | HF], vib=(0, 10, 1), meter=(14, 12, 2))
    I("horn", "ТРОМБОН", [HOLD | HIT | LOUD | LF | MF], vib=(26, 5, 2), meter=(12, 10, 2))
    I("bass", "КОНТРАБ.", [HIT | LOUD | LF], meter=(14, 0, 6))

    harm = INTRO + BLUES * 4 + END
    br = s.bar_rows
    hm = []
    for i, h in enumerate(harm):
        for k, sym in enumerate(halves(h)):
            hm.append((i * br + k * br // 2, sym))
    C = [4 + 12 * k for k in range(4)]          # начала квадратов
    E = 4 + 48
    rest = lambda n: "r1 | " * n

    # Труба: тема, тема, соло, шаут; концовка
    s.voice(0, "@tpt {porta 5} " + " | ".join(INTRO_MEL) + " | " + " | ".join(HEAD) + " | "
            + " | ".join(HEAD) + " | " + " | ".join(SOLO) + " | " + " | ".join(SHOUT) + " | "
            + " | ".join(END_MEL) + " |")
    # Второй голос: тромбон под темой во втором квадрате, орган-соло в четвертом
    second = harmonize(s, " | ".join(HEAD), hm, BB_MIXO, br * C[1], lo=3, hi=9)
    s.voice(3, "@horn {porta 5} " + rest(4 + 12) + second + " | " + rest(12)
            + "@organ {porta 4} " + " | ".join(ORGAN) + " | @comp o4 d1 | f1 |")
    # Комп органа: две направляющие ноты
    s.voice(1, "@comp q7 " + comp(harm[:-2], 0) + " o4 a-1 | ^1 |")
    s.voice(2, "@comp q7 " + comp(harm[:-2], 1) + " o4 d1 | ^1 |")
    # Шагающий бас
    s.voice(4, "@bass q7 " + walking(harm[:-2], low=34) + " o2 b-4 r4 f4 r4 | o2 b-2 r2 |")
    # Ударные: щетки (2 и 4) и бочка (1 и 3); райд «динг-динг-а-динг»
    kick = "K4 s4 K4 s4 | "
    s.drums(5, "r1 | r1 | r1 | K4 s4 K8 K8 s4 | " + kick * 48 + "K4 s4 K4 s4 | K4 r4 r2 |", KIT6)
    ride = "z4 z8 z8 z4 z8 z8 | "
    s.drums(6, "r1 | r1 | " + ride * 2 + ride * 48 + "x1 | r1 |", KIT7)

    # Свинг по всей пьесе
    long_t = s.speed * 4 * 2 // 3
    s.swing(0, br * (E + 2) - 1, long_t, s.speed * 4 - long_t)
    s.text(0, "СВИНГ: СТРОКИ ПО 17 И 9 ТИКОВ, ВОСЬМЫЕ 2:1")
    s.text(br * C[0], "ТРУБА С СУРДИНОЙ СЧ+ВЧ, ПОДТЯЖКА - ЛИГА СО СКОЛЬЖЕНИЕМ")
    s.text(br * C[1], "ТРОМБОН НИЖЕ ТРУБЫ, ХАММОНД: ТЕРЦИЯ И СЕПТИМА")
    s.text(br * C[2], "СОЛО ТРУБЫ: БЛЮЗОВЫЕ НОТЫ")
    s.text(br * C[3], "СОЛО ХАММОНДА: ЛЕСЛИ - БЫСТРОЕ ВИБРАТО")
    s.harmony = hm
    return s


if __name__ == "__main__":
    s = song()
    s.build()
    print(s.stats)
    print("вне аккорда:", check_harmony(s, s.harmony, every=s.rpb, label="IV",
                                        allow=(1, 6)))   # блюзовые ноты от си-бемоль
