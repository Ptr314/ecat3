# Программы со всеми формами команд К1801ВМ2, по частям, чтобы влезть в
# область кода. Случай: преамбула, потом COPIES копий команды (у некоторых
# форм перед каждой копией своя подготовка, после копий - обработчик).
# Область данных 060000-077777 заполнена FILL.
#
#   gen_all.py           - копии подряд (all*.mem, all*.json)
#   PAIR=1 gen_all.py    - после каждой копии SEC (pair*.mem, pair*.json)
#
# Поток команд каждого случая (адреса выполненных команд по порядку) пишется
# в json: у ВМ2 выборка следующего слова идёт раньше данных текущей команды,
# и граница команды в трассе находится только по нему (extract.py).
import json, sys, os
PAIR = os.environ.get('PAIR') == '1'
PREFIX = 'pair' if PAIR else 'all'

COPIES = 8
FILL = 0o070000
CODE_LIMIT = 0o060000
TABLES = 0o074000          # таблицы указателей RTS/RTI/JMP @(R)+
REGS = {0: 0o062000, 1: 0o064000, 2: 0o066000, 3: 0o0, 4: 0o072000, 5: 0o073000}

# режимы операндов: (имя, режим, регистр, вид слова после кода)
MODES = [('R', 0, None, None), ('(R)', 1, None, None), ('(R)+', 2, None, None),
         ('@(R)+', 3, None, None), ('-(R)', 4, None, None), ('@-(R)', 5, None, None),
         ('X(R)', 6, None, 'x0'), ('@X(R)', 7, None, 'x0'),
         ('#n', 2, 7, 'imm'), ('@#a', 3, 7, 'abs'), ('a', 6, 7, 'rel'), ('@a', 7, 7, 'rel')]

DOUBLE = {'MOV': 0o01, 'CMP': 0o02, 'BIT': 0o03, 'BIC': 0o04, 'BIS': 0o05, 'ADD': 0o06,
          'MOVB': 0o11, 'CMPB': 0o12, 'BITB': 0o13, 'BICB': 0o14, 'BISB': 0o15, 'SUB': 0o16}
SINGLE = {'CLR': 0o0050, 'COM': 0o0051, 'INC': 0o0052, 'DEC': 0o0053, 'NEG': 0o0054,
          'ADC': 0o0055, 'SBC': 0o0056, 'TST': 0o0057, 'ROR': 0o0060, 'ROL': 0o0061,
          'ASR': 0o0062, 'ASL': 0o0063, 'SWAB': 0o0003, 'SXT': 0o0067,
          'CLRB': 0o1050, 'COMB': 0o1051, 'INCB': 0o1052, 'DECB': 0o1053, 'NEGB': 0o1054,
          'ADCB': 0o1055, 'SBCB': 0o1056, 'TSTB': 0o1057, 'RORB': 0o1060, 'ROLB': 0o1061,
          'ASRB': 0o1062, 'ASLB': 0o1063, 'MTPS': 0o1064, 'MFPS': 0o1067}
# Команды EIS: приёмник - R4 (с R5 у MUL, DIV, ASHC), источник - любой.
# DIV перед каждой копией получает делимое без переполнения (DIVV - с ним);
# у ASH и ASHC число сдвигов в данных - 0, сдвиг стоит своё сверх шаблона
EIS = {'MUL': 0o070, 'DIV': 0o071, 'ASH': 0o072, 'ASHC': 0o073, 'DIVV': 0o071}

def operand(mi, reg):
    name, mode, r, kind = MODES[mi]
    return mode, (r if r is not None else reg), kind

def forms():
    out = []
    for op, code in DOUBLE.items():
        for si in range(12):
            for di in range(12):
                if di == 8: continue
                out.append(dict(name='%s %s,%s' % (op, MODES[si][0], MODES[di][0]),
                                kind='double', op=code, src=si, dst=di))
    for op, code in SINGLE.items():
        for di in range(12):
            if di == 8 and op not in ('TST', 'TSTB', 'MTPS'): continue
            out.append(dict(name='%s %s' % (op, MODES[di][0]), kind='single', op=code, dst=di))
    for op, code in EIS.items():
        for si in range(12):
            out.append(dict(name='%s %s,R4' % (op, MODES[si][0]), kind='eis', eis=op, op=code, src=si))
    for di in range(12):
        if di == 8: continue
        out.append(dict(name='XOR R,%s' % MODES[di][0], kind='xor', dst=di))
    for cond, code, taken in (('BR', 0o000400, 1), ('BNE', 0o001000, 1), ('BEQ', 0o001400, 0),
                              ('BCS', 0o103400, 1), ('BCC', 0o103000, 0), ('BPL', 0o100000, 1)):
        out.append(dict(name='%s %s' % (cond, 'taken' if taken else 'not'), kind='branch', op=code))
    for mode in ('@#a', 'X(R)', '@X(R)', '@(R)+', 'a'):
        out.append(dict(name='JMP ' + mode, kind='jmp', mode=mode))
    for mode in ('@#a', 'X(R)', '@X(R)', '@(R)+', 'a'):
        out.append(dict(name='JSR PC,' + mode, kind='jsr', mode=mode))
    out.append(dict(name='RTS PC', kind='rts'))
    out.append(dict(name='RTI', kind='rti'))
    out.append(dict(name='RTT', kind='rtt'))
    out.append(dict(name='SOB loop', kind='sob'))
    out.append(dict(name='EMT+RTI', kind='emt'))
    out.append(dict(name='TRAP+RTI', kind='trap'))
    out.append(dict(name='IOT+RTI', kind='iot'))
    out.append(dict(name='BPT+RTI', kind='bpt'))
    out.append(dict(name='NOP', kind='word', word=0o000240))
    out.append(dict(name='SEC', kind='word', word=0o000261))
    out.append(dict(name='MARK 0', kind='mark'))
    return out

