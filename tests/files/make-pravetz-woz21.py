# Диск WOZ 2.1 для disk-pravetz-8a-woz21: pravetz-dos33.woz, у которого
# дорожки 0 (загрузчик DOS) и 17 (каталог) записаны потоком (чанк FLUX):
# байт - время до следующей смены потока в тиках по 125 нс, 255 - прибавить и
# читать дальше. Поток - как с настоящего привода: на 1,5% медленнее и с
# дрожанием +-3 тика, а одна длинная пауза - через 255.
#
#   python make-pravetz-woz21.py  ->  pravetz-dos33-flux.woz
import os
import random
import struct
import zlib

here = os.path.dirname(os.path.abspath(__file__))
data = open(os.path.join(here, 'pravetz-dos33.woz'), 'rb').read()
chunks = {}
at = 12
while at + 8 <= len(data):
    cid, size = data[at:at + 4], struct.unpack_from('<I', data, at + 4)[0]
    chunks[cid] = (at + 8, size)
    at += 8 + size
tmap = bytearray(data[chunks[b'TMAP'][0]:chunks[b'TMAP'][0] + 160])
trks = chunks[b'TRKS'][0]
count = max(i for i in tmap if i != 0xFF) + 1


def track(i):
    start, blocks, bits = struct.unpack_from('<HHI', data, trks + i * 8)
    raw = data[start * 512:(start + blocks) * 512]
    return [(raw[b >> 3] >> (7 - (b & 7))) & 1 for b in range(bits)]


random.seed(5)
tracks = []
kinds = []
for i in range(count):
    bits = track(i)
    if tmap[0] == i or tmap[17 * 4] == i:
        out = bytearray()
        gap = 0
        first = True
        for b in bits:
            gap += 1
            if not b:
                continue
            ticks = int(round(gap * 32 * 1.015)) + random.randint(-3, 3)
            if first:
                ticks += 300            # длинная пауза: 255 и остаток
                first = False
            while ticks >= 255:
                out.append(255)
                ticks -= 255
            out.append(max(1, ticks))
            gap = 0
        tracks.append(bytes(out))
        kinds.append('flux')
    else:
        packed = bytearray((len(bits) + 7) // 8)
        for k, v in enumerate(bits):
            if v:
                packed[k >> 3] |= 0x80 >> (k & 7)
        tracks.append((bytes(packed), len(bits)))
        kinds.append('bits')

flux = bytearray([0xFF] * 160)
for i, k in enumerate(kinds):
    if k == 'flux':
        for q in range(160):
            if tmap[q] == i:
                tmap[q] = 0xFF
                flux[q] = i
blocks = []
for t, k in zip(tracks, kinds):
    size = len(t) if k == 'flux' else len(t[0])
    blocks.append((size + 511) // 512)
trk_blocks = 3 + sum(blocks)
flux_block = trk_blocks
size = (flux_block + 1) * 512
out = bytearray(size)
out[0:8] = b'WOZ2\xff\x0a\x0d\x0a'
out[12:80] = data[12:80]
out[20] = 3                                             # INFO version 3
struct.pack_into('<HH', out, 20 + 46, flux_block, max(b for b, k in zip(blocks, kinds) if k == 'flux'))
out[80:88] = b'TMAP' + struct.pack('<I', 160)
out[88:248] = bytes(tmap)
out[248:256] = b'TRKS' + struct.pack('<I', trk_blocks * 512 - 256)
block = 3
for i, (t, k) in enumerate(zip(tracks, kinds)):
    payload, n = (t, len(t)) if k == 'flux' else t
    struct.pack_into('<HHI', out, 256 + i * 8, block, blocks[i], n)
    out[block * 512:block * 512 + len(payload)] = payload
    block += blocks[i]
out[flux_block * 512:flux_block * 512 + 8] = b'FLUX' + struct.pack('<I', 160)
out[flux_block * 512 + 8:flux_block * 512 + 168] = bytes(flux)
struct.pack_into('<I', out, 8, zlib.crc32(bytes(out[12:])) & 0xFFFFFFFF)
open(os.path.join(here, 'pravetz-dos33-flux.woz'), 'wb').write(out)
