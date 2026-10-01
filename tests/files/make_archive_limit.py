"""
Собирает tests/files/ext-errors/archive-too-large.ext.zip для теста предела
распаковки.

Архив крошечный, но в его каталоге у файла big.bin записан размер 2 ГБ:
так выглядит архив-бомба, где мегабайты нулей разворачиваются в гигабайты.
Эмулятор складывает заявленные размеры до распаковки (им можно верить:
ZipReader::read() отказывает файлу, данные которого вышли другого размера) и
должен отказаться с ошибкой, не записав ни байта. До чтения big.bin дело не
доходит, поэтому настоящих данных в нем три байта.
"""

import io
import os
import struct
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "ext-errors", "archive-too-large.ext.zip")

EXT = """\
// Архив заявляет 2 ГБ распакованных данных: отказ до распаковки
// @error The archive is too large
@extends bk/1801vm1-test.cfg
@version ошибка
"""

buf = io.BytesIO()
with zipfile.ZipFile(buf, "w", zipfile.ZIP_STORED) as z:
    z.writestr("archive-too-large.ext", EXT.encode("utf-8"))
    z.writestr("big.bin", b"\x00\x00\x00")
data = bytearray(buf.getvalue())

# Размер в записи центрального каталога (смещение 24 от сигнатуры PK\1\2)
name = b"big.bin"
p = 0
while True:
    p = data.index(b"PK\x01\x02", p)
    name_len = struct.unpack_from("<H", data, p + 28)[0]
    if bytes(data[p + 46:p + 46 + name_len]) == name:
        struct.pack_into("<I", data, p + 24, 0x80000000)
        break
    p += 4

with open(OUT, "wb") as f:
    f.write(data)
print("written", OUT, len(data), "bytes")
