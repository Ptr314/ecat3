# Время форм КМ1801ВМ3 по модели кристалла: программы форм tools/vm2-timing
# (gen_all.py, восемь копий каждой формы), стенд tb_vm3.v с ответом памяти
# через RDLY тактов CLC.
#
#   py measure.py build                 - собрать стенд (нужен CPU11 рядом)
#   py measure.py run 2 4 6             - прогнать все части при RDLY = 2, 4, 6
#   py measure.py table 2 4 6           - время форм: такты при каждом RDLY
#
# Время команды - период её копий: от выборки кода одной копии до выборки
# кода следующей (поток flow из json генератора), медиана по копиям кроме
# первой и последней. Команды между копиями (FILL_FORMS) вычитаются. Подробно -
# README.md.
import sys, os, json, glob, subprocess, concurrent.futures, statistics

TRAP = 0o057700          # обработчик ловушек стенда (RTI)
HERE = os.path.dirname(os.path.abspath(__file__))
CPU11 = os.environ.get('CPU11', 'CPU11')
IVERILOG = os.environ.get('IVERILOG', r'C:\iverilog\bin')


def build():
    h = os.path.join(CPU11, 'vm3', 'hdl')
    src = [os.path.join(HERE, 'tb_vm3.v'),
           os.path.join(h, 'syn', 'rtl', 'vm3.v'), os.path.join(h, 'syn', 'rtl', 'vm3_qbus.v'),
           os.path.join(h, 'wbc', 'rtl', 'vm3_plm.v'), os.path.join(h, 'wbc', 'rtl', 'vm3_mmu.v')]
    subprocess.check_call([os.path.join(IVERILOG, 'iverilog'), '-g2005', '-o', 'tb_vm3.vvp'] + src)


def run_one(args):
    mem, rdly = args
    out = 'log3_%s_r%d.txt' % (mem[:-4], rdly)
    with open(out, 'w') as fh:
        subprocess.call([os.path.join(IVERILOG, 'vvp'), '-n', 'tb_vm3.vvp', '+mem=' + mem,
                         '+vm2start', '+RDLY=%d' % rdly, '+LIMIT=20000000'], stdout=fh)
    return out


def run(rdlys):
    mems = sorted(glob.glob('all*.mem'))
    jobs = [(m, r) for r in rdlys for m in mems]
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count()) as ex:
        for out in ex.map(run_one, jobs):
            tail = open(out).read().strip().splitlines()[-2:]
            print(out, tail[0] if tail else '')


def fetch_times(log):
    ev = []
    for line in open(log):
        p = line.split()
        if p and p[0] == 'S':
            ev.append((int(p[1]), int(p[2], 8)))
    return ev


def form_times(part, rdly):
    cases = json.load(open('%s.json' % part))
    mem = [int(l, 16) for l in open('%s.mem' % part)]
    ev = fetch_times('log3_%s_r%d.txt' % (part, rdly))
    res = {}
    pos = 0
    for c in cases:
        flow = c['flow'] + [c['end']]
        start = pos
        t = []
        for a in flow:
            while pos < len(ev) and ev[pos][1] != a:
                pos += 1
            if pos >= len(ev):
                break
            t.append(ev[pos][0])
            pos += 1
        # конец части - начало следующей: следующей она и достаётся
        pos -= 1
        if len(t) < len(flow):
            res[c['name']] = None
            continue
        if any(a == TRAP for tt, a in ev[start:pos]):
            res[c['name']] = None
            continue
        # позиции копий в потоке
        dts = []
        fi = 0
        positions = []
        for a in c['copies']:
            while flow[fi] != a:
                fi += 1
            positions.append(fi)
            fi += 1
        for p, q in zip(positions[1:-1], positions[2:]):
            dts.append(t[q] - t[p])
        # команды между копиями (у DIV - перезарядка делимого и SEC)
        fill = tuple(mem[a >> 1] for a in flow[positions[1] + 1:positions[2]])
        res[c['name']] = (statistics.median(dts), fill) if dts else None
    return res


# Команды между копиями, время которых вычитается из периода копий: ВМ3
# выбирает следующее слово заранее, и от выборки DIV до выборки следующей за
# ним MOV проходит не время DIV, а меньше - остаток уходит на MOV
FILL_FORMS = {0o012704: 'MOV #n,R', 0o012705: 'MOV #n,R', 0o000261: 'SEC'}


def fill_corrected(allres, r):
    out = {}
    for k, v in allres.items():
        if v.get(r) is None:
            out[k] = None
            continue
        period, fill = v[r]
        if fill and all(w in FILL_FORMS for w in fill):
            period -= sum(allres[FILL_FORMS[w]][r][0] for w in fill)
        elif fill:
            period = None       # MARK, EMT+RTI: в таблицу не идут
        out[k] = period
    return out


def table(rdlys):
    parts = sorted(p[:-5] for p in glob.glob('all*.json'))
    allres = {}
    for r in rdlys:
        for part in parts:
            for k, v in form_times(part, r).items():
                allres.setdefault(k, {})[r] = v
    fixed = {r: fill_corrected(allres, r) for r in rdlys}
    allres = {k: {r: fixed[r][k] for r in rdlys} for k in allres}
    json.dump(allres, open('vm3_forms.json', 'w'), indent=0, ensure_ascii=False)
    for k, v in allres.items():
        print('%-24s %s' % (k, ' '.join('%5s' % v.get(r) for r in rdlys)))


if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'build':
        build()
    elif cmd == 'run':
        run([int(x) for x in sys.argv[2:]])
    elif cmd == 'table':
        table([int(x) for x in sys.argv[2:]])