def emit_operand(a, kind):
    if kind == 'x0': return 0
    if kind == 'imm': return 1
    if kind == 'abs': return FILL
    if kind == 'rel': return (FILL - (a + 2)) & 0xFFFF
    return None

class Prog:
    def __init__(self):
        self.mem = [FILL] * 32768
        self.a = 0o000100
        self.tab = TABLES
        self.flow = []
    def w(self, v):
        self.mem[self.a >> 1] = v & 0xFFFF; self.a += 2
    def tw(self, v):
        self.mem[self.tab >> 1] = v & 0xFFFF; self.tab += 2
        return self.tab - 2
    def ins(self, *words):
        """команда, выполняемая по порядку"""
        self.flow.append(self.a)
        for v in words: self.w(v)

def build_one(p, f):
    """одна команда формы без подготовки: для форм, которым она не нужна"""
    kind = f['kind']
    if kind == 'double':
        ms, rs, ks = operand(f['src'], 1); md, rd, kd = operand(f['dst'], 2)
        words = [(f['op'] << 12) | (ms << 9) | (rs << 6) | (md << 3) | rd]
        at = p.a + 2
        if ks: words.append(emit_operand(at, ks)); at += 2
        if kd: words.append(emit_operand(at, kd))
        p.ins(*words)
    elif kind == 'single':
        md, rd, kd = operand(f['dst'], 2)
        words = [(f['op'] << 6) | (md << 3) | rd]
        if kd: words.append(emit_operand(p.a + 2, kd))
        p.ins(*words)
    elif kind == 'eis':
        ms, rs, ks = operand(f['src'], 1)
        words = [(f['op'] << 9) | (4 << 6) | (ms << 3) | rs]
        if ks: words.append(0 if ks == 'imm' and f['eis'] in ('ASH', 'ASHC') else emit_operand(p.a + 2, ks))
        p.ins(*words)
    elif kind == 'xor':
        md, rd, kd = operand(f['dst'], 2)
        words = [0o074000 | (1 << 6) | (md << 3) | rd]
        if kd: words.append(emit_operand(p.a + 2, kd))
        p.ins(*words)
    elif kind == 'branch':
        p.ins(f['op'] | 0)
    elif kind == 'word':
        p.ins(f['word'])
    else:
        raise ValueError(kind)

