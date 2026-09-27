import json, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.argv = sys.argv[:1] + ['x']
import model
from extract import load_events, transactions, form_key
insns = json.load(open('mix.json')); mem = [int(l, 16) for l in open('mix.mem')]
def ft(run):
    trs = transactions(load_events(run)); first = {}
    for i, t in enumerate(trs):
        if t['I'] is not None and t['O'] is None and t['addr'] in set(insns) and t['addr'] not in first: first[t['addr']] = i
    return trs, first
trD, fD = ft('mixD')
sims = {}
for run, hz in (('mixH', 6000000), ('mixE', 4000000)):
    tr, f = ft(run)
    sims[run] = (hz, (tr[f[insns[-1]]]['S'] - tr[f[insns[0]]]['S']) / 2.0)
def span(hz, p):
    ch = model.Chain(); reply = model.Reply(hz, 1333, p[0], p[1], p[2], p[3])
    for k in range(len(insns) - 1):
        key = form_key(mem[insns[k] >> 1], True)
        seg = trD[fD[insns[k]] + 1:fD[insns[k + 1]] + 1]
        steps = model.table[key]['steps']
        reps = [reply if (n < len(seg) and seg[n]['addr'] < 0o100000) else None for n in range(len(steps))]
        ch.run(key, reps)
    return ch.start / 12.0
best = None
for sr in range(0, 401, 50):
  for sw in range(0, 601, 100):
    for sm in range(0, 801, 100):
      for lat in range(300, 901, 50):
        err = sum(abs(span(hz, (sr, sw, sm, lat)) - s) / s for hz, s in sims.values())
        if best is None or err < best[0]: best = (err, sr, sw, sm, lat)
print('best', best)
for run, (hz, s) in sims.items(): print(run, s, span(hz, best[1:]))
