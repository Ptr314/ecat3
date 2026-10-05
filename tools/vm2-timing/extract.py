# Шаблоны циклов шины каждой формы по трассам со статической памятью.
#
# У ВМ2 команда, получив свой код (выбранный предыдущей командой), первым
# делом выбирает слово за ним - своё слово операнда или код следующей
# команды - и только потом обращается к данным. Поэтому шаблон команды по
# адресу A - циклы от выборки A+2 до выборки A'+2 следующей выполненной
# команды A' (не включая её). Порядок выполнения берётся из json генератора
# (flow): в трассе его не отличить от упреждающих выборок.
#
# Шаг: вид R/W/M, роль I (своё слово команды), D (данные), F (выборка дальше
# своих слов: код следующей команды, упреждающая выборка, адрес перехода),
# gap - тактов CLC от отпускания строба прошлого цикла до SYNC, strobe - от
# SYNC до DIN/DOUT, r2w - от отпускания DIN до DOUT (только M).
import json, sys, collections, pickle

DATA_START = 0o060000

def ext(spec):
    m, r = (spec >> 3) & 7, spec & 7
    return 1 if m in (6, 7) or (r == 7 and m in (2, 3)) else 0

def insn_len(w):
    op = w >> 12
    if op in (1, 2, 3, 4, 5, 6, 0o11, 0o12, 0o13, 0o14, 0o15, 0o16):
        return 1 + ext((w >> 6) & 0o77) + ext(w & 0o77)
    if (w & 0o170000) == 0o070000 and (w & 0o007000) != 0o007000:   # MUL DIV ASH ASHC XOR
        return 1 + ext(w & 0o77)
    if (w & 0o177000) == 0o004000:                                   # JSR
        return 1 + ext(w & 0o77)
    if (w & 0o077700) in (0o000100, 0o000300) or (w & 0o077000) in (0o005000, 0o006000):
        return 1 + ext(w & 0o77)                                     # JMP, SWAB, одноадресные
    if (w & 0o177700) in (0o006700, 0o106400, 0o106700):             # SXT, MTPS, MFPS
        return 1 + ext(w & 0o77)
    return 1

def spec_index(spec):
    m, r = (spec >> 3) & 7, spec & 7
    return m * 2 + (1 if r == 7 else 0)

DOUBLE_OPS = [1, 2, 3, 4, 5, 6, 0o11, 0o12, 0o13, 0o14, 0o15, 0o16]
SINGLE_OPS = [0o0050, 0o0051, 0o0052, 0o0053, 0o0054, 0o0055, 0o0056, 0o0057, 0o0060, 0o0061,
              0o0062, 0o0063, 0o0003, 0o0067, 0o1050, 0o1051, 0o1052, 0o1053, 0o1054, 0o1055,
              0o1056, 0o1057, 0o1060, 0o1061, 0o1062, 0o1063, 0o1064, 0o1067]
EIS_OPS = [0o070, 0o071, 0o072, 0o073]

def form_key(w, taken=None, case=None):
    op = w >> 12
    if op in DOUBLE_OPS:
        return ('D', DOUBLE_OPS.index(op), spec_index((w >> 6) & 0o77), spec_index(w & 0o77))
    if (w >> 6) in SINGLE_OPS:
        return ('S', SINGLE_OPS.index(w >> 6), spec_index(w & 0o77))
    if (w >> 9) in EIS_OPS:
        e = EIS_OPS.index(w >> 9)
        if e == 1 and case is not None and case.startswith('DIVV'): e = 4    # DIV с переполнением
        return ('E', e, spec_index(w & 0o77))
    if (w & 0o177000) == 0o074000: return ('XOR', spec_index(w & 0o77))
    if (w & 0o177700) == 0o000100: return ('JMP', spec_index(w & 0o77))
    if (w & 0o177000) == 0o004000: return ('JSR', spec_index(w & 0o77))
    if (w & 0o177770) == 0o000200: return ('RTS',)
    if (w & 0o177000) == 0o077000: return ('SOB', taken)
    b = w & 0o177400
    if 0o000400 <= b <= 0o003400 or 0o100000 <= b <= 0o103400:
        return ('BR', taken)
    if (w & 0o177400) in (0o104000, 0o104400): return ('EMT',)
    if w in (0o000003, 0o000004): return ('EMT',)
    if w == 0o000002: return ('RTI',)
    if w == 0o000006: return ('RTT',)
    if (w & 0o177700) == 0o006400: return ('MARK',)
    if (w & 0o177740) == 0o000240: return ('CC', 1 if w & 0o20 else 0)
    return ('?', w)

def transactions(log):
    out = []
    cur = None
    for l in open(log):
        p = l.split()
        if not p or p[0] != 'E' or 'x' in p[2] or 'z' in p[2]: continue
        k, a, t = p[1], int(p[2], 8), round(float(p[3]))
        if k == 'S':
            cur = dict(addr=a, S=t, I=None, O=None, rel=[], A=False)
            out.append(cur)
        elif k == 'Q':                       # DIN без адреса: IAKO или безадресное чтение
            cur = dict(addr=None, S=None, I=t, O=None, rel=[], A=False, una=True)
            out.append(cur)
        elif cur is not None:
            if k == 'I' and cur['I'] is None: cur['I'] = t
            elif k == 'O' and cur['O'] is None: cur['O'] = t
            elif k in 'io': cur['rel'].append(t)
            elif k == 'A': cur['A'] = True
    return [t for t in out if t['rel']]

