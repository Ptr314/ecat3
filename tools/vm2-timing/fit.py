# Точка отсчёта каждого шага. ВМ2 ждёт и шину, и свою микропрограмму:
#   SYNC шага = max(шина свободна, база + смещение),
# где «шина свободна» - отпускание прошлого цикла + 3 такта после чтения (4
# после записи), а база - момент, от которого считает микропрограмма: начало
# команды s0 или отпускание одного из её прошлых циклов (данные, которых она
# ждёт). DOUT записи - так же, max(SYNC + 5, база + смещение); у
# чтения-модификации-записи - max(отпускание чтения + r2w, база + смещение).
# Начало следующей команды s0' = база конца + смещение, база - отпускание или
# строб одного из циклов этой команды (строб + 4 - когда микропрограмма не
# ждёт свой последний цикл).
#
# Подбор - по трассам всех четырёх режимов памяти (log_, wlog_, dlog_, clog_):
# время события предсказывается по настоящим временам прошлых событий той же
# трассы, так что каждый шаг проверяется отдельно, и выбирается база, при
# которой совпадает больше всего экземпляров. Условность: s0 команды после
# SEC - строб SEC + 8 (тогда смещение первого шага - «свой промежуток» из
# table.pkl), у SEC конец - строб + 8.
#
#   fit.py              - подбор, fit.pkl и отчёт
import json, pickle, sys, collections
import extract

PREFIXES = {0: 'log_', 1: 'wlog_', 2: 'dlog_', 3: 'clog_'}
PARTS = [('all', n) for n in range(6)] + [('pair', n) for n in range(7)] + [('x', 0)]
GMIN = {'R': 3, 'W': 4, 'M': 4}
# на стыке команд: после чтения данных, которым кончилась команда, - 7
# «быстрая» форма (свой промежуток 1) начинает раньше обычной (3) только
# после формы без обращений к данным, как SEC; после формы с данными - как все
def early_ok(table, key):
    return all(x[1] in 'FI' for x in table[key]['steps'])
def off_after(table, prev_key, off):
    return off if early_ok(table, prev_key) else max(off, 3)
def shift(table, prev_key, off):
    """насколько сдвигается вся команда, когда ей нельзя начать рано"""
    return off_after(table, prev_key, off) - off
def set_s0e(table, off0, i):
    i['s0e'] = None if i.get('s0') is None else i['s0'] + shift(table, i['prev']['key'], off0[i['key']])
def gfirst(step):
    return 7 if (step['kind'] == 'R' and step['role'] == 'D') else GMIN[step['kind']]
SEC = ('CC', 1)
SEC_END = ('st', 0, 8)          # конец SEC: строб её выборки + 8

ZGRID = {}
TRACE_FWD = [0] if '--fwd' in sys.argv else []
EARLY_RULE = '--early' in sys.argv
TABLE_G = None
PAIRS = collections.Counter()
BAD = {False: [], True: []}

def instances(mode, prefix, part):
    """экземпляры команд по порядку выполнения: key, шаги с временами"""
    cases = json.load(open('%s%s.json' % (prefix, part)))
    mem = [int(l, 16) for l in open('%s%s.mem' % (prefix, part))]
    log = '%s%s%s.txt' % (PREFIXES[mode], prefix, part)
    trs = extract.transactions(log)
    zt = [round(float(l.split()[3])) for l in open(log) if l.startswith('E Z')]
    ZGRID[(mode, prefix, part)] = zt[0] if zt else 0
    out = []
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
        seq = []
        for j in range(len(flow) - 1):
            a = flow[j]
            w = mem[a >> 1]
            n = extract.insn_len(w)
            own = set(range(a + 2, a + 2 * n, 2))
            ahead = {flow[j + 1], a + 2 * n}
            taken = None
            k0 = extract.form_key(w)[0]
            if k0 == 'SOB': taken = flow[j + 1] != a + 2
            elif k0 == 'BR': taken = 'taken' in c['name']
            key = extract.form_key(w, taken, c['name'])
            steps = []
            for t in trs[bounds[j]:bounds[j + 1]]:
                if t.get('una'):
                    steps.append(dict(kind='R', role='N', S=t['I'], st=t['I'], rel=t['rel'][-1]))
                    continue
                role = 'I' if (extract.is_fetch(t) and t['addr'] in own) else \
                       'F' if (extract.is_fetch(t) and t['addr'] in ahead) else 'D'
                if t['I'] is not None and t['O'] is not None:
                    steps.append(dict(kind='M', role=role, S=t['S'], st=t['I'], rrel=t['rel'][0],
                                      dout=t['O'], rel=t['rel'][-1], addr=t['addr']))
                elif t['I'] is not None:
                    steps.append(dict(kind='R', role=role, S=t['S'], st=t['I'], rel=t['rel'][-1], addr=t['addr']))
                else:
                    steps.append(dict(kind='W', role=role, S=t['S'], st=t['O'], dout=t['O'], rel=t['rel'][-1], addr=t['addr']))
            seq.append(dict(key=key, steps=steps, mode=mode, case=c['name'], addr=a,
                            cid=(mode, prefix, part, c['name'], c['start'])))
        for j in range(1, len(seq)):
            seq[j]['prev'] = seq[j - 1]
        first = flow.index(c['copies'][2])
        last = len(flow) - 1 - flow[::-1].index(c['copies'][-1])
        if 0 < first and last < len(seq):
            seq[first]['check_from'] = True
            seq[last]['check_to'] = True
        out += seq[1:]
    return out

