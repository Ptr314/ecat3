# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
"""
И. Берлин, Puttin' on the Ritz (1929): своя аранжировка - витрина приемов ЯЗС.

Мелодия и гармония - по первому изданию (Irving Berlin Inc., 1929; в США
общественное достояние с 2025 года): куплет фа мажор 16 тактов, припев фа минор
AABA по 8 тактов, бридж ля-бемоль мажор. Свинг 152, такт 4/4 (в оригинале
alla breve).

Форма и приемы:
- вступление: чечетка - два ударных перекликаются триолями, бас на доминанте;
- куплет: труба с сурдиной (СЧ+ВЧ, подъезд), тромбон «уа» (НЧ -> НЧ+СЧ с ударом D4)
  в длинных нотах мелодии, фортепьяно на 2 и 4;
- переход: глиссандо тромбона - лига с портаменто через октаву;
- припев 1: альт-саксофон и тенор вторым голосом, в A2 кларнет, удары меди;
  бридж - вибрафон с «мотором» (TREMOLO: D5 гаснет и возвращается 6 раз в
  секунду), струнные на D5, арфа триолями; A3 - трубы тутти;
- припев 2: синтезатор с портаменто и эхом (тот же голос с DELAY - нота на
  90 мс позже), аккорды арпеджио по тикам (ARP) с разверткой «фильтра» (BUS
  раз в такт), бас октавами, бочка на каждую долю; бридж - чечетка соло на
  шагающем басе; A3 - шаут всем составом;
- концовка: «Put-tin' on the Ritz» дважды, последний раз с замедлением, аккорд и удар.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from compose import Song                                     # noqa: E402
from instruments import HF, MF, LF, LOUD, HIT, HOLD, KIT6, KIT7   # noqa: E402
from common import chord, name, walking, harmonize, shuffle, tp, check_harmony   # noqa: E402

RPB = 3
F_MIN = [0, 1, 3, 5, 7, 8, 10]              # фа минор

# --- мелодия (первое издание) ---
VERSE = ["o4 c4 d4 f4 d4", "o4 f8 e4 g8 ^2", "o4 c4 d4 f4 d4", "o4 f8 e4 g8 ^2",
         "o4 e-4 f4 a-4 f4", "o4 a-8 g4 b-8 ^2", "o4 e-4 f4 a-4 f4", "o4 a-8 g4 b-8 ^2",
         "o5 c2 o4 g4 ^8 e8", "o4 g4 f4 e8 d8 ^4", "o5 c2 o4 g4 ^8 e8", "o4 g4 f4 e8 d8 ^4",
         "o4 a4 a4 e4 e4", "o4 a1", "o4 b4 b4 b8 a8 g4", "o5 c1"]
H_VERSE = ["F", "C", "F", "C7", "Ab", "Eb", "Ab", "Eb7", ["C", "A7"], "G7", ["C", "A7"], "G7",
           "A", "D7", "G7", "C7"]
A_ = ["o4 f4 a-4 o5 c4 o4 c8 f8", "^8 o4 a-8 o5 c4 o4 c4 f4", "o4 a-4 o5 c4 o4 c8 f4 a-8",
      "o5 c4 o4 c4 e4 g4", "o4 b-1", "^2 o5 c8 o4 b-8 a-8 g8", "o4 f1"]
A_FILL = "r1"
BRIDGE = ["o4 b-4 o5 c4 d-4 e-4", "o5 f4 e-4 d-4 c4", "o4 b-4 r8 b-8 ^2", "o5 c4 o4 b4 b-8 o5 e-8 ^4",
          "o4 a-4 b-4 o5 c4 d-4", "o5 e-4 d-4 c4 o4 b-4", "o4 a-4 r8 a-8 ^2", "r1"]
H_A = ["Fm", "Fm", "Fm", "C7", "C7", "C7", "Fm", ["Db7", "C7"]]
H_A3 = H_A[:7] + ["Fm"]
H_B = ["Bbm", "Bbm", "Eb7", "Ab", "Ab", "Eb7", "Ab", ["Db7", "C7"]]
H_CHORUS = H_A + H_A + H_B + H_A3
HOOK = "o5 c8 o4 b-8 a-8 g8"


def n(x, ln):
    return "o%d %s%s" % (x // 12, name(x), ln)


def bars(lst):
    return " | ".join(lst) + " |"


def near(pc, ref):
    return min((x for x in range(ref - 6, ref + 7) if x % 12 == pc), key=lambda x: abs(x - ref))


def tones(sym):
    root, q, _ = chord(sym)
    return [(root + i) % 12 for i in q]


def guide(sym):
    root, q, _ = chord(sym)
    return (root + q[1]) % 12, (root + (q[3] if len(q) > 3 else q[2])) % 12


def per_bar(harm, one, two=None):
    two = two or (lambda x: one(x).replace("1", "2"))
    return " ".join((one(h) if isinstance(h, str) else " ".join(two(x) for x in h)) + " |"
                    for h in harm)


def chorus_melody(last=True):
    a = A_ + [A_FILL]
    a3 = A_ + (["o4 f1"] if not last else [A_FILL])
    return bars(a + a + BRIDGE + a3)


ARP = {"m": 0x37, "": 0x47, "7": 0x4A}


def arp_bar(sym, length="1"):
    root, q, _ = chord(sym)
    kind = "7" if sym.endswith("7") else ("m" if sym.endswith("m") else "")
    return "{arp 0x%02X} %s" % (ARP[kind], n(near(root, 45), length))


def song():
    s = Song("PUTTIN' ON THE RITZ", "БЕРЛИН 1929, СВОЯ АРАНЖИРОВКА", bpm=152,
             rows_per_beat=RPB, beats_per_bar=4, frame_bars=2)
    I = s.instrument
    scoop = [-8, 2, 2, 2, 2, 0]
    I("tptm", "ТРУБА", [HOLD | HIT | LOUD | MF | HF], pitch=[-6, 2, 2, 2, 0], vib=(28, 5, 2), meter=(14, 12, 2))
    I("wah", "ТРОМБОН", [HOLD | HIT | LOUD | LF] * 6 + [HOLD | HIT | LOUD | LF | MF], pitch=scoop,
      meter=(12, 10, 2))
    I("tbn", "ТРОМБОН", [HOLD | HIT | LOUD | LF | MF], meter=(13, 11, 2))
    I("alto", "АЛЬТ", [HOLD | HIT | LOUD | LF | MF], pitch=scoop, vib=(26, 4, 3), meter=(13, 11, 2))
    I("tenor", "ТЕНОР", [HOLD | HIT | LOUD | LF], pitch=scoop, vib=(26, 4, 3), meter=(12, 10, 2))
    I("clar", "КЛАРНЕТ", [HOLD | HIT | LOUD | MF], vib=(24, 4, 2), meter=(13, 11, 2))
    I("tpts", "ТРУБЫ", [HOLD | HIT | LOUD | LF | MF | HF] * 2 + [HOLD | HIT | LOUD | LF | MF],
      pitch=[-6, 2, 2, 2, 0], vib=(30, 5, 2), meter=(15, 13, 2))
    I("stab", "МЕДЬ", [HOLD | HIT | LOUD | LF | MF | HF, HOLD | HIT | LOUD | LF | MF], meter=(14, 10, 4))
    I("vibes", "ВИБРАФОН", [HIT | HOLD | LOUD | LF | MF], meter=(12, 10, 2))
    I("strings", "СТРУННЫЕ", [HOLD | LF], vib=(24, 4, 3), meter=(9, 8, 1))
    I("harp", "АРФА", [HIT | LF | MF | HF], meter=(10, 0, 10))
    I("synth", "СИНТЕЗ.", [HOLD | HIT | LOUD | MF | HF], vib=(20, 6, 2), meter=(14, 12, 2))
    I("echo", "ЭХО", [HOLD | HIT | MF | HF], vib=(20, 6, 2), meter=(10, 8, 2))
    I("arpsyn", "АРПЕДЖИО", [HIT | HOLD | LF | MF], meter=(11, 9, 2))
    I("piano", "ФОРТЕП.", [HIT | LF | MF | HF, HIT | LF | MF], meter=(10, 0, 10))
    I("bass", "КОНТРАБ.", [HIT | LOUD | LF | MF, HIT | LOUD | LF], meter=(14, 0, 6))
    I("sbass", "СИНТ-БАС", [HIT | HOLD | LOUD | LF | MF, HIT | HOLD | LOUD | LF], meter=(14, 8, 4))

    INTRO = ["C7"] * 4
    VAMP = ["C7", "C7"]
    END = ["C7", "Fm", "C7", "Fm"]
    SECTIONS = [("intro", INTRO), ("verse", H_VERSE), ("vamp", VAMP), ("ch1", H_CHORUS),
                ("ch2", H_CHORUS), ("end", END)]
    br = s.bar_rows
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
    R = lambda nm, off=0: (at[nm] + off) * br
    ch = {c: [] for c in range(7)}

    def add(c, text, drums=False):
        ch[c].append(shuffle(text, drums, RPB))

    # Чечетка: такты ударных 6 и 7, перекличка триолями
    TAP6 = ["S12 r12 S12 r4 S12 S12 r12 r4", "r4 p12 r12 p12 r4 S12 S12 S12",
            "S12 r12 r12 S12 r12 r12 p12 p12 r12 S4", "S12 S12 S12 S12 S12 S12 p4 r4"]
    TAP7 = ["r12 x12 r12 x12 h12 x12 r12 r12 x12 x12 h12 x12", "x12 h12 x12 r4 x12 r12 r12 r4",
            "r12 x12 h12 r12 x12 h12 r12 r12 h12 r4", "r4 r4 x12 h12 x12 h4"]
    tap6 = " | ".join(TAP6) + " |"
    tap7 = " | ".join(TAP7) + " |"

    # --- вступление: чечетка на доминанте ---
    add(0, "r1 | " * 4)
    add(1, "r1 | " * 4)
    add(2, "r1 | " * 4)
    add(3, "r1 | " * 4)
    add(4, "@bass o2 c4 r4 r2 | o2 c4 r4 r2 | o2 c4 r4 r2 | o2 c4 r4 g4 b4 |")
    add(5, tap6, drums=True)
    add(6, tap7, drums=True)

    # --- куплет: труба с сурдиной, тромбон «уа», фортепьяно ---
    verse = bars(VERSE)
    add(0, "@tptm " + verse)
    wah_bars = {1: "C", 3: "C7", 5: "Eb", 7: "Eb7", 13: "D7"}
    add(1, "@wah " + " ".join(("r2 %s %s |" % (n(near(guide(wah_bars[i])[0], 52), "4"),
                                                n(near(guide(wah_bars[i])[1], 52), "4"))) if i in wah_bars
                              else "r1 |" for i in range(16)))
    add(2, "@piano " + per_bar(H_VERSE, lambda x: "r4 %s r4 %s" % (n(near(guide(x)[1], 55), "4"),
                                                                  n(near(guide(x)[1], 55), "4")),
                               lambda x: "r4 %s" % n(near(guide(x)[1], 55), "4")))
    add(3, "@piano " + per_bar(H_VERSE, lambda x: "r4 %s r4 %s" % (n(near(guide(x)[0], 50), "4"),
                                                                  n(near(guide(x)[0], 50), "4")),
                               lambda x: "r4 %s" % n(near(guide(x)[0], 50), "4")))

    def two_feel(harm):
        out = []
        for h in harm:
            syms = h if isinstance(h, list) else [h]
            if len(syms) == 1:
                root, q, b = chord(syms[0])
                out.append("%s r4 %s r4 |" % (n(near(b, 29), "4"), n(near((root + q[2]) % 12, 29), "4")))
            else:
                out.append(" ".join("%s r4" % n(near(chord(x)[2], 29), "4") for x in syms) + " |")
        return " ".join(out)
    add(4, "@bass " + two_feel(H_VERSE))
    add(5, "K4 r4 K4 r4 | " * 16, drums=True)
    add(6, "r4 z4 r4 z4 | " * 16, drums=True)

    # --- переход: глиссандо тромбона, брейк ---
    add(0, "r1 | r1 |")
    add(1, "@tbn {porta 3} o3 c2 o4 _c2 | {porta 0} o4 c4 r4 r2 |")
    add(2, "r1 | r1 |")
    add(3, "r1 | r1 |")
    add(4, "o2 c4 r4 r2 | o2 c4 r4 g4 b4 |")
    add(5, "r1 | t8 t8 u8 u8 v8 v8 w8 w8 |", drums=True)
    add(6, "r1 | r1 |", drums=True)

    # --- припев 1 ---
    mel = chorus_melody()
    mel_sw = shuffle(mel, False, RPB)
    c1 = R("ch1")
    harm1 = harmonize(s, mel_sw, hm, F_MIN, c1, lo=3, hi=9)
    # Ведущий: альт (A1), кларнет (A2), вибрафон с мотором (B), трубы (A3)
    def take(text, b0, b1):
        bs = [b for b in text.split("|") if b.strip()]
        return " | ".join(x.strip() for x in bs[b0:b1]) + " |"
    lead1 = ("@alto " + take(mel_sw, 0, 8) + " @clar " + take(mel_sw, 8, 16)
             + " @vibes {trem 16} " + take(mel_sw, 16, 24) + " @tpts {trem 0} " + take(mel_sw, 24, 32))
    ch[0].append(lead1)
    ch[1].append("@tenor " + take(harm1, 0, 16) + " @strings " +
                 shuffle(per_bar(H_B, lambda x: n(near(guide(x)[0], 51), "1"),
                                 lambda x: n(near(guide(x)[0], 51), "2")), False, RPB)
                 + " @alto " + take(harm1, 24, 32))
    # Удары меди: на доли 1 и «и» 2 в тактах 4-6 каждого A, арфа в бридже
    def stabs(w):
        def bar_a(i, x):
            if i in (3, 4, 5):
                return "%s r4 r8 %s r4" % (n(near(guide(x)[w], 50 + 3 * w), "4"), n(near(guide(x)[w], 50 + 3 * w), "8"))
            return "r1"
        out = []
        for sec in range(4):
            for i in range(8):
                h = H_CHORUS[sec * 8 + i]
                if sec == 2:
                    syms = h if isinstance(h, list) else [h, h]
                    trips = []
                    for x in syms:
                        tn = tones(x)
                        trips.append(" ".join(n(near(tn[k % len(tn)], 55 + 2 * w) + (12 if k >= len(tn) else 0),
                                                "12") for k in range(6)))
                    out.append(("@harp " if i == 0 else "") + " ".join(trips) + " |")
                else:
                    x = h if isinstance(h, str) else h[-1]
                    out.append(("@stab q6 " if i == 0 and sec != 2 else "") + bar_a(i, x) + " |")
        return " ".join(out)
    add(2, stabs(0))
    add(3, stabs(1))
    add(4, "@bass " + walking(H_CHORUS, low=24))
    add(5, ("K4 S4 K4 S4 | " * 7 + "K4 S4 t8 u8 v8 w8 | ") * 4, drums=True)
    add(6, "z4 z8 z8 z4 z8 z8 | " * 32, drums=True)

    # --- припев 2: синтезатор, эхо, арпеджио с разверткой, бас октавами ---
    c2 = R("ch2")
    syn = "@synth {porta 6} " + take(mel_sw, 0, 16)
    echo_notes = []
    for t in take(mel_sw, 0, 16).split():
        if t[0] in "abcdefg" or t[0] == "_":
            echo_notes.append("{delay 9} " + t)
        else:
            echo_notes.append(t)
    sweep = [4, 6, 14, 15, 11, 9, 11, 15]
    arps = []
    for i, h in enumerate(H_CHORUS[:16]):
        syms = h if isinstance(h, list) else [h]
        bus = "{bus %d} " % sweep[(i // 2) % len(sweep)]
        arps.append(bus + " ".join(arp_bar(x, "1" if len(syms) == 1 else "2") for x in syms) + " |")
    octb = per_bar(H_CHORUS[:16], lambda x: " ".join(n(near(chord(x)[2], 29) + 12 * (k % 2), "8") for k in range(8)),
                   lambda x: " ".join(n(near(chord(x)[2], 29) + 12 * (k % 2), "8") for k in range(4)))
    # бридж: чечетка соло на шагающем басе; A3: шаут
    add(0, syn + " r1 | " * 8 + " @tpts {porta 0} " + take(mel_sw, 24, 32))
    ch[1].append("@echo " + " ".join(echo_notes) + " " + "r1 | " * 8 + " @alto " + take(harm1, 24, 32))
    ch[2].append("@arpsyn " + " ".join(arps) + " " + "r1 | " * 8 + " {arp 0} {bus 0xFF} @stab q6 "
                 + shuffle(per_bar(H_A3, lambda x: "%s r4 r8 %s r4" % (n(near(guide(x)[0], 50), "4"),
                                                                        n(near(guide(x)[0], 50), "8")),
                                   lambda x: "%s r4" % n(near(guide(x)[0], 50), "4")), False, RPB))
    add(3, "@strings " + per_bar(H_CHORUS[:16], lambda x: n(near(guide(x)[1], 55), "1"),
                                 lambda x: n(near(guide(x)[1], 55), "2"))
        + " " + "r1 | " * 8 + " @stab " + per_bar(H_A3, lambda x: "%s r4 r8 %s r4" % (
            n(near(guide(x)[1], 54), "4"), n(near(guide(x)[1], 54), "8")),
            lambda x: "%s r4" % n(near(guide(x)[1], 54), "4")))
    add(4, "@sbass " + octb + " @bass " + walking(H_B, low=24) + " @sbass " +
        per_bar(H_A3, lambda x: " ".join(n(near(chord(x)[2], 29) + 12 * (k % 2), "8") for k in range(8)),
                lambda x: " ".join(n(near(chord(x)[2], 29) + 12 * (k % 2), "8") for k in range(4))))
    add(5, "k4 k4 k4 k4 | " * 15 + "k4 k4 t8 u8 v8 w8 | " + tap6 + " " + tap6 + " " + ("k4 s4 k4 s4 | " * 7)
        + "k4 s4 t8 u8 v8 w8 |", drums=True)
    add(6, "h8 H8 h8 H8 h8 H8 h8 H8 | " * 16 + tap7 + " " + tap7 + " " + ("o4 z8 z8 z4 z8 z8 | " * 8), drums=True)

    # --- концовка: хук дважды, аккорд, удар ---
    add(0, "r2 " + HOOK + " | o4 f2 r2 | r2 " + HOOK + " | o4 f4 r4 r2 |")
    add(1, "r2 o4 e8 g8 f8 e8 | o4 c2 r2 | r2 o4 e8 g8 f8 e8 | o4 c4 r4 r2 |")
    add(2, "r2 o4 g4 g4 | o4 a-2 r2 | r2 o4 g4 g4 | o4 a-4 r4 r2 |")
    add(3, "r2 o4 b-4 b-4 | o5 c2 r2 | r2 o4 b-4 b-4 | o5 c4 r4 r2 |")
    add(4, "r2 o2 c4 c4 | o2 f2 r2 | r2 o2 c4 c4 | o2 f4 r4 r2 |")
    add(5, "r2 S4 S4 | k2 r2 | r2 S4 S4 | k4 r4 r2 |", drums=True)
    add(6, "r1 | H2 r2 | r1 | H4 r4 r2 |", drums=True)

    for c in range(5):
        end = s.voice(c, " ".join(ch[c]), octave=4, length=4)
        assert end == total * br, (c, end / br, total)
    for c in (5, 6):
        end = s.drums(c, " ".join(ch[c]), KIT6 if c == 5 else KIT7, length=4)
        assert end == total * br, (c, end / br, total)

    s.ritard(R("end", 2), R("end", 4) - 1, 152, 100, every=RPB)
    T = lambda nm, txt, off=0: s.text(R(nm, off) + 1, txt)
    T("intro", "ЧЕЧЕТКА: ДВА УДАРНЫХ ПЕРЕКЛИКАЮТСЯ ТРИОЛЯМИ")
    T("verse", "КУПЛЕТ: ТРУБА С СУРДИНОЙ, ТРОМБОН УА - НЧ, ПОТОМ НЧ+СЧ С D4")
    T("vamp", "ГЛИССАНДО ТРОМБОНА: ЛИГА С ПОРТАМЕНТО ЧЕРЕЗ ОКТАВУ")
    T("ch1", "ПРИПЕВ: АЛЬТ-САКСОФОН, ТЕНОР ВТОРЫМ ГОЛОСОМ, УДАРЫ МЕДИ")
    T("ch1", "КЛАРНЕТ: КВАДРАТ НА ПОЛОСЕ СЧ", 8)
    T("ch1", "ВИБРАФОН С МОТОРОМ: ТРЕМОЛО - D5 ГАСНЕТ И ВОЗВРАЩАЕТСЯ", 16)
    T("ch1", "ТРУБЫ НА ВСЕХ ПОЛОСАХ С ПОДЪЕЗДОМ", 24)
    T("ch2", "СИНТЕЗАТОР: ЭХО - ТА ЖЕ НОТА С DELAY 9 ТИКОВ, ARP И BUS")
    T("ch2", "ЧЕЧЕТКА СОЛО НА ШАГАЮЩЕМ БАСЕ", 16)
    T("ch2", "ШАУТ: ВЕСЬ СОСТАВ", 24)
    T("end", "PUTTIN' ON THE RITZ!")
    s.harmony = hm
    s.sections = at
    return s


if __name__ == "__main__":
    s = song()
    s.build()
    print(s.stats, s.sections)
    for t in s.texts:
        assert len(t) <= 64, len(t)
    print("вне аккорда:", check_harmony(s, s.harmony, channels=[2, 3, 4], every=RPB, label="ritz"))
