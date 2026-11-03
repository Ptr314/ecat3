# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
"""Проверка движка: все команды и эффекты по разу"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from compose import Song                                    # noqa: E402
from instruments import add_standard, KIT6, KIT7, HF, MF, LF, LOUD, HIT, HOLD   # noqa: E402


def song():
    s = Song("ПРОВЕРКА ДВИЖКА", "ВСЕ ЭФФЕКТЫ", bpm=120, rows_per_beat=4)
    add_standard(s)
    s.instrument("arpchord", "АРПЕДЖ.", [HOLD | HIT | LOUD | MF | LF], arp=[0, 4, 7], arp_loop=0)
    s.instrument("zap", "ЛАЗЕР", [HIT | LOUD | MF | HF], pitch=[-24] * 12 + [0], meter=(15, 0, 12))
    s.instrument("wah", "ВАУ", [HOLD | HIT | LOUD | HF, HOLD | HIT | LOUD | HF | MF, HOLD | LOUD | MF,
                                HOLD | LOUD | MF | LF, HOLD | LOUD | LF], meter=(14, 10, 2))
    s.text(0, "НОТЫ И ИНСТРУМЕНТЫ")
    s.voice(0, "@organ o4 l4 c e g > c | @pluck l8 c < g e c g e c r | "
               "@strings l2 a _b | {porta 6} > _c < _a | "
               "@lead {vib 0x53} l1 e | {vib 0} {su 2} l2 c {su 0} {sd 3} c {sd 0} | "
               "@arpchord l1 c | {arp 0x37} @organ l1 c {arp 0} | "
               "@zap l4 > c c c c | @wah l2 < c c | {trem 8} @strings l1 g {trem 0} | "
               "{retrig 6} @piano l1 c {retrig 0} | {det -8} @organ l1 c {det 0} | "
               "{tr 12} l4 c {tr 0} {delay 3} c {cut 2} c {bus 2} c {bus 0xFF} | r1 |")
    s.voice(4, "@bass o2 l8 [c c g g a a g r]8 l1 r r r r r r r |")
    s.drums(5, "l8 [k r s r k k s r]8 l1 r r r r r r r |", KIT6)
    s.drums(6, "l8 [h h h o]16 l1 r r r r r r r |", KIT7)
    s.text(s.bar(8), "АРПЕДЖИО, ЛАЗЕР, ФИЛЬТР")
    return s


if __name__ == "__main__":
    s = song()
    data = s.build()
    out = sys.argv[1] if len(sys.argv) > 1 else "test_fx.yz"
    open(out, "wb").write(data)
    print(s.stats)