def build_case(p, f):
    start = p.a
    p.flow = []
    table_needed = f['kind'] in ('rts', 'rti', 'rtt') or \
        (f['kind'] in ('jmp', 'jsr') and f['mode'] in ('@(R)+',))
    p.ins(0o005037, 0o177702)             # CLR @#177702: стенд восстанавливает данные
    pre_at = p.a
    for r, v in REGS.items():
        p.ins(0o012700 | r, v)
    p.ins(0o012706, 0o077000)             # SP
    sp_fix = p.a - 2
    r3_fix = pre_at + 3 * 4 + 2           # слово значения MOV #..,R3
    p.ins(0o000261)                        # SEC
    copies = []
    traps = []
    kind = f['kind']
    for k in range(COPIES):
        if kind == 'eis' and f['eis'] in ('DIV', 'DIVV'):
            # делимое: без переполнения 0:5, с ним 077777:0
            p.ins(0o012704, 0 if f['eis'] == 'DIV' else 0o077777)
            p.ins(0o012705, 5 if f['eis'] == 'DIV' else 0)
            p.ins(0o000261)
        copies.append(p.a)
        if kind == 'double':
            ms, rs, ks = operand(f['src'], 1); md, rd, kd = operand(f['dst'], 2)
            words = [(f['op'] << 12) | (ms << 9) | (rs << 6) | (md << 3) | rd]
            at = p.a + 2
            if ks: words.append(emit_operand(at, ks)); at += 2
            if kd: words.append(emit_operand(at, kd))
            p.ins(*words)
        elif kind == 'single':
            md, rd, kd = operand(f['dst'], 2)
            words = [(f['op'] << 6) | (md << 3) | rd]
            if kd: words.append(emit_operand(p.a + 2, kd))
            p.ins(*words)
        elif kind == 'eis':
            ms, rs, ks = operand(f['src'], 1)
            words = [(f['op'] << 9) | (4 << 6) | (ms << 3) | rs]
            if ks: words.append(0 if ks == 'imm' and f['eis'] in ('ASH', 'ASHC') else emit_operand(p.a + 2, ks))
            p.ins(*words)
        elif kind == 'xor':
            md, rd, kd = operand(f['dst'], 2)
            words = [0o074000 | (1 << 6) | (md << 3) | rd]
            if kd: words.append(emit_operand(p.a + 2, kd))
            p.ins(*words)
        elif kind == 'branch':
            p.ins(f['op'] | 0)            # смещение 0: следующая команда в обоих случаях
        elif kind == 'word':
            p.ins(f['word'])
        elif kind in ('jmp', 'jsr'):
            base = 0o000100 if kind == 'jmp' else 0o004700
            m = f['mode']
            if m == '@#a':
                p.ins(base | 0o37, p.a + 4)
            elif m == 'X(R)':             # R3 = 0: X - сам адрес перехода
                p.ins(base | 0o63, p.a + 4)
            elif m == '@X(R)':            # R3 = 0: X - адрес указателя
                ptr = p.tw(0)
                p.ins(base | 0o73, ptr)
                p.mem[ptr >> 1] = p.a
            elif m == '@(R)+':            # R3 идёт по таблице адресов
                p.ins(base | 0o33)
                p.tw(p.a)
            elif m == 'a':                # относительный, X(PC) = 0
                p.ins(base | 0o67, 0)
        elif kind == 'rts':
            p.ins(0o000207); p.tw(p.a)
        elif kind in ('rti', 'rtt'):
            p.ins(0o000002 if kind == 'rti' else 0o000006); p.tw(p.a); p.tw(0o000000)
        elif kind == 'sob':
            p.ins(0o012704, 3)            # MOV #3,R4 ; SOB R4,. - три раза
            p.flow += [p.a, p.a]
            p.ins(0o077401)
        elif kind in ('emt', 'trap', 'iot', 'bpt'):
            p.ins({'emt': 0o104000, 'trap': 0o104400, 'iot': 0o000004, 'bpt': 0o000003}[kind])
            traps.append(len(p.flow))     # сюда встанет обработчик
        elif kind == 'mark':
            # MARK 0 берёт PC из R5: R5 = следующая команда
            p.ins(0o012705, p.a + 6)
            p.ins(0o006400)
            p.ins(0o012706, 0o077000)
        if PAIR:
            p.ins(0o000261)               # SEC
    end = p.a
    if kind in ('emt', 'trap', 'iot', 'bpt'):
        p.w(0o000401)                     # BR через обработчик
        handler = p.a
        p.w(0o000002)                     # RTI
        vec = {'emt': 0o30, 'trap': 0o34, 'iot': 0o20, 'bpt': 0o14}[kind]
        f['vector'] = (vec, handler)
        for i in reversed(traps):
            p.flow.insert(i, handler)
    if table_needed:
        first = (p.tab - (2 * COPIES if kind not in ('rti', 'rtt') else 4 * COPIES))
        if kind in ('rts', 'rti', 'rtt'):
            p.mem[sp_fix >> 1] = first
        else:
            p.mem[r3_fix >> 1] = first
    f['copies'] = copies
    f['start'] = start
    f['end'] = end
    f['flow'] = list(p.flow)

def main():
    parts = []
    p = Prog()
    part_forms = []
    for f in forms():
        save = (p.a, p.tab, list(p.mem))
        build_case(p, f)
        if p.a > CODE_LIMIT - 256 or p.tab > 0o077000 - 64:
            parts.append((p, part_forms)); p = Prog(); part_forms = []
            build_case(p, f)
        part_forms.append(f)
    parts.append((p, part_forms))
    for n, (p, pf) in enumerate(parts):
        vecs = {}
        for f in pf:
            if 'vector' in f:
                if f['vector'][0] in vecs:
                    sys.exit('two cases on one vector in part %d' % n)
                vecs[f['vector'][0]] = f['vector'][1]
        for w in [0o012737, 1, 0o177700, 0o000777]:
            p.w(w)
        p.mem[0] = 0o000100; p.mem[1] = 0o000000      # пуск ВМ2: PC и PSW из 000000
        for v, h in vecs.items():
            p.mem[v >> 1] = h; p.mem[(v >> 1) + 1] = 0
        json.dump(pf, open('%s%d.json' % (PREFIX, n), 'w'))
        with open('%s%d.mem' % (PREFIX, n), 'w') as fh:
            for w in p.mem: fh.write('%04x\n' % w)
        print('part', n, len(pf), 'forms, code to', oct(p.a), 'vectors', {oct(k): oct(v) for k, v in vecs.items()})

if __name__ == '__main__':
    main()
