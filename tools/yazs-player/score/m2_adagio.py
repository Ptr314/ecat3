# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
"""
Симфония «А-Г-А-Т», часть II: Адажио, фа мажор.

Тема виолончели - лейтмотив ля-соль-ля-си-бемоль в мажоре. Струнные - один
D5: плавная атака ячейки (~0.3 с), удержание без удара; вибрато с задержкой,
как у смычка. Арфа - удар D4 без удержания. В середине скрипка и альт играют
тему в унисон с расстройкой на 1/16 полутона - хорус двух счетчиков; литавры
- ударный 6 тремоло.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from compose import Song                                     # noqa: E402
from instruments import HF, MF, LF, LOUD, HIT, HOLD, KIT6, KIT7   # noqa: E402
from common import chord, near, voicing, name, check_harmony  # noqa: E402

# Аккорды по тактам (список - по половинам такта)
HARM = [
    # A: тема виолончели
    "F", "Dm", "Bb", "C7", "F", "Am", "Gm7", "C7", "Dm", "Bb", ["Gm", "C7"], "F",
    # B: скрипки, кульминация
    "Dm", "Bb", "F", "C", "Dm", "Gm", "A7", "Dm", "Bb", "C", ["Am", "Dm"], ["Gm7", "C7"],
    # A': возвращение, минорная субдоминанта, затихание
    "F", "Dm", "Bb", "C7", "F", "Bbm", "F", "F",
]

CELLO = [
    "o3 a4. g8 a4 b-4", "o4 d2 c4 o3 a4", "o3 b-2. a8 g8", "o3 g2 e4 g4",
    "o3 a4. g8 a4 b-4", "o4 c2 e4 c4", "o3 b-4. a8 g4 f4", "o3 e2 g2",
    "o3 f4. e8 f4 a4", "o4 d2 c4 o3 b-4", "o3 b-4 g4 e4 c4", "o3 f1",
]
VIOLIN = [
    "o5 a4. g8 a4 b-4", "o5 d2 c4 o4 b-4", "o5 a2. f4", "o5 g2 e4 c4",
    "o5 a4. b-8 > c4 d4", "o6 d2 o5 b-4 g4", "o5 c#2 e4 g4", "o5 f2. a4",
    "o6 d4. c8 d4 f4", "o6 e2 c4 o5 g4", "o6 c4 o5 a4 a4 f4", "o5 g4 b-4 e4 g4",
]
CELLO2 = [
    "o3 a4. g8 a4 b-4", "o4 d2 c4 o3 a4", "o3 b-2. a8 g8", "o3 g2 e4 c4",
    "o3 a1", "o3 b-2 d-2", "o3 a1", "o3 f2 r2",
]


def halves(h):
    return [h, h] if isinstance(h, str) else h


def pad(harm, top1=76, top2=71):
    """Два голоса фона: ближайшие тона аккорда к прошлым, второй ниже первого"""
    p1, p2 = top1, top2
    a, b = [], []
    for h in harm:
        for sym in halves(h):
            root, q, _ = chord(sym)
            pcs = [(root + i) % 12 for i in q]
            c1 = min((near(pc, p1) for pc in pcs), key=lambda n: abs(n - p1))
            c2s = [near(pc, p2) for pc in pcs if pc != c1 % 12]
            c2 = min((n for n in c2s if n < c1), key=lambda n: abs(n - p2), default=c1 - 3)
            a.append(c1)
            b.append(c2)
            p1, p2 = c1, c2
    return a, b


def legato_line(notes):
    """Половинные; одинаковая соседняя нота - связкой"""
    out = []
    for i, n in enumerate(notes):
        if i and notes[i - 1] == n:
            out.append("^2")
        else:
            out.append("o%d%s2" % (n // 12, name(n)))
        if i % 2 == 1:
            out.append("|")
    return " ".join(out)


def harp(harm, top=65):
    out = []
    for h in harm:
        for sym in halves(h):
            v = voicing(sym, top, 3)
            ext = v + [x + 12 for x in v]
            for i in (0, 1, 2, 3):
                n = ext[i]
                out.append("o%d%s8" % (n // 12, name(n)))
        out.append("|")
    return " ".join(out)


def bass(harm):
    out = []
    prev = 29
    for h in harm:
        hs = halves(h)
        for sym in hs:
            _, _, b = chord(sym)
            n = near(b, prev)
            if n < 26:
                n += 12
            if n > 38:
                n -= 12
            prev = n
            out.append("o%d%s2" % (n // 12, name(n)))
        out.append("|")
    return " ".join(out)


def song():
    s = Song("II. АДАЖИО", "СИМФОНИЯ А-Г-А-Т ДЛЯ ЯЗС 5/2", bpm=56, rows_per_beat=4)
    I = s.instrument
    I("cello", "ВИОЛОНЧ.", [HOLD | LOUD | LF | MF], vib=(20, 4, 2), meter=(13, 12, 1))
    I("violin", "СКРИПКА", [HOLD | LOUD | MF | HF], vib=(14, 5, 2), meter=(13, 12, 1))
    I("viola", "АЛЬТ", [HOLD | LOUD | MF | HF], vib=(14, 5, 2), detune=-1, meter=(12, 11, 1))
    I("pad", "СТРУННЫЕ", [HOLD | LF | MF], meter=(9, 8, 1))
    I("pad_f", "СТРУННЫЕ", [HOLD | LOUD | LF | MF], meter=(12, 11, 1))
    I("harp", "АРФА", [HIT | LF | MF], meter=(11, 0, 10))
    I("harp_hi", "АРФА", [HIT | MF | HF], meter=(10, 0, 10))
    I("cbass", "КОНТРАБ.", [HOLD | LOUD | LF], meter=(13, 12, 1))

    A, B = 12, 24                      # начала разделов (такты)
    n_bars = len(HARM)

    # Мелодия: виолончель, скрипка с детюном +1, виолончель
    s.voice(0, "@cello " + " | ".join(CELLO) + " | "
            "@violin {det 1} " + " | ".join(VIOLIN) + " | "
            "{det 0} @cello " + " | ".join(CELLO2) + " |")
    # Фон: два голоса; в кульминации громче
    p1, p2 = pad(HARM)
    for ch, p in ((1, p1), (2, p2)):
        s.voice(ch, "@pad " + legato_line(p[:2 * A]) + " @pad_f " + legato_line(p[2 * A:2 * B])
                + " @pad " + legato_line(p[2 * B:]))
    # Канал 4: арфа в A и A', альт в унисон со скрипкой (хорус) в B
    s.voice(3, "@harp " + harp(HARM[:A]) + " @viola " + " | ".join(VIOLIN) + " | @harp "
            + harp(HARM[B:-1]) + " @harp_hi o5 l16 f a > c f a > c f a r2 |")
    s.voice(4, "@cbass " + bass(HARM))
    # Литавры: тремоло в такте 19, удар на 20; тарелка - на вершине
    s.drums(5, "r1 " * 19 + "l16 " + "K " * 8 + "k " * 8 + "| k4 r4 r2 |", KIT6)
    s.drums(6, "r1 " * 20 + "| x1 |", KIT7)

    br = s.bar_rows
    s.set_tempo(0, 56)
    s.ritard(br * (B - 2), br * B - 1, 56, 50)
    s.set_tempo(br * B, 54)
    s.ritard(br * (n_bars - 3), br * n_bars - 1, 54, 36, every=2)
    s.text(0, "ВИОЛОНЧЕЛЬ: ОДИН D5 - ПЛАВНАЯ АТАКА, ВИБРАТО С ЗАДЕРЖКОЙ")
    s.text(br * 2, "АРФА: УДАР D4 БЕЗ УДЕРЖАНИЯ, СЧ+НЧ")
    s.text(br * A, "СКРИПКА И АЛЬТ В УНИСОН +-1/16 ПОЛУТОНА: ХОРУС")
    s.text(br * 19, "ЛИТАВРЫ: УДАРНЫЙ 6 ТРЕМОЛО, D3 - ГРОМЧЕ")
    s.text(br * B, "МИНОРНАЯ СУБДОМИНАНТА, ЗАТИХАНИЕ")

    harm = []
    for i, h in enumerate(HARM):
        for k, sym in enumerate(halves(h)):
            harm.append((i * br + k * br // 2, sym))
    s.harmony = harm
    return s


if __name__ == "__main__":
    s = song()
    s.build()
    print(s.stats)
    print("вне аккорда:", check_harmony(s, s.harmony, every=s.rpb, label="II"))
