#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
"""Делает uknc-hdd.img: крошечный образ винчестера УК-НЦ для проверки контроллера.

Геометрию контроллер берёт из первого сектора образа, как это делают программы
разметки WD и ID: байт 0 - число секторов на дорожке, байт 1 - число головок,
число цилиндров считается по размеру файла. Здесь 4 сектора, 2 головки и 8
цилиндров - 64 сектора, 32 КБ, чего хватает, чтобы проверить пересчёт
цилиндр-головка-сектор в смещение.

В каждом секторе первое слово - его номер по порядку, второе - то же число в
обратном коде, дальше текст с номером. Сценарий читает несколько секторов по
разным адресам и сверяет эти слова, так что ошибка в пересчёте видна сразу.
Первый сектор занят геометрией, его первое слово - это её два байта.
"""
import os

SECTORS = 4
HEADS = 2
CYLINDERS = 8
SECTOR_SIZE = 512

image = bytearray(CYLINDERS * HEADS * SECTORS * SECTOR_SIZE)

for lba in range(CYLINDERS * HEADS * SECTORS):
    sector = bytearray(SECTOR_SIZE)
    if lba == 0:
        # Геометрия: эти два байта контроллер читает при вставке образа
        sector[0] = SECTORS
        sector[1] = HEADS
    else:
        sector[0:2] = lba.to_bytes(2, "little")
        sector[2:4] = (lba ^ 0xFFFF).to_bytes(2, "little")
    text = b"UKNC HDD TEST SECTOR %d" % lba
    sector[4:4 + len(text)] = text
    image[lba * SECTOR_SIZE:(lba + 1) * SECTOR_SIZE] = sector

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "uknc-hdd.img")
with open(out, "wb") as f:
    f.write(image)
print("written", out, len(image), "bytes")
