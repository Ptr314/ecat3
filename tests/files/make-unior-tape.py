# Небольшая лента Юниора (.bt) для теста магнитофона.
#
# Контейнер снят с образов А. Морозова и сверен с ПЗУ машины:
#   [dword]                                   - заголовок файла
#   [dword пауза][dword длина][AA AA 19 00][данные]
# Пауза считается в битовых интервалах (2400 бод), данные начинаются с
# синхросимвола $E6, который ВВ51 ловит в режиме охоты.
#
# Запись в том виде, в каком ее читает команда МОНИТОРа "I" (FBBB):
#   $E6 $00 <начало:2> <конец:2> <данные> <сумма:2>
# Адреса и сумма идут старшим байтом вперед, сумма - простое 16-разрядное
# сложение байт блока (FE4D). Запускается блок с адреса "конец минус два"
# (FBE6), поэтому последними тремя байтами стоит переход на начало.
#
# Программа пишет "TAPE OK" в экранный буфер и останавливается: удачную
# загрузку видно и в памяти, и на снимке экрана.
import struct

ORG = 0x0100
SCREEN = 0xF802                     # Вторая строка экрана

code = bytearray()
code += bytes([0x21, SCREEN & 0xFF, SCREEN >> 8])       # LXI H,SCREEN
text_at = ORG + 18                                      # адрес строки: сразу за HLT
code += bytes([0x11, text_at & 0xFF, text_at >> 8])     # LXI D,text
loop = ORG + len(code)
code += bytes([0x1A])                                   # LDAX D
code += bytes([0xB7])                                   # ORA A
done = ORG + len(code) + 3 + 1 + 2 + 3                  # адрес HLT: JZ, MOV, INX+INX, JMP
code += bytes([0xCA, done & 0xFF, done >> 8])           # JZ done
code += bytes([0x77])                                   # MOV M,A
code += bytes([0x23, 0x13])                             # INX H / INX D
code += bytes([0xC3, loop & 0xFF, loop >> 8])           # JMP loop
assert ORG + len(code) == done, (hex(ORG + len(code)), hex(done))
code += bytes([0x76])                                   # HLT
assert ORG + len(code) == text_at, (hex(ORG + len(code)), hex(text_at))
code += b"TAPE OK\x00"

prog = bytes(code) + bytes([0xC3, ORG & 0xFF, ORG >> 8])   # пуск идет сюда

start = ORG
end = ORG + len(prog) - 1
checksum = sum(prog) & 0xFFFF

body = bytes([0xE6, 0x00, start >> 8, start & 0xFF, end >> 8, end & 0xFF]) \
     + prog + bytes([checksum >> 8, checksum & 0xFF])

record = struct.pack('<II', 2400, len(body)) + bytes([0xAA, 0xAA, 0x19, 0x00]) + body
out = struct.pack('<I', 0) + record

with open('unior-test.bt', 'wb') as f:
    f.write(out)

print('unior-test.bt: %d байт, программа %04X..%04X (%d байт), сумма %04X, пуск с %04X'
      % (len(out), start, end, len(prog), checksum, end - 2))

# Чистая кассета: файл из одного заголовка, записей нет. Длину такой ленте
# дает само устройство (blank_length, по умолчанию сторона С-60), потому что
# в файле ее взять неоткуда
with open('unior-blank.bt', 'wb') as f:
    f.write(struct.pack('<I', 0))

print('unior-blank.bt: чистая кассета, %d байт' % 4)
