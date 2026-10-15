# Every instruction form worth timing, split into programs that fit the code area.
# Layout of a case: preamble, then COPIES copies of the instruction (plus, for a few
# forms, a helper after the copies). Data area 060000-077777 is filled with FILL.
import json, sys, os
PAIR = os.environ.get('PAIR') == '1'   # every copy followed by SEC
PREFIX = 'pair' if PAIR else 'all'

COPIES = 8
FILL = 0o070000
CODE_LIMIT = 0o060000
TABLES = 0o074000          # per-case pointer tables for RTS/RTI/JMP @(R)+ live here
REGS = {0: 0o062000, 1: 0o064000, 2: 0o066000, 3: 0o0, 4: 0o072000, 5: 0o073000}

# operand modes: (name, mode, reg, extra-word kind) ; kinds: None, 'x0' index 0,
# 'imm', 'abs' (absolute FILL), 'rel' (PC relative to FILL)
MODES = [('R', 0, None, None), ('(R)', 1, None, None), ('(R)+', 2, None, None),
         ('@(R)+', 3, None, None), ('-(R)', 4, None, None), ('@-(R)', 5, None, None),
         ('X(R)', 6, None, 'x0'), ('@X(R)', 7, None, 'x0'),
         ('#n', 2, 7, 'imm'), ('@#a', 3, 7, 'abs'), ('a', 6, 7, 'rel'), ('@a', 7, 7, 'rel'),
         ('PC', 0, 7, None)]
PC = 12                    # PC as a plain register, mode 0
PC_DATA = 0o060000         # words a PC destination case reads, written by its preamble

DOUBLE = {'MOV': 0o01, 'CMP': 0o02, 'BIT': 0o03, 'BIC': 0o04, 'BIS': 0o05, 'ADD': 0o06,
          'MOVB': 0o11, 'CMPB': 0o12, 'BITB': 0o13, 'BICB': 0o14, 'BISB': 0o15, 'SUB': 0o16}
SINGLE = {'CLR': 0o0050, 'COM': 0o0051, 'INC': 0o0052, 'DEC': 0o0053, 'NEG': 0o0054,
          'ADC': 0o0055, 'SBC': 0o0056, 'TST': 0o0057, 'ROR': 0o0060, 'ROL': 0o0061,
          'ASR': 0o0062, 'ASL': 0o0063, 'SWAB': 0o0003, 'SXT': 0o0067,
          'CLRB': 0o1050, 'COMB': 0o1051, 'INCB': 0o1052, 'DECB': 0o1053, 'NEGB': 0o1054,
          'ADCB': 0o1055, 'SBCB': 0o1056, 'TSTB': 0o1057, 'RORB': 0o1060, 'ROLB': 0o1061,
          'ASRB': 0o1062, 'ASLB': 0o1063, 'MTPS': 0o1064, 'MFPS': 0o1067}

def operand(mi, reg):
    name, mode, r, kind = MODES[mi]
    return mode, (r if r is not None else reg), kind

