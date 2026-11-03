# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
"""
Н. А. Римский-Корсаков, «Полет шмеля» (1899-1900). Переложение для ЯЗС.

Партия солиста - октавой ниже оригинала (data_bumblebee.py): шестнадцатые
лигой, без нового удара на каждой ноте; «жужжит» сам узкий импульс ячейки ниже
~550 Гц. Повторы одной ноты (тремоло 32-ми) - новым ударом. Переключать полосу
каждый тик нельзя: шина ВЧ - фильтр верхних частот, и каждое переключение -
щелчок (в первой версии энергия выше 3 кГц была в 15 раз больше).

Аккомпанемент свой, по аккордам, подобранным по мелодии: стаккато квинты и
терции на каждую долю, бас пиццикато (корень и квинта), тремоло струнных -
повторный удар каждые 3 тика (RETRIG).
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from compose import Song                                     # noqa: E402
from instruments import MF, LF, LOUD, HIT, HOLD               # noqa: E402
import data_bumblebee as D                                   # noqa: E402


def song():
    s = Song("ПОЛЕТ ШМЕЛЯ", "РИМСКИЙ-КОРСАКОВ (1900)", bpm=D.TEMPO[0][1],
             rows_per_beat=D.RPB, beats_per_bar=2, frame_bars=2)
    I = s.instrument
    I("bee", "ШМЕЛЬ", [HOLD | HIT | LOUD | LF | MF], meter=(15, 13, 2))
    I("trem", "ТРЕМОЛО", [HIT | LF | MF], meter=(9, 0, 6))
    I("stacc", "АККОРДЫ", [HIT | LF | MF], meter=(10, 0, 12))
    I("pizzb", "БАС", [HIT | LOUD | LF], meter=(13, 0, 10))

    total = D.BARS * s.bar_rows
    voices = ["@bee " + D.CH[0], "@trem {retrig 3} " + D.CH[1], "@stacc " + D.CH[2],
              "@stacc " + D.CH[3], "@pizzb " + D.CH[4]]
    for c, text in enumerate(voices):
        end = s.voice(c, text, octave=4, length=4)
        assert end == total, (c, end, total)

    br = s.bar_rows
    T = lambda bar, txt: s.text(bar * br + 2, txt)
    T(0, "ШМЕЛЬ: ШЕСТНАДЦАТЫЕ ЛИГОЙ, БЕЗ НОВОГО УДАРА НА НОТУ")
    T(5, "СТАККАТО НА КАЖДУЮ ДОЛЮ, ТРЕМОЛО СТРУННЫХ - ПОВТОРНЫЙ УДАР")
    T(22, "ТРЕМОЛО 32-МИ: ТА ЖЕ НОТА - НОВЫМ УДАРОМ D4")
    return s


if __name__ == "__main__":
    s = song()
    s.build()
    print(s.stats)
    for t in s.texts:
        assert len(t) <= 64
