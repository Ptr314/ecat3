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
#
#   python make_bk_smk_hdi.py

import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))

raw = open(os.path.join(HERE, 'bk-smk-hdd.img'), 'rb').read()
header = bytearray(512)
struct.pack_into('<H', header, 0, 0x045A)   # несъемный накопитель
struct.pack_into('<H', header, 2, 99)       # слово 1: цилиндры
struct.pack_into('<H', header, 6, 2)        # слово 3: головки
struct.pack_into('<H', header, 12, 17)      # слово 6: секторы
open(os.path.join(HERE, 'bk-smk-hdd.hdi'), 'wb').write(bytes(header) + raw)
