# Проверка модели цепочки на памяти КЦГД: тот же расчёт, что в
# Vm1BusTiming (timing = vm2), по шаблонам table.pkl, против прогонов тех же
# программ на модели кристалла с окном (WINDOW=1 run_all.py, wlog_*.txt).
# Каждый случай: от границы третьей копии до границы последней; начальная
# точка - отпускание строба перед ней по трассе, окна - события Z трассы.
#
#   model.py all:0 pair:0 ...
import json, pickle, sys, collections
import extract

DEBUG = None
# формы, чей хвост идёт и во время ожидания их последнего цикла
TABLE = None
OVERLAP = lambda key: all(st[1] in 'FI' for st in TABLE[key]['steps'])
RELEASE = {'R': 4, 'W': 3}
# наименьший промежуток от отпускания строба до следующего SYNC: после
# записи шина свободна на такт позже
GMIN = {'R': 3, 'W': 4, 'M': 4}

def release(strobe, kind, slow, zs):
    """отпускание строба, начатого в strobe (такты CLC)"""
    rply = strobe
    if slow:
        k = zs(strobe)                      # первая точка окна позже строба
        rply = k + 4                        # RPLY в конце следующего знакоместа
    d = rply - strobe
    d += d & 1
    return strobe + d + RELEASE[kind]

# Начало команды и что ей позволяет шина - как в Vm1BusTiming (timing = vm2):
# хвост от строба последнего цикла у формы без данных, иначе от отпускания;
# «быстрая» форма (свой промежуток меньше 3) начинает так только после формы
# без данных, иначе вся команда сдвигается; после чтения данных, которым
# кончилась прошлая команда, шина ждёт 7 тактов
def start(prev, key, anchor, rel_prev, tail, last_kind):
    free = OVERLAP(prev)
    s0 = (anchor if free else rel_prev) + tail
    g0 = TABLE[key]['steps'][0][2]
    if not free and g0 < 3: s0 += 3 - g0
    last = TABLE[prev]['steps'][-1]
    bus = rel_prev + (7 if (last[0] == 'R' and last[1] == 'D') else GMIN[last_kind])
    return s0, bus

def run(prefix, part, table, window=True):
    global TABLE
    TABLE = table
    cases = json.load(open('%s%s.json' % (prefix, part)))
    mem = [int(l, 16) for l in open('%s%s.mem' % (prefix, part))]
    mode = int(window)
    log = ({0: 'log_', 1: 'wlog_', 2: 'dlog_', 3: 'clog_'}[mode] + '%s%s.txt') % (prefix, part)
    trs = extract.transactions(log)
    zt = [round(float(l.split()[3])) for l in open(log) if l.startswith('E Z')] or [0]
    z0, zp = zt[0], 12
    def zs(t):
        n = (t - z0) // zp + 1
        return z0 + n * zp
    res = []
    pos = 0
    for c in cases:
        if c['kind'] == 'interrupt': continue
        flow = c['flow']
        bounds = []
        for a in flow:
            while pos < len(trs) and not (extract.is_fetch(trs[pos]) and trs[pos]['addr'] == a + 2):
                pos += 1
            if pos >= len(trs): break
            bounds.append(pos); pos += 1
        if len(bounds) != len(flow): continue
        pos = bounds[-1]
        first = flow.index(c['copies'][2])
        last = len(flow) - 1 - flow[::-1].index(c['copies'][-1])
        # ключи форм
        keys = []
        for j, a in enumerate(flow):
            w = mem[a >> 1]
            taken = None
            k0 = extract.form_key(w)[0]
            if k0 == 'SOB': taken = flow[j + 1] != a + 2 if j + 1 < len(flow) else False
            elif k0 == 'BR': taken = 'taken' in c['name']
            keys.append(extract.form_key(w, taken, c['name']))
        if any(k not in table for k in keys[first - 1:last]): continue
        rel_prev = trs[bounds[first] - 1]['rel'][-1]
        last_kind = table[keys[first - 1]]['steps'][-1][0]
        tail = table[keys[first - 1]]['tail']
        prevt = trs[bounds[first] - 1]
        anchor = (prevt['I'] if prevt['O'] is None else prevt['O']) + (4 if prevt['O'] is None else 3)
        for j in range(first, last):
            f = table[keys[j]]
            t = rel_prev
            s0, bus = start(keys[j - 1], keys[j], anchor, rel_prev, tail, last_kind)
            seg = trs[bounds[j]:bounds[j + 1]]
            for i, st in enumerate(f['steps']):
                kind, role, gap, strobe_off, r2w = st
                sync = t + gap
                if i == 0: sync = max(s0 + gap, bus)
                strobe = sync + strobe_off
                if kind == 'W' and r2w: strobe = max(strobe, s0 + r2w)
                # медленно ли - по адресу того же цикла в трассе
                addr = seg[i]['addr'] if i < len(seg) else 0
                # как в эмуляторе: память шага - по его роли (код - I и F,
                # данные - D по адресу из трассы, если он там того же вида)
                if role in 'IF': code = True
                elif role == 'N': code = None
                else: code = not (addr is not None and addr >= 0o060000)
                slow = code is not None and ((mode == 1 and (code or addr < 0o100000)) or
                                             (mode == 2 and not code and addr is not None and addr < 0o100000) or
                                             (mode == 3 and code))
                if DEBUG == c['name'] and i < len(seg):
                    print(' ', keys[j], i, st, 'pred S', sync, 'real S', seg[i]['S'], 'addr', oct(addr or 0))
                if role == 'N':
                    t = strobe + r2w
                elif kind == 'M':
                    rel = release(strobe, 'R', slow, zs)
                    t = release(rel + r2w, 'W', slow, zs)
                else:
                    t = release(strobe, kind, slow, zs)
                anchor = strobe + RELEASE['R' if kind == 'R' else 'W']
            rel_prev, last_kind, tail = t, f['steps'][-1][0], f['tail']
        # первая выборка последней команды
        s0, bus = start(keys[last - 1], keys[last], anchor, rel_prev, tail, last_kind)
        pred = max(s0 + table[keys[last]]['steps'][0][2], bus)
        real = trs[bounds[last]]['S']
        res.append((c['name'], pred - real, real - trs[bounds[first]]['S']))
    return res

if __name__ == '__main__':
    table = pickle.load(open('table.pkl', 'rb'))
    TABLE = table
    allres = []
    window = True
    for spec in sys.argv[1:]:
        if spec == 'fast': window = 0; continue
        if spec.startswith('mode='): window = int(spec[5:]); continue
        if spec.startswith('debug='): DEBUG = spec[6:]; continue
        prefix, part = spec.split(':')
        allres += run(prefix, part, table, window)
    ok = sum(1 for r in allres if r[1] == 0)
    print('совпало', ok, 'из', len(allres))
    dist = collections.Counter(r[1] for r in allres)
    print('расхождения (модель - кристалл):', sorted(dist.items()))
    bad = [r for r in allres if r[1] != 0]
    for r in bad[:40]: print('  ', r)
