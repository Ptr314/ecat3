# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
"""
ЯЗС-буги: рок-н-ролл и буги-вуги в духе рокабилли 50-х, ля мажор, около 3 минут.

Части разного темпа:
- вступление, 150: фортепьяно, левая рука - буги;
- тема, 176, два квадрата блюза: «голос», ритм-гитара квинта-секста, бас буги,
  во втором квадрате - подпевки «ду-воп»;
- соло фортепьяно, 176: триоли, глиссандо и тремоло октавами;
- баллада 12/8, 66: триоли фортепьяно, хор «у-у», голос;
- соло саксофона и «четверки» (фортепьяно, гитара, саксофон), 132, свинг;
- соло контрабаса со слэпом, разгон 132 -> 192;
- соло гитары двойными нотами, 192; стоп-тайм; тема; концовка с замедлением.

Шаффл - сетка триолей: 3 строки на долю, восьмая шаффла - 2 + 1 строки
(shuffle() переводит «8» в «6» и «12» по месту в доле). Хук темы - ля-соль-
ля-си (А-Г-А-Т) из симфонии.

Инструменты различаются атакой и движением высоты, не только полосами:
- голос: подъезд к ноте снизу на треть полутона (макрос высоты), вибрато через 0.2 с;
- саксофон: первые тики на всех полосах, подъезд на 3/4 полутона, рычание - тремоло 4 тика;
- фортепьяно: удар D4, первый тик ярче; глиссандо и тремоло - арпеджио по тикам
  с повторным ударом (RETRIG);
- гитара: щипок на СЧ+ВЧ, подтяжки - лигой со скольжением;
- контрабас: щелчок слэпа - первый тик на всех полосах; ритм-гитара тише, без D3;
- баллада тише: голос и бас без D3.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from compose import Song                                     # noqa: E402
from instruments import HF, MF, LF, LOUD, HIT, HOLD, KIT6, KIT7   # noqa: E402
from common import chord, name, walking, check_harmony      # noqa: E402
from common import shuffle as common_shuffle                 # noqa: E402

RPB = 3
B12 = ["A7", "A7", "A7", "A7", "D7", "D7", "A7", "A7", "E7", "D7", "A7", "E7"]

# --- шаффл: common.shuffle при 3 строках на долю ---


def shuffle(text, drums=False):
    return common_shuffle(text, drums, RPB)


# --- аккомпанемент ---

COMP_ROOT = {"A": 33, "D": 38, "E": 28}        # ритм-гитара: ля и ми на басовых струнах
BASS_ROOT = {"A": 21, "D": 26, "E": 28}
BOOGIE = [0, 4, 7, 9, 10, 9, 7, 4]


def n8(n, ln="8"):
    return "o%d %s%s" % (n // 12, name(n), ln)


def boogie(harm, base=BASS_ROOT, up=0):
    """Бас буги восьмыми: 1-3-5-6-b7-6-5-3"""
    out = []
    for sym in harm:
        r = base[sym[0]] + up
        out.append(" ".join(n8(r + d) for d in BOOGIE) + " |")
    return " ".join(out)


def comp(harm):
    """Ритм-гитара «квинта-секста»: (корень, квинта/секста); в последнем такте
    квадрата - септима. -> (нижний голос, верхний)"""
    lo, hi = [], []
    for i, sym in enumerate(harm):
        r = COMP_ROOT[sym[0]]
        top = [7, 7, 9, 9, 10, 10, 9, 9] if i % 12 == 11 else [7, 7, 9, 9, 7, 7, 9, 9]
        lo.append(" ".join(n8(r) for _ in top) + " |")
        hi.append(" ".join(n8(r + d) for d in top) + " |")
    return " ".join(lo), " ".join(hi)


def rest(n):
    return "r1 | " * n


def hit(sym, base, tail="r4 r2 |"):
    """Удар на первую долю"""
    return n8(base[sym[0]], "4") + " " + tail


# --- мелодии ---

HEAD = [
    "r8 o3 e8 a8 a8 g8 a8 b4", "o4 c#4 o3 a4 r2",
    "r8 o3 e8 a8 a8 g8 a8 b4", "o4 d4 c#8 o3 a8 r2",
    "r8 o3 a8 o4 d8 d8 c8 d8 e4", "o4 f#4 d4 r2",
    "r8 o3 e8 a8 a8 g8 a8 b4", "o4 c#4 o3 a4 r2",
    "r4 o3 b4 b8 a8 g#8 b8", "o3 a4 f#4 a8 f#8 d4",
    "o3 e8 g8 a8 o4 c8 c#4 o3 a4", "r4 o3 b4 g#4 e4",
]
HEAD2_END = [           # второй квадрат: в конце выше, с подъездом
    "r4 o4 e4 e8 d8 o3 b8 o4 d8", "o4 c#4 o3 a4 o4 c8 c#8 d4",
    "o4 e8 c#8 o3 a8 o4 c8 c#4 o3 a4", "r4 o3 b8 o4 d8 e4 o3 b4",
]
WOP = {1: "r2 o3 e8 e8 c#4", 3: "r2 o3 g8 g8 e4", 5: "r2 o3 a8 a8 f#4", 7: "r2 o3 e8 e8 c#4"}
OOH = ["o3 d1", "o3 c1", "o3 c#1", "o3 d1"]           # такты 9-12: септимы и терции

PIANO_SOLO = [
    "o4 [e12 g12 a12]4", "o4 [e12 g12 a12]4",
    "o4 c12 c#12 e12 a4 g12 e12 c#12 o3 a4",
    "{retrig 3} @pgliss o5 a4 {retrig 0} @piano o3 a4 b12 o4 c12 c#12 e4",
    "[o4 f#12 a12 o5 c12]4", "o4 d12 f12 f#12 a4 o5 c12 o4 a12 f#12 d4",
    "{retrig 5} @ptrem o4 c#2. {retrig 0} @piano o3 a12 o4 c12 c#12",
    "o4 e4 c#12 c12 o3 a12 g4 a4",
    "o4 b12 g#12 e12 b12 g#12 e12 d12 o3 b12 g#12 o4 d12 o3 b12 g#12",
    "o4 a12 f#12 d12 a12 f#12 d12 o5 c4 o4 a4",
    "{retrig 3} @pgliss o5 e4 {retrig 0} @piano o4 c12 c#12 e12 a4 e4",
    "o4 g#12 b12 o5 d12 e4 o4 b4 g#4",
]
BALLAD = [
    "o4 e4 c#6 o3 a12 b4 o4 c#4", "o3 a2 r12 a12 b12 o4 c#4",
    "o4 d4 f#6 e12 d4 c#4", "o3 b2. r4",
    "o4 e4 a6 g#12 f#4 e4", "o4 c#2 r12 o3 a12 b12 o4 c#4",
    "o4 d4 f#6 a12 f#4 e4", "o4 e2. r4",
]
BALLAD_H = ["A", "F#m", "D", "E7"] * 2
BAL_OOH_HI = ["o3 e1", "o3 f#1", "o3 f#1", "o3 g#1"] * 2
BAL_OOH_LO = ["o3 c#1", "o3 c#1", "o2 a1", "o2 b1"] * 2
BAL_TING = {"A": "o4 [a12 o5 c#12 e12 o4]4", "F#m": "o4 [a12 o5 c#12 f#12 o4]4",
            "D": "o4 [a12 o5 d12 f#12 o4]4", "E7": "o4 [g#12 b12 o5 d12 o4]4"}
BAL_BASS = {"A": (21, 28), "F#m": (18, 25), "D": (26, 21), "E7": (28, 23)}

SAX = [
    "{trem 4} o3 a4 a4 {trem 0} g8 a8 b4", "o4 c#4 r4 o3 a8 o4 c8 c#8 e8",
    "{trem 4} o3 e2 {trem 0} g8 e8 g8 a8", "o3 a4 g8 a8 o4 c8 c#8 o3 a4",
    "{trem 4} o4 d4 d4 {trem 0} c8 d8 e4", "o4 f#4 r4 f8 _f#8 d4",
    "o4 e8 c#8 o3 a8 g8 a4 r4", "o3 c8 c#8 e8 g8 a8 o4 c8 c#8 e8",
    "{trem 3} o4 b2 {trem 0} a8 g#8 e8 d8", "o4 a4 f#8 a8 o5 c8 o4 a8 f#4",
    "o4 e4 c#8 e8 c8 c#8 o3 a4", "o3 b4 g#4 e4 {trem 4} o2 e4 {trem 0}",
]
TRADE = [
    "@piano o4 [e12 g12 a12]4", "o4 c12 c#12 e12 a4 g12 e12 c#12 o3 a4",
    "{retrig 5} @ptrem o4 e2. {retrig 0} @piano c#12 c12 o3 a12", "o3 g12 a12 o4 c12 c#4 o3 a4 r4",
    "@gtr o4 d8 d8 d8 d8 c8 d8 c8 o3 a8", "o4 f#4 f#8 d8 f8 _f#8 d4",
    "o4 e8 e8 e8 e8 c#8 e8 c8 c#8", "o4 a4 g8 e8 c#4 o3 a4",
    "@sax {trem 4} o3 b4 b4 {trem 0} a8 g#8 e4", "o3 a4 f#8 a8 o4 c8 c#8 d4",
    "o4 e4 c#8 c8 o3 a8 g8 a4", "o3 g#4 b4 o4 d4 e4",
]
TRADE_LO = ["o3 a8 a8 a8 a8 g8 a8 g8 e8", "o4 d4 d8 o3 a8 o4 c8 _d8 o3 a4",
            "o4 c#8 c#8 c#8 c#8 o3 a8 o4 c#8 o3 a8 a8", "o4 e4 c#8 o3 a8 a4 e4"]
BASS_SOLO = [
    " ".join(n8(21 + d) for d in BOOGIE),
    "o1 a8 o2 a8 o1 a8 o2 a8 g8 e8 c#8 c8",
    "o1 a8 o2 c#8 e8 g8 a8 g8 e8 c#8",
    "o2 c#4 d4 d#4 e4",
]
GTR = [
    "o4 e8 e8 e8 e8 e8 e8 e8 e8", "o4 g8 e8 g8 e8 c#4 o3 a4",
    "o4 c8 _c#8 e8 a8 _b8 a8 e8 c#8", "o4 a2 g8 e8 c#8 c8",
    "o4 d8 f#8 a8 o5 c8 d4 o4 a4", "o4 f8 _f#8 f#8 f#8 f#4 d4",
    "o4 a8 a8 a8 a8 g8 a8 g8 e8", "o4 c#4 c8 o3 a8 g8 e8 a4",
    "o4 b4 o5 _d4 o4 b8 g#8 e4", "o4 a8 f#8 d8 f#8 a8 o5 c8 d4",
    "o4 e8 c#8 o3 a8 o4 c8 c#8 e8 a4", "o4 g#8 b8 o5 d8 e8 r2",
]
GTR_LO = {0: "o4 c#8 c8 c#8 c8 c#8 c8 c#8 c8", 6: "o4 e8 e8 e8 e8 c#8 e8 c#8 o3 a8"}
STOP = ["r4 o3 a8 g8 a8 b8 o4 c#4", "r4 o3 a8 g8 a8 b8 o4 d4",
        "r4 o4 e8 d8 c#8 o3 b8 a4", "o3 a2 r2"]
TAG_RIFF = [" ".join(n8(45 + d) for d in BOOGIE), " ".join(n8(50 + d) for d in BOOGIE)]


def harmony_marks(s, sections):
    out = []
    bar = 0
    for harm in sections:
        for sym in harm:
            out.append((bar * s.bar_rows, sym))
            bar += 1
    return out


def song():
    s = Song("ЯЗС-БУГИ", "РОК-Н-РОЛЛ 50-Х ДЛЯ ЯЗС 5/2", bpm=150,
             rows_per_beat=RPB, beats_per_bar=4, frame_bars=2)
    I = s.instrument
    gliss = [0, 0]
    for d in [-1, -3, -5, -7, -8, -10, -12, -13, -15, -17]:   # ля мажор вниз
        gliss += [d] * 3
    I("voice", "ГОЛОС", [HOLD | HIT | LOUD | LF | MF], pitch=[-6, 2, 2, 2, 0],
      vib=(22, 4, 3), meter=(14, 12, 2))
    I("voice_s", "ГОЛОС", [HOLD | HIT | LF | MF], pitch=[-6, 2, 2, 2, 0],
      vib=(22, 4, 3), meter=(12, 10, 2))
    I("wop", "ХОР", [HOLD | HIT | LF | MF], meter=(11, 8, 3))
    I("wah", "ХОР", [HOLD | LF] * 10 + [HOLD | HIT | LF | MF], vib=(30, 4, 2), meter=(10, 9, 2))
    I("ooh", "ХОР", [HOLD | LF], vib=(30, 4, 2), meter=(9, 8, 1))
    I("sax", "САКСОФОН", [HOLD | HIT | LOUD | LF | MF | HF] * 2 + [HOLD | HIT | LOUD | LF | MF],
      pitch=[-12, 3, 3, 3, 3, 0], vib=(30, 4, 3), meter=(15, 13, 2))
    I("gtr", "ГИТАРА", [HIT | LOUD | MF | HF], vib=(40, 5, 2), meter=(14, 0, 6))
    I("gtr2", "ГИТАРА", [HIT | MF | HF], meter=(11, 0, 6))
    piano = [HIT | LOUD | LF | MF | HF, HIT | LOUD | LF | MF]
    I("piano", "ФОРТЕП.", piano, meter=(14, 0, 8))
    I("pgliss", "ГЛИСС.", piano, arp=gliss, meter=(14, 0, 8))
    I("ptrem", "ТРЕМОЛО", piano, arp=[0] * 4 + [12] * 5 + [0] * 5, arp_loop=4, meter=(14, 10, 4))
    I("ting", "ФОРТЕП.", [HIT | LF | MF], meter=(9, 0, 10))
    I("rgtr", "РИТМ-ГИТ", [HIT | LF | MF], meter=(10, 0, 10))
    I("rgtr_l", "РИТМ-ГИТ", [HIT | LOUD | LF | MF], meter=(13, 0, 10))
    I("bass", "КОНТРАБ.", [HIT | LOUD | LF | MF | HF, HIT | LOUD | LF], meter=(15, 0, 6))
    I("bass_s", "КОНТРАБ.", [HIT | LF | MF, HIT | LF], meter=(12, 0, 6))

    br = s.bar_rows
    SECTIONS = [                    # (имя, гармония по тактам)
        ("intro", ["E7", "D7", "A7", "E7"]),
        ("a1", B12), ("a2", B12), ("piano", B12),
        ("trans", ["A7", "A"]), ("ballad", BALLAD_H),
        ("sax", B12), ("trade", B12),
        ("break", ["A7"] * 4), ("gtr", B12), ("stop", ["A7"] * 4), ("final", B12),
        ("tag", ["A7", "D7", "A7", "A6"]), ("end", ["A6"]),
    ]
    at = {}
    bar = 0
    for nm, h in SECTIONS:
        at[nm] = bar
        bar += len(h)
    total = bar
    R = lambda nm: at[nm] * br

    sh = shuffle
    ch = {c: [] for c in range(7)}

    def add(c, text, swing=True, drums=False):
        ch[c].append(sh(text, drums) if swing else text)

    # --- вступление: фортепьяно соло ---
    add(0, "@piano o4 [g#12 b12 o5 d12 o4]4 | o4 [f#12 a12 o5 c12 o4]4 | "
           "{retrig 5} @ptrem o4 c#2. {retrig 0} @piano o3 a12 o4 c12 c#12 | o4 g#4 b4 o5 d4 e4 |")
    add(1, rest(4))
    add(2, rest(4))
    add(3, rest(4))
    add(4, "@piano " + boogie(["E7", "D7", "A7", "E7"], up=12))
    add(5, "r1 | r1 | r1 | r2 S8 S8 S8 S8 |", drums=True)
    add(6, rest(4), drums=True)

    # --- тема, два квадрата ---
    add(0, "@voice " + " | ".join(HEAD) + " | " + " | ".join(HEAD[:8] + HEAD2_END) + " |")
    add(1, rest(12) + " ".join(("@wop " + WOP[i] + " |") if i in WOP else
                               (("@wah " + OOH[i - 8] + " |") if i >= 8 else "r1 |")
                               for i in range(12)))
    lo, hi = comp(B12 * 3)
    add(2, "@rgtr " + lo)
    add(3, "@rgtr " + hi)
    add(4, "@bass " + boogie(B12 * 3))
    beat = "K4 s4 K4 s4 | "
    fill = "K4 s4 t8 u8 v8 w8 | "
    hats = "h8 h8 h8 h8 h8 h8 h8 h8 | "
    add(5, (beat * 11 + fill) * 3, drums=True)
    add(6, hats * 36, drums=True)

    # --- соло фортепьяно (каналы 2-4 - выше, вместе с темой) ---
    add(0, " | ".join(PIANO_SOLO) + " |")
    add(1, rest(12))

    # --- переход: удар, глиссандо, замедление; такт баллады ---
    add(0, "@piano o4 a4 {retrig 3} @pgliss o5 a4 {retrig 0} r2 | r1 |")
    add(1, rest(2))
    add(2, hit("A7", COMP_ROOT) + " r1 |")
    add(3, "o3 g4 r4 r2 | @ting " + BAL_TING["A"] + " |", swing=False)
    add(4, hit("A7", BASS_ROOT) + " @bass_s o1 a4 r4 o2 e4 r4 |", swing=False)
    add(5, "k4 r4 r2 | r1 |", drums=True)
    add(6, "H4 r4 r2 | r1 |", drums=True)

    # --- баллада 12/8 ---
    add(0, "@voice_s " + " | ".join(BALLAD) + " |", swing=False)
    add(1, "@ooh " + " | ".join(BAL_OOH_HI) + " |", swing=False)
    add(2, "@ooh " + " | ".join(BAL_OOH_LO) + " |", swing=False)
    add(3, " ".join(BAL_TING[h] + " |" for h in BALLAD_H), swing=False)
    add(4, "@bass_s " + " ".join("%s r4 %s r4 |" % (n8(BAL_BASS[h][0], "4"), n8(BAL_BASS[h][1], "4"))
                    for h in BALLAD_H), swing=False)
    add(5, "K4 S4 K4 S4 | " * 8, swing=False, drums=True)
    add(6, ("z12 " * 12 + "| ") * 8, swing=False, drums=True)

    # --- саксофон и четверки, 132 ---
    add(0, "@sax {porta 6} " + " | ".join(SAX) + " | " + " | ".join(TRADE) + " |")
    add(1, rest(16) + "@gtr2 {porta 6} " + " | ".join(TRADE_LO) + " | " + rest(4))
    lo, hi = comp(B12 * 2)
    add(2, "@rgtr " + lo)
    add(3, "@rgtr " + hi)
    add(4, "@bass " + walking(B12 * 2, low=21))
    ride = "z4 z8 z8 z4 z8 z8 | "
    add(5, (beat * 11 + fill) * 2, drums=True)
    add(6, ride * 24, drums=True)

    # --- соло контрабаса, разгон ---
    add(0, rest(4))
    add(1, rest(4))
    add(2, rest(4))
    add(3, rest(4))
    add(4, " | ".join(BASS_SOLO) + " |")
    add(5, "r1 | r1 | r1 | t8 t8 u8 u8 v8 v8 w8 w8 |", drums=True)
    add(6, "r8 x8 r8 x8 r8 x8 r8 x8 | " * 3 + "r1 |", drums=True)

    # --- соло гитары, 192 ---
    add(0, "@gtr {porta 8} " + " | ".join(GTR) + " |")
    add(1, "@gtr2 " + " ".join((GTR_LO[i] + " |") if i in GTR_LO else "r1 |" for i in range(12)))
    lo, hi = comp(B12)
    add(2, "@rgtr " + lo)
    add(3, "@rgtr " + hi)
    add(4, "@bass " + boogie(B12))
    beat_l = "K4 s4 K4 s4 | "
    add(5, beat_l * 11 + fill, drums=True)
    add(6, hats * 12, drums=True)

    # --- стоп-тайм ---
    add(0, "@voice " + " | ".join(STOP) + " |")
    add(1, rest(4))
    add(2, hit("A7", COMP_ROOT) * 4)
    add(3, "o3 g4 r4 r2 | " * 4)
    add(4, hit("A7", BASS_ROOT) * 4)
    add(5, "k4 r4 r2 | " * 3 + "k4 r4 t8 u8 v8 w8 |", drums=True)
    add(6, "H4 r4 r2 | " * 4, drums=True)

    # --- тема, громче ---
    add(0, "@voice " + " | ".join(HEAD[:8] + HEAD2_END) + " |")
    add(1, " ".join(("@wop " + WOP[i] + " |") if i in WOP else
                    (("@wah " + OOH[i - 8] + " |") if i >= 8 else "r1 |") for i in range(12)))
    lo, hi = comp(B12)
    add(2, "@rgtr_l " + lo)
    add(3, "@rgtr_l " + hi)
    add(4, "@bass " + boogie(B12))
    add(5, ("k4 s4 k4 s4 | " * 11) + fill, drums=True)
    add(6, hats * 12, drums=True)

    # --- концовка: рифф в унисон, удары, аккорд A6 с тремоло ---
    add(0, "@sax " + " | ".join(TAG_RIFF) + " | o3 a4 r8 a8 r4 a4 | "
           "{retrig 5} @ptrem o4 e1 | {retrig 0} @piano o4 a4 r4 r2 |")
    add(1, "r1 | r1 | @wop o3 e4 r8 e8 r4 e4 | @sax o3 f#1 | o3 e4 r4 r2 |")
    add(2, "@rgtr_l " + boogie(["A7", "D7"], up=12) + " o2 a4 r8 a8 r4 a4 | o2 a1 | o2 a4 r4 r2 |")
    add(3, "r1 | r1 | o3 g4 r8 g8 r4 g4 | @rgtr_l o3 c#1 | o3 c#4 r4 r2 |")
    add(4, "@bass " + boogie(["A7", "D7"]) + " o1 a4 r8 a8 r4 a4 | o1 a1 | o1 a4 r4 r2 |")
    add(5, "k4 s4 k4 s4 | k4 s4 k4 s4 | k4 r8 k8 r4 k4 | "
           "k4 S12 S12 S12 S12 S12 S12 S12 S12 S12 | k4 r4 r2 |", drums=True)
    add(6, hats * 2 + "H4 r8 H8 r4 H4 | H1 | H4 r4 r2 |", drums=True)

    for c in range(5):
        end = s.voice(c, " ".join(ch[c]), octave=3, length=4)
        assert end == total * br, (c, end // br, total)
    for c in (5, 6):
        end = s.drums(c, " ".join(ch[c]), KIT6 if c == 5 else KIT7, length=4)
        assert end == total * br, (c, end // br, total)

    # --- темп ---
    s.set_tempo(R("a1"), 176)
    s.ritard(R("trans"), R("trans") + br - 1, 176, 120, every=3)
    s.set_tempo(R("trans") + br, 66)
    s.set_tempo(R("sax"), 132)
    s.ritard(R("break"), R("gtr") - 1, 132, 192, every=6)
    s.set_tempo(R("gtr"), 192)
    s.ritard(R("tag") + 2 * br, R("end") + br - 1, 192, 116, every=3)

    # --- подписи ---
    T = s.text
    T(0, "БУГИ-ВУГИ: ЛЕВАЯ РУКА 1-3-5-6-7-6-5-3, ТРЕМОЛО ОКТАВАМИ")
    T(R("a1"), "ГОЛОС: ПОДЪЕЗД К НОТЕ СНИЗУ, ВИБРАТО ЧЕРЕЗ 0.2 С. ШАФФЛ 2:1")
    T(R("a1") + br, "РИТМ-ГИТАРА: КВИНТА-СЕКСТА, КОНТРАБАС СО ЩЕЛЧКОМ СЛЭПА")
    T(R("a2"), "ХОР ДУ-ВОП: ОТВЕТЫ ГОЛОСУ, ДУ-ВА - ПОЛОСЫ НЧ -> НЧ+СЧ")
    T(R("piano"), "ФОРТЕПЬЯНО: ТРИОЛИ, ГЛИССАНДО - АРПЕДЖИО С ПОВТОРНЫМ УДАРОМ")
    T(R("ballad"), "БАЛЛАДА 12/8, 66: ТРИОЛИ ФОРТЕПЬЯНО, ХОР У-У НА D5")
    T(R("sax"), "САКСОФОН: ПОДЪЕЗД, РЫЧАНИЕ - ТРЕМОЛО 4 ТИКА. СВИНГ 132")
    T(R("trade"), "ЧЕТВЕРКИ: ФОРТЕПЬЯНО, ГИТАРА ДВОЙНЫМИ НОТАМИ, САКСОФОН")
    T(R("break"), "СОЛО КОНТРАБАСА: СЛЭП НА СЛАБЫХ ДОЛЯХ, РАЗГОН 132 -> 192")
    T(R("gtr"), "СОЛО ГИТАРЫ: ДВОЙНЫЕ НОТЫ, ПОДТЯЖКИ ЛИГОЙ СО СКОЛЬЖЕНИЕМ")
    T(R("stop"), "СТОП-ТАЙМ: ВЕСЬ СОСТАВ - НА ПЕРВУЮ ДОЛЮ")
    T(R("final"), "ТЕМА: А-Г-А-Т - ЛЯ-СОЛЬ-ЛЯ-СИ")
    T(R("tag"), "КОНЦОВКА: РИФФ В УНИСОН, АККОРД A6 С ТРЕМОЛО")

    s.harmony = harmony_marks(s, [h for _, h in SECTIONS])
    s.sections = at
    return s


if __name__ == "__main__":
    s = song()
    s.build()
    print(s.stats, s.sections)
    print("вне аккорда:", check_harmony(s, s.harmony, every=s.rpb, label="буги",
                                        allow=(0, 2, 6, 11)))   # до, ре, фа-диез, си: блюзовая терция, кварта, секста, нона
