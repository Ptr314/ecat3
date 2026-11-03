# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
"""
С. Джоплин, The Entertainer (1902). Переложение для ЯЗС.

Ноты - по набору Mutopia (data_entertainer.py), повторы: A и B дважды, A,
трио один раз, переход, D дважды. Каналы:
- 1: правая рука, верхний голос;
- 2: правая рука, второй голос сверху (октава или терция), чуть расстроен -
  «разбитое» пианино;
- 3, 4: левая рука, аккорд на слабую долю (две верхние ноты);
- 5: левая рука, бас на долю.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from compose import Song                                     # noqa: E402
from instruments import HF, MF, LF, LOUD, HIT                # noqa: E402
import data_entertainer as D                                 # noqa: E402

TEMPO = 80


def song():
    s = Song("THE ENTERTAINER", "СКОТТ ДЖОПЛИН (1902), РЭГТАЙМ", bpm=TEMPO,
             rows_per_beat=D.RPB, beats_per_bar=2, frame_bars=2)
    I = s.instrument
    I("rh", "ФОРТЕП.", [HIT | LOUD | LF | MF | HF], meter=(14, 0, 8))
    I("rh2", "ФОРТЕП.", [HIT | LOUD | LF | MF | HF, HIT | LOUD | LF | MF], detune=1, meter=(12, 0, 8))
    I("lh", "ФОРТЕП.", [HIT | LOUD | LF | MF], meter=(11, 0, 10))
    I("bass", "ФОРТЕП.", [HIT | LOUD | LF | MF, HIT | LOUD | LF], meter=(13, 0, 8))

    total = D.BARS * s.bar_rows
    for c, ins in enumerate(["rh", "rh2", "lh", "lh", "bass"]):
        end = s.voice(c, "@" + ins + " " + D.CH[c], octave=4, length=4)
        assert end == total, (c, end, total)

    br = s.bar_rows
    T = lambda bar, txt: s.text(bar * br + 1, txt)
    T(0, "РЭГТАЙМ: ПРАВАЯ РУКА ОКТАВАМИ - ДВА КАНАЛА, ВТОРОЙ РАССТРОЕН")
    T(4, "ЛЕВАЯ РУКА - СТРАЙД: БАС НА ДОЛЮ, АККОРД НА СЛАБУЮ")
    T(36, "ВТОРАЯ ТЕМА")
    T(68, "ПЕРВАЯ ТЕМА")
    T(84, "ТРИО: ФА МАЖОР")
    T(104, "ПОСЛЕДНЯЯ ТЕМА")
    return s


if __name__ == "__main__":
    s = song()
    s.build()
    print(s.stats)
    for t in s.texts:
        assert len(t) <= 64
