# Steady-state instruction time of each case from the bus logs, against the measurements
import json, sys

import os
cases = json.load(open(os.environ.get('CASES', 'cases.json')))
COL = {'B': 0, 'C': 1, 'D': 2, 'E': 3, 'F': 4, 'G': 5, 'H': 6, 'I': 7, 'J': 8}

def load(name):
    reads = {}
    try:
        for line in open('log_%s.txt' % name):
            p = line.split()
            if len(p) == 3 and p[0] == 'R':
                reads.setdefault(int(p[1], 8), []).append(int(p[2]))
    except FileNotFoundError:
        return None
    return reads

def periods(reads):
    out = []
    for c in cases:
        cp = c['copies']
        try:
            t = [reads[a][0] for a in cp]
        except KeyError:
            out.append(None); continue
        # CPU clock count; average over the last 9 copies (multiple of 3 for 16/3)
        out.append((t[11] - t[2]) / 9.0)
    return out

def main():
    cols = sys.argv[1:] or ['B', 'C', 'D', 'E', 'H']
    res = {}
    for n in cols:
        r = load(n)
        if r: res[n] = periods(r)
    bad = {n: 0 for n in res}
    for i, c in enumerate(cases):
        line = []
        for n in res:
            sim = res[n][i]
            if 'xlsx' in c:
                ref = c['xlsx'][COL.get(os.environ.get('REF', n), 0)]
            elif os.environ.get('REF', n) == 'B':
                ref = c['real']
            else:
                ref = None
            ok = ref is None or (sim is not None and abs(sim - ref) < 0.1)
            if not ok: bad[n] += 1
            line.append('%s:%s/%s%s' % (n, 'None' if sim is None else ('%g' % round(sim, 2)),
                                        '-' if ref is None else '%g' % ref, '' if ok else '!!'))
        print('%-22s %s' % (c['name'], '  '.join(line)))
    print('mismatches:', bad, 'of', len(cases))

main()