def forms():
    out = []
    for op, code in DOUBLE.items():
        for si in range(12):
            for di in range(12):
                if di == 8: continue                    # immediate destination
                out.append(dict(name='%s %s,%s' % (op, MODES[si][0], MODES[di][0]),
                                kind='double', op=code, src=si, dst=di))
    for op, code in SINGLE.items():
        for di in range(12):
            if di == 8 and op not in ('TST', 'TSTB', 'MTPS'): continue
            out.append(dict(name='%s %s' % (op, MODES[di][0]), kind='single', op=code, dst=di))
    for di in range(12):
        if di == 8: continue
        out.append(dict(name='XOR R,%s' % MODES[di][0], kind='xor', dst=di))
    # PC as a plain register. The chip runs another microprogram when it writes
    # PC (MOV (SP)+,PC takes as long as RTS PC on a real БК, not as MOV
    # (SP)+,R0): every copy writes the address of the next instruction into it,
    # so the copies still follow each other (MOV the next address, the other
    # operations a zero source)
    for op, code in DOUBLE.items():
        for di in range(12):
            if di == 8: continue
            # MOVB PC,R2 right after MOVB PC,R2 makes the model read the
            # second opcode for ever; after SEC it runs (pairs only)
            if op == 'MOVB' and not PAIR: continue
            out.append(dict(name='%s PC,%s' % (op, MODES[di][0]), kind='double', op=code, src=PC, dst=di))
        if op == 'MOVB': continue                       # sign extended: no code address
        for si in range(13):
            if si == PC and op in ('ADD', 'SUB', 'BIC', 'BICB'): continue
            out.append(dict(name='%s %s,PC' % (op, MODES[si][0]), kind='pcdst', op=code, src=si))
    out.append(dict(name='XOR R,PC', kind='pcdst', op=None, src=0))
    out.append(dict(name='TST PC', kind='single', op=SINGLE['TST'], dst=PC))
    out.append(dict(name='TSTB PC', kind='single', op=SINGLE['TSTB'], dst=PC))
    for cond, code, taken in (('BR', 0o000400, 1), ('BNE', 0o001000, 1), ('BEQ', 0o001400, 0),
                              ('BCS', 0o103400, 1), ('BCC', 0o103000, 0), ('BPL', 0o100000, 1)):
        out.append(dict(name='%s %s' % (cond, 'taken' if taken else 'not'), kind='branch', op=code))
    for mode in ('@#a', 'X(R)', '@X(R)', '@(R)+', 'a'):
        out.append(dict(name='JMP ' + mode, kind='jmp', mode=mode))
    for mode in ('@#a', 'X(R)', '@X(R)', '@(R)+', 'a'):
        out.append(dict(name='JSR PC,' + mode, kind='jsr', mode=mode))
    out.append(dict(name='RTS PC', kind='rts'))
    out.append(dict(name='RTI', kind='rti'))
    out.append(dict(name='RTT', kind='rtt'))
    out.append(dict(name='SOB loop', kind='sob'))
    out.append(dict(name='EMT+RTI', kind='emt'))
    out.append(dict(name='TRAP+RTI', kind='trap'))
    out.append(dict(name='IOT+RTI', kind='iot'))
    out.append(dict(name='BPT+RTI', kind='bpt'))
    out.append(dict(name='NOP', kind='word', word=0o000240))
    out.append(dict(name='SEC', kind='word', word=0o000261))
    out.append(dict(name='MARK 0', kind='mark'))
    return out

def emit_operand(mem, a, kind, words_after_opcode_addr):
    """extra word for an operand; a = address of this extra word"""
    if kind == 'x0': return 0
    if kind == 'imm': return 1
    if kind == 'abs': return FILL
    if kind == 'rel': return (FILL - (a + 2)) & 0xFFFF
    return None

class Prog:
    def __init__(self):
        self.mem = [FILL] * 32768
        self.a = 0o000100
        self.tab = TABLES
    def w(self, v):
        self.mem[self.a >> 1] = v & 0xFFFF; self.a += 2
    def tw(self, v):
        self.mem[self.tab >> 1] = v & 0xFFFF; self.tab += 2
        return self.tab - 2

