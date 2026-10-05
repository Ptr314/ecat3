# Прогон программ на модели К1801ВМ2 (tb_vm2.v): сборка один раз на вариант
# стенда, прогоны параллельно. CPU11 - клон github.com/1801BM1/cpu11,
# iverilog и vvp - в PATH или в C:/iverilog/bin.
import os, subprocess, shutil, concurrent.futures

HERE = os.path.dirname(os.path.abspath(__file__))
for d in ('C:/iverilog/bin',):
    if os.path.isdir(d) and d not in os.environ['PATH']:
        os.environ['PATH'] = d + os.pathsep + os.environ['PATH']


def build(window=0, trace=0, out_dir='.'):
    src = os.path.join(os.environ['CPU11'], 'vm2', 'hdl')
    vvp = os.path.join(out_dir, 'tb_w%d_t%d.vvp' % (window, trace))
    if not os.path.exists(vvp):
        subprocess.check_call(['iverilog', '-g2005', '-o', vvp, '-s', 'tb_vm2',
                               '-P', 'tb_vm2.WINDOW=%d' % window, '-P', 'tb_vm2.TRACE=%d' % trace,
                               os.path.join(src, 'syn', 'rtl', 'vm2.v'),
                               os.path.join(src, 'syn', 'rtl', 'vm2_qbus.v'),
                               os.path.join(src, 'wbc', 'rtl', 'vm2_plm.v'),
                               os.path.join(HERE, 'tb_vm2.v')])
    return vvp


def write_mem(path, words):
    with open(path, 'w') as f:
        for w in words: f.write('%04x\n' % (w & 0xFFFF))


def run(vvp, mem_path, log_path):
    with open(log_path, 'w') as f:
        subprocess.call(['vvp', '-n', vvp, '+mem=' + mem_path], stdout=f, stderr=subprocess.STDOUT)
    return log_path


def run_many(jobs, window=0, trace=0, workers=None):
    """jobs: list of (name, words). Returns {name: log path}"""
    vvp = build(window, trace)
    for name, words in jobs: write_mem(name + '.mem', words)
    workers = workers or os.cpu_count()
    out = {}
    with concurrent.futures.ThreadPoolExecutor(workers) as ex:
        futs = {ex.submit(run, vvp, name + '.mem', 'log_%s.txt' % name): name for name, _ in jobs}
        for f in concurrent.futures.as_completed(futs):
            out[futs[f]] = f.result()
    return out


def end_clock(log):
    for l in open(log):
        if l.startswith('END'): return int(l.split()[1])
    return None


def events(log):
    """bus events: (kind, address, time in CLC)"""
    ev = []
    for l in open(log):
        p = l.split()
        if p and p[0] == 'E' and 'x' not in p[2] and 'z' not in p[2]:
            ev.append((p[1], int(p[2], 8), float(p[3])))
    return ev
