# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
#
# Собирает пробы платы AZBK:
#   AZHWT.BIN, AZPIC.BIN (и листинги) из azhwt.mac, azpic.mac; их копии для
#   тестов - tests/files/azhwt.bin, azpic.bin;
#   AZTEST.IMG - диск данных ANDOS (800 КБ, FAT12, загрузочный сектор
#   andos330.img поставки) с файлами AZHWT и AZPIC, адрес загрузки 01000:
#   владелец платы кладет его на карту и монтирует на D1;
#   dist/AZBK-TESTS.zip - пакет для владельцев: образ, программы, OWNERS.txt
#   (как ЧИТАЙМЕНЯ.TXT) и снимки ответов модели eCat3 (tests/expected/
#   work-bk0011m-azbk-hwtest-*.png, work-bk0011m-azbk-azpic-*.png).
# Образ, листинги и пакет в git не идут.
#
#   py tools/azbk-hwtest/build.py
import os
import shutil
import struct
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
ANDOS = os.path.join(ROOT, 'deploy', 'software', 'bk', 'andos330.img')
PROGRAMS = (('azhwt', 'AZHWT'), ('azpic', 'AZPIC'))


def fat12_set(fat, i, v):
    o = i * 3 // 2
    if i & 1:
        fat[o] = (fat[o] & 0x0F) | ((v & 0x0F) << 4)
        fat[o + 1] = (v >> 4) & 0xFF
    else:
        fat[o] = v & 0xFF
        fat[o + 1] = (fat[o + 1] & 0xF0) | ((v >> 8) & 0x0F)


def andos_disk(files, img_path, label):
    """Пустой диск ANDOS с загрузочным сектором andos330.img и файлами
    [(имя 8.3 без точки, программа БК .BIN)]: корень (байт 21 записи - 0),
    адрес загрузки - в поле времени, как у ANDOS"""
    src = open(ANDOS, 'rb').read()
    bps = struct.unpack_from('<H', src, 11)[0]
    spc = src[13]
    res = struct.unpack_from('<H', src, 14)[0]
    fats = src[16]
    root = struct.unpack_from('<H', src, 17)[0]
    total = struct.unpack_from('<H', src, 19)[0]
    spf = struct.unpack_from('<H', src, 22)[0]
    img = bytearray(total * bps)
    img[0:bps] = src[0:bps]
    fat = bytearray(spf * bps)
    fat12_set(fat, 0, 0xF00 | src[21])
    fat12_set(fat, 1, 0xFFF)
    ro = (res + fats * spf) * bps
    first_data = (ro + root * 32 + bps - 1) // bps * bps
    cluster_bytes = spc * bps
    img[ro:ro + 11] = label.encode('ascii').ljust(11)[:11]
    img[ro + 11] = 0x08
    entry = ro + 32
    next_cluster = 2
    for name, bin_path in files:
        data = open(bin_path, 'rb').read()
        load, length = struct.unpack_from('<HH', data, 0)
        body = data[4:4 + length]
        n = (len(body) + cluster_bytes - 1) // cluster_bytes
        for k in range(n):
            fat12_set(fat, next_cluster + k, (next_cluster + k + 1) if k + 1 < n else 0xFFF)
        img[entry:entry + 11] = name.encode('ascii').ljust(11)[:11]
        img[entry + 11] = 0x00
        img[entry + 21] = 0
        struct.pack_into('<H', img, entry + 22, load)
        struct.pack_into('<H', img, entry + 26, next_cluster)
        struct.pack_into('<I', img, entry + 28, len(body))
        o = first_data + (next_cluster - 2) * cluster_bytes
        img[o:o + len(body)] = body
        next_cluster += n
        entry += 32
    for k in range(fats):
        o = (res + k * spf) * bps
        img[o:o + len(fat)] = fat
    open(img_path, 'wb').write(img)


def main():
    bins = []
    for src, name in PROGRAMS:
        out = os.path.join(HERE, name + '.BIN')
        r = subprocess.run([sys.executable, '-I', os.path.join(HERE, 'asm11.py'),
                            os.path.join(HERE, src + '.mac'), out, os.path.join(HERE, name + '.lst')])
        if r.returncode:
            sys.exit(r.returncode)
        shutil.copyfile(out, os.path.join(ROOT, 'tests', 'files', src + '.bin'))
        bins.append((name, out))
    if not os.path.exists(ANDOS):
        print('no', ANDOS, '- AZTEST.IMG and the package are not made')
        return
    img = os.path.join(HERE, 'AZTEST.IMG')
    andos_disk(bins, img, 'AZBKTEST')
    print(img)
    dist = os.path.join(HERE, 'dist')
    os.makedirs(dist, exist_ok=True)
    pkg = os.path.join(dist, 'AZBK-TESTS.zip')
    expected = os.path.join(ROOT, 'tests', 'expected')
    with zipfile.ZipFile(pkg, 'w', zipfile.ZIP_DEFLATED) as z:
        z.write(img, 'AZTEST.IMG')
        for name, path in bins:
            z.write(path, name + '.BIN')
        # Блокнот Windows: CRLF и метка UTF-8
        text = open(os.path.join(HERE, 'OWNERS.txt'), encoding='utf-8').read().replace('\n', '\r\n')
        z.writestr('ЧИТАЙМЕНЯ.TXT', ('﻿' + text).encode('utf-8'))
        for f in sorted(os.listdir(expected)):
            # Снимок 4 пробы AZHWT - ANDOS после выхода, он не нужен
            if f.startswith(('work-bk0011m-azbk-hwtest-', 'work-bk0011m-azbk-azpic-')) and f.endswith('.png') \
                    and f != 'work-bk0011m-azbk-hwtest-4.png':
                short = f.replace('work-bk0011m-azbk-', 'eCat3-')
                z.write(os.path.join(expected, f), 'eCat3/' + short)
    print(pkg)


if __name__ == '__main__':
    main()
