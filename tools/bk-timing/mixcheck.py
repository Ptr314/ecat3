# The chain model against the simulation on a random mix of forms.
import json, sys, pickle
sys.argv = sys.argv[:1] + ['x']
import model
from extract import load_events, transactions, form_key, insn_len

P = dict(win=1333, sr=250, sw=400, sm=700, lat=550, fm=0)
insns = json.load(open('mix.json'))
mem = [int(l, 16) for l in open('mix.mem')]
table = model.table

def fetch_times(run):
    trs = transactions(load_events(run))
    first = {}
    for i, t in enumerate(trs):
        if t['I'] is not None and t['O'] is None and t['addr'] in set(insns) and t['addr'] not in first:
            first[t['addr']] = i
    return trs, first

def sim_span(run, half_ns):
    trs, first = fetch_times(run)
    t0 = trs[first[insns[0]]]['S']; t1 = trs[first[insns[-1]]]['S']
    return (t1 - t0) / 2.0          # clocks

def model_span(run_d, clock_hz, slow):
    trs, first = fetch_times(run_d)
    ch = model.Chain()
    reply = model.Reply(clock_hz, P['win'], P['sr'], P['sw'], P['sm'], P['lat'])
    total = 0
    unknown = 0
    for k in range(len(insns) - 1):
        a0 = insns[k]
        w = mem[a0 >> 1]
        key = form_key(w, True)
        if key[0] == 'SOB': key = ('SOB', True)
        i0 = first[a0]; i1 = first[insns[k + 1]]
        seg = trs[i0 + 1:i1 + 1]
        if key not in table:
            unknown += 1; continue
        steps = table[key]['steps']
        reps = []
        for n, s in enumerate(steps):
            addr = seg[n]['addr'] if n < len(seg) else 0
            reps.append(reply if (slow and addr < 0o100000) else None)
        c = ch.run(key, reps)
        total += c
        if k == 0: first_start = 0
    return total + (ch.start - ch.now) / 12.0, unknown

for name, hz, half in (('mixD', 3000000, 166.667), ('mixE', 4000000, 125.0), ('mixH', 6000000, 83.333)):
    try:
        s = sim_span(name, half)
    except Exception as e:
        print(name, 'not ready', e); continue
    m, unk = model_span('mixD', hz, name != 'mixD')
    print('%s: simulation %.1f clocks, model %.1f clocks, difference %.2f%% (%d forms unknown)'
          % (name, s, m, 100.0 * (m - s) / s, unk))
