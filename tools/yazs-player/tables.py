# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
"""
Таблицы проигрывателя и перекодировка текста в коды экрана Агата-9.

Высота в движке - число в 1/16 полутона от C0: нота * 16 + доли. Период
счетчика ВИ53 берется из таблицы нулевой октавы (12 x 16 значений) и
сдвигается вправо на номер октавы с округлением.
"""

import math

CLOCK = 1021000          # такт счетчиков тонов, Гц (такт процессора Агата-9)
TIMER_CLOCK = 7980       # такт счетчика прерываний ЯЗС, Гц (CLOCK / 128)
A4 = 440.0
NOTES = 96               # C0..B7
FINE = 16                # долей в полутоне


def freq(note, fine=0):
    """note: 0 = C0; fine: 1/16 полутона"""
    return A4 * 2 ** ((note + fine / FINE - 57) / 12.0)


def period_table():
    """Периоды нулевой октавы: 12 полутонов x 16 долей"""
    return [int(round(CLOCK / freq(n, f))) for n in range(12) for f in range(FINE)]


def period(note, fine=0):
    """Период, который даст движок (тот же расчет, что в player.a65)"""
    tab = period_table()
    total = note * FINE + fine
    q, f = divmod(total, FINE)
    octv, pc = divmod(q, 12)
    p = tab[pc * FINE + f]
    if octv:
        p = (p + (1 << (octv - 1))) >> octv
    return p


VIB_STEPS = 64           # шагов на период вибрато
VIB_DEPTHS = 16          # глубина 0..15 долей


def vibrato_table():
    """vib[depth * 64 + phase] - смещение в долях, со знаком (байт)"""
    out = []
    for d in range(VIB_DEPTHS):
        for p in range(VIB_STEPS):
            v = int(round(d * math.sin(2 * math.pi * p / VIB_STEPS)))
            out.append(v & 0xFF)
    return out


# Экран Агата-9 (АЦР-32 и АЦР-64): коды знакогенератора A9-fnt.rom.
# $A0-$BF - пробел, знаки и цифры, $C0-$DF - латиница (заглавные),
# $E0-$FF - кириллица заглавная в порядке КОИ-7, $60-$7F - строчная,
# $40-$5F - строчная латиница, $00-$1F - псевдографика
_CYR = "ЮАБЦДЕФГХИЙКЛМНОПЯРСТУЖВЬЫЗШЭЩЧЪ"
_CYR_LOW = "юабцдефгхийклмнопярстужвьызшэщчъ"
_ASCII = " !\"#$%&'()*+,-./0123456789:;<=>?"
_LAT = "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
_LAT_LOW = "@abcdefghijklmnopqrstuvwxyz{|}~"

AGAT = {}
for i, ch in enumerate(_ASCII):
    AGAT[ch] = 0xA0 + i
for i, ch in enumerate(_LAT):
    AGAT[ch] = 0xC0 + i
for i, ch in enumerate(_LAT_LOW):
    AGAT.setdefault(ch, 0x40 + i)
for i, ch in enumerate(_CYR):
    AGAT[ch] = 0xE0 + i
for i, ch in enumerate(_CYR_LOW):
    AGAT[ch] = 0x60 + i
AGAT["Ё"] = AGAT["Е"]
AGAT["ё"] = AGAT["е"]
# Псевдографика: столбики снизу 2/8, 4/8, 7/8, рамки
GFX = {"bar1": 0x01, "bar2": 0x02, "bar3": 0x03,
       "tl": 0x10, "tt": 0x11, "tr": 0x12, "lt": 0x13, "cross": 0x14,
       "h": 0x1B, "rt": 0x1C, "bl": 0x1D, "bt": 0x1E, "br": 0x1F}


def agat(text, width=None, upper=False):
    """Строка в коды экрана; width - дополнить пробелами или обрезать"""
    if upper:
        text = text.upper()
    out = bytearray()
    for ch in text:
        if ch not in AGAT:
            raise ValueError("нет знака %r в наборе Агата" % ch)
        out.append(AGAT[ch])
    if width is not None:
        out = out[:width] + bytes([0xA0]) * max(0, width - len(out))
    return bytes(out)


def name_dos(text):
    """Имя файла каталога ДОС: 30 знаков, старший бит, дополнено $A0"""
    return agat(text, 30, upper=True)


if __name__ == "__main__":
    tab = period_table()
    print("C0..B0:", tab[::16])
    for n, name in ((57, "A4"), (60, "C5"), (24, "C2"), (84, "C7"), (95, "B7")):
        p = period(n)
        print("%s: N=%d, %.2f Гц (надо %.2f), %+.1f цента" % (
            name, p, CLOCK / p, freq(n), 1200 * math.log2(CLOCK / p / freq(n))))
