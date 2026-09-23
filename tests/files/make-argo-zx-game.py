# -*- coding: utf-8 -*-
# Исходник игры из deploy/scripts/argo-zx-game.ecat: она там лежит байтами, а
# собирается отсюда. Игра своя, не с ленты - игр со Спектрума в проекте нет.
#
# Запуск выдаёт zxgame.inc, готовые строки COMMAND mem.set для сценария.
#
# "Лови": сверху падает блок, внизу ездит тарелка. Поймал - счёт растёт,
# пропустил - счёт обнуляется. Управление O и P (влево-вправо).
#
# Пишется прямо в экранный файл Спектрума, клавиатура читается портом $FE.

CODE = 0x8000          # Куда ложится программа
VARS = 0x8300          # Переменные

# --- крошечный ассемблер: два прохода, метки абсолютные ---
class Asm:
    def __init__(self, org):
        self.org = org
        self.out = bytearray()
        self.labels = {}
        self.fixups = []            # (позиция, имя, тип)
    def label(self, name):
        self.labels[name] = self.org + len(self.out)
    def db(self, *bs):
        for b in bs: self.out.append(b & 0xFF)
    def dw(self, v):
        self.out.append(v & 0xFF); self.out.append((v >> 8) & 0xFF)
    def dwl(self, name):
        self.fixups.append((len(self.out), name, 'abs'))
        self.out.append(0); self.out.append(0)
    def rel(self, name):
        self.fixups.append((len(self.out), name, 'rel'))
        self.out.append(0)
    def resolve(self):
        for pos, name, kind in self.fixups:
            t = self.labels[name]
            if kind == 'abs':
                self.out[pos] = t & 0xFF; self.out[pos+1] = (t >> 8) & 0xFF
            else:
                d = t - (self.org + pos + 1)
                assert -128 <= d <= 127, (name, d)
                self.out[pos] = d & 0xFF
        return bytes(self.out)

a = Asm(CODE)
L = a.label

# Адреса переменных
PCOL = VARS + 0        # столбец тарелки 0..31
TCOL = VARS + 1        # столбец падающего блока
TROW = VARS + 2        # его строка 0..22
SCORE = VARS + 3       # счёт
SEED = VARS + 4        # для псевдослучайного столбца

PLAYER_ROW = 23

# --- точка входа ---
L('start')
a.db(0xF3)                                  # DI
a.db(0x31); a.dw(0x7FF0)                    # LD SP,$7FF0
# экран в ноль
a.db(0x21); a.dw(0x4000)                    # LD HL,$4000
a.db(0x11); a.dw(0x4001)                    # LD DE,$4001
a.db(0x01); a.dw(0x17FF)                    # LD BC,$17FF
a.db(0x36, 0x00)                            # LD (HL),0
a.db(0xED, 0xB0)                            # LDIR
# атрибуты: чёрное по белому
a.db(0x21); a.dw(0x5800)                    # LD HL,$5800
a.db(0x11); a.dw(0x5801)                    # LD DE,$5801
a.db(0x01); a.dw(0x02FF)                    # LD BC,$02FF
a.db(0x36, 0x38)                            # LD (HL),$38
a.db(0xED, 0xB0)                            # LDIR
# верхняя строка счёта - яркая красная на чёрном
a.db(0x21); a.dw(0x5800)                    # LD HL,$5800
a.db(0x11); a.dw(0x5801)                    # LD DE,$5801
a.db(0x01); a.dw(0x001F)                    # LD BC,$001F
a.db(0x36, 0x42)                            # LD (HL),$42  (BRIGHT, INK 2)
a.db(0xED, 0xB0)                            # LDIR
# начальные значения
a.db(0x3E, 16); a.db(0x32); a.dw(PCOL)      # LD A,16 / LD (PCOL),A
a.db(0x3E, 5);  a.db(0x32); a.dw(TCOL)      # LD A,5  / LD (TCOL),A
a.db(0xAF);     a.db(0x32); a.dw(TROW)      # XOR A   / LD (TROW),A
a.db(0xAF);     a.db(0x32); a.dw(SCORE)
a.db(0x3E, 0x2B); a.db(0x32); a.dw(SEED)

# --- главный цикл ---
L('loop')
# стереть блок
a.db(0x3A); a.dw(TROW)                      # LD A,(TROW)
a.db(0x47)                                  # LD B,A
a.db(0x3A); a.dw(TCOL)                      # LD A,(TCOL)
a.db(0x4F)                                  # LD C,A
a.db(0x1E, 0x00)                            # LD E,0
a.db(0xCD); a.dwl('cell')                   # CALL cell
# стереть тарелку
a.db(0x06, PLAYER_ROW)                      # LD B,23
a.db(0x3A); a.dw(PCOL)
a.db(0x4F)                                  # LD C,A
a.db(0x1E, 0x00)
a.db(0xCD); a.dwl('cell')

# клавиши: полуряд $DF - P(разряд 0), O(разряд 1)
a.db(0x3E, 0xDF)                            # LD A,$DF
a.db(0xDB, 0xFE)                            # IN A,($FE)
a.db(0x47)                                  # LD B,A
a.db(0xCB, 0x48)                            # BIT 1,B   (O - влево)
a.db(0x20); a.rel('no_left')                # JR NZ,no_left
a.db(0x3A); a.dw(PCOL)
a.db(0xB7)                                  # OR A
a.db(0x28); a.rel('no_left')                # JR Z,no_left
a.db(0x3D)                                  # DEC A
a.db(0x32); a.dw(PCOL)
L('no_left')
a.db(0xCB, 0x40)                            # BIT 0,B   (P - вправо)
a.db(0x20); a.rel('no_right')
a.db(0x3A); a.dw(PCOL)
a.db(0xFE, 31)                              # CP 31
a.db(0x28); a.rel('no_right')
a.db(0x3C)                                  # INC A
a.db(0x32); a.dw(PCOL)
L('no_right')

