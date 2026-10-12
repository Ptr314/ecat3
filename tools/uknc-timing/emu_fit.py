# подбор параметров памяти по замерам на копии расчёта эмулятора
import sys, itertools, tests, emu_model as em
which = sys.argv[1] if len(sys.argv) > 1 else 'cpu'
p = 125000 if which == 'cpu' else 160000
col = 0 if which == 'cpu' else 1
EXCL = ('(PC)', 'miss', 'hit', 'MUL', 'JMP')
forms = [(n, t['words'](0)[0], (c, pp)[col]) for n, (t, c, pp) in tests.TSSPD.items() if not any(e in n for e in EXCL)]
def evaluate(kw, drift=(0,), verbose=False):
    tot = 0; rows = []
    for n, w, real in forms:
        vs = []
        for d in drift:
            m = em.Mem(p, **kw); m.grid = m.grid * (1000000 + d) // 1000000
            vs.append(em.period(w, m, 200, 3200 if d else 300))
        v = sum(vs) / len(vs)
        tot += abs(v - real); rows.append((n, v, real))
    if verbose:
        for n, v, real in rows: print('  %-22s %6.2f real %6.2f %+6.2f' % (n, v, real, v - real))
    return tot / len(forms)
if __name__ == '__main__':
    axes = [(a.split('=')[0], [int(x) for x in a.split('=')[1].split(',')]) for a in sys.argv[2:] if '=' in a]
    drift = (100,) if 'drift' in sys.argv else (0,)
    res = []
    for combo in itertools.product(*[v for _, v in axes]):
        kw = dict(zip([k for k, _ in axes], combo))
        res.append((evaluate(kw, drift), kw))
    res.sort(key=lambda x: x[0])
    for r in res[:12]: print('%.2f' % r[0], r[1])
    evaluate(res[0][1], drift, True)
