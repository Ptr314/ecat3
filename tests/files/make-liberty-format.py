# Программа для disk-pravetz-8a-liberty-format: разметка цилиндра 3 диска
# Liberty Drive командой ВГ93 Write Track (обе стороны, 10 секторов по 512
# байт) и чтение двух секторов обратно командой Read Sector.
#
# Грузится в $1000. Порты платы в слоте 5: $C0D0-$C0D3 - ВГ93, $C0D4 -
# разряд 7 = DRQ, $C0D8 - защелка (разряд 0 - дисковод, 1 - сторона,
# 6 - «драйвер работает»). Сектор s стороны h заполняется байтом
# $A0 + h*$10 + s, последний байт сектора - $5A. Прочитанное ложится в
# $6000 (сторона 1, сектор 10) и $6200 (сторона 0, сектор 1), состояние
# ВГ93 в конце - в $6400.
#
#   python make-liberty-format.py  ->  liberty-format.bin
import os

ORG = 0x1000
CYL = 3
code = bytearray()
labels = {}
fixups = []         # (offset, label, kind) kind: 'abs', 'lo', 'hi', 'rel'

def here():
    return ORG + len(code)

def label(name):
    labels[name] = here()

def emit(*b):
    code.extend(b)

def ref(name, kind):
    fixups.append((len(code), name, kind))
    code.extend(b'\0\0' if kind == 'abs' else b'\0')

def lda_imm(v): emit(0xA9, v)
def sta(a): emit(0x8D, a & 0xFF, a >> 8)
def lda(a): emit(0xAD, a & 0xFF, a >> 8)
def jsr(name): emit(0x20); ref(name, 'abs')
def jmp(name): emit(0x4C); ref(name, 'abs')
def bmi(name): emit(0x30); ref(name, 'rel')
def bcs(name): emit(0xB0); ref(name, 'rel')
def bne(name): emit(0xD0); ref(name, 'rel')
def ptr(name):
    emit(0xA9); ref(name, 'lo'); emit(0x85, 0xFA)
    emit(0xA9); ref(name, 'hi'); emit(0x85, 0xFB)
def ptr_abs(a):
    emit(0xA9, a & 0xFF, 0x85, 0xFA, 0xA9, a >> 8, 0x85, 0xFB)

WD, DRQ, LATCH = 0xC0D0, 0xC0D4, 0xC0D8

# Начало: дисковод 0, сторона 0, Restore и Seek на цилиндр 3
lda_imm(0x40); sta(LATCH)
lda_imm(0x00); sta(WD); jsr('wait')
lda_imm(CYL); sta(WD + 3); lda_imm(0x10); sta(WD); jsr('wait')
ptr('side0'); jsr('wtrack')
lda_imm(0x42); sta(LATCH)
ptr('side1'); jsr('wtrack')
# Обратно: сторона 1, сектор 10 -> $6000; сторона 0, сектор 1 -> $6200
lda_imm(10); sta(WD + 2); ptr_abs(0x6000); jsr('rsec')
lda_imm(0x40); sta(LATCH)
lda_imm(1); sta(WD + 2); ptr_abs(0x6200); jsr('rsec')
lda(WD); sta(0x6400)
emit(0x60)

label('wait')
lda(WD); emit(0x4A); bcs('wait'); emit(0x60)

# Write Track: байт в каждый DRQ, пока ВГ93 занят
label('wtrack')
lda_imm(0xF0); sta(WD)
label('wl')
lda(DRQ); bmi('wd')
lda(WD); emit(0x4A); bcs('wl'); emit(0x60)
label('wd')
emit(0xA0, 0x00, 0xB1, 0xFA); sta(WD + 3)
emit(0xE6, 0xFA); bne('wl'); emit(0xE6, 0xFB); jmp('wl')

# Read Sector
label('rsec')
lda_imm(0x80); sta(WD)
label('rl')
lda(DRQ); bmi('rd')
lda(WD); emit(0x4A); bcs('rl'); emit(0x60)
label('rd')
emit(0xA0, 0x00); lda(WD + 3); emit(0x91, 0xFA)
emit(0xE6, 0xFA); bne('rl'); emit(0xE6, 0xFB); jmp('rl')

def track(side):
    t = bytearray()
    t += b'\x4E' * 80 + b'\x00' * 12 + b'\xF6' * 3 + b'\xFC' + b'\x4E' * 50
    for s in range(1, 11):
        t += b'\x00' * 12 + b'\xF5' * 3 + bytes([0xFE, CYL, side, s, 0x02, 0xF7])
        t += b'\x4E' * 22 + b'\x00' * 12 + b'\xF5' * 3 + b'\xFB'
        t += bytes([0xA0 + side * 0x10 + s]) * 511 + b'\x5A'
        t += b'\xF7' + b'\x4E' * 24
    assert len(t) <= 6250
    return t + b'\x4E' * (6300 - len(t))

label('side0')
code += track(0)
label('side1')
code += track(1)

for off, name, kind in fixups:
    a = labels[name]
    if kind == 'abs':
        code[off] = a & 0xFF; code[off + 1] = a >> 8
    elif kind == 'lo':
        code[off] = a & 0xFF
    elif kind == 'hi':
        code[off] = a >> 8
    else:
        d = a - (ORG + off + 1)
        assert -128 <= d < 128, name
        code[off] = d & 0xFF

assert ORG + len(code) <= 0x6000
open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'liberty-format.bin'), 'wb').write(code)
