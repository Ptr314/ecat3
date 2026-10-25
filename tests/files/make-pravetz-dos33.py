# Диск DOS 3.3 на 140 Кб для disk-pravetz-8a-dos33: дорожки 0-2 (DOS) и
# файл HELLO из диска Правеца с agatcomp.ru ("П8ру 01.dsk", NOSORT/RUSKI01),
# остальные файлы выброшены, их секторы обнулены и освобождены в VTOC.
#
#   python make-pravetz-dos33.py "П8ру 01.dsk"  ->  pravetz-dos33.dsk
import os
import sys

SECTOR = 256
src = bytearray(open(sys.argv[1], 'rb').read())
assert len(src) == 35 * 16 * SECTOR

def sec(img, t, s):
    o = (t * 16 + s) * SECTOR
    return img[o:o + SECTOR]

out = bytearray(len(src))
def put(t, s, data):
    o = (t * 16 + s) * SECTOR
    out[o:o + SECTOR] = data

for t in range(3):
    for s in range(16):
        put(t, s, sec(src, t, s))

used = set((t, s) for t in range(3) for s in range(16))
vtoc = bytearray(sec(src, 17, 0))
used.add((17, 0))

# Каталог: цепочка секторов дорожки 17, в первом остается только HELLO
t, s = vtoc[1], vtoc[2]
first = True
hello = None
while t:
    c = bytearray(sec(src, t, s))
    used.add((t, s))
    if first:
        for e in range(7):
            o = 0x0B + e * 35
            name = bytes(b & 0x7F for b in c[o + 3:o + 33]).rstrip()
            if hello is None and c[o] not in (0, 0xFF) and name == b'HELLO':
                hello = bytes(c[o:o + 35])
        c[0x0B:0x0B + 7 * 35] = bytes(7 * 35)
        c[0x0B:0x0B + 35] = hello
        first = False
    else:
        c[0x0B:0x0B + 7 * 35] = bytes(7 * 35)
    put(t, s, c)
    t, s = c[1], c[2]
assert hello is not None

# HELLO: список секторов и данные
t, s = hello[0], hello[1]
while t:
    ts = sec(src, t, s)
    put(t, s, ts)
    used.add((t, s))
    for k in range(122):
        dt, ds = ts[0x0C + 2 * k], ts[0x0D + 2 * k]
        if dt:
            put(dt, ds, sec(src, dt, ds))
            used.add((dt, ds))
    t, s = ts[1], ts[2]

# Битовая карта VTOC: 4 байта на дорожку с $38, разряд 1 = свободен,
# сектор 15 - старший разряд первого байта
for tr in range(35):
    bits = 0
    for sc in range(16):
        if (tr, sc) not in used:
            bits |= 1 << sc
    o = 0x38 + tr * 4
    vtoc[o:o + 4] = bytes([bits >> 8, bits & 0xFF, 0, 0])
put(17, 0, vtoc)

open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'pravetz-dos33.dsk'), 'wb').write(out)
