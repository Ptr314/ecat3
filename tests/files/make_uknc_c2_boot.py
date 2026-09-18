#!/usr/bin/env python3
"""Загрузочный блок для теста стыка С2 УК-НЦ (tests/scripts/boot-uknc-c2.ecat).

Пункт 4 меню ЗАГРУЗКА посылает в С2 символ @, принимает 512 байт в ОЗУ ЦП с
адреса 0 и, если первое слово 000240, переходит на адрес 0. Так грузит
RT-11 сервер HX; здесь вместо загрузчика HX - программа, которая проверяет
сам порт:

  1. передаёт по линии строку "C2 OK\\r\\n";
  2. включает петлю (разряд 2 XCSR), передаёт Z, дожидается его в приёмнике
     и кладёт в слово 0700;
  3. разрешает прерывание приёмника (вектор 370), передаёт Q; обработчик
     кладёт принятый байт в 0704 и увеличивает счётчик 0702;
  4. выключает петлю и стоит на BR .

Все прочие векторы блока указывают на RTI: блок ложится на область векторов.

Запуск: python make_uknc_c2_boot.py  ->  uknc-c2-boot.bin рядом со скриптом.
"""

import os
import struct

RCSR, RBUF, XCSR, XBUF = 0o176570, 0o176572, 0o176574, 0o176576
RESULT_LOOP, FLAG, RESULT_IRQ = 0o700, 0o702, 0o704
ORIGIN = 0o400


class Asm:
    """Совсем маленький ассемблер: слова, метки и переходы на них."""

    def __init__(self, origin):
        self.origin = origin
        self.words = []
        self.labels = {}
        self.fixups = []            # (индекс слова, метка, вид)

    def pc(self):
        return self.origin + 2 * len(self.words)

    def label(self, name):
        self.labels[name] = self.pc()

    def w(self, *words):
        self.words.extend(words)

    def br(self, opcode, name):
        self.fixups.append((len(self.words), name, 'br'))
        self.words.append(opcode)

    def addr(self, name):
        self.fixups.append((len(self.words), name, 'abs'))
        self.words.append(0)

    def resolve(self):
        for index, name, kind in self.fixups:
            target = self.labels[name]
            if kind == 'abs':
                self.words[index] = target
            else:
                offset = (target - (self.origin + 2 * index + 2)) // 2
                assert -128 <= offset <= 127, name
                self.words[index] |= offset & 0o377


a = Asm(ORIGIN)
a.w(0o012706, 0o002000)                     # MOV #2000,SP
a.w(0o106427, 0o000340)                     # MTPS #340
a.w(0o012701); a.addr('msg')                # MOV #msg,R1
a.label('next')
a.w(0o112100)                               # MOVB (R1)+,R0
a.br(0o001400, 'sent')                      # BEQ sent
a.label('wait_tx')
a.w(0o105737, XCSR)                         # TSTB @#XCSR
a.br(0o100000, 'wait_tx')                   # BPL wait_tx
a.w(0o110037, XBUF)                         # MOVB R0,@#XBUF
a.br(0o000400, 'next')                      # BR next
a.label('sent')
# Последний символ ещё идёт по линии: петлю включать только после него
a.label('wait_last')
a.w(0o105737, XCSR)                         # TSTB @#XCSR
a.br(0o100000, 'wait_last')                 # BPL wait_last
a.w(0o012737, 0o000004, XCSR)               # MOV #4,@#XCSR - петля
a.w(0o112737, ord('Z'), XBUF)               # MOVB #'Z,@#XBUF
a.label('wait_rx')
a.w(0o105737, RCSR)                         # TSTB @#RCSR
a.br(0o100000, 'wait_rx')                   # BPL wait_rx
a.w(0o113737, RBUF, RESULT_LOOP)            # MOVB @#RBUF,@#700
# Прерывание приёмника
a.w(0o012737); a.addr('handler'); a.w(0o000370)   # MOV #handler,@#370
a.w(0o012737, 0o000340, 0o000372)           # MOV #340,@#372
a.w(0o012737, 0o000100, RCSR)               # MOV #100,@#RCSR
a.w(0o106427, 0o000000)                     # MTPS #0
a.w(0o112737, ord('Q'), XBUF)               # MOVB #'Q,@#XBUF
a.label('wait_irq')
a.w(0o005737, FLAG)                         # TST @#702
a.br(0o001400, 'wait_irq')                  # BEQ wait_irq
a.w(0o005037, XCSR)                         # CLR @#XCSR - петля выключена
a.w(0o000777)                               # BR .
a.label('handler')
a.w(0o113737, RBUF, RESULT_IRQ)             # MOVB @#RBUF,@#704
a.w(0o005237, FLAG)                         # INC @#702
a.w(0o000002)                               # RTI
# Заглушка для всех прочих векторов: блок ложится на область векторов, и
# нулевой вектор увёл бы любое чужое прерывание (кадровое, каналов) на адрес 0
a.label('ignore')
a.w(0o000002)                               # RTI
a.label('msg')
text = b'C2 OK\r\n\0'
if len(text) % 2: text += b'\0'
a.w(*struct.unpack('<%dH' % (len(text) // 2), text))
a.resolve()

block = bytearray(512)
# Первое слово - NOP, иначе ПЗУ не перейдёт на блок; второе - переход на код
struct.pack_into('<HH', block, 0, 0o000240, 0o000400 | ((ORIGIN - 4) // 2))
for vector in range(0o004, 0o400, 4):
    struct.pack_into('<HH', block, vector, a.labels['ignore'], 0o340)
code = struct.pack('<%dH' % len(a.words), *a.words)
assert ORIGIN + len(code) <= RESULT_LOOP, 'код залез на результаты'
block[ORIGIN:ORIGIN + len(code)] = code

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'uknc-c2-boot.bin')
with open(out, 'wb') as f:
    f.write(block)
print(out, len(block))
