# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
#
# A small two-pass PDP-11 assembler, a MACRO-11 subset, enough for test
# programs of the БК:
#   labels (NAME:, local 1$: between two plain labels), NAME = expr, . = expr;
#   .WORD .BYTE .ASCII .ASCIZ (any delimiter) .EVEN .BLKW .BLKB .END;
#   numbers octal, 10. decimal, 'c a character; expressions with + - * / & !
#   left to right, <> grouping, unary - and ^C;
#   every addressing mode, the К1801ВМ1 instruction set (MTPS, MFPS, SOB, XOR,
#   SXT, MARK is not there).
#
#   py asm11.py SOURCE.mac OUT.BIN [LISTING.lst]
#
# OUT.BIN is a БК program file: load address, length, then the bytes.
import re
import sys

DOUBLE = {'MOV': 0o010000, 'CMP': 0o020000, 'BIT': 0o030000, 'BIC': 0o040000,
          'BIS': 0o050000, 'ADD': 0o060000, 'SUB': 0o160000,
          'MOVB': 0o110000, 'CMPB': 0o120000, 'BITB': 0o130000, 'BICB': 0o140000,
          'BISB': 0o150000}
SINGLE = {'CLR': 0o005000, 'COM': 0o005100, 'INC': 0o005200, 'DEC': 0o005300,
          'NEG': 0o005400, 'ADC': 0o005500, 'SBC': 0o005600, 'TST': 0o005700,
          'ROR': 0o006000, 'ROL': 0o006100, 'ASR': 0o006200, 'ASL': 0o006300,
          'SWAB': 0o000300, 'SXT': 0o006700, 'JMP': 0o000100,
          'CLRB': 0o105000, 'COMB': 0o105100, 'INCB': 0o105200, 'DECB': 0o105300,
          'NEGB': 0o105400, 'ADCB': 0o105500, 'SBCB': 0o105600, 'TSTB': 0o105700,
          'RORB': 0o106000, 'ROLB': 0o106100, 'ASRB': 0o106200, 'ASLB': 0o106300,
          'MTPS': 0o106400, 'MFPS': 0o106700}
BRANCH = {'BR': 0o000400, 'BNE': 0o001000, 'BEQ': 0o001400, 'BGE': 0o002000,
          'BLT': 0o002400, 'BGT': 0o003000, 'BLE': 0o003400, 'BPL': 0o100000,
          'BMI': 0o100400, 'BHI': 0o101000, 'BLOS': 0o101400, 'BVC': 0o102000,
          'BVS': 0o102400, 'BCC': 0o103000, 'BHIS': 0o103000, 'BCS': 0o103400,
          'BLO': 0o103400}
NOARG = {'HALT': 0o000000, 'WAIT': 0o000001, 'RTI': 0o000002, 'BPT': 0o000003,
         'IOT': 0o000004, 'RESET': 0o000005, 'RTT': 0o000006, 'NOP': 0o000240,
         'CLC': 0o000241, 'CLV': 0o000242, 'CLZ': 0o000244, 'CLN': 0o000250,
         'CCC': 0o000257, 'SEC': 0o000261, 'SEV': 0o000262, 'SEZ': 0o000264,
         'SEN': 0o000270, 'SCC': 0o000277}
REGS = {'R0': 0, 'R1': 1, 'R2': 2, 'R3': 3, 'R4': 4, 'R5': 5, 'R6': 6, 'R7': 7,
        'SP': 6, 'PC': 7}


class AsmError(Exception):
    pass


