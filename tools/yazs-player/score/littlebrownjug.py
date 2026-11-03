# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
"""
Little Brown Jug - мелодия Дж. Уиннера (1869, общественное достояние), своя
аранжировка в духе биг-бэндов 40-х. Си-бемоль мажор, свинг 160, около 2:40.

Мелодия - по скрипичной записи из Traditional Tune Archive (ре мажор, 2/4, AABB
по 8 тактов): здесь на большую терцию ниже, такт 2/4 - такт 4/4 (восьмые -
четверти, шестнадцатые - свинговые восьмые). Аранжировка своя (не Финегана):
- вступление: ритм-группа, тромбоны «уа» на 2 и 4;
- корус 1: тема - кларнет сверху, альт на терцию-сексту ниже, тенор октавой ниже;
- корус 2: A A - соло трубы с сурдиной на подкладке саксофонов, B B - тромбоны
  с планжером, рифф альта;
- корус 3: шаут - трубы с темой, тромбоны ударами;
- концовка: «ля-ля-ля-соль-ля», «до - си-бемоль», аккорд с секстой, дробь, удар.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from compose import Song                                     # noqa: E402
from instruments import HF, MF, LF, LOUD, HIT, HOLD, KIT6, KIT7   # noqa: E402
from common import chord, name, walking, harmonize, shuffle, tp, check_harmony   # noqa: E402

RPB = 3
BB_MAJ = [0, 2, 3, 5, 7, 9, 10]
HA = ["Bb", "Eb", "F7", "Bb", "Bb", "Eb", "F7", ["F7", "Bb"]]
HB = ["Bb", "Eb", "F7", "Bb", "Bb", ["Eb", "C7"], "F7", ["F7", "Bb"]]
CHORUS = HA + HA + HB + HB
INTRO = ["Bb", "G7", ["Cm7", "F7"], "F7"]
END = ["F7", ["F7", "Bb"], "Bb", "Bb"]

A = ["o4 d4 f4 f2", "o4 e-4 g4 g2", "o4 a8 a8 a4 g4 a4", "o4 b-4 o5 c4 d2",
     "o4 d4 f4 f2", "o4 e-4 g4 g2", "o4 a8 a8 a4 g4 a4", "o5 c4 o4 b-4 b-2"]
B = ["o5 d4 o4 b-4 f4 d4", "o4 e-4 g4 g2", "o4 a8 a8 a4 g4 a4", "o4 b-4 o5 c4 d4 o4 b-8 o5 c8",
     "o5 d4 o4 b-4 f4 d4", "o4 e-4 g4 g2", "o4 a8 a8 a4 g4 a4", "o5 c4 o4 b-4 b-2"]
TO_A = "o5 c4 o4 b-4 b-4 f8 e-8"        # последний такт с затактом к A
TO_B = "o5 c4 o4 b-4 b-4 b-8 o5 c8"     # ... к B
SOLO = [
    "r8 o4 f8 b-8 o5 d8 f4 d8 c8", "o5 e-4 c8 o4 b-8 g4 e-4",
    "o4 a8 o5 c8 e-8 f8 a4 g8 f8", "o4 d8 f8 b-2 r4",
    "r8 o5 d8 _c#8 c8 o4 b-8 g#8 g8 f8", "o4 g4 b-8 o5 c8 e-4 r4",
    "o5 c8 o4 a8 f8 a8 o5 c8 e-8 d8 c8", "o4 b-4 r4 r2",
    "o5 f4 d8 f8 r8 d8 f4", "o5 e-8 d8 c8 o4 b-8 g8 b-8 o5 c8 e-8",
    "o5 d8 c8 o4 a8 f8 e-8 f8 a8 o5 c8", "o5 d4 o4 b-4 f4 d4",
    "o4 f12 g12 a12 b-8 o5 d8 f4 r4", "o5 g4 f8 e-8 d8 e-8 c4",
    "o5 c8 e-8 d8 c8 o4 a8 o5 c8 o4 f8 a8", "o4 b-2 r4 b-8 o5 c8",
]


def n(x, ln):
    return "o%d %s%s" % (x // 12, name(x), ln)


def bars(lst):
    return " | ".join(lst) + " |"


def near(pc, ref):
    return min((x for x in range(ref - 6, ref + 7) if x % 12 == pc), key=lambda x: abs(x - ref))


def guide(sym):
    """Терция и септима (у мажорного трезвучия - секста)"""
    root, q, _ = chord(sym)
    return (root + q[1]) % 12, (root + (q[3] if len(q) > 3 else 9)) % 12


def per_bar(harm, one, two):
    """one(аккорд) - такт с одним аккордом, two(аккорд) - половина такта"""
    return " ".join((one(h) if isinstance(h, str) else " ".join(two(x) for x in h)) + " |"
                    for h in harm)


def song():
    s = Song("LITTLE BROWN JUG", "ДЖ. УИННЕР (1869), СВИНГ", bpm=160,
             rows_per_beat=RPB, beats_per_bar=4, frame_bars=2)
    I = s.instrument
    scoop = [-8, 2, 2, 2, 2, 0]
    I("clar", "КЛАРНЕТ", [HOLD | HIT | LOUD | MF], vib=(24, 4, 2), meter=(14, 12, 2))
    I("alto", "АЛЬТ", [HOLD | HIT | LOUD | LF | MF], pitch=scoop, vib=(26, 4, 3), meter=(13, 11, 2))
    I("tenor", "ТЕНОР", [HOLD | HIT | LOUD | LF], pitch=scoop, vib=(26, 4, 3), meter=(13, 11, 2))
    I("tptm", "ТРУБА", [HOLD | HIT | LOUD | MF | HF], pitch=[-6, 2, 2, 2, 0], vib=(30, 5, 2),
      meter=(14, 12, 2))
    I("tbn", "ТРОМБОНЫ", [HOLD | HIT | LOUD | LF] * 5 + [HOLD | HIT | LOUD | LF | MF],
      pitch=[-6, 2, 2, 2, 0], meter=(13, 11, 2))
    I("tpts", "ТРУБЫ", [HOLD | HIT | LOUD | LF | MF | HF] * 2 + [HOLD | HIT | LOUD | LF | MF],
      pitch=[-6, 2, 2, 2, 0], vib=(30, 5, 2), meter=(15, 13, 2))
    I("pad", "САКСОФ.", [HOLD | LF | MF], vib=(30, 4, 2), meter=(9, 8, 1))
    I("riff", "АЛЬТ", [HOLD | HIT | LF | MF], meter=(11, 8, 3))
    I("piano", "ФОРТЕП.", [HIT | LF | MF | HF, HIT | LF | MF], meter=(10, 0, 10))
    I("bass", "КОНТРАБ.", [HIT | LOUD | LF | MF, HIT | LOUD | LF], meter=(14, 0, 6))

    br = s.bar_rows
    SECTIONS = [("intro", INTRO), ("ch1", CHORUS), ("ch2", CHORUS), ("shout", CHORUS), ("end", END)]
    at, hm, bar = {}, [], 0
    for nm, h in SECTIONS:
        at[nm] = bar
        for sym in h:
            if isinstance(sym, list):
                hm += [(bar * br, sym[0]), (bar * br + br // 2, sym[1])]
            else:
                hm.append((bar * br, sym))
            bar += 1
    total = bar
    R = lambda nm: at[nm] * br
    ch = {c: [] for c in range(7)}

    def add(c, text, drums=False):
        ch[c].append(shuffle(text, drums, RPB))

    # Тема целиком: A (с затактом к A), A (с затактом к B), B (к B), B
    lead = bars(A[:7] + [TO_A] + A[:7] + [TO_B] + B[:7] + [TO_B] + B)
    lead_sw = shuffle(lead, False, RPB)
    comp = per_bar(CHORUS, lambda x: "r4 %s r4 %s" % (n(near(guide(x)[1], 52), "4"), n(near(guide(x)[0], 52), "4")),
                   lambda x: "r4 %s" % n(near(guide(x)[1], 52), "4"))
    ride = "z4 z8 z8 z4 z8 z8 | "
    soft = ("K4 S4 K4 S4 | " * 7 + "K4 S4 S8 S8 S8 S8 | ") * 4

    # --- вступление ---
    add(0, "r1 | r1 | r1 | r2. @clar f8 e-8 |")
    add(1, "r1 | r1 | r1 | r1 |")
    add(2, "r1 | r1 | r1 | r1 |")
    add(3, "@tbn " + per_bar(INTRO, lambda x: "r4 %s r4 %s" % (n(near(guide(x)[0], 50), "4"),
                                                               n(near(guide(x)[1], 50), "4")),
                             lambda x: "r4 %s" % n(near(guide(x)[0], 50), "4")))
    add(4, "@bass " + walking(INTRO, low=22))
    add(5, "K4 S4 K4 S4 | " * 3 + "K4 S4 S8 S8 S8 S8 |", drums=True)
    add(6, ride * 4, drums=True)

    # --- корус 1: тема ---
    add(0, "@clar " + lead)
    add(1, "@alto " + harmonize(s, lead_sw, hm, BB_MAJ, R("ch1"), lo=3, hi=9))
    add(2, "@tenor " + tp(lead_sw, -12))
    add(3, "@piano " + comp)
    add(4, "@bass " + walking(CHORUS, low=22))
    add(5, soft, drums=True)
    add(6, ride * 32, drums=True)

    # --- корус 2: соло трубы (A A), тромбоны (B B) ---
    pads = lambda w, ref: per_bar(CHORUS[:16], lambda x: n(near(guide(x)[w], ref), "1"),
                                  lambda x: n(near(guide(x)[w], ref), "2"))
    tb = bars(B[:7] + [TO_B] + B)
    tb_sw = tp(shuffle(tb, False, RPB), -12)
    riff = per_bar(CHORUS[16:], lambda x: "r8 %s r8 %s %s r4" % (
        n(near(guide(x)[0], 55), "8"), n(near(guide(x)[0], 55), "8"), n(near(guide(x)[1], 57), "4")),
        lambda x: "r8 %s r4" % n(near(guide(x)[0], 55), "8"))
    add(0, "@tptm {porta 8} " + bars(SOLO[:15] + ["o4 b-2 r2"]) + " @riff {porta 0} " + riff)
    # Подкладка 15 тактов, в 16-м - затакт тромбонов к B
    pad_hi = per_bar(CHORUS[:15], lambda x: n(near(guide(x)[1], 55), "1"),
                     lambda x: n(near(guide(x)[1], 55), "2"))
    add(1, "@pad " + pad_hi + " o4 d2 r4 @tbn o3 b-8 o4 c8 |")
    ch[1].append(tb_sw)
    add(2, "@pad " + pads(0, 50) + " @tbn " + harmonize(s, tb_sw, hm, BB_MAJ, R("ch2") + 16 * br, lo=3, hi=9))
    add(3, "@piano " + comp)
    add(4, "@bass " + walking(CHORUS, low=22))
    add(5, soft, drums=True)
    add(6, ride * 32, drums=True)

    # --- корус 3: шаут ---
    add(0, "@tpts " + lead)
    add(1, "@alto " + harmonize(s, lead_sw, hm, BB_MAJ, R("shout"), lo=3, hi=9))
    add(2, "@tenor " + tp(lead_sw, -12))
    add(3, "@tbn " + per_bar(CHORUS, lambda x: "%s r4 r8 %s r4" % (n(near(guide(x)[1], 50), "4"),
                                                                    n(near(guide(x)[0], 50), "8")),
                             lambda x: "%s r4" % n(near(guide(x)[1], 50), "4")))
    add(4, "@bass " + walking(CHORUS, low=22))
    add(5, ("k4 s4 k4 s4 | " * 7 + "k4 s4 t8 u8 v8 w8 | ") * 4, drums=True)
    add(6, ("o4 z8 z8 z4 z8 z8 | " + ride * 3) * 8, drums=True)

    # --- концовка ---
    add(0, "o4 a8 a8 a4 g4 a4 | o5 c4 o4 b-4 r2 | o4 g1 | o4 g4 r4 r2 |")
    add(1, "o4 f8 f8 f4 e-4 f4 | o4 a4 f4 r2 | o4 d1 | o4 d4 r4 r2 |")
    add(2, "o3 a8 a8 a4 g4 a4 | o4 e-4 d4 r2 | o3 b-1 | o3 b-4 r4 r2 |")
    add(3, "o3 e-4 r4 e-4 r4 | o3 f4 r4 r2 | o3 f1 | o3 f4 r4 r2 |")
    add(4, "o2 f4 c4 f4 e4 | o2 e-4 d4 r2 | o1 b-1 | o1 b-4 r4 r2 |")
    add(5, "k4 s4 k4 s4 | k4 s4 r2 | " + "S12 " * 12 + "| k4 r4 r2 |", drums=True)
    add(6, "o4 z8 z8 z4 z8 z8 | H4 r4 r2 | H1 | H4 r4 r2 |", drums=True)

    for c in range(5):
        end = s.voice(c, " ".join(ch[c]), octave=4, length=4)
        assert end == total * br, (c, end / br, total)
    for c in (5, 6):
        end = s.drums(c, " ".join(ch[c]), KIT6 if c == 5 else KIT7, length=4)
        assert end == total * br, (c, end / br, total)

    s.ritard(R("end") + br, R("end") + 3 * br - 1, 160, 110, every=RPB)
    T = lambda nm, txt, off=0: s.text(R(nm) + off * br + 1, txt)
    T("intro", "СВИНГ 160: ВОСЬМЫЕ 2:1, ШАГАЮЩИЙ БАС, РАЙД ДИНЬ-ДИНЬ-А-ДИНЬ")
    T("ch1", "ТЕМА: КЛАРНЕТ НА СЧ, АЛЬТ НИЖЕ, ТЕНОР ОКТАВОЙ НИЖЕ")
    T("ch2", "СОЛО ТРУБЫ С СУРДИНОЙ: СЧ+ВЧ, ПОДЪЕЗД К НОТЕ")
    T("ch2", "ТРОМБОНЫ С ПЛАНЖЕРОМ: УА - НЧ, ПОТОМ НЧ+СЧ С УДАРОМ", 16)
    T("shout", "ШАУТ-КОРУС: ТРУБЫ, САКСОФОНЫ И ТРОМБОНЫ - ВЕСЬ ОРКЕСТР")
    T("end", "КОНЦОВКА: АККОРД СИ-БЕМОЛЬ С СЕКСТОЙ И ДРОБЬ")
    s.harmony = hm
    s.sections = at
    return s


if __name__ == "__main__":
    s = song()
    s.build()
    print(s.stats, s.sections)
    for t in s.texts:
        assert len(t) <= 64, len(t)
    print("вне аккорда:", check_harmony(s, s.harmony, every=RPB, label="LBJ", allow=(7,)))
