#!/usr/bin/env python3
# Образы винчестера для тестов СМК-512 БК.
#
# bk-smk-hdd.img - не генерируется: это test_hdd.img из тестов bkemu-android
# (github.com/3cky/bkemu-android, lib/src/test/resources, GPL-3.0). Таблица
# разделов АльтПро в блоке 7 (40 цилиндров, 16 головок, 63 сектора) и только
# начало диска, 100 КБ, но системы на нем хватает для загрузки ANDOS.
#
# bk-smk-hdd.hdi делает этот скрипт: тот же образ с 512-байтным паспортом
# накопителя (ответ на IDENTIFY) спереди, как у образов *.hdi. Геометрия в
# паспорте нарочно неверная (99/2/17): тест boot-bk0011m-smk-hdi проверяет, что
# таблица разделов главнее паспорта, а сектора читаются после заголовка.
# Модель, номер и версия в паспорте свои, и контрольная сумма в слове 255
# верная: work-bk0011m-smk-identify проверяет, что на IDENTIFY приходит этот
# паспорт, с геометрией 40/16/63 вместо неверной (и в словах 54-58 и 60-61
# тоже) и с пересчитанной суммой.
#
#   python make_bk_smk_hdi.py

import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))


def text(header, word, s, words):
    # Строка паспорта: байты в слове переставлены, хвост - пробелы
    s = s.ljust(words * 2).encode('ascii')
    for i, c in enumerate(s):
        header[word * 2 + (i ^ 1)] = c


raw = open(os.path.join(HERE, 'bk-smk-hdd.img'), 'rb').read()
header = bytearray(512)
wrong = 99 * 2 * 17
struct.pack_into('<H', header, 0, 0x045A)   # несъемный накопитель
struct.pack_into('<H', header, 2, 99)       # слово 1: цилиндры
struct.pack_into('<H', header, 6, 2)        # слово 3: головки
struct.pack_into('<H', header, 12, 17)      # слово 6: секторы
text(header, 10, 'HDI-0001', 10)            # слова 10-19: заводской номер
text(header, 23, '2.0', 4)                  # слова 23-26: версия прошивки
text(header, 27, 'HDI PASSPORT TEST DRIVE', 20)  # слова 27-46: модель
struct.pack_into('<H', header, 98, 0x0200)  # слово 49: LBA
struct.pack_into('<H', header, 106, 1)      # слово 53: слова 54-58 верны
struct.pack_into('<HHHI', header, 108, 99, 2, 17, wrong)   # слова 54-58
struct.pack_into('<I', header, 120, wrong)  # слова 60-61: секторов всего
header[510] = 0xA5                          # слово 255: подпись и сумма
header[511] = (0x100 - sum(header[:511])) & 0xFF
open(os.path.join(HERE, 'bk-smk-hdd.hdi'), 'wb').write(bytes(header) + raw)
