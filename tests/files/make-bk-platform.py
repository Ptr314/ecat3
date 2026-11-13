# Делает bk-platform.bin и 123-bk-platform-long.zip для тестов platform-*: запуск
# программы по имени платформы (deploy/launch/platforms.cfg).
#
# Программа грузится с 02000, а не с обычного 01000: тест видит, что пуск идет
# с адреса загрузки, а не с постоянного. ПЗУ она не зовет (EMT 16 БОС БК0011М
# после G Монитора ничего не печатает): кладет SP, с которым ее запустили, и
# слово на вершине стека в 02100 и 02102 и рисует белую полосу прямо в экран.
#
#   02000  010637 002100       MOV  SP,@#2100      ; SP при пуске
#   02004  011637 002102       MOV  (SP),@#2102    ; слово на вершине стека
#   02010  012700 054400       MOV  #54400,R0      ; строка 100 экрана
#   02014  012701 000400       MOV  #400,R1        ; 8 строк по 32 слова
#   02020  012720 177777  1$:  MOV  #177777,(R0)+
#   02024  077103              SOB  R1,1$
#   02026  000777              BR   .
#
# bk-platform-long.bin - та же программа, дополненная нулями до 050000: при
# загрузке в Мониторе БК0011М она проходит по стеку БОС (043500), и пустить ее
# командой G Монитора нельзя - Монитор виснет. Лежит в архиве, названном так,
# как их хранит каталог retro-catalog: "<номер>-<имя>.zip"; лишний текстовый
# файл в нем проверяет выбор программы внутри архива. Нули сжимаются, и архив
# остается крошечным.
#
# bk-platform-disk.bkd - образ дискеты из одного загрузочного сектора (дисковод
# считает остаток пустым). BIOS СМК-512 читает блок 0 в 01000 и пускает его;
# сектор кладет в 02100 метку 0123456, в 02102 SP, с которым его пустили, и рисует
# ту же полосу. 42-bk-platform-disk.zip - тот же образ в архиве с readme.txt,
# bk-platform-disk.img - он же с другим расширением дискеты БК.
#
#   01000  000240                     NOP                 ; как у загрузочных секторов БК
#   01002  012737 123456 002100       MOV  #123456,@#2100
#   01010  010637 002102              MOV  SP,@#2102
#   01014  012700 054400              MOV  #54400,R0
#   01020  012701 000400              MOV  #400,R1
#   01024  012720 177777        1$:   MOV  #177777,(R0)+
#   01030  077103                     SOB  R1,1$
#   01032  000777                     BR   .
#
#   py tests/files/make-bk-platform.py

import os
import struct
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))

ADDRESS = 0o2000
LONG_END = 0o50000
WORDS = [0o010637, 0o002100,
         0o011637, 0o002102,
         0o012700, 0o054400,
         0o012701, 0o000400,
         0o012720, 0o177777,
         0o077103,
         0o000777]


def bk_file(data):
    return struct.pack("<HH", ADDRESS, len(data)) + data


code = b"".join(struct.pack("<H", w) for w in WORDS)
with open(os.path.join(HERE, "bk-platform.bin"), "wb") as f:
    f.write(bk_file(code))

long_blob = bk_file(code + bytes(LONG_END - ADDRESS - len(code)))

# Постоянные даты: архив выходит одним и тем же при каждом запуске
STAMP = (2026, 11, 12, 0, 0, 0)
with zipfile.ZipFile(os.path.join(HERE, "123-bk-platform-long.zip"), "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr(zipfile.ZipInfo("readme.txt", date_time=STAMP), b"eCat3 platform launch test\r\n",
               compress_type=zipfile.ZIP_DEFLATED)
    z.writestr(zipfile.ZipInfo("bk-platform-long.bin", date_time=STAMP), long_blob,
               compress_type=zipfile.ZIP_DEFLATED)

BOOT = [0o000240,
        0o012737, 0o123456, 0o002100,
        0o010637, 0o002102,
        0o012700, 0o054400,
        0o012701, 0o000400,
        0o012720, 0o177777,
        0o077103,
        0o000777]
boot = b"".join(struct.pack("<H", w) for w in BOOT)
disk = boot + bytes(512 - len(boot))
for ext in ("bkd", "img"):
    with open(os.path.join(HERE, "bk-platform-disk." + ext), "wb") as f:
        f.write(disk)

with zipfile.ZipFile(os.path.join(HERE, "42-bk-platform-disk.zip"), "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr(zipfile.ZipInfo("readme.txt", date_time=STAMP), b"eCat3 platform launch test\r\n",
               compress_type=zipfile.ZIP_DEFLATED)
    z.writestr(zipfile.ZipInfo("bk-platform-disk.bkd", date_time=STAMP), disk,
               compress_type=zipfile.ZIP_DEFLATED)
