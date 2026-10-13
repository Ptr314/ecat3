# Диск RT-11 с тестом мыши УК-НЦ MOUTST (регистр 177400 ПП) с диска
# moutst.dsk автора протокола (github.com/Ptr314/ecat3/issues/62). Образ
# обрезан по последнему занятому блоку: эмулятор дополняет его нулями до
# размера дискеты.
#
#   py make_disk.py      -> mouse.dsk рядом
import os, struct

HERE = os.path.dirname(os.path.abspath(__file__))
FILES = ['MOUTST.SAV']
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
    name, ext = fn.split('.')
    struct.pack_into('<7H', seg, p, 0o2000, rad50(name[:3]), rad50(name[3:6]), rad50(ext), n, 0, 0)
    p += 14
    img[blk * 512:blk * 512 + len(data)] = data
    blk += n
struct.pack_into('<7H', seg, p, 0o1000, rad50('EMP'), rad50('TY'), rad50('FIL'), TOTAL - blk, 0, 0)
p += 14
struct.pack_into('<H', seg, p, 0o4000)                  # конец сегмента
img[6 * 512:8 * 512] = seg

open(os.path.join(HERE, 'mouse.dsk'), 'wb').write(img[:blk * 512])
print('mouse.dsk:', blk, 'blocks')
