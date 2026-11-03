# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
"""
П. И. Чайковский, «Щелкунчик» (1892): Танец Феи Драже. Переложение для ЯЗС.

Ноты - из полной партитуры (data_sugarplum.py, сведено midi2mml.py): пять
каналов по важности голосов:
- 1: челеста, верхний голос, двумя октавами ниже;
- 2: бас-кларнет, кларнеты, английский рожок, флейты, нижний голос челесты;
- 3: первые скрипки, гобои, вторые скрипки;
- 4: альты, вторые скрипки, валторны, фаготы;
- 5: контрабасы, виолончели.
Пиццикато и arco - по программам партитуры. Сетка 48 строк на долю: в каденции
челесты 64-е и секстоли.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from compose import Song                                     # noqa: E402
from instruments import HF, MF, LF, LOUD, HIT, HOLD           # noqa: E402
import data_sugarplum as D                                   # noqa: E402


def song():
    s = Song("ТАНЕЦ ФЕИ ДРАЖЕ", "ЧАЙКОВСКИЙ, ЩЕЛКУНЧИК (1892)", bpm=D.TEMPO[0][1],
             rows_per_beat=D.RPB, beats_per_bar=2, frame_bars=1)
    I = s.instrument
    I("celesta", "ЧЕЛЕСТА", [HIT | LOUD | LF | MF | HF], arp=[12, 0], meter=(14, 0, 8))
    I("celesta2", "ЧЕЛЕСТА", [HIT | LF | MF | HF], arp=[12, 0], meter=(10, 0, 8))
    I("bclar", "БАС-КЛАР", [HOLD | HIT | LOUD | LF], meter=(13, 11, 2))
    I("clar", "КЛАРНЕТ", [HOLD | HIT | LF | MF], meter=(11, 9, 2))
    I("ehorn", "АНГЛ.РОЖ", [HOLD | HIT | MF], meter=(10, 8, 2))
    I("flute", "ФЛЕЙТА", [HOLD | LOUD | LF], vib=(16, 5, 2), meter=(11, 10, 2))
    I("oboe", "ГОБОЙ", [HOLD | HIT | LOUD | MF | HF], meter=(12, 10, 2))
    I("bassoon", "ФАГОТ", [HOLD | HIT | LF | MF], meter=(11, 9, 2))
    I("horn", "ВАЛТОРНА", [HOLD | LF], meter=(10, 9, 1))
    I("pizz", "ПИЦЦ.", [HIT | LOUD | LF | MF], meter=(12, 0, 12))
    I("pizz_b", "ПИЦЦ.", [HIT | LOUD | LF], meter=(13, 0, 10))
    I("violin", "СКРИПКИ", [HOLD | LOUD | LF | MF], vib=(14, 5, 2), meter=(12, 11, 1))
    I("viola", "АЛЬТЫ", [HOLD | LF | MF], vib=(14, 5, 2), meter=(10, 9, 1))
    I("cello", "ВИОЛОНЧ.", [HOLD | LOUD | LF], meter=(12, 11, 1))
    I("cbass", "КОНТРАБ.", [HOLD | LOUD | LF], meter=(12, 11, 1))

    total = D.BARS * s.bar_rows
    for c in range(5):
        end = s.voice(c, D.CH[c], octave=4, length=4)
        assert end == total, (c, end, total)
    for beat, bpm in D.TEMPO[1:]:
        s.set_tempo(int(round(beat * D.RPB)), bpm)

    br = s.bar_rows
    T = lambda bar, txt: s.text(bar * br + D.RPB // 2, txt)
    T(0, "ПИЦЦИКАТО: БАС НА ДОЛЮ, АККОРД НА СЛАБУЮ")
    T(4, "ЧЕЛЕСТА: ЩИПОК НА ВСЕХ ПОЛОСАХ, ПЕРВЫЙ ТИК НА ОКТАВУ ВЫШЕ")
    T(7, "БАС-КЛАРНЕТ: ХРОМАТИЧЕСКИЕ ПАССАЖИ 32-МИ, ПОЛОСА НЧ")
    T(16, "ДЕРЕВЯННЫЕ: ФЛЕЙТА НА НЧ - ПОЧТИ СИНУС, ГОБОЙ НА СЧ+ВЧ")
    T(32, "КАДЕНЦИЯ ЧЕЛЕСТЫ: 64-Е И СЕКСТОЛИ, 48 СТРОК НА ДОЛЮ")
    T(36, "РЕПРИЗА: СКРИПКИ ARCO - ПЛАВНАЯ АТАКА D5 И ВИБРАТО")
    return s


if __name__ == "__main__":
    s = song()
    s.build()
    print(s.stats)
    for t in s.texts:
        assert len(t) <= 64
