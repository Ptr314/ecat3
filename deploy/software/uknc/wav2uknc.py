#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
"""
Конвертер записи магнитофона УК-НЦ (WAV) в образ ленты eCat3 (.uknc.tap).

    python wav2uknc.py запись.wav [образ.uknc.tap]

Сигнал - полупериоды двух длин, как их пишет драйвер ПЗУ периферийного
процессора: бит 0 - один длинный период (два полупериода по 416 мкс), бит 1 -
два коротких (четыре по 208 мкс). Байт - бит 0, восемь битов младшим вперёд,
два бита 1. Файл на ленте:

    пилот-тон (8000 битов 1)
    имя - 16 байтов, длина в словах, адрес загрузки
    пилот-тон (2000 битов 1)
    данные и контрольная сумма (сумма слов с переносом по кругу)

Программа, записанная из RT-11 (например, sav2wav из ukncbtl-utils), лежит на
ленте двумя такими файлами: загрузчиком в 256 слов по адресу 0 и остальной
частью по адресу 1000.

Образ .uknc.tap - те же файлы подряд, без сигнала и без контрольной суммы:
16 байтов имени, длина, адрес, данные. Его читает магнитофон eCat3.

Скорость ленты не обязана быть точной: короткий полупериод оценивается
заново по ходу чтения, как это делает и ПЗУ по пилот-тону.
"""

import argparse
import array
import os
import sys
import wave

NAME_SIZE = 16

# Столько битов 1 подряд между байтами - начало новой записи (пилот-тон).
# Внутри записи байты идут вплотную, между ними только два стоповых бита
RECORD_GAP = 256


