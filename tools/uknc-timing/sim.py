# Прогон циклов из копий команды на tb_uknc.v: период одной копии в тактах CLC.
# CPU11 - клон github.com/1801BM1/cpu11, iverilog и vvp - в PATH или в
# C:/iverilog/bin. Промежуточные файлы - в work/ текущего каталога.
import os, subprocess, concurrent.futures, re

HERE = os.path.dirname(os.path.abspath(__file__))
CPU11 = os.path.join(os.environ['CPU11'], 'vm2', 'hdl')
if os.path.isdir('C:/iverilog/bin'):
    os.environ['PATH'] = 'C:/iverilog/bin' + os.pathsep + os.environ['PATH']
WORK = os.path.abspath('work')
os.makedirs(WORK, exist_ok=True)
VVP = os.path.join(WORK, 'tb.vvp')


def build(trace=0):
    vvp = VVP if not trace else VVP + '.t'
    src = [os.path.join(CPU11, 'syn', 'rtl', 'vm2.v'), os.path.join(CPU11, 'syn', 'rtl', 'vm2_qbus.v'),
           os.path.join(CPU11, 'wbc', 'rtl', 'vm2_plm.v'), os.path.join(HERE, 'tb_uknc.v')]
    if not os.path.exists(vvp) or os.path.getmtime(vvp) < max(os.path.getmtime(s) for s in src):
        subprocess.check_call(['iverilog', '-g2005', '-o', vvp, '-s', 'tb_uknc', '-P', 'tb_uknc.TRACE=%d' % trace] + src)
    return vvp


START = 0o1000
DATA = 0o060000
MARK = [0o005037, 0o177710]          # CLR @#177710


def program(t, k1, k2):
    """t: dict(words=fn(addr)->list, regs={n: v}, pre=fn(addr)->list (перед каждой копией, не считается))"""
    mem = [0] * 32768
    a = START
    def w(v):
        nonlocal a
        mem[a >> 1] = v & 0xFFFF; a += 2
    mem[0] = START; mem[1] = 0o340
    regs = t.get('regs', {})
    # регистры ставятся перед каждым блоком: одинаковый код между метками
    # сокращается в разности; None - адрес первой копии блока + at
    def setup():
        first = a + 4 * len(regs) + 4
        for r, v in regs.items():
            w(0o012700 | r); w(first + t.get('at', 0) if v is None else v)
    for blk in (k1, k2):
        setup()
        for x in MARK: w(x)
        for i in range(blk):
            for x in t['words'](a): w(x)
    setup()
    for x in MARK: w(x)
    for x in (0o012737, 1, 0o177700): w(x)
    for i in range(0o060000 >> 1, 0o160000 >> 1):
        if mem[i] == 0: mem[i] = t.get('fill', 0o060000)
    return mem


def run_one(args):
    name, t, k1, k2, plus, trace = args
    mem = program(t, k1, k2)
    fn = re.sub(r'[^A-Za-z0-9]', '_', name) + '_%x.mem' % (hash(tuple(sorted(plus.items()))) & 0xFFFFFF)
    with open(os.path.join(WORK, fn), 'w') as f:
        for x in mem: f.write('%04x\n' % x)
    vvp = build(trace)
    out = subprocess.run(['vvp', '-n', vvp, '+mem=' + fn] + ['+%s=%s' % kv for kv in plus.items()],
                         cwd=WORK, capture_output=True, text=True).stdout
    os.remove(os.path.join(WORK, fn))
    ms = [float(l.split()[1]) for l in out.splitlines() if l.startswith('M ')]
    if len(ms) < 3: return name, None, out
    return name, ((ms[2] - ms[1]) - (ms[1] - ms[0])) / (k2 - k1), out


def run(tests, plus, k1=16, k2=80, trace=0, workers=None):
    """tests: {name: t}; результат {name: такты CLC на копию}"""
    build(trace)
    jobs = [(n, t, k1, k2, plus, trace) for n, t in tests.items()]
    res = {}
    with concurrent.futures.ThreadPoolExecutor(workers or os.cpu_count()) as ex:
        for n, v, out in ex.map(run_one, jobs):
            res[n] = v if not trace else (v, out)
    return res
