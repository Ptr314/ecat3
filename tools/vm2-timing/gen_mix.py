# Независимая проверка: программы из случайных разных команд, каких в
# подборе не было подряд. Каждая - один «случай» с потоком выполнения, как у
# gen_all.py: mix<N>.mem и mix<N>.json.
#
#   gen_mix.py [программ] [команд] [зерно] [имя]
import json, random, sys
import gen_all as g

n_prog = int(sys.argv[1]) if len(sys.argv) > 1 else 8
n_ins = int(sys.argv[2]) if len(sys.argv) > 2 else 300
rnd = random.Random(int(sys.argv[3]) if len(sys.argv) > 3 else 1)
name = sys.argv[4] if len(sys.argv) > 4 else 'mix'

# формы, которым не нужна своя подготовка (таблицы, векторы, делимое)
pool = [f for f in g.forms() if f['kind'] in ('double', 'single', 'xor', 'branch', 'word')
        or (f['kind'] == 'eis' and f['eis'] in ('MUL', 'ASH', 'ASHC'))]
# MTPS меняет приоритет, SXT и MFPS безобидны; MTPS убран, чтобы не трогать PSW
pool = [f for f in pool if not f['name'].startswith('MTPS')]

for k in range(n_prog):
    p = g.Prog()
    p.flow = []
    p.ins(0o005037, 0o177702)
    for r, v in g.REGS.items(): p.ins(0o012700 | r, v)
    p.ins(0o012706, 0o077000)
    p.ins(0o000261)
    starts = []
    for i in range(n_ins):
        # указатели - заново каждые 8 команд, чтобы не уйти из данных
        if i % 8 == 0:
            p.ins(0o012701, g.REGS[1]); p.ins(0o012702, g.REGS[2])
        f = dict(rnd.choice(pool))
        starts.append(p.a)
        before = len(p.flow)
        g.build_one(p, f)
    p.ins(0o012737, 1, 0o177700)
    p.w(0o000777)
    p.mem[0] = 0o000100; p.mem[1] = 0
    case = dict(name='mix %d' % k, kind='mix', start=0o100, end=p.a,
                copies=starts, flow=list(p.flow))
    json.dump([case], open('%s%d.json' % (name, k), 'w'))
    with open('%s%d.mem' % (name, k), 'w') as fh:
        for w in p.mem: fh.write('%04x\n' % w)
print(n_prog, 'программ по', n_ins, 'команд,', len(pool), 'форм в выборке')
