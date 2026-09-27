# Generates the test program for the VM1 + 037 testbench and the case list
import json, re, sys, os
HERE = os.path.dirname(os.path.abspath(__file__))

COPIES = 12
CODE_START = 0o000000
REG_INIT = {0: 0o062000, 1: 0o064000, 2: 0o066000}
FILL = 0o070000            # every data word, a valid pointer into the data area

def enc_mode(s):
    """operand text -> (mode, reg, extra words)"""
    s = s.strip()
    m = re.fullmatch(r'R(\d)', s)
    if m: return 0, int(m.group(1)), []
    m = re.fullmatch(r'@M\(R(\d)\)', s)
    if m: return 7, int(m.group(1)), [0]
    m = re.fullmatch(r'M\(R(\d)\)', s)
    if m: return 6, int(m.group(1)), [0]
    m = re.fullmatch(r'@0\(R(\d)\)', s)
    if m: return 7, int(m.group(1)), [0]
    m = re.fullmatch(r'@-\(R(\d)\)', s)
    if m: return 5, int(m.group(1)), []
    m = re.fullmatch(r'@\(R(\d)\)\+', s)
    if m: return 3, int(m.group(1)), []
    m = re.fullmatch(r'-\(R(\d)\)', s)
    if m: return 4, int(m.group(1)), []
    m = re.fullmatch(r'\(R(\d)\)\+', s)
    if m: return 2, int(m.group(1)), []
    m = re.fullmatch(r'\(R(\d)\)', s)
    if m: return 1, int(m.group(1)), []
    m = re.fullmatch(r'@#fm', s)
    if m: return 3, 7, [FILL]
    m = re.fullmatch(r'#1', s)
    if m: return 2, 7, [1]
    raise ValueError(s)

DOUBLE = {'MOV': 0o010000, 'CMP': 0o020000, 'BIT': 0o030000, 'BIC': 0o040000,
          'BIS': 0o050000, 'ADD': 0o060000, 'SUB': 0o160000,
          'MOVB': 0o110000, 'CMPB': 0o120000}
SINGLE = {'CLR': 0o005000, 'COM': 0o005100, 'INC': 0o005200, 'DEC': 0o005300,
          'NEG': 0o005400, 'TST': 0o005700, 'SWAB': 0o000300, 'ROR': 0o006000,
          'ROL': 0o006100, 'ASR': 0o006200, 'ASL': 0o006300,
          'CLRB': 0o105000, 'TSTB': 0o105700, 'COMB': 0o105100, 'INCB': 0o105200}

def assemble(text):
    """one instruction -> list of words; 'next' placeholders resolved later"""
    op, _, args = text.partition(' ')
    if op in DOUBLE:
        a, b = args.split(',')
        ms, rs, xs = enc_mode(a); md, rd, xd = enc_mode(b)
        return [DOUBLE[op] | (ms << 9) | (rs << 6) | (md << 3) | rd] + xs + xd
    if op in SINGLE:
        md, rd, xd = enc_mode(args)
        return [SINGLE[op] | (md << 3) | rd] + xd
    if text == 'NOP': return [0o000240]
    if text == 'BR next': return [0o000400]
    if text == 'BNE next': return [0o001000]
    if text == 'BLO next': return [0o103400]
    if text == 'JMP next': return [0o000167, 0]
    if text == 'JMP @#next': return [0o000137, 'next']
    raise ValueError(text)

def cases():
    out = []
    rows = [l.rstrip('\n').split('\t') for l in open(os.path.join(HERE, 'measured.tsv'), encoding='utf-8')]
    kind = None
    for r in rows:
        if r[0].startswith('==='):
            kind = 'x' if 'instruc' in r[0] else None; continue
        if kind and len(r) >= 10 and r[1]:
            try: vals = [float(x) for x in r[1:10]]
            except ValueError: continue
            out.append({'name': r[0], 'text': r[0], 'xlsx': vals})
    real = [('COM R0',12),('COM (R0)',32),('TST R1',12),('TSTB (R1)',24),('TST (R1)',24),
            ('TSTB (R1)+',24),('TSTB -(R1)',28),('TST (R1)+',24),('TST -(R1)',28),('SWAB R0',12),
            ('CLR R1',12),('CLR (R1)',32),('CLR (R1)+',32),('CLR @(R1)+',44),('CLR @-(R2)',48),
            ('MOV R1,R2',12),('MOV (R0)+,R1',28),('MOV R0,-(R2)',36),('MOV (R0)+,(R1)+',44),
            ('MOV (R0)+,(R1)',40),('MOV (R0),(R1)+',44),('MOV (R0),(R1)',40),('DEC (R1)+',32),
            ('INC (R1)+',32),('INC R0',12),('MOV (R0)+,@-(R0)',48),('MOV -(R1),@(R1)+',52),
            ('BNE next',16),('BLO next',16),('NOP',12),('MOV (R1),@0(R1)',60),('MOV (R0)+,@#fm',48),
            ('TSTB @#fm',36),('DEC @#fm',44),('BIT (R0)+,@#fm',48),('CMP R0,@#fm',44),
            ('SUB @#fm,R0',40),('BIC #1,R0',28),('JMP next',32),('BR next',16),
            ('MOV (R0),@(R0)+',48),('MOV (R1),@-(R1)',48),('JMP @#next',32)]
    for t, v in real:
        out.append({'name': t, 'text': t, 'real': v})
    return out

def build(prefix='', names=None):
    mem = [FILL] * 32768
    cs = cases()
    if names: cs = [c for c in cs if c['name'] in names]
    a = CODE_START
    for c in cs:
        # preamble: registers, SP, flags (Z=0, C=1)
        pre = []
        for r, v in REG_INIT.items():
            pre += [0o012700 | r, v]
        pre += [0o012706, 0o077000, 0o000261]     # MOV #77000,SP ; SEC
        for w in pre:
            mem[a >> 1] = w; a += 2
        c['copies'] = []
        for k in range(COPIES):
            words = assemble(c['text'])
            start = a
            c['copies'].append(start)
            nxt = start + 2 * len(words)
            for w in words:
                mem[a >> 1] = nxt if w == 'next' else w; a += 2
        c['end'] = a
        if a >= 0o060000:
            sys.exit('code overflow at case ' + c['name'])
    # terminate: write to 177700 (marker), then loop
    for w in [0o012737, 1, 0o177700, 0o000777]:
        mem[a >> 1] = w; a += 2
    with open('test%s.mem' % prefix, 'w') as f:
        for w in mem: f.write('%04x\n' % w)
    json.dump(cs, open('cases%s.json' % prefix, 'w'))
    print(len(cs), 'cases, code ends at', oct(a))

if __name__ == '__main__':
    if len(sys.argv) > 1: build(sys.argv[1], sys.argv[2].split(';'))
    else: build()
