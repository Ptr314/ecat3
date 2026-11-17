# Запись кассеты БК (WAV) для tape-bk-0010-01-wav: файл WAVTEST, 16 байт по
# адресу 002000, в кодировании Монитора (tape_bk.h: пилот, маркер, заголовок,
# данные, контрольная сумма), но звуком, какой снимают с магнитофона:
# синусоидальные полупериоды, постоянная составляющая, шум, лента на 15%
# медленнее, чем пишет БК0010-01. 44100 Гц, 8 бит, моно.
#
# Пилот короче, чем у Монитора (3000 импульсов против 4096): ему хватает 2048
# ровных подряд и 128 на усреднение. Ровных - значит каждый полупериод не
# короче предыдущего больше чем на два оборота цикла (около 30 мкс), поэтому
# шум здесь умеренный, а частота 44100: при 22050 на полупериод приходится
# меньше трех выборок, и фронты между ними гуляют сильнее допуска.
#
#   python make-bk-wav.py                     ->  bk-0010-01-record.wav
#   python make-bk-wav.py --invert out.wav    ->  та же запись в обратной полярности
import math
import os
import random
import struct
import sys

RATE = 44100
UNIT = 1e6 / 9464 * 1.15            # мкс: единица БК0010-01, на 15% длиннее
ADDRESS = 0o2000
NAME = b'WAVTEST'.ljust(16, b' ')
DATA = bytes([0x42, 0x4B, 0x20, 0x57, 0x41, 0x56, 0x00, 0xFF, 0x11, 0x22, 0x33, 0x44, 0x55, 0xAA, 0x01, 0x80])

invert = '--invert' in sys.argv
args = [a for a in sys.argv[1:] if a != '--invert']

halves = []                         # (уровень, единиц)


def pulse(high, low):
    halves.append((1, high))
    halves.append((0, low))


def sync(count):
    for _ in range(count):
        pulse(1, 1)


def marker():
    pulse(4, 4)
    pulse(2, 2)
    pulse(1, 1)


def byte(b):
    for k in range(8):
        if (b >> k) & 1:
            pulse(2, 2)
        else:
            pulse(1, 1)
        pulse(1, 1)                 # разделитель


def checksum(data):
    s = 0
    for b in data:
        s += b
        if s > 0xFFFF:
            s = (s & 0xFFFF) + 1
    return s


sync(3000)
marker()
sync(8)
marker()
for b in struct.pack('<HH', ADDRESS, len(DATA)) + NAME:
    byte(b)
sync(8)
marker()
cs = checksum(DATA)
for b in DATA + struct.pack('<H', cs):
    byte(b)
sync(32)

random.seed(1)
SILENCE = int(RATE * 0.1)
starts = []                         # начало каждого полупериода, с
t = SILENCE / RATE
for level, units in halves:
    starts.append(t)
    t += units * UNIT / 1e6
starts.append(t)
samples = [128] * SILENCE
k = 0
for n in range(SILENCE, int(t * RATE)):
    now = n / RATE
    while starts[k + 1] <= now:
        k += 1
    level, units = halves[k]
    phase = (now - starts[k]) / (starts[k + 1] - starts[k])
    sign = 1 if level != invert else -1
    v = 0.08 + 0.5 * sign * math.sin(math.pi * phase) + random.gauss(0, 0.01)
    samples.append(max(0, min(255, int(128 + 127 * v))))
samples += [128] * SILENCE

data = bytes(samples)
wav = b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVE'
wav += b'fmt ' + struct.pack('<IHHIIHH', 16, 1, 1, RATE, RATE, 1, 8)
wav += b'data' + struct.pack('<I', len(data)) + data
out = args[0] if args else os.path.join(os.path.dirname(os.path.abspath(__file__)), 'bk-0010-01-record.wav')
open(out, 'wb').write(wav)
