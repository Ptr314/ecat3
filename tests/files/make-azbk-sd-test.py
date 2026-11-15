# Собирает tests/scripts/work-bk0011m-azbk-sd.ecat (команды карты microSD
# контроллера AZ) и work-bk0011m-azbk-azini.ecat (диски из AZ.INI карты): их
# выполняет маленький интерпретатор в ОЗУ БК.
#
#   py tests/files/make-azbk-sd-test.py
#
# Таблица операций (с 04000, по два слова): 1 x - команда x в CSR, ждать
# готовности, CSR в выходной буфер; 2 x - x в DR; 3 n - n слов из DR в буфер;
# 4 a - строка слов с адреса a в DR до нулевого слова включительно; 0 - конец.
# Выходной буфер - с 06000, строки - с 010000. Дата и время файлов в записях
# оглавления зависят от компьютера и в лог не идут.
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'scripts', 'work-bk0011m-azbk-sd.ecat')

# Интерпретатор, с 01000
PROGRAM = [
    0o012701, 0o177220,         # 1000 MOV #177220,R1
    0o012702, 0o004000,         # 1004 MOV #4000,R2
    0o012705, 0o006000,         # 1010 MOV #6000,R5
    0o012203,                   # 1014 L: MOV (R2)+,R3
    0o001430,                   # 1016 BEQ END
    0o012204,                   # 1020 MOV (R2)+,R4
    0o005303,                   # 1022 DEC R3
    0o001005,                   # 1024 BNE NOT1
    0o010411,                   # 1026 MOV R4,(R1)
    0o105711,                   # 1030 W: TSTB (R1)
    0o100376,                   # 1032 BPL W
    0o011125,                   # 1034 MOV (R1),(R5)+
    0o000766,                   # 1036 BR L
    0o005303,                   # 1040 NOT1: DEC R3
    0o001003,                   # 1042 BNE NOT2
    0o010461, 0o000002,         # 1044 MOV R4,2(R1)
    0o000761,                   # 1050 BR L
    0o005303,                   # 1052 NOT2: DEC R3
    0o001004,                   # 1054 BNE NOT3
    0o016125, 0o000002,         # 1056 R: MOV 2(R1),(R5)+
    0o077403,                   # 1062 SOB R4,R
    0o000753,                   # 1064 BR L
    0o012403,                   # 1066 NOT3: S: MOV (R4)+,R3
    0o010361, 0o000002,         # 1070 MOV R3,2(R1)
    0o001374,                   # 1074 BNE S
    0o000746,                   # 1076 BR L
    0o000777,                   # 1100 END: BR .
]

lines = []


def emit(s=''):
    lines.append(s)


def words_line(dev, addr, words):
    out = []
    for i in range(0, len(words), 8):
        out.append('COMMAND %s.set16(%o, %s)' % (dev, addr + i * 2, ', '.join('%06o' % w for w in words[i:i + 8])))
    return out


class Scenario:
    def __init__(self, title):
        self.title = title
        self.ops = []
        self.strings = []
        self.str_addr = 0o10000
        self.out = 0o6000           # следующее слово выходного буфера
        self.logs = []              # (подпись, адрес, слов)

    def s(self, text):
        b = text.encode('ascii') + b'\0'
        if len(b) % 2: b += b'\0'
        words = [b[i] | (b[i + 1] << 8) for i in range(0, len(b), 2)]
        if words[-1] != 0: words.append(0)
        addr = self.str_addr
        self.strings.append((addr, words))
        self.str_addr += len(words) * 2
        return addr

    def cmd(self, x, label=None):
        self.ops += [1, x]
        if label: self.logs.append((label, self.out, 1))
        self.out += 2

    def dr(self, x):
        self.ops += [2, x]

    def read(self, n, label=None):
        self.ops += [3, n]
        if label: self.logs.append((label, self.out, n))
        self.out += n * 2

    def string(self, text):
        self.ops += [4, self.s(text)]

    # Готовые последовательности
    def reset(self):
        self.cmd(0)

    def to_iobuf(self, text):       # 016 + строка
        self.cmd(0o16)
        self.string(text)

    def to_cmos(self, text):        # 023 + строка
        self.cmd(0o23)
        self.string(text)

    def dir_record(self, label):
        self.reset()
        self.cmd(0o13, label + ': CSR 013')
        self.cmd(0o15)
        # размер (2 слова), [дата, время - не в лог], атрибуты и имя (7 слов)
        self.logs.append((label + ': размер', self.out, 2))
        self.logs.append((label + ': атрибут и имя', self.out + 8, 7))
        self.ops += [3, 11]
        self.out += 22

    def emit(self):
        emit('PRINT "=== %s ==="' % self.title)
        emit('COMMAND cpu.stop()')
        for addr, words in self.strings:
            for l in words_line('ram0', addr, words): emit(l)
        for l in words_line('ram0', 0o4000, self.ops + [0, 0]): emit(l)
        emit('COMMAND cpu.setreg(PSW, 340)')
        emit('COMMAND cpu.setreg(PC, 1000)')
        emit('COMMAND cpu.run()')
        emit('WAIT _100')
        for label, addr, n in self.logs:
            emit('PRINT "%s"' % label)
            emit('LOG ram0.value16(%o, %o)' % (addr, addr + n * 2 - 1))
        emit('LOG cpu.registers')


