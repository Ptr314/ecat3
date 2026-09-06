# Достает CPUTEST2 из образа ANDOS (deploy/software/bk/andos.img).
#
# Диск размечен под FAT12, но каталог у него плоский: подкаталоги ANDOS -
# это записи с атрибутом метки тома, а файлы идут следом за своей меткой.
# Нужный нам файл лежит в CPUTestS и грузится по адресу 030000 без заголовка.
#
#   py tests/files/extract_cputest2.py

import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
IMAGE = os.path.join(HERE, '..', '..', 'deploy', 'software', 'bk', 'andos.img')
NAME = 'CPUTEST2'

d = open(IMAGE, 'rb').read()

bytes_per_sector = struct.unpack('<H', d[11:13])[0]
sectors_per_cluster = d[13]
reserved = struct.unpack('<H', d[14:16])[0]
fat_count = d[16]
root_entries = struct.unpack('<H', d[17:19])[0]
sectors_per_fat = struct.unpack('<H', d[22:24])[0]

fat_start = reserved * bytes_per_sector
fat = d[fat_start:fat_start + sectors_per_fat * bytes_per_sector]
root_start = (reserved + fat_count * sectors_per_fat) * bytes_per_sector
root_size = root_entries * 32
data_start = root_start + root_size
cluster_size = sectors_per_cluster * bytes_per_sector


def next_cluster(n):
    b = (n * 3) // 2
    v = fat[b] | (fat[b + 1] << 8)
    return (v >> 4) if n & 1 else (v & 0xfff)


def read_file(cluster, size):
    out = b''
    while 2 <= cluster < 0xff0 and len(out) < size:
        off = data_start + (cluster - 2) * cluster_size
        out += d[off:off + cluster_size]
        cluster = next_cluster(cluster)
    return out[:size]


for i in range(root_start, root_start + root_size, 32):
    e = d[i:i + 32]
    if e[0] in (0, 0xe5) or e[11] != 0:
        continue
    name = e[0:8].decode('ascii').rstrip() + e[8:11].decode('ascii').rstrip()
    if name != NAME:
        continue
    cluster = struct.unpack('<H', e[26:28])[0]
    size = struct.unpack('<I', e[28:32])[0]
    open(os.path.join(HERE, NAME), 'wb').write(read_file(cluster, size))
    print('%s: %d bytes' % (NAME, size))
    break
else:
    raise SystemExit('%s not found in %s' % (NAME, IMAGE))
