# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
"""
Тур по тембрам ЯЗС: что умеет ячейка, по одному приему, с подписями на
экране. Фраза - лейтмотив ля-соль-ля-си-до-ми-ля.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from compose import Song                                     # noqa: E402
from instruments import HF, MF, LF, LOUD, HIT, HOLD, KIT6, KIT7   # noqa: E402

PHRASE = "o4 a8 g8 a8 b8 > c8 e8 < a4"          # такт 4/4

BUS = [(LF, "НЧ"), (MF, "СЧ"), (HF, "ВЧ"), (LF | MF, "НЧ+СЧ"), (MF | HF, "СЧ+ВЧ"),
       (LF | HF, "НЧ+ВЧ"), (LF | MF | HF, "НЧ+СЧ+ВЧ")]


def song():
    s = Song("ТУР ПО ТЕМБРАМ ЯЗС 5/2", "ЧТО УМЕЕТ ЯЧЕЙКА: ПО ОДНОМУ ПРИЕМУ", bpm=126,
             rows_per_beat=4)
    br = s.bar_rows
    v0 = []                    # такты голоса 1
    others = {1: [], 2: [], 3: [], 4: []}
    d6, d7 = [], []
    bar = [0]

    def step(caption, text, ins=None, bars=1, extra=None, drum6=None, drum7=None):
        s.text(bar[0] * br, caption)
        v0.append(("@%s " % ins if ins else "") + text)
        for ch in others:
            others[ch].append((extra or {}).get(ch, "r1 | " * bars))
        d6.append(drum6 or "r1 | " * bars)
        d7.append(drum7 or "r1 | " * bars)
        bar[0] += bars

    I = s.instrument
    # 1. Полосы
    for bits, label in BUS:
        I("b%d" % bits, label, [HOLD | HIT | LOUD | bits], meter=(14, 12, 2))
        step("ПОЛОСА %s: D0 ВЧ, D1 СЧ, D2 НЧ" % label, PHRASE + " | ", "b%d" % bits)
    # 2. Огибающие
    I("e_hit", "D4", [HIT | LOUD | LF | MF], meter=(15, 0, 8))
    I("e_hold", "D5", [HOLD | LOUD | LF | MF], meter=(12, 12, 1))
    I("e_both", "D4+D5", [HOLD | HIT | LOUD | LF | MF], meter=(15, 12, 2))
    step("D4: УДАР, СПАД ~0.5 С (ЩИПОК)", "o4 a4 g4 a4 b4 | > c2 e2 | ", "e_hit", 2)
    step("D5: ПЛАВНАЯ АТАКА ~0.3 С, УДЕРЖАНИЕ", "o4 a4 g4 a4 b4 | > c2 e2 | ", "e_hold", 2)
    step("D4+D5: УДАР С УДЕРЖАНИЕМ (ОРГАН)", "o4 a4 g4 a4 b4 | > c2 e2 | ", "e_both", 2)
    # 3. Громкость D3
    I("v_soft", "БЕЗ D3", [HOLD | HIT | LF | MF], meter=(9, 7, 2))
    step("D3 = 0: НА 8-10 ДБ ТИШЕ", PHRASE + " | ", "v_soft")
    step("D3 = 1: ГРОМЧЕ", PHRASE + " | ", "e_both")
    # 4. Ударные
    I("silent", "-", [HOLD], meter=(0, 0, 0))
    step("УДАРНЫЙ 6: 8 ВЫСОТ (D0-D2), D3 = 0 И 1", "r1 | r1 | r1 | r1 | ", "silent", 4,
         drum6="l8 =0 r8 =1 r8 =2 r8 =3 r8 | =4 r8 =5 r8 =6 r8 =7 r8 | "
               "=8 r8 =9 r8 =10 r8 =11 r8 | =12 r8 =13 r8 =14 r8 =15 r8 | ")
    step("УДАРНЫЙ 7: ШУМ 7-11 КГЦ, 4 ВЫСОТЫ, D3", "r1 | r1 | ", "silent", 2,
         drum7="l4 =0 =1 =2 =3 | =8 =9 =10 =11 | ")
    # 5. Высота и эффекты
    I("lead", "СОЛО", [HOLD | HIT | LOUD | MF | HF], meter=(14, 12, 2))
    step("ВИБРАТО: ТАБЛИЦА СИНУСА, ГЛУБИНА 3/16 ПОЛУТОНА",
         "{vib 0x53} o5 a1 | {vib 0x73} a1 {vib 0} | ", "lead", 2)
    step("СКОЛЬЖЕНИЕ: СЧЕТЧИК ВИ53 МЕНЯЕТ ПЕРИОД КАЖДЫЙ ТИК",
         "{su 4} o4 a1 {su 0} | {sd 4} o5 a1 {sd 0} | ", "lead", 2)
    step("ПОРТАМЕНТО: ЛИГА СО СКОЛЬЖЕНИЕМ К НОТЕ",
         "{porta 3} o4 a4 _g4 _a4 _b4 | > _c4 _e4 < _a2 {porta 0} | ", "lead", 2)
    step("АРПЕДЖИО: АККОРД В ОДНОМ КАНАЛЕ (0-3-7 ПО ТИКАМ)",
         "{arp 0x37} o4 a1 | {arp 0x47} f1 {arp 0} | ", "e_both", 2)
    step("ТРЕМОЛО: D5 ПЕРЕКЛЮЧАЕТСЯ, RC ЯЧЕЙКИ СГЛАЖИВАЕТ",
         "{trem 10} o4 a1 | {trem 6} a1 {trem 0} | ", "e_hold", 2)
    I("chorus_a", "ХОРУС", [HOLD | HIT | LOUD | MF | LF], detune=1, meter=(13, 12, 2))
    I("chorus_b", "ХОРУС", [HOLD | HIT | LOUD | MF | LF], detune=-1, meter=(13, 12, 2))
    step("ХОРУС: ДВА КАНАЛА В УНИСОН, РАССТРОЙКА +-1/16", "o4 a2 e2 | a1 | ", "chorus_a", 2,
         extra={1: "@chorus_b o4 a2 e2 | a1 | "})
    I("echo", "ЭХО", [HOLD | HIT | MF], meter=(9, 7, 3))
    step("ЭХО: КОПИЯ НА 3 СТРОКИ ПОЗЖЕ, БЕЗ D3", PHRASE + " | " + PHRASE + " | ", "lead", 2,
         extra={2: "@echo r8. o4 a8 g8 a8 b8 > c8 e8 < a4 a8 g8 a8 b8 > c8 e8 < a16 | "})
    step("ФИЛЬТР: ПОЛОСЫ ПЕРЕКЛЮЧАЮТСЯ НА ХОДУ",
         "{bus 12} o4 a4 {bus 14} a4 {bus 10} a4 {bus 11} a4 | {bus 9} a4 {bus 11} a4 {bus 14} a4 "
         "{bus 12} a4 {bus 0xFF} | ", "e_both", 2)
    I("zap", "ЛАЗЕР", [HIT | LOUD | MF | HF], pitch=[-24] * 14 + [0], meter=(15, 0, 12))
    step("ЛАЗЕР: МАКРОС ВЫСОТЫ -1.5 ПОЛУТОНА ЗА ТИК", "o6 c4 c4 e4 c4 | c8 c8 e8 c8 g4 c4 | ",
         "zap", 2)
    I("kick", "СИНТ-БОЧ", [HIT | LOUD | LF], pitch=[-48, -40, -32, -24, -16, -12, -8, -4, 0],
      meter=(15, 0, 14))
    step("СИНТ-БОЧКА НА ТОНОВОМ КАНАЛЕ: ПАДАЮЩАЯ ВЫСОТА", "o4 c4 c4 c4 c8 c8 | c4 c4 c4 r4 | ",
         "kick", 2)
    # 6. Все вместе
    I("pad", "ПЭД", [HOLD | LOUD | LF | MF], meter=(11, 10, 1))
    I("bass", "БАС", [HIT | LOUD | LF], meter=(15, 0, 6))
    step("ВСЕ ВМЕСТЕ: 5 ТОНОВ И 2 УДАРНЫХ",
         PHRASE + " | o5 c2 e2 | a1 | r1 | ", "e_both", 4,
         extra={1: "@pad o4 e1 | f1 | e1 | r1 | ", 2: "@pad o4 c1 | c1 | c#1 | r1 | ",
                3: "@pad o3 a1 | a1 | a1 | r1 | ", 4: "@bass q6 o2 a8 a8 a8 a8 a8 a8 a8 a8 | "
                "f8 f8 f8 f8 f8 f8 f8 f8 | a2 r2 | r1 | "},
         drum6="=8:4 =14:4 =8:4 =14:4 | =8:4 =14:4 =8:4 =14:4 | =8:1 | r1 | ",
         drum7="l8 r8 =3 r8 =3 r8 =3 r8 =3 | r8 =3 r8 =3 r8 =3 r8 =3 | =8:1 | r1 | ")

    s.voice(0, " ".join(v0))
    for ch, parts in others.items():
        s.voice(ch, " ".join(parts))
    s.voice(5, " ".join(d6))
    s.voice(6, " ".join(d7))
    s.set_tempo(0, 126)
    return s


if __name__ == "__main__":
    s = song()
    s.build()
    print(s.stats)
