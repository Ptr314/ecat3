# Выдуманный диск 140 Кб Агата или Apple II в обоих порядках секторов, для теста
# порядков посекторных образов: agat-140-order.dsk в порядке DOS 3.3 и
# agat-140-order.po в порядке ProDOS. В каждом секторе - его дорожка и номер в DOS 3.3,
# дальше байты с шагом 37, так что сектор не на своем месте меняет дорожки, которые
# дисковод строит из образа.
#
# Файл держит 16 секторов дорожки по 256 байт в своем порядке; таблицы дают сектор
# файла для каждого физического сектора дорожки.
import os

DOS_ORDER = (0, 7, 14, 6, 13, 5, 12, 4, 11, 3, 10, 2, 9, 1, 8, 15)
PRODOS_ORDER = (0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15)

HERE = os.path.dirname(os.path.abspath(__file__))


def sector(track, number):
    start = track * 16 + number
    return bytes([track, number]) + bytes((start + 37 * i) % 256 for i in range(254))


dsk = bytearray()
po = bytearray()
for track in range(35):
    # Сектора дорожки по их номерам в DOS 3.3
    dos_track = [sector(track, number) for number in range(16)]
    po_track = [None] * 16
    for physical in range(16):
        po_track[PRODOS_ORDER[physical]] = dos_track[DOS_ORDER[physical]]
    dsk += b"".join(dos_track)
    po += b"".join(po_track)

with open(os.path.join(HERE, "agat-140-order.dsk"), "wb") as f:
    f.write(dsk)
with open(os.path.join(HERE, "agat-140-order.po"), "wb") as f:
    f.write(po)