def base_time(inst, base):
    """время базы в экземпляре: ('s0',), ('rel', j), ('st', j, плюс)"""
    if base[0] == 's0': return inst.get('s0e', inst.get('s0'))
    if base[0] == 'rel': return inst['steps'][base[1]]['rel']
    if base[0] == 'rrel': return inst['steps'][base[1]]['rrel']
    return None

def end_time(inst, end):
    """s0 следующей команды по базе конца этой"""
    kind, j, off = end
    s = inst['steps']
    if j >= len(s): return None
    return (s[j]['rel'] if kind == 'rel' else s[j]['st'] + 4) + off

def check_only(specs):
    """проверка на программах, которых не было в подборе (gen_mix.py)"""
    global TABLE_G
    table = pickle.load(open('table.pkl', 'rb'))
    TABLE_G = table
    f = pickle.load(open('fit.pkl', 'rb'))
    params, ends, off0 = f['params'], f['ends'], f['off0']
    data = []
    for mode in range(4):
        for spec in specs:
            prefix, part = spec.split(':')
            data += instances(mode, prefix, part)
    def fits(inst):
        t = table.get(inst['key'])
        return t is not None and len(t['steps']) == len(inst['steps']) and             all(a[0] == b['kind'] and a[1] == b['role'] for a, b in zip(t['steps'], inst['steps']))
    good = [i for i in data if i['key'] in table and 'prev' in i]
    print('экземпляров', len(good), 'не по шаблону', sum(1 for i in good if not fits(i)))
    for i in good:
        e = ends.get(i['prev']['key'])
        if e is None: i['s0'] = None; continue
        def one(e):
            kind, j, off = e
            if kind == 's0': return None
            st = i['prev']['steps']
            if j >= len(st): return None
            return (st[j]['rel'] if kind == 'rel' else st[j]['st'] + 4) + off
        if isinstance(e[0], tuple):
            a, b = one(e[0]), one(e[1]); i['s0'] = None if a is None or b is None else max(a, b)
        else: i['s0'] = one(e)
    for i in good:
        if i['key'] in off0: set_s0e(table, off0, i)
    # локально: первый SYNC и шаги по настоящим временам
    GM = GMIN
    fs = collections.Counter(); st_bad = collections.Counter(); tot = 0
    for i in good:
        if i['s0'] is None or i['key'] not in params or not fits(i): continue
        p = i['prev']['steps'][-1]
        tot += 1
        pred = max(p['rel'] + gfirst(p), i['s0'] + off_after(table, i['prev']['key'], off0[i['key']]))
        if pred != i['steps'][0]['S']:
            fs[(i['prev']['key'][0], i['prev']['steps'][-1]['kind'], i['key'][0], i['mode'], i['steps'][0]['S'] - pred)] += 1
            if i['mode'] == 0: PAIRS[(i['prev']['key'], ends.get(i['prev']['key']), tuple(x[1] for x in table[i['prev']['key']]['steps']), i['key'], off0[i['key']], table[i['key']]['steps'][0][1], i['steps'][0]['S'] - pred)] += 1
        sp = params[i['key']]['steps']
        for idx in range(1, len(i['steps'])):
            b = sp[idx - 1]['sync']
            terms = b if isinstance(b[0], tuple) and isinstance(b[0][0], tuple) else (b,)
            q = i['steps']
            bus = q[idx - 1]['rel'] + GM[q[idx - 1]['kind']]
            def btv(bb):
                if bb[0] == 's0': return i['s0e']
                if bb[0] == 'rel': return q[bb[1]]['rel']
                return q[bb[1]]['rrel']
            pr = max(bus, *[btv(bb) + o for bb, o in terms])
            if pr != q[idx]['S']: st_bad[(i['key'], idx, i['mode'], q[idx]['S'] - pr)] += 1
    # DOUT и отпускание (быстрая память, режим 0)
    dbad = collections.Counter(); rbad = collections.Counter(); sbad = collections.Counter()
    for i in good:
        if i['s0'] is None or i['key'] not in params or not fits(i): continue
        q = i['steps']; pk = params[i['key']]
        for idx, st in enumerate(q):
            if st['kind'] == 'R' and st['role'] != 'N' and st['st'] - st['S'] != 1: sbad[(st['role'], st['st'] - st['S'])] += 1
            if i['mode'] == 0:
                exp = st['st'] + 4 if st['kind'] in 'R' else (st['dout'] + 3)
                if st['role'] != 'N' and st['rel'] != exp: rbad[(st['kind'], st['role'], st['rel'] - exp)] += 1
            if st['kind'] in 'WM':
                dp = pk.get('dout0') if idx == 0 else pk['steps'][idx - 1].get('dout')
                if not dp: continue
                b_, o = dp
                if b_[0] == 'sync': bt_ = st['S']
                elif b_[0] == 's0': bt_ = i['s0e']
                elif b_[0] == 'rel': bt_ = q[b_[1]]['rel']
                else: bt_ = q[b_[1]]['rrel']
                lo = st['S'] + 5 if st['kind'] == 'W' else st['rrel'] + 6
                pr = max(lo, bt_ + o)
                if pr != st['dout']: dbad[(i['key'], idx, i['mode'], st['dout'] - pr)] += 1
    print('DOUT мимо', sum(dbad.values()), 'отпускание (режим 0) мимо', sum(rbad.values()), 'строб чтения не S+1', sum(sbad.values()))
    for x, n in dbad.most_common(8): print('   DOUT', x, n)
    for x, n in rbad.most_common(8): print('   отпускание', x, n)
    for x, n in sbad.most_common(8): print('   DIN', x, n)
    print('экземпляров', tot, 'первый SYNC мимо', sum(fs.values()), 'шаги мимо', sum(st_bad.values()))
    for x, n in fs.most_common(15): print('   первый', x, n)
    for x, n in st_bad.most_common(10): print('   шаг', x, n)
    for x, n in PAIRS.most_common(25): print('   пара', x, n)
    # те же переходы, совпавшие: какие у них предшественники
    okp = collections.Counter()
    for i in good:
        if i['mode'] or i['s0'] is None or not fits(i): continue
        p = i['prev']['steps'][-1]
        pred = max(p['rel'] + gfirst(p), i['s0'] + off0[i['key']])
        if i['prev']['key'][0] == 'D' and p['kind'] == 'R' and i['key'][0] in 'DS':
            okp[(pred == i['steps'][0]['S'], table[i['prev']['key']]['steps'][-1][1], i['prev']['steps'][-1]['role'])] += 1
    print('  D(R) -> D/S, режим 0:', dict(okp))
    gaps = collections.Counter()
    for i in good:
        if not fits(i): continue
        p = i['prev']['steps'][-1]
        gaps[(p['role'], p['kind'], i['steps'][0]['S'] - p['rel'])] += 1
    print('  первый SYNC от отпускания прошлого цикла, по его роли и виду:')
    for x in sorted(gaps): print('    ', x, gaps[x])
    for new in ('cpp', False, True):
        BAD[new is True].clear()
        res = check(good, table, params, ends, off0, new)
        n = sum(res.values()); mae = sum(abs(k) * v for k, v in res.items()) / max(1, n)
        name = {'cpp': 'как сейчас в C++', False: 'старая + правила стыка', True: 'точки отсчёта'}[new]
        print('%-24s точно %d из %d, средняя ошибка %.1f такта' % (name, res[0], n, mae))

