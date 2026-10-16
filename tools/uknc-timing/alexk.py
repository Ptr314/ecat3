# Тесты Alex_K с UTEST.DSK (C* - центральный процессор, P* - периферийный):
# разбор протоколов настоящих машин (alexk/*.log) и вывода эмулятора
# (run_utest.py), формы таблиц и сравнение.
#
# Две машины литеры 7 с разными видеоконтроллерами: «001» - К1515ХМ2-001
# (доработанный), «031» - К1515ХМ1-031 (ранний) (протоколы ЦП и ПП у обеих, остальные -
# PRDWR1, CRDWR1, PSOB - только у 031; P2MOV, PBMOV, PNMOV без пометки - 001,
# по таблице PMOV в PNMOV.log). Время в тактах своего процессора: ЦП считает
# копии команды за кадр (159744 такта = 8 МГц x 19.968 мс), ПП - по своему
# таймеру. Эмулятор выполняет тот же код, так что числа сравниваются прямо.
#
#   py alexk.py DIR [TEST...]     - сводка расхождений вывода эмулятора в DIR
import collections, glob, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
NUM = re.compile(r'^-?\d+\.\d+$')

def parse_lines(lines, test, runs):
    """(test, метка, столбец) -> [значения]"""
    for l in lines:
        l = l.rstrip()
        if not l.strip() or 'Press' in l or l.startswith('.') or 'Протокол' in l or l.startswith('*'):
            continue
        # тест печатает часть чисел с запятой вместо точки (47,0)
        toks = [re.sub(r'^(\d+),(\d)$', r'\1.\2', t) for t in l.split()]
        if len(toks) > 3 and toks[1] == '=' and toks[0] in ('R2', 'R3'):
            label, vals = ' '.join(toks[:3]), toks[3:]          # PSOB1/PSOB2: целые
        else:
            vals = []
            while len(toks) > 1 and (NUM.match(toks[-1]) or toks[-1].startswith('***')):
                vals.insert(0, toks.pop())
            if not vals:
                continue
            label = ' '.join(toks)
        for i, v in enumerate(vals):
            if not v.startswith('*'):
                runs[(test, label, i)].append(float(v))

def parse_log(path):
    runs = collections.defaultdict(list)
    test, buf = None, []
    for l in open(path, encoding='utf-8'):
        if l.startswith('.RU '):
            if test: parse_lines(buf, test, runs)
            test, buf = l.split()[1], []
        else:
            buf.append(l)
    if test: parse_lines(buf, test, runs)
    return runs

def mean(d):
    return {k: sum(v) / len(v) for k, v in d.items() if v}

def machine(name):
    """'001' или '031': среднее по всем прогонам"""
    d = collections.defaultdict(list)
    for f in glob.glob(os.path.join(HERE, 'alexk', '*.log')):
        if os.path.basename(f)[:-4].endswith(name):
            for k, v in parse_log(f).items():
                d[k] += v
    return mean(d)

def emulator(dirname):
    """Вывод run_utest.py: TEST.txt - экраны подряд, одна строка может
    попасть на несколько экранов, берётся первая"""
    runs = {}
    for f in glob.glob(os.path.join(dirname, '*.txt')):
        r = collections.defaultdict(list)
        parse_lines(open(f, encoding='utf-8').readlines(), os.path.basename(f)[:-4], r)
        for k, v in r.items():
            runs[k] = v[0]
    return runs

# Таблицы команд: строки - источник (через R0), столбцы - приёмник (через R1)
COLS = [0o01, 0o11, 0o21, 0o31, 0o41, 0o51, 0o61, 0o71, 0o27, 0o37, 0o67, 0o77]
ROWS = [0o00, 0o10, 0o20, 0o30, 0o40, 0o50, 0o60, 0o70, 0o27, 0o37, 0o67, 0o77]
OPS = {'MOV': 0o01, 'MOVB': 0o11, 'CMP': 0o02, 'CMPB': 0o12, 'BIT': 0o03, 'BITB': 0o13,
       'BIC': 0o04, 'BICB': 0o14, 'BIS': 0o05, 'BISB': 0o15, 'ADD': 0o06, 'SUB': 0o16}
OP1 = {'CLR': 0o0050, 'CLRB': 0o1050, 'COM': 0o0051, 'COMB': 0o1051, 'INC': 0o0052, 'INCB': 0o1052,
       'DEC': 0o0053, 'DECB': 0o1053, 'NEG': 0o0054, 'NEGB': 0o1054, 'ADC': 0o0055, 'ADCB': 0o1055,
       'SBC': 0o0056, 'SBCB': 0o1056, 'TST': 0o0057, 'TSTB': 0o1057, 'ROR': 0o0060, 'RORB': 0o1060,
       'ROL': 0o0061, 'ROLB': 0o1061, 'ASR': 0o0062, 'ASRB': 0o1062, 'ASL': 0o0063, 'ASLB': 0o1063,
       'SWAB': 0o0003, 'SXT': 0o0067, 'MTPS': 0o1064, 'MFPS': 0o1067, 'XOR': 0o0740}
JCOLS = [0o21, 0o31, 0o51, 0o61, 0o71, 0o17, 0o27, 0o37, 0o67, 0o77]

def forms(prefix):
    """(тест, метка, столбец) -> (группа, слово команды, переход выполняется).
    Ряды ...R таблиц ПП (источник в ПЗУ) сюда не входят: у них память данных
    другая, их проверяет только прогон эмулятора"""
    out = {}
    for name, op in OPS.items():
        for r in ROWS:
            key = 'key' if r >> 3 <= 2 and r != 0o27 else 'op2'
            for i, c in enumerate(COLS):
                g = key if key == 'key' and c >> 3 <= 2 and c != 0o27 else 'op2'
                out[(prefix + name, '%02o' % r, i)] = (g, (op << 12) | (r << 6) | c, True)
    for name, op in OP1.items():
        for i, c in enumerate(COLS):
            out[(prefix + 'OP1', name, i)] = ('op1', (op << 6) | c, True)
    for i, c in enumerate(JCOLS):
        out[(prefix + 'JMP', 'JMP', i)] = ('jmp', 0o000100 | c, True)
        out[(prefix + 'JMP', 'JSR', i)] = ('jmp', 0o004700 | c, True)
    for name, w, taken in (('NOP', 0o240, True), ('CCC', 0o257, True), ('SCC', 0o277, True),
                           ('BR', 0o400, True), ('BEQ', 0o1400, True), ('BNE', 0o1000, False)):
        out[(prefix + 'BR', name, 0)] = ('br', w, taken)
    return out

def summary(emu, m1, m3, tests=None):
    per = collections.defaultdict(lambda: ([], [], []))
    for k, v in emu.items():
        if tests and k[0] not in tests: continue
        a, b = m1.get(k), m3.get(k)
        t = per[k[0]]
        if a is not None: t[0].append(v - a)
        if b is not None: t[1].append(v - b)
        if a is not None and b is not None: t[2].append(v - (a + b) / 2)
    f = lambda l: '%6.2f %5.2f %4d' % (sum(l) / len(l), sum(abs(x) for x in l) / len(l), len(l)) if l else '%17s' % '-'
    print('%-8s %-17s   %-17s   %-17s' % ('', 'к 001 сдвиг ошибка', 'к 031', 'к среднему'))
    for t in sorted(per):
        print('%-8s %s   %s   %s' % (t, f(per[t][0]), f(per[t][1]), f(per[t][2])))

if __name__ == '__main__':
    summary(emulator(sys.argv[1]), machine('001'), machine('031'), set(sys.argv[2:]) or None)
