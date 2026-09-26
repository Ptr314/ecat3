# -*- coding: utf-8 -*-
"""Крошечные ленты ZX Spectrum для регрессии Арго.

Настоящую кассету машины набор использовать не может: она 215 Кбайт и лежит вне
git, то есть ПЗУ Спектрума в тестах нет. Зато сам сигнал проверить можно, и для
этого хватает ленты из двух блоков - заголовка и шестнадцати байт данных.

Блок .tap: признак, данные, контрольная сумма (XOR всего предыдущего).
Признак $00 - заголовок, $FF - данные. Заголовок несет тип (3 - CODE), имя из
десяти знаков, длину и два параметра.

Пауза между блоками взята 200 мс вместо обычной тысячи: на звук это не влияет,
а эмулируемого времени тест тратит на две с лишним секунды меньше.
"""
import io
import os

NAME = b'ZXTEST    '
DATA = bytes(range(1, 17))          # 16 байт: $01..$10
ADDR = 0x9000
PAUSE_MS = 200


def block(flag, payload):
    b = bytes([flag]) + payload
    s = 0
    for x in b:
        s ^= x
    return b + bytes([s])


def header():
    return block(0x00, bytes([3]) + NAME
                 + bytes([len(DATA) & 0xFF, len(DATA) >> 8])
                 + bytes([ADDR & 0xFF, ADDR >> 8])
                 + bytes([0x00, 0x80]))


def data():
    return block(0xFF, DATA)


def write_tap(path, blocks):
    out = bytearray()
    for b in blocks:
        out += bytes([len(b) & 0xFF, len(b) >> 8]) + b
    io.open(path, 'wb').write(bytes(out))
    print('%-28s %4d байт, блоков %d' % (os.path.basename(path), len(out), len(blocks)))


def write_tzx(path, blocks, extra=b'', pauses=None):
    out = bytearray(b'ZXTape!\x1a\x01\x14')
    # $30 - текстовое описание: его разбор обязан пропускать
    text = b'eCat3 test tape'
    out += bytes([0x30, len(text)]) + text
    for i, b in enumerate(blocks):
        ms = PAUSE_MS if pauses is None else pauses[i]
        out += bytes([0x10, ms & 0xFF, ms >> 8, len(b) & 0xFF, len(b) >> 8]) + b
    out += extra
    io.open(path, 'wb').write(bytes(out))
    print('%-28s %4d байт' % (os.path.basename(path), len(out)))


here = os.path.dirname(os.path.abspath(__file__))
blocks = [header(), data()]
write_tap(os.path.join(here, 'zx-test.tap'), blocks)
write_tzx(os.path.join(here, 'zx-test.tzx'), blocks)
# Лента с блоком, которого мы не умеем: $11 - запись на нестандартной скорости.
# Такую надо отвергать, а не играть до нее и вставать
write_tzx(os.path.join(here, 'zx-bad.tzx'), blocks[:1], extra=bytes([0x11]) + bytes(20))
# Пауза 0 у заголовка: блок данных идет вплотную за ним. Ноль паузы совпадал
# со значением «лента кончилась», и второй блок не звучал вовсе
write_tzx(os.path.join(here, 'zx-nopause.tzx'), blocks, pauses=[0, PAUSE_MS])
