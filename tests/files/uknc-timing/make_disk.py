# Диск RT-11 с тестами времени команд УК-НЦ: TSSPD v0.1i (Titus) и тесты
# Patron (MOV, MOVB, CMP, ADD, OP1, JSR, IRQ v1.2) из темы zx-pk.ru 14702. Образ
# обрезан по последнему занятому блоку: эмулятор дополняет его нулями до
# размера дискеты.
#
#   py make_disk.py      -> timing.dsk рядом
#   py make_disk.py OUT.dsk FILE... -> такой же диск с другими файлами
#                        (тест процессора: ../uknc-cputest; тесты Alex_K
#                        с UTEST.DSK: py make_disk.py alexk.dsk alexk/CMOV.SAV
#                        alexk/PMOV.SAV alexk/CBR.SAV alexk/PBR.SAV
#                        alexk/CRDWR.SAV alexk/PRDWR.SAV alexk/C030.SAV
#                        alexk/P030.SAV alexk/CHLT.SAV alexk/PHLT.SAV
#                        alexk/CHINT.SAV; тест клавиатуры SCAN оттуда же:
#                        py make_disk.py alexk-scan.dsk alexk/SCAN.SAV)
import os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'timing.dsk')
FILES = ['TSSPD.SAV', 'MOV.SAV', 'MOVB.SAV', 'CMP.SAV', 'ADD.SAV', 'OP1.SAV', 'JSR.SAV', 'IRQ.SAV']
if len(sys.argv) > 2:
    OUT = os.path.abspath(sys.argv[1])
    FILES = [os.path.abspath(f) for f in sys.argv[2:]]
TOTAL = 1600                    # блоков на дискете МХ: 80 дорожек, 2 стороны
R50 = ' ABCDEFGHIJKLMNOPQRSTUVWXYZ$.%0123456789'

def rad50(s):
    v = 0
    for c in (s + '   ')[:3]:
        v = v * 40 + R50.index(c)
    return v

img = bytearray(TOTAL * 512)
# Блок 1 - домашний: кластер, первый блок каталога, версия, метки
hb = bytearray(512)
struct.pack_into('<H', hb, 0o722, 1)
struct.pack_into('<H', hb, 0o724, 6)
struct.pack_into('<H', hb, 0o726, 0o107123)            # V3A в RAD50
hb[0o730:0o744] = b'RT11A       '
hb[0o744:0o760] = b'            '
hb[0o760:0o774] = b'DECRT11A    '
struct.pack_into('<H', hb, 0o776, sum(struct.unpack_from('<255H', hb, 0)) & 0xFFFF)
img[512:1024] = hb

# Каталог: четыре сегмента с блока 6, файлы - сразу за ними
NSEG = 4
first = 6 + NSEG * 2
seg = bytearray(1024)
struct.pack_into('<5H', seg, 0, NSEG, 0, 1, 0, first)
p = 10
blk = first
for fn in FILES:
    data = open(os.path.join(HERE, fn), 'rb').read()
    n = (len(data) + 511) // 512
    name, ext = os.path.basename(fn).split('.')
    struct.pack_into('<7H', seg, p, 0o2000, rad50(name[:3]), rad50(name[3:6]), rad50(ext), n, 0, 0)
    p += 14
    img[blk * 512:blk * 512 + len(data)] = data
    blk += n
struct.pack_into('<7H', seg, p, 0o1000, rad50('EMP'), rad50('TY'), rad50('FIL'), TOTAL - blk, 0, 0)
p += 14
struct.pack_into('<H', seg, p, 0o4000)                  # конец сегмента
img[6 * 512:8 * 512] = seg

open(OUT, 'wb').write(img[:blk * 512])
print(os.path.basename(OUT) + ':', blk, 'blocks')
