# Небольшая лента Арго (.bt) для теста магнитофона.
#
# Контейнер и формат записи те же, что у Юниора (см. make-unior-tape.py):
#   [dword начало][dword длительность][dword длина][AA AA 19 00][E6 00 начало:2 конец:2 данные сумма:2]
# Адреса и сумма старшим байтом вперед, запуск идет с адреса "конец минус два"
# (FB61 в ПЗУ Арго), поэтому последними тремя байтами стоит переход на начало.
#
# Программа пишет "LOAD OK" в экранный буфер и останавливается. Буквы взяты из
# тех тридцати знаков, что монитор кладет в знакогенератор при старте: полного
# шрифта без ленты с TCP/M на этой машине нет, и "TAPE OK" вышло бы дырявым.
import os
import struct

ORG = 0x0100
SCREEN = 0xF8A2                     # Третья строка экрана, мимо приглашения

code = bytearray()
code += bytes([0x21, SCREEN & 0xFF, SCREEN >> 8])       # LD HL,SCREEN
text_at = ORG + 18                                      # адрес строки: сразу за HALT
code += bytes([0x11, text_at & 0xFF, text_at >> 8])     # LD DE,text
loop = ORG + len(code)
code += bytes([0x1A])                                   # LD A,(DE)
code += bytes([0xB7])                                   # OR A
done = ORG + len(code) + 3 + 1 + 2 + 3                  # адрес HALT
code += bytes([0xCA, done & 0xFF, done >> 8])           # JP Z,done
code += bytes([0x77])                                   # LD (HL),A
code += bytes([0x23, 0x13])                             # INC HL / INC DE
code += bytes([0xC3, loop & 0xFF, loop >> 8])           # JP loop
assert ORG + len(code) == done, (hex(ORG + len(code)), hex(done))
code += bytes([0x76])                                   # HALT
assert ORG + len(code) == text_at, (hex(ORG + len(code)), hex(text_at))
code += b"LOAD OK" + bytes([0])

prog = bytes(code) + bytes([0xC3, ORG & 0xFF, ORG >> 8])   # пуск идет сюда

start = ORG
end = ORG + len(prog) - 1
checksum = sum(prog) & 0xFFFF

body = bytes([0xE6, 0x00, start >> 8, start & 0xFF, end >> 8, end & 0xFF])      + prog + bytes([checksum >> 8, checksum & 0xFF])

# Перед каждой записью в файле стоят три слова: когда она начинается, сколько
# длится и какой она длины вместе с преамбулой. Первые два - в единицах, которых
# на байт приходится 8000/скорость, то есть в миллисекундах при 2400 бод; пауза
# перед записью получается как ее начало минус конец предыдущей
LEADER = 1000                                   # Секунда ракорда перед записью
length = 4 + len(body)
duration = round(length * 8 * 1000 / 2400)
record = struct.pack('<III', LEADER, duration, length) + bytes([0xAA, 0xAA, 0x19, 0x00]) + body
out = record

# Рядом со сценарием, а не в текущем каталоге: запущенный из корня
# репозитория, он оставлял там устаревшую копию
with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'argo-test.bt'), 'wb') as f:
    f.write(out)

print('argo-test.bt: %d байт, программа %04X..%04X (%d байт), сумма %04X, пуск с %04X'
      % (len(out), start, end, len(prog), checksum, end - 2))