def build_pc_case(p, f):
    """a write to PC as a plain register: every copy puts the address of the
    instruction after it there. The source words lie at PC_DATA (pointers to
    them at PC_DATA + 040 + ...), written by the preamble after the data area
    is restored; a register or (R) source takes a register of its own per copy"""
    start = p.a
    si = f['src']
    name, ms, rs, ks = MODES[si]
    per_reg = ms in (0, 1) and rs is None              # R, (R): one register a copy
    n = 6 if per_reg else COPIES
    mov = f['op'] == 0o01
    xor = f['op'] is None
    p.w(0o005037); p.w(0o177702)
    regfix = {}
    for r in range(6):
        p.w(0o012700 | r); regfix[r] = p.a; p.w(0)
    p.w(0o012706); p.w(0o077000)
    direct = lambda k: PC_DATA + 2 * k                 # the value read by copy k
    indirect = lambda k: PC_DATA + 0o40 + 2 * k        # its value behind a pointer
    # where each copy's value lies, and the pointer to it if any
    if ms in (3, 5, 7) or si == 11:
        vals = [indirect(k) for k in range(n)]
        ptrs = [direct(n - 1 - k) if ms == 5 else direct(k) for k in range(n)]
    elif ms == 4:
        vals = [direct(n - 1 - k) for k in range(n)]; ptrs = None
    else:
        vals = [direct(k) for k in range(n)]; ptrs = None
    valfix = []
    for k in range(n):
        if per_reg and ms == 0: break                   # the value is in the register
        p.w(0o012737); valfix.append(p.a); p.w(0); p.w(vals[k])
        if ptrs: p.w(0o012737); p.w(vals[k]); p.w(ptrs[k])
    if not per_reg:
        r1 = {2: PC_DATA, 3: PC_DATA, 4: PC_DATA + 2 * n, 5: PC_DATA + 2 * n}.get(ms, 0)
        p.mem[regfix[1] >> 1] = r1
    p.w(0o000261)                                       # SEC
    copies = []
    for k in range(n):
        copies.append(p.a)
        r = k if per_reg else (rs if rs is not None else 1)
        nxt = p.a + (4 if ks else 2)
        v = nxt if mov else 0
        if xor:
            p.w(0o074007 | (r << 6))
        else:
            p.w((f['op'] << 12) | (ms << 9) | (r << 6) | 0o07)
        if ks == 'imm': p.w(v)
        elif ks == 'abs': p.w(vals[k])
        elif ks == 'x0': p.w(ptrs[k] if ptrs else vals[k])
        elif ks == 'rel': p.w(((ptrs[k] if ptrs else vals[k]) - (p.a + 2)) & 0xFFFF)
        if per_reg and ms == 0: p.mem[regfix[r] >> 1] = v
        elif per_reg: p.mem[regfix[r] >> 1] = vals[k]
        if valfix: p.mem[valfix[k] >> 1] = v
        if PAIR:
            p.w(0o000261)                 # SEC
    f['copies'] = copies
    f['start'] = start
    f['end'] = p.a

def build_case(p, f):
    if f['kind'] == 'pcdst':
        return build_pc_case(p, f)
    start = p.a
    regs = dict(REGS)
    table_needed = f['kind'] in ('rts', 'rti', 'rtt') or \
        (f['kind'] in ('jmp', 'jsr') and f['mode'] in ('@(R)+',))
    # preamble; R3 set later when a table is needed
    p.w(0o005037); p.w(0o177702)          # CLR @#177702: the testbench restores the data area
    pre_at = p.a
    for r, v in regs.items():
        p.w(0o012700 | r); p.w(v)
    p.w(0o012706); p.w(0o077000)          # SP
    sp_fix = p.a - 2
    r3_fix = pre_at + 3 * 4 + 2           # the value word of MOV #..,R3
    p.w(0o000261)                          # SEC
    copies = []
    handler = None
    for k in range(COPIES):
        copies.append(p.a)
        kind = f['kind']
        if kind == 'double':
            ms, rs, ks = operand(f['src'], 1); md, rd, kd = operand(f['dst'], 2)
            p.w((f['op'] << 12) | (ms << 9) | (rs << 6) | (md << 3) | rd)
            if ks: p.w(emit_operand(p.mem, p.a, ks, 0))
            if kd: p.w(emit_operand(p.mem, p.a, kd, 0))
        elif kind == 'single':
            md, rd, kd = operand(f['dst'], 2)
            p.w((f['op'] << 6) | (md << 3) | rd)
            if kd: p.w(emit_operand(p.mem, p.a, kd, 0))
        elif kind == 'xor':
            md, rd, kd = operand(f['dst'], 2)
            p.w(0o074000 | (1 << 6) | (md << 3) | rd)
            if kd: p.w(emit_operand(p.mem, p.a, kd, 0))
        elif kind == 'branch':
            p.w(f['op'] | 0)              # offset 0: next instruction either way
        elif kind == 'word':
            p.w(f['word'])
        elif kind in ('jmp', 'jsr'):
            base = 0o000100 if kind == 'jmp' else 0o004700   # JMP / JSR PC,
            m = f['mode']
            if m == '@#a':
                p.w(base | 0o37); p.w(p.a + 2)
            elif m == 'X(R)':             # R3 = 0: X is the absolute target
                p.w(base | 0o63); p.w(p.a + 2)
            elif m == '@X(R)':            # R3 = 0: X is the address of a pointer
                ptr = p.tw(0)
                p.w(base | 0o73); p.w(ptr)
                p.mem[ptr >> 1] = p.a
            elif m == '@(R)+':            # R3 walks a table of targets
                p.w(base | 0o33)
                p.tw(p.a)
            elif m == 'a':                # relative, offset 0 via X(PC)
                p.w(base | 0o67); p.w(0)
        elif kind == 'rts':
            p.w(0o000207); p.tw(p.a)
        elif kind in ('rti', 'rtt'):
            p.w(0o000002 if kind == 'rti' else 0o000006); p.tw(p.a); p.tw(0o000000)
        elif kind == 'sob':
            p.w(0o012704); p.w(3)         # MOV #3,R4 ; SOB R4,.
            p.w(0o077401)
        elif kind in ('emt', 'trap', 'iot', 'bpt'):
            p.w({'emt': 0o104000, 'trap': 0o104400, 'iot': 0o000004, 'bpt': 0o000003}[kind])
        elif kind == 'mark':
            # MARK 0 pops R5 into PC: R5 = next, SP = address of a word holding R5's new value
            p.w(0o012705); p.w(p.a + 6)    # MOV #next,R5 (2 words) ; MARK 0
            p.w(0o006400)
            p.w(0o012706); p.w(0o077000)
        if PAIR:
            p.w(0o000261)                 # SEC
    end = p.a
    if f['kind'] in ('emt', 'trap', 'iot', 'bpt'):
        p.w(0o000401)                     # BR over the handler
        handler = p.a
        p.w(0o000002)                     # RTI
        vec = {'emt': 0o30, 'trap': 0o34, 'iot': 0o20, 'bpt': 0o14}[f['kind']]
        # set the vector in the preamble is impossible (already emitted); vectors are
        # constant per program, so they are collected and written by the caller
        f['vector'] = (vec, handler)
    if table_needed:
        # R3 or SP must point at the case's table: the preamble is patched
        first = (p.tab - (2 * COPIES if f['kind'] != 'rti' and f['kind'] != 'rtt' else 4 * COPIES))
        if f['kind'] in ('rts', 'rti', 'rtt'):
            p.mem[sp_fix >> 1] = first
        else:
            p.mem[r3_fix >> 1] = first
    f['copies'] = copies
    f['start'] = start
    f['end'] = end

