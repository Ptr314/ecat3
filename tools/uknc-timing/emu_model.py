# Расчёт эмулятора (Vm1BusTiming::finish, ветка ВМ2) с памятью UKNCRamTiming:
# копия кода C++ на Python, чтобы сравнивать эмулятор со стендом по формам.
import re, os

INC = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'src', 'emulator', 'devices', 'cpu',
                   'vm2_timing_table.inc')
K_READ, K_WRITE, K_RMW = 0, 1, 2
R_DATA, R_STREAM, R_NONE, R_PREFETCH = 0, 1, 2, 3
KIND = {'K_READ': 0, 'K_WRITE': 1, 'K_RMW': 2}
ROLE = {'R_DATA': 0, 'R_STREAM': 1, 'R_NONE': 2, 'R_PREFETCH': 3}

def _load():
    s = open(INC, encoding='utf-8').read()
    steps = [(KIND[a], ROLE[b], int(c), int(d), int(e)) for a, b, c, d, e in
             re.findall(r'\{ Vm1BusTiming::(K_\w+), Vm1BusTiming::(R_\w+), (\d+), (\d+), (\d+) \}', s)]
    def arr(name):
        body = s[s.index(name):]
        body = body[body.index('{') + 1:body.index('};')]
        return [int(x) for x in re.findall(r'\d+', body)]
    return steps, arr('VM2_FIRST'), arr('VM2_COUNT'), arr('VM2_TAIL')
STEPS, FIRST, COUNT, TAIL = _load()

F2_SINGLE = 12 * 256
F2_EIS = F2_SINGLE + 28 * 16
F2_XOR = F2_EIS + 5 * 16
F2_JMP = F2_XOR + 16
F2_JSR = F2_JMP + 16
F2_RTS = F2_JSR + 16
(F2_RTI, F2_RTT, F2_TRAP, F2_MARK, F2_CC_CLEAR, F2_CC_SET, F2_BR_TAKEN, F2_BR_NOT,
 F2_SOB_TAKEN, F2_SOB_NOT) = range(F2_RTS + 1, F2_RTS + 11)
DOUBLE_INDEX = [-1, 0, 1, 2, 3, 4, 5, -1, -1, 6, 7, 8, 9, 10, 11, -1]
SINGLE_OPS = [0o0050, 0o0051, 0o0052, 0o0053, 0o0054, 0o0055, 0o0056, 0o0057, 0o0060, 0o0061,
              0o0062, 0o0063, 0o0003, 0o0067, 0o1050, 0o1051, 0o1052, 0o1053, 0o1054, 0o1055,
              0o1056, 0o1057, 0o1060, 0o1061, 0o1062, 0o1063, 0o1064, 0o1067]

def spec(sp): return ((sp >> 3) & 7) * 2 + (1 if (sp & 7) == 7 else 0)

def form2(w, taken=True):
    if (w & 0o177000) == 0o077000: return F2_SOB_TAKEN if taken else F2_SOB_NOT
    b = w & 0o177400
    if (0o000400 <= b <= 0o003400) or (0o100000 <= b <= 0o103400): return F2_BR_TAKEN if taken else F2_BR_NOT
    if (w & 0o170000) == 0o070000 and ((w >> 9) & 7) <= 3:
        return F2_EIS + ((w >> 9) & 7) * 16 + spec(w & 0o77)
    di = DOUBLE_INDEX[w >> 12]
    if di >= 0: return (di * 16 + spec((w >> 6) & 0o77)) * 16 + spec(w & 0o77)
    if ((w >> 6) & 0o1777) in SINGLE_OPS: return F2_SINGLE + SINGLE_OPS.index((w >> 6) & 0o1777) * 16 + spec(w & 0o77)
    if (w & 0o177000) == 0o074000: return F2_XOR + spec(w & 0o77)
    if (w & 0o177700) == 0o000100: return F2_JMP + spec(w & 0o77)
    if (w & 0o177000) == 0o004000: return F2_JSR + spec(w & 0o77)
    if (w & 0o177740) == 0o000240: return F2_CC_SET if (w & 0o20) else F2_CC_CLEAR
    raise ValueError(oct(w))

def is_byte(w):
    return (w & 0o100000) != 0 and (w >> 12) != 0o17 and not ((w & 0o177000) in (0o104000,))

TPC = 12
RELEASE = {K_READ: 4, K_WRITE: 3}
BUS_GAP = [3, 4, 4]
GAP_AFTER_DATA = 7
EARLY = 3
REFETCH_GAP, REFETCH_GAP2 = 17, 3
SOB_TARGET = 15