class Assembler:
    def __init__(self):
        self.symbols = {}
        self.mem = {}
        self.listing = []

    # ------------------------------------------------------------ expressions
    def term(self, s, i, final):
        while i < len(s) and s[i] == ' ':
            i += 1
        if i >= len(s):
            raise AsmError('expression expected')
        c = s[i]
        if c == '<':
            v, i = self.expr(s, i + 1, final)
            if i >= len(s) or s[i] != '>':
                raise AsmError('> expected')
            return v, i + 1
        if c == '-':
            v, i = self.term(s, i + 1, final)
            return (-v) & 0xFFFF, i
        if s.startswith('^C', i):
            v, i = self.term(s, i + 2, final)
            return (~v) & 0xFFFF, i
        if c == "'":
            # 'x - код знака; ' в конце - пробел, срезанный вместе с операндом
            return (ord(s[i + 1]) if i + 1 < len(s) else 32), i + 2
        if c == '.' and (i + 1 >= len(s) or not (s[i + 1].isalnum() or s[i + 1] in '$_')):
            return self.loc, i + 1
        m = re.match(r'\d+\.?', s[i:])
        if m:
            t = m.group(0)
            v = int(t[:-1], 10) if t.endswith('.') else int(t, 8)
            return v & 0xFFFF, i + len(t)
        m = re.match(r'[A-Za-z_$.][A-Za-z0-9_$.]*', s[i:])
        if m:
            name = m.group(0).upper()
            if name in self.symbols:
                return self.symbols[name], i + len(m.group(0))
            if final:
                raise AsmError('undefined symbol ' + name)
            return 0, i + len(m.group(0))
        raise AsmError('bad expression: ' + s[i:])

    def expr(self, s, i, final):
        v, i = self.term(s, i, final)
        while True:
            while i < len(s) and s[i] == ' ':
                i += 1
            if i >= len(s) or s[i] not in '+-*/&!':
                return v & 0xFFFF, i
            op = s[i]
            w, i = self.term(s, i + 1, final)
            if op == '+': v += w
            elif op == '-': v -= w
            elif op == '*': v *= w
            elif op == '/': v = v // w if w else 0
            elif op == '&': v &= w
            else: v |= w
            v &= 0xFFFF

    def value(self, s, final):
        s = s.strip()
        v, i = self.expr(s, 0, final)
        if s[i:].strip():
            raise AsmError('junk after expression: ' + s[i:])
        return v

    # --------------------------------------------------------------- operands
    def operand(self, s, final, word_addr):
        """-> (mode/reg 6 bits, [extra words as callables of their address])"""
        s = s.strip()
        u = s.upper()
        if u in REGS:
            return REGS[u], []
        m = re.fullmatch(r'@?\((R[0-7]|SP|PC)\)\+', u)
        if m:
            return ((3 if u[0] == '@' else 2) << 3) | REGS[m.group(1)], []
        m = re.fullmatch(r'@?-\((R[0-7]|SP|PC)\)', u)
        if m:
            return ((5 if u[0] == '@' else 4) << 3) | REGS[m.group(1)], []
        m = re.fullmatch(r'\((R[0-7]|SP|PC)\)', u)
        if m:
            return (1 << 3) | REGS[m.group(1)], []
        m = re.fullmatch(r'@(R[0-7]|SP|PC)', u)
        if m:
            return (1 << 3) | REGS[m.group(1)], []
        if s.startswith('#'):
            v = self.value(s[1:], final)
            return 0o27, [lambda a, v=v: v]
        if s.startswith('@#'):
            v = self.value(s[2:], final)
            return 0o37, [lambda a, v=v: v]
        m = re.fullmatch(r'(@?)(.*)\((R[0-7]|SP|PC)\)', s, re.I)
        if m:
            v = self.value(m.group(2), final) if m.group(2).strip() else 0
            return ((7 if m.group(1) else 6) << 3) | REGS[m.group(3).upper()], [lambda a, v=v: v]
        defer = s.startswith('@')
        v = self.value(s[1:] if defer else s, final)
        return (0o77 if defer else 0o67), [lambda a, v=v: (v - (a + 2)) & 0xFFFF]

    # ----------------------------------------------------------------- lines
    def emit_word(self, w):
        self.mem[self.loc] = w & 0xFF
        self.mem[self.loc + 1] = (w >> 8) & 0xFF
        self.loc += 2

    def instruction(self, op, args, final):
        start = self.loc
        if op in NOARG:
            self.emit_word(NOARG[op]); return
        if op in BRANCH or op == 'SOB':
            if op == 'SOB':
                r, target = [a.strip() for a in args.split(',', 1)]
                t = self.value(target, final)
                off = (start + 2 - t) // 2
                if final and not 0 <= off <= 0o77:
                    raise AsmError('SOB out of range')
                self.emit_word(0o077000 | (REGS[r.upper()] << 6) | (off & 0o77)); return
            t = self.value(args, final)
            off = (t - (start + 2)) // 2
            if final and not -128 <= off <= 127:
                raise AsmError('branch out of range')
            self.emit_word(BRANCH[op] | (off & 0xFF)); return
        if op in ('EMT', 'TRAP'):
            v = self.value(args, final) if args.strip() else 0
            self.emit_word((0o104000 if op == 'EMT' else 0o104400) | (v & 0xFF)); return
        if op == 'RTS':
            self.emit_word(0o000200 | REGS[args.strip().upper()]); return
        if op == 'JSR':
            r, dst = [a.strip() for a in args.split(',', 1)]
            words = []
            md, xd = self.operand(dst, final, start)
            words = [0o004000 | (REGS[r.upper()] << 6) | md]
            self.emit_word(words[0])
            for x in xd: self.emit_word(x(self.loc))
            return
        if op == 'XOR':
            r, dst = [a.strip() for a in args.split(',', 1)]
            md, xd = self.operand(dst, final, start)
            self.emit_word(0o074000 | (REGS[r.upper()] << 6) | md)
            for x in xd: self.emit_word(x(self.loc))
            return
        if op in DOUBLE:
            src, dst = split_operands(args)
            ms, xs = self.operand(src, final, start)
            md, xd = self.operand(dst, final, start)
            self.emit_word(DOUBLE[op] | (ms << 6) | md)
            for x in xs + xd: self.emit_word(x(self.loc))
            return
        if op in SINGLE:
            md, xd = self.operand(args, final, start)
            self.emit_word(SINGLE[op] | md)
            for x in xd: self.emit_word(x(self.loc))
            return
        raise AsmError('unknown instruction ' + op)

    def directive(self, op, args, final, raw):
        if op == '.WORD':
            for a in split_operands(args) if args.strip() else ['0']:
                self.emit_word(self.value(a, final))
        elif op == '.BYTE':
            for a in split_operands(args):
                self.mem[self.loc] = self.value(a, final) & 0xFF; self.loc += 1
        elif op in ('.ASCII', '.ASCIZ'):
            # Строки в любых разделителях и байты <выражение> подряд
            t = raw.strip()
            while t:
                if t[0] == '<':
                    v, i = self.expr(t, 1, final)
                    if i >= len(t) or t[i] != '>':
                        raise AsmError('> expected')
                    self.mem[self.loc] = v & 0xFF; self.loc += 1
                    t = t[i + 1:].strip()
                    continue
                end = t.index(t[0], 1)
                for ch in t[1:end].encode('ascii'):
                    self.mem[self.loc] = ch; self.loc += 1
                t = t[end + 1:].strip()
            if op == '.ASCIZ':
                self.mem[self.loc] = 0; self.loc += 1
        elif op == '.EVEN':
            if self.loc & 1:
                self.mem[self.loc] = 0; self.loc += 1
        elif op == '.BLKW':
            self.loc += 2 * self.value(args, final)
        elif op == '.BLKB':
            self.loc += self.value(args, final)
        elif op in ('.END', '.TITLE', '.IDENT', '.LIST', '.NLIST', '.ENABL', '.DSABL'):
            pass
        else:
            raise AsmError('unknown directive ' + op)

    def assemble(self, lines, final):
        self.loc = 0
        self.mem = {}
        scope = ''
        for n, line in enumerate(lines, 1):
            text = strip_comment(line)
            try:
                self.line(text, final, n, line)
            except AsmError as e:
                raise AsmError('line %d: %s\n  %s' % (n, e, line.rstrip()))

    def line(self, text, final, n, raw):
        start = self.loc
        while True:
            m = re.match(r'\s*([A-Za-z_$.][A-Za-z0-9_$.]*|\d+\$):', text)
            if not m:
                break
            name = m.group(1).upper()
            if name.endswith('$'):
                name = self.scope + '.' + name
            else:
                self.scope = name
            if name in self.labels:
                raise AsmError('label defined twice: ' + name)
            self.symbols[name] = self.loc
            self.labels.add(name)
            text = text[m.end():]
        text = re.sub(r'(?<![A-Za-z0-9_$.])(\d+\$)', lambda mm: self.scope + '.' + mm.group(1), text)
        s = text.strip()
        if not s:
            if final: self.listing.append((start, b'', raw))
            return
        m = re.match(r'([A-Za-z_$.][A-Za-z0-9_$.]*)\s*=\s*(.*)$', s)
        if m:
            name = m.group(1).upper()
            v = self.value(m.group(2), final)
            if name == '.':
                self.loc = v
            else:
                self.symbols[name] = v
            if final: self.listing.append((start, b'', raw))
            return
        parts = re.split(r'\s+', s, maxsplit=1)
        op, args = parts[0].upper(), (parts[1] if len(parts) > 1 else '')
        if op.startswith('.'):
            self.directive(op, args, final, args)
        else:
            self.instruction(op, args, final)
        if final:
            self.listing.append((start, bytes(self.mem.get(a, 0) for a in range(start, self.loc)), raw))

    def run(self, source):
        lines = source.splitlines()
        self.labels = set()
        self.scope = ''
        self.assemble(lines, False)
        self.labels = set()
        self.scope = ''
        self.assemble(lines, False)
        self.labels = set()
        self.scope = ''
        self.listing = []
        self.assemble(lines, True)