emit('''# БК0011М + AZBK: карта microSD контроллера AZ (вики MAXIOL «API контроллеров
# AZ», утилиты AZMNT/AZUMNT/AZSMNT, AZLIB02). Карта - каталог
# tests/files/azbk-sd (tests/files/bk0011m-azbk-sd.ext), записи машины - в
# памяти карты. Сценарий собирает tests/files/make-azbk-sd-test.py: таблицу
# операций выполняет интерпретатор в ОЗУ БК (описание - в начале скрипта
# сборки), CSR после каждой команды - 0200 (готово) или 0100200 (ошибка).
#
# 1. Объем карты (056/057): 1024 МБ, свободно 1023.
# 2. Оглавление корня (003, 013 и 015 по записи): DISKS, HELLO.TXT,
#    LOWER.TXT (lower.txt компьютера), SCREENS, затем пустое имя. Атрибуты -
#    020 каталог, 040 файл. В DISKS - только TEST.IMG: long-file-name.txt
#    не 8.3, карта его не видит.
# 3. Чтение HELLO.TXT (023 имя, 050, 051 длина 11, 052, 022): «HELLO AZBK!»;
#    следующая 052 - ошибка (файл прочитан и закрыт).
# 4. Ошибки открытия (051 - код FatFs и разряд 15 старшего слова): нет файла
#    4, «..» 6, сетевой диск N: 3, нет каталога 5.
# 5. Запись NEW.TXT (053 с малыми буквами, 051 = 0, 0, 054 длина 5, 023,
#    055): лишняя 055 - ошибка; чтение назад - «ABCDE»; оглавление DISKS не
#    тронуто, файл - в памяти карты (поле files гнезда az-sd).
# 6. Монтирование (004 «D01=0:/disks/test.img»), блок 0 с AZ1: 012345 054321;
#    таблица 011 - имя как в строке монтирования; повторно на D01 - ошибка,
#    на D05 (нет привода) - ошибка; 014 «D01» и 014 номером в DR.
# 7. 047: HELLO.TXT в память AZ со словного адреса 04000000 (страница 01000);
#    без открытого файла - ошибка, код 9.
# 8. Снимок экрана (044): заголовок в странице 076 - метка 0240, страница 4,
#    одна; имя по часам платы - 0:/SCREENS/ДДЧЧММСС.SCR, файл под этим
#    именем - 1024 + 4096 байт (012000). Без метки - ошибка, в буфере
#    «ERROR tag».
# 9. Блок 1 образа, смонтированного с карты, записан машиной (006); после 014
#    файл TEST.IMG на карте (в ее памяти) - уже с ним: 111111 122222.
# 10. 050 HELLO.TXT, затем 053 COPY.TXT: файлы разные, 052 читает HELLO.TXT и
#    после 053, 055 пишет буфер в COPY.TXT; COPY.TXT читается назад.
# 11. Гнездо az-sd командами сценария (так же их дает кнопка SD окна):
#    protect 0/1 - запись в каталог и обратно (ничего не пишется); eject
#    снимает образ, смонтированный с карты (AZ2), команды карты - ошибка;
#    load вставляет каталог снова - файлов, записанных в память прежней
#    карты, на новой нет.
# @timeout 60

MACHINE "../../tests/files/bk0011m-azbk-sd.ext"
LOGDEFS _16,_8
WAIT _500
COMMAND cpu.stop()''')
for l in words_line('ram0', 0o1000, PROGRAM): emit(l)
emit('LOG azbk-disk.sd')