def main():
    global TABLE_G
    if '--check' in sys.argv:
        return check_only([a for a in sys.argv[1:] if ':' in a])
    table = pickle.load(open('table.pkl', 'rb'))
    TABLE_G = table
    data = []
    parts = list(PARTS)
    for a in sys.argv[1:]:
        if a.startswith('--train='):
            name, cnt = a[8:].split(':')
            parts += [(name, n) for n in range(int(cnt))]
    for mode in range(4):
        for prefix, part in parts:
            try: data += instances(mode, prefix, part)
            except FileNotFoundError: pass
    print(len(data), 'экземпляров')
    # только экземпляры, чья раскладка - как у шаблона формы
    def fits(inst):
        t = table.get(inst['key'])
        return t is not None and len(t['steps']) == len(inst['steps']) and \
            all(a[0] == b['kind'] and a[1] == b['role'] for a, b in zip(t['steps'], inst['steps']))
    good = [i for i in data if fits(i) and 'prev' in i and fits(i['prev'])]
    print(len(good), 'с раскладкой шаблона')
    by_key = collections.defaultdict(list)
    for i in good: by_key[i['key']].append(i)
    off0 = {k: v['steps'][0][2] for k, v in table.items()}

    off0[SEC] = 3
    def first_sync_v(inst, s0, v):
        p = inst['prev']['steps'][-1]
        return max(p['rel'] + gfirst(p), s0 + off_after(table, inst['prev']['key'], v))
    def first_sync(inst, s0):
        return first_sync_v(inst, s0, off0[inst['key']])

    # 1. конец каждой формы и смещение первого шага, поочерёдно. Конец - от
    # отпускания или строба своего цикла либо от своего s0; смещение первого
    # шага - от s0. Нормировка: у SEC смещение первого шага - 3
    for i in good: i['s0'] = None
    order = good                                  # предшественник всегда раньше
    ends = {}
    def end_one(inst, e):
        kind, j, off = e
        if kind == 's0':
            s0 = inst.get('s0e', inst.get('s0'))
            return None if s0 is None else s0 + off
        return end_time(inst, e)
    def end_of(inst, e):
        """e - одна база (kind, j, off) или пара таких: max"""
        if isinstance(e[0], tuple):
            a, b = end_one(inst, e[0]), end_one(inst, e[1])
            return None if a is None or b is None else max(a, b)
        return end_one(inst, e)
    def compute_s0():
        for i in order:
            e = ends.get(i['prev']['key'])
            i['s0'] = end_of(i['prev'], e) if e else None
            set_s0e(table, off0, i)
    succ = collections.defaultdict(list)
    for i in good: succ[i['prev']['key']].append(i)
    first_of = collections.defaultdict(list)
    for i in good: first_of[i['key']].append(i)
    for rnd in range(6):
        for k, lst in succ.items():
            n = len(table[k]['steps'])
            cands = collections.Counter()
            for i in lst:
                for kind in ('rel', 'st', 's0'):
                    for j in (range(n) if kind != 's0' else [0]):
                        base = end_of(i['prev'], (kind, j, 0))
                        if base is None: continue
                        cands[(kind, j, i['steps'][0]['S'] - off0[i['key']] - base)] += 1
            def score(e):
                m = 0
                for i in lst:
                    s0 = end_of(i['prev'], e)
                    if s0 is not None and first_sync(i, s0) == i['steps'][0]['S']: m += 1
                return m
            best, best_n = None, None
            top = [e for e, _ in cands.most_common(60)]
            for e in top:
                pref = (score(e), 1, e[0] == 'rel' and e[1] == n - 1, e[2])
                if best_n is None or pref > best_n: best, best_n = e, pref
            # не всё объяснено одной базой - max двух
            if best_n is not None and best_n[0] < len(lst):
                few = top[:15]
                for x in range(len(few)):
                    for y in range(x + 1, len(few)):
                        e = (few[x], few[y])
                        pref = (score(e), 0, False, 0)
                        if pref > best_n: best, best_n = e, pref
            if best: ends[k] = best
        compute_s0()
        for k, lst in first_of.items():
            if k == SEC: continue
            cands = collections.Counter(i['steps'][0]['S'] - i['s0'] for i in lst if i['s0'] is not None)
            best, best_n = off0[k], -1
            for v, _ in cands.most_common(20):
                m = sum(1 for i in lst if i['s0'] is not None and first_sync_v(i, i['s0'], v) == i['steps'][0]['S'])
                if m > best_n: best, best_n = v, m
            off0[k] = best
        compute_s0()
        e_ok = sum(1 for i in good if i['s0'] is not None and first_sync(i, i['s0']) == i['steps'][0]['S'])
        print('проход', rnd, 'первый SYNC совпал', e_ok, 'из', len(good))
    # совместно: конец формы и смещение её первого шага (они связаны через
    # повторы X; X), остальное - как подобрано
    for rnd in range(3):
        for k in list(first_of):
            if k == SEC or k not in succ: continue
            n = len(table[k]['steps'])
            mine = first_of[k]                     # экземпляры X: их первый шаг
            after = succ[k]                        # после X: их первый шаг
            ecands = collections.Counter()
            for i in after:
                for kind in ('rel', 'st', 's0'):
                    for j in (range(n) if kind != 's0' else [0]):
                        base = end_of(i['prev'], (kind, j, 0))
                        if base is None: continue
                        ecands[(kind, j, i['steps'][0]['S'] - off0[i['key']] - base)] += 1
            ocands = collections.Counter(i['steps'][0]['S'] - i['s0'] for i in mine if i['s0'] is not None)
            old_end, old_off = ends.get(k), off0[k]
            def total(e, v):
                m = 0
                for i in after:                    # их s0 - от конца X
                    s0 = end_of(i['prev'], e)
                    if s0 is None: continue
                    off = v if i['key'] == k else off0[i['key']]
                    if first_sync_v(i, s0, off) == i['steps'][0]['S']: m += 1
                for i in mine:                     # после другой формы: s0 прежний
                    if i['prev']['key'] == k or i['s0'] is None: continue
                    if first_sync_v(i, i['s0'], v) == i['steps'][0]['S']: m += 1
                return m
            best = (total(old_end, old_off), old_end, old_off)
            for v, _ in ocands.most_common(6):
                # кандидаты конца - при этом смещении у повторов X; X
                ec = collections.Counter()
                for i in after:
                    off = v if i['key'] == k else off0[i['key']]
                    for kind in ('rel', 'st', 's0'):
                        for j in (range(n) if kind != 's0' else [0]):
                            base = end_of(i['prev'], (kind, j, 0))
                            if base is None: continue
                            ec[(kind, j, i['steps'][0]['S'] - off - base)] += 1
                for e, _ in ec.most_common(12):
                    t = total(e, v)
                    if t > best[0]: best = (t, e, v)
            ends[k], off0[k] = best[1], best[2]
        compute_s0()
        e_ok = sum(1 for i in good if i['s0'] is not None and first_sync(i, i['s0']) == i['steps'][0]['S'])
        print('совместный проход', rnd, 'первый SYNC совпал', e_ok, 'из', len(good))
    miss = collections.Counter()
    for i in good:
        if i['s0'] is None: miss[('no s0', i['prev']['key'][0])] += 1; continue
        d = i['steps'][0]['S'] - first_sync(i, i['s0'])
        if d: miss[(i['prev']['key'], i['key'], i['mode'], d)] += 1
    for x, n in miss.most_common(12): print('   ', x, n)
    if '--form' in sys.argv:
        k = ('D', 0, 0, 2)
        print('конец', ends.get(k), 'смещение', off0[k], 'конец SEC', ends.get(SEC))
        c = collections.Counter()
        for i in first_of[k]:
            p = i['prev']; pl = p['steps'][-1]
            c[(p['key'], i['mode'], 'S-s0', None if i['s0'] is None else i['steps'][0]['S'] - i['s0'],
               'S-prel', i['steps'][0]['S'] - pl['rel'], pl['kind'], 'prel-pst', pl['rel'] - pl['st'],
               'ok', i['s0'] is not None and first_sync(i, i['s0']) == i['steps'][0]['S'])] += 1
        for x, n in sorted(c.items(), key=str): print('   ', x, n)
    if '--sec' in sys.argv:
        c = collections.Counter()
        for i in good:
            if i['s0'] is None or i['prev']['key'] != SEC: continue
            sec = i['prev']
            d = i['steps'][0]['S'] - first_sync(i, i['s0'])
            ss = sec['steps'][0]
            pp = sec['prev']
            c[(d, i['mode'], pp['key'][0], pp['steps'][-1]['kind'], 'sec S-s0', None if sec['s0'] is None else ss['S'] - sec['s0'],
               'S-prevrel', ss['S'] - pp['steps'][-1]['rel'], 'rel-st', ss['rel'] - ss['st'], 'next S-secrel', i['steps'][0]['S'] - ss['rel'])] += 1
        for x, n in sorted(c.items(), key=lambda x: -x[1])[:30]: print('   ', x, n)
    if '--why' in sys.argv:
        k = miss.most_common(1)[0][0][1]
        print('форма', k, 'смещение первого шага', off0[k], 'шаблон', table[k])
        ctx = collections.Counter()
        for i in first_of[k]:
            if i['s0'] is None: continue
            p = i['prev']; ps = p['steps'][-1]
            ctx[(p['key'], i['mode'], i['steps'][0]['S'] - i['s0'], i['steps'][0]['S'] - ps['rel'], ps['kind'])] += 1
        for x, n in sorted(ctx.items(), key=str)[:30]: print('     ', x, n)

    # 2. шаги: база SYNC и DOUT
    params = {}
    bad_steps = collections.Counter()
    for k, lst in by_key.items():
        lst = [i for i in lst if i['s0'] is not None]
        if not lst: continue
        tpl = table[k]['steps']
        sp = []
        for idx in range(1, len(tpl)):
            prevk = tpl[idx - 1][0]
            bases = [('s0',)] + [('rel', j) for j in range(idx)] + \
                    [('rrel', j) for j in range(idx) if tpl[j][0] == 'M']
            def pred_sync(i, base, off):
                s = i['steps']
                b = base_time(i, base)
                return max(s[idx - 1]['rel'] + GMIN[s[idx - 1]['kind']], b + off)
            cands = collections.Counter()
            for i in lst:
                for b in bases:
                    cands[(b, i['steps'][idx]['S'] - base_time(i, b))] += 1
            best, bestv = None, None
            top = [c for c, _ in cands.most_common(60)]
            for (b, off) in top:
                m = sum(1 for i in lst if pred_sync(i, b, off) == i['steps'][idx]['S'])
                pref = (m, 1, b == ('rel', idx - 1), off)
                if bestv is None or pref > bestv: best, bestv = (b, off), pref
            if bestv[0] < len(lst):
                few = top[:15]
                for x in range(len(few)):
                    for y in range(x + 1, len(few)):
                        (b1, o1), (b2, o2) = few[x], few[y]
                        m = sum(1 for i in lst if max(pred_sync(i, b1, o1), base_time(i, b2) + o2) == i['steps'][idx]['S'])
                        pref = (m, 0, False, 0)
                        if pref > bestv: best, bestv = ((b1, o1), (b2, o2)), pref
            miss = len(lst) - bestv[0]
            if miss: bad_steps[(k, idx, 'S')] = miss
            sp.append(dict(sync=best))
        params[k] = dict(end=ends.get(k), off0=off0[k], steps=sp)
        # DOUT записи (и записи в чтении-модификации-записи)
        for idx, st in enumerate(tpl):
            if st[0] not in 'WM': continue
            bases = [('sync',), ('s0',)] + [('rel', j) for j in range(idx)] + \
                    ([('rrel', idx)] if st[0] == 'M' else [])
            def btime(i, b):
                if b[0] == 'sync': return i['steps'][idx]['S']
                if b == ('rrel', idx): return i['steps'][idx]['rrel']
                return base_time(i, b)
            lo = (lambda i: i['steps'][idx]['S'] + 5) if st[0] == 'W' else (lambda i: i['steps'][idx]['rrel'] + 6)
            cands = collections.Counter()
            for i in lst:
                for b in bases:
                    cands[(b, i['steps'][idx]['dout'] - btime(i, b))] += 1
            best, bestv = None, None
            for (b, off), _ in cands.most_common(60):
                m = sum(1 for i in lst if max(lo(i), btime(i, b) + off) == i['steps'][idx]['dout'])
                pref = (m, b[0] == 'sync' or b == ('rrel', idx), -off)
                if bestv is None or pref > bestv: best, bestv = (b, off), pref
            miss = len(lst) - bestv[0]
            if miss: bad_steps[(k, idx, 'D')] = miss
            if idx == 0: params[k]['dout0'] = best
            else: params[k]['steps'][idx - 1]['dout'] = best
    print('шагов, где база объясняет не всё:', len(bad_steps), 'промахов', sum(bad_steps.values()))
    for (k, idx, w), n in bad_steps.most_common(25): print('  ', k, idx, w, n, 'из', len(by_key[k]))
    bases = collections.Counter()
    for k, p in params.items():
        for s in p['steps']: bases[s['sync'][0][0]] += 1
    print('базы SYNC:', dict(bases))
    pickle.dump(dict(params=params, ends=ends, off0=off0), open('fit.pkl', 'wb'))
    # старая модель: s0 экземпляров - по её правилам из настоящих времён
    for new in (False, True):
        for i in good:
            p = i['prev']
            if new: continue
        res = check(good, table, params, ends, off0, new)
        tot = sum(res.values())
        print('новая' if new else 'старая', 'модель: совпало', res[0], 'из', tot, sorted(res.items())[:12])
        for x in BAD[new is True][:12]: print('     ', x)



