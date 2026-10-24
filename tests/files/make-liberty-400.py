# Диск Liberty Drive на 400 Кб для disk-pravetz-8a-liberty: 800 блоков по 512
# байт, цилиндр за цилиндром, стороны чередуются (блок = цилиндр*20 +
# сторона*10 + сектор-1). Блок 0 - загрузчик: через драйвер ProDOS в ПЗУ
# платы ($C50A, слот 5) читает блоки 2, 15, 25 и 799 и печатает их первые
# байты - метки A, B, C, D. Так проверяется путь блок -> цилиндр, сторона,
# сектор, включая последний сектор второй стороны последнего цилиндра.
#
#   python make-liberty-400.py  ->  liberty-400.img
import os

BLOCKS = [2, 15, 25, 799]
MARKS = b'ABCD'

code = bytearray()
def emit(*b):
    code.extend(b)

ORG = 0x0801
emit(0xA9, 0x01, 0x85, 0x42)            # LDA #1; STA $42      команда: чтение
emit(0xA9, 0x50, 0x85, 0x43)            # LDA #$50; STA $43    слот 5, дисковод 1
emit(0xA9, 0x00, 0x85, 0x44)            # LDA #0; STA $44      буфер $1000
emit(0xA9, 0x10, 0x85, 0x45)            # LDA #$10; STA $45
emit(0xA9, 0x00, 0x85, 0x06)            # LDA #0; STA $06      номер в списке
loop = ORG + len(code)
emit(0xA6, 0x06)                        # LDX $06
emit(0xBD, 0x00, 0x00)                  # LDA lo,X   (адрес заполним ниже)
lo_fix = len(code) - 2
emit(0x85, 0x46)                        # STA $46
emit(0xBD, 0x00, 0x00)                  # LDA hi,X
hi_fix = len(code) - 2
emit(0x85, 0x47)                        # STA $47
emit(0x20, 0x0A, 0xC5)                  # JSR $C50A
emit(0xAD, 0x00, 0x10)                  # LDA $1000
emit(0x20, 0xF0, 0xFD)                  # JSR COUT1: после PR#5 CSW еще указывает на плату
emit(0xE6, 0x06)                        # INC $06
emit(0xA5, 0x06)                        # LDA $06
emit(0xC9, len(BLOCKS))                 # CMP #4
emit(0xD0, (loop - (ORG + len(code) + 2)) & 0xFF)   # BNE loop
halt = ORG + len(code)
emit(0x4C, halt & 0xFF, halt >> 8)      # JMP *
lo = ORG + len(code)
code.extend(b & 0xFF for b in BLOCKS)
hi = ORG + len(code)
code.extend(b >> 8 for b in BLOCKS)
code[lo_fix:lo_fix + 2] = bytes([lo & 0xFF, lo >> 8])
code[hi_fix:hi_fix + 2] = bytes([hi & 0xFF, hi >> 8])

disk = bytearray(800 * 512)
disk[0] = 0x01                          # загрузчик ProDOS: число блоков
disk[1:1 + len(code)] = code
for b, m in zip(BLOCKS, MARKS):
    disk[b * 512] = m | 0x80

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'liberty-400.img')
open(out, 'wb').write(disk)
print(out, len(disk))
