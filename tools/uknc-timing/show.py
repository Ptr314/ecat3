# show.py ppu|cpu key=v ... : все формы при одном наборе параметров
import sys, sim, tests
which = 1 if sys.argv[1] == 'cpu' else 2
plus = dict(clc=125 if which == 1 else 160, mclk=125 if which == 1 else 160, slots=1, mask=1, rd=0, wr=0)
only = None
for a in sys.argv[2:]:
    k, v = a.split('=')
    if k == 'only': only = v; continue
    plus[k] = v
EXCL = ('MUL',)
forms = {n: v for n, v in tests.TSSPD.items() if not any(e in n for e in EXCL) and (only is None or only in n)}
r = sim.run({n: v[0] for n, v in forms.items()}, plus, 8, 40)
err = []
for n, v in forms.items():
    e = (r[n] - v[which]) if r[n] is not None else 999
    if '(PC)' not in n and 'miss' not in n and 'hit' not in n: err.append(e)
    print('  %-45s %7.2f real %7.2f  %+6.2f' % (n, r[n] if r[n] is not None else -1, v[which], e))
print('mae (без PC и кэша) %.2f' % (sum(abs(e) for e in err) / max(1, len(err))))
