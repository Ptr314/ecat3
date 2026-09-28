import json, sys, collections
import os as _os
HERE = _os.path.dirname(_os.path.abspath(__file__))
exec(open(_os.path.join(HERE, 'analyze.py'), encoding='utf-8').read().split('def main')[0])
real11 = {c['text']: c['real11'] for c in json.load(open(_os.path.join(HERE, 'bk0011.json')))}
def ref(c, col):
    if 'xlsx' in c: return c['xlsx'][COL[col]]
    return c.get('real') if col == 'B' else (real11.get(c['text']) if col == 'E' else None)
def per(reads, L):
    out = []
    for c in cases:
        try: t = [reads[a][0] for a in c['copies']]
        except KeyError: out.append(None); continue
        out.append((t[11] - t[11 - L]) / float(L))
    return out
for arg in sys.argv[1:]:
    run, col = arg.split(':')
    r = load(run)
    if not r: print(run, 'no log'); continue
    L = 6 if col in 'EFG' else 8
    ok = n = 0; d = collections.Counter()
    for c, v in zip(cases, per(r, L)):
        x = ref(c, col)
        if x is None or v is None: continue
        n += 1
        if abs(v - x) < 0.1: ok += 1
        else: d[round(v - x)] += 1
    print('%-18s vs %s: %3d of %d  diffs %s' % (run, col, ok, n, sorted(d.items())[:12]))
