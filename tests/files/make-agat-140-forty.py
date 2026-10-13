# Выдуманный диск 140 Кб Агата или Apple II из 40 дорожек, для теста таких дисков:
# agat-140-forty.dsk в порядке DOS 3.3. Сектора - те же, что в agat-140-order.dsk
# (make-agat-140-order.py): его дорожка и номер в DOS 3.3, дальше байты с шагом 37;
# первые 35 дорожек у двух дисков совпадают, отличаются только последние пять.
import os

HERE = os.path.dirname(os.path.abspath(__file__))


def sector(track, number):
    start = track * 16 + number
    return bytes([track, number]) + bytes((start + 37 * i) % 256 for i in range(254))


with open(os.path.join(HERE, "agat-140-forty.dsk"), "wb") as f:
    f.write(b"".join(sector(track, number) for track in range(40) for number in range(16)))
