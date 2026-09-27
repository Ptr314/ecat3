# The timing model exactly as Vm1BusTiming does it in C++ (ticks = 1/12 clock),
# evaluated on the measurements: steady state of one form repeated.
import pickle, json, sys, re, math, os
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from extract import form_key
import gen

TPC = 12; TPH = 6
FAST_READ, FAST_WRITE, RELEASE = 1, 2, 3
PREFETCH_RELEASE = FAST_READ + 1 + RELEASE          # strobe 1 + reply 1 + release 3

table = pickle.load(open('table.pkl', 'rb'))

def conv(ns, clock_hz):
    return (ns * clock_hz * TPC + 500000000) // 1000000000

class Reply:
    def __init__(self, clock_hz, window, setup_r, setup_w, setup_m, delay, rmw_extra=0):
        self.window = conv(window, clock_hz); self.setup_r = conv(setup_r, clock_hz)
        self.setup_w = conv(setup_w, clock_hz); self.setup_m = conv(setup_m, clock_hz)
        self.delay = conv(delay, clock_hz); self.rmw_extra = rmw_extra * TPH

def reply_time(strobe, kind, rmw_write, r):
    if r is None or r.window == 0:
        if kind == 'R': return strobe + FAST_READ * TPH
        return strobe + FAST_WRITE * TPH + (r.rmw_extra if (rmw_write and r) else 0)
    setup = r.setup_r if kind == 'R' else (r.setup_m if rmw_write else r.setup_w)
    x = strobe + setup
    point = x + (r.window - x % r.window) % r.window
    seen = point + r.delay
    return (seen + TPH - 1) // TPH * TPH

class Chain:
    def __init__(self):
        self.now = 0; self.start = 0; self.release = 0
    def run(self, key, replies):
        """replies: one per step (the last is the prefetch). Returns clocks."""
        t = table[key]
        steps, tail = t['steps'], t['tail']
        cur = self.start; last_sync = cur
        for s, r in zip(steps, replies):
            kind, role, gap, strobe, r2w = s
            sync = cur + gap * TPH; last_sync = sync
            st = sync + strobe * TPH
            if kind == 'M':
                rel = reply_time(st, 'R', False, r) + RELEASE * TPH
                st2 = rel + r2w * TPH
                cur = reply_time(st2, 'W', True, r) + RELEASE * TPH
            else:
                cur = reply_time(st, kind, False, r) + RELEASE * TPH
        self.release = cur
        self.start = cur + tail * TPH       # the microprogram finishes after the prefetch
        clocks = (self.start - self.now) // TPC
        self.now += clocks * TPC
        return clocks

class FastCard:
    def __init__(self, halves): self.window = 0; self.rmw_extra = halves * TPH

def steady(key, slow_code, slow_data, reply, n=48, fast=None):
    ch = Chain()
    steps = table[key]['steps']
    reps = []
    for i, s in enumerate(steps):
        stream = s[1] == 'I'
        slow = slow_code if stream else slow_data
        reps.append(reply if slow else fast)
    total = []
    for k in range(n):
        total.append(ch.run(key, reps))
    # average over the second half, a multiple of 3 steps for the 16/3 window
    m = len(total) // 2 // 3 * 3
    return sum(total[-m:]) / m

COL = {'B': 0, 'C': 1, 'D': 2, 'E': 3, 'F': 4, 'G': 5, 'H': 6, 'I': 7, 'J': 8}
CONF = {'B': (3000000, 1, 1), 'C': (3000000, 0, 1), 'D': (3000000, 0, 0),
        'E': (4000000, 1, 1), 'F': (4000000, 0, 1), 'G': (4000000, 0, 0),
        'H': (6000000, 1, 1), 'I': (6000000, 0, 1), 'J': (6000000, 0, 0)}

def case_key(text):
    words = gen.assemble(text)
    w = words[0]
    taken = None
    k = form_key(w, taken)
    if k[0] == 'BR': k = ('BR', True)                # BR, BNE with Z=0, BLO with C=1: all taken
    return k

def evaluate(p, cols, show=False, cases=None):
    bad = 0; tot = 0; lines = []
    for c in cases:
        try: key = case_key(c['text'])
        except Exception as e: continue
        if key not in table:
            if show: lines.append('%-22s no form %s' % (c['name'], key))
            continue
        line = []
        for col in cols:
            if 'xlsx' in c: ref = c['xlsx'][COL[col]]
            elif col == 'B' and 'real' in c: ref = c['real']
            elif col == 'E' and 'real11' in c: ref = c['real11']
            else: continue
            hz, sc, sd = CONF[col]
            r = Reply(hz, p['win'], p['sr'], p['sw'], p['sm'], p['lat'], p.get('fm', 0))
            v = steady(key, sc, sd, r, fast=FastCard(p.get('fm', 0)))
            ok = abs(v - ref) < 0.1
            tot += 1; bad += (not ok)
            line.append('%s:%g/%g%s' % (col, round(v, 2), ref, '' if ok else '!!'))
        if show: lines.append('%-22s %s' % (c['name'], '  '.join(line)))
    return bad, tot, lines

if __name__ == '__main__':
    cases = json.load(open('cases.json'))
    try: cases += json.load(open(os.path.join(HERE, 'bk0011.json')))
    except FileNotFoundError: pass
    cols = sys.argv[1].split(',')
    p = dict(win=1333, sr=60, sw=200, sm=520, lat=680, fm=0)
    if len(sys.argv) > 2: p.update(json.loads(sys.argv[2]))
    if len(sys.argv) > 3 and sys.argv[3] == 'fit':
        steps = {'sr': 20, 'sw': 20, 'sm': 20, 'lat': 20}
        b, t, _ = evaluate(p, cols, cases=cases)
        imp = True
        while imp:
            imp = False
            for k, st in steps.items():
                for d in (-st, st, -3 * st, 3 * st, -6 * st, 6 * st):
                    q = dict(p); q[k] = max(0, q[k] + d)
                    b2, _, _ = evaluate(q, cols, cases=cases)
                    if b2 < b: p, b, imp = q, b2, True
        print('fit', p)
    b, t, lines = evaluate(p, cols, show=True, cases=cases)
    print('\n'.join(lines))
    print('mismatches', b, 'of', t, p)
