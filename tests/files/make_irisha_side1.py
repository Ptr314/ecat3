#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
"""Делает irisha-side1.dsk: минимальный образ Ириши для проверки порядка дорожек.

Геометрия дискеты Ириши: 2 стороны, 40 дорожек, 9 секторов по 512 байт, CP/M с
блоком 2048 байт, 64 записи каталога, без системных дорожек. Файл короче полного
объема эмулятор дополняет нулями, поэтому достаточно двух дорожек образа.

В образе один файл SIDE1.TXT, чей единственный блок 90 лежит на логической
дорожке 40, то есть на цилиндре 0 стороны 1. В порядке cylinders (дампы .dsk)
эта дорожка идет в образе второй, со смещения 4608; в порядке sides она была бы
на смещении 40*4608, и TYPE не нашел бы данных.
"""
import os

TRACK = 9 * 512
image = bytearray(2 * TRACK)

# Каталог: 64 записи по 32 байта, свободные заполнены E5
for i in range(64):
    image[i * 32:(i + 1) * 32] = b"\xE5" * 32
entry = bytearray(32)
entry[0] = 0                                   # пользователь 0
entry[1:12] = b"SIDE1   TXT"
entry[12:15] = b"\x00\x00\x00"                 # extent 0
entry[15] = 1                                  # одна запись по 128 байт
entry[16] = 90                                 # блок 90 = 184320 = логическая дорожка 40
image[0:32] = entry

# Данные файла: цилиндр 0, сторона 1 = вторая дорожка образа в порядке cylinders
data = b"SIDE 1 OK\r\n"
data += b"\x1A" * (128 - len(data))
image[TRACK:TRACK + len(data)] = data

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "irisha-side1.dsk")
with open(out, "wb") as f:
    f.write(image)
print("written", out, len(image), "bytes")