# ---------------------------------------------------------------- проверка
# Предсказание вперёд по случаю: от начала третьей копии до начала последней,
# по своим же предсказанным временам, память - по роли шага (как в эмуляторе)
REL = {'R': 4, 'W': 3}

def release(strobe, kind, slow, z0):
    rply = strobe
    if slow:
        n = (strobe - z0) // 12 + 1
        rply = z0 + n * 12 + 4
    d = rply - strobe
    d += d & 1
    return strobe + d + REL[kind]

def slow_of(mode, role, addr):
    if role == 'N': return False
    code = role in 'IF' or (addr is not None and addr < 0o060000)
    if mode == 1: return code or addr is None or addr < 0o100000
    if mode == 2: return not code and addr is not None and addr < 0o100000
    if mode == 3: return code
    return False

def check(good, table, params, ends, off0, new=True):
    by_case = collections.defaultdict(list)
    for i in good: by_case[i['cid']].append(i)
    res = collections.Counter()
    for cid, lst in by_case.items():
        try:
            a = next(k for k, i in enumerate(lst) if i.get('check_from'))
            b = next(k for k, i in enumerate(lst) if i.get('check_to'))
        except StopIteration: continue
        mode = cid[0]
        z0 = ZGRID[cid[:3]]
        # исходное состояние - настоящее, у предшественника первой
        prev = lst[a]['prev']
        state = dict(rel=prev['steps'][-1]['rel'], kind=prev['steps'][-1]['kind'],
                     role=prev['steps'][-1]['role'], s0=lst[a].get('s0'))
        if new is True and state['s0'] is None: continue
        if new is not True:
            pk = prev['key']
            if pk not in table: continue
            pl = prev['steps']
            data_free = all(x[1] in 'FI' for x in table[pk]['steps'])
            state['s0'] = (pl[-1]['st'] + 4 if data_free else pl[-1]['rel']) + table[pk]['tail']
        # старая модель: якорь и хвост
        ok = True
        for i in lst[a:b + 1]:
            k = i['key']
            if k not in table or (new is True and k not in params): ok = False; break
            tpl = table[k]['steps']
            if i is lst[b]:
                pred = first_pred(i, k, tpl, state, params, off0, new)
                res[pred - i['steps'][0]['S']] += 1
                if pred != i['steps'][0]['S']: BAD[new is True].append((cid, pred - i['steps'][0]['S']))
                break
            times = []
            s0 = state['s0']
            s0e = s0 + (shift(TABLE_G, state['key'], off0[k] if new is True else tpl[0][2]) if (EARLY_RULE and state.get('key') and new != 'cpp') else 0)
            if TRACE_FWD and new and cid[0] == 0 and cid[2] == '0':
                fp = first_pred(i, k, tpl, state, params, off0, new)
                if fp != i['steps'][0]['S'] or i.get('s0') != s0:
                    print('  расхождение', k, 'S пред', fp, 'факт', i['steps'][0]['S'], 's0 пред', s0, 'факт', i.get('s0'), 'пред.', state.get('key'))
                    if state.get('times'):
                        pi = i['prev']
                        print('     шаги предшественника пред:', [(t['S'], t['st'], t['rel']) for t in state['times']])
                        print('     факт:', [(t['S'], t['st'], t['rel']) for t in pi['steps']], 'его s0 факт', pi.get('s0'), 'пред', state.get('s0_used'))
                        print('     параметры:', params.get(state['key']), 'конец', ends.get(state['key']))
                    TRACE_FWD.append(1)
                    if len(TRACE_FWD) > 6: raise SystemExit
            for idx, st in enumerate(tpl):
                kind, role = st[0], st[1]
                addr = None
                if idx == 0:
                    sync = first_pred(i, k, tpl, state, params, off0, new)
                else:
                    g = state_rel + GMIN[times[-1]['kind']]
                    if new is True:
                        sp = params[k]['steps'][idx - 1]['sync']
                        sync = max(g, *[bt(times, s0e, b) + o for b, o in (sp if isinstance(sp[0], tuple) and isinstance(sp[0][0], tuple) else (sp,))])
                    else:
                        sync = state_rel + st[2]
                if role == 'N':
                    din = sync
                    rel = din + st[4]
                    times.append(dict(kind='R', S=sync, st=din, rel=rel)); state_rel = rel; continue
                slow = slow_of(mode, role, i['steps'][idx].get('addr'))
                if kind == 'R':
                    stb = sync + 1
                    rel = release(stb, 'R', slow, z0)
                    times.append(dict(kind='R', S=sync, st=stb, rel=rel))
                elif kind == 'W':
                    if new is True:
                        dp = params[k].get('dout0') if idx == 0 else params[k]['steps'][idx - 1].get('dout')
                        stb = max(sync + 5, bt(times, s0e, dp[0], sync) + dp[1]) if dp else sync + st[3]
                    else:
                        stb = sync + st[3]
                        if st[4] and not any(t['kind'] == 'R' for t in times[1:]): stb = max(stb, (s0e if new is False else s0) + st[4])
                    rel = release(stb, 'W', slow, z0)
                    times.append(dict(kind='W', S=sync, st=stb, rel=rel))
                else:
                    stb = sync + 1
                    rrel = release(stb, 'R', slow, z0)
                    if new is True:
                        dp = params[k].get('dout0') if idx == 0 else params[k]['steps'][idx - 1].get('dout')
                        dout = max(rrel + 6, bt(times, s0e, dp[0], sync, rrel) + dp[1]) if dp else rrel + st[4]
                    else:
                        dout = rrel + st[4]
                    rel = release(dout, 'W', slow, z0)
                    times.append(dict(kind='M', S=sync, st=stb, rrel=rrel, rel=rel))
                state_rel = times[-1]['rel']
            state = dict(rel=times[-1]['rel'], kind=times[-1]['kind'], role=tpl[-1][1], times=times, s0=s0e, key=k, s0_used=s0)
            state['s0'] = next_s0(state, table, ends, new is True)
    return res