# 1. Объем
s = Scenario('1. Объем карты')
s.reset()
s.cmd(0o56, '056')
s.cmd(0o57, '057')
s.read(2, 'всего, свободно (МБ)')
s.emit()

# 2. Оглавление
s = Scenario('2. Оглавление корня и DISKS')
s.reset()
s.to_iobuf('0:/')
s.cmd(0o3, 'CSR 003 0:/')
for i in range(5): s.dir_record('корень %d' % (i + 1))
s.reset()
s.to_iobuf('disks')
s.cmd(0o3, 'CSR 003 disks')
for i in range(2): s.dir_record('DISKS %d' % (i + 1))
s.reset()
s.to_iobuf('0:/NOPE')
s.cmd(0o3, 'CSR 003 0:/NOPE (нет каталога)')
s.emit()

# 3. Чтение
s = Scenario('3. Чтение HELLO.TXT')
s.reset()
s.to_cmos('HELLO.TXT')
s.cmd(0o50, 'CSR 050')
s.cmd(0o51)
s.read(2, '051: длина')
s.cmd(0o52, 'CSR 052')
s.cmd(0o22)
s.read(6, 'данные')
s.cmd(0o52, 'CSR 052 после конца')
s.emit()

# 4. Ошибки
s = Scenario('4. Ошибки открытия')
for name, what in (('NOPE.TXT', 'нет файла'), ('0:/../X', '..'), ('N:/X.DSK', 'сетевой диск'),
                   ('NODIR/A.TXT', 'нет каталога'), ('LONGNAME1.TXT', 'не 8.3')):
    s.reset()
    s.to_cmos(name)
    s.cmd(0o50)
    s.cmd(0o51)
    s.read(2, '051 %s (%s)' % (name, what))
s.emit()

# 5. Запись
s = Scenario('5. Запись NEW.TXT')
s.reset()
s.to_cmos('new.txt')
s.reset()                           # как WriteAZINI: сброс между 023 и 053
s.cmd(0o53, 'CSR 053')
s.cmd(0o51)
s.read(2, '051: состояние')
s.cmd(0o54)
s.dr(5)
s.dr(0)
s.cmd(0o23)
s.dr(0x4241)                        # "AB"
s.dr(0x4443)                        # "CD"
s.dr(0x0045)                        # "E"
s.cmd(0o55, 'CSR 055')
s.cmd(0o55, 'CSR 055 лишняя')
s.reset()
s.to_cmos('NEW.TXT')
s.cmd(0o50)
s.cmd(0o51)
s.read(2, '051: длина NEW.TXT')
s.cmd(0o52)
s.cmd(0o22)
s.read(3, 'данные NEW.TXT')
s.emit()
emit('LOG az-sd.files')

# 6. Монтирование
s = Scenario('6. Монтирование')
s.reset()
s.to_iobuf('D01=0:/disks/test.img')
s.cmd(0o4, 'CSR 004 D01')
s.reset()
s.dr(1)
s.cmd(0o1, 'CSR 001 AZ1')
s.dr(0)
s.cmd(0o2)
s.cmd(0o5, 'CSR 005')
s.cmd(0o15)
s.read(2, 'блок 0 AZ1')
s.reset()
s.cmd(0o11)
s.cmd(0o15)
s.read(198)                          # AZ0
s.logs.append(('011 AZ1: размер, флаги, начало имени', s.out, 16))
s.read(16)
s.reset()
s.to_iobuf('D01=0:/HELLO.TXT')
s.cmd(0o4, 'CSR 004 D01 занят')
s.reset()
s.to_iobuf('D05=0:/HELLO.TXT')
s.cmd(0o4, 'CSR 004 D05 нет привода')
s.reset()
s.to_iobuf('D01')
s.cmd(0o14, 'CSR 014 D01')
s.reset()
s.dr(1)
s.cmd(0o1, 'CSR 001 AZ1 после 014')
s.reset()
s.to_iobuf('D1=0:/DISKS/TEST.IMG')
s.cmd(0o4, 'CSR 004 D1')
s.reset()
s.dr(1)
s.cmd(0o14, 'CSR 014 номером в DR')
s.reset()
s.dr(1)
s.cmd(0o14, 'CSR 014 пустого')
s.emit()
emit('LOG az1.attached')