# блок падает
a.db(0x3A); a.dw(TROW)
a.db(0x3C)                                  # INC A
a.db(0xFE, PLAYER_ROW)                      # CP 23
a.db(0x38); a.rel('falling')                # JR C,falling
# долетел: поймал или нет
a.db(0x3A); a.dw(TCOL)
a.db(0x47)                                  # LD B,A
a.db(0x3A); a.dw(PCOL)
a.db(0xB8)                                  # CP B
a.db(0x20); a.rel('missed')                 # JR NZ,missed
a.db(0x3A); a.dw(SCORE)
a.db(0xFE, 31)
a.db(0x28); a.rel('no_inc')
a.db(0x3C)
a.db(0x32); a.dw(SCORE)
L('no_inc')
a.db(0x18); a.rel('respawn')
L('missed')
a.db(0xAF)
a.db(0x32); a.dw(SCORE)
L('respawn')
# новый столбец: seed = seed*5 + 7
a.db(0x3A); a.dw(SEED)
a.db(0x4F)                                  # LD C,A
a.db(0x87)                                  # ADD A,A
a.db(0x87)                                  # ADD A,A
a.db(0x81)                                  # ADD A,C
a.db(0xC6, 7)                               # ADD A,7
a.db(0x32); a.dw(SEED)
a.db(0xE6, 0x1F)                            # AND $1F
a.db(0x32); a.dw(TCOL)
a.db(0xAF)                                  # XOR A
L('falling')
a.db(0x32); a.dw(TROW)

# нарисовать блок
a.db(0x3A); a.dw(TROW)
a.db(0x47)
a.db(0x3A); a.dw(TCOL)
a.db(0x4F)
a.db(0x1E, 0x7E)                            # LD E,$7E  - рисунок блока
a.db(0xCD); a.dwl('cell')
# нарисовать тарелку
a.db(0x06, PLAYER_ROW)
a.db(0x3A); a.dw(PCOL)
a.db(0x4F)
a.db(0x1E, 0xFF)                            # LD E,$FF
a.db(0xCD); a.dwl('cell')
# счёт полоской в верхней строке
a.db(0xCD); a.dwl('bar')
# задержка
a.db(0x01); a.dw(0x1800)                    # LD BC,$1800
L('delay')
a.db(0x0B)                                  # DEC BC
a.db(0x78)                                  # LD A,B
a.db(0xB1)                                  # OR C
a.db(0x20); a.rel('delay')
a.db(0xC3); a.dwl('loop')                   # JP loop

# --- cell: знакоместо B=строка, C=столбец, E=рисунок (все восемь растров) ---
L('cell')
a.db(0xC5)                                  # PUSH BC
a.db(0x78)                                  # LD A,B
a.db(0xE6, 0x18)                            # AND $18
a.db(0xF6, 0x40)                            # OR $40
a.db(0x67)                                  # LD H,A
a.db(0x78)                                  # LD A,B
a.db(0xE6, 0x07)                            # AND 7
a.db(0x07); a.db(0x07); a.db(0x07); a.db(0x07); a.db(0x07)  # RLCA x5
a.db(0xB1)                                  # OR C
a.db(0x6F)                                  # LD L,A
a.db(0x06, 8)                               # LD B,8
L('cell_l')
a.db(0x73)                                  # LD (HL),E
a.db(0x7C)                                  # LD A,H
a.db(0xC6, 0x01)                            # ADD A,1
a.db(0x67)                                  # LD H,A
a.db(0x10); a.rel('cell_l')                 # DJNZ cell_l
a.db(0xC1)                                  # POP BC
a.db(0xC9)                                  # RET

# --- bar: счёт полоской в строке 0 ---
L('bar')
a.db(0x21); a.dw(0x4000)                    # LD HL,$4000
a.db(0x06, 32)                              # LD B,32
a.db(0x0E, 0x00)                            # LD C,0
L('bar_l')
a.db(0x3A); a.dw(SCORE)
a.db(0xB9)                                  # CP C
a.db(0x3E, 0x00)                            # LD A,0
a.db(0x30); a.rel('bar_off')                # JR NC,bar_off  (score >= C -> закрашено)
a.db(0x18); a.rel('bar_put')
L('bar_off')
a.db(0x3E, 0x3C)                            # LD A,$3C
L('bar_put')
a.db(0x77)                                  # LD (HL),A
a.db(0x23)                                  # INC HL
a.db(0x0C)                                  # INC C
a.db(0x10); a.rel('bar_l')                  # DJNZ bar_l
a.db(0xC9)                                  # RET

code = a.resolve()
print('длина программы:', len(code), 'байт, метки:', {k: hex(v) for k, v in sorted(a.labels.items(), key=lambda x: x[1])})

# --- выдать команды .ecat ---
lines = []
for i in range(0, len(code), 16):
    chunk = code[i:i+16]
    vals = ','.join('$%02X' % b for b in chunk)
    lines.append('COMMAND mem.set($%04X,%s)' % (CODE + i, vals))
open('zxgame.inc', 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
print('записано zxgame.inc, строк:', len(lines))
