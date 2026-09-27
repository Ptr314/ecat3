# table.pkl -> vm1_timing_table.inc (the layout of Vm1BusTiming::F_*)
import pickle, sys, collections

table = pickle.load(open('table.pkl', 'rb'))
occ = pickle.load(open('occ.pkl', 'rb'))

F_DOUBLE = 0
F_SINGLE = F_DOUBLE + 12 * 256
F_XOR = F_SINGLE + 28 * 16
F_JMP = F_XOR + 16
F_JSR = F_JMP + 16
F_RTS = F_JSR + 16
names = ['RTS', 'RTI', 'RTT', 'TRAP', 'MARK', 'CC_CLEAR', 'CC_SET', 'BR_TAKEN', 'BR_NOT',
         'SOB_TAKEN', 'SOB_NOT', 'INTERRUPT', 'INTERRUPT_VIRQ', 'WAIT', 'RESET']
F = {n: F_RTS + i for i, n in enumerate(names)}
F_COUNT = F_RTS + len(names)

forms = [None] * F_COUNT
origin = [''] * F_COUNT
approx = []

def put(idx, key, how='sim'):
    forms[idx] = table[key]; origin[idx] = '%s %s' % (how, key)

for key in table:
    k0 = key[0]
    if k0 == 'D': put(F_DOUBLE + key[1] * 256 + key[2] * 16 + key[3], key)
    elif k0 == 'S': put(F_SINGLE + key[1] * 16 + key[2], key)
    elif k0 == 'XOR': put(F_XOR + key[1], key)
    elif k0 == 'JMP': put(F_JMP + key[1], key)
    elif k0 == 'JSR': put(F_JSR + key[1], key)
    elif k0 == 'RTS': put(F['RTS'], key)
    elif k0 == 'RTI': put(F['RTI'], key)
    elif k0 == 'RTT': put(F['RTT'], key)
    elif k0 == 'EMT': put(F['TRAP'], key)
    elif k0 == 'MARK': put(F['MARK'], key)
    elif k0 == 'CC': put(F['CC_SET'] if key[1] else F['CC_CLEAR'], key)
    elif k0 == 'BR': put(F['BR_TAKEN'] if key[1] else F['BR_NOT'], key)
    elif k0 == 'SOB': put(F['SOB_TAKEN'] if key[1] else F['SOB_NOT'], key)

# interrupt entry from part X: the steps between the discarded prefetch and the
# handler's opcode fetch; RTI follows in the same span and is cut off
irq = collections.Counter()
for name, pred, key, steps in occ:
    if name == 'IRQ2' and len(steps) == 8:
        irq[(pred, steps[:5])] += 1
(pred, entry), _ = irq.most_common(1)[0]
entry = [list(s) for s in entry]
entry[0][2] -= table[pred]['tail']
forms[F['INTERRUPT']] = dict(steps=entry, tail=0); origin[F['INTERRUPT']] = 'sim IRQ2'
# VIRQ: the same, with the IAKO cycle after the pushes (trace of part X: DIN
# 4 half clocks after the release, RPLY 3 later, the vector read 8 after that)
v = [list(s) for s in entry]
v = v[:2] + [['R', 'N', 4, 2, 0], ['R', 'D', 7, 1, 0]] + v[3:]
forms[F['INTERRUPT_VIRQ']] = dict(steps=v, tail=0); origin[F['INTERRUPT_VIRQ']] = 'sim VIRQ trace'

# forms the chip never ran here: the nearest one that did
def spec(m, pc): return m * 2 + pc
def fill(base, n_ops, fmt):
    for op in range(n_ops):
        for s in range(16):
            idx = base + fmt(op, s)
            if forms[idx] is None:
                m, pc = divmod(s, 2)
                alt = [spec(m, 0)] if pc else []
                if m == 1 and pc: alt.append(spec(2, 0))       # #n as a destination: (R)+
                alt += {1: [spec(2, 0)], 2: [spec(3, 0)], 4: [spec(5, 0)],
                        5: [spec(7, 0)], 0: [], 3: [], 6: [], 7: []}.get(m, [])
                for a in alt:
                    j = base + fmt(op, a)
                    if forms[j] is not None:
                        forms[idx] = forms[j]; origin[idx] = 'like ' + origin[j]
                        approx.append((idx, origin[idx])); break