# 7. Файл в память AZ
s = Scenario('7. Файл в память AZ (047)')
s.reset()
s.to_cmos('HELLO.TXT')
s.cmd(0o50)
s.cmd(0o51)
s.read(2)
s.cmd(0o23)
s.dr(0)
s.dr(0o20)
s.reset()
s.cmd(0o47, 'CSR 047')
s.read(2, '047: состояние')
s.reset()
s.cmd(0o47, 'CSR 047 без файла')
s.read(2, '047: состояние без файла')
s.emit()
emit('LOG azbk-ram.value16(10000000, 10000013)')

# 8. Снимок экрана
emit('COMMAND azbk-ram.set16(760000, 240, 0, 0, 0, 0, 4, 1)')
s = Scenario('8. Снимок экрана (044)')
s.reset()
s.cmd(0o23)
s.dr(0)
s.cmd(0o44, 'CSR 044')
s.cmd(0o22)
name_at = s.out
s.read(13, 'имя снимка')            # 23 знака, ноль и нулевое слово
s.reset()
s.cmd(0o23)
s.ops += [4, name_at]               # то же имя назад
s.cmd(0o50)
s.cmd(0o51)
s.read(2, '051: длина снимка')
s.emit()
emit('COMMAND azbk-ram.set16(760000, 0)')
s = Scenario('8. Снимок без метки')
s.reset()
s.to_cmos('SHOT.SCR')
s.cmd(0o44, 'CSR 044 без метки')
s.cmd(0o22)
s.read(5, 'ответ в буфере')
s.emit()
emit('LOG az-sd.files')

# 9. Запись в образ, смонтированный с карты
s = Scenario('9. Запись в смонтированный образ и снятие')
s.reset()
s.to_iobuf('D01=0:/DISKS/TEST.IMG')
s.cmd(0o4, 'CSR 004 D01')
s.reset()
s.dr(1)
s.cmd(0o1)
s.dr(1)
s.cmd(0o2)
s.cmd(0o16)
s.dr(0o111111)
s.dr(0o122222)
s.cmd(0o6, 'CSR 006 блок 1')
s.reset()
s.to_iobuf('D01')
s.cmd(0o14, 'CSR 014 D01')
s.reset()
s.to_cmos('0:/DISKS/TEST.IMG')
s.cmd(0o50)
s.cmd(0o51)
s.read(2, '051: длина TEST.IMG')
s.cmd(0o52)
s.cmd(0o52)
s.cmd(0o22)
s.read(2, 'блок 1 файла после снятия')
s.emit()
emit('LOG az-sd.files')

# 10. Чтение одного файла вперемешку с записью другого
s = Scenario('10. Копирование: 050 и 053 - разные файлы')
s.reset()
s.to_cmos('HELLO.TXT')
s.cmd(0o50)
s.cmd(0o51)
s.read(2, '051: длина HELLO.TXT')
s.reset()
s.to_cmos('COPY.TXT')
s.cmd(0o53)
s.cmd(0o51)
s.read(2, '051: состояние COPY.TXT')
s.cmd(0o54)
s.dr(11)
s.dr(0)
s.cmd(0o52, 'CSR 052 HELLO.TXT после 053')
s.cmd(0o55, 'CSR 055 COPY.TXT')
s.reset()
s.to_cmos('COPY.TXT')
s.cmd(0o50)
s.cmd(0o51)
s.read(2, '051: длина COPY.TXT')
s.cmd(0o52)
s.cmd(0o22)
s.read(6, 'данные COPY.TXT')
s.emit()
emit('LOG az-sd.files')
emit('LOG azbk-disk.file')