def strip_comment(line):
    # У .ASCII/.ASCIZ комментарий начинается только вне строк и <байтов>
    m = re.match(r'(\s*(?:[A-Za-z_$.][A-Za-z0-9_$.]*:\s*)*\.ASCI[IZ]\s+)', line, re.I)
    if m:
        i = m.end()
        while i < len(line):
            c = line[i]
            if c in ' 	':
                i += 1
            elif c == ';':
                return line[:i]
            elif c == '<':
                j = line.find('>', i)
                i = len(line) if j < 0 else j + 1
            else:
                j = line.find(c, i + 1)
                i = len(line) if j < 0 else j + 1
        return line
    out = []
    i = 0
    while i < len(line):
        c = line[i]
        if c == ';':
            break
        if c == "'":
            out.append(line[i:i + 2]); i += 2; continue
        out.append(c)
        i += 1
    return ''.join(out)


def split_operands(args):
    parts, depth, cur = [], 0, ''
    i = 0
    while i < len(args):
        c = args[i]
        if c == "'":
            cur += args[i:i + 2]; i += 2; continue
        if c in '(<': depth += 1
        if c in ')>': depth -= 1
        if c == ',' and depth == 0:
            parts.append(cur); cur = ''
        else:
            cur += c
        i += 1
    parts.append(cur)
    return [p.strip() for p in parts]