def bt(times, s0, b, sync=None, rrel=None):
    if b[0] == 's0': return s0
    if b[0] == 'sync': return sync
    if b[0] == 'rel': return times[b[1]]['rel']
    if b[0] == 'rrel': return rrel if rrel is not None and b[1] == len(times) else times[b[1]]['rrel']

def next_s0(state, table, ends, new):
    k = state['key']; times = state['times']
    if not new:
        data_free = all(s[1] in 'FI' for s in table[k]['steps'])
        return (times[-1]['st'] + 4 if data_free else times[-1]['rel']) + table[k]['tail']
    e = ends.get(k)
    def one(e):
        kind, j, off = e
        if kind == 's0': return state['s0'] + off
        return (times[j]['rel'] if kind == 'rel' else times[j]['st'] + 4) + off
    if e is None: return times[-1]['rel']
    if isinstance(e[0], tuple): return max(one(e[0]), one(e[1]))
    return one(e)

def first_pred(i, k, tpl, state, params, off0, new):
    if new == 'cpp':
        return max(state['rel'] + GMIN[state['kind']], state['s0'] + tpl[0][2])
    g = state['rel'] + (7 if (state['kind'] == 'R' and state.get('role') == 'D') else GMIN[state['kind']])
    off = off0[k] if new is True else tpl[0][2]
    if state.get('key') is not None and EARLY_RULE: off = off_after(TABLE_G, state['key'], off)
    return max(g, state['s0'] + off)

if __name__ == '__main__':
    main()
