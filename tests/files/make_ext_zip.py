"""
Собирает tests/files/ext-vm1.ext.zip для теста ext-vm1-zip.

Архив - упакованное расширение конфигурации: файл .ext в корне и образ ПЗУ,
на который он ссылается по имени. Расширение сжато (deflate), образ лежит как
есть (stored), так что тест проходит оба метода, которые читает эмулятор.

Программа в ПЗУ та же, что в tests/scripts/ext-vm1-inline.ext, только
заливает видеопамять словом 0125252, чтобы снимки двух тестов различались.
"""

import os
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))

WORDS = [0o100004, 0o000340,             # начальные PC и PSW
         0o012700, 0o040000,             # MOV #040000,R0
         0o012701, 0o020000,             # MOV #020000,R1
         0o012702, 0o125252,             # MOV #125252,R2
         0o010220,                       # MOV R2,(R0)+
         0o077102,                       # SOB R1,0100020
         0o000777]                       # BR .

EXT = """\
// Упакованное расширение: образ ПЗУ лежит в архиве рядом с этим файлом
@extends bk/1801vm1-test.cfg
@version Тест К1801ВМ1 (.ext.zip)
-bios:data
bios:size = _22
bios:image = fill.rom
-mapper:@memory[100000-100021]
mapper:@memory[100000-100025] = bios {mode = r}
@script
# Не должен выполняться: сценарий теста, загрузивший машину, важнее
PRINT "встроенный сценарий выполнился"
EXIT
"""


def main():
    rom = b"".join(w.to_bytes(2, "little") for w in WORDS)
    path = os.path.join(HERE, "ext-vm1.ext.zip")
    # Время у записей фиксированное, чтобы архив собирался байт в байт
    stamp = (2026, 1, 1, 0, 0, 0)
    with zipfile.ZipFile(path, "w") as z:
        info = zipfile.ZipInfo("ext-vm1.ext", stamp)
        info.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(info, EXT.encode("utf-8"))
        info = zipfile.ZipInfo("fill.rom", stamp)
        info.compress_type = zipfile.ZIP_STORED
        z.writestr(info, rom)
    print("%s: %d байт" % (path, os.path.getsize(path)))


if __name__ == "__main__":
    main()
