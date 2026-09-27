# Bus transaction templates of every instruction form from fast/fast traces.
#
# A template covers what the processor does between the release of its own
# opcode fetch and the release of the next opcode fetch (the prefetch), which is
# the last step. Every step: kind R/W/M, role I (instruction stream) or D (data),
# gap = half clocks from the previous release to SYNC, strobe = SYNC to DIN/DOUT,
# r2w = read release to DOUT of the write half (M only).
import json, sys, collections

DATA_START = 0o060000

def insn_len(w):
    """words of a PDP-11 instruction (the 1801VM1 set)"""
    def ext(spec):
        m, r = (spec >> 3) & 7, spec & 7
        return 1 if m in (6, 7) or (r == 7 and m in (2, 3)) else 0
    op = w >> 12
    if op in (1, 2, 3, 4, 5, 6, 0o11, 0o12, 0o13, 0o14, 0o15, 0o16):
        return 1 + ext((w >> 6) & 0o77) + ext(w & 0o77)
    if (w & 0o177000) in (0o004000, 0o074000):            # JSR, XOR
        return 1 + ext(w & 0o77)
    if (w & 0o077700) in (0o000100, 0o000300) or (w & 0o077000) in (0o005000, 0o006000):
        return 1 + ext(w & 0o77)                           # JMP, SWAB, single
    if (w & 0o177700) in (0o006700, 0o106400, 0o106700):   # SXT, MTPS, MFPS
        return 1 + ext(w & 0o77)
    return 1

def spec_index(spec):
    m, r = (spec >> 3) & 7, spec & 7
    return m * 2 + (1 if r == 7 else 0)

DOUBLE_OPS = [1, 2, 3, 4, 5, 6, 0o11, 0o12, 0o13, 0o14, 0o15, 0o16]
SINGLE_OPS = [0o0050, 0o0051, 0o0052, 0o0053, 0o0054, 0o0055, 0o0056, 0o0057, 0o0060, 0o0061,
              0o0062, 0o0063, 0o0003, 0o0067, 0o1050, 0o1051, 0o1052, 0o1053, 0o1054, 0o1055,
              0o1056, 0o1057, 0o1060, 0o1061, 0o1062, 0o1063, 0o1064, 0o1067]

def form_key(w, taken=None):
    op = w >> 12
    if op in DOUBLE_OPS:
        return ('D', DOUBLE_OPS.index(op), spec_index((w >> 6) & 0o77), spec_index(w & 0o77))
    if (w >> 6) in SINGLE_OPS:
        return ('S', SINGLE_OPS.index(w >> 6), spec_index(w & 0o77))
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

def load_events(name):
    ev = []
    for l in open('log_%s.txt' % name):
        p = l.split()
        if p and p[0] == 'E' and 'x' not in p[2]:
            ev.append((p[1], int(p[2], 8), float(p[3])))
    return ev

def transactions(ev):
    """group events by SYNC: list of dict(addr, S, strobes...)"""
    out = []
    cur = None
    for e in ev:
        if e[0] == 'S':
            cur = dict(addr=e[1], S=e[2], I=None, O=None, rel=[], P=[])
            out.append(cur)
        elif cur is not None:
            if e[0] == 'I' and cur['I'] is None: cur['I'] = e[2]
            elif e[0] == 'O' and cur['O'] is None: cur['O'] = e[2]
            elif e[0] in 'io': cur['rel'].append(e[2])
            elif e[0] == 'P': cur['P'].append(e[2])
    return [t for t in out if t['rel']]

def occurrences(prefix, part):
    """every instruction executed in the steady part of each case:
    (case name, predecessor key, key, steps)"""
    cases = json.load(open('%s%s.json' % (prefix, part)))
    mem = [int(l, 16) for l in open('%s%s.mem' % (prefix, part))]
    trs = transactions(load_events({'all': 'A', 'pair': 'P', 'x': 'X'}[prefix] + str(part)))
    out = []
    by_addr = collections.defaultdict(list)
    for i, t in enumerate(trs):
        if t['I'] is not None and t['O'] is None: by_addr[t['addr']].append(i)
    for c in cases:
        lo, hi = c['start'], c['end'] + 0o200
        starts = set()
        a = c['copies'][0]
        while a < c['end']:
            starts.add(a); a += 2 * insn_len(mem[a >> 1])
        if c['kind'] in ('emt', 'trap', 'iot', 'bpt'):
            starts.add(c['end'] + 2)
        if not by_addr.get(c['copies'][2]) or not by_addr.get(c['copies'][-1]):
            print('missing', prefix, part, c['name']); continue
        i0 = by_addr[c['copies'][2]][0]
        i1 = [i for i in by_addr[c['copies'][-1]] if i > i0]
        if not i1: print('missing end', c['name']); continue
        seg = trs[i0:i1[0] + 1]
        fetch_pos = [k for k, t in enumerate(seg) if t['addr'] in starts and t['O'] is None and t['I'] is not None]
        prev_key = None
        for a_, b_ in zip(fetch_pos, fetch_pos[1:]):
            opaddr = seg[a_]['addr']
            w = mem[opaddr >> 1]
            steps = []
            prev_rel = seg[a_]['rel'][-1]
            for t in seg[a_ + 1:b_ + 1]:
                role = 'I' if (lo <= t['addr'] < hi and t['addr'] >= 0o100 and t['O'] is None
                               and t['I'] is not None and t['addr'] < DATA_START) else 'D'
                if t['I'] is not None and t['O'] is not None:
                    st = ('M', role, round(t['S'] - prev_rel), round(t['I'] - t['S']), round(t['O'] - t['rel'][0]))
                elif t['I'] is not None:
                    st = ('R', role, round(t['S'] - prev_rel), round(t['I'] - t['S']), 0)
                else:
                    st = ('W', role, round(t['S'] - prev_rel), round(t['O'] - t['S']), 0)
                steps.append(st)
                prev_rel = t['rel'][-1]
            nxt = seg[b_]['addr']
            taken = None
            if form_key(w)[0] == 'SOB':
                taken = (nxt != opaddr + 2)
            elif form_key(w)[0] == 'BR':
                taken = 'taken' in c['name']
            key = form_key(w, taken)
            out.append((c['name'], prev_key, key, tuple(steps)))
            prev_key = key
    return out

if __name__ == '__main__':
    import pickle
    occ = []
    for spec in sys.argv[1:]:
        prefix, part = spec.split(':')
        occ += occurrences(prefix, part)
    pickle.dump(occ, open('occ.pkl', 'wb'))
    print(len(occ), 'occurrences')