def fill_double():
    # PC used as a plain register (mode 0, 1, 4, 5 with R7) runs the microcode
    # of any other register: the same form without the PC bit
    for op in range(12):
        for s in range(16):
            for d in range(16):
                idx = F_DOUBLE + op * 256 + s * 16 + d
                if forms[idx] is not None: continue
                for s2, d2 in ((s, d & ~1), (s & ~1, d), (s & ~1, d & ~1), (s, spec(2, 0)), (s & ~1, spec(2, 0))):
                    j = F_DOUBLE + op * 256 + s2 * 16 + d2
                    if forms[j] is not None:
                        forms[idx] = forms[j]; origin[idx] = 'like ' + origin[j]; break

fill_double()
fill(F_SINGLE, 28, lambda op, s: op * 16 + s)
fill(F_XOR, 1, lambda op, s: s)
fill(F_JMP, 1, lambda op, s: s)
fill(F_JSR, 1, lambda op, s: s)
# JMP/JSR through a register without the increment: the (R)+ form
for base in (F_JMP, F_JSR):
    for s, like in ((spec(1, 0), spec(2, 0)), (spec(4, 0), spec(2, 0)), (spec(5, 0), spec(3, 0))):
        if forms[base + s] is None and forms[base + like] is not None:
            forms[base + s] = forms[base + like]; origin[base + s] = 'like ' + origin[base + like]

KIND = {'R': 'K_READ', 'W': 'K_WRITE', 'M': 'K_RMW'}
ROLE = {'D': 'R_DATA', 'I': 'R_STREAM', 'N': 'R_NONE'}
PREFETCH_RELEASE = 5

steps_out = []; first = []; count = []; end = []
dedup = {}
for i in range(F_COUNT):
    f = forms[i]
    if f is None:
        first.append(0); count.append(0); end.append(0); continue
    t = tuple(tuple(s) for s in f['steps'])
    if t not in dedup:
        dedup[t] = len(steps_out)
        steps_out += list(t)
    first.append(dedup[t]); count.append(len(t)); end.append(f['tail'])

missing = sum(1 for c in count if c == 0)
with open(sys.argv[1] if len(sys.argv) > 1 else 'vm1_timing_table.inc', 'w', newline='\n') as o:
    o.write('// Generated by tools/bk-timing/emit_inc.py from a simulation of the К1801ВМ1\n')
    o.write('// (github 1801BM1/cpu11, synchronous model, microprogram A). Do not edit.\n')
    o.write('// %d forms, %d without a template (the old count is used for them)\n\n' % (F_COUNT, missing))
    o.write('static const Vm1BusTiming::Step VM1_STEPS[] = {\n')
    for s in steps_out:
        o.write('    { Vm1BusTiming::%s, Vm1BusTiming::%s, %d, %d, %d },\n' % (KIND[s[0]], ROLE[s[1]], s[2], s[3], s[4]))
    o.write('};\n\n')
    def arr(name, typ, vals):
        o.write('static const %s %s[Vm1BusTiming::F_COUNT] = {\n' % (typ, name))
        for i in range(0, len(vals), 16):
            o.write('    ' + ', '.join(str(v) for v in vals[i:i + 16]) + ',\n')
        o.write('};\n\n')
    assert len(first) == F_COUNT
    arr('VM1_FIRST', 'uint16_t', first)
    arr('VM1_COUNT', 'uint8_t', count)
    arr('VM1_TAIL', 'uint8_t', end)
print('forms', F_COUNT, 'steps', len(steps_out), 'missing', missing, 'approximated', len(approx))