def read_wav(path):
    """Отсчёты моно-сигнала в виде списка чисел и частота дискретизации."""
    with wave.open(path, 'rb') as w:
        channels = w.getnchannels()
        width = w.getsampwidth()
        rate = w.getframerate()
        raw = w.readframes(w.getnframes())

    if width == 1:
        data = [b - 128 for b in raw]                   # 8 бит без знака
    elif width == 2:
        data = array.array('h')
        data.frombytes(raw[:len(raw) // 2 * 2])
        if sys.byteorder == 'big':
            data.byteswap()
    elif width == 3:
        data = [int.from_bytes(raw[i:i + 3], 'little', signed=True)
                for i in range(0, len(raw) - 2, 3)]
    elif width == 4:
        data = array.array('i')
        data.frombytes(raw[:len(raw) // 4 * 4])
        if sys.byteorder == 'big':
            data.byteswap()
    else:
        raise ValueError('неподдерживаемая разрядность WAV: %d байт' % width)

    if channels > 1:
        data = [sum(data[i:i + channels]) for i in range(0, len(data) - channels + 1, channels)]
    return list(data), rate


def half_periods(samples, rate):
    """Длительности полупериодов в микросекундах.

    Постоянная составляющая записи не обязана быть нулём (в оцифровках с
    кассет она гуляет), поэтому уровень сравнивается со скользящим средним
    примерно за три периода бита. Переход засчитывается с гистерезисом, чтобы
    шум около нуля не давал лишних фронтов, а момент перехода уточняется
    линейной интерполяцией - на 22 кГц короткий полупериод занимает всего
    четыре-пять отсчётов, и без неё длины получаются слишком грубыми."""
    n = len(samples)
    window = max(8, int(rate * 0.0025))                 # 2,5 мс
    prefix = [0] * (n + 1)
    for i, v in enumerate(samples):
        prefix[i + 1] = prefix[i] + v

    peak = max(abs(v) for v in samples) or 1
    hysteresis = peak * 0.06

    edges = []
    state = 0
    prev = 0.0
    half = window // 2
    for i in range(n):
        lo = i - half if i >= half else 0
        hi = i + half if i + half < n else n
        x = samples[i] - (prefix[hi] - prefix[lo]) / (hi - lo)
        if state <= 0 and x > hysteresis:
            if state < 0:
                edges.append(crossing(i, prev, x))
            state = 1
        elif state >= 0 and x < -hysteresis:
            if state > 0:
                edges.append(crossing(i, prev, x))
            state = -1
        prev = x

    scale = 1e6 / rate
    return [(edges[k + 1] - edges[k]) * scale for k in range(len(edges) - 1)]


def crossing(i, prev, x):
    # Точка, где сигнал прошёл через ноль между отсчётами i-1 и i. Гистерезис
    # может сработать не на первом отсчёте после нуля - тогда это оценка
    if prev * x < 0:
        return i - 1 + prev / (prev - x)
    return float(i)


def to_bits(halves):
    """Полупериоды в биты. Как и ПЗУ (130530), бит опознаётся по первому
    полупериоду, остальные пропускаются. Длина короткого полупериода следится
    по ходу: её начальное значение - медиана начала записи, где идёт пилот-тон."""
    if not halves:
        return []
    head = sorted(halves[:2048])
    unit = head[len(head) // 2]

    bits = []
    i = 0
    n = len(halves)
    while i < n:
        h = halves[i]
        if h > unit * 1.5:
            bits.append(0)
            i += 2
        else:
            bits.append(1)
            # Только короткие полупериоды уточняют единицу, и медленно:
            # отдельный искажённый фронт не должен её сбить
            if h > unit * 0.5:
                unit += (h - unit) * 0.02
            i += 4
    return bits


def to_records(bits):
    """Биты в записи: байт начинается с бита 0, длинный ряд единиц перед ним
    означает новую запись."""
    records = []
    ones = RECORD_GAP
    i = 0
    n = len(bits)
    while i < n:
        if bits[i]:
            ones += 1
            i += 1
            continue
        if i + 9 > n:
            break
        b = 0
        for k in range(8):
            b |= bits[i + 1 + k] << k
        if ones >= RECORD_GAP or not records:
            records.append(bytearray())
        records[-1].append(b)
        ones = 0
        i += 9
    return records


def word(data, pos):
    lo = data[pos] if pos < len(data) else 0
    hi = data[pos + 1] if pos + 1 < len(data) else 0
    return lo | (hi << 8)


def checksum(data, words):
    s = 0
    for k in range(words):
        s += word(data, 2 * k)
        s = (s & 0xFFFF) + (s >> 16)
    return s


def show_name(raw):
    # Имя пишет программа, а не ПЗУ: у RT-11 и Бейсика это КОИ-8
    try:
        text = bytes(raw).decode('koi8-r')
    except UnicodeDecodeError:
        text = ''
    if all(c.isprintable() for c in text):
        return '"%s"' % text
    return ' '.join('%03o' % b for b in raw)


def to_image(records, log):
    """Записи в образ: заголовок, затем его данные без контрольной суммы."""
    image = bytearray()
    errors = 0
    r = 0
    files = 0
    while r < len(records):
        header = records[r]
        r += 1
        if len(header) < NAME_SIZE:
            # Щелчок включения магнитофона перед пилот-тоном даёт байт-другой:
            # заголовком это быть не может
            continue
        files += 1
        name = header[:NAME_SIZE]
        length = word(header, NAME_SIZE) if len(header) >= NAME_SIZE + 4 else 0
        address = word(header, NAME_SIZE + 2) if len(header) >= NAME_SIZE + 4 else 0
        image += name
        image += bytes((length & 0xFF, length >> 8, address & 0xFF, address >> 8))

        status = ''
        if length:
            data = records[r] if r < len(records) else bytearray()
            r += 1
            got = len(data) // 2
            body = bytearray(data[:2 * length])
            body += bytes(2 * length - len(body))
            if got < length + 1:
                status = 'ОБРЫВ: прочитано %d слов из %d' % (max(0, got - 1), length)
                errors += 1
            else:
                expected = word(data, 2 * length)
                actual = checksum(body, length)
                if expected == actual:
                    status = 'сумма %06o верна' % actual
                else:
                    status = 'СУММА НЕ СОШЛАСЬ: на ленте %06o, посчитано %06o' % (expected, actual)
                    errors += 1
            image += body
        log('  файл %d: имя %s, длина %d слов (%o), адрес %06o%s' %
            (files, show_name(name), length, length, address,
             (', ' + status) if status else ''))
    return image, files, errors


def main():
    parser = argparse.ArgumentParser(
        description='Конвертер записи магнитофона УК-НЦ (WAV) в образ ленты eCat3 (.uknc.tap)')
    parser.add_argument('wav', help='запись магнитофона в формате WAV')
    parser.add_argument('output', nargs='?',
                        help='образ ленты; по умолчанию - имя записи с расширением .uknc.tap')
    args = parser.parse_args()

    output = args.output or os.path.splitext(args.wav)[0] + '.uknc.tap'

    samples, rate = read_wav(args.wav)
    print('%s: %d Гц, %.1f с' % (args.wav, rate, len(samples) / rate))
    if rate < 16000:
        print('  частота дискретизации мала: короткий полупериод занимает меньше трёх отсчётов')

    halves = half_periods(samples, rate)
    bits = to_bits(halves)
    records = to_records(bits)
    image, files, errors = to_image(records, print)

    if files == 0:
        print('Файлов на ленте не найдено')
        return 1

    with open(output, 'wb') as f:
        f.write(image)
    print('Записано %s: %d байт, файлов %d%s' %
          (output, len(image), files, (', ошибок %d' % errors) if errors else ''))
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