# 11. Гнездо: запись в каталог, вынуть и вставить карту командами
emit('PRINT "=== 11. Гнездо az-sd ==="')
emit('LOG az-sd.protected')
emit('COMMAND az-sd.protect(0)')
emit('LOG az-sd.protected')
emit('COMMAND az-sd.protect(1)')
emit('LOG az-sd.protected')
emit('LOG az-sd.file')
s = Scenario('11. Монтирование D02 перед тем, как вынуть карту')
s.reset()
s.to_iobuf('D02=0:/DISKS/TEST.IMG')
s.cmd(0o4, 'CSR 004 D02')
s.emit()
emit('LOG az2.attached')
emit('COMMAND az-sd.eject()')
emit('LOG az-sd.attached')
emit('LOG az-sd.files')
emit('LOG azbk-disk.sd')
emit('LOG az2.attached')
s = Scenario('11. Без карты')
s.reset()
s.cmd(0o56, 'CSR 056 без карты')
s.emit()
emit('COMMAND az-sd.load("../files/azbk-sd")')
emit('LOG az-sd.attached')
emit('LOG az-sd.file')
emit('LOG az-sd.files')
s = Scenario('11. Карта снова')
s.reset()
s.to_cmos('NEW.TXT')
s.cmd(0o50)
s.cmd(0o51)
s.read(2, '051 NEW.TXT: файлов прежней карты в памяти больше нет')
s.reset()
s.to_cmos('HELLO.TXT')
s.cmd(0o50)
s.cmd(0o51)
s.read(2, '051 HELLO.TXT')
s.emit()
emit('EXIT')

open(OUT, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')
print(OUT)

# ------------------------------------------------------------------------
# work-bk0011m-azbk-azini: диски из AZ.INI карты при включении и после 037
del lines[:]
OUT2 = os.path.join(HERE, '..', 'scripts', 'work-bk0011m-azbk-azini.ecat')
emit('''# БК0011М + AZBK: диски из AZ.INI карты microSD - при включении и по команде
# 037, как у платы (AZMNT MAXIOL пишет AZ.INI и перезапускает плату, чтобы они
# подключились). Карта tests/files/azbk-sd-ini, машина
# tests/files/bk0011m-azbk-azini.ext; сценарий собирает
# tests/files/make-azbk-sd-test.py.
#
# 1. В AZ.INI - D1=0:/disks/test.img, D02 (файла нет), D5 (привода нет). После
#    включения AZ1 - TEST.IMG, в таблице 011 - строкой из AZ.INI; AZ2 пуст, AZ0
#    (в AZ.INI его нет) - andos330.img конфигурации, с него и грузится система.
# 2. Новый AZ.INI - только D3 - пишется в память карты (023 053 054 023 055) и
#    команда 037 перезапускает плату: AZ1, смонтированный с карты и больше не
#    названный, снят, AZ3 - TEST.IMG, AZ0 по-прежнему andos330.img.
# @timeout 60

MACHINE "../../tests/files/bk0011m-azbk-azini.ext"
LOGDEFS _16,_8
WAIT _500
PRINT "=== 1. После включения ==="
LOG az0.file
LOG az1.attached
LOG az1.file
LOG az2.attached
LOG az3.attached
COMMAND cpu.stop()''')
for l in words_line('ram0', 0o1000, PROGRAM): emit(l)

s = Scenario('1. Таблица 011: AZ1')
s.reset()
s.cmd(0o11)
s.cmd(0o15)
s.read(198)
s.logs.append(('011 AZ1: размер, флаги, имя', s.out, 16))
s.read(16)
s.emit()

new_ini = '[DISKS]\r\nD3=0:/DISKS/TEST.IMG\r\n'
data = new_ini.encode('ascii')
if len(data) % 2: data += b'\0'
s = Scenario('2. Новый AZ.INI и 037')
s.reset()
s.to_cmos('AZ.INI')
s.reset()
s.cmd(0o53, 'CSR 053')
s.cmd(0o54)
s.dr(len(new_ini))
s.dr(0)
s.cmd(0o23)
for i in range(0, len(data), 2): s.dr(data[i] | (data[i + 1] << 8))
s.cmd(0o55, 'CSR 055')
s.emit()
emit('LOG az-sd.files')
emit('COMMAND mapper.set16(177220, 37)')
emit('WAIT _1000')
PRINT_AFTER = ['PRINT "=== 2. После 037 ==="', 'LOG az0.file', 'LOG az1.attached', 'LOG az3.attached', 'LOG az3.file', 'EXIT']
for l in PRINT_AFTER: emit(l)
open(OUT2, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')
print(OUT2)
