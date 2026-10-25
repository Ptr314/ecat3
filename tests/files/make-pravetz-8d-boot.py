# Диск для disk-pravetz-8d-boot: 140 Кб, на нем только загрузочный сектор
# (дорожка 0, сектор 0). Образа настоящего диска 8DOS у нас нет, поэтому
# проверяется путь, который проходит ПЗУ контроллера: CALL 800 ($0320)
# переключает окно ПЗУ на вторую половину, та читает сектор 0 в $B800 и
# переходит туда. Программа сектора печатает строку, включает оверлей
# (запись в $0381 отдает $C000-$FFFF ОЗУ), пишет и читает $C000, выключает
# его ($0380) и читает ПЗУ: оба байта остаются в $0200 и $0201.
#
#   python make-pravetz-8d-boot.py  ->  pravetz-8d-boot.dsk
import os

MSG = b'BOOT OK'
prog = [
    0xA2, 0x00,             # B800 LDX #0
    0xBD, 0x27, 0xB8,       # B802 LDA MSG,X
    0xF0, 0x06,             # B805 BEQ B80D
    0x9D, 0xAA, 0xBB,       # B807 STA $BBAA,X
    0xE8,                   # B80A INX
    0xD0, 0xF5,             # B80B BNE B802
    0x8D, 0x81, 0x03,       # B80D STA $0381   оверлей: ОЗУ в $C000
    0xA9, 0x55,             # B810 LDA #$55
    0x8D, 0x00, 0xC0,       # B812 STA $C000
    0xAD, 0x00, 0xC0,       # B815 LDA $C000
    0x8D, 0x00, 0x02,       # B818 STA $0200
    0x8D, 0x80, 0x03,       # B81B STA $0380   снова ПЗУ
    0xAD, 0x00, 0xC0,       # B81E LDA $C000
    0x8D, 0x01, 0x02,       # B821 STA $0201
    0x4C, 0x24, 0xB8,       # B824 JMP B824
]
assert len(prog) == 0x27
sector = bytes(prog) + MSG + b'\x00'
image = sector + bytes(256 - len(sector)) + bytes(143360 - 256)

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'pravetz-8d-boot.dsk')
with open(out, 'wb') as f:
    f.write(image)