class Mem:
    """UKNCRamTiming, времена в пс"""
    def __init__(self, period_ps, grid=640, phase=0, reply=320, reply_write=None, read=0, write=0,
                 write_byte=None, rmw_write=None, rmw_write_byte=None, late=0, setup=0, drift=0):
        self.p = period_ps; self.grid = grid * 1000 * (1000000 + drift) // 1000000; self.phase = phase * 1000
        self.setup = setup * 1000
        self.reply = reply * 1000; self.reply_write = (reply if reply_write is None else reply_write) * 1000
        self.read = read * 1000; self.write = write * 1000
        self.write_byte = (write if write_byte is None else write_byte) * 1000
        self.rmw_write = (write if rmw_write is None else rmw_write) * 1000
        self.rmw_write_byte = ((self.rmw_write if write_byte is None else self.write_byte) if rmw_write_byte is None else rmw_write_byte * 1000)
        self.late = late; self.busy = 0
    def answer(self, write, byte, rmw, strobe_clk):
        strobe = strobe_clk * self.p
        frm = max(strobe + self.setup, self.busy)
        st = max(frm - self.phase, 0)
        st = -(-st // self.grid) * self.grid + self.phase
        if st < frm: st += self.grid
        keep = self.read
        if write: keep = (self.rmw_write_byte if byte else self.rmw_write) if rmw else (self.write_byte if byte else self.write)
        self.busy = st + keep
        rply = st + (self.reply_write if write else self.reply)
        return int((rply - strobe) // self.p) + self.late

class Chain:
    def __init__(self, mem):
        self.mem = mem
        self.start = TPC          # нечётный такт
        self.s0 = self.start; self.kind2 = K_READ; self.flags_free = True; self.last_data_read = False
        self.refetch = False
    def release(self, strobe, kind, byte=False, rmw=False):
        # strobe - в тиках абсолютно
        clocks = self.mem.answer(kind != K_READ, byte, rmw, strobe // TPC) if self.mem else 0
        rply = strobe + clocks * TPC
        d = -(-(rply - strobe) // TPC)
        d += d & 1
        return strobe + (d + RELEASE[K_READ if kind == K_READ else K_WRITE]) * TPC
    def finish(self, w, writes_next=False, taken=True):
        """writes_next: команда пишет в слово своего следующего кода;
        taken: переход или SOB, условие которого выполнено"""
        form = form2(w, taken)
        n = COUNT[form]
        if n == 0: return None
        steps = STEPS[FIRST[form]:FIRST[form] + n]
        byte = is_byte(w)
        s0 = self.s0
        if not self.flags_free and steps[0][2] < EARLY:
            s0 += (EARLY - steps[0][2]) * TPC
        t = self.start
        data_read = False; data_free = True; last_strobe = 0
        prefetched = False; refetch = False; reread = False
        pc_dest = form < F2_EIS and (form & 15) == 3
        for i, (kind, role, gap, so, r2w) in enumerate(steps):
            sync = t + gap * TPC
            if form == F2_SOB_TAKEN and i == 1:
                # выборка цели - от строба выборки наперёд, не от её отпускания
                sync = max(t + BUS_GAP[K_READ] * TPC, last_strobe + SOB_TARGET * TPC)
            if i == 0:
                sync = s0 + gap * TPC
                bus = self.start + (GAP_AFTER_DATA if self.last_data_read else BUS_GAP[self.kind2]) * TPC
                sync = max(sync, bus)
            strobe = sync + so * TPC
            if kind == K_WRITE and r2w and not data_read:
                strobe = max(strobe, s0 + r2w * TPC)
            last_strobe = strobe
            if role == R_NONE:
                t = strobe + r2w * TPC
            elif kind == K_RMW:
                rel = self.release(strobe, K_READ)
                t = self.release(rel + r2w * TPC, K_WRITE, byte, True)
            else:
                t = self.release(strobe, kind)
            if role == R_PREFETCH: prefetched = True
            elif role == R_DATA and kind != K_READ and prefetched and writes_next:
                if pc_dest:
                    t = self.release(t + (BUS_GAP[K_WRITE] + 1) * TPC, K_READ); reread = True
                else:
                    refetch = True
            if i == 0 and self.refetch:
                t = self.release(t + (REFETCH_GAP + 1) * TPC, K_READ)
                t = self.release(t + (REFETCH_GAP2 + 1) * TPC, K_READ)
            if role == R_NONE: data_free = False
            if role == R_DATA:
                data_free = False
                if kind != K_WRITE: data_read = True
        tail = TAIL[form] * TPC
        frm = (last_strobe + RELEASE[K_READ] * TPC) if data_free else t
        self.start = t
        self.s0 = frm + tail
        reread_last = reread and steps[-1][0] != K_READ
        self.kind2 = K_READ if reread_last else steps[-1][0]
        self.flags_free = data_free
        self.last_data_read = not reread_last and steps[-1][1] == R_DATA and steps[-1][0] == K_READ
        self.refetch = refetch
        return max(self.s0, self.start)

def period(w, mem, n1=50, n2=250, writes_next=False, taken=True):
    c = Chain(mem)
    ends = []
    for i in range(n2):
        e = c.finish(w, writes_next, taken)
        if e is None: return None
        ends.append(e)
    return (ends[n2 - 1] - ends[n1 - 1]) / (n2 - n1) / TPC
