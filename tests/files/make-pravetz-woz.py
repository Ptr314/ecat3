# Диск WOZ для disk-pravetz-8a-woz и disk-pravetz-8a-woz-halftrack: биты
# дорожек pravetz-dos33.dsk, какими их читает Disk II (сохранены самим
# эмулятором: fdd.save в .woz), из них оставлены дорожки 0-2 (DOS) и 16-19
# (каталог и место под новые файлы), остальные позиции головки пустые. На
# полудорожке 0.5 (четверть 2) - своя короткая дорожка: синхробайты и метка
# D5 AA B7, за ней байт A5.
#
#   python make-pravetz-woz.py <eCat3-headless.exe>  ->  pravetz-dos33.woz
import os
import struct
import subprocess
import sys
import zlib

here = os.path.dirname(os.path.abspath(__file__))
deploy = os.path.join(here, '..', '..', 'deploy')
full = os.path.join(here, 'pravetz-dos33-full.woz')
script = os.path.join(here, 'make-pravetz-woz.ecat')
with open(script, 'w') as f:
    f.write('MACHINE "pravetz/Pravetz-8A.cfg"\n'
            'COMMAND fdd0.load("../tests/files/pravetz-dos33.dsk")\n'
            'COMMAND fdd0.save("%s")\nEXIT\n' % full.replace('\\', '/'))
subprocess.check_call([sys.argv[1], '--no-sound', script], cwd=deploy)
os.remove(script)

data = open(full, 'rb').read()
os.remove(full)
assert data[:8] == b'WOZ2\xff\x0a\x0d\x0a'
chunks = {}
at = 12
while at + 8 <= len(data):
    cid, size = data[at:at + 4], struct.unpack_from('<I', data, at + 4)[0]
    chunks[cid] = (at + 8, size)
    at += 8 + size
tmap = data[chunks[b'TMAP'][0]:chunks[b'TMAP'][0] + 160]
trks = chunks[b'TRKS'][0]

def track_bits(i):
    start, blocks, bits = struct.unpack_from('<HHI', data, trks + i * 8)
    raw = data[start * 512:(start + blocks) * 512]
    return [(raw[b >> 3] >> (7 - (b & 7))) & 1 for b in range(bits)]

def nibbles(seq):
    out = []
    for b in seq:
        out += [(b >> k) & 1 for k in range(7, -1, -1)]
    return out

keep = [0, 1, 2, 16, 17, 18, 19]
tracks = [track_bits(tmap[t * 4]) for t in keep]
newmap = [0xFF] * 160
for i, t in enumerate(keep):
    for q in (t * 4 - 1, t * 4, t * 4 + 1):
        if q >= 0:
            newmap[q] = i
# Полудорожка 0.5: 64 синхробайта по 10 бит, метка, байт A5, снова синхро
sync = nibbles([0xFF]) + [0, 0]
marker = sync * 64 + nibbles([0xD5, 0xAA, 0xB7, 0xA5]) + sync * 16
newmap[2] = len(tracks)
tracks.append(marker * 40)

blocks = [(len(b) + 4095) // 4096 for b in tracks]
size = (3 + sum(blocks)) * 512
out = bytearray(size)
out[0:8] = b'WOZ2\xff\x0a\x0d\x0a'
out[12:80] = data[12:80]
struct.pack_into('<H', out, 64, max(blocks))
out[80:88] = b'TMAP' + struct.pack('<I', 160)
out[88:248] = bytes(newmap)
out[248:256] = b'TRKS' + struct.pack('<I', size - 256)
block = 3
for i, b in enumerate(tracks):
    struct.pack_into('<HHI', out, 256 + i * 8, block, blocks[i], len(b))
    for k, v in enumerate(b):
        if v:
            out[block * 512 + (k >> 3)] |= 0x80 >> (k & 7)
    block += blocks[i]
struct.pack_into('<I', out, 8, zlib.crc32(bytes(out[12:])) & 0xFFFFFFFF)
open(os.path.join(here, 'pravetz-dos33.woz'), 'wb').write(out)