def step_of(t, prev_rel, role):
    if t['S'] is None:
        # безадресное чтение (IAKO, SEL): без SYNC; в r2w - от DIN до отпускания
        return ('R', role, t['I'] - prev_rel, 0, t['rel'][-1] - t['I'])
    start = t['S']
    if t['I'] is not None and t['O'] is not None:
        return ('M', role, start - prev_rel, t['I'] - start, t['O'] - t['rel'][0])
    if t['I'] is not None:
        return ('R', role, start - prev_rel, t['I'] - start, 0)
    return ('W', role, start - prev_rel, t['O'] - start, 0)

def is_fetch(t):
    return t['addr'] is not None and t['I'] is not None and t['O'] is None and \
        0o100 <= t['addr'] < DATA_START

def interrupts(c, trs, mem):
    """вход в прерывание: от безадресного чтения после записи в маркер до
    выборки кода обработчика включительно. Предшественник - MOV R0,@#a"""
    out = []
    name = 'INT' if c['name'] == 'IRQ' else 'VINT'
    vec = 0o100 if c['name'] == 'IRQ' else 0o340
    pred = form_key(mem[c['copies'][0] >> 1])
    lo = c['copies'][2]
    for i, t in enumerate(trs):
        if t['addr'] != vec or t['I'] is None: continue
        j = i
        while j > 0 and not (trs[j - 1]['O'] is not None and trs[j - 1]['addr'] in (0o177704, 0o177706)):
            j -= 1
        k = i
        while k < len(trs) and trs[k]['addr'] != c['handler']: k += 1
        if trs[j - 1]['S'] < 0 or k >= len(trs): continue
        # только копии с третьей
        if not any(trs[q]['addr'] is not None and lo <= trs[q]['addr'] < c['end'] for q in range(max(0, j - 4), j)):
            continue
        prev_rel = trs[j - 1]['rel'][-1]
        steps = []
        for t2 in trs[j:k + 1]:
            if t2.get('una'): role = 'N'
            elif t2['addr'] == c['handler']: role = 'F'
            else: role = 'D'
            steps.append(step_of(t2, prev_rel, role))
            prev_rel = t2['rel'][-1]
        out.append((c['name'], pred, (name,), tuple(steps)))
    return out

def occurrences(prefix, part):
    cases = json.load(open('%s%s.json' % (prefix, part)))
    mem = [int(l, 16) for l in open('%s%s.mem' % (prefix, part))]
    trs = transactions('log_%s%s.txt' % (prefix, part))
    out = []
    pos = 0
    for c in cases:
        if c['kind'] == 'interrupt':
            out += interrupts(c, trs, mem)
            continue
        flow = c['flow']
        # граница каждой выполненной команды: первая выборка A+2 после прошлой
        bounds = []
        for a in flow:
            want = a + 2
            while pos < len(trs) and not (is_fetch(trs[pos]) and trs[pos]['addr'] == want):
                pos += 1
            if pos >= len(trs):
                print('lost', prefix, part, c['name'], oct(a)); break
            bounds.append(pos)
            pos += 1
        if len(bounds) != len(flow): continue
        pos = bounds[-1]
        # измеряются копии с третьей по последнюю (как у ВМ1): их окружение одно
        first = flow.index(c['copies'][2])
        last = len(flow) - 1 - flow[::-1].index(c['copies'][-1])
        prev_key = None
        for j in range(first - 1, last):
            a = flow[j]
            w = mem[a >> 1]
            n = insn_len(w)
            own = set(range(a + 2, a + 2 * n, 2))
            # дальше своих слов выбирается только код следующей выполненной
            # команды и слово сразу за этой (упреждающая выборка); прочие
            # чтения в области кода - данные по указателю
            ahead = {flow[j + 1], a + 2 * n}
            seg = trs[bounds[j]:bounds[j + 1]]
            prev_rel = trs[bounds[j] - 1]['rel'][-1]
            steps = []
            for t in seg:
                if t.get('una'): role = 'N'
                elif is_fetch(t) and t['addr'] in own: role = 'I'
                elif is_fetch(t) and t['addr'] in ahead: role = 'F'
                else: role = 'D'
                steps.append(step_of(t, prev_rel, role))
                prev_rel = t['rel'][-1]
            nxt = flow[j + 1]
            taken = None
            k0 = form_key(w)[0]
            if k0 == 'SOB': taken = (nxt != a + 2)
            elif k0 == 'BR': taken = 'taken' in c['name']
            key = form_key(w, taken, c['name'])
            if j >= first:
                out.append((c['name'], prev_key, key, tuple(steps)))
            prev_key = key
    return out

if __name__ == '__main__':
    occ = []
    for spec in sys.argv[1:]:
        prefix, part = spec.split(':')
        occ += occurrences(prefix, part)
    pickle.dump(occ, open('occ.pkl', 'wb'))
    print(len(occ), 'occurrences')
