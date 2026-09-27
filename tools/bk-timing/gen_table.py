# Timing table of every form from the occurrences extract.py collected.
#   own template  - the form right after SEC (runs "form; SEC"): its steps with
#                   the gaps it has when nothing is left of its predecessor
#   tail          - how much later SEC starts after this form than after SEC:
#                   the microprogram still working after the prefetch released
#   end           - the tail as a distance from the prefetch SYNC (a fast
#                   prefetch releases 5 half clocks after SYNC)
# The repeats ("form; form; ...") are the check: first gap = tail + own gap.
import pickle, collections, json, sys

FAST_PREFETCH_RELEASE = 5

occ = pickle.load(open('occ.pkl', 'rb'))
by = collections.defaultdict(collections.Counter)
for name, pred, key, steps in occ:
    by[(pred, key)][steps] += 1

CC = ('CC', 1)
def most(c): return c.most_common(1)[0][0] if c else None

g_cc_raw = most(by[(CC, CC)])[0][2]
# SEC itself keeps working after its prefetch: the typical form is the zero
SEC_TAIL = int(sys.argv[1]) if len(sys.argv) > 1 else 4
g_cc = g_cc_raw - SEC_TAIL
print('SEC after SEC: first gap', g_cc_raw, '-> own gap of SEC', g_cc)

keys = sorted({k for (_, k) in by}, key=str)
table = {}
problems = []
for k in keys:
    own = most(by.get((CC, k), collections.Counter()))
    if own is None:
        continue
    sec_after = most(by.get((k, CC), collections.Counter()))
    tail = (sec_after[0][2] - g_cc) if sec_after else 0
    steps = [list(s) for s in own]
    steps[0][2] -= SEC_TAIL             # it followed SEC, which was still busy
    table[k] = dict(steps=steps, tail=tail)
    # consistency of the SEC after it
    if len(by.get((k, CC), {})) > 1:
        problems.append(('SEC after', k, dict(by[(k, CC)])))
    if len(by.get((CC, k), {})) > 1:
        problems.append(('after SEC', k, dict(by[(CC, k)])))

# forms never seen right after SEC: take any occurrence whose predecessor is known
for (pred, k), cnt in list(by.items()):
    if k in table or pred not in table: continue
    steps = [list(s) for s in most(cnt)]
    steps[0][2] -= table[pred]['tail']
    succ = [c for (p2, k2), c in by.items() if p2 == k and k2 in table]
    tail = 0
    if succ:
        # the next form's first gap minus its own gap
        for (p2, k2), c in by.items():
            if p2 == k and k2 in table:
                tail = most(c)[0][2] - table[k2]['steps'][0][2]; break
    table[k] = dict(steps=steps, tail=max(0, tail))
    print('derived', k, 'from predecessor', pred, table[k])

tails = [v['tail'] for v in table.values()]
print('forms', len(table), 'tail min', min(tails), 'max', max(tails),
      'distribution', sorted(collections.Counter(tails).items()))

# check against the repeats
bad = 0
for k, v in table.items():
    rep = most(by.get((k, k), collections.Counter()))
    if rep is None: continue
    pred = [list(s) for s in v['steps']]
    pred[0][2] += v['tail']
    if [list(s) for s in rep] != pred:
        bad += 1
        if bad <= 25: print('REPEAT MISMATCH', k, 'repeat', rep, 'model', pred)
print('repeat mismatches', bad)
for p in problems[:20]: print('INCONSISTENT', p)

pickle.dump(table, open('table.pkl', 'wb'))
