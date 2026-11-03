# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
"""
Э. Григ, «Пер Гюнт», сюита 1, ор. 46 № 3 (1888): Танец Анитры. Переложение для ЯЗС.

Ноты - по набору Mutopia № 281 для струнных и треугольника (data_anitra.py,
сведено midi2mml.py), Tempo di Mazurka, 3/4, сетка 16 строк на долю (мелизмы -
64-е перед долей). Каналы:
- 1: первые скрипки с сурдиной - полоса СЧ, удар только в первый тик (на быстрых
  нотах слышна каждая), вибрато;
- 2-4: вторые скрипки, альты, виолончели пиццикато - короткий щипок, без D3;
- 5: контрабасы пиццикато;
- ударный 7: треугольник - самый высокий шум ($0B, D3 - звенит дольше).
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from compose import Song                                     # noqa: E402
from instruments import HF, MF, LF, LOUD, HIT, HOLD, KIT7    # noqa: E402
import midi2mml                                               # noqa: E402
import data_anitra as D                                       # noqa: E402

TEMPO = 132


def triangle(total):
    lens = midi2mml._lengths(D.RPB)
    out, pos = [], 0
    for r in sorted(set(D.TRIANGLE)) + [total]:
        if r > pos:
            out += ["r" + lens[p] for p in midi2mml._split(r - pos, lens)]
            pos = r
        if r < total:
            out.append("H" + lens[1])
            pos += 1
    return " ".join(out)


def song():
    s = Song("ТАНЕЦ АНИТРЫ", "Э. ГРИГ, ПЕР ГЮНТ, ОР. 46", bpm=TEMPO,
             rows_per_beat=D.RPB, beats_per_bar=3, frame_bars=1)
    I = s.instrument
    I("vln", "СКРИПКИ", [HOLD | HIT | LOUD | MF, HOLD | LOUD | MF], vib=(16, 5, 2), meter=(13, 12, 1))
    I("pizz", "ПИЦЦ.", [HIT | LF | MF, HIT | LF], meter=(10, 0, 10))
    I("pizzc", "ВИОЛОНЧ.", [HIT | LOUD | LF, HIT | LF], meter=(11, 0, 8))
    I("pizzb", "КОНТРАБ.", [HIT | LOUD | LF], meter=(12, 0, 6))

    total = D.BARS * s.bar_rows
    for c, ins in enumerate(["vln", "pizz", "pizz", "pizzc", "pizzb"]):
        end = s.voice(c, "@%s %s" % (ins, D.CH[c]), octave=4, length=4)
        assert end == total, (c, end, total)
    s.drums(5, "r2. | " * D.BARS, KIT7, length=4)
    end = s.drums(6, triangle(total), KIT7, length=4)
    assert end == total, end

    # Последние такты - замедление
    br = s.bar_rows
    s.ritard((D.BARS - 3) * br, total - 1, TEMPO, 92, every=D.RPB)
    T = lambda bar, txt: s.text(bar * br + 4, txt)
    T(0, "МАЗУРКА: СКРИПКИ С СУРДИНОЙ - СЧ, УДАР ТОЛЬКО В ПЕРВЫЙ ТИК")
    T(2, "ПИЦЦИКАТО ВТОРЫХ СКРИПОК, АЛЬТОВ, ВИОЛОНЧЕЛЕЙ - БЕЗ D3")
    T(5, "МЕЛИЗМЫ - 64-Е ПЕРЕД ДОЛЕЙ, 16 СТРОК НА ДОЛЮ")
    T(6, "ТРЕУГОЛЬНИК - УДАРНЫЙ 7: САМЫЙ ВЫСОКИЙ ШУМ С D3")
    return s


if __name__ == "__main__":
    s = song()
    s.build()
    print(s.stats)
    for t in s.texts:
        assert len(t) <= 64, len(t)
