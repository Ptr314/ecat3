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


# Образы *.hdi для boot-uknc-hdd-hdi: перед диском 512 байт паспорта
# накопителя (ответ на IDENTIFY), слова 1, 3 и 6 - цилиндры, головки, секторы.
#
# uknc-hdd.hdi - тот же диск с разметкой WD, но паспорт говорит о 16 цилиндрах,
# а в файле только 4: образ обрезан, как его раздают для bkemu. Геометрия
# должна быть 16/2/4 - разметка из сектора 0 диска (за паспортом, а не из
# самого паспорта) и цилиндры из паспорта, - а сектора за концом файла читаются
# пустыми.
#
# uknc-hdd-blank.hdi - неразмеченный диск: паспорт 8/2/4 и один пустой сектор.
# Разметки нет, геометрия берется из паспорта целиком.

def passport(cylinders, heads, sectors, model):
    header = bytearray(SECTOR_SIZE)
    header[0:2] = (0x045A).to_bytes(2, "little")   # несъемный накопитель
    header[2:4] = cylinders.to_bytes(2, "little")
    header[6:8] = heads.to_bytes(2, "little")
    header[12:14] = sectors.to_bytes(2, "little")
    text = model.ljust(40).encode("ascii")         # слова 27-46, байты в слове переставлены
    for i, c in enumerate(text):
        header[54 + (i ^ 1)] = c
    header[510] = 0xA5                              # слово 255: подпись и контрольная сумма
    header[511] = (0x100 - sum(header[:511])) & 0xFF
    return bytes(header)


here = os.path.dirname(os.path.abspath(__file__))
hdi = passport(16, HEADS, SECTORS, "UKNC HDI TEST") + bytes(image[:4 * HEADS * SECTORS * SECTOR_SIZE])
with open(os.path.join(here, "uknc-hdd.hdi"), "wb") as f:
    f.write(hdi)
print("written uknc-hdd.hdi", len(hdi), "bytes")

blank = passport(8, HEADS, SECTORS, "UKNC HDI BLANK") + bytes(SECTOR_SIZE)
with open(os.path.join(here, "uknc-hdd-blank.hdi"), "wb") as f:
    f.write(blank)
print("written uknc-hdd-blank.hdi", len(blank), "bytes")
