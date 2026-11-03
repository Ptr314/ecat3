# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
"""
Симфония «А-Г-А-Т», часть V: Электрический сон, синтвейв, ми минор, 124 BPM.

Что показывает плата: арпеджиатор 16-ми с «фильтром» - эффектом BUS полосы
переключаются НЧ -> НЧ+СЧ -> СЧ -> СЧ+ВЧ, как развертка среза; октавный бас
восьмыми; лид со скольжением (PORTA) и вибрато; эхо - копия лида на пунктирную
восьмую позже, без D3; лазеры - макрос высоты, падающей на 1.5 полутона за
тик; подъем перед дропом - непрерывное скольжение (SLIDE_UP); в бридже -
аккорды в одном канале эффектом ARP (0-3-7 по тикам, «чиптюн»). Бочка и малый
- ударный 6, хэты - ударный 7. В дропе - лейтмотив ля-соль-ля-си.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from compose import Song                                     # noqa: E402
from instruments import HF, MF, LF, LOUD, HIT, HOLD, KIT6, KIT7   # noqa: E402
from common import chord, near, voicing, name, check_harmony   # noqa: E402

LOOP = ["Em", "C", "G", "D"]
BRIDGE = ["Am", "C", "D", "Em", "Am", "C", "D", "D"]

VERSE = [
    "o5 e4. d8 e4 g4", "o5 e2 g4 e4", "o5 d4. c8 d4 o4 b4", "o4 a2. r4",
    "o5 e4. d8 e4 g4", "o5 g4. a8 g4 e4", "o5 d4 o4 b4 g4 b4", "o4 a2. r4",
]
DROP = [
    "o5 a8. g16 a8 b8 b2", "o6 c8 o5 b8 g8 e8 e2", "o5 d8. c16 d8 g8 g2", "o5 f#4 e4 d4 o4 a4",
    "o5 a8. g16 a8 b8 b2", "o6 c8 o5 b8 > c8 < b8 g4 e4", "o5 d4 g4 b4 a4", "o5 f#2. r4",
]
BRIDGE_MEL = [
    "o5 c2 e2", "o5 e2 g2", "o5 f#2 a2", "o5 g1",
    "o5 a2 > c2", "o6 e2 c2", "o5 a2 f#2", "o5 d2 {su 3} d2 {su 0}",
]


def bass_line(harm):
    out = []
    for sym in harm:
        _, _, b = chord(sym)
        n = near(b, 28)
        lo, hi = "o%d%s8" % (n // 12, name(n)), "o%d%s8" % ((n + 12) // 12, name(n))
        out.append(" ".join([lo, hi] * 4) + " |")
    return " ".join(out)


def arp_line(harm, top=76, pattern=(0, 1, 2, 3, 4, 3, 2, 1)):
    out = []
    for sym in harm:
        v = voicing(sym, top - 12, 3)
        ext = v + [x + 12 for x in v] + [x + 24 for x in v]
        notes = [ext[i] for i in pattern] * 2
        out.append(" ".join("o%d%s16" % (n // 12, name(n)) for n in notes) + " |")
    return " ".join(out)


def pad_line(harm, ref=67):
    out = []
    prev = ref
    for sym in harm:
        root, q, _ = chord(sym)
        pcs = [(root + i) % 12 for i in q]
        n = min((near(pc, prev) for pc in pcs), key=lambda x: abs(x - prev))
        prev = n
        out.append("o%d%s1 |" % (n // 12, name(n)))
    return " ".join(out)


def song():
    s = Song("V. ЭЛЕКТРИЧЕСКИЙ СОН", "СИМФОНИЯ А-Г-А-Т ДЛЯ ЯЗС 5/2", bpm=124, rows_per_beat=4)
    I = s.instrument
    I("lead", "СИНТ-ЛИД", [HOLD | HIT | LOUD | MF | HF], vib=(22, 5, 2), meter=(15, 13, 2))
    I("echo", "ЭХО", [HOLD | HIT | MF], vib=(22, 5, 2), meter=(9, 7, 3))
    I("arp", "АРПЕДЖИО", [HIT | LOUD | MF | HF], meter=(12, 0, 16))
    I("bass", "СИНТ-БАС", [HIT | LOUD | LF], meter=(15, 0, 10))
    I("pad", "ПЭД", [HOLD | LOUD | LF | MF], vib=(40, 3, 1), meter=(11, 10, 1))
    I("chip", "ЧИПТЮН", [HOLD | HIT | LOUD | MF | HF], meter=(13, 12, 1))
    I("zap", "ЛАЗЕР", [HIT | LOUD | MF | HF], pitch=[-24] * 14 + [0], meter=(15, 0, 12))
    I("riser", "ПОДЪЕМ", [HOLD | LOUD | MF | HF], meter=(12, 12, 0))

    sec = [("intro", 8), ("verse", 16), ("break", 8), ("drop", 16), ("bridge", 8),
           ("drop2", 8), ("outro", 6)]
    at = {}
    pos = 0
    for n, ln in sec:
        at[n] = pos
        pos += ln
    total = pos
    harm = (LOOP * 2 + LOOP * 4 + LOOP * 2 + LOOP * 4 + BRIDGE + LOOP * 2
            + ["Em", "C", "Em", "Em", "Em", "Em"])
    assert len(harm) == total
    rest = lambda n: "r1 | " * n

    # Канал 1: лид
    s.voice(0, "@lead {porta 6} " + rest(6) + "@zap o6 c4 r4 c4 r4 | e4 r4 c4 c4 | @lead "
            + " | ".join(VERSE * 2) + " | "
            + rest(4) + "@riser {su 1} o5 e1 ^1 | {su 0} r1 | r1 | @lead "
            + " | ".join(DROP * 2) + " | " + " | ".join(BRIDGE_MEL) + " | "
            + " | ".join(DROP) + " | o5 e1 | ^1 | @zap o6 e4 r4 c4 r4 | r1 | r1 | r1 |")
    # Канал 2: пэд; в бридже - аккорды ARP
    s.voice(1, "@pad " + pad_line(harm[:at["bridge"]]) + " @chip "
            + " ".join("{arp 0x%d7} o%d%s1 |" % (4 if chord(h)[1][1] == 4 else 3,
                                                 (n := near(chord(h)[0], 64)) // 12, name(n))
                       for h in BRIDGE)
            + " {arp 0} @pad " + pad_line(harm[at["drop2"]:]))
    # Канал 3: арпеджиатор с разверткой «фильтра»
    sweep = ["{bus 12}", "{bus 12}", "{bus 14}", "{bus 14}", "{bus 10}", "{bus 10}",
             "{bus 11}", "{bus 9}"]
    intro = " ".join(sw + " " + bar for sw, bar in zip(sweep, arp_line(harm[:8]).split(" |")[:8])) + " |"
    s.voice(2, "@arp " + intro + " {bus 0xFF} " + arp_line(harm[8:at["break"]])
            + " {bus 2} " + arp_line(harm[at["break"]:at["drop"]]) + " {bus 0xFF} "
            + arp_line(harm[at["drop"]:at["bridge"]]) + rest(8) + arp_line(harm[at["drop2"]:at["outro"]])
            + " {bus 11} " + " ".join(sw + " " + bar for sw, bar in zip(
                ["{bus 11}", "{bus 10}", "{bus 14}", "{bus 12}", "{bus 4}", "{bus 4}"],
                arp_line(harm[at["outro"]:]).split(" |")[:6])) + " |")
    # Канал 4: эхо лида (на 3 строки позже) в куплете и дропе, в бридже - бас-пэд
    def echo(bars):
        # на 3 строки позже: у последней паузы такта 3 строки отрезаны
        t = " ".join(bars)          # без черт: эхо сдвинуто на 3 строки
        assert t.endswith(" r4")
        return "r8. " + t[:-3] + " r16 | "
    s.voice(3, "@echo " + rest(8) + echo(VERSE * 2) + rest(8) + echo(DROP * 2) + rest(8)
            + echo(DROP) + rest(6))
    # Канал 5: бас
    s.voice(4, "@bass q6 " + rest(8) + bass_line(harm[8:at["break"]]) + rest(8)
            + bass_line(harm[at["drop"]:at["outro"]]) + " o2 e1 | ^1 | " + rest(4))
    # Ударные
    beat = "k4 s4 k8 k8 s4 | "
    s.drums(5, rest(8) + beat * 16 + rest(6) + "l16 " + "s " * 16 + " | " + "s " * 8 + "k " * 8 + " | "
            + beat * 16 + rest(8) + beat * 8 + "k4 s4 k4 s4 | k1 |" + rest(4), KIT6)
    hats = "r8 h8 r8 h8 r8 h8 r8 o8 | "
    s.drums(6, rest(8) + hats * 16 + hats * 8 + "c1 | " + hats * 15 + "h4 h4 h4 h4 | " * 8
            + "c1 | " + hats * 7 + rest(6), KIT7)

    br = s.bar_rows
    s.set_tempo(0, 124)
    s.ritard(br * (total - 3), br * total - 1, 124, 96, every=4)
    s.text(0, "АРПЕДЖИО 16-МИ, BUS: НЧ -> СЧ -> ВЧ - РАЗВЕРТКА ФИЛЬТРА")
    s.text(br * 6, "ЛАЗЕР: МАКРОС ВЫСОТЫ, -1.5 ПОЛУТОНА ЗА ТИК")
    s.text(br * at["verse"], "ЛИД СО СКОЛЬЖЕНИЕМ, ЭХО НА ПУНКТИРНУЮ ВОСЬМУЮ БЕЗ D3")
    s.text(br * at["break"], "ПОДЪЕМ: НЕПРЕРЫВНОЕ СКОЛЬЖЕНИЕ ВВЕРХ")
    s.text(br * at["drop"], "ДРОП: ЛЕЙТМОТИВ ЛЯ-СОЛЬ-ЛЯ-СИ")
    s.text(br * at["bridge"], "ЧИПТЮН: АККОРД В ОДНОМ КАНАЛЕ ЭФФЕКТОМ ARP")
    s.text(br * at["outro"], "ФИЛЬТР ЗАКРЫВАЕТСЯ: ВЧ -> СЧ -> НЧ")
    s.harmony = [(i * br, h) for i, h in enumerate(harm)]
    return s


if __name__ == "__main__":
    s = song()
    s.build()
    print(s.stats)
    print("вне аккорда:", check_harmony(s, s.harmony, every=s.rpb, label="V", channels=(0, 1, 4)))