def main():
    src = open(sys.argv[1], encoding='utf-8').read()
    a = Assembler()
    try:
        a.run(src)
    except AsmError as e:
        sys.exit('%s: %s' % (sys.argv[1], e))
    lo, hi = min(a.mem), max(a.mem) + 1
    if hi & 1: hi += 1
    data = bytes(a.mem.get(i, 0) for i in range(lo, hi))
    out = bytes([lo & 0xFF, lo >> 8, len(data) & 0xFF, len(data) >> 8]) + data
    open(sys.argv[2], 'wb').write(out)
    if len(sys.argv) > 3:
        with open(sys.argv[3], 'w', encoding='utf-8', newline='\n') as f:
            for addr, b, raw in a.listing:
                words = ' '.join('%06o' % (b[i] | (b[i + 1] << 8) if i + 1 < len(b) else b[i])
                                 for i in range(0, min(len(b), 6), 2))
                f.write('%06o  %-20s %s\n' % (addr, words, raw.rstrip()))
            f.write('\nSymbols:\n')
            for k in sorted(a.symbols):
                if '.' in k and k.split('.')[-1].endswith('$'): continue
                f.write('  %-12s %06o\n' % (k, a.symbols[k]))
    print('%s: %06o-%06o, %d bytes' % (sys.argv[2], lo, hi - 1, len(data)))


if __name__ == '__main__':
    main()
