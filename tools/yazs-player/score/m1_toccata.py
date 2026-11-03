# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
"""
Симфония «А-Г-А-Т», часть I: Токката и фуга, ля минор.

Лейтмотив симфонии - криптограмма слова АГАТ: ля (A), соль (G), ля (A),
си (Т = «ти»). Токката объявляет его октавами полного органа, фуга строит
на нем тему. Регистровка - сочетания полос ячейки: полный орган - все три
полосы с D3, флейта - СЧ и ВЧ без D3, педаль - НЧ с D3. Удар D4 на каждой
ноте дает «чиф» органной трубы, D5 держит звук.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from compose import Song                                     # noqa: E402
from instruments import HF, MF, LF, LOUD, HIT, HOLD          # noqa: E402
from common import check_harmony, tp                         # noqa: E402

# Тема фуги (ля минор) и противосложение (к ответу в ми миноре): по два такта
S = ["a8 g8 a8 b8 > c8 e8 < a4", "g#16 a16 b16 > c16 d8 c16 < b16 > c8 < b8 a4"]
CS = ["g2 b4 > c8 < b8", "b4 a4 b8 a8 g4"]


def song():
    s = Song("I. ТОККАТА И ФУГА", "СИМФОНИЯ А-Г-А-Т ДЛЯ ЯЗС 5/2", bpm=66,
             rows_per_beat=8, beats_per_bar=4)
    I = s.instrument
    I("tutti", "ОРГАН", [HOLD | HIT | LOUD | LF | MF | HF], meter=(15, 12, 2))
    I("princ", "ПРИНЦИП.", [HOLD | HIT | LOUD | LF | MF], meter=(13, 11, 2))
    I("flute", "ФЛЕЙТА 4", [HOLD | HIT | LOUD | MF | HF], meter=(12, 10, 2))
    I("gedackt", "БУРДОН 8", [HOLD | HIT | MF | LF], meter=(10, 8, 2))
    I("pedal", "ПЕДАЛЬ16", [HOLD | HIT | LOUD | LF], meter=(14, 12, 2))
    I("trem", "ТРЕМУЛЯН", [HOLD | HIT | LOUD | LF | MF | HF], vib=(30, 5, 2), meter=(15, 12, 2))

    bars = []                        # (гармония, {голос: текст})

    def bar(h, **v):
        bars.append((h, v))

    # ---- Токката, такты 0-9 ----
    bar("Am", v0="o5 a8 g16 a16 b8 > c16 < b16 a2", v1="o4 a8 g16 a16 b8 > c16 < b16 a2",
        v4="r2 o1 a2")
    bar(["E", "E7b9"], v0="o6 l32 e d c < b a g# f e d c < b a g# f e d o4 b2",
        v1="r2 o4 f2", v2="r2 o4 d2", v3="r2 o3 g#2", v4="o1 e1")
    bar("Am", v0="o5 l16 a e c e a e c e > c < a e a > c < a e a",
        v1="o4 e1", v2="o4 c1", v3="o3 a1", v4="o1 a1")
    bar(["Dm", "E"], v0="o5 l16 a f d f a f d f b g# e g# b g# e g#",
        v1="o4 f2 e2", v2="o4 d2 o3 b2", v3="o3 a2 g#2", v4="o2 d2 o1 e2")
    bar("Am", v0="o5 e1", v1="o5 c1", v2="o4 a1", v3="o3 a8 g16 a16 b8 > c16 < b16 a2",
        v4="o1 a1")
    bar(["F", "Bdim"], v0="o5 l16 a f c f a f c f b f d f b f d f",
        v1="o5 c2 o4 b2", v2="o4 a2 f2", v3="o3 f2 d2", v4="o1 f2 b2")
    bar("E7", v0="o4 l32 [b > c <]8 o4 g#2", v1="o4 e1", v2="o4 d1", v3="o3 g#1", v4="o1 e1")
    bar(["Am", "Dm/F"], v0="o5 l16 c e a e c e a e d f a f d f a f",
        v1="o4 a2 a2", v2="o4 e2 f2", v3="o3 c2 d2", v4="o1 a2 f2")
    bar(["E7", "E"], v0="o5 l16 b g# e g# d e g# b o5 g#2",
        v1="o5 e2 o4 b2", v2="o4 d2 g#2", v3="o3 b2 e2", v4="o1 e1")
    bar(["E", "-"], v0="o5 b2 r2", v1="o5 e2 r2", v2="o4 g#2 r2", v3="o3 e2 r2", v4="o1 e2 r2")

    # ---- Фуга, такты 10-37 ----
    F0 = len(bars)
    # Экспозиция: альт - тема, сопрано - ответ, тенор - тема, педаль - ответ
    bar("Am", v1="@princ o4 " + S[0])
    bar(["E", "E7", "Am", "Am"], v1="o4 " + S[1])
    bar("Em", v0="@flute " + tp(S[0], 7), v1="o4 " + CS[0])
    bar(["B", "B7", "Em", "Em"], v0=tp(S[1], 7), v1="o4 " + CS[1])
    bar("Am", v0="o5 a2 > c2", v1="o5 c2 e4 f8 e8", v2="@gedackt o3 " + S[0])
    bar(["E", "E7", "Am", "Am"], v0="o5 g#2 a2", v1="o5 e4 d4 e8 d8 c4", v2="o3 " + S[1])
    bar("Em", v0="o5 g2 b2", v1="o5 e1", v2="o3 " + CS[0], v4="@pedal " + tp(S[0], 7, 1))
    bar(["B7", "B7", "Em", "Em"], v0="o5 a2 g2", v1="o5 d#2 e2", v2="o3 " + CS[1],
        v4=tp(S[1], 7, 1))
    # Эпизод 1: секвенция по квинтам вниз
    bar(["Dm", "G"], v0="o5 l16 d f a f e d c# d < b > d g d c < b a b",
        v1="o4 a2 b2", v2="o3 f2 d2", v4="o2 d2 o1 g2")
    bar(["C", "F"], v0="o5 l16 c e g e f e d e < a > c f c < b- a g a",
        v1="o4 g2 a2", v2="o3 e2 c2", v4="o2 c2 o1 f2")
    bar(["Bdim", "E"], v0="o4 l16 b > d f d e d c d < g# b > e < b > d c < b > c",
        v1="o4 f2 e2", v2="o3 d2 b2", v4="o1 b2 e2")
    bar(["Am", "E7"], v0="o4 l16 a > c e c d c < b > c < b g# e g# b > d c < b",
        v1="o4 e2 d2", v2="o3 c2 g#2", v4="o1 a2 e2")
    # Средний проход: тема в ре миноре у сопрано
    bar("Dm", v0=tp(S[0], 5), v1="o4 f2 a4 b-8 a8", v2="o3 a1", v4="o2 d1")
    bar(["A", "A7", "Dm", "Dm"], v0=tp(S[1], 5), v1="o4 a4 g4 a8 g8 f4",
        v2="o3 a2 a2", v4="o1 a2 o2 d2")
    # Эпизод 2: голова темы на ступенях вверх
    bar("Dm", v0="o5 d8 c8 d8 e8 f2", v1="o4 a2 a2", v2="o3 f1", v4="o2 d1")
    bar(["E", "E7"], v0="o5 e8 d8 e8 f8 g#2", v1="o4 b2 b2", v2="o3 g#2 o4 d2", v4="o1 e1")
    bar(["Am", "F"], v0="o5 a8 g8 a8 b8 > c2", v1="o5 e2 f2", v2="o4 c2 o3 a2", v4="o1 a2 f2")
    bar(["Dm", "E"], v0="o5 f8 e8 d8 e8 g#2", v1="o4 a2 b2", v2="o4 d2 e2", v4="o2 d2 o1 e2")
    # Вторая экспозиция: тема в теноре, потом в сопрано
    bar("Am", v0="o5 c2 e4 f8 e8", v1="o4 a1", v2="o3 " + S[0], v4="o1 a1")
    bar(["E", "E7", "Am", "Am"], v0="o5 e4 d4 e8 d8 c4", v1="o4 b2 a2", v2="o3 " + S[1],
        v4="o1 e2 a2")
    bar("Am", v0="@tutti o5 " + S[0], v1="o4 c2 e4 f8 e8", v2="o3 a1", v4="o1 a1")
    bar(["E", "E7", "Am", "Am"], v0="o5 " + S[1], v1="o4 e4 d4 e8 d8 c4", v2="o3 g#2 a2",
        v4="o1 e2 a2")
    # Органный пункт на ми
    bar(["E", "Am/E"], v0="o4 l16 b > e g# e < b > e g# e c e a e c e a e",
        v1="o4 g#2 a2", v2="o4 e2 e2", v4="o1 e1")
    bar(["Dm/E", "E"], v0="o5 l16 d f a f d f a f < b > e g# e < b > e g# e",
        v1="o4 a2 g#2", v2="o4 f2 e2", v4="o1 e1")
    bar("E7", v0="o5 l16 d e g# b > d < b g# e d e g# b > d < b g# e",
        v1="o4 g#1", v2="o4 d1", v4="o1 e1")
    bar("E7b9", v0="o5 l16 f g# b > d f d < b g# f d < b g# f d < b g#",
        v1="o4 f1", v2="o4 d1", v4="o1 e1")
    # Тема в педали, полный орган
    bar("Am", v0="o5 e1", v1="o5 c1", v2="o4 a1", v3="@princ o4 e1",
        v4="@pedal o1 " + S[0])
    bar(["E", "E7", "Am", "Am"], v0="o5 e4 d4 c2", v1="o4 b4 b4 a2", v2="o4 g#4 g#4 e2",
        v3="o4 e2 e2", v4="o1 " + S[1])

    # ---- Кода, такты 38-43 ----
    C0 = len(bars)
    bar("Am", v0="o5 a8 g16 a16 b8 > c16 < b16 a2", v1="o4 a8 g16 a16 b8 > c16 < b16 a2",
        v2="o4 e1", v3="o4 c1", v4="o1 a1")
    bar(["Dm", "E7"], v0="o5 a2 g#2", v1="o5 f2 e2", v2="o5 d2 d2", v3="o4 a2 b2", v4="o1 f2 e2")
    bar(["Am", "F"], v0="o5 a2 a2", v1="o5 e2 f2", v2="o5 c2 c2", v3="o4 a2 a2", v4="o1 a2 f2")
    bar(["Dm6", "E7"], v0="o5 b2 g#2", v1="o5 f2 e2", v2="o5 d2 d2", v3="o4 a2 b2", v4="o1 d2 e2")
    bar("A", v0="@trem o5 a1", v1="@trem o5 e1", v2="@trem o5 c#1", v3="@trem o4 a1",
        v4="o1 a1")
    bar("A", v0="o5 a2 r2", v1="o5 e2 r2", v2="o5 c#2 r2", v3="o4 a2 r2", v4="o1 a2 r2")

    # Голоса: по тактам, пустой такт - пауза
    start = {0: "@tutti", 1: "@tutti", 2: "@princ", 3: "@princ", 4: "@pedal"}
    for ch in range(5):
        key = "v%d" % ch
        text = [start[ch]]
        for h, v in bars:
            text.append(v.get(key, "r1"))
            text.append("|")
        s.voice(ch, " ".join(text))

    # Темп: токката свободно, фуга живее, кода шире
    br = s.bar_rows
    s.set_tempo(0, 66)
    s.ritard(br * 8, br * 10 - 1, 66, 48)
    s.set_tempo(br * F0, 100)
    s.set_tempo(br * C0, 72)
    s.ritard(br * (C0 + 2), br * (C0 + 6) - 1, 72, 40, every=4)
    # Подписи
    s.text(0, "ЛЕЙТМОТИВ А-Г-А-Т: ЛЯ-СОЛЬ-ЛЯ-СИ  ПОЛНЫЙ ОРГАН: НЧ+СЧ+ВЧ, D3")
    s.text(br * 2, "ТОККАТА: ПЯТЬ ГОЛОСОВ, ПЕДАЛЬ НА ПОЛОСЕ НЧ")
    s.text(br * 6, "ТРЕЛЬ 32-МИ: СЧЕТЧИК ВИ53 МЕНЯЕТ ПЕРИОД КАЖДЫЕ 3 ТИКА")
    s.text(br * F0, "ФУГА. ТЕМА В АЛЬТЕ: ПРИНЦИПАЛ НЧ+СЧ")
    s.text(br * (F0 + 2), "ОТВЕТ В СОПРАНО: ФЛЕЙТА СЧ+ВЧ, ПРОТИВОСЛОЖЕНИЕ В АЛЬТЕ")
    s.text(br * (F0 + 6), "ТЕМА В ПЕДАЛИ: НЧ С D3")
    s.text(br * (F0 + 8), "ЭПИЗОД: СЕКВЕНЦИЯ ПО КВИНТАМ")
    s.text(br * (F0 + 22), "ОРГАННЫЙ ПУНКТ НА МИ")
    s.text(br * (F0 + 26), "ТЕМА В ПЕДАЛИ, ПОЛНЫЙ ОРГАН")
    s.text(br * (C0 + 4), "ПИКАРДИЙСКАЯ ТЕРЦИЯ, ТРЕМУЛЯНТ - ВИБРАТО")

    # Гармония для проверки
    harm = []
    for i, (h, v) in enumerate(bars):
        hs = [h] if isinstance(h, str) else h
        for k, sym in enumerate(hs):
            harm.append((i * br + k * br // len(hs), sym))
    s.harmony = harm
    return s


if __name__ == "__main__":
    s = song()
    s.build()
    print(s.stats)
    # сильные доли: каждые полдоли (восьмые)
    n = check_harmony(s, s.harmony, every=s.rpb, label="I")
    print("вне аккорда на долях:", n)