def main():
    fs = forms()
    if os.environ.get('ONLY_PC') == '1':             # a quick look at the PC forms alone
        fs = [f for f in fs if f.get('src') == PC or f.get('dst') == PC or f['kind'] == 'pcdst']
    parts = []
    cur = []
    p = Prog()
    part_forms = []
    for f in fs:
        save = (p.a, p.tab)
        if f['kind'] in ('emt', 'trap', 'iot', 'bpt'):
            # traps share one vector per program: one trap form per program
            if any(g['kind'] in ('emt', 'trap', 'iot', 'bpt') and
                   g['kind'] == f['kind'] for g in part_forms):
                pass
        build_case(p, f)
        if p.a > CODE_LIMIT - 64 or p.tab > 0o077000 - 64:
            parts.append((p, part_forms)); p = Prog(); part_forms = []
            build_case(p, f)
        part_forms.append(f)
    parts.append((p, part_forms))
    for n, (p, pf) in enumerate(parts):
        vecs = {}
        for f in pf:
            if 'vector' in f:
                if f['vector'][0] in vecs:
                    sys.exit('two cases on one vector in part %d' % n)
                vecs[f['vector'][0]] = f['vector'][1]
        # the code starts at 0, so vectors 14..34 are inside the first case: the
        # program begins with a jump over a vector block instead
        for w in [0o012737, 1, 0o177700, 0o000777]:
            p.w(w)
        p.mem[0] = 0o000137; p.mem[1] = 0o000100      # start: JMP @#100
        for v, h in vecs.items():
            p.mem[v >> 1] = h; p.mem[(v >> 1) + 1] = 0
        json.dump(pf, open('%s%d.json' % (PREFIX, n), 'w'))
        with open('%s%d.mem' % (PREFIX, n), 'w') as fh:
            for w in p.mem: fh.write('%04x\n' % w)
        print('part', n, len(pf), 'forms, code to', oct(p.a), 'vectors', {oct(k): oct(v) for k, v in vecs.items()})

if __name__ == '__main__':
    main()
