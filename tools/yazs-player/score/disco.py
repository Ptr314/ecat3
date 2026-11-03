# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
"""
ЯЗС-диско: европоп и диско 70-х, ре мажор -> ми мажор, 122, около 3 минут.

Форма: вступление (колокольчики) - куплет - перед припевом - припев - проигрыш -
куплет - перед припевом - припев - брейк (бочка, хлопки, струнные) - соло
синтезатора - два припева тоном выше - концовка.

Приметы жанра:
- дуэт: второй голос на терцию-сексту ниже (harmonize);
- бас октавами восьмыми; в припеве бас идет вниз по ступеням;
- бочка (ударный 6) на каждую долю; ударный 7 - хэт на слабые доли, хлопок на 2 и 4;
- фортепьяно на слабые доли, высокие струнные на D5;
- модуляция на тон вверх в последних припевах.
В припеве - лейтмотив симфонии: ля-соль-ля-си (А-Г-А-Т).

Инструменты:
- голос 1: подъезд к ноте на четверть полутона, частое узкое вибрато;
- голос 2: тот же, на НЧ - чуть темнее и тише;
- колокольчики: щипок на СЧ+ВЧ, первый тик на октаву выше;
- синтезатор: два канала в октаву с расстройкой на 1/8 полутона, портаменто;
- бас: щипок, первые тики ярче.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from compose import Song                                     # noqa: E402
from instruments import HF, MF, LF, LOUD, HIT, HOLD, KIT6, KIT7   # noqa: E402
from common import chord, name, voicing, harmonize, tp, check_harmony   # noqa: E402

D_MAJ = [1, 2, 4, 6, 7, 9, 11]
E_MAJ = [1, 3, 4, 6, 8, 9, 11]

INTRO_H = ["D", "Bm", "G", "A"]
VERSE_H = ["D", "D", "G", "D", "Bm", "E7", "G", "A"]
PRE_H = ["Em", "F#m", "G", "A7"]
CHORUS_H = ["D", "A/C#", "Bm", "D/A", "G", "D/F#", "Em7", "A7"]
BREAK_H = ["Bm", "G", "D", "A", "Bm", "G", "Em", "A7"]
SOLO_H = ["D", "D", "G", "D", "Bm", "E7", "G", "B7"]
CHORUS_E = ["E", "B/D#", "C#m", "E/B", "A", "E/G#", "F#m7", "B7"]
TAG_H = ["E", "C#m", "A", "E"]

HOOK = [
    "o5 f#8 a8 f#16 e16 d8 e8 f#4 r8",
    "o5 d8 f#8 d16 c#16 o4 b8 o5 c#8 d4 r8",
    "o4 b8 o5 d8 g8 f#16 e16 d8 e8 d8 o4 b8",
    "o5 c#8 e8 a8 g16 f#16 e4 r4",
]
VERSE = [
    "r8 o4 a8 a8 f#8 a8 b8 a8 f#8", "o4 e8 d8 e8 f#8 ^4 r4",
    "r8 o4 b8 b8 g8 b8 o5 d8 c#8 o4 b8", "o4 a4 f#8 e8 d4 r4",
    "r8 o4 f#8 f#8 d8 f#8 a8 g8 f#8", "o4 g#8 f#8 e8 d8 e4 r4",
    "r8 o4 g8 a8 b8 o5 d8 c#8 o4 b8 a8", "o4 a4. g16 f#16 e4 r4",
]
PRE = [
    "o4 g8 g8 g8 a8 b4 a8 g8", "o4 a8 a8 a8 b8 o5 c#4 o4 b8 a8",
    "o4 b8 b8 b8 o5 c#8 d4 c#8 o4 b8", "o5 c#4 e4 c#4 o4 a4",
]
CHORUS = [
    "o5 d8. d16 c#8 d8 ^4 o4 a4", "o4 b8. a16 b8 o5 c#8 ^4 o4 a4",
    "o4 b8. b16 a8 b8 ^4 f#4", "o4 a8. a16 g8 f#8 ^2",
    "o4 a8 g8 a8 b8 ^4 o5 d4", "o5 c#8 d8 c#8 o4 a8 ^4 f#4",
    "o4 g8 f#8 e8 d8 e4 g4", "o4 a4 g4 f#4 e4",
]
STRINGS_CH = [66, 64, 62, 62, 59, 57, 59, 61]          # над голосами: фа-диез2 вниз
BREAK_STR = ["o4 f#2. e4", "o4 d2. o3 b4", "o4 a2. f#4", "o4 e1",
             "o4 b2. a4", "o4 g2. f#4", "o4 e2 g4 b4", "o5 c#1"]
BREAK_V1 = ["o4 f#1", "o4 d1", "o4 e1", "o4 e1"]
BREAK_V2 = ["o3 b1", "o3 b1", "o3 b1", "o4 c#1"]
SOLO = [
    "o4 a8 o5 d8 f#8 a8 ^4 f#8 e8", "o5 d8 e16 f#16 e8 d8 o4 a2",
    "o4 g8 b8 o5 d8 g8 ^4 f#8 e8", "o5 f#8 e16 d16 e8 f#8 d2",
    "o4 b8 o5 d8 f#8 b8 ^4 a8 f#8", "o5 g#8 f#16 e16 d8 o4 b8 o5 e2",
    "o5 d16 e16 d16 o4 b16 g8 b8 o5 d8 e8 g8 f#8", "o5 f#4 d#4 o4 b4 a4",
]
AHHA = {1: "r2. o4 a4", 3: "r2. o4 f#4", 5: "r2. o4 g#4", 7: "r2. o4 c#4"}


def n(x, ln):
    return "o%d %s%s" % (x // 12, name(x), ln)


def bars(lst):
    return " | ".join(lst) + " |"


def rest(k):
    return "r1 | " * k


def bass_line(harm, end=None):
    """Октавы восьмыми; низ - в окне си1..ля-диез2"""
    out = []
    for sym in harm:
        pc = chord(sym)[2]
        lo = next(x for x in range(23, 35) if x % 12 == pc)
        out.append(" ".join(n(lo, "8") + " " + n(lo + 12, "8") for _ in range(4)) + " |")
    return " ".join(out)


def stab_pair(sym):
    """Две ноты фортепьяно: у септаккорда - терция и септима, иначе - плотно под ре1"""
    root, q, _ = chord(sym.split("/")[0])
    if len(q) >= 4:
        third = (root + q[1]) % 12
        sev = (root + q[3]) % 12
        a = next(x for x in range(49, 37, -1) if x % 12 == third)
        b = next(x for x in range(52, 40, -1) if x % 12 == sev)
        return tuple(sorted((a, b)))
    return tuple(voicing(sym.split("/")[0], 50, 2))


def stabs(harm, which):
    return " ".join(" ".join("r8 " + n(stab_pair(s)[which], "8") for _ in range(4)) + " |"
                    for s in harm)


def pads(harm, which, top=50):
    return " ".join(n(voicing(s.split("/")[0], top, 2)[which], "1") + " |" for s in harm)


def song():
    s = Song("ЯЗС-ДИСКО", "ЕВРОПОП 70-Х ДЛЯ ЯЗС 5/2", bpm=122, rows_per_beat=4,
             beats_per_bar=4, frame_bars=2)
    I = s.instrument
    I("lead", "ГОЛОС 1", [HOLD | HIT | LOUD | LF | MF], pitch=[-4, 2, 2, 0],
      vib=(18, 5, 2), meter=(14, 12, 2))
    I("harm", "ГОЛОС 2", [HOLD | HIT | LOUD | LF], pitch=[-4, 2, 2, 0],
      vib=(18, 5, 2), meter=(13, 11, 2))
    I("ahha", "ХОР", [HOLD | LF] * 6 + [HOLD | HIT | LF | MF], meter=(11, 9, 2))
    I("glock", "КОЛОКОЛ.", [HIT | LOUD | MF | HF], arp=[12, 0], meter=(14, 0, 8))
    I("piano", "ФОРТЕП.", [HIT | LOUD | LF | MF | HF, HIT | LOUD | LF | MF], meter=(13, 0, 8))
    I("stab", "ФОРТЕП.", [HIT | LF | MF | HF, HIT | LF | MF], meter=(10, 0, 12))
    I("str", "СТРУННЫЕ", [HOLD | LF | MF], meter=(9, 8, 1))
    I("str_l", "СТРУННЫЕ", [HOLD | LOUD | LF | MF], vib=(24, 4, 3), meter=(12, 11, 1))
    I("syn", "СИНТЕЗ.", [HOLD | HIT | LOUD | MF | HF], vib=(20, 5, 2), meter=(15, 13, 2))
    I("syn_lo", "СИНТЕЗ.", [HOLD | HIT | LF | MF], vib=(20, 5, 2), detune=2, meter=(11, 9, 2))
    I("bass", "БАС", [HIT | LOUD | LF | MF | HF, HIT | LOUD | LF | MF, HIT | LOUD | LF],
      meter=(15, 0, 6))

    SECTIONS = [("intro", INTRO_H), ("v1", VERSE_H), ("pre1", PRE_H), ("ch1", CHORUS_H),
                ("hook", INTRO_H), ("v2", VERSE_H), ("pre2", PRE_H), ("ch2", CHORUS_H),
                ("break", BREAK_H), ("solo", SOLO_H), ("che1", CHORUS_E), ("che2", CHORUS_E),
                ("tag", TAG_H)]
    br = s.bar_rows
    at, hm = {}, []
    bar = 0
    for nm, h in SECTIONS:
        at[nm] = bar
        for sym in h:
            hm.append((bar * br, sym))
            bar += 1
    total = bar
    R = lambda nm: at[nm] * br
    ch = {c: [] for c in range(7)}

    def add(c, text):
        ch[c].append(text)

    def drums(k6, k7):
        add(5, k6)
        add(6, k7)

    four = "k4 k4 k4 k4 | "
    four_v = "K4 K4 K4 K4 | "      # в куплете бочка короче и тише
    fill = "k4 k4 t16 t16 u16 u16 v16 v16 w16 w16 | "
    hat_v = "h8 h8 c8 h8 h8 h8 c8 h8 | "
    hat_c = "h8 H8 c8 H8 h8 H8 c8 H8 | "
    hat_f = "h8 h8 c8 h8 c16 c16 c16 c16 c4 | "

    def verse(lead_extra):
        add(0, "@lead " + bars(VERSE))
        add(1, lead_extra)
        add(2, "@stab " + stabs(VERSE_H, 1))
        add(3, "@stab " + stabs(VERSE_H, 0))
        add(4, "@bass " + bass_line(VERSE_H))
        drums(four_v * 8, hat_v * 8)

    def pre(nm):
        add(0, "@lead " + bars(PRE))
        add(1, "@harm " + harmonize(s, bars(PRE), hm, D_MAJ, R(nm)))
        add(2, "@str " + pads(PRE_H, 1))
        add(3, "@str " + pads(PRE_H, 0))
        add(4, "@bass " + bass_line(PRE_H))
        drums(four * 3 + fill, hat_v * 3 + hat_f)

    def chorus(nm, lead, key, harm, top):
        add(0, "@lead " + lead)
        add(1, "@harm " + harmonize(s, lead, hm, key, R(nm)))
        add(2, "@stab " + stabs(harm, 1))
        add(3, top)
        add(4, "@bass " + bass_line(harm))
        drums(four * 8, hat_c * 8)

    def hook(nm, harm, semis=0):
        txt = bars(HOOK) if not semis else tp(bars(HOOK), semis)
        add(0, "@glock " + txt)
        add(1, "@piano " + tp(txt, -12))
        add(2, "@stab " + stabs(harm, 1))
        add(3, "@stab " + stabs(harm, 0))
        add(4, "@bass " + bass_line(harm))

    # --- вступление ---
    hook("intro", INTRO_H)
    drums("r1 | r1 | " + four * 2, "r8 h8 r8 h8 r8 h8 r8 h8 | " * 2 + hat_v * 2)
    # --- куплет, перед припевом, припев ---
    verse(rest(8))
    pre("pre1")
    strings = "@str " + " ".join(n(x, "1") + " |" for x in STRINGS_CH)
    chorus("ch1", bars(CHORUS), D_MAJ, CHORUS_H, strings)
    # --- проигрыш ---
    hook("hook", INTRO_H)
    drums(four * 3 + fill, hat_c * 4)
    # --- второй куплет с «а-ха» хора ---
    verse(" ".join(("@ahha " + AHHA[i] + " |") if i in AHHA else "r1 |" for i in range(8)))
    pre("pre2")
    chorus("ch2", bars(CHORUS), D_MAJ, CHORUS_H, strings)
    # --- брейк: бочка и хлопки, струнные ведут, потом голоса ---
    add(0, rest(4) + "@lead " + bars(BREAK_V1))
    add(1, rest(4) + "@harm " + bars(BREAK_V2))
    add(2, rest(4) + "@stab " + stabs(BREAK_H[4:], 1))
    add(3, "@str_l " + bars(BREAK_STR))
    add(4, "@bass " + bass_line(BREAK_H))
    drums(four * 7 + fill, "r4 c4 r4 c4 | " * 4 + hat_v * 3 + "h8 h8 c8 h8 r2 | ")
    # --- соло синтезатора ---
    add(0, "@syn {porta 12} " + bars(SOLO))
    add(1, "@syn_lo {porta 12} " + tp(bars(SOLO), -12))
    add(2, "@stab " + stabs(SOLO_H, 1))
    add(3, "@stab " + stabs(SOLO_H, 0))
    add(4, "@bass " + bass_line(SOLO_H))
    drums(four * 7 + fill, hat_c * 8)
    # --- два припева тоном выше; во втором колокольчики удваивают голос ---
    lead_e = tp(bars(CHORUS), 2)
    lead_e_end = tp(bars(CHORUS[:7] + ["o4 a4 g4 f#4 e4"]), 2)
    strings_e = "@str_l {porta 0} " + " ".join(n(x + 2, "1") + " |" for x in STRINGS_CH)
    chorus("che1", lead_e, E_MAJ, CHORUS_E, strings_e)
    chorus("che2", lead_e_end, E_MAJ, CHORUS_E, "@glock " + tp(lead_e_end, 12))
    # --- концовка: проигрыш в ми мажоре и аккорд ---
    tag = tp(bars(HOOK[:3]), 2)
    add(0, "@glock " + tag + " o5 e16 g#16 b16 o6 e16 ^4 ^2 |")
    add(1, "@piano " + tp(tag, -12) + " o4 e4 r4 r2 |")
    add(2, "@stab " + stabs(TAG_H[:3], 1) + " @str_l o4 g#1 |")
    add(3, "@stab " + stabs(TAG_H[:3], 0) + " @str_l o4 b1 |")
    add(4, "@bass " + bass_line(TAG_H[:3]) + " o2 e4 r4 r2 |")
    drums(four * 3 + "k4 r4 r2 |", hat_c * 3 + "H4 r4 r2 |")

    for c in range(5):
        end = s.voice(c, " ".join(ch[c]), octave=4, length=4)
        assert end == total * br, (c, end / br, total)
    for c in (5, 6):
        end = s.drums(c, " ".join(ch[c]), KIT6 if c == 5 else KIT7, length=4)
        assert end == total * br, (c, end / br, total)

    T = lambda row, txt: s.text(row + 2, txt)   # подпись - не на сильную долю
    T(0, "ДИСКО 122: БОЧКА НА КАЖДУЮ ДОЛЮ, ХЭТ НА СЛАБУЮ, БАС ОКТАВАМИ")
    T(R("v1"), "КУПЛЕТ: ГОЛОС С ПОДЪЕЗДОМ, ФОРТЕПЬЯНО НА СЛАБЫЕ ДОЛИ")
    T(R("pre1"), "ПЕРЕД ПРИПЕВОМ: ДВА ГОЛОСА, СТРУННЫЕ НА D5")
    T(R("ch1"), "ПРИПЕВ: ДУЭТ, БАС ВНИЗ ПО СТУПЕНЯМ, СТРУННЫЕ ВЫСОКО")
    T(R("ch1") + 4 * br, "ЛЯ-СОЛЬ-ЛЯ-СИ - ЛЕЙТМОТИВ А-Г-А-Т")
    T(R("hook"), "КОЛОКОЛЬЧИКИ: ЩИПОК СЧ+ВЧ, ПЕРВЫЙ ТИК НА ОКТАВУ ВЫШЕ")
    T(R("v2"), "ХОР: А-ХА - ПОЛОСЫ НЧ -> НЧ+СЧ С УДАРОМ D4")
    T(R("break"), "БРЕЙК: БОЧКА И ХЛОПКИ, МЕЛОДИЯ У СТРУННЫХ")
    T(R("solo"), "СИНТЕЗАТОР: ДВА КАНАЛА В ОКТАВУ С РАССТРОЙКОЙ, ПОРТАМЕНТО")
    T(R("che1"), "ПРИПЕВ ТОНОМ ВЫШЕ: МИ МАЖОР")
    T(R("che2"), "КОЛОКОЛЬЧИКИ УДВАИВАЮТ ГОЛОС НА ОКТАВУ ВЫШЕ")
    T(R("tag"), "КОНЦОВКА: ПРОИГРЫШ В МИ МАЖОРЕ")
    s.harmony = hm
    s.sections = at
    return s


if __name__ == "__main__":
    s = song()
    s.build()
    print(s.stats, s.sections)
    for t in s.texts:
        assert len(t) <= 64
    print("вне аккорда:", check_harmony(s, s.harmony, every=s.rpb, label="диско"))
