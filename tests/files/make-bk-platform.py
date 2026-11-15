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
# 7-bk-smk-overlay.zip - тот же образ так, как программы для СМК-512 лежат в
# каталоге: bins/*.bin (основная программа и оверлей, их она сама грузит с
# диска) и один и тот же диск под разные системы - disks/OVERLAY_CSIDOS.IMG,
# disks/overlay_andos.img, disks/OVERLAY_MKDOS.IMG (регистр разный нарочно). Ни
# одно имя не совпадает с именем архива; платформа «БК0011М + СМК512» берет
# только образы, а из них по строке prefer = ANDOS - образ под ANDOS.
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
# 9-bk-81-tracks.zip - образ дискеты на 81 дорожку (829440 байт, так размечает
# MK-DOS; в архиве почти одни нули): дисковод БК растет под него до max_tracks.
# Загрузочный сектор читает через вход 0160004 ПЗУ дисковода блок 1600 -
# дорожка 80, сторона 0 - и кладет его первое слово (метка 0161616) в 02100, а
# при ошибке (C) - R0 в 02102:
#
#   01000  000240                     NOP
#   01002  012700 003100              MOV  #3100,R0       ; блок 1600
#   01006  012701 000400              MOV  #400,R1        ; 256 слов
#   01012  012702 003000              MOV  #3000,R2       ; буфер
#   01016  004737 160004              JSR  PC,@#160004    ; R3 - область ПЗУ, как его пустили
#   01022  103404                     BCS  1$
#   01024  013737 003000 002100       MOV  @#3000,@#2100
#   01032  000777                     BR   .
#   01034  010037 002102        1$:   MOV  R0,@#2102
#   01040  000777                     BR   .
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

with zipfile.ZipFile(os.path.join(HERE, "7-bk-smk-overlay.zip"), "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr(zipfile.ZipInfo("bins/overlay.exe.bin", date_time=STAMP), bk_file(code),
               compress_type=zipfile.ZIP_DEFLATED)
    z.writestr(zipfile.ZipInfo("bins/overlay.ovl.bin", date_time=STAMP), bk_file(bytes(16)),
               compress_type=zipfile.ZIP_DEFLATED)
    for name in ("disks/OVERLAY_CSIDOS.IMG", "disks/overlay_andos.img", "disks/OVERLAY_MKDOS.IMG"):
        z.writestr(zipfile.ZipInfo(name, date_time=STAMP), disk, compress_type=zipfile.ZIP_DEFLATED)

BOOT81 = [0o000240,
          0o012700, 0o003100,
          0o012701, 0o000400,
          0o012702, 0o003000,
          0o004737, 0o160004,
          0o103404,
          0o013737, 0o003000, 0o002100,
          0o000777,
          0o010037, 0o002102,
          0o000777]
boot81 = b"".join(struct.pack("<H", w) for w in BOOT81)
disk81 = bytearray(81 * 2 * 10 * 512)
disk81[0:len(boot81)] = boot81
disk81[1600 * 512:1600 * 512 + 2] = struct.pack("<H", 0o161616)
with zipfile.ZipFile(os.path.join(HERE, "9-bk-81-tracks.zip"), "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr(zipfile.ZipInfo("bk-81-tracks.img", date_time=STAMP), bytes(disk81),
               compress_type=zipfile.ZIP_DEFLATED)
