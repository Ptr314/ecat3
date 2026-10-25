# Запись кассеты Правец-82 (WAV) для tape-pravetz-82-wav: одна запись Монитора,
# 16 байт ($0800-$080F) и контрольная сумма, но звуком, какой снимают с
# магнитофона: синусоидальные полупериоды, постоянная составляющая, шум,
# лента на 2% медленнее. 11025 Гц, 8 бит, моно.
#
#   python make-pravetz-wav.py  ->  pravetz-82-record.wav
import math
import os
import random
import struct

RATE = 11025
SLOW = 1.02
DATA = bytes([0x50, 0x52, 0x41, 0x56, 0x45, 0x54, 0x5A, 0x2D, 0x38, 0x32, 0x00, 0xFF, 0x11, 0x22, 0x33, 0x44])

halves = []                         # микросекунды
halves += [650] * int(4.0e6 / 650)  # заголовок 770 Гц, 4 с: READ ждет 3,5
halves += [200, 250]                # синхроимпульс
s = 0xFF
for i, b in enumerate(list(DATA) + [None]):
    if b is None:
        b = s
    else:
        s ^= b
    for k in range(7, -1, -1):
        h = 500 if (b >> k) & 1 else 250
        halves += [h, h]

random.seed(1)
samples = [128] * int(RATE * 0.3)
t = 0.0
sign = 1
for h in halves:
    length = h * SLOW / 1e6
    n0 = int(t * RATE)
    t += length
    n1 = int(t * RATE)
    for n in range(n0, n1):
        phase = (n / RATE - (t - length)) / length
        v = 0.08 + 0.5 * sign * math.sin(math.pi * min(max(phase, 0), 1)) + random.gauss(0, 0.03)
        samples.append(max(0, min(255, int(128 + 127 * v))))
    sign = -sign
samples += [128] * int(RATE * 0.3)

data = bytes(samples)
wav = b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVE'
wav += b'fmt ' + struct.pack('<IHHIIHH', 16, 1, 1, RATE, RATE, 1, 8)
wav += b'data' + struct.pack('<I', len(data)) + data
open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'pravetz-82-record.wav'), 'wb').write(wav)
