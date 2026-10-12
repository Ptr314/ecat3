# быстрая сетка по представительному набору форм: gsub.py cpu|ppu key=v1,v2 ...
import sys, itertools, sim, tests
SUB = ['NOP', 'MOV R0,R0', 'MOV (R0),R1', 'MOV R1,(R0)', 'MOV (R0),(R0)', 'MOV nnnn(R0),R1',
       'MOV R1,nnnn(R0)', 'MOV @nnnn(R0),R1', 'CLR (R0)', 'INC (R0)', 'MOVB R1,(R0)', 'CLRB (R0)',
       'BIC R1,(R0)', 'JMP (R2); ADD R1,R2', 'JMP (PC)']
which = 1 if sys.argv[1] == 'cpu' else 2
forms = {n: tests.TSSPD[n] for n in SUB}
base = dict(clc=125 if which == 1 else 160, mclk=80, slots=1, mask=1, rd=0, wr=0)
axes = [(a.split('=')[0], a.split('=')[1].split(',')) for a in sys.argv[2:]]
for combo in itertools.product(*[v for _, v in axes]):
    plus = dict(base)
    for (k, _), v in zip(axes, combo): plus[k] = v
    r = sim.run({n: v[0] for n, v in forms.items()}, plus, int(plus.pop("k1", 8)), int(plus.pop("k2", 40)))
    err = {n: (r[n] - v[which]) if r[n] is not None else 999 for n, v in forms.items()}
    mae = sum(abs(e) for e in err.values()) / len(err)
    print(' '.join('%s=%s' % (k, v) for (k, _), v in zip(axes, combo)), 'mae %.2f' % mae,
          ' '.join('%+.1f' % e for e in err.values()), flush=True)
