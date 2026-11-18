# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
#
# Собирает пробы платы AZBK:
#   AZHWT.BIN, AZPIC.BIN, AZIRQ.BIN (и листинги) из azhwt.mac, azpic.mac,
#   azirq.mac; их копии для тестов - tests/files/azhwt.bin, azpic.bin,
#   azirq.bin;
#   AZTEST.IMG - загрузочный диск ANDOS: andos330.img поставки целиком и
#   в его корне файлы AZHWT, AZPIC и AZIRQ, адрес загрузки 01000. Владелец платы
#   кладет его на карту и монтирует на D1 (на D0 у него ANDOS) или на D0
#   вместо своего диска. Загрузочный сектор ANDOS берет систему с кластера 2:
#   диск только с программами на D0 запускал вместо ANDOS.SYS код AZHWT;
#   dist/AZBK-TESTS.zip - пакет для владельцев: образ, программы, OWNERS.txt
#   (как ЧИТАЙМЕНЯ.TXT) и снимки ответов модели eCat3 (tests/expected/
#   work-bk0011m-azbk-hwtest-*.png, work-bk0011m-azbk-azpic-*.png,
#   work-bk0011m-azbk-azirq.png).
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
PROGRAMS = (('azhwt', 'AZHWT'), ('azpic', 'AZPIC'), ('azirq', 'AZIRQ'))


def fat12_get(fat, i):
    o = i * 3 // 2
    v = fat[o] | (fat[o + 1] << 8)
    return (v >> 4) if i & 1 else (v & 0xFFF)


def fat12_set(fat, i, v):
    o = i * 3 // 2
    if i & 1:
        fat[o] = (fat[o] & 0x0F) | ((v & 0x0F) << 4)
        fat[o + 1] = (v >> 4) & 0xFF
    else:
        fat[o] = v & 0xFF
        fat[o + 1] = (fat[o + 1] & 0xF0) | ((v >> 8) & 0x0F)


def andos_disk(files, img_path):
    """Диск ANDOS поставки (andos330.img) и в его корне файлы [(имя 8.3 без
    точки, программа БК .BIN)]: записи - в свободные места корня после
    последней (байт 21, каталог ANDOS, - 0), данные - в свободные кластеры,
    адрес загрузки - в поле времени, как у ANDOS"""
    img = bytearray(open(ANDOS, 'rb').read())
    bps = struct.unpack_from('<H', img, 11)[0]
    spc = img[13]
    res = struct.unpack_from('<H', img, 14)[0]
    fats = img[16]
    root = struct.unpack_from('<H', img, 17)[0]
    total = struct.unpack_from('<H', img, 19)[0]
    spf = struct.unpack_from('<H', img, 22)[0]
    fat = bytearray(img[res * bps:(res + spf) * bps])
    ro = (res + fats * spf) * bps
    first_data = (ro + root * 32 + bps - 1) // bps * bps
    cluster_bytes = spc * bps
    clusters = (total * bps - first_data) // cluster_bytes
    free = [c for c in range(2, clusters + 2) if fat12_get(fat, c) == 0]
    slots = [k for k in range(root) if img[ro + k * 32] in (0x00, 0xE5)]
    # После последней занятой записи: ANDOS кончает каталог на первой нулевой
    used = max([k for k in range(root) if img[ro + k * 32] not in (0x00, 0xE5)] + [-1])
    slots = [k for k in slots if k > used]
    for name, bin_path in files:
        data = open(bin_path, 'rb').read()
        load, length = struct.unpack_from('<HH', data, 0)
        body = data[4:4 + length]
        n = (len(body) + cluster_bytes - 1) // cluster_bytes
        if n > len(free) or not slots:
            sys.exit('no room for %s on %s' % (name, ANDOS))
        chain, free = free[:n], free[n:]
        for k, c in enumerate(chain):
            fat12_set(fat, c, chain[k + 1] if k + 1 < n else 0xFFF)
            o = first_data + (c - 2) * cluster_bytes
            part = body[k * cluster_bytes:(k + 1) * cluster_bytes]
            img[o:o + cluster_bytes] = part.ljust(cluster_bytes, bytes(1))
        entry = ro + slots.pop(0) * 32
        img[entry:entry + 32] = bytes(32)
        img[entry:entry + 11] = name.encode('ascii').ljust(11)[:11]
        struct.pack_into('<H', img, entry + 22, load)
        struct.pack_into('<H', img, entry + 26, chain[0])
        struct.pack_into('<I', img, entry + 28, len(body))
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
    andos_disk(bins, img)
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
            if f.startswith(('work-bk0011m-azbk-hwtest-', 'work-bk0011m-azbk-azpic-', 'work-bk0011m-azbk-azirq')) \
                    and f.endswith('.png') and f != 'work-bk0011m-azbk-hwtest-4.png':
                short = f.replace('work-bk0011m-azbk-', 'eCat3-')
                z.write(os.path.join(expected, f), 'eCat3/' + short)
    print(pkg)


if __name__ == '__main__':
    main()
